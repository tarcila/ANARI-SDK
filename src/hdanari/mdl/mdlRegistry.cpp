// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlRegistry.h"

#include <mi/base/enums.h>
#include <mi/base/handle.h>
#include <mi/base/ilogger.h>
#include <mi/neuraylib/factory.h>
#include <mi/neuraylib/iarray.h>
#include <mi/neuraylib/idatabase.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/ilogging_configuration.h>
#include <mi/neuraylib/imaterial_instance.h>
#include <mi/neuraylib/imdl_backend.h>
#include <mi/neuraylib/imdl_backend_api.h>
#include <mi/neuraylib/imdl_configuration.h>
#include <mi/neuraylib/imdl_execution_context.h>
#include <mi/neuraylib/imdl_factory.h>
#include <mi/neuraylib/imdl_impexp_api.h>
#include <mi/neuraylib/imodule.h>
#include <mi/neuraylib/ineuray.h>
#include <mi/neuraylib/iplugin_api.h>
#include <mi/neuraylib/iplugin_configuration.h>
#include <mi/neuraylib/iscene_element.h>
#include <mi/neuraylib/iscope.h>
#include <mi/neuraylib/istring.h>
#include <mi/neuraylib/itransaction.h>
#include <mi/neuraylib/itype.h>
#include <mi/neuraylib/ivalue.h>
#include <mi/neuraylib/iversion.h>

#include <pxr/base/tf/instantiateSingleton.h>
#include <pxr/base/tf/scoped.h>
#include <pxr/base/tf/singleton.h>
#include <pxr/pxr.h>

#ifdef MI_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <array>
#include <stdexcept>

using namespace std::string_literals;
using mi::base::make_handle;

#ifdef MI_PLATFORM_WINDOWS
#define loadLibrary(s) LoadLibrary(s)
#define freeLibrary(l) FreeLibrary(l)
#define getProcAddress(l, s) GetProcAddress(l, s)
#else
#define loadLibrary(s) dlopen(s, RTLD_LAZY)
#define freeLibrary(l) dlclose(l)
#define getProcAddress(l, s) dlsym(l, s)
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(HdAnariMdlRegistry);

HdAnariMdlRegistry* HdAnariMdlRegistry::GetInstance() {
  try {
    return &TfSingleton<HdAnariMdlRegistry>::GetInstance();
  } catch (const std::exception& e) {
    TF_RUNTIME_ERROR("Failed to create MdlRegistry singleton.");
    return nullptr;
  }
}

HdAnariMdlRegistry::HdAnariMdlRegistry() : HdAnariMdlRegistry({}, {}) {}

HdAnariMdlRegistry::HdAnariMdlRegistry(mi::base::ILogger* logger) : HdAnariMdlRegistry({}, logger) {}

HdAnariMdlRegistry::HdAnariMdlRegistry(mi::neuraylib::INeuray* neuray) : HdAnariMdlRegistry(neuray, {}) {}

HdAnariMdlRegistry::HdAnariMdlRegistry(mi::neuraylib::INeuray *neuray, mi::base::ILogger* logger)
{
  if (neuray && logger) {
    throw std::runtime_error("Only one of neuray or logger can be provided to libmdl::Core");
  }

  static constexpr const auto filename = "libmdl_sdk" MI_BASE_DLL_FILE_EXT;
  bool success = false;

  TfScoped handleCleanup([this, &success]() {
    if (success) return;

    m_executionContext = {};
    m_globalScope = {};
    m_neuray = {};

    if (m_dllHandle)
      freeLibrary(m_dllHandle);
    m_dllHandle = {};
  });

  if (neuray) {
    m_neuray = mi::base::make_handle_dup(neuray);
    m_dllHandle = {};
  } else {
    // Load library
    m_dllHandle = loadLibrary(filename);

    if (m_dllHandle == nullptr)
      throw std::runtime_error("Failed to load MDL SDK library "s + filename);

    // Get neuray main entry point
    void *symbol = getProcAddress(m_dllHandle, "mi_factory");
    if (symbol == nullptr)
      throw std::runtime_error("Failed to find MDL SDK mi_factory symbol");

    m_neuray = mi::neuraylib::mi_factory<mi::neuraylib::INeuray>(symbol);
    if (m_neuray == nullptr) {
      // Check if we have a valid neuray instance, otherwise check why.
      auto version = make_handle(
          mi::neuraylib::mi_factory<mi::neuraylib::IVersion>(symbol));
      if (!version) {
        throw std::runtime_error("Cannot get MDL SDK library version");
      } else {
        throw std::runtime_error(
            "Cannot get INeuray interface from mi_factory, either there is a version mismatch or the interface has already been acquired: "s
            "Expected version is " MI_NEURAYLIB_PRODUCT_VERSION_STRING
            ", library version is "
            + version->get_product_version());
      }
    }

    // Get the MDL configuration component so main path can be added.
    auto mdlConfiguration = make_handle(
        m_neuray->get_api_component<mi::neuraylib::IMdl_configuration>());
    mdlConfiguration->add_mdl_system_paths();
    mdlConfiguration->add_mdl_user_paths();

    auto loggingConfig = make_handle(m_neuray->get_api_component<mi::neuraylib::ILogging_configuration>());
    if (logger) {
      loggingConfig->set_receiving_logger(logger);
    } else {
      logger = loggingConfig->get_receiving_logger();
    }

    m_logger = logger;

    auto pluginConf = make_handle(
        m_neuray->get_api_component<mi::neuraylib::IPlugin_configuration>());
    if (mi::Sint32 res = pluginConf->load_plugin_library(
            "nv_openimageio" MI_BASE_DLL_FILE_EXT);
        res != 0) {
      logger->message(mi::base::MESSAGE_SEVERITY_WARNING,
          "plugins",
          "Failed to load the nv_openimageio plugin");
    }
    if (mi::Sint32 res =
            pluginConf->load_plugin_library("dds" MI_BASE_DLL_FILE_EXT);
        res != 0) {
      logger->message(mi::base::MESSAGE_SEVERITY_WARNING,
          "plugins",
          "Failed to load the dds plugin");
    }

    m_neuray->start();
  }
  
  // Get the global scope from the database
  auto database = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IDatabase>());
  if (!database.is_valid_interface())
    throw std::runtime_error("Failed to retrieve neuray database component");

  m_globalScope = make_handle(database->get_global_scope());
  if (!m_globalScope.is_valid_interface())
    throw std::runtime_error("Failed to acquire neuray database global scope");

  // Get an execution context for later use.
  m_mdlFactory = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IMdl_factory>());
  if (!m_mdlFactory.is_valid_interface()) {
    throw std::runtime_error("Failed to retrieve MDL factory component");
  }

  m_executionContext =
      make_handle(m_mdlFactory->create_execution_context());
  if (!m_executionContext.is_valid_interface()) {
    throw std::runtime_error("Failed acquiring an execution context");
  }

  success = true;
}

HdAnariMdlRegistry::~HdAnariMdlRegistry()
{
  m_executionContext = {};
  m_mdlFactory = {};
  m_globalScope = {};
  if (m_dllHandle) {
    m_neuray->shutdown();
    m_neuray = {};
    freeLibrary(m_dllHandle);
  }
  m_dllHandle = {};
}

auto HdAnariMdlRegistry::createScope(std::string_view scopeName, mi::neuraylib::IScope* parent) -> mi::neuraylib::IScope* {
  auto database = make_handle(m_neuray->get_api_component<mi::neuraylib::IDatabase>());

  return database->create_scope(parent);
}

auto HdAnariMdlRegistry::removeScope(mi::neuraylib::IScope* scope) -> void {
  auto database = make_handle(m_neuray->get_api_component<mi::neuraylib::IDatabase>());

  database->remove_scope(scope->get_id());
}

auto HdAnariMdlRegistry::createTransaction(
    mi::neuraylib::IScope *scope) -> mi::neuraylib::ITransaction *
{
  if (!scope)
    scope = m_globalScope.get();
  return scope->create_transaction();
}

auto HdAnariMdlRegistry::loadModule(std::string_view moduleOrFileName,
    mi::neuraylib::ITransaction *transaction) -> const mi::neuraylib::IModule *
{
  auto impexpApi = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IMdl_impexp_api>());

  auto moduleName = std::string(moduleOrFileName);

  // Check if this is a single MDL name, such as OmniPBR.mdl and
  // resolve it to its equivalent module name, such as ::OmniPBR.
  if (auto len = moduleName.length(); len > 4) {
    auto extension = moduleName.substr(len - 4);
    if (moduleName.find('/') == std::string::npos && extension == ".mdl") {
      moduleName = "::"s + moduleName.substr(0, len - 4);
    }
  }

  // Try and
  if (auto name = make_handle(
          impexpApi->get_mdl_module_name(moduleName.c_str()));
      name.is_valid_interface()) {
    moduleName = name->get_c_str();
  }

  if (impexpApi->load_module(
          transaction, moduleName.c_str(), m_executionContext.get())
      < 0)
    return {};

  // Get the database name for the module we loaded
  auto moduleDbName = make_handle(
      m_mdlFactory->get_db_module_name(std::string(moduleName).c_str()));
  return transaction->access<mi::neuraylib::IModule>(moduleDbName->get_c_str());
}

auto HdAnariMdlRegistry::getFunctionDefinition(const mi::neuraylib::IModule *module,
    std::string_view functionName,
    mi::neuraylib::ITransaction *transaction)
    -> const mi::neuraylib::IFunction_definition *
{
  std::string functionQualifiedName;
  if (functionName.back()
      == ')') // Already a full qualified function signature.
    functionQualifiedName = functionName;
  else { // Needs more work to get what we need.
    functionQualifiedName = functionName;
    auto overloads = make_handle(
        module->get_function_overloads(functionQualifiedName.c_str()));
    if (!overloads.is_valid_interface() || overloads->get_length() != 1)
      return {};

    auto firstOverload = make_handle(
        overloads->get_element<mi::IString>(static_cast<mi::Size>(0)));
    functionQualifiedName = firstOverload->get_c_str();
  }

  fprintf(stderr, "Getting function defintiion for %s\n", functionQualifiedName.c_str());

  return transaction->access<mi::neuraylib::IFunction_definition>(
      functionQualifiedName.c_str());
}

auto HdAnariMdlRegistry::getCompiledMaterial(
    const mi::neuraylib::IFunction_definition *functionDefinition,
    bool classCompilation)
    -> mi::neuraylib::ICompiled_material *
{
  mi::Sint32 ret = 0;
  auto functionCall =
      make_handle(functionDefinition->create_function_call(0, &ret));
  if (ret != 0)
    return {};

  auto executionContext =
      make_handle(m_mdlFactory->clone(m_executionContext.get()));

  auto materialInstance = make_handle(
      functionCall->get_interface<mi::neuraylib::IMaterial_instance>());
  auto compiledMaterial = materialInstance->create_compiled_material(
      classCompilation ? mi::neuraylib::IMaterial_instance::CLASS_COMPILATION
                       : mi::neuraylib::IMaterial_instance::DEFAULT_OPTIONS,
      executionContext.get());

  if (executionContext->get_messages_count() > 0) {
    fprintf(stderr, "Compilation log:\n");
    for (auto i = 0; i < executionContext->get_messages_count(); ++i) {
      auto message = executionContext->get_message(i);
      fprintf(stderr, "  %s\n", message->get_string());
    }
  }

  if (executionContext->get_error_messages_count() > 0) {
    fprintf(stderr, "Compilation error log:\n");
    for (auto i = 0; i < executionContext->get_error_messages_count(); ++i) {
      auto message = executionContext->get_error_message(i);
      fprintf(stderr, "  %s\n", message->get_string());
    }
  }

  return compiledMaterial;
}

auto HdAnariMdlRegistry::getPtxTargetCode(
    const mi::neuraylib::ICompiled_material *compiledMaterial,
    mi::neuraylib::ITransaction *transaction)
    -> const mi::neuraylib::ITarget_code *
{
  auto backendApi = make_handle(m_neuray->get_api_component<mi::neuraylib::IMdl_backend_api>());

  auto ptxBackend = make_handle(
      backendApi->get_backend(mi::neuraylib::IMdl_backend_api::MB_CUDA_PTX));
  auto executionContext =
      make_handle(m_mdlFactory->clone(m_executionContext.get()));
  
  // FIXME: Do we actually need to do this? Most probably best not to
  // load until proven we need that.
  executionContext->set_option("resolve_resources", false);
  ptxBackend->set_option("resolve_resources", "0");

  // ANARI attributes 0 to 3
  const int numTextureSpaces = 4; 
  // Number of actually supported textures. MDL's default, let's assume this is enough for now
  const int numTextureResults = 32;

  ptxBackend->set_option("num_texture_spaces", std::to_string(numTextureSpaces).c_str());
  ptxBackend->set_option("num_texture_results", std::to_string(numTextureResults).c_str());
  ptxBackend->set_option_binary("llvm_renderer_module", nullptr, 0);
  ptxBackend->set_option("visible_functions", "");

  ptxBackend->set_option("sm_version", "52");
  ptxBackend->set_option("tex_lookup_call_mode", "direct_call");
  ptxBackend->set_option("lambda_return_mode", "value");
  ptxBackend->set_option("texture_runtime_with_derivs", "off");
  ptxBackend->set_option("inline_aggressively", "on");
  ptxBackend->set_option("opt_level", "2");
  ptxBackend->set_option("enable_exceptions", "off");

  // For now, only consider surface scattering.
  static std::array<mi::neuraylib::Target_function_description, 1> descs{
      {{"surface.scattering", "mdlBsdf"}},
  };

  // Generate target code for the compiled material
  mi::base::Handle<mi::neuraylib::ILink_unit> linkUnit(
      ptxBackend->create_link_unit(transaction, executionContext.get()));
  linkUnit->add_material(
      compiledMaterial, data(descs), size(descs), executionContext.get());

  if (!logExecutionContextMessages(executionContext.get()))
    return {};

  auto targetCode = ptxBackend->translate_link_unit(linkUnit.get(), executionContext.get());
  if (!logExecutionContextMessages(executionContext.get())) {
    return {};
  }

  return targetCode;
}

auto HdAnariMdlRegistry::loadTexture(std::string_view filePath, mi::neuraylib::ITransaction* transaction) -> mi::neuraylib::IValue_texture*
{
  auto vf = make_handle(m_mdlFactory->create_value_factory(transaction));
  auto tf = make_handle(vf->get_type_factory());

  auto textureType = make_handle(tf->create_texture(mi::neuraylib::IType_texture::TS_2D));
  auto textureValue = vf->create_texture(textureType.get(), std::string(filePath).c_str());

  return textureValue;
}

auto HdAnariMdlRegistry::logExecutionContextMessages(const mi::neuraylib::IMdl_execution_context* executionContext) -> bool {
  for (auto i = 0ull, messageCount = executionContext->get_messages_count(); i < messageCount; ++i) {
    auto message = make_handle(executionContext->get_message(i));
    m_logger->message(message->get_severity(), "misc", message->get_string());
  }

  for (auto i = 0ull, messageCount = executionContext->get_error_messages_count(); i < messageCount; ++i) {
    auto message = make_handle(executionContext->get_error_message(i));
    m_logger->message(message->get_severity(), "misc", message->get_string());
  }

  return executionContext->get_error_messages_count() == 0;
}

PXR_NAMESPACE_CLOSE_SCOPE

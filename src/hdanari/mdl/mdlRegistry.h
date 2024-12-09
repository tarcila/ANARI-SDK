// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "api.h"

#include <mi/base/handle.h>
#include <mi/base/ilogger.h>
#include <mi/base/uuid.h>
#include <mi/neuraylib/icompiled_material.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/iimage.h>
#include <mi/neuraylib/imdl_backend.h>
#include <mi/neuraylib/imdl_backend_api.h>
#include <mi/neuraylib/imdl_compiler.h>
#include <mi/neuraylib/imdl_execution_context.h>
#include <mi/neuraylib/imdl_factory.h>
#include <mi/neuraylib/imodule.h>
#include <mi/neuraylib/ineuray.h>
#include <mi/neuraylib/iscope.h>
#include <mi/neuraylib/itexture.h>
#include <mi/neuraylib/itransaction.h>
#include <mi/neuraylib/target_code_types.h>

#include <pxr/base/tf/singleton.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class HDANARI_MDL_API HdAnariMdlRegistry {
    friend class TfSingleton<HdAnariMdlRegistry>;
public:
    HDANARI_MDL_API static HdAnariMdlRegistry* GetInstance();


    // The main neuray interface can only be acquired once. Possibly get it
    // as a parameter instead of allocating it internally.
    // Note that we allow overriding the logger only if we own the
    // neuray instance, otherwise we assume logging is already taken care of
    HdAnariMdlRegistry();
    HdAnariMdlRegistry(mi::base::ILogger* logger);
    HdAnariMdlRegistry(mi::neuraylib::INeuray* neuray);

    ~HdAnariMdlRegistry();

    // The main neuray interface can only be acquired once. Make sure it can be
    // shared if taken from there. The original subsystem keeps the ownership of
    // the returned value.
    auto getINeuray() const -> mi::neuraylib::INeuray*
    {
        return m_neuray.get();
    }

    auto getMdlFactory() const -> mi::neuraylib::IMdl_factory* {
        return m_mdlFactory.get();
    }

    auto createScope(std::string_view scopeName, mi::neuraylib::IScope* parent = {}) -> mi::neuraylib::IScope*;
    auto removeScope(mi::neuraylib::IScope* scope) -> void;
    auto createTransaction(mi::neuraylib::IScope* scope = {}) -> mi::neuraylib::ITransaction*;

    auto loadModule(std::string_view moduleOrFileName, mi::neuraylib::ITransaction* transaction) -> const mi::neuraylib::IModule*;

    auto getFunctionDefinition(const mi::neuraylib::IModule* module, std::string_view functionName, mi::neuraylib::ITransaction* transaction)
        -> const mi::neuraylib::IFunction_definition*;

    auto getCompiledMaterial(const mi::neuraylib::IFunction_definition*, bool classCompilation = true) -> mi::neuraylib::ICompiled_material*;
    auto getPtxTargetCode(const mi::neuraylib::ICompiled_material* compiledMaterial, mi::neuraylib::ITransaction* transaction) -> const mi::neuraylib::ITarget_code*;

    auto loadTexture(std::string_view filePath, mi::neuraylib::ITransaction* transaction) -> mi::neuraylib::IValue_texture*;

private:
    HdAnariMdlRegistry(mi::neuraylib::INeuray* neuray, mi::base::ILogger* logger);

#ifdef MI_PLATFORM_WINDOWS
    using DllHandle = HANDLE;
#else
    using DllHandle = void*;
#endif
    DllHandle m_dllHandle;
    mi::base::Handle<mi::neuraylib::INeuray> m_neuray;
    mi::base::Handle<mi::neuraylib::IScope> m_globalScope;
    mi::base::Handle<mi::neuraylib::IMdl_factory> m_mdlFactory;
    mi::base::Handle<mi::neuraylib::IMdl_execution_context> m_executionContext;
    mi::base::Handle<mi::base::ILogger> m_logger;

    auto logExecutionContextMessages(const mi::neuraylib::IMdl_execution_context* executionContext) -> bool;
};

PXR_NAMESPACE_CLOSE_SCOPE

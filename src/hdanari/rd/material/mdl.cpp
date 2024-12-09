#include "mdl.h"

#include <anari/anari_cpp.hpp>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <iostream>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;

PXR_NAMESPACE_OPEN_SCOPE

anari::Material HdAnariMdlMaterial::CreateMaterial(anari::Device device, const HdMaterialNetwork2Interface &materialNetworkIface) {
    return anari::newObject<anari::Material>(device, "mdl");
}

void HdAnariMdlMaterial::SyncMaterialParameters(
    anari::Device device,
    anari::Material material,
    const HdMaterialNetwork2Interface &materialNetworkIface,
    const HdAnariMaterial::PrimvarBinding &primvarBinding,
    const HdAnariMaterial::PrimvarMapping &primvarMapping,
    const HdAnariMaterial::SamplerMapping &samplerMapping)
{
  auto con = materialNetworkIface.GetTerminalConnection(
      HdMaterialTerminalTokens->surface);
  if (con.first) {
    TfToken terminalNode = con.second.upstreamNodeName;

    ProcessMdlNode(device, material, materialNetworkIface, terminalNode, primvarBinding, primvarMapping, samplerMapping);
  } else {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s",
        materialNetworkIface.GetMaterialPrimPath().GetText());
  }
}

void HdAnariMdlMaterial::ProcessMdlNode(anari::Device device,
    anari::Material material,
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken terminal,
    const HdAnariMaterial::PrimvarBinding &primvarBinding,
    const HdAnariMaterial::PrimvarMapping &primvarMapping,
    const HdAnariMaterial::SamplerMapping &samplerMapping)
{
    auto shaderNode = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(materialNetworkIface.GetNodeType(terminal));
    auto uri = shaderNode->GetResolvedImplementationURI();
    
    anari::setParameter(device, material, "source", uri);
    anari::setParameter(device, material, "sourceType", "module");

    std::cout << "<<< MATERIAL PARAMETERS" << std::endl;
    for (auto name: materialNetworkIface.GetAuthoredNodeParameterNames(terminal)) {
        std::cout << "Processing " << name.GetString() << std::endl;
        auto value = materialNetworkIface.GetNodeParameterValue(terminal, name);
        if (value.IsHolding<bool>()) {
            anari::setParameter(device, material, name.GetText(), value.UncheckedGet<bool>());
        } else if (value.IsHolding<int>()) {
            anari::setParameter(device, material, name.GetText(), value.UncheckedGet<int>());
        } else if (value.IsHolding<float>()) {
            anari::setParameter(device, material, name.GetText(), value.UncheckedGet<float>());
        } else if (value.IsHolding<TfToken>()) {
            auto s = value.UncheckedGet<TfToken>().GetString();
            static constexpr const auto colorSpacePrefix = "colorSpace:"sv;
            if (s.substr(0, size(colorSpacePrefix)) == colorSpacePrefix) {
                s = s.substr(size(colorSpacePrefix)) + ".colorspace"s;
            }
            anari::setParameter(device, material, name.GetText(), s);
        } else if (value.IsHolding<SdfAssetPath>()) {
            auto s = value.UncheckedGet<SdfAssetPath>().GetResolvedPath();
            fprintf(stderr, " Setting %s to resolved %s (from %s)\n", name.GetText(), s.c_str(), value.UncheckedGet<SdfAssetPath>().GetAssetPath().c_str());
          // FIXME: Workaround dds for now...
          
          if (auto p = s.find(".dds"sv); p != std::string::npos) {
            s.replace(p, 4, ".png");
            fprintf(stderr, " We hit a .dds, try and find a .png instead\n");
          }

          anari::setParameter(device, material, name.GetText(), s);
        } else {
            std::cout << "  Don't know how to handle " << name.GetString() << " of type " << 
                value.GetTypeName() << std::endl;
        }

    }
    std::cout << ">>> MATERIAL PARAMETERS" << std::endl;
}

PXR_NAMESPACE_CLOSE_SCOPE

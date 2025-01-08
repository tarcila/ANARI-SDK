#include "mdl.h"

#include <anari/anari_cpp.hpp>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/pxr.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4i.h>
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

    std::cout << "<<< MATERIAL PARAMETERS for " << materialNetworkIface.GetMaterialPrimPath().GetString() << std::endl;
    fprintf(stderr, "  anari material pointer %p\n", material);
    for (auto name: materialNetworkIface.GetAuthoredNodeParameterNames(terminal)) {
        auto nodeName = terminal;
        auto inputName = name;

        std::cout << "Processing " << nodeName.GetString() << ":" << inputName.GetString() << std::endl;

        auto cnxs = materialNetworkIface.GetNodeInputConnection(terminal, name);
        if (cnxs.size()) {
            auto cnx = cnxs.front();
            nodeName = cnx.upstreamNodeName;
            inputName = cnx.upstreamOutputName;
            std::cout << "  connected to " << nodeName.GetString() << ":" << inputName.GetString() << std::endl;
        }

        auto value = materialNetworkIface.GetNodeParameterValue(nodeName, inputName);
        if (value.IsHolding<bool>()) {
            anari::setParameter(device, material, name.GetText(), value.UncheckedGet<bool>());
        } else if (value.IsHolding<int>()) {
            anari::setParameter(device, material, name.GetText(), value.UncheckedGet<int>());
        } else if (value.IsHolding<GfVec2i>()) {
            auto v = value.UncheckedGet<GfVec2i>();
            anari::setParameter<int[2]>(device, material, name.GetText(), {v[0], v[1]});
        } else if (value.IsHolding<GfVec3i>()) {
            auto v = value.UncheckedGet<GfVec3i>();
            anari::setParameter<int[3]>(device, material, name.GetText(), {v[0], v[1], v[2]});
        } else if (value.IsHolding<GfVec4i>()) {
            auto v = value.UncheckedGet<GfVec4i>();
            anari::setParameter<int[4]>(device, material, name.GetText(), {v[0], v[1], v[2], v[3]});
        } else if (value.IsHolding<float>()) {
            anari::setParameter(device, material, name.GetText(), value.UncheckedGet<float>());
        } else if (value.IsHolding<GfVec2f>()) {
            auto v = value.UncheckedGet<GfVec2f>();
            anari::setParameter<float[2]>(device, material, name.GetText(), {v[0], v[1]});
        } else if (value.IsHolding<GfVec3f>()) {
            auto v = value.UncheckedGet<GfVec3f>();
            anari::setParameter<float[3]>(device, material, name.GetText(), {v[0], v[1], v[2]});
        } else if (value.IsHolding<GfVec4f>()) {
            auto v = value.UncheckedGet<GfVec4f>();
            anari::setParameter<float[4]>(device, material, name.GetText(), {v[0], v[1], v[2], v[3]});
        } else if (value.IsHolding<TfToken>()) {
            auto s = value.UncheckedGet<TfToken>().GetString();
            static constexpr const auto colorSpacePrefix = "colorSpace:"sv;
            if (s.substr(0, size(colorSpacePrefix)) == colorSpacePrefix) {
                s = s.substr(size(colorSpacePrefix)) + ".colorspace"s;
            }
            anari::setParameter(device, material, name.GetText(), s);
        } else if (value.IsHolding<SdfAssetPath>()) {
            auto s = value.UncheckedGet<SdfAssetPath>().GetResolvedPath();
            if (s.empty()) {
                s = value.UncheckedGet<SdfAssetPath>().GetAssetPath();
            }
            if (s.empty()) {
                fprintf(stderr, "Skipping empty texture for %s\n", name.GetText());
            } else {
                fprintf(stderr, " Setting %s to resolved %s (from %s)\n", name.GetText(), s.c_str(), value.UncheckedGet<SdfAssetPath>().GetAssetPath().c_str());
                // FIXME: Workaround dds for now...
                if (auto p = s.find(".dds"sv); p != std::string::npos) {
                    fprintf(stderr, " We hit a .dds, try and find a .png instead\n");
                    s.replace(p, 4, ".png");
                }
                anari::setParameter(device, material, name.GetText(), s);
            }
        } else {
            std::cout << "  Don't know how to handle " << name.GetString() << " of type " << 
                value.GetTypeName() << std::endl;
        }

    }
    std::cout << ">>> MATERIAL PARAMETERS" << std::endl;
}

HdAnariMaterial::PrimvarMapping
HdAnariMdlMaterial::EnumeratePrimvars(
    const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal)
{
  auto con = materialNetworkIface.GetTerminalConnection(terminal);
  if (!con.first) {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s",
        materialNetworkIface.GetMaterialPrimPath().GetText());
    return {};
  }

  // Fake it so we are mapping primvars:st on the mesh to attribute0.
  // FIXME: Find out how to map more than one texture coordinate. Maybe by checking for
  // some primvar role if any?
  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);

  return {{materialNetworkIface.GetMaterialPrimPath(), TfToken("st")}};
}

PXR_NAMESPACE_CLOSE_SCOPE

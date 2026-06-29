// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "materialx.h"

#include "rd/debugCodes.h"

#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdMtlx/hdMtlx.h>
#include <pxr/usd/sdf/path.h>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>

#include <anari/anari_cpp.hpp>

#include <string>

namespace mx = MaterialX;

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Convert the Hydra MaterialX network to an inline .mtlx document string.
// `texData` (optional) collects the MaterialX<->Hydra texture/primvar mapping
// the sampler binding (Task 4) needs.
std::string BuildInlineMtlxDocument(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    HdMtlxTexturePrimvarData *texData)
{
  // The create function may mutate the interface, so it takes a non-const
  // pointer; the getters below are const.
  auto *iface =
      const_cast<HdMaterialNetwork2Interface *>(&materialNetworkIface);

  TfToken terminalNodeName =
      iface->GetTerminalConnection(HdMaterialTerminalTokens->surface)
          .second.upstreamNodeName;

  // The 3rd arg is the terminal shader's INPUT connection names (base_color,
  // specular_roughness, normal, ...), NOT the {surface} role. Passing the role
  // yields a degenerate document with every wired input dropped.
  TfTokenVector connectionNames =
      iface->GetNodeInputConnectionNames(terminalNodeName);

  mx::DocumentPtr doc = HdMtlxCreateMtlxDocumentFromHdMaterialNetworkInterface(
      iface, terminalNodeName, connectionNames, HdMtlxStdLibraries(), texData);

  // Skip elements that came from the standard libraries so the inline document
  // carries only the material's own graph (matches USD's own callers).
  mx::XmlWriteOptions opts;
  opts.elementPredicate = [](mx::ConstElementPtr e) {
    return !e->hasSourceUri();
  };
  return mx::writeToXmlString(doc, &opts);
}

} // namespace

anari::Material HdAnariMaterialXMaterial::CreateMaterial(anari::Device device)
{
  return anari::newObject<anari::Material>(device, "materialx");
}

std::string HdAnariMaterialXMaterial::ComputeContentKey(
    const HdMaterialNetwork2Interface &materialNetworkIface)
{
  // The serialized document is itself a path-independent content identifier.
  return BuildInlineMtlxDocument(materialNetworkIface, nullptr);
}

HdAnariMaterial::PrimvarMapping HdAnariMaterialXMaterial::EnumeratePrimvars(
    const HdMaterialNetwork2Interface &materialNetworkIface, TfToken)
{
  // Map the mesh's `st` to attribute0, matching the MDL backend. Refined later
  // if MaterialX networks reference non-default texcoord sets.
  return {{materialNetworkIface.GetMaterialPrimPath(), TfToken("st")}};
}

HdAnariMaterial::TextureDescMapping HdAnariMaterialXMaterial::EnumerateTextures(
    const HdMaterialNetwork2Interface &, TfToken)
{
  return {};
}

HdAnariMaterial::SamplerMapping
HdAnariMaterialXMaterial::SyncMaterialParameters(anari::Device device,
    anari::Material material,
    const HdMaterialNetwork2Interface &materialNetworkIface,
    const HdAnariMaterial::PrimvarBinding &,
    const HdAnariMaterial::PrimvarMapping &)
{
  HdMtlxTexturePrimvarData texData;
  std::string xml = BuildInlineMtlxDocument(materialNetworkIface, &texData);

  TF_DEBUG_MSG(HD_ANARI_RD_MATERIAL,
      "MaterialX %s: %zu byte inline document, %zu texture node(s)\n",
      materialNetworkIface.GetMaterialPrimPath().GetText(),
      xml.size(),
      texData.hdTextureNodes.size());

  anari::setParameter(device, material, "sourceType", "documentInline");
  anari::setParameter(device, material, "source", xml);

  // Host-resolved sampler binding for wired textures lands in Task 4. The single
  // material commit happens in the caller (HdAnariMaterial::Sync).
  return {};
}

PXR_NAMESPACE_CLOSE_SCOPE

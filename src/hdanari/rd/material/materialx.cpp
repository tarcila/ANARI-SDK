// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "materialx.h"

#include "rd/debugCodes.h"
#include "rd/material/textureLoader.h"
#include "rd/materialTokens.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdMtlx/hdMtlx.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>

#include <anari/anari_cpp.hpp>

#include <map>
#include <set>
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

  // Clear baked image file paths: the host binds an ANARI sampler to each
  // texture argument, so the device must not try (and fail, then ERROR) to
  // resolve the document's asset paths. The `file` input stays (keeping the
  // generated texture_2d argument), only its value is dropped.
  for (mx::ElementPtr element : doc->traverseTree()) {
    mx::NodePtr node = element->asA<mx::Node>();
    if (!node)
      continue;
    const std::string &category = node->getCategory();
    if (category != "image" && category != "tiledimage")
      continue;
    if (mx::InputPtr fileInput = node->getInput("file"))
      fileInput->removeAttribute(mx::ValueElement::VALUE_ATTRIBUTE);
  }

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

HdAnariMaterial::PrimvarMapping HdAnariMaterialXMaterial::EnumeratePrimvars(
    const HdMaterialNetwork2Interface &materialNetworkIface, TfToken)
{
  // The generated MDL reads the default texture coordinate set as
  // state::texture_coordinate(0) == ANARI attribute0. Request `st`; geometry
  // resolves this to the mesh's actual texture-coordinate primvar when it is
  // named otherwise (e.g. Blender's `UVMap`). Named MaterialX geompropvalue
  // sets compile to MDL parameters (not texcoord reads) and need device support
  // -- see materialx.h.
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

  HdAnariMaterial::SamplerMapping samplers;
  if (texData.hdTextureNodes.empty())
    return samplers; // untextured: the single commit happens in the caller

  // The device exposes the bindable MaterialX paths only after a commit (it
  // parses the document then). Commit once for discovery, read back the exact
  // paths, and bind a host-resolved image2D sampler to each wired texture.
  // Using the device-reported paths is robust to however hdMtlx nests the
  // image node (top level vs a nodegraph). The final routing commit happens in
  // the caller (HdAnariMaterial::Sync).
  anari::commitParameters(device, material);

  const char **textureInputs = nullptr;
  if (!anari::getProperty(
          device, material, "textureInputs", textureInputs, ANARI_WAIT)
      || !textureInputs) {
    return samplers;
  }

  // hdMtlx names each MaterialX node with HdMtlxCreateNameFromPath(hdPath) --
  // the Hydra leaf in a standard build, the full '/'->'_' path under
  // PXR_DCC_LOCATION_ENV_VAR. Key on that same function so the match holds for
  // both. Two Hydra nodes can still collapse to one name (same leaf in
  // different nodegraphs); those are inherently unresolvable from the device
  // path alone, so record them and skip rather than misbind the wrong texture.
  std::map<std::string, SdfPath> hdNodeByMtlxName;
  std::set<std::string> ambiguousNames;
  for (const SdfPath &p : texData.hdTextureNodes) {
    const std::string name = HdMtlxCreateNameFromPath(p);
    if (!hdNodeByMtlxName.emplace(name, p).second)
      ambiguousNames.insert(name);
  }

  for (const char **it = textureInputs; *it; ++it) {
    const std::string origin = *it; // e.g. "img1/file" or "NG_xxx/img1/file"
    const auto inputSlash = origin.find_last_of('/');
    if (inputSlash == std::string::npos)
      continue;
    const std::string nodePath = origin.substr(0, inputSlash);
    const std::string nodeName = nodePath.substr(nodePath.find_last_of('/') + 1);

    if (ambiguousNames.count(nodeName)) {
      TF_WARN("MaterialX %s: texture input '%s' maps to an ambiguous node name "
              "'%s' (shared by multiple Hydra texture nodes); skipping",
          materialNetworkIface.GetMaterialPrimPath().GetText(),
          origin.c_str(),
          nodeName.c_str());
      continue;
    }

    const auto found = hdNodeByMtlxName.find(nodeName);
    if (found == hdNodeByMtlxName.end()) {
      TF_WARN("MaterialX %s: texture input '%s' has no matching Hydra node",
          materialNetworkIface.GetMaterialPrimPath().GetText(), origin.c_str());
      continue;
    }
    const SdfPath &hdTexPath = found->second;

    const VtValue fileValue = materialNetworkIface.GetNodeParameterValue(
        hdTexPath.GetAsToken(), HdAnariMaterialTokens->file);
    if (!fileValue.IsHolding<SdfAssetPath>())
      continue;
    const SdfAssetPath &assetPath = fileValue.UncheckedGet<SdfAssetPath>();
    std::string asset = assetPath.GetResolvedPath();
    if (asset.empty())
      asset = assetPath.GetAssetPath();
    if (asset.empty())
      continue;

    // Load raw: the MaterialX/MDL image node applies the document's colorspace,
    // so a hardware sRGB decode here would double-decode.
    auto array = HdAnariTextureLoader::LoadHioTexture2D(device,
        asset,
        HdAnariTextureLoader::MinMagFilter::Linear,
        HdAnariTextureLoader::ColorSpace::Raw);
    if (!array)
      continue;

    auto sampler = anari::newObject<anari::Sampler>(device, "image2D");
    anari::setAndReleaseParameter(device, sampler, "image", array);
    anari::setParameter(device, sampler, "inAttribute", "attribute0");
    anari::setParameter(device, sampler, "filter", "linear");
    anari::commitParameters(device, sampler);

    anari::setParameter(device, material, origin.c_str(), sampler);
    samplers[hdTexPath] = sampler;
  }

  return samplers;
}

PXR_NAMESPACE_CLOSE_SCOPE

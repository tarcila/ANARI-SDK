// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../material.h"

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/pxr.h>

#include <anari/anari_cpp.hpp>

PXR_NAMESPACE_OPEN_SCOPE

// Renders a Hydra MaterialX network by converting it to an inline .mtlx
// document (USD's hdMtlx) and handing it to a device `materialx` material via
// sourceType="documentInline". No MaterialX->MDL transcoding happens here; the
// device does that. Wired textures are bound as host-resolved ANARI samplers by
// their MaterialX path (the device's `textureInputs` property).
//
// Limitations (v1):
//  - image2D samplers only; the sampler supplies texels, while the document's
//    MDL image node drives the UV (texcoord set 0), wrap, and filter -- the
//    sampler's own UV source / wrap / transform are bypassed.
//  - Texture colorspace is carried in the document by hdMtlx and decoded by the
//    generated MDL, so samplers are loaded Raw (no double sRGB decode).
//  - Normal maps are not yet supported: triangle geometry currently exposes no
//    tangents for the MDL normal-mapping path.
struct HdAnariMaterialXMaterial final
{
  static anari::Material CreateMaterial(anari::Device device);

  static HdAnariMaterial::PrimvarMapping EnumeratePrimvars(
      const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal);

  // Always empty: MaterialX textures are bound as samplers inside
  // SyncMaterialParameters, not through the generic TextureDesc path.
  static HdAnariMaterial::TextureDescMapping EnumerateTextures(
      const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal);

  // Builds the inline .mtlx document, sets sourceType/source on the material,
  // and (Task 4) binds host-resolved samplers by MaterialX path. Returns the
  // samplers it created so the owning prim releases them. The single material
  // commit happens in the caller (HdAnariMaterial::Sync).
  static HdAnariMaterial::SamplerMapping SyncMaterialParameters(
      anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface &materialNetworkIface,
      const HdAnariMaterial::PrimvarBinding &primvarBinding,
      const HdAnariMaterial::PrimvarMapping &primvarMapping);
};

PXR_NAMESPACE_CLOSE_SCOPE

// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../material.h"

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/pxr.h>

#include <anari/anari_cpp.hpp>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

// Renders a Hydra MaterialX network by converting it to an inline .mtlx
// document (USD's hdMtlx) and handing it to a device `materialx` material via
// sourceType="documentInline". No MaterialX->MDL transcoding happens here; the
// device does that. Textures are bound as host-resolved ANARI samplers by their
// MaterialX path (Task 4).
struct HdAnariMaterialXMaterial final
{
  static anari::Material CreateMaterial(anari::Device device);

  // Path-independent content key (the serialized document). Scaffolding for a
  // future shared-material cache; unused while materials are prim-owned.
  static std::string ComputeContentKey(
      const HdMaterialNetwork2Interface &materialNetworkIface);

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

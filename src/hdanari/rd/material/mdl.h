// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../material.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMdlMaterial final
{
  static anari::Material GetOrCreateMaterial(anari::Device device,
      HdSceneDelegate *sceneDelegate,
      HdAnariMaterial* materialPrim);

  static void ProcessUsdPreviewSurfaceNode(anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken terminal,
      const HdAnariMaterial::PrimvarBinding &primvarBinding,
      const HdAnariMaterial::PrimvarMapping &primvarMapping,
      const HdAnariMaterial::SamplerMapping &samplerMapping);
};

PXR_NAMESPACE_CLOSE_SCOPE

// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "points.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/geomSubset.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <algorithm>
#include <anari/anari_cpp.hpp>
#include <iterator>
// std
#include <string>

#include "anariTokens.h"
#include "geometry.h"

using namespace std::string_literals;

PXR_NAMESPACE_OPEN_SCOPE

// HdAnariPoints definitions
// ////////////////////////////////////////////////////

HdAnariPoints::HdAnariPoints(
    anari::Device d, const SdfPath &id, const SdfPath &instancerId)
    : HdAnariGeometry(d, HdAnariTokens->sphere, id, instancerId)
{}

HdDirtyBits HdAnariPoints::GetInitialDirtyBitsMask() const
{
  auto mask = HdChangeTracker::Clean | HdChangeTracker::InitRepr
      | HdChangeTracker::DirtyExtent | HdChangeTracker::DirtyPoints
      | HdChangeTracker::DirtyPrimID | HdChangeTracker::DirtyPrimvar
      | HdChangeTracker::DirtyRepr | HdChangeTracker::DirtyMaterialId
      | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility
      | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyInstancer;

  return mask;
}

HdAnariGeometry::GeomSpecificPrimvars HdAnariPoints::GetGeomSpecificPrimvars(
    HdSceneDelegate *sceneDelegate,
    HdDirtyBits *dirtyBits,
    const TfToken::Set &allPrimvars,
    const VtVec3fArray &points)
{
#if 0
  // Sphere radii //
  if (HdChangeTracker::IsDirty(HdChangeTracker::DirtyWidths)) {
    if (const auto &widthsVt = sceneDelegate->Get(GetId(), HdTokens->widths);
        widthsVt.IsHolding<VtFloatArray>()) {
      const auto &widths = widthsVt.UncheckedGet<VtFloatArray>();
      VtFloatArray radii;
      radii.reserve(std::size(widths));
      std::transform(std::cbegin(widths),
          std::cend(widths),
          std::back_inserter(radii),
          [](auto v) { return v / 2.0f; });
      _SetGeometryAttributeArray(
          HdAnariTokens->radius, HdAnariTokens->vertexRadius, VtValue(radii));
    } else {
      _SetGeometryAttributeArray(
          HdAnariTokens->radius, HdAnariTokens->vertexRadius, VtValue());
    }
  }
#endif
  return {};
}

HdAnariGeometry::PrimvarSource HdAnariPoints::UpdatePrimvarSource(HdSceneDelegate *sceneDelegate,
    HdInterpolation interpolation,
    const TfToken &attributeName,
    const VtValue &value)
{
#if 0
  switch (interpolation) {
  case HdInterpolationConstant: {
    _SetGeometryAttributeConstant(attributeName, value);
    break;
  }
  case HdInterpolationUniform:
  case HdInterpolationFaceVarying: {
    break;
  }
  case HdInterpolationVarying:
  case HdInterpolationVertex: {
    auto bindingPoint = _GetVertexBindingPoint(attributeName);
    if (bindingPoint.IsEmpty()) {
      TF_CODING_ERROR("%s is not a valid vertex geometry attribute\n",
          attributeName.GetText());
      break;
    }
    _SetGeometryAttributeArray(attributeName, bindingPoint, value);
    break;
  }
  default:
    break;
  }
#else
  return {};
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE

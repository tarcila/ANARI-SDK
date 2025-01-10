// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mesh.h"

#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
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
#include <pxr/imaging/hd/smoothNormals.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>

#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
#include <iterator>
#include <mutex>

#include "anariTokens.h"
#include "geometry.h"
#include "renderParam.h"

using namespace std::string_literals;

PXR_NAMESPACE_OPEN_SCOPE

// HdAnariPoints definitions
// ////////////////////////////////////////////////////

HdAnariMesh::HdAnariMesh(
    anari::Device d, const SdfPath &id, const SdfPath &instancerId)
    : HdAnariGeometry(d, HdAnariTokens->triangle, id, instancerId)
{}

HdDirtyBits HdAnariMesh::GetInitialDirtyBitsMask() const
{
  return HdChangeTracker::Clean | HdChangeTracker::InitRepr
      | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology
      | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility
      | HdChangeTracker::DirtyCullStyle | HdChangeTracker::DirtyDoubleSided
      | HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtySubdivTags
      | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyNormals
      | HdChangeTracker::DirtyInstancer | HdChangeTracker::DirtyPrimID
      | HdChangeTracker::DirtyRepr | HdChangeTracker::DirtyMaterialId;
}

void HdAnariMesh::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam_,
    HdDirtyBits *dirtyBits,
    const TfToken & reprToken)
{
  auto renderParam = static_cast<HdAnariRenderParam*>(renderParam_);
  
  std::scoped_lock _(renderParam->deviceMutex());

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())) {
      auto rp = static_cast<HdAnariRenderParam*>(renderParam_);

    topology_ = HdMeshTopology(GetMeshTopology(sceneDelegate), 0);
    meshUtil_ = std::make_unique<HdAnariMeshUtil>(&topology_, GetId());
    adjacency_.reset();

    meshUtil_->ComputeTriangleIndices(
        &triangulatedIndices_, &trianglePrimitiveParams_);

    if (!triangulatedIndices_.empty()) {
      fprintf(stderr, "=== Creating topology\n");
      triangles_ = _GetAttributeArray(VtValue(triangulatedIndices_), ANARI_UINT32_VEC3);
    } else {
      anari::release(_anari.device, triangles_);
      triangles_ = {};
    }
    geomsubsets_ = topology_.GetGeomSubsets();
  }

  // FIXME: Should not be an override of Sync, but a tool function returning what's needed.
  HdAnariGeometry::Sync(sceneDelegate, renderParam_, dirtyBits, reprToken);
}

HdGeomSubsets HdAnariMesh::GetGeomSubsets(
  HdSceneDelegate *sceneDelegate,
  HdDirtyBits *dirtyBits
)
{
  return geomsubsets_;
}

HdAnariGeometry::GeomSpecificPrimvars HdAnariMesh::GetGeomSpecificPrimvars(
    HdSceneDelegate *sceneDelegate,
    HdDirtyBits *dirtyBits,
    const TfToken::Set &allPrimvars,
    const VtVec3fArray &points)
{
  GeomSpecificPrimvars primvars;

  // Topology
  primvars.push_back({HdAnariTokens->primitiveIndex, triangles_});

  // Normals
  const auto &normals = sceneDelegate->Get(GetId(), HdTokens->normals);
  bool normalIsAuthored =
      allPrimvars.find(HdTokens->normals) != std::cend(allPrimvars);
  bool doSmoothNormals = (topology_.GetScheme() != PxOsdOpenSubdivTokens->none)
      && (topology_.GetScheme() != PxOsdOpenSubdivTokens->bilinear);

  if (!normalIsAuthored && doSmoothNormals) {
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())
        || HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, GetId(), HdTokens->points)) {
      if (!adjacency_.has_value()) {
        adjacency_.emplace();
        adjacency_->BuildAdjacencyTable(&topology_);
      }

      if (TF_VERIFY(std::size(points) > 0)) {
        const VtVec3fArray &normals = Hd_SmoothNormals::ComputeSmoothNormals(
            &*adjacency_, std::size(points), std::cbegin(points));
        normals_ = _GetAttributeArray(VtValue(normals));
        primvars.push_back({
        HdAnariTokens->faceVaryingNormal,
        normals_
        });
      } else {
        if (normals_)
          anari::release(_anari.device, normals_);
        normals_ = {};
      }
    }
  } else {
    if (normals_)
      anari::release(_anari.device, normals_);
    normals_ = {};
  }

  return primvars;
}

HdAnariGeometry::PrimvarSource HdAnariMesh::UpdatePrimvarSource(HdSceneDelegate *sceneDelegate,
    HdInterpolation interpolation,
    const TfToken &attributeName,
    const VtValue &value)
{
  fprintf(stderr, "=== Creating source for primvar %s\n", attributeName.GetText());
  switch (interpolation) {
  case HdInterpolationConstant: {
    fprintf(stderr, "  holding a constant value of type %s\n", value.GetTypeName().c_str());
    if (value.IsArrayValued()) {
      if (value.GetArraySize() == 0) {
        TF_RUNTIME_ERROR("Constant interpolation with no value.");
        return {};
      }
      if (value.GetArraySize() > 1) {
        TF_RUNTIME_ERROR("Constant interpolation with more than one value.");
      }
    }

    GfVec4f v;
    if (_GetVtValueAsAttribute(value, v)) {
      return v;
    } else {
      TF_RUNTIME_ERROR("Error extracting value from primvar %s", attributeName.GetText());
    }

#if 0
    if (value.IsHolding<VtFloatArray>()) {
      return GfVec4f(value.UncheckedGet<VtFloatArray>()[0], 0.0f, 0.0f, 1.0f);
    } else if (value.IsHolding<VtVec2fArray>()) {
      auto vec2 = value.UncheckedGet<VtVec2fArray>()[0];
      return GfVec4f(vec2[0], vec2[1], 0.0f, 1.0f);
    } else if (value.IsHolding<VtVec3fArray>()) {
      auto vec3 = value.UncheckedGet<VtVec3fArray>()[0];
      return GfVec4f(vec3[0], vec3[1], vec3[2], 1.0f);
    } else if (value.IsHolding<VtVec4fArray>()) {
      return value.UncheckedGet<VtVec4fArray>()[0];
    } if (value.IsHolding<VtIntArray>()) {
      return GfVec4f(value.UncheckedGet<VtIntArray>()[0], 0.0f, 0.0f, 1.0f);
    } else if (value.IsHolding<VtVec2iArray>()) {
      auto vec2 = value.UncheckedGet<VtVec2iArray>()[0];
      return GfVec4f(vec2[0], vec2[1], 0.0f, 1.0f);
    } else if (value.IsHolding<VtVec3iArray>()) {
      auto vec3 = value.UncheckedGet<VtVec3iArray>()[0];
      return GfVec4f(vec3[0], vec3[1], vec3[2], 1.0f);
    } else if (value.IsHolding<VtVec4iArray>()) {
      return value.UncheckedGet<VtVec4iArray>()[0];
    } else {
      TF_RUNTIME_ERROR("Unsupported type %s", value.GetTypeName().c_str());
      return {};
    }
    } else {
    if (value.IsHolding<float>()) {
      return GfVec4f(value.UncheckedGet<float>(), 0.0f, 0.0f, 1.0f);
    } else if (value.IsHolding<GfVec2f>()) {
      auto vec2 = value.UncheckedGet<GfVec2f>();
      return GfVec4f(vec2[0], vec2[1], 0.0f, 1.0f);
    } else if (value.IsHolding<GfVec3f>()) {
      auto vec3 = value.UncheckedGet<GfVec3f>();
      return GfVec4f(vec3[0], vec3[1], vec3[2], 1.0f);
    } else if (value.IsHolding<GfVec4f>()) {
      return value.UncheckedGet<GfVec4f>();
    } if (value.IsHolding<int>()) {
      return GfVec4f(value.UncheckedGet<int>(), 0.0f, 0.0f, 1.0f);
    } else if (value.IsHolding<GfVec2i>()) {
      auto vec2 = value.UncheckedGet<GfVec2i>();
      return GfVec4f(vec2[0], vec2[1], 0.0f, 1.0f);
    } else if (value.IsHolding<GfVec3i>()) {
      auto vec3 = value.UncheckedGet<GfVec3i>();
      return GfVec4f(vec3[0], vec3[1], vec3[2], 1.0f);
    } else if (value.IsHolding<GfVec4i>()) {
      return value.UncheckedGet<GfVec4i>();
    } else {
      TF_RUNTIME_ERROR("Unsupported type %s", value.GetTypeName().c_str());
      return {};
    }
    }
#endif
    break;
  }
  case HdInterpolationUniform: {
    VtValue perFace;
    meshUtil_->GatherPerFacePrimvar(
        GetId(), attributeName, value, trianglePrimitiveParams_, &perFace);
    const auto& perFaceV = perFace.UncheckedGet<VtVec3iArray>();
    return _GetAttributeArray(perFace, ANARI_UINT32_VEC3);
  }
  case HdInterpolationFaceVarying: {
    HdVtBufferSource buffer(attributeName, value);
    VtValue triangulatedPrimvar;
    auto success =
        meshUtil_->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulatedPrimvar);

    if (success) {
      return _GetAttributeArray(triangulatedPrimvar);
    } else {
      TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
      return {};
    }
    break;
  }
  case HdInterpolationVarying:
  case HdInterpolationVertex: {
    return _GetAttributeArray(value);
  }
  case HdInterpolationInstance:
  case HdInterpolationCount:
    assert(false);
    break;
  }

  return {};
}

void HdAnariMesh::Finalize(HdRenderParam* renderParam_) {
  if (normals_) anari::release(_anari.device, normals_);
  if (triangles_) anari::release(_anari.device, triangles_);
  HdAnariGeometry::Finalize(renderParam_);
}

PXR_NAMESPACE_CLOSE_SCOPE

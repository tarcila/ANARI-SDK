// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>
// pxr
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/geomSubset.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sphereSchema.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vertexAdjacency.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <map>
// std
#include <unordered_map>
#include <variant>
#include <vector>

#include "material.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariGeometry : public HdMesh
{
  HF_MALLOC_TAG_NEW("new HdAnariGeometry");

 public:
  HdAnariGeometry(anari::Device d,
      const TfToken &geometryType,
      const SdfPath &id,
      const SdfPath &instancerId = SdfPath());
  ~HdAnariGeometry() override;

  HdDirtyBits GetInitialDirtyBitsMask() const override;

  void Finalize(HdRenderParam *renderParam) override;

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits,
      const TfToken &reprToken) override;

  void GatherInstances(std::vector<anari::Instance> &instances) const;

  HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

 protected:
  struct GeomSpecificPrimVar {
    TfToken bindingPoint;
    anari::Array1D array;
  };
  using GeomSpecificPrimvars = std::vector<GeomSpecificPrimVar>;

  using PrimvarSource = std::variant<
    std::monostate,
    GfVec4f,
    anari::Array1D
  >;

  virtual GeomSpecificPrimvars GetGeomSpecificPrimvars(
      HdSceneDelegate *sceneDelegate,
      HdDirtyBits *dirtyBits,
      const TfToken::Set &allPrimvars,
      const VtVec3fArray &points) { return {}; };

  virtual PrimvarSource UpdatePrimvarSource(HdSceneDelegate *sceneDelegate,
      HdInterpolation interpolation,
      const TfToken &attributeName,
      const VtValue &value) = 0;

  TfToken _GetPrimitiveBindingPoint(const TfToken &attribute);
  TfToken _GetFaceVaryingBindingPoint(const TfToken &attribute);
  TfToken _GetVertexBindingPoint(const TfToken &attribute);

  anari::Array1D _GetAttributeArray(const VtValue &v,
      anari::DataType forcedType = ANARI_UNKNOWN);

  void _SetGeometryAttributeConstant(
      const TfToken &attributeName, const VtValue &v);
  void _SetGeometryAttributeArray(const TfToken &attributeName,
      const TfToken &bindingPoint,
      const VtValue &v,
      anari::DataType forcedType = ANARI_UNKNOWN);
#if 1
  void _SetInstanceAttributeArray(const TfToken &attributeName,
      const VtValue &v,
      anari::DataType forcedType = ANARI_UNKNOWN);
#endif

  void _InitRepr(const TfToken &reprToken, HdDirtyBits *dirtyBits) override;

  static bool _GetVtValueAsAttribute(VtValue v, GfVec4f &out);
  static bool _GetVtArrayBufferData(VtValue v, const void **data, size_t *size, anari::DataType *type);

  HdAnariGeometry(const HdAnariGeometry &) = delete;
  HdAnariGeometry &operator=(const HdAnariGeometry &) = delete;

  virtual HdGeomSubsets GetGeomSubsets(HdSceneDelegate *sceneDelegate, HdDirtyBits *dirtyBits) { return {}; }

 private:
  // Data //
  bool _populated{false};

  TfToken geometryType_;

  struct GeomSubsetInfo {
    anari::Material material;
    HdAnariMaterial::PrimvarBinding primvarBinding;
  };
  GeomSubsetInfo mainGeomInfo_;
  std::unordered_map<SdfPath, GeomSubsetInfo, SdfPath::Hash> geomSubsetsInfo_;

  std::unordered_map<TfToken, PrimvarSource,  TfToken::HashFunctor> primvarSources_;
  std::unordered_map<TfToken, PrimvarSource,  TfToken::HashFunctor> instancePrimvarSources_;

protected:
  struct AnariObjects
  {
    anari::Device device{nullptr};
    anari::Group group{nullptr};
    anari::Instance instance{nullptr};
  } _anari;
};

PXR_NAMESPACE_CLOSE_SCOPE

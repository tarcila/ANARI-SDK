// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "geometry.h"

#include <anari/anari.h>
#include <anari/anari_cpp/Traits.h>
#include <anari/frontend/anari_enums.h>
#include <anari/frontend/type_utility.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/tf.h>
#include <pxr/base/trace/staticKeyData.h>
#include <pxr/base/trace/trace.h>
#include <pxr/base/vt/types.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/dirtyList.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/pxr.h>
#include <stdint.h>
#include <algorithm>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
#include <cstring>
#include <iterator>
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <variant>

#include "anariTokens.h"
#include "debugCodes.h"
#include "instancer.h"
#include "material.h"
#include "renderParam.h"

PXR_NAMESPACE_OPEN_SCOPE

// Helper functions ///////////////////////////////////////////////////////////

template <typename VT_ARRAY_T>
static bool _GetVtArrayBufferData_T(
    VtValue v, const void **data, size_t *size, anari::DataType *type)
{
  if (v.IsHolding<VT_ARRAY_T>()) {
    auto a = v.Get<VT_ARRAY_T>();
    *data = a.cdata();
    *size = a.size();
    *type = anari::ANARITypeFor<typename VT_ARRAY_T::value_type>::value;
    return true;
  }
  return false;
}

bool HdAnariGeometry::_GetVtArrayBufferData(
    VtValue v, const void **data, size_t *size, anari::DataType *type)
{
  if (_GetVtArrayBufferData_T<VtIntArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec2iArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec3iArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec4iArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtUIntArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtFloatArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec2fArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec3fArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec4fArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtMatrix4fArray>(v, data, size, type))
    return true;
  return false;
}

template <typename T>
static bool _GetVtValueAsAttribute_T(VtValue v, GfVec4f &out)
{
  if (v.IsHolding<T>()) {
    auto a = v.Get<T>();
    out = GfVec4f(0.f, 0.f, 0.f, 1.f);
    std::memcpy(&out, &a, sizeof(a));
    return true;
  }
  return false;
}

template <typename T>
static bool _GetVtValueArrayAsAttribute_T(VtValue v, GfVec4f &out)
{
  if (v.IsHolding<T>()) {
    auto a = v.Get<T>();
    out = GfVec4f(0.f, 0.f, 0.f, 1.f);
    std::memcpy(&out, &a[0], sizeof(a[0]));
    return true;
  }
  return false;
}

bool HdAnariGeometry::_GetVtValueAsAttribute(VtValue v, GfVec4f &out)
{
  if (_GetVtValueAsAttribute_T<float>(v, out))
    return true;
  else if (_GetVtValueAsAttribute_T<GfVec2f>(v, out))
    return true;
  else if (_GetVtValueAsAttribute_T<GfVec3f>(v, out))
    return true;
  else if (_GetVtValueAsAttribute_T<GfVec4f>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtFloatArray>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtVec2fArray>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtVec3fArray>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtVec4fArray>(v, out))
    return true;
  return false;
}

HdAnariGeometry::HdAnariGeometry(anari::Device d,
    const TfToken &geometryType,
    const SdfPath &id,
    const SdfPath &instancerId)
    : HdMesh(id), geometryType_(geometryType)
{
  if (!d)
    return;

  _anari.device = d;
  _anari.group = anari::newObject<anari::Group>(d);
  anari::commitParameters(d, _anari.group);

  _anari.instance = anari::newObject<anari::Instance>(d, "transform");
  anari::setParameter(_anari.device, _anari.instance, "group", _anari.group);
  anari::commitParameters(d, _anari.instance);
}

HdAnariGeometry::~HdAnariGeometry()
{
  if (!_anari.device)
    return;
  anari::release(_anari.device, _anari.instance);
  anari::release(_anari.device, _anari.group);
}

HdDirtyBits HdAnariGeometry::GetInitialDirtyBitsMask() const
{
  return HdChangeTracker::AllDirty;
}

void HdAnariGeometry::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam_,
    HdDirtyBits *dirtyBits,
    const TfToken & /*reprToken*/)
{
  HD_TRACE_FUNCTION();
  HF_MALLOC_TAG_FUNCTION();

  auto *renderParam = static_cast<HdAnariRenderParam *>(renderParam_);
  if (!renderParam || !_anari.device)
    return;

  HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
  const SdfPath &id = GetId();

  // update our own instancer data.
  _UpdateInstancer(sceneDelegate, dirtyBits);

  // Make sure we call sync on parent instancers.
  // XXX: In theory, this should be done automatically by the render index.
  // At the moment, it's done by rprim-reference.  The helper function on
  // HdInstancer needs to use a mutex to guard access, if there are actually
  // updates pending, so this might be a contention point.
  HdInstancer::_SyncInstancerAndParents(
      sceneDelegate->GetRenderIndex(), GetInstancerId());

  // Handle material sync first
  SetMaterialId(sceneDelegate->GetMaterialId(id));

  // Enumerate primvars defiend on the geometry. Compute first, then plain.
  std::vector<TfToken> allPrimvars;
  bool pointsIsComputationPrimvar = false;
  bool displayColorIsAuthored = false;
  for (auto i = 0; i < HdInterpolationCount; ++i) {
    auto interpolation = HdInterpolation(i);

    for (const auto &pv : sceneDelegate->GetExtComputationPrimvarDescriptors(
             id, HdInterpolation(i))) {
      allPrimvars.push_back(pv.name);
      if (pv.name == HdTokens->points)
        pointsIsComputationPrimvar = true;
      if (pv.name == HdTokens->displayColor)
        displayColorIsAuthored = true;
    }
  }

  for (auto i = 0; i < HdInterpolationCount; ++i) {
    for (const auto &pv :
        sceneDelegate->GetPrimvarDescriptors(GetId(), HdInterpolation(i))) {
      allPrimvars.push_back(pv.name);
      if (pv.name == HdTokens->displayColor)
        displayColorIsAuthored = true;
    }
  }
  std::sort(begin(allPrimvars), end(allPrimvars));

  // Get an exhaustive list of primvars used by the different materials
  // referencing this geometry.
  std::vector<TfToken> activePrimvars;

  // Check for dirty primvars and primvars that are actually used
  // Handle all primvars from the main geometry and all geosubsets.
  // Assume that points are always to be bound.
  GeomSubsetInfo mainGeomInfo = mainGeomInfo_;

  if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
    HdAnariMaterial *material = static_cast<HdAnariMaterial *>(
        renderIndex.GetSprim(HdPrimTypeTokens->material, GetMaterialId()));
    if (auto mat = material ? material->GetAnariMaterial() : nullptr) {
      mainGeomInfo.material = material->GetAnariMaterial();
      mainGeomInfo.primvarBinding = material->GetPrimvarBinding();
    } else {
      mainGeomInfo.material = renderParam->GetDefaultMaterial();
      mainGeomInfo.primvarBinding = renderParam->GetDefaultPrimvarBinding();
    }
    mainGeomInfo_ = mainGeomInfo;
  }

  for (auto primvarBinding : mainGeomInfo.primvarBinding) {
    activePrimvars.push_back(primvarBinding.first);
  }
  std::sort(begin(activePrimvars), end(activePrimvars));

  std::vector<GeomSubsetInfo> geomSubsetInfos;

  // FIXME: How to check if a subset is dirty? Check for geometry, topology and
  // more?
  auto geomSubsets = GetGeomSubsets(sceneDelegate, dirtyBits);
  for (auto subset : geomSubsets) {
    HdAnariMaterial *material = static_cast<HdAnariMaterial *>(
        renderIndex.GetSprim(HdPrimTypeTokens->material, subset.materialId));
    if (auto mat = material ? material->GetAnariMaterial() : nullptr) {
      geomSubsetInfos.push_back({mat, material->GetPrimvarBinding()});
    } else {
      geomSubsetInfos.push_back({renderParam->GetDefaultMaterial(),
          renderParam->GetDefaultPrimvarBinding()});
    }
  }

  for (auto geomSubsetInfo : geomSubsetInfos) {
    for (auto primvarBinding : geomSubsetInfo.primvarBinding) {
      activePrimvars.push_back(primvarBinding.first);
    }
  }

  for (auto primvar : activePrimvars) {
    fprintf(stderr, "ACTIVE PRIMVAR: %s\n", primvar.GetText());
  }

  // Special case for points and normals that are/might be implicitely used no
  // matter material depenencies.
  if (binary_search(cbegin(allPrimvars), cend(allPrimvars), HdTokens->points)) {
    activePrimvars.push_back(HdTokens->points);
  }
  if (binary_search(
          cbegin(allPrimvars), cend(allPrimvars), HdTokens->normals)) {
    activePrimvars.push_back(HdTokens->normals);
  }

  // Sort and uniqify.
  std::sort(begin(activePrimvars), end(activePrimvars));
  activePrimvars.erase(std::unique(begin(activePrimvars), end(activePrimvars)),
      end(activePrimvars));

  // List primvars to be removed and added.
  auto previousPrimvars = std::vector<TfToken>();
  for (auto primvarArray : primvarSources_) {
    previousPrimvars.push_back(primvarArray.first);
  }
  std::sort(begin(previousPrimvars), end(previousPrimvars));

  auto removedPrimvars = std::vector<TfToken>();
  std::set_difference(cbegin(previousPrimvars),
      cend(previousPrimvars),
      cbegin(activePrimvars),
      cend(activePrimvars),
      back_inserter(removedPrimvars));

  // Kill an inactive or dirty primvar arrays and list primvars to be updated.
  auto outdatedPrimvars = std::vector<TfToken>();
  std::set_difference(cbegin(activePrimvars),
      cend(activePrimvars),
      cbegin(removedPrimvars),
      cend(removedPrimvars),
      back_inserter(outdatedPrimvars));
  for (auto it = begin(primvarSources_); it != end(primvarSources_);) {
    auto isDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, it->first);
    bool isRemoved = std::binary_search(
        cbegin(removedPrimvars), cend(removedPrimvars), it->first);
    if (!isRemoved)
      outdatedPrimvars.push_back(it->first);
    if (isDirty || isRemoved) {
      if (std::holds_alternative<anari::Array1D>(it->second))
        anari::release(_anari.device, std::get<anari::Array1D>(it->second));
      it = primvarSources_.erase(it);
    } else {
      ++it;
    }
  }

  std::sort(begin(outdatedPrimvars), end(outdatedPrimvars));
  outdatedPrimvars.erase(
      std::unique(begin(outdatedPrimvars), end(outdatedPrimvars)),
      end(outdatedPrimvars));

  // Gather primvars sources
  auto computationPrimvarDescriptors =
      HdExtComputationPrimvarDescriptorVector();
  for (auto interpolation = 0; interpolation < HdInterpolationCount;
       ++interpolation) {
    if (interpolation == HdInterpolationInstance)
      continue;

    auto pvds = sceneDelegate->GetExtComputationPrimvarDescriptors(
        GetId(), HdInterpolation(interpolation));
    for (auto pvd : pvds) {
      if (std::binary_search(
              cbegin(outdatedPrimvars), cend(outdatedPrimvars), pvd.name)) {
        computationPrimvarDescriptors.push_back(pvd);
      }
    }
  }

  auto primvarDescriptors = HdPrimvarDescriptorVector();
  for (auto interpolation = 0; interpolation < HdInterpolationCount;
       ++interpolation) {
    if (interpolation == HdInterpolationInstance)
      continue;

    auto pvds = sceneDelegate->GetPrimvarDescriptors(
        GetId(), HdInterpolation(interpolation));
    for (auto pvd : pvds) {
      if (std::binary_search(
              cbegin(outdatedPrimvars), cend(outdatedPrimvars), pvd.name)) {
        primvarDescriptors.push_back(pvd);
      }
    }
  }

  HdExtComputationUtils::ValueStore computationPrimvarSources =
      HdExtComputationUtils::GetComputedPrimvarValues(
          computationPrimvarDescriptors, sceneDelegate);

  // Create missing primvars. We might be implicit normals if smoothing is on
  // for a mesh for instance.
  VtVec3fArray points;
  if (auto it = computationPrimvarSources.find(HdTokens->points);
      it != std::cend(computationPrimvarSources)) {
    if (it->second.IsHolding<VtVec3fArray>())
      points = it->second.UncheckedGet<VtVec3fArray>();
  }

  if (std::size(points) == 0) {
    if (auto vtPoints = sceneDelegate->Get(id, HdTokens->points);
        vtPoints.IsHolding<VtVec3fArray>())
      points = vtPoints.UncheckedGet<VtVec3fArray>();
  }

  // Make sure we have all the information to do the actual parameter binding.
  std::unordered_map<TfToken, HdInterpolation, TfToken::HashFunctor>
      primvarToInterpolation;

  fprintf(stderr, "Creating resources...\n");
  {
    for (auto pvd : computationPrimvarDescriptors) {
      const auto &valueIt = computationPrimvarSources.find(pvd.name);
      if (!TF_VERIFY(valueIt != std::cend(computationPrimvarSources)))
        continue;
      const auto &value = valueIt->second;
      auto source = UpdatePrimvarSource(
          sceneDelegate, pvd.interpolation, pvd.name, value);

      // if (std::holds_alternative<anari::Array1D>(source))
      //  anari::retain(_anari.device, std::get<anari::Array1D>(source));
      primvarSources_.insert({pvd.name, source});
      primvarToInterpolation.insert({pvd.name, pvd.interpolation});
    }

    for (auto pvd : primvarDescriptors) {
      const auto &value = sceneDelegate->Get(id, pvd.name);
      auto source = UpdatePrimvarSource(
          sceneDelegate, pvd.interpolation, pvd.name, value);
      // if (std::holds_alternative<anari::Array1D>(source))
      //   anari::retain(_anari.device, std::get<anari::Array1D>(source));
      primvarSources_.insert({pvd.name, source});
      primvarToInterpolation.insert({pvd.name, pvd.interpolation});
    }
  }

  fprintf(stderr, "Binding resources...\n");
  // Handle geometries
  std::vector<anari::Surface> surfaces;
  if (geomSubsetInfos.empty()) {
    // Creating new geometry, making sure that points and normals are correctly
    // bound.
    auto geometry = anari::newObject<anari::Geometry>(
        _anari.device, geometryType_.GetText());
    mainGeomInfo.primvarBinding.insert(
        {HdTokens->points, HdAnariTokens->position});
    mainGeomInfo.primvarBinding.insert(
        {HdTokens->normals, HdAnariTokens->normal});

    // Iterate all primvars.
    for (auto &&[primvar, bindingPoint] : mainGeomInfo.primvarBinding) {
      if (auto primvarSourceIt = primvarSources_.find(primvar);
          primvarSourceIt != cend(primvarSources_)) {
        auto interpIt = primvarToInterpolation.find(primvar);
        switch (interpIt->second) {
        case HdInterpolationConstant:
          // FIXME: Should be coming from primvarArrays_ which should actually
          // be named primvarValues_...
          fprintf(
              stderr, "   binding to constant `%s`\n", bindingPoint.GetText());
          anari::setParameter(_anari.device,
              geometry,
              bindingPoint.GetText(),
              std::get<GfVec4f>(primvarSourceIt->second));
          // Do Something here
          break;
        case HdInterpolationFaceVarying:
          fprintf(stderr,
              "   binding to facevarying `%s`\n",
              _GetFaceVaryingBindingPoint(bindingPoint).GetText());
          anari::setParameter(_anari.device,
              geometry,
              _GetFaceVaryingBindingPoint(bindingPoint).GetText(),
              std::get<anari::Array1D>(primvarSourceIt->second));
          break;
        case HdInterpolationUniform:
          fprintf(stderr,
              "   binding to uniform `%s`\n",
              _GetPrimitiveBindingPoint(bindingPoint).GetText());
          anari::setParameter(_anari.device,
              geometry,
              _GetPrimitiveBindingPoint(bindingPoint).GetText(),
              std::get<anari::Array1D>(primvarSourceIt->second));
          break;
        case HdInterpolationVarying:
        case HdInterpolationVertex:
          fprintf(stderr,
              "   binding to vertex `%s`\n",
              _GetVertexBindingPoint(bindingPoint).GetText());
          anari::setParameter(_anari.device,
              geometry,
              _GetVertexBindingPoint(bindingPoint).GetText(),
              std::get<anari::Array1D>(primvarSourceIt->second));
          break;
        case HdInterpolationInstance:
          // Not handled here.
          break;
        case HdInterpolationCount:
        default:
          assert(false);
          break;
        }
      }
    }

    // We do try and get them at each sync, as it is not clear when those are
    // dirtied. The expectation is that the implementation of
    // GetGeomSpecificPrimvars are doing the caching for us.
    auto geomSpecificBindingPoints = GetGeomSpecificPrimvars(sceneDelegate,
        dirtyBits,
        TfToken::Set(cbegin(allPrimvars), cend(allPrimvars)),
        points);
    for (auto &&[bindingPoint, array] : geomSpecificBindingPoints) {
      fprintf(stderr, "binding %p to %s\n", array, bindingPoint.GetText());
      anari::setParameter(
          _anari.device, geometry, bindingPoint.GetText(), array);
    }

    anari::commitParameters(_anari.device, geometry);

    auto surface = anari::newObject<anari::Surface>(_anari.device);
    surfaces.push_back(surface);
    anari::setAndReleaseParameter(_anari.device, surface, "geometry", geometry);
    anari::setParameter(
        _anari.device, surface, "material", mainGeomInfo.material);
    anari::setParameter(_anari.device, surface, "id", uint32_t(GetPrimId()));
    anari::commitParameters(_anari.device, surface);

  } else {
    // FIXME: Not implemented yet
  }

  anari::setParameterArray1D(
      _anari.device, _anari.group, "surface", data(surfaces), size(surfaces));
  for (auto surface : surfaces)
    anari::release(_anari.device, surface);
  anari::commitParameters(_anari.device, _anari.group);

  // Now with this instancing
  // Populate instance objects.

  // Transforms //
  if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)
      // || HdChangeTracker::IsInstancerDirty(*dirtyBits, id)
      || HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id)) {
    auto baseTransform = sceneDelegate->GetTransform(id);

    // Set instance parameters
    if (GetInstancerId().IsEmpty()) {
      anari::setParameter(_anari.device,
          _anari.instance,
          "transform",
          GfMatrix4f(baseTransform));
      anari::setParameter(_anari.device, _anari.instance, "id", 0u);
    } else {
      auto instancer = static_cast<HdAnariInstancer *>(
          renderIndex.GetInstancer(GetInstancerId()));

      // Transforms
      const VtMatrix4dArray &transformsd =
          instancer->ComputeInstanceTransforms(id);
      VtMatrix4fArray transforms(std::size(transformsd));
      std::transform(std::cbegin(transformsd),
          std::cend(transformsd),
          std::begin(transforms),
          [&baseTransform](
              const auto &tx) { return GfMatrix4f(baseTransform * tx); });

      VtUIntArray ids(transforms.size());
      std::iota(std::begin(ids), std::end(ids), 0);

      if (auto it = instancePrimvarSources_.find(HdAnariTokens->transform);
          it != end(instancePrimvarSources_)) {
        anari::release(_anari.device, std::get<anari::Array1D>(it->second));
      }
      auto transformsArray = anari::newArray1D(
          _anari.device, std::data(transforms), std::size(transforms));
      anari::setParameter(_anari.device,
          _anari.instance,
          HdAnariTokens->transform.GetText(),
          transformsArray);

      if (auto it = instancePrimvarSources_.find(HdAnariTokens->id);
          it != end(instancePrimvarSources_)) {
        anari::release(_anari.device, std::get<anari::Array1D>(it->second));
      }
      auto idsArray =
          anari::newArray1D(_anari.device, std::data(ids), std::size(ids));
      anari::setParameter(_anari.device,
          _anari.instance,
          HdAnariTokens->id.GetText(),
          idsArray);
    }

    // FIXME: This check has been lost in the rework...
    // thisInstancer->IsPrimvarDirty(pv.name)

    // Primvars
    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)
        || HdChangeTracker::IsInstancerDirty(*dirtyBits, id)
        || HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id)) {
      auto instancer = static_cast<HdAnariInstancer *>(
          renderIndex.GetInstancer(GetInstancerId()));

      // Process primvars
      HdPrimvarDescriptorVector instancePrimvarDescriptors;
      for (auto instancerId = GetInstancerId(); !instancerId.IsEmpty();
           instancerId = renderIndex.GetInstancer(instancerId)->GetParentId()) {
        for (const auto &pv : sceneDelegate->GetPrimvarDescriptors(
                 instancerId, HdInterpolationInstance)) {
          if (pv.name == HdInstancerTokens->instanceRotations
              || pv.name == HdInstancerTokens->instanceScales
              || pv.name == HdInstancerTokens->instanceTranslations
              || pv.name == HdInstancerTokens->instanceTransforms)
            continue;

          if (auto it = instancePrimvarSources_.find(pv.name);
              it != std::cend(instancePrimvarSources_)) {
            auto thisInstancer = static_cast<const HdAnariInstancer *>(
                renderIndex.GetInstancer(instancerId));

            anari::setParameter(_anari.device,
                _anari.instance, 
                instancer->GatherInstancePrimvar(GetId(), pv.name));

            if (prevBindingPoint != newBindingPoint ||) {
              instancePrimvarDescriptors.push_back(pv);
            }
          }
        }

        for (const auto &pv : instancePrimvarDescriptors) {
          if (auto it = updatedPrimvarBinding.find(pv.name);
              it != std::cend(updatedPrimvarBinding)) {
            _SetInstanceAttributeArray(it->second, );
          }
        }
      }

      anari::commitParameters(_anari.device, _anari.instance);

      //  primvarBinding_ = updatedPrimvarBinding;

      if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _UpdateVisibility(sceneDelegate, dirtyBits);
        // renderParam->MarkNewSceneVersion();
      }

      if (!_populated) {
        renderParam->RegisterGeometry(this);
        _populated = true;
      }

      *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
    }

    void HdAnariGeometry::GatherInstances(
        std::vector<anari::Instance> & instances) const
    {
      if (IsVisible()) {
        instances.push_back(_anari.instance);
      }
    }

    void HdAnariGeometry::Finalize(HdRenderParam * renderParam_)
    {
      if (_populated) {
        auto *renderParam = static_cast<HdAnariRenderParam *>(renderParam_);
        renderParam->UnregisterGeometry(this);
        _populated = false;
      }

      for (const auto &primvarSource : primvarSources_) {
        if (std::holds_alternative<anari::Array1D>(primvarSource.second)) {
          anari::release(
              _anari.device, std::get<anari::Array1D>(primvarSource.second));
        }
      }
    }

    HdDirtyBits HdAnariGeometry::_PropagateDirtyBits(HdDirtyBits bits) const
    {
      return bits;
    }

    void HdAnariGeometry::_InitRepr(
        const TfToken &reprToken, HdDirtyBits *dirtyBits)
    {
      TF_UNUSED(dirtyBits);

      // Create an empty repr.
      _ReprVector::iterator it = std::find_if(
          _reprs.begin(), _reprs.end(), _ReprComparator(reprToken));
      if (it == _reprs.end()) {
        _reprs.emplace_back(reprToken, HdReprSharedPtr());
      }
    }

    anari::Array1D HdAnariGeometry::_GetAttributeArray(
        const VtValue &value, anari::DataType overrideType)
    {
      anari::DataType type = ANARI_UNKNOWN;
      const void *data = nullptr;
      size_t size = 0;

      if (!value.IsEmpty()
          && _GetVtArrayBufferData(value, &data, &size, &type)) {
        assert(size);
        // Don't assume any ownership of the data. Map can copy asap.
        if (overrideType != ANARI_UNKNOWN)
          type = overrideType;
        auto array = anari::newArray1D(_anari.device, type, size);
        auto ptr = anari::map<void>(_anari.device, array);
        std::memcpy(ptr, data, size * anari::sizeOf(type));
        anari::unmap(_anari.device, array);
        fprintf(stderr,
            "   Creating buffer %p of type %s with %lu values\n",
            array,
            value.GetTypeName().c_str(),
            size);
        return array;
      }

      fprintf(stderr, "  Cannot extract value buffer\n");
      return {};
    }

#if USE_INSTANCE_ARRAYS
    void HdAnariGeometry::_SetInstanceAttributeArray(
        const TfToken &attributeName,
        const VtValue &value,
        anari::DataType forcedType)
    {
      anari::DataType type = ANARI_UNKNOWN;
      const void *data = nullptr;
      size_t size = 0;

      if (!value.IsEmpty()
          && _GetVtArrayBufferData(value, &data, &size, &type)) {
        TF_DEBUG_MSG(HD_ANARI_RD_GEOMETRY,
            "Assigning instance primvar %s to mesh %s\n",
            attributeName.GetText(),
            GetId().GetText());
        anari::setParameterArray1D(_anari.device,
            _anari.instance,
            attributeName.GetText(),
            forcedType == ANARI_UNKNOWN ? type : forcedType,
            data,
            size);
        instanceBindingPoints_.emplace(attributeName, attributeName);
      } else {
        if (auto it = instanceBindingPoints_.find(attributeName);
            it != std::end(instanceBindingPoints_)) {
          instanceBindingPoints_.erase(it);
          anari::unsetParameter(
              _anari.device, _anari.instance, attributeName.GetText());
          TF_DEBUG_MSG(HD_ANARI_RD_GEOMETRY,
              "Clearing instance primvar %s on mesh %s\n",
              attributeName.GetText(),
              GetId().GetText());
        }
      }
    }
#endif

    TfToken HdAnariGeometry::_GetPrimitiveBindingPoint(const TfToken &attribute)
    {
      if (attribute == HdAnariTokens->attribute0)
        return HdAnariTokens->primitiveAttribute0;
      if (attribute == HdAnariTokens->attribute1)
        return HdAnariTokens->primitiveAttribute1;
      if (attribute == HdAnariTokens->attribute2)
        return HdAnariTokens->primitiveAttribute2;
      if (attribute == HdAnariTokens->attribute3)
        return HdAnariTokens->primitiveAttribute3;
      if (attribute == HdAnariTokens->color)
        return HdAnariTokens->primitiveColor;

      return {};
    }
    TfToken HdAnariGeometry::_GetFaceVaryingBindingPoint(
        const TfToken &attribute)
    {
      if (attribute == HdAnariTokens->attribute0)
        return HdAnariTokens->faceVaryingAttribute0;
      if (attribute == HdAnariTokens->attribute1)
        return HdAnariTokens->faceVaryingAttribute1;
      if (attribute == HdAnariTokens->attribute2)
        return HdAnariTokens->faceVaryingAttribute2;
      if (attribute == HdAnariTokens->attribute3)
        return HdAnariTokens->faceVaryingAttribute3;
      if (attribute == HdAnariTokens->color)
        return HdAnariTokens->faceVaryingColor;
      if (attribute == HdAnariTokens->normal)
        return HdAnariTokens->faceVaryingNormal;

      return {};
    }
    TfToken HdAnariGeometry::_GetVertexBindingPoint(const TfToken &attribute)
    {
      if (attribute == HdAnariTokens->attribute0)
        return HdAnariTokens->vertexAttribute0;
      if (attribute == HdAnariTokens->attribute1)
        return HdAnariTokens->vertexAttribute1;
      if (attribute == HdAnariTokens->attribute2)
        return HdAnariTokens->vertexAttribute2;
      if (attribute == HdAnariTokens->attribute3)
        return HdAnariTokens->vertexAttribute3;
      if (attribute == HdAnariTokens->color)
        return HdAnariTokens->vertexColor;
      if (attribute == HdAnariTokens->normal)
        return HdAnariTokens->vertexNormal;
      if (attribute == HdAnariTokens->position)
        return HdAnariTokens->vertexPosition;

      return {};
    }

  PXR_NAMESPACE_CLOSE_SCOPE
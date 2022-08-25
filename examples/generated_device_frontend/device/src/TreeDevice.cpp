// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// This file was generated by generate_device_frontend.py from Device.cpp

#include <cstdarg>
#include <cstdint>
#include "TreeDevice.h"
#include "anari/backend/LibraryImpl.h"

// debug interface
#include "anari/ext/debug/DebugObject.h"

namespace anari_sdk{
namespace tree{


    template <typename T>
    void writeToVoidP(void *_p, T v)
{
  T *p = (T *)_p;
  *p = v;
}

void *TreeDevice::mapArray(ANARIArray handle)
{
  if (auto obj = handle_cast<ArrayObjectBase *>(handle)) {
    return obj->map();
  } else {
    return nullptr;
  }
}
void TreeDevice::unmapArray(ANARIArray handle)
{
  if (auto obj = handle_cast<ArrayObjectBase *>(handle)) {
    obj->unmap();
  }
}

anari::debug_device::ObjectFactory *getDebugFactory();

int TreeDevice::getProperty(ANARIObject handle,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  if (handle == this_device() && type == ANARI_FUNCTION_POINTER && std::strncmp(name, "debugObjects", 12) == 0) {
    writeToVoidP(mem, getDebugFactory);
    return 1;
  } else if (auto obj = handle_cast<ObjectBase *>(handle)) {
    return obj->getProperty(name, type, mem, size, mask);
  } else {
    return 0;
  }
}

void TreeDevice::setParameter(
    ANARIObject handle, const char *name, ANARIDataType type, const void *mem)
{
  if (auto obj = handle_cast<ObjectBase *>(handle)) {
    obj->set(name, type, mem);
  }
}

void TreeDevice::unsetParameter(ANARIObject handle, const char *name)
{
  if (auto obj = handle_cast<ObjectBase *>(handle)) {
    obj->unset(name);
  }
}

void TreeDevice::commitParameters(ANARIObject handle)
{
  if (auto obj = handle_cast<ObjectBase *>(handle)) {
    obj->commit();
  }
  if (handle == this_device()) {
    if (deviceObject.current.statusCallback.get(
            ANARI_STATUS_CALLBACK, &statusCallback)) {
      statusCallbackUserData = nullptr;
      deviceObject.current.statusCallbackUserData.get(
          ANARI_VOID_POINTER, &statusCallbackUserData);
    } else {
      statusCallback = defaultStatusCallback();
      statusCallbackUserData = defaultStatusCallbackUserPtr();
    }
  }
}

void TreeDevice::release(ANARIObject handle)
{
  if (handle == this_device()) {
    if (refcount.fetch_sub(1) == 1) {
      delete this;
    }
  } else if (auto obj = handle_cast<ObjectBase *>(handle)) {
    obj->release();
  }
}

void TreeDevice::retain(ANARIObject handle)
{
  if (handle == this_device()) {
    refcount++;
  } else if (auto obj = handle_cast<ObjectBase *>(handle)) {
    obj->retain();
  }
}

void TreeDevice::releaseInternal(ANARIObject handle, ANARIObject owner)
{
  if (auto obj = handle_cast<ObjectBase *>(handle)) {
    obj->releaseInternal(owner);
  }
}

void TreeDevice::retainInternal(ANARIObject handle, ANARIObject owner)
{
  if (auto obj = handle_cast<ObjectBase *>(handle)) {
    obj->retainInternal(owner);
  }
}

const void *TreeDevice::frameBufferMap(ANARIFrame handle,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  if (auto obj = handle_cast<FrameObjectBase *>(handle)) {
    return obj->mapFrame(channel, width, height, pixelType);
  } else {
    return 0;
  }
}

void TreeDevice::frameBufferUnmap(ANARIFrame handle, const char *channel)
{
  if (auto obj = handle_cast<FrameObjectBase *>(handle)) {
    obj->unmapFrame(channel);
  }
}

void TreeDevice::renderFrame(ANARIFrame handle)
{
  if (auto obj = handle_cast<FrameObjectBase *>(handle)) {
    obj->renderFrame();
  }
}
int TreeDevice::frameReady(ANARIFrame handle, ANARIWaitMask mask)
{
  if (auto obj = handle_cast<FrameObjectBase *>(handle)) {
    return obj->frameReady(mask);
  } else {
    return 0;
  }
}
void TreeDevice::discardFrame(ANARIFrame handle)
{
  if (auto obj = handle_cast<FrameObjectBase *>(handle)) {
    obj->discardFrame();
  }
}

/////////////////////////////////////////////////////////////////////////////
// Helper/other functions and data members
/////////////////////////////////////////////////////////////////////////////

TreeDevice::TreeDevice(ANARILibrary library)
    : DeviceImpl(library),
      refcount(1),
      deviceObject(this_device(), this_device())
{
  objects.emplace_back(nullptr); // reserve the null index for the null handle
  statusCallback = defaultStatusCallback();
  statusCallbackUserData = defaultStatusCallbackUserPtr();
}

ObjectBase *TreeDevice::fromHandle(ANARIObject handle)
{
  if (handle == static_cast<ANARIObject>(this_device())) {
    return &deviceObject;
  }

  uintptr_t idx = reinterpret_cast<uintptr_t>(handle);

  std::lock_guard<std::recursive_mutex> guard(mutex);
  if (idx < objects.size()) {
    return objects[idx].get();
  } else {
    return nullptr;
  }
}

// query functions
const char **query_object_types(ANARIDataType type);
const void *query_object_info(ANARIDataType type,
    const char *subtype,
    const char *infoName,
    ANARIDataType infoType);
const void *query_param_info(ANARIDataType type,
    const char *subtype,
    const char *paramName,
    ANARIDataType paramType,
    const char *infoName,
    ANARIDataType infoType);

// internal "api" functions
void anariRetainInternal(ANARIDevice d, ANARIObject handle, ANARIObject owner)
{
  reinterpret_cast<TreeDevice *>(d)->retainInternal(handle, owner);
}
void anariReleaseInternal(ANARIDevice d, ANARIObject handle, ANARIObject owner)
{
  reinterpret_cast<TreeDevice *>(d)->releaseInternal(handle, owner);
}
void anariDeleteInternal(ANARIDevice d, ANARIObject handle)
{
  reinterpret_cast<TreeDevice *>(d)->deallocate(handle);
}
void anariReportStatus(ANARIDevice handle,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *format,
    ...)
{
  if (TreeDevice *d = deviceHandle<TreeDevice *>(handle)) {
    if (d->statusCallback) {
      va_list arglist;
      va_list arglist_copy;
      va_start(arglist, format);
      va_copy(arglist_copy, arglist);
      int count = std::vsnprintf(nullptr, 0, format, arglist);
      va_end(arglist);

      std::vector<char> formattedMessage(size_t(count + 1));

      std::vsnprintf(
          formattedMessage.data(), size_t(count + 1), format, arglist_copy);
      va_end(arglist_copy);

      d->statusCallback(d->statusCallbackUserData,
          d->this_device(),
          source,
          sourceType,
          severity,
          code,
          formattedMessage.data());
    }
  }
}

} //namespace tree
} //namespace anari_sdk


    static char deviceName[] = "tree";

extern "C" DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_NEW_DEVICE(
    tree, library, subtype)
{
  if (subtype == std::string("default")
      || subtype == std::string("tree"))
    return (ANARIDevice) new anari_sdk::tree::TreeDevice(library);
  return nullptr;
}

extern "C" DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_INIT(tree) {}

extern "C" DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(
    tree, library)
{
  (void)library;
  static const char *devices[] = {deviceName, nullptr};
  return devices;
}

extern "C" DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_SUBTYPES(
    tree, library, deviceSubtype, objectType)
{
  (void)library;
  (void)deviceSubtype;
  return anari_sdk::tree::query_object_types(objectType);
}

extern "C" DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_PROPERTY(
    tree,
    library,
    deviceSubtype,
    objectSubtype,
    objectType,
    propertyName,
    propertyType)
{
  (void)library;
  (void)deviceSubtype;
  return anari_sdk::tree::query_object_info(
      objectType, objectSubtype, propertyName, propertyType);
}

extern "C" DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_PARAMETER_PROPERTY(
    tree,
    library,
    deviceSubtype,
    objectSubtype,
    objectType,
    parameterName,
    parameterType,
    propertyName,
    propertyType)
{
  (void)library;
  (void)deviceSubtype;
  return anari_sdk::tree::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      propertyName,
      propertyType);
}

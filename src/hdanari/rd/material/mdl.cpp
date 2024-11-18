#include "mdl.h"

#include <anari/anari_cpp.hpp>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

anari::Material HdAnariMdlMaterial::GetOrCreateMaterial(anari::Device device,
    HdSceneDelegate *sceneDelegate,
    HdAnariMaterial* materialPrim)
{
    auto material = anari::newObject<anari::Material>(device, "mdl");
   

    return material;
}

PXR_NAMESPACE_CLOSE_SCOPE

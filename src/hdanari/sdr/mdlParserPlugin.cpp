// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlParserPlugin.h"

#include "tokens.h"

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/pxr.h>
#include <pxr/usd/ndr/declare.h>
#include <pxr/usd/ndr/node.h>
#include <pxr/usd/sdr/shaderNode.h>

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(HdAnariMdlParserPlugin)

NdrNodeUniquePtr HdAnariMdlParserPlugin::Parse(const NdrNodeDiscoveryResult& discoveryRes)
{
    return NdrNodeUniquePtr(
        new NdrNode(
            discoveryRes.identifier,
            discoveryRes.version,
            discoveryRes.subIdentifier.GetString(),
            discoveryRes.family,
            discoveryRes.sourceType,
            discoveryRes.sourceType,
            discoveryRes.resolvedUri,
            discoveryRes.resolvedUri,
            NdrPropertyUniquePtrVec{}
        )
    );
}

const NdrTokenVec& HdAnariMdlParserPlugin::GetDiscoveryTypes() const
{
    static const NdrTokenVec discoveryTypes = { HdAnariSdrTokens->mdl };
    return discoveryTypes;
}

const TfToken& HdAnariMdlParserPlugin::GetSourceType() const
{
    return HdAnariSdrTokens->mdl;
}

PXR_NAMESPACE_CLOSE_SCOPE

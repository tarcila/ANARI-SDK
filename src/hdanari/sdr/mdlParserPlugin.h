// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "api.h"

#include <pxr/pxr.h>
#include <pxr/usd/ndr/parserPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class HDANARI_SDR_API HdAnariMdlParserPlugin : public NdrParserPlugin
{
public:
    /// Takes the specified `NdrNodeDiscoveryResult` instance, which was a
    /// result of the discovery process, and generates a new `NdrNode`.
    /// The node's name, source type, and family must match.
    NdrNodeUniquePtr Parse(const NdrNodeDiscoveryResult& discoveryRes) override;

    /// Returns the types of nodes that this plugin can parse.
    ///
    /// "Type" here is the discovery type (in the case of files, this will
    /// probably be the file extension, but in other systems will be data that
    /// can be determined during discovery). This type should only be used to
    /// match up a `NdrNodeDiscoveryResult` to its parser plugin; this value is
    /// not exposed in the node's API.
    const NdrTokenVec& GetDiscoveryTypes() const override;

    /// Returns the source type that this parser operates on.
    ///
    /// A source type is the most general type for a node. The parser plugin is
    /// responsible for parsing all discovery results that have the types
    /// declared under `GetDiscoveryTypes()`, and those types are collectively
    /// identified as one "source type".
    const TfToken& GetSourceType() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

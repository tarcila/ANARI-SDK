// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlParserPlugin.h"
#include "mdlNodes.h"
#include "tokens.h"

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/pxr.h>
#include <pxr/usd/ndr/declare.h>
#include <pxr/usd/ndr/node.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <cstdio>

using namespace std::string_literals;

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(HdAnariMdlParserPlugin)

NdrNodeUniquePtr HdAnariMdlParserPlugin::Parse(const NdrNodeDiscoveryResult& discoveryRes)
{
#if 0
    fprintf(stderr, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

    fprintf(stderr, "discoveryRes.identifier %s\n", discoveryRes.identifier.GetText());
    fprintf(stderr, "discoveryRes.version %s\n", discoveryRes.version.GetString().c_str());
    fprintf(stderr, "discoveryRes.name %s\n", discoveryRes.name.c_str());
    fprintf(stderr, "discoveryRes.family %s\n", discoveryRes.family.GetText());
    fprintf(stderr, "discoveryRes.sourceType %s\n", discoveryRes.sourceType.GetText());
    fprintf(stderr, "discoveryRes.sourceCode %s\n", discoveryRes.sourceCode.c_str());
    fprintf(stderr, "discoveryRes.uri %s\n", discoveryRes.uri.c_str());
    fprintf(stderr, "discoveryRes.resolvedUri %s\n", discoveryRes.resolvedUri.c_str());
    fprintf(stderr, "discoveryRes.subIdentifier %s\n", discoveryRes.subIdentifier.GetText());
    for (const auto& m: discoveryRes.metadata) {
        fprintf(stderr, "   metadata: %s: %s\n", m.first.GetText(), m.second.c_str());
    }

    fprintf(stderr, "==================================================\n");

    auto sdrNode = MdlSdrShaderNode::ParseSdrDiscoveryResult(discoveryRes);
    if (!sdrNode) return {};
    

    fprintf(stderr, "identifier %s\n", sdrNode->GetIdentifier().GetText());
    fprintf(stderr, "version %s\n", sdrNode->GetVersion().GetString().c_str());
    fprintf(stderr, "name %s\n", sdrNode->GetName().c_str());
    fprintf(stderr, "family %s\n", sdrNode->GetFamily().GetText());
    fprintf(stderr, "context %s\n", sdrNode->GetContext().GetText());                 // context
    fprintf(stderr, "sourceType %s\n", sdrNode->GetSourceType().GetText());
    fprintf(stderr, "definitionUri %s\n", sdrNode->GetResolvedDefinitionURI().c_str());
    fprintf(stderr, "implementationUri %s\n", sdrNode->GetResolvedImplementationURI().c_str());
    fprintf(stderr, "sourceCode %s\n", sdrNode->GetSourceCode().c_str());
    fprintf(stderr, "infoString %s\n", sdrNode->GetInfoString().c_str());
    fprintf(stderr, "implementationName %s\n", sdrNode->GetImplementationName().c_str());

    fprintf(stderr, "metadata:\n");
    for (const auto& m: sdrNode->GetMetadata()) {
        fprintf(stderr, "   - %s: %s\n", m.first.GetText(), m.second.c_str());
    }


    fprintf(stderr, "Inputs:\n");
    auto inputs  = sdrNode->GetInputNames();
    std::sort(
        begin(inputs), end(inputs), [](const auto& a, const auto& b) {
            return a.GetString() < b.GetString();
        }
    );
    for (const auto& name: inputs) {
        auto prop = sdrNode->GetInput(name);
        fprintf(stderr, "  name: %s\n", prop->GetName().GetText());
        fprintf(stderr, "    type: %s\n", prop->GetType().GetText());
        fprintf(stderr, "    isOutput: %i\n", prop->IsOutput());
        fprintf(stderr, "    isArray: %i\n", prop->IsArray());
        fprintf(stderr, "    isDynamicArray %i\n", prop->IsDynamicArray());
        fprintf(stderr, "    arraySize: %i\n", prop->GetArraySize());
        fprintf(stderr, "    info: %s\n", prop->GetInfoString().c_str());
        for (const auto& m: prop->GetMetadata()) {
            fprintf(stderr, "      metadata: %s: %s\n", m.first.GetText(), m.second.c_str());
        }
    }

    auto outputs  = sdrNode->GetOutputNames();
    std::sort(
        begin(inputs), end(inputs), [](const auto& a, const auto& b) {
            return a.GetString() < b.GetString();
        }
    );
    fprintf(stderr, "Outputs:\n");
    for (const auto& name: outputs) {
        auto prop = sdrNode->GetOutput(name);
        fprintf(stderr, "  name: %s\n", prop->GetName().GetText());
        fprintf(stderr, "    type: %s\n", prop->GetType().GetText());
        fprintf(stderr, "    isOutput: %i\n", prop->IsOutput());
        fprintf(stderr, "    isArray: %i\n", prop->IsArray());
        fprintf(stderr, "    isDynamicArray %i\n", prop->IsDynamicArray());
        fprintf(stderr, "    arraySize: %i\n", prop->GetArraySize());
        fprintf(stderr, "    info: %s\n", prop->GetInfoString().c_str());
        fprintf(stderr, "    metadata:\n");
        for (const auto& m: prop->GetMetadata()) {
            fprintf(stderr, "      - %s: %s\n", m.first.GetText(), m.second.c_str());
        }
    }

    fprintf(stderr, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
#else
    auto sdrNode = MdlSdrShaderNode::ParseSdrDiscoveryResult(discoveryRes);
#endif
    return NdrNodeUniquePtr(sdrNode);
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

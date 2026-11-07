// What a game material's shader reads that a stock material of its kind cannot: an opaque or masked
// surface material that reads the scene's depth or color, a Custom formula that reads the scene
// color copy only translucent stock materials have, a Custom formula that reads the game's XluCut
// clipping plane (View members only the game's engine has, P4KadanMaterialExpressions.cpp), and a
// Custom formula that reads the scene's own stencil, which only the game's engine binds (its SceneTextures has SceneStencilTexture; stock 4.26's
// FSceneTextureUniformParameters holds only CustomStencilTexture, so no stock shader can compile it). Reflection Custom formulas need the native base
// pass's nested binding before BasePassCommon defines its alias. The recovered graph keeps each exact
// formula; scene preview nodes use fixed values, while reflection uses the actual stock renderer.
#pragma once
#include "material_json.h"
#include <string>
namespace P4MapKit
{
namespace Material
{

// The one place the game shader path maps a Kadan game node: the read the game's shader makes, as the
// stock node of that name with the same inputs and properties. Empty for any other node.
inline std::string KadanGameReadMeaning(const std::string& type)
{
    if (type == "KadanSceneDepth")
        return "SceneDepth";
    if (type == "KadanSceneColor")
        return "SceneColor";
    if (type == "KadanGameCustom")
        return "Custom";
    if (type == "KadanReflectionCustom")
        return "Custom";
    return {};
}

// True when a Custom formula reads the game's own View extension fields (the Bubble water-height cut's
// C4GlobalClippingPlane and C4XluGlobalClippingPlaneSign). Stock 4.26's View has no such members.
inline bool MaterialGameViewFields(const std::string& code)
{
    return code.find("ResolvedView.C4GlobalClippingPlane") != std::string::npos ||
           code.find("ResolvedView.C4XluGlobalClippingPlaneSign") != std::string::npos;
}

// True when a stock material of the graph's kind may read the scene itself.
inline bool MaterialStockSceneReads(const Json& graph)
{
    const auto text = [&](const char* key)
    {
        return graph.Has(key) && graph.At(key).kind == Json::String ? graph.At(key).string : std::string();
    };
    const auto domain = text("domain"), blend = text("blend_mode");
    if (!domain.empty() && domain != "MD_Surface")
        return true;
    return blend == "BLEND_Translucent" || blend == "BLEND_Additive" || blend == "BLEND_Modulate" ||
           blend == "BLEND_AlphaComposite" || blend == "BLEND_AlphaHoldout";
}

// The graph with its scene reads named as Kadan game nodes where a stock material of its kind cannot
// make them. Every other node, input, property and link is kept.
inline Json KadanGameReads(Json graph, bool anyOpaqueOrMaskedOwner = false)
{
    if (!graph.Has("nodes"))
        return graph;
    const bool stock = !anyOpaqueOrMaskedOwner && MaterialStockSceneReads(graph);
    for (auto& item : graph["nodes"].object)
    {
        auto& node = item.second;
        if (!node.Has("type"))
            continue;
        const auto type = node.At("type").Text();
        if (!stock && type == "SceneDepth")
            node["type"] = Json("KadanSceneDepth");
        else if (!stock && type == "SceneColor")
            node["type"] = Json("KadanSceneColor");
        else if (type == "Custom" && node.Has("props") && node.At("props").Has("Code") &&
                 node.At("props").At("Code").kind == Json::String)
        {
            const auto& code = node.At("props").At("Code").string;
            if (code.find("SceneColorCopyTexture") != std::string::npos ||
                code.find("SceneStencilTexture") != std::string::npos || MaterialGameViewFields(code))
                node["type"] = Json("KadanGameCustom");
            else if (code.find("ReflectionStruct.") != std::string::npos)
                node["type"] = Json("KadanReflectionCustom");
        }
    }
    return graph;
}

// The graph with each Kadan game node as the stock read it stands for: the formula the game's shader
// computes, as the decompiler proves it.
inline Json StockGameReads(Json graph)
{
    if (!graph.Has("nodes"))
        return graph;
    for (auto& item : graph["nodes"].object)
    {
        auto& node = item.second;
        if (!node.Has("type"))
            continue;
        const auto meaning = KadanGameReadMeaning(node.At("type").Text());
        if (!meaning.empty())
            node["type"] = Json(meaning);
    }
    return graph;
}
}
} // P4MapKit::Material

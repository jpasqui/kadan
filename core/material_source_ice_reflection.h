// Port of material_ice_reflection.py: Ice's authored reflection-to-emissive branch. Power R scales two
// real reflection lookups that reach EmissiveColor; without ENABLE_SKY_LIGHT the game's helper emits its
// diagnostic magenta instead, which is what a program compiled without the sky light holds. That branch
// is replaced by the game's own helper as a Custom formula keeping the engine fields, array layer, sampler
// and native mip expression (stock Unreal's roughness helper clamps differently and is not used).
#pragma once
#include "material_json.h"
#include "material_lifter.h"
namespace P4MapKit
{
namespace Material
{
inline const std::string& OriginalIceReflectionCode()
{
    static const std::string code =
        "#if ENABLE_SKY_LIGHT\n"
        "float InfiniteLevel = rcp(0.0f);\n"
        "float4 Capture = TextureCubeArraySampleLevel(ReflectionStruct.ReflectionCubemap, ReflectionStruct.ReflectionCubemapSampler, Direction, 0.0f, View.ReflectionCubemapMaxMip - InfiniteLevel);\n"
        "float3 Sky = TextureCubeSampleLevel(ReflectionStruct.SkyLightCubemap, ReflectionStruct.SkyLightCubemapSampler, Direction, ReflectionStruct.SkyLightParameters.x - InfiniteLevel).rgb;\n"
        "float3 Reflected = 1.5f * Capture.rgb * View.IndirectLightingColorScale + (1.0f - Capture.a) * Sky * View.SkyLightColor.rgb;\n"
        "return Power * Reflected;\n"
        "#else\n"
        "return Power * float3(1.5f, 0.0f, 1.5f);\n"
        "#endif";
    return code;
}
// One Multiply of a scalar parameter and the helper's magenta constant (1.5, 0, 1.5): the reflection branch of a
// program compiled without the sky light.
struct MaterialIceReflectionBranch
{
    std::string Node, Parameter;
    Json Power;
};
inline std::vector<MaterialIceReflectionBranch> OriginalIceReflectionBranches(const Json& graph)
{
    const auto& nodes = graph.At("nodes");
    std::vector<MaterialIceReflectionBranch> candidates;
    for (const auto& item : nodes.object)
    {
        const auto& node = item.second;
        if (node.At("type").Text() != "Multiply" || !node.Has("inputs"))
            continue;
        const auto& inputs = node.At("inputs");
        if (inputs.object.size() != 2 || !inputs.Has("A") || !inputs.Has("B"))
            continue;
        std::string scalar, parameter;
        bool constant = false;
        for (const auto* side : {"A", "B"})
        {
            const auto& child = nodes.At(inputs.At(side).array.at(0).Text());
            const auto type = child.At("type").Text();
            if (scalar.empty() && type == "ScalarParameter" && child.Has("props") &&
                child.At("props").Has("ParameterName"))
            {
                scalar = side;
                parameter = child.At("props").At("ParameterName").Text();
            }
            if (!constant && type == "Constant3Vector" && child.Has("props") && child.At("props").Has("Constant"))
            {
                const auto& value = child.At("props").At("Constant");
                constant = value.kind == Json::Array && value.array.size() == 3 && value.array[0].Numeric() == 1.5 &&
                           value.array[1].Numeric() == 0. && value.array[2].Numeric() == 1.5;
            }
        }
        if (!scalar.empty() && constant)
            candidates.push_back({item.first, parameter, inputs.At(scalar)});
    }
    return candidates;
}
// recover(): the one magenta branch becomes the helper.
inline Json ApplyOriginalIceReflection(Json graph)
{
    const std::string vector = "IceReflectionVector";
    if (graph.At("nodes").Has(vector))
        throw Refused("Ice reflection graph node name collision", "Ice reflection contract");
    const auto candidates = OriginalIceReflectionBranches(graph);
    if (candidates.size() != 1)
        throw Refused("Ice reflection has no unique native magenta fallback branch", "Ice reflection contract");
    auto& nodes = graph["nodes"];
    nodes[vector] =
        Object({{"type", "ReflectionVectorWS"}, {"inputs", Json::MakeObject()}, {"props", Json::MakeObject()}});
    auto inputs = Json::MakeObject();
    inputs["Direction"] = Array({vector, ""});
    inputs["Power"] = candidates[0].Power;
    nodes[candidates[0].Node] = Object(
        {{"type", "Custom"},
         {"inputs", inputs},
         {"props",
          Object({{"Code", OriginalIceReflectionCode()},
                  {"OutputType", "CMOT_Float3"},
                  {"Inputs", Array({Object({{"InputName", "Direction"}}), Object({{"InputName", "Power"}})})}})}});
    return graph;
}
}
} // P4MapKit::Material

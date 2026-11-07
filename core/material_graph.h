// Versioned material graph file boundary. Renderer availability is a separate check.
#pragma once
#include "material_json.h"
#include <functional>
#include <set>

namespace P4MapKit
{
namespace Material
{
struct Refused : std::runtime_error
{
    std::string name;
    explicit Refused(const std::string& text, const std::string& category = "")
        : std::runtime_error(text), name(category.empty() ? text : category)
    {
    }
};
inline Json Array(std::initializer_list<Json> values)
{
    return Json(std::vector<Json>(values));
}
inline Json Object(std::initializer_list<std::pair<const std::string, Json>> values)
{
    return Json(std::map<std::string, Json>(values));
}
inline bool IsMember(const std::string& value, std::initializer_list<const char*> choices)
{
    for (const auto c : choices)
        if (value == c)
            return true;
    return false;
}
struct GraphValidation
{
    std::size_t nodes = 0, links = 0, custom_nodes = 0;
    std::set<std::string> node_types;
    std::vector<std::string> renderer_requirements;
};
inline void RequireKind(const Json& v, Json::Kind kind, const std::string& where)
{
    if (v.kind != kind)
        throw Refused(where + ": wrong JSON type", "graph field type");
}
inline void FiniteTree(const Json& value, const std::string& path, std::size_t depth = 0)
{
    if (depth > 128)
        throw Refused(path + ": property nesting limit", "graph depth limit");
    if (value.kind == Json::Number && !std::isfinite(value.number))
        throw Refused(path + ": nonfinite number", "nonfinite graph property");
    for (std::size_t i = 0; i < value.array.size(); ++i)
        FiniteTree(value.array[i], path + "[" + std::to_string(i) + "]", depth + 1);
    for (const auto& item : value.object)
        FiniteTree(item.second, path + "." + item.first, depth + 1);
}
inline GraphValidation ValidateGraph(const Json& graph, bool requireKnownSamplerTypes = false,
                                     std::size_t maximumNodes = 1000000)
{
    RequireKind(graph, Json::Object, "graph");
    if (!graph.Has("format") || graph.At("format").kind != Json::Number || graph.At("format").number != 2)
        throw Refused("material graph requires format 2", "graph format");
    const auto& nodes = graph.At("nodes");
    const auto& outputs = graph.At("outputs");
    RequireKind(nodes, Json::Object, "nodes");
    RequireKind(outputs, Json::Object, "outputs");
    if (nodes.object.size() > maximumNodes)
        throw Refused("material graph node limit", "graph node limit");
    GraphValidation result;
    result.nodes = nodes.object.size();
    for (const auto key : {"domain", "blend_mode", "shading_model"})
        RequireKind(graph.At(key), Json::String, key);
    if (!IsMember(graph.At("domain").string, {"MD_Surface", "MD_DeferredDecal", "MD_LightFunction", "MD_Volume",
                                              "MD_PostProcess", "MD_UI", "MD_RuntimeVirtualTexture"}))
        throw Refused("the material domain " + graph.At("domain").string, "material domain");
    if (!IsMember(graph.At("blend_mode").string, {"BLEND_Opaque", "BLEND_Masked", "BLEND_Translucent", "BLEND_Additive",
                                                  "BLEND_Modulate", "BLEND_AlphaComposite", "BLEND_AlphaHoldout"}))
        throw Refused("the material blend mode " + graph.At("blend_mode").string, "material blend mode");
    if (!IsMember(graph.At("shading_model").string,
                  {"MSM_Unlit", "MSM_DefaultLit", "MSM_Subsurface", "MSM_PreintegratedSkin", "MSM_ClearCoat",
                   "MSM_SubsurfaceProfile", "MSM_TwoSidedFoliage", "MSM_Hair", "MSM_Cloth", "MSM_Eye",
                   "MSM_SingleLayerWater", "MSM_ThinTranslucent", "MSM_FromMaterialExpression"}))
        throw Refused("the material shading model " + graph.At("shading_model").string, "material shading model");
    for (const auto key : {"two_sided", "tangent_space_normal"})
        RequireKind(graph.At(key), Json::Boolean, key);
    RequireKind(graph.At("clip"), Json::Number, "clip");
    FiniteTree(graph, "graph");
    std::map<std::string, std::vector<std::string>> edges;
    auto link = [&](const Json& value, const std::string& where)
    {
        RequireKind(value, Json::Array, where);
        if (value.array.size() < 2 || value.array.size() > 3)
            throw Refused(where + ": a link has two or three items", "graph link shape");
        RequireKind(value.array[0], Json::String, where + ".node");
        RequireKind(value.array[1], Json::String, where + ".channels");
        if (nodes.object.count(value.array[0].string) == 0)
            throw Refused(where + ": missing node " + value.array[0].string, "graph dangling link");
        if (value.array[1].string.size() > 4 || value.array[1].string.find_first_not_of("RGBA") != std::string::npos)
            throw Refused(where + ": invalid component mask", "graph component mask");
        if (value.array.size() == 3)
        {
            const auto& index = value.array[2];
            RequireKind(index, Json::Number, where + ".output");
            if (index.number < 0 || index.number > INT32_MAX || std::floor(index.number) != index.number)
                throw Refused(where + ": invalid output selector", "graph output selector");
        }
        ++result.links;
        return value.array[0].string;
    };
    const std::set<std::string> supported = {"VectorParameter",
                                             "Constant",
                                             "ScreenPosition",
                                             "Custom",
                                             "Multiply",
                                             "Add",
                                             "Constant2Vector",
                                             "Divide",
                                             "PixelDepth",
                                             "ScalarParameter",
                                             "AppendVector",
                                             "TextureCoordinate",
                                             "TextureSampleParameter2D",
                                             "TextureObject",
                                             "VertexNormalWS",
                                             "Subtract",
                                             "Min",
                                             "Max",
                                             "SceneColor",
                                             "Time",
                                             "Constant3Vector",
                                             "Constant4Vector",
                                             "StaticSwitchParameter",
                                             "VertexColor",
                                             "Frac",
                                             "Abs",
                                             "CollectionParameter",
                                             "WorldPosition",
                                             "Saturate",
                                             "LinearInterpolate",
                                             "OneMinus",
                                             "If",
                                             "Floor",
                                             "Ceil",
                                             "SquareRoot",
                                             "DotProduct",
                                             "Distance",
                                             "SmoothStep",
                                             "Power",
                                             "SceneTexture",
                                             "TextureSample",
                                             "ViewSize",
                                             "Cosine",
                                             "Sine",
                                             "CameraVectorWS",
                                             "PixelNormalWS",
                                             "ObjectPositionWS",
                                             "SceneDepth",
                                             "EyeAdaptation",
                                             "TransformPosition",
                                             "Normalize",
                                             "CameraPositionWS",
                                             "ActorPositionWS",
                                             "SkyAtmosphereLightDirection",
                                             "Transform",
                                             "ObjectOrientation",
                                             "ParticleColor",
                                             "DynamicParameter",
                                             "ParticlePositionWS",
                                             "TwoSidedSign",
                                             "ReflectionVectorWS",
                                             "ParticleSubUVProperties",
                                             "Logarithm2",
                                             "ObjectRadius",
                                             "Sign",
                                             "PrecomputedAOMask",
                                             "SkyAtmosphereLightDiskLuminance",
                                             "ReflectionCapturePassSwitch",
                                             "TextureSampleParameterCube",
                                             "TextureObjectParameter",
                                             "TextureProperty",
                                             "DDX",
                                             "DDY",
                                             "ClearCoatNormalCustomOutput",
                                             "ParticleSubUV",
                                             "ParticleSpeed",
                                             "ParticleSize",
                                             "TextureSampleParameterSubUV"};
    for (const auto& entry : nodes.object)
    {
        const auto& node = entry.second;
        RequireKind(node, Json::Object, entry.first);
        const auto type = node.At("type").Text();
        if (!supported.count(type) && type != "ShadowReplace")
            throw Refused(entry.first + ": the node " + type, "graph node type");
        result.node_types.insert(type);
        const auto& inputs = node.At("inputs");
        const auto& props = node.At("props");
        RequireKind(inputs, Json::Object, entry.first + ".inputs");
        RequireKind(props, Json::Object, entry.first + ".props");
        for (const auto& input : inputs.object)
            edges[entry.first].push_back(link(input.second, entry.first + "." + input.first));
        if (type == "ShadowReplace" && (inputs.object.size() != 2 || !inputs.Has("Default") || !inputs.Has("Shadow")))
            throw Refused(entry.first + ": ShadowReplace requires exact Default and Shadow inputs",
                          "shadow replace inputs");
        if (type.find("TextureSample") == 0)
        {
            if (!props.Has("SamplerSource"))
                throw Refused(entry.first + ": sampler source is not known", "sampler source not known");
            if (!IsMember(props.At("SamplerSource").Text(),
                          {"SSM_FromTextureAsset", "SSM_Wrap_WorldGroupSettings", "SSM_Clamp_WorldGroupSettings"}))
                throw Refused(entry.first + ": sampler source " + props.At("SamplerSource").Text(), "sampler source");
            if (!props.Has("SamplerType"))
            {
                if (requireKnownSamplerTypes)
                    throw Refused(entry.first + ": sampler type is not known", "sampler type not known");
                result.renderer_requirements.push_back(entry.first +
                                                       ": sampler type must be established from the extracted texture");
            }
            else if (!IsMember(props.At("SamplerType").Text(),
                               {"SAMPLERTYPE_Color", "SAMPLERTYPE_Grayscale", "SAMPLERTYPE_Alpha", "SAMPLERTYPE_Normal",
                                "SAMPLERTYPE_Masks", "SAMPLERTYPE_DistanceFieldFont", "SAMPLERTYPE_LinearColor",
                                "SAMPLERTYPE_LinearGrayscale", "SAMPLERTYPE_External", "SAMPLERTYPE_VirtualColor",
                                "SAMPLERTYPE_VirtualGrayscale", "SAMPLERTYPE_VirtualAlpha", "SAMPLERTYPE_VirtualNormal",
                                "SAMPLERTYPE_VirtualMasks", "SAMPLERTYPE_VirtualLinearColor",
                                "SAMPLERTYPE_VirtualLinearGrayscale"}))
                throw Refused(entry.first + ": sampler type " + props.At("SamplerType").Text(), "sampler type");
        }
        if (type == "Custom")
        {
            ++result.custom_nodes;
            RequireKind(props.At("Code"), Json::String, entry.first + ".Code");
            RequireKind(props.At("OutputType"), Json::String, entry.first + ".OutputType");
            if (!IsMember(props.At("OutputType").string,
                          {"CMOT_Float1", "CMOT_Float2", "CMOT_Float3", "CMOT_Float4", "CMOT_MaterialAttributes"}))
                throw Refused(entry.first + ": Custom output type", "custom output type");
            std::set<std::string> names;
            if (props.Has("Inputs"))
            {
                RequireKind(props.At("Inputs"), Json::Array, entry.first + ".Inputs");
                for (const auto& item : props.At("Inputs").array)
                {
                    const auto name = item.At("InputName").Text();
                    if (name.empty() || !names.insert(name).second)
                        throw Refused(entry.first + ": duplicated/empty Custom input", "custom input name");
                }
            }
            for (const auto& input : inputs.object)
                if (!names.count(input.first))
                    throw Refused(entry.first + ": undeclared Custom input " + input.first, "custom input declaration");
            for (const auto& name : names)
                if (!inputs.Has(name))
                    throw Refused(entry.first + ": disconnected Custom input " + name, "custom input declaration");
            result.renderer_requirements.push_back(entry.first +
                                                   ": Custom source requires the named engine inputs and policy");
        }
    }
    for (const auto& output : outputs.object)
    {
        if (!IsMember(output.first, {"BaseColor",
                                     "Metallic",
                                     "Specular",
                                     "Roughness",
                                     "Normal",
                                     "EmissiveColor",
                                     "Opacity",
                                     "OpacityMask",
                                     "SubsurfaceColor",
                                     "AmbientOcclusion",
                                     "Refraction",
                                     "WorldPositionOffset",
                                     "WorldDisplacement",
                                     "TessellationMultiplier",
                                     "PixelDepthOffset",
                                     "Anisotropy",
                                     "Tangent",
                                     "ClearCoat",
                                     "ClearCoatRoughness",
                                     "ClearCoatBottomNormal",
                                     "CustomData0",
                                     "CustomData1",
                                     "ShadingModel"}))
            throw Refused("the material output " + output.first, "graph material output");
        link(output.second, "output." + output.first);
    }
    // Iterative DFS bounds stack use on generated graphs and checks even unreachable nodes.
    std::map<std::string, int> state;
    for (const auto& node : nodes.object)
    {
        if (state[node.first] == 2)
            continue;
        std::vector<std::pair<std::string, std::size_t>> stack{{node.first, 0}};
        state[node.first] = 1;
        while (!stack.empty())
        {
            auto& top = stack.back();
            auto& outgoing = edges[top.first];
            if (top.second == outgoing.size())
            {
                state[top.first] = 2;
                stack.pop_back();
                continue;
            }
            const auto next = outgoing[top.second++];
            if (state[next] == 1)
                throw Refused("material graph cycle at " + next, "graph cycle");
            if (state[next] == 0)
            {
                state[next] = 1;
                stack.emplace_back(next, 0);
            }
        }
    }
    return result;
}
}
} // P4MapKit::Material

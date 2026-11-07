// Exact engine node identities from material_domains.py, domain_extras and
// supplemental_domains. Resource bindings are established by separate readers.
#pragma once
#include "material_graph.h"
#include "material_symbolic.h"
namespace P4MapKit
{
namespace Material
{
inline const char* MaterialRawNormalCode()
{
    return "return Texture2DSample(Texture, TextureSampler, UV);";
}
inline const char* MaterialRawNormalDescription()
{
    return "Original encoded normal texture channels";
}
// True when a postprocess fixed texture is a normal map (its sampler type among the graph defaults).
inline bool PostFixedNormal(const Json& defaults, const std::string& texture)
{
    if (!defaults.Has("fixed_textures"))
        return false;
    const auto& fixed = defaults.At("fixed_textures");
    for (const auto& key : {texture, texture.substr(0, texture.find('.'))})
        if (fixed.Has(key) && fixed.At(key).kind == Json::String)
            return fixed.At(key).string == "SAMPLERTYPE_Normal";
    return false;
}
inline bool IsPostSampleFormula(const Value& value)
{
    return symbolic(value) && IsMember(value.expression->op, {"POSTSCENE", "POSTFIXED", "POSTRAWSAMPLE", "POSTMAPPED"});
}
template <class T>
inline bool LiftPostSource(T& lifter, const Value& value, bool normal, std::pair<std::string, int>& result)
{
    if (!symbolic(value))
        return false;
    const auto& op = value.expression->op;
    const auto& args = value.expression->args;
    if (IsPostSampleFormula(value))
    {
        if (normal)
            throw Refused("postprocess engine input is not a packed normal sample", "postprocess normal sample");
        if (op == "POSTSCENE")
        {
            if (args.size() != 3 || args[0].kind != Value::Boolean)
                throw Refused("invalid checked postprocess scene formula", "postprocess source shape");
            result = {
                lifter.node("SceneTexture", Object({{"Coordinates", lifter.lift({args[1], args[2]})}}),
                            Object({{"SceneTextureId", "PPI_PostProcessInput0"}, {"bFiltered", args[0].boolean}})),
                4};
            return true;
        }
        if (op == "POSTFIXED")
        {
            if (args.size() != 4 || args[0].kind != Value::Text || args[1].kind != Value::Text)
                throw Refused("invalid checked postprocess fixed texture", "postprocess source shape");
            auto sampler = args[1].text;
            while (!sampler.empty() && sampler.front() == '/')
                sampler.erase(sampler.begin());
            if (PostFixedNormal(lifter.defaults, args[0].text))
            {
                // Encoded normal channels, read directly (material_raw_normal.py): a Normal texture object keeps
                // the texture's compression, and the sample makes no UnpackNormalMap.
                if (sampler != "SSM_FromTextureAsset")
                    throw Refused("encoded normal fixed read lacks the exact asset-sampler/plain-2D contract",
                                  "postprocess raw normal");
                const auto texture =
                    lifter.node("TextureObject", Json::MakeObject(),
                                Object({{"Texture", args[0].text}, {"SamplerType", "SAMPLERTYPE_Normal"}}));
                const auto uv = lifter.lift({args[2], args[3]});
                result = {lifter.node("Custom", Object({{"Texture", Array({texture, ""})}, {"UV", uv}}),
                                      Object({{"Code", MaterialRawNormalCode()},
                                              {"OutputType", "CMOT_Float4"},
                                              {"Description", MaterialRawNormalDescription()},
                                              {"Inputs", Array({Object({{"InputName", "Texture"}}),
                                                                Object({{"InputName", "UV"}})})}})),
                          4};
                return true;
            }
            result = {
                lifter.node(
                    "TextureSample", Object({{"Coordinates", lifter.lift({args[2], args[3]})}}),
                    Object({{"Texture", args[0].text}, {"SamplerSource", sampler}, {"AutomaticViewMipBias", false}})),
                4};
            return true;
        }
        if (op == "POSTRAWSAMPLE")
        {
            if (!args.empty())
                throw Refused("invalid raw postprocess scene formula", "postprocess source shape");
            result = {lifter.node("Custom", Json::MakeObject(),
                                  Object({{"Code", "return SceneTextureLookup(GetViewportUV(Parameters), 14, false);"},
                                          {"OutputType", "CMOT_Float4"}})),
                      4};
            return true;
        }
        if (args.size() != 2)
            throw Refused("invalid mapped postprocess scene formula", "postprocess source shape");
        result = {
            lifter.node("Custom", Object({{"UV", lifter.lift(args)}}),
                        Object({{"Code", "return SceneTextureLookup(ViewportUVToSceneTextureUV(UV, 14), 14, false);"},
                                {"OutputType", "CMOT_Float4"},
                                {"Inputs", Array({Object({{"InputName", "UV"}})})}})),
            4};
        return true;
    }
    if (op != "SRC" || args.size() != 2 || args[0].kind != Value::Text)
        return false;
    const auto& name = args[0].text;
    if (name == "PostViewportUV")
    {
        result = {lifter.node("ScreenPosition"), 2};
        return true;
    }
    if (name == "PostViewSize")
    {
        result = {lifter.node("ViewSize"), 2};
        return true;
    }
    if (name == "PostDefaultBufferUV")
    {
        result = {lifter.node("Custom", Json::MakeObject(),
                              Object({{"Code", "return GetDefaultSceneTextureUV(Parameters, 14);"},
                                      {"OutputType", "CMOT_Float2"}})),
                  2};
        return true;
    }
    if (name == "View" && args[1].kind == Value::Text && args[1].text == "pre_view_translation")
    {
        result = {lifter.node("Custom", Json::MakeObject(),
                              Object({{"Code", "return ResolvedView.PreViewTranslation;"},
                                      {"OutputType", "CMOT_Float3"},
                                      {"Description", "Engine View: PreViewTranslation"}})),
                  3};
        return true;
    }
    if (name == "SupplementalViewRow")
    {
        if (args[1].kind != Value::Tuple || args[1].tuple.size() != 2 || args[1].tuple[0].kind != Value::Text ||
            args[1].tuple[0].text != "SVPositionToTranslatedWorld" ||
            (args[1].tuple[1].kind != Value::Float && args[1].tuple[1].kind != Value::Int &&
             args[1].tuple[1].kind != Value::Boolean) ||
            args[1].tuple[1].number() != std::floor(args[1].tuple[1].number()) || args[1].tuple[1].number() < 0 ||
            args[1].tuple[1].number() > 3)
            throw Refused("unbound supplemental View member", "supplemental View member");
        const auto member =
            std::string("SVPositionToTranslatedWorld[") + std::to_string(int(args[1].tuple[1].number())) + "]";
        result = {lifter.node("Custom", Json::MakeObject(),
                              Object({{"Code", "return ResolvedView." + member + ";"},
                                      {"OutputType", "CMOT_Float4"},
                                      {"Description", "Engine View: " + member}})),
                  4};
        return true;
    }
    return false;
}
}
}

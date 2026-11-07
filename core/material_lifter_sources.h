// Lifter: the sources a formula reads, as expression nodes (part of material_lifter.h).
#pragma once
#include "material_lifter.h"
namespace P4MapKit
{
namespace Material
{
inline std::pair<std::string, int> Lifter::source(const Value& x, bool normal)
{
    std::pair<std::string, int> post;
    if (LiftPostSource(*this, x, normal, post))
        return post;
    const auto& a = Args(x);
    const auto op = x.expression->op;
    if (op == "REFLECTION_SAMPLE")
    {
        if (normal || a.empty() || a[0].kind != Value::Text)
            throw Refused("engine reflection color is not a normal-map sample", "reflection sample");
        const auto role = a[0].text;
        const bool array = role == "reflection";
        if (!IsMember(role, {"sky", "sky_blend", "reflection"}) || a.size() != std::size_t(array ? 6 : 5))
            throw Refused("unknown authored reflection resource or operand shape", "reflection sample");
        Json inputs = Object({{"Direction", lift({a[1], a[2], a[3]})}}),
             names = Array({Object({{"InputName", "Direction"}})});
        if (array)
        {
            inputs["Layer"] = lift({a[4]});
            names.array.push_back(Object({{"InputName", "Layer"}}));
        }
        const auto field = array ? "ReflectionCubemapMaxMip" : "SkyLightMaxMip";
        const auto& lod = a.back();
        const auto minusInf = [](const Value& v)
        {
            return IsNumber(v) && std::isinf(v.number()) && v.number() < 0;
        };
        bool folded = minusInf(lod);
        if (IsExpr(lod, "ADD"))
        {
            auto expected = src("ReflectionField", Value(field));
            folded = (Same(Args(lod)[0], expected) && minusInf(Args(lod)[1])) ||
                     (Same(Args(lod)[1], expected) && minusInf(Args(lod)[0]));
        }
        std::string mip = "Mip";
        const auto mode = folded ? "negative_infinity" : "input";
        if (folded)
            mip = std::string("(") +
                  (array ? "View.ReflectionCubemapMaxMip" : "ReflectionStruct.SkyLightParameters.x") + " - rcp(0.0f))";
        else
        {
            inputs["Mip"] = lift({lod});
            names.array.push_back(Object({{"InputName", "Mip"}}));
        }
        const auto texture = array           ? "ReflectionCubemap"
                             : role == "sky" ? "SkyLightCubemap"
                                             : "SkyLightBlendDestinationCubemap";
        const auto sampler = std::string(texture) + "Sampler";
        const auto result =
            (array ? std::string("TextureCubeArraySampleLevel(") : std::string("TextureCubeSampleLevel(")) +
            "ReflectionStruct." + texture + ", ReflectionStruct." + sampler + ", Direction, " +
            (array ? "Layer, " : "") + mip + ")";
        return {node("Custom", inputs,
                     Object({{"Code", "#if ENABLE_SKY_LIGHT\nreturn " + result +
                                          ";\n#else\nreturn float4(0, 0, 0, 0);\n#endif"},
                             {"OutputType", "CMOT_Float4"},
                             {"Description", "P4 native reflection sample: " + role + ":" + mode},
                             {"Inputs", names}})),
                4};
    }
    if (op == "CUBE_SAMPLE")
    {
        if (a.size() != 5 || a[0].kind != Value::Text || a[1].kind != Value::Text || normal)
            throw Refused("cube normal-map unpacking or operand shape is not checked", "cube sample");
        const auto parameter = a[0].text;
        auto sampler = a[1].text;
        while (!sampler.empty() && sampler[0] == '/')
            sampler.erase(sampler.begin());
        const auto said = Default("sampler_types", parameter);
        if (said && said->kind == Json::String && said->string == "SAMPLERTYPE_Normal")
            throw Refused("a cube texture marked normal has no checked unpack contract");
        auto props =
            Object({{"ParameterName", parameter}, {"SamplerSource", sampler}, {"AutomaticViewMipBias", false}});
        if (said && said->kind == Json::String)
            props["SamplerType"] = *said;
        else
            unknown_samplers.insert(parameter);
        if (const auto texture = Default("textures", parameter))
            if (texture->kind == Json::String && !texture->string.empty())
                props["Texture"] = *texture;
        return {node("TextureSampleParameterCube", Object({{"Coordinates", lift({a[2], a[3], a[4]})}}), props), 4};
    }
    if (op == "SAMPLE")
    {
        if (a.size() < 4 || a[0].kind != Value::Text || a[1].kind != Value::Text)
            throw Refused("invalid sample formula", "sample formula");
        const auto name = a[0].text;
        auto mode = a[1].text;
        auto slash = mode.find('/');
        auto sampler = slash == std::string::npos ? "" : mode.substr(slash + 1);
        mode = mode.substr(0, slash);
        if (!normal)
        {
            const auto type = Default("sampler_types", name);
            if (type && type->kind == Json::String && type->string == "SAMPLERTYPE_Normal")
            {
                auto sample = source(x, true).first;
                auto scaled = node("Multiply", Object({{"A", link(sample, "RG")}}), Object({{"ConstB", .5}}));
                return {node("Add", Object({{"A", link(scaled)}}), Object({{"ConstB", .5}})), 2};
            }
        }
        if (const auto fixed = Default("fixed_textures", name))
        {
            if (fixed->kind != Json::String || fixed->string.empty() ||
                (fixed->string == "SAMPLERTYPE_Normal") != normal)
                throw Refused("fixed texture sampler type disagrees with its compiled unpack: " + name,
                              "fixed texture sampler type");
            auto props = Object({{"Texture", name}, {"SamplerType", *fixed}}),
                 inputs = Object({{"Coordinates", lift({a[2], a[3]})}});
            if (!sampler.empty())
                props["SamplerSource"] = sampler;
            if (mode == "plain")
                props["AutomaticViewMipBias"] = false;
            else if (mode == "bias" || mode == "plainbias" || mode == "level" || mode == "levelauto")
            {
                if (a.size() != 5)
                    throw Refused("fixed texture sample has no mip input", "sample formula");
                props["MipValueMode"] = mode == "bias" || mode == "plainbias" ? "TMVM_MipBias" : "TMVM_MipLevel";
                if (mode != "bias")
                    props["AutomaticViewMipBias"] = mode == "levelauto";
                inputs["MipValue"] = lift({a[4]});
            }
            else if (mode != "auto")
                throw Refused("fixed texture sampling mode is not supported: " + mode, "fixed texture sampling mode");
            return {node("TextureSample", inputs, props), 4};
        }
        Json props = Object({{"ParameterName", name}}), inputs = Json::MakeObject();
        if (mode == "plain")
            props["AutomaticViewMipBias"] = false;
        else if (mode == "bias" || mode == "plainbias" || mode == "level" || mode == "levelauto")
        {
            if (a.size() != 5)
                throw Refused("sample mip formula has no mip input", "sample formula");
            props["MipValueMode"] = mode == "bias" || mode == "plainbias" ? "TMVM_MipBias" : "TMVM_MipLevel";
            if (mode != "bias")
                props["AutomaticViewMipBias"] = mode == "levelauto";
            inputs["MipValue"] = lift({a[4]});
        }
        else if (mode != "auto")
            throw Refused("a " + mode + " texture read", "sample mode");
        if (const auto texture = Default("textures", name))
            if (texture->kind == Json::String && !texture->string.empty())
                props["Texture"] = *texture;
        if (!sampler.empty())
            props["SamplerSource"] = sampler;
        const auto type = Default("sampler_types", name);
        const bool known = type && type->kind == Json::String;
        if (known && (type->string == "SAMPLERTYPE_Normal") != normal)
            throw Refused("sampler type: the texture of " + name + " makes it " + type->string + ", and its shader " +
                              (normal ? "unpacks" : "does not unpack") + " it as a normal map",
                          "settings disagree: sampler type");
        if (known || normal)
            props["SamplerType"] = known ? *type : Json("SAMPLERTYPE_Normal");
        else if (defaults.Has("sampler_types"))
            unknown_samplers.insert(name);
        inputs["Coordinates"] = lift({a[2], a[3]});
        return {node("TextureSampleParameter2D", inputs, props), 4};
    }
    if (op == "TOWORLD" || op == "TOTANGENT")
    {
        if (a.size() != 3)
            throw Refused("a transform without three components", "transform shape");
        auto props =
            Object({{"TransformSourceType", op == "TOWORLD" ? "TRANSFORMSOURCE_Tangent" : "TRANSFORMSOURCE_World"},
                    {"TransformType", op == "TOWORLD" ? "TRANSFORM_World" : "TRANSFORM_Tangent"}});
        return {node("Transform",
                     Object({{"Input", Same(a[0], a[1]) && Same(a[0], a[2]) ? transformInput(a) : lift(a)}}), props),
                3};
    }
    if (op == "SCENE")
    {
        if (a.size() != 3 || a[0].kind != Value::Text)
            throw Refused("invalid scene formula", "scene formula");
        const auto kind = a[0].text;
        auto offset = offsetScene(x);
        if (offset.kind != Json::Null)
            return {offset.array[0].Text(), kind == "Depth" ? 1 : 3};
        const auto isScreen = [&](const Value& v, int c)
        {
            return IsExpr(v, "COMP") && Args(v).size() == 2 && NumberIs(Args(v)[1], c) && IsExpr(Args(v)[0], "SRC") &&
                   Args(Args(v)[0])[0].text == "ScreenUV" && Args(Args(v)[0])[1].kind == Value::Null;
        };
        if ((kind == "Depth" || kind == "Color") && isScreen(a[1], 0) && isScreen(a[2], 1))
            return {node(kind == "Depth" ? "SceneDepth" : "SceneColor"), kind == "Depth" ? 1 : 3};
        if (IsMember(kind, {"Color", "ColorPointClamp", "ColorViewClamp", "Depth", "DeviceDepth"}))
        {
            const bool color = kind == "Color" || kind == "ColorPointClamp" || kind == "ColorViewClamp";
            auto usage = node(color ? "SceneColor" : "SceneDepth");
            const std::string usagePin = color ? "UsageColor" : "UsageDepth";
            auto inputs = Object({{"UV", lift({a[1], a[2]})}, {usagePin, link(usage)}});
            std::string code = kind == "DeviceDepth" ? "return LookupDeviceZ(UV);"
                               : color               ? "return DecodeSceneColorForMaterialNode(UV);"
                                                     : "return CalcSceneDepth(UV);";
            std::string description =
                kind == "DeviceDepth" ? "Engine raw scene depth" : "Engine scene lookup at exact buffer UV";
            if (kind == "ColorPointClamp" || kind == "ColorViewClamp")
            {
                const std::string sampler = kind == "ColorPointClamp" ? "SceneTexturesStruct_PointClampSampler"
                                                                      : "View.SharedBilinearClampedSampler";
                code = "return pow(Texture2DSample(SceneColorCopyTexture, " + sampler +
                       ", UV).rgb, 4.0) * 10.0 * View.OneOverPreExposure;";
                description = "Game SceneColor copy through its proved " + kind + " sampler";
            }
            return {node("Custom", inputs,
                         Object({{"Code", code},
                                 {"OutputType", color ? "CMOT_Float3" : "CMOT_Float1"},
                                 {"Inputs", Array({Object({{"InputName", "UV"}}), Object({{"InputName", usagePin}})})},
                                 {"Description", description}})),
                    color ? 3 : 1};
        }
        throw Refused("a scene " + kind + " read away from the pixel's own place",
                      "a scene texture read away from the pixel's own place");
    }
    if (op != "SRC" || a.size() != 2 || a[0].kind != Value::Text)
        throw Refused("a use of " + op + " that has no node", "source formula");
    const auto kind = a[0].text;
    const auto& detail = a[1];
    Json props = Json::MakeObject();
    if (kind == "TextureTexelSize")
    {
        if (detail.kind != Value::Text)
            throw Refused("texture size parameter name is not text", "texture size parameter");
        props["ParameterName"] = detail.text;
        if (const auto texture = Default("textures", detail.text))
            if (texture->kind == Json::String && !texture->string.empty())
                props["Texture"] = *texture;
        if (const auto sampler = Default("sampler_types", detail.text))
            if (sampler->kind == Json::String && !sampler->string.empty())
                props["SamplerType"] = *sampler;
        auto object = node("TextureObjectParameter", Json::MakeObject(), props);
        return {node("TextureProperty", Object({{"TextureObject", link(object)}}),
                     Object({{"Property", "TMTM_TexelSize"}})),
                2};
    }
    if (kind == "ReflectionField")
    {
        if (detail.kind != Value::Text)
            throw Refused("unknown live reflection field", "reflection field");
        const std::map<std::string, std::pair<std::string, int>> fields = {
            {"IndirectLightingColorScale", {"View.IndirectLightingColorScale", 3}},
            {"SkyLightColor", {"View.SkyLightColor", 4}},
            {"ReflectionCubemapMaxMip", {"View.ReflectionCubemapMaxMip", 1}},
            {"SkyLightMaxMip", {"ReflectionStruct.SkyLightParameters.x", 1}}};
        auto field = fields.find(detail.text);
        if (field == fields.end())
            throw Refused("unknown live reflection field", "reflection field");
        auto code = "return " + field->second.first + ";";
        if (detail.text == "SkyLightMaxMip")
            code = "#if ENABLE_SKY_LIGHT\n" + code + "\n#else\nreturn 0.0f;\n#endif";
        return {node("Custom", Json::MakeObject(),
                     Object({{"Code", code},
                             {"OutputType", "CMOT_Float" + std::to_string(field->second.second)},
                             {"Description", "P4 native reflection field: " + detail.text}})),
                field->second.second};
    }
    if (kind == "WaterViewRow")
    {
        if (detail.kind != Value::Tuple || detail.tuple.size() != 2 || detail.tuple[0].kind != Value::Text ||
            !IsNumber(detail.tuple[1]))
            throw Refused("an unbound water View row", "an unbound water View row");
        const auto name = detail.tuple[0].text;
        const auto row = detail.tuple[1].number();
        const bool matrix = IsMember(
            name, {"TranslatedWorldToView", "TranslatedWorldToCameraView", "ViewToClip", "ClipToTranslatedWorld"});
        if ((!matrix && name != "BufferSizeAndInvSize") || row < 0 || row >= (matrix ? 4 : 1) || std::floor(row) != row)
            throw Refused("an unbound water View row", "an unbound water View row");
        const auto suffix = matrix ? "[" + std::to_string(int(row)) + "]" : "";
        return {node("Custom", Json::MakeObject(),
                     Object({{"Code", "return ResolvedView." + name + suffix + ";"},
                             {"OutputType", "CMOT_Float4"},
                             {"Description", "Engine View: " + name + suffix}})),
                4};
    }
    const std::map<std::string, std::pair<std::string, int>> sources = {
        {"ViewportPixelPosition", {"ScreenPosition", 2}},
        {"PrecomputedAOMask", {"PrecomputedAOMask", 1}},
        {"TexCoord", {"TextureCoordinate", 2}},
        {"VertexColor", {"VertexColor", 4}},
        {"TangentZ", {"VertexNormalWS", 3}},
        {"ParticleColor", {"ParticleColor", 4}},
        {"ParticlePositionWS", {"ParticlePositionWS", 3}},
        {"Time", {"Time", 1}},
        {"WorldPosition", {"WorldPosition", 3}},
        {"CameraVector", {"CameraVectorWS", 3}},
        {"ScalarParameter", {"ScalarParameter", 1}},
        {"VectorParameter", {"VectorParameter", 4}},
        {"CollectionScalar", {"CollectionParameter", 1}},
        {"CollectionVector", {"CollectionParameter", 4}},
        {"WorldPositionNoOffsets", {"WorldPosition", 3}},
        {"PixelNormal", {"PixelNormalWS", 3}},
        {"TwoSidedSign", {"TwoSidedSign", 1}},
        {"PixelDepth", {"PixelDepth", 1}},
        {"SkyAtmosphereLightDiskLuminance", {"SkyAtmosphereLightDiskLuminance", 3}},
        {"EyeAdaptation", {"EyeAdaptation", 1}},
        {"CameraPosition", {"CameraPositionWS", 3}}};
    if (kind == "TranslatedWorldPositionNoOffsets")
        return {node("WorldPosition", Json::MakeObject(),
                     Object({{"WorldPositionShaderOffset", "WPT_CameraRelativeNoOffsets"}})),
                3};
    if (kind == "SceneStencilMask2")
        return {
            node(
                "Custom", Json::MakeObject(),
                Object(
                    {{"Code",
                      "return float(SceneTexturesStruct.SceneStencilTexture.Load(int3(int2(Parameters.SvPosition.xy * View.BufferSizeAndInvSize.zw * View.BufferSizeAndInvSize.xy), 0)) STENCIL_COMPONENT_SWIZZLE & 2u);"},
                     {"Description", "P4 compiled scene-stencil bit 1 at the buffer pixel"},
                     {"OutputType", "CMOT_Float1"}})),
            1};
    if (kind == "View" || kind == "Primitive")
    {
        if (detail.kind != Value::Text)
            throw Refused("the input " + kind, "the input " + kind);
        const std::map<std::string, std::pair<std::string, int>> map = {
            {"View.sun_direction", {"SkyAtmosphereLightDirection", 3}},
            {"View.world_camera_origin", {"CameraPositionWS", 3}},
            {"Primitive.object_position", {"ObjectPositionWS", 3}},
            {"Primitive.actor_position", {"ActorPositionWS", 3}},
            {"Primitive.object_radius", {"ObjectRadius", 1}},
            {"Primitive.object_orientation", {"ObjectOrientation", 3}}};
        auto i = map.find(kind + "." + detail.text);
        if (i != map.end())
            return {node(i->second.first), i->second.second};
        const std::map<std::string, std::string> view = {{"view_rect_min", "ViewRectMin"},
                                                         {"view_size", "ViewSizeAndInvSize"},
                                                         {"screen_scale_bias", "ScreenPositionScaleBias"},
                                                         {"inv_device_z", "InvDeviceZToWorldZTransform"},
                                                         {"buffer_uv_min_max", "BufferBilinearUVMinMax"}};
        auto v = view.find(detail.text);
        if (kind == "View" && v != view.end())
        {
            int width = detail.text == "view_rect_min" ? 2 : 4;
            return {node("Custom", Json::MakeObject(),
                         Object({{"Code", "return ResolvedView." + v->second + (width == 2 ? ".xy" : "") + ";"},
                                 {"OutputType", "CMOT_Float" + std::to_string(width)},
                                 {"Description", "Engine View: " + v->second}})),
                    width};
        }
        // The game View extension the Bubble water-height cut reads (material_bubble_exact_clip.source).
        if (kind == "View" && (detail.text == "C4GlobalClippingPlane" || detail.text == "C4XluGlobalClippingPlaneSign"))
        {
            const bool plane = detail.text == "C4GlobalClippingPlane";
            return {node("Custom", Json::MakeObject(),
                         Object({{"Code", plane ? "return ResolvedView.C4GlobalClippingPlane.xy;"
                                                : "return ResolvedView.C4XluGlobalClippingPlaneSign;"},
                                 {"OutputType", plane ? "CMOT_Float2" : "CMOT_Float1"},
                                 {"Description", "Actual game View extension: " + detail.text}})),
                    plane ? 2 : 1};
        }
        if (kind == "View" && detail.text == "c4_sky_light_param")
            return {node("VectorParameter", Json::MakeObject(),
                         Object({{"ParameterName", "C4SkyLightParam"},
                                 {"DefaultValue", Array({0.0, 0.0, 0.0, 0.0})},
                                 {"Group", "The game sets this"}})),
                    4};
        throw Refused("the input " + kind + " " + detail.text, "the input " + kind + " " + detail.text);
    }
    const std::map<std::string, std::pair<const char*, int>> raster = {
        {"SvPosition", {"return Parameters.SvPosition;", 4}},
        {"TangentSign", {"return Parameters.UnMirrored;", 1}},
        {"TangentX", {"return Parameters.TangentToWorld[0];", 3}},
        {"ScreenUV", {"return GetSceneTextureUV(Parameters);", 2}}};
    auto r = raster.find(kind);
    if (r != raster.end())
        return {node("Custom", Json::MakeObject(),
                     Object({{"Code", r->second.first},
                             {"OutputType", "CMOT_Float" + std::to_string(r->second.second)},
                             {"Description", "Engine raster input: " + kind}})),
                r->second.second};
    auto s = sources.find(kind);
    if (s == sources.end())
        throw Refused("the input " + kind, "the input " + kind);
    if (kind == "WorldPositionNoOffsets")
        props["WorldPositionShaderOffset"] = "WPT_ExcludeAllShaderOffsets";
    if (kind == "TexCoord" || kind == "SkyAtmosphereLightDiskLuminance")
    {
        if (!IsNumber(detail) || detail.number() < 0 || std::floor(detail.number()) != detail.number())
            throw Refused("invalid source index", "source index");
        props[kind == "TexCoord" ? "CoordinateIndex" : "LightIndex"] = detail.number();
    }
    if (kind == "ScalarParameter" || kind == "VectorParameter")
    {
        if (detail.kind != Value::Text)
            throw Refused("a parameter name that is not text", "parameter name");
        props["ParameterName"] = detail.text;
        const auto d = Default(kind == "ScalarParameter" ? "scalars" : "vectors", detail.text);
        props["DefaultValue"] = d ? *d : kind == "ScalarParameter" ? Json(0.0) : Array({0.0, 0.0, 0.0, 0.0});
    }
    if (kind == "CollectionScalar" || kind == "CollectionVector")
    {
        if (detail.kind != Value::Tuple || detail.tuple.size() != 2 || detail.tuple[0].kind != Value::Text ||
            detail.tuple[1].kind != Value::Text)
            throw Refused("invalid collection source tuple", "collection source tuple");
        props["Collection"] = detail.tuple[0].text;
        props["ParameterName"] = detail.tuple[1].text;
    }
    return {node(s->second.first, Json::MakeObject(), props), s->second.second};
}
inline FormulaPart Lifter::unpacked(const Value& x)
{
    if (IsExpr(x, "ADD") && NumberIs(Args(x)[1], -1) && IsExpr(Args(x)[0], "MUL") && NumberIs(Args(Args(x)[0])[1], 2))
    {
        const auto& inner = Args(Args(x)[0])[0];
        if (IsExpr(inner, "COMP") && IsExpr(Args(inner)[0], "SAMPLE") &&
            (NumberIs(Args(inner)[1], 0) || NumberIs(Args(inner)[1], 1)))
            return {Args(inner)[0], int(Args(inner)[1].number()), true, true};
    }
    if (IsExpr(x, "RCP") && IsExpr(Args(x)[0], "RSQ") && IsExpr(Args(Args(x)[0])[0], "SAT"))
    {
        const auto& inside = Args(Args(Args(x)[0])[0])[0];
        if (IsExpr(inside, "ADD") && NumberIs(Args(inside)[1], 1) && IsExpr(Args(inside)[0], "NEG"))
        {
            const auto& dot = Args(Args(inside)[0])[0];
            if (IsExpr(dot, "ADD") && IsExpr(Args(dot)[0], "MUL") && IsExpr(Args(dot)[1], "MUL") &&
                Same(Args(Args(dot)[0])[0], Args(Args(dot)[0])[1]) &&
                Same(Args(Args(dot)[1])[0], Args(Args(dot)[1])[1]))
            {
                auto a = unpacked(Args(Args(dot)[0])[0]), b = unpacked(Args(Args(dot)[1])[0]);
                if (a.valid && b.valid && Same(a.source, b.source) && a.component == 0 && b.component == 1)
                    return {a.source, 2, true, true};
            }
        }
    }
    return {};
}
inline FormulaPart Lifter::part(const Value& x)
{
    if (IsExpr(x, "COMP") && Args(x).size() == 2 && Args(x)[0].kind == Value::Expr && IsNumber(Args(x)[1]))
    {
        if (IsExpr(Args(x)[0], "SAMPLE") && Args(x)[1].number() > 1)
        {
            const auto& sample = Args(x)[0];
            if (Args(sample)[0].kind == Value::Text)
            {
                const auto type = Default("sampler_types", Args(sample)[0].text);
                if (type && type->kind == Json::String && type->string == "SAMPLERTYPE_Normal")
                    throw Refused("a packed B or A component of a Normal sampler cannot be re-encoded from decoded XY",
                                  "packed normal texture channels");
            }
        }
        const auto& source = Args(x)[0];
        if (IsPostSampleFormula(source) || IsExpr(source, "SRC") || IsExpr(source, "SAMPLE") ||
            IsExpr(source, "TOWORLD") || IsExpr(source, "TOTANGENT") || IsExpr(source, "SCENE") ||
            IsExpr(source, "REFLECTION_SAMPLE") || IsExpr(source, "CUBE_SAMPLE"))
            return {source, int(Args(x)[1].number()), false, true};
    }
    auto hit = unpacked(x);
    if (hit.valid && normal_samples.count(hit.source.expression->id))
        return hit;
    return {};
}
}
} // P4MapKit::Material

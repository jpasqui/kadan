// Native from-scratch graph compiler. Diagnostic shading deliberately retains
// the established Python recipe. Production availability is a separate contract.
#pragma once
#include "custom_material_emitter.h"
#include "material_json.h"
#include "custom_material_functions.h"
#include <set>
#include <regex>
#include <cctype>
namespace P4MapKit
{
namespace CustomMaterial
{
using Json = Material::Json;
inline const Json& Get(const Json& j, const std::string& key)
{
    static const Json none;
    return j.Has(key) ? j.At(key) : none;
}
inline std::string Text(const Json& j, const std::string& key, const std::string& d = "")
{
    auto& v = Get(j, key);
    return v.kind == Json::Null ? d : v.Text();
}
inline double Number(const Json& j, const std::string& key, double d = 0)
{
    auto& v = Get(j, key);
    return v.kind == Json::Null ? d : v.Numeric();
}
inline bool Flag(const Json& v)
{
    if (v.kind == Json::Null)
        return false;
    if (v.kind == Json::Boolean)
        return v.boolean;
    if (v.kind == Json::Number)
        return v.number != 0;
    if (v.kind == Json::String)
    {
        auto t = v.string;
        std::transform(t.begin(), t.end(), t.begin(),
                       [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); });
        return t == "true" || t == "1";
    }
    throw std::runtime_error("invalid boolean property");
}
inline bool Flag(const Json& j, const std::string& key, bool d = false)
{
    return j.Has(key) ? Flag(j.At(key)) : d;
}
inline std::vector<double> Numbers(const Json& j, std::vector<double> d)
{
    if (j.kind == Json::Null)
        return d;
    if (j.kind == Json::Number)
        return {j.Numeric()};
    if (j.kind != Json::Array)
        throw std::runtime_error("numeric vector required");
    std::vector<double> a;
    for (auto& v : j.array)
        a.push_back(v.Numeric());
    return a;
}
inline std::string Package(std::string s)
{
    auto q = s.find('\'');
    if (q != std::string::npos)
    {
        auto e = s.find('\'', q + 1);
        if (e == std::string::npos)
            throw std::runtime_error("unclosed package reference");
        s = s.substr(q + 1, e - q - 1);
        if (s.size() > 1 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
    }
    auto dot = s.find('.');
    return s.substr(0, dot);
}
inline std::string TextureOf(const Json& n)
{
    auto t = Text(Get(n, "props"), "Texture");
    return t.empty() ? "/Engine/EngineResources/DefaultTexture" : Package(t);
}
struct CompileResult
{
    std::string program;
    bool tangents = false, primitive = false;
    std::vector<std::string> nodes;
};
struct MaterialInputProgram
{
    Emitter emitter;
    std::map<std::string, Value> pins;
    std::vector<std::string> nodes;
};

class GraphCompiler
{
    Json graph;
    const Json* nodes;
    Emitter e;
    std::map<std::string, std::string> slots;
    std::map<std::string, Value> done;
    std::set<std::string> busy, used;
    std::size_t nesting = 0;
    const Json& need_link(const Json& ins, const std::string& pin, const std::string& type)
    {
        auto& link = Get(ins, pin);
        if (link.kind == Json::Null)
            throw std::runtime_error(type + " needs its " + pin + " input connected");
        return link;
    }
    Value input(const Json& ins, const std::string& pin)
    {
        return value(Get(ins, pin));
    }
    Value input(const Json& ins, const std::string& pin, Value d)
    {
        auto& l = Get(ins, pin);
        return l.kind == Json::Null ? d : value(l);
    }
    Value either(const Json& ins, const Json& p, const std::string& pin, const std::string& prop, double d)
    {
        return input(ins, pin, Constant({Number(p, prop, d)}));
    }
    std::string type(const Json& n)
    {
        return Text(n, "type");
    }
    const Json& lookup(const std::string& id)
    {
        return nodes->At(id);
    }
    int index(const Json& link)
    {
        if (link.array.size() < 3 || link.array[2].kind == Json::Null)
            return 0;
        double n = link.array[2].Numeric();
        if (n < 0 || n > 4096 || n != std::floor(n))
            throw std::runtime_error("invalid output index");
        return int(n);
    }
    Json forwarded(const Json& n, int ix)
    {
        auto t = type(n);
        auto& ins = Get(n, "inputs");
        auto& p = Get(n, "props");
        if (t == "StaticSwitch" || t == "StaticSwitchParameter")
        {
            if (t == "StaticSwitchParameter")
            {
                need_link(ins, "A", t);
                need_link(ins, "B", t);
            }
            bool on = t == "StaticSwitch" && Get(ins, "Value").kind != Json::Null ? static_value(Get(ins, "Value"))
                                                                                  : Flag(p, "DefaultValue");
            return need_link(ins, on ? "A" : "B", t);
        }
        if (t == "FeatureLevelSwitch" || t == "QualitySwitch" || t == "ShadingPathSwitch")
        {
            int selectionIndex = t == "FeatureLevelSwitch" ? 3 : 1;
            if (t == "QualitySwitch")
            {
                auto q = Text(graph, "quality_level", "High");
                static const std::map<std::string, int> m = {
                    {"Low", 0}, {"High", 1}, {"Medium", 2}, {"Epic", 3}, {"Default", -1}};
                if (!m.count(q))
                    throw std::runtime_error("invalid quality_level");
                selectionIndex = m.at(q);
            }
            auto d = need_link(ins, "Default", t);
            auto l = selectionIndex < 0 ? Json() : Get(ins, "Inputs" + std::to_string(selectionIndex));
            return l.kind == Json::Null ? d : l;
        }
        static const std::map<std::string, std::vector<std::string>> switches = {
            {"ShaderStageSwitch", {"PixelShader", "PixelShader", "VertexShader"}},
            {"GIReplace", {"Default", "Default"}},
            {"LightmassReplace", {"Realtime", "Realtime", "Lightmass"}},
            {"ShadowReplace", {"Default", "Default", "Shadow"}},
            {"MaterialProxyReplace", {"Realtime", "Realtime", "MaterialProxy"}},
            {"RayTracingQualitySwitch", {"Normal", "Normal", "RayTraced"}},
            {"PreviousFrameSwitch", {"CurrentFrame", "CurrentFrame"}},
            {"ReflectionCapturePassSwitch", {"Default", "Default", "Reflection"}},
            {"VirtualTextureFeatureSwitch", {"No", "Yes", "No"}},
            {"RuntimeVirtualTextureReplace", {"Default", "Default", "VirtualTextureOutput"}}};
        if (switches.count(t))
        {
            if (t == "ShadowReplace" && e.depth)
                throw std::runtime_error("depth and shadow passes require different ShadowReplace programs");
            auto& row = switches.at(t);
            for (std::size_t i = 1; i < row.size(); i++)
                need_link(ins, row[i], t);
            return need_link(ins, row[0], t);
        }
        if (t == "Reroute")
            return Get(ins, "Input");
        if (t == "MaterialFunctionCall")
            return need_link(ins, "Output" + std::to_string(ix), t);
        if (t == "FunctionOutput")
            return need_link(ins, "A", t);
        if (t == "FunctionInput")
        {
            auto l = Get(ins, "Input");
            if (l.kind != Json::Null)
                return l;
            if (Flag(p, "bUsePreviewValueAsDefault"))
                return Get(ins, "Preview");
        }
        return Json();
    }
    bool static_value(Json link)
    {
        std::set<std::string> seen;
        for (;;)
        {
            validate_link(link);
            auto id = link.array[0].Text();
            if (!seen.insert(id).second)
                throw std::runtime_error("static nodes form a loop");
            auto& n = lookup(id);
            auto t = type(n);
            auto& p = Get(n, "props");
            if (t == "StaticBool")
                return Flag(p, "Value");
            if (t == "StaticBoolParameter")
                return Flag(p, "DefaultValue");
            link = forwarded(n, index(link));
            if (link.kind == Json::Null)
                throw std::runtime_error("StaticSwitch Value requires a static boolean");
        }
    }
    void validate_link(const Json& l)
    {
        if (l.kind != Json::Array || l.array.size() < 2 || l.array.size() > 3 || l.array[0].kind != Json::String ||
            (l.array[1].kind != Json::String && l.array[1].kind != Json::Null))
            throw std::runtime_error("invalid material link");
    }
    Value coordinates(const Json& ins, const Json& p, const std::string& pin)
    {
        if (Get(ins, pin).kind != Json::Null)
            return value(Get(ins, pin));
        if (Number(p, "ConstCoordinate") != 0)
            throw std::runtime_error("only texture coordinate 0 is supported here");
        return e.uv();
    }
    Value vec3(Value v)
    {
        if (v.size == 2)
            throw std::runtime_error("three components required");
        return e.op("MOV", 3, {v});
    }
    Value node(const std::string& id, std::string out, int ix)
    {
        auto& n = lookup(id);
        auto t = type(n);
        auto& ins = Get(n, "inputs");
        auto& p = Get(n, "props");
        std::transform(out.begin(), out.end(), out.begin(),
                       [](char c) { return char(std::toupper(static_cast<unsigned char>(c))); });
        if (!out.empty() && out != "RGBA")
        {
            Json l = Json::MakeArray();
            l.array = {Json(id), Json(t == "VectorParameter" ? "RGBA" : ""), Json(ix)};
            auto v = value(l);
            std::transform(out.begin(), out.end(), out.begin(),
                           [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); });
            return e.swizzle(v, out);
        }
        if (t == "Constant")
            return Constant({Number(p, "R")});
        if (t == "Constant2Vector" || t == "Constant3Vector" || t == "Constant4Vector")
        {
            int size = t[8] - '0';
            auto c = Numbers(Get(p, "Constant"), {Number(p, "R"), Number(p, "G"), Number(p, "B"), Number(p, "A")});
            if (c.size() < std::size_t(size))
                throw std::runtime_error("short constant vector");
            c.resize(size);
            return Constant(c);
        }
        if (t == "ScalarParameter")
        {
            auto name = Text(p, "ParameterName");
            if (e.material_parameters.count(name))
                return e.parameter(name, 1);
            if (e.owned_uniforms)
                throw std::runtime_error("owned scalar parameter binding absent");
            return Constant({Number(p, "DefaultValue")});
        }
        if (t == "VectorParameter")
        {
            auto name = Text(p, "ParameterName");
            if (e.material_parameters.count(name))
                return e.parameter(name, out == "RGBA" ? 4 : 3);
            if (e.owned_uniforms)
                throw std::runtime_error("owned vector parameter binding absent");
            auto c = Numbers(Get(p, "DefaultValue"), {0, 0, 0, 1});
            int size = out == "RGBA" ? 4 : 3;
            if (c.size() < std::size_t(size))
                throw std::runtime_error("short vector parameter");
            c.resize(size);
            return Constant(c);
        }
        if (t == "CollectionParameter")
        {
            if (!p.Has("CollectionValue"))
                throw std::runtime_error("collection needs editor-resolved default");
            auto c = Numbers(p.At("CollectionValue"), {});
            if (c.size() != 1 && c.size() != 4)
                throw std::runtime_error("collection default requires one or four components");
            return Constant(c);
        }
        if (t == "Transform")
        {
            auto a = input(ins, "Input");
            if (a.size != 3)
                throw std::runtime_error("Transform needs three components");
            auto source = Text(p, "TransformSourceType", "TRANSFORMSOURCE_TANGENT"),
                 target = Text(p, "TransformType", "TRANSFORM_WORLD");
            source = source.substr(source.find_last_of('_') + 1);
            target = target.substr(target.find_last_of('_') + 1);
            return e.transform_vector(a, source, target);
        }
        if (t == "Time")
            return e.view("game_time", 1);
        if (t == "TextureCoordinate")
        {
            double index = Number(p, "CoordinateIndex");
            if (index != std::floor(index))
                throw std::runtime_error("invalid texture coordinate");
            auto uv = e.uv(int(index));
            double u = Number(p, "UTiling", 1), v = Number(p, "VTiling", 1);
            return u == 1 && v == 1 ? e.op("MOV", 2, {uv}) : e.op("MUL", 2, {uv, Constant({u, v})});
        }
        if (t == "WorldPosition")
            return e.world_position();
        if (t == "VertexNormalWS")
            return e.normal();
        if (t == "PixelNormalWS")
            return e.pixel_normal();
        if (t == "CameraVectorWS")
            return e.camera_vector();
        if (t == "TextureSample" || t == "TextureSampleParameter2D" || t == "TextureObjectParameter")
        {
            if (Get(ins, "TextureObject").kind != Json::Null)
                throw std::runtime_error("texture object pins are unavailable");
            auto l = Get(ins, "Coordinates");
            if (l.kind == Json::Null)
                l = Get(ins, "UVs");
            auto fallback = e.uv();
            auto uv = l.kind == Json::Null ? fallback : value(l);
            auto tex = TextureOf(n);
            auto parameter = e.owned_uniforms && t != "TextureSample" ? Text(p, "ParameterName")
                             : slots.count(tex)                       ? slots.at(tex)
                                                                      : "Diffuse_01";
            if (e.owned_uniforms && t == "TextureSample" && !slots.count(tex))
                throw std::runtime_error("owned texture sample binding absent");
            auto sampled = e.sample(uv, parameter, e.owned_uniforms && Flag(p, "bApplyViewMipBias", true));
            auto sampler = Text(p, "SamplerType");
            std::transform(sampler.begin(), sampler.end(), sampler.begin(),
                           [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); });
            auto result = sampler.find("normal") != std::string::npos
                              ? e.unpack_normal(sampled)
                              : sampled; /* UE names the first output of a texture sample "" with an RGB mask. */
            return out.empty() && ix == 0 && result.size == 4 ? e.swizzle(result, "rgb") : result;
        }
        static const std::map<std::string, std::string> binary = {
            {"Add", "ADD"}, {"Multiply", "MUL"}, {"Min", "MIN"}, {"Max", "MAX"}};
        if (binary.count(t) || t == "Subtract" || t == "Divide")
        {
            auto a = either(ins, p, "A", "ConstA", t == "Subtract" || t == "Divide" ? 1 : 0),
                 b = either(ins, p, "B", "ConstB", t == "Divide" ? 2 : 1);
            int size = Size({a, b});
            if (t == "Subtract")
                return e.op("ADD", size, {a, e.op("MUL", b.size, {b, Constant({-1})})});
            if (t == "Divide")
                return e.op("MUL", size, {a, e.per_component("RCP", b)});
            return e.op(binary.at(t), size, {a, b});
        }
        if (t == "Sine" || t == "Cosine" || t == "Tangent")
        {
            auto x = input(ins, "Input");
            double period = Number(p, "Period", 1);
            bool scale = t == "Tangent" ? period > 0 : period != 0;
            if (scale)
                x = e.op("MUL", x.size, {x, Constant({(t == "Tangent" ? 6.28318530717958 : 6.283185307) / period})});
            if (t == "Tangent")
            {
                auto sine = e.per_component("SIN", x), cosine = e.per_component("COS", x);
                return e.op("MUL", x.size, {sine, e.per_component("RCP", cosine)});
            }
            return e.per_component(t == "Sine" ? "SIN" : "COS", x);
        }
        static const std::map<std::string, std::string> unary = {
            {"Frac", "FRC"}, {"Floor", "FLR"}, {"Ceil", "CEIL"}, {"Truncate", "TRUNC"}};
        if (unary.count(t))
        {
            auto x = input(ins, "Input");
            return e.op(unary.at(t), x.size, {x});
        }
        if (t == "Abs")
        {
            auto x = input(ins, "Input");
            return e.op("MAX", x.size, {x, e.op("MUL", x.size, {x, Constant({-1})})});
        }
        if (t == "OneMinus")
        {
            auto x = input(ins, "Input");
            return e.op("ADD", x.size, {e.op("MUL", x.size, {x, Constant({-1})}), Constant({1})});
        }
        if (t == "Saturate")
        {
            auto x = input(ins, "Input");
            return e.op("MOV", x.size, {x}, true);
        }
        if (t == "Clamp")
        {
            auto x = input(ins, "Input"), lo = either(ins, p, "Min", "MinDefault", 0),
                 hi = either(ins, p, "Max", "MaxDefault", 1);
            return e.op("MIN", x.size, {e.op("MAX", x.size, {x, lo}), hi});
        }
        if (t == "LinearInterpolate")
        {
            auto alpha = either(ins, p, "Alpha", "ConstAlpha", .5);
            if (alpha == Constant({0}))
                return either(ins, p, "A", "ConstA", 0);
            if (alpha == Constant({1}))
                return either(ins, p, "B", "ConstB", 1);
            auto a = either(ins, p, "A", "ConstA", 0), b = either(ins, p, "B", "ConstB", 1);
            int size = Size({a, b});
            return e.op("MAD", size, {alpha, e.op("ADD", size, {b, e.op("MUL", a.size, {a, Constant({-1})})}), a});
        }
        if (t == "Power")
        {
            auto base = input(ins, "Base"), exp = either(ins, p, "Exponent", "ConstExponent", 2);
            auto lg = e.per_component("LG2", e.op("MAX", base.size, {base, Constant({1e-8})}));
            return e.per_component("EX2", e.op("MUL", base.size, {lg, exp}));
        }
        if (t == "DotProduct")
        {
            auto a = input(ins, "A"), b = input(ins, "B");
            int componentCount = Size({a, b});
            auto d = e.temp();
            e.emit("DP" + std::to_string(componentCount) + ".F32 " + d + ".x, " + e.src(a, componentCount) + ", " +
                   e.src(b, componentCount));
            return {d, 1};
        }
        if (t == "Normalize")
            return e.normalize(input(ins, "VectorInput"));
        if (t == "ComponentMask" || t == "StaticComponentMaskParameter")
        {
            auto x = input(ins, "Input");
            std::string channels;
            for (char c : std::string("rgba"))
            {
                std::string prop =
                    (t == "StaticComponentMaskParameter" ? "Default" : "") + std::string(1, char(std::toupper(c)));
                if (Flag(p, prop))
                    channels += c;
            }
            return e.swizzle(x, channels);
        }
        if (t == "AppendVector")
        {
            auto a = input(ins, "A"), b = input(ins, "B");
            return e.append(a, b);
        }
        if (t == "Fresnel")
        {
            auto pn = e.pixel_normal();
            auto surfaceNormal = input(ins, "Normal", pn);
            auto ndv = e.temp();
            auto cv = e.camera_vector();
            e.emit("DP3.F32 " + ndv + ".x, " + e.src(surfaceNormal, 3) + ", " + e.src(cv, 3));
            auto edge = e.op(
                "ADD", 1, {e.op("MUL", 1, {e.op("MAX", 1, {{ndv, 1}, Constant({0})}), Constant({-1})}), Constant({1})});
            auto exp = either(ins, p, "ExponentIn", "Exponent", 5);
            auto lg = e.per_component("LG2", e.op("MAX", 1, {edge, Constant({1e-8})}));
            auto powered = e.per_component("EX2", e.op("MUL", 1, {lg, exp}));
            auto base = either(ins, p, "BaseReflectFractionIn", "BaseReflectFraction", .04);
            return e.op("MAD", 1,
                        {powered, e.op("ADD", 1, {e.op("MUL", 1, {base, Constant({-1})}), Constant({1})}), base});
        }
        if (t == "SquareRoot")
            return e.sqrt(input(ins, "Input"));
        if (t == "Sign")
        {
            auto x = input(ins, "Input");
            auto pos = e.op("SGT", x.size, {x, Constant({0})}), neg = e.op("SLT", x.size, {x, Constant({0})});
            return e.op("ADD", x.size, {pos, e.op("MUL", x.size, {neg, Constant({-1})})});
        }
        if (t == "Distance")
        {
            auto a = input(ins, "A"), b = input(ins, "B");
            int componentCount = Size({a, b});
            auto d = e.op("ADD", componentCount, {a, e.op("MUL", b.size, {b, Constant({-1})})});
            auto sq = e.temp();
            if (componentCount > 1)
                e.emit("DP" + std::to_string(componentCount) + ".F32 " + sq + ".x, " + d.text + ", " + d.text);
            else
                e.emit("MUL.F32 " + sq + ".x, " + d.text + ".x, " + d.text + ".x");
            return e.sqrt({sq, 1});
        }
        if (t == "CrossProduct")
        {
            auto a = input(ins, "A"), b = input(ins, "B");
            return e.cross(a, b);
        }
        if (t == "If")
        {
            auto a = input(ins, "A"), b = either(ins, p, "B", "ConstB", 0), gt = input(ins, "AGreaterThanB"),
                 lt = input(ins, "ALessThanB"), eq = input(ins, "AEqualsB", gt);
            int size = Size({gt, lt, eq});
            double threshold = Number(p, "EqualsThreshold", 1e-5);
            auto diff = e.op("ADD", 1, {a, e.op("MUL", 1, {b, Constant({-1})})});
            auto gm = e.op("SGT", 1, {diff, Constant({threshold})}),
                 lm = e.op("SLT", 1, {diff, Constant({-threshold})});
            auto res = e.op("MOV", size, {eq});
            res = e.op("MAD", size, {gm, e.op("ADD", size, {gt, e.op("MUL", size, {res, Constant({-1})})}), res});
            return e.op("MAD", size, {lm, e.op("ADD", size, {lt, e.op("MUL", size, {res, Constant({-1})})}), res});
        }
        if (t == "Panner")
        {
            auto uv = e.uv();
            uv = input(ins, "Coordinate", uv);
            auto time = e.view("game_time", 1);
            time = input(ins, "Time", time);
            auto speed = input(ins, "Speed", Constant({Number(p, "SpeedX"), Number(p, "SpeedY")}));
            return e.op("MAD", 2, {e.op("MOV", 2, {time}), speed, uv});
        }
        if (t == "Round")
        {
            auto x = input(ins, "Input");
            return e.op("FLR", x.size, {e.op("ADD", x.size, {x, Constant({.5})})});
        }
        if (t == "Desaturation")
        {
            auto color = input(ins, "Input"), fraction = input(ins, "Fraction", Constant({1}));
            auto factors = Numbers(Get(p, "LuminanceFactors"), {.3, .59, .11});
            if (factors.size() < 3)
                throw std::runtime_error("short LuminanceFactors");
            factors.resize(3);
            auto gray = e.temp();
            e.emit("DP3.F32 " + gray + ".x, " + e.src(color, 3) + ", " + e.src(Constant(factors), 3));
            return e.op("MAD", 3,
                        {fraction, e.op("ADD", 3, {{gray, 1}, e.op("MUL", 3, {color, Constant({-1})})}), color});
        }
        auto link = forwarded(n, ix);
        if (t == "FunctionInput")
        {
            auto kind = Text(p, "InputType", "FunctionInput_Vector3");
            kind = kind.substr(kind.find_last_of('_') + 1);
            static const std::map<std::string, int> sizes = {
                {"Scalar", 1}, {"Vector2", 2}, {"Vector3", 3}, {"Vector4", 4}};
            if (!sizes.count(kind))
                throw std::runtime_error("unsupported function input type");
            int size = sizes.at(kind);
            if (link.kind != Json::Null)
            {
                auto v = value(link);
                if (v.size > 1 && v.size < size)
                    throw std::runtime_error("function input has too few components");
                return v.size > size ? e.swizzle(v, std::string("rgba").substr(0, size)) : v;
            }
            if (!Flag(p, "bUsePreviewValueAsDefault"))
                throw std::runtime_error("function input is unconnected");
            auto a = Numbers(Get(p, "PreviewValue"), {0, 0, 0, 1});
            if (a.size() < std::size_t(size))
                throw std::runtime_error("short preview");
            a.resize(size);
            return Constant(a);
        }
        if (link.kind != Json::Null)
            return value(link);
        if (t == "Arccosine" || t == "ArccosineFast")
            return e.acos(input(ins, "Input"), t == "ArccosineFast");
        if (t == "Arcsine" || t == "ArcsineFast")
        {
            auto x = input(ins, "Input");
            auto c = e.acos(x, t == "ArcsineFast");
            return e.op("ADD", x.size, {e.neg(c), Constant({1.5707963267949})});
        }
        if (t == "Arctangent" || t == "ArctangentFast")
            return e.atan(input(ins, "Input"), t == "ArctangentFast");
        if (t == "Arctangent2" || t == "Arctangent2Fast")
        {
            auto y = input(ins, "Y"), x = input(ins, "X");
            return e.atan2(y, x, t == "Arctangent2Fast");
        }
        if (t == "Fmod")
        {
            auto a = input(ins, "A"), b = input(ins, "B");
            if (b.size > a.size)
                throw std::runtime_error("Fmod B has more components than A");
            auto q = e.op("TRUNC", a.size, {e.op("MUL", a.size, {a, e.per_component("RCP", b)})});
            return e.op("MAD", a.size, {e.neg(q), b, a});
        }
        if (t == "Logarithm2")
            return e.per_component("LG2", input(ins, "X"));
        if (t == "Logarithm10")
        {
            auto x = input(ins, "X");
            return e.op("MUL", x.size, {e.per_component("LG2", x), Constant({.301029995663981})});
        }
        if (t == "Step")
        {
            auto y = either(ins, p, "Y", "ConstY", 0), x = either(ins, p, "X", "ConstX", 1);
            return e.op("SGE", Size({x, y}), {x, y});
        }
        if (t == "SmoothStep" || t == "InverseLinearInterpolate")
        {
            auto loPin = t == "SmoothStep" ? "Min" : "A", hiPin = t == "SmoothStep" ? "Max" : "B";
            double lc = Number(p, std::string("Const") + loPin, 0), hc = Number(p, std::string("Const") + hiPin, 1);
            auto lo = input(ins, loPin, Constant({lc})), hi = input(ins, hiPin, Constant({hc})),
                 x = either(ins, p, "Value", "ConstValue", 0);
            int size = Size({lo, hi, x});
            auto& xl = Get(ins, "Value");
            auto& ll = Get(ins, loPin);
            auto& hl = Get(ins, hiPin);
            if (xl.kind != Json::Null && xl.Dump() == ll.Dump())
                return Constant(std::vector<double>(size, 0));
            if (xl.kind != Json::Null && xl.Dump() == hl.Dump())
                return Constant(std::vector<double>(size, 1));
            bool same = ll.kind != Json::Null ? ll.Dump() == hl.Dump() : hl.kind == Json::Null && lc == hc;
            if (same)
            {
                if (t == "SmoothStep")
                    return e.op("SGE", size, {x, lo});
                throw std::runtime_error("InverseLinearInterpolate endpoints coincide");
            }
            auto diff = e.sub(size, x, lo);
            auto denom = e.sub(Size({lo, hi}), hi, lo);
            auto ratio =
                e.op("MUL", size, {diff, e.per_component("RCP", denom)}, t == "SmoothStep" || Flag(p, "bClampResult"));
            if (t == "InverseLinearInterpolate")
                return ratio;
            auto square = e.op("MUL", size, {ratio, ratio}),
                 slope = e.op("MAD", size, {ratio, Constant({-2}), Constant({3})});
            return e.op("MUL", size, {square, slope});
        }
        if (t == "ConstantBiasScale")
        {
            auto x = input(ins, "Input");
            return e.op(
                "MUL", x.size,
                {e.op("ADD", x.size, {x, Constant({Number(p, "Bias", 1)})}), Constant({Number(p, "Scale", .5)})});
        }
        if (t == "SphereMask")
        {
            auto a = input(ins, "A"), b = input(ins, "B");
            int size = Size({a, b});
            auto delta = e.sub(size, a, b);
            auto dist = size == 1 ? e.abs(delta) : e.sqrt(e.dot(size, delta, delta));
            Value radius, hardness;
            if (Get(ins, "Radius").kind != Json::Null)
            {
                auto r = input(ins, "Radius");
                radius = e.per_component("RCP", e.op("MAX", r.size, {r, Constant({.00001})}));
            }
            else
                radius = Constant({1 / std::max(.00001, Number(p, "AttenuationRadius", 256))});
            if (Get(ins, "Hardness").kind != Json::Null)
            {
                auto h = input(ins, "Hardness");
                auto soft = e.op("ADD", h.size, {e.neg(h), Constant({1})});
                hardness = e.per_component("RCP", e.op("MAX", h.size, {soft, Constant({.00001})}));
            }
            else
                hardness = Constant({1 / std::max(1 - Number(p, "HardnessPercent", 100) * .01, .00001)});
            size = Size({dist, radius, hardness});
            auto near = e.op("ADD", size, {e.neg(e.op("MUL", size, {dist, radius})), Constant({1})});
            return e.op("MUL", size, {near, hardness}, true);
        }
        if (t == "Rotator")
        {
            auto time = Get(ins, "Time").kind == Json::Null ? e.view("game_time", 1) : input(ins, "Time");
            auto angle = e.op("MUL", 1, {time, Constant({Number(p, "Speed", .25)})});
            auto uv = coordinates(ins, p, "Coordinate");
            if (uv.size == 4)
                throw std::runtime_error("Rotator coordinate has four components");
            double cx = Number(p, "CenterX", .5), cy = Number(p, "CenterY", .5);
            auto d = e.op("ADD", 2, {uv, Constant({-cx, -cy})});
            auto cs = e.temp(), row = e.temp(), r = e.temp();
            e.emit("COS.F32 " + cs + ".x, " + angle.text + ".x");
            e.emit("SIN.F32 " + cs + ".y, " + angle.text + ".x");
            e.emit("MOV.F " + row + ".x, " + cs + ".x");
            e.emit("MOV.F " + row + ".y, -" + cs + ".y");
            e.emit("DP2.F32 " + r + ".x, " + row + ", " + d.text);
            e.emit("DP2.F32 " + r + ".y, " + cs + ".yxyx, " + d.text);
            auto turned = e.op("ADD", 2, {{r, 2}, Constant({cx, cy})});
            return uv.size == 3 ? e.append(turned, e.swizzle(uv, "b")) : turned;
        }
        if (t == "RotateAboutAxis")
        {
            auto axis = vec3(input(ins, "NormalizedRotationAxis"));
            double period = Number(p, "Period", 1);
            if (period == 0)
                throw std::runtime_error("RotateAboutAxis period is zero");
            auto angle = e.op("MUL", 1, {input(ins, "RotationAngle"), Constant({6.28318530717958 / period})});
            auto pivot = vec3(input(ins, "PivotPoint")), position = vec3(input(ins, "Position"));
            auto along = e.dot(3, axis, e.sub(3, position, pivot));
            auto closest = e.op("MAD", 3, {axis, along, pivot});
            auto u = e.sub(3, position, closest), v = e.cross(axis, u);
            auto sine = e.per_component("SIN", angle), cosine = e.per_component("COS", angle);
            auto turned = e.op("MAD", 3, {v, sine, e.op("MUL", 3, {u, cosine})});
            return e.sub(3, e.op("ADD", 3, {closest, turned}), position);
        }
        if (t == "BumpOffset")
        {
            auto height = e.op("MOV", 1, {input(ins, "Height")});
            double plane = Number(p, "ReferencePlane", .5);
            Value scale;
            if (Get(ins, "HeightRatioInput").kind != Json::Null)
            {
                auto r = e.op("MOV", 1, {input(ins, "HeightRatioInput")});
                scale = e.op("MAD", 1, {r, height, e.op("MUL", 1, {r, Constant({-plane})})});
            }
            else
            {
                double r = Number(p, "HeightRatio", .05);
                scale = e.op("MAD", 1, {height, Constant({r}), Constant({-plane * r})});
            }
            auto uv = coordinates(ins, p, "Coordinate");
            if (uv.size > 2)
                throw std::runtime_error("BumpOffset requires float2 coordinates");
            return e.op("MAD", 2, {e.camera_tangent(), scale, uv});
        }
        if (t == "DDX" || t == "DDY")
        {
            auto x = input(ins, "Value");
            return e.op(t, x.size, {x});
        }
        if (t == "DeriveNormalZ")
        {
            auto xy = e.op("MOV", 2, {input(ins, "InXY")});
            auto rest = e.op("ADD", 1, {e.neg(e.dot(2, xy, xy)), Constant({1})}, true);
            return e.append(xy, e.sqrt(rest));
        }
        if (t == "TwoSidedSign")
            return e.two_sided_sign();
        if (t == "CameraPositionWS")
            return e.view("world_camera_origin", 3);
        if (t == "ReflectionVectorWS")
        {
            Value normal = Get(ins, "CustomWorldNormal").kind == Json::Null ? e.pixel_normal()
                                                                            : vec3(input(ins, "CustomWorldNormal"));
            if (Get(ins, "CustomWorldNormal").kind != Json::Null && Flag(p, "bNormalizeCustomWorldNormal"))
                normal = e.normalize(normal);
            auto view = e.camera_vector();
            auto twice = e.op("MUL", 1, {e.dot(3, normal, view), Constant({2})});
            return e.op("MAD", 3, {normal, twice, e.neg(view)});
        }
        if (t == "ScreenPosition")
        {
            if (ix == 1)
                return e.pixel_position();
            auto pos = e.pixel_position(), size = e.swizzle(e.view("view_size", 4), "ba");
            return e.op("MUL", 2, {pos, size});
        }
        if (t == "PixelDepth")
        {
            auto d = e.temp();
            e.emit("RCP.F32 " + d + ".x, fragment.position.w");
            return {d, 1};
        }
        if (t == "ViewSize")
            return e.swizzle(e.view("view_size", 4), "rg");
        if (t == "VertexTangentWS")
        {
            auto f = e.tangent_frame();
            return e.op("MOV", 3, {{f[0], 4}});
        }
        if (t == "DeltaTime")
            return e.view("delta_time", 1);
        static const std::map<std::string, std::pair<std::string, int>> primitive = {
            {"ObjectPositionWS", {"object_position", 3}},
            {"ObjectRadius", {"object_radius", 1}},
            {"ObjectBounds", {"object_bounds", 3}},
            {"ObjectOrientation", {"object_orientation", 3}},
            {"ActorPositionWS", {"actor_position", 3}}};
        if (primitive.count(t))
        {
            auto x = primitive.at(t);
            return e.primitive(x.first, x.second);
        }
        throw std::runtime_error("node type " + t + " is unavailable in the native custom material compiler");
    }

public:
    GraphCompiler(Json g, std::map<std::string, std::string> s = {}, bool depth = false)
        : graph(std::move(g)), nodes(&graph.At("nodes")), e(depth, Flag(graph, "two_sided")), slots(std::move(s))
    {
        if (nodes->kind != Json::Object || nodes->object.size() > 100000)
            throw std::runtime_error("invalid graph nodes");
    }
    GraphCompiler(Json g, std::map<std::string, std::string> s, Emitter initial)
        : graph(std::move(g)), nodes(&graph.At("nodes")), e(std::move(initial)), slots(std::move(s))
    {
        if (nodes->kind != Json::Object || nodes->object.size() > 100000)
            throw std::runtime_error("invalid graph nodes");
        if (e.depth || e.two_sided != Flag(graph, "two_sided"))
            throw std::runtime_error("seeded material emitter settings mismatch");
    }
    MaterialInputProgram translucent_inputs()
    {
        auto& outputs = Get(graph, "outputs");
        std::map<std::string, Value> pins;
        pins["EmissiveColor"] = e.op("MAX", 3, {input(outputs, "EmissiveColor", Constant({0, 0, 0})), Constant({0})});
        pins["Opacity"] = e.op("MOV", 1, {input(outputs, "Opacity", Constant({1}))}, true);
        MaterialInputProgram result;
        result.emitter = std::move(e);
        result.pins = std::move(pins);
        result.nodes.assign(used.begin(), used.end());
        return result;
    }
    MaterialInputProgram material_inputs(bool includeOpacity = false)
    {
        auto& outputs = Get(graph, "outputs");
        if (Get(graph, "clip").kind != Json::Null)
            e.clip(input(outputs, "OpacityMask", Constant({1})), Number(graph, "clip"));
        if (Get(outputs, "Normal").kind != Json::Null)
        {
            auto normal = value(Get(outputs, "Normal"));
            e.cache["pn"] = Flag(graph, "tangent_space_normal", true) ? e.tangent_to_world(normal)
                                                                      : e.facing_sign(e.normalize(normal));
        }
        std::map<std::string, Value> pins;
        pins["Normal"] = e.pixel_normal();
        for (const auto& name : {"BaseColor", "Metallic", "Specular", "Roughness", "AmbientOcclusion"})
        {
            int size = std::string(name) == "BaseColor" ? 3 : 1;
            double fallback = std::string(name) == "Specular" || std::string(name) == "Roughness" ? .5
                              : std::string(name) == "AmbientOcclusion"                           ? 1
                                                                                                  : 0;
            auto supplied =
                input(outputs, name, size == 3 ? Constant({fallback, fallback, fallback}) : Constant({fallback}));
            pins[name] = e.op("MOV", size, {supplied}, true);
        }
        pins["EmissiveColor"] = e.op("MOV", 3, {input(outputs, "EmissiveColor", Constant({0, 0, 0}))});
        if (includeOpacity)
            pins["Opacity"] = e.op("MOV", 1, {input(outputs, "Opacity", Constant({1}))}, true);
        MaterialInputProgram result;
        result.emitter = std::move(e);
        result.pins = std::move(pins);
        result.nodes.assign(used.begin(), used.end());
        return result;
    }
    Value value(const Json& link)
    {
        validate_link(link);
        auto id = link.array[0].Text();
        auto out = link.array[1].kind == Json::Null ? "" : link.array[1].Text();
        int ix = index(link);
        auto key = id + "\x1f" + out + "\x1f" + std::to_string(ix);
        if (done.count(key))
            return done[key];
        if (!busy.insert(key).second)
            throw std::runtime_error("nodes form a loop at " + id);
        if (++nesting > 256)
            throw std::runtime_error("material link depth exceeded");
        auto v = node(id, out, ix);
        --nesting;
        busy.erase(key);
        used.insert(id);
        done[key] = v;
        return v;
    }
    CompileResult compile()
    {
        auto& outputs = Get(graph, "outputs");
        bool masked = Get(graph, "clip").kind != Json::Null;
        if (e.depth && !masked)
            throw std::runtime_error("depth program requires masked material");
        if (masked && Get(outputs, "OpacityMask").kind != Json::Null)
            e.clip(value(Get(outputs, "OpacityMask")), Number(graph, "clip"));
        if (e.depth)
        {
            e.emit("MOV.F result_color0, {0, 0, 0, 0}.x");
            return result();
        }
        if (Get(outputs, "BaseColor").kind == Json::Null && Get(outputs, "EmissiveColor").kind == Json::Null)
            throw std::runtime_error("connect BaseColor or EmissiveColor");
        if (Get(outputs, "Normal").kind != Json::Null)
        {
            auto normal = value(Get(outputs, "Normal"));
            e.cache["pn"] = Flag(graph, "tangent_space_normal", true) ? e.tangent_to_world(normal)
                                                                      : e.facing_sign(e.normalize(normal));
        }
        auto colour = Constant({0, 0, 0});
        if (Get(outputs, "BaseColor").kind != Json::Null)
        {
            auto base = value(Get(outputs, "BaseColor"));
            if (base.size < 3)
                base = e.op("MOV", 3, {base});
            auto sun = e.normalize(e.view("sun_direction", 3));
            auto ndl = e.temp();
            auto pn = e.pixel_normal();
            e.emit("DP3.F32.SAT " + ndl + ".x, " + pn.text + ", " + sun.text);
            colour = e.view("sun_color", 3);
            auto peak = e.temp();
            e.emit("MAX.F " + peak + ".x, " + colour.text + ".x, " + colour.text + ".y");
            e.emit("MAX.F " + peak + ".x, " + peak + ".x, " + colour.text + ".z");
            e.emit("MAX.F " + peak + ".x, " + peak + ".x, {0.0001, 0, 0, 0}.x");
            auto tint = e.op("MUL", 3, {colour, e.per_component("RCP", {peak, 1})});
            auto light = e.op("MAD", 3, {tint, {ndl, 1}, Constant({.35, .35, .35})});
            colour = e.op("MUL", 3, {base, light});
        }
        if (Get(outputs, "EmissiveColor").kind != Json::Null)
        {
            auto em = value(Get(outputs, "EmissiveColor"));
            colour = e.op("ADD", 3, {colour, em});
        }
        auto exposure = e.view("pre_exposure", 1);
        e.emit("MUL.F32 result_color0.xyz, " + e.src(colour, 3) + ", " + exposure.text + ".x");
        e.emit("MOV.F result_color0.w, {0, 0, 0, 0}.x");
        return result();
    }
    CompileResult compile_mask()
    {
        e.depth = true;
        return compile();
    }
    CompileResult result()
    {
        CompileResult r;
        r.program = e.program();
        r.tangents = e.cache.count("attrib:in_texcoord10") != 0;
        r.primitive = e.uses_primitive;
        r.nodes.assign(used.begin(), used.end());
        return r;
    }
};
inline CompileResult CompileGraph(const Json& graph, const std::map<std::string, std::string>& slots = {},
                                  bool depth = false, const Json& functions = Json())
{
    return GraphCompiler(InlineFunctions(graph, functions), slots, depth).compile();
}
// A masked graph's mask program (clip, no color) with a seeded emitter, for Kadan's own mask passes whose Material
// buffer holds the material's own parameters (OwnMasked). The emitter is seeded as a material emitter, then compiled
// as a depth program.
inline CompileResult CompileMaskWithEmitter(const Json& graph, Emitter initial,
                                            const std::map<std::string, std::string>& slots = {})
{
    if (initial.depth)
        throw std::runtime_error("seed a mask program with a material emitter");
    return GraphCompiler(InlineFunctions(graph, Json()), slots, std::move(initial)).compile_mask();
}
inline MaterialInputProgram CompileMaterialInputs(const Json& graph,
                                                  const std::map<std::string, std::string>& slots = {},
                                                  const Json& functions = Json())
{
    return GraphCompiler(InlineFunctions(graph, functions), slots, false).material_inputs();
}
inline MaterialInputProgram CompileMaterialInputsWithEmitter(const Json& graph, Emitter initial,
                                                             const std::map<std::string, std::string>& slots = {},
                                                             const Json& functions = Json())
{
    return GraphCompiler(InlineFunctions(graph, functions), slots, std::move(initial)).material_inputs();
}
inline MaterialInputProgram CompileTranslucentLitInputsWithEmitter(const Json& graph, Emitter initial,
                                                                   const std::map<std::string, std::string>& slots = {},
                                                                   const Json& functions = Json())
{
    return GraphCompiler(InlineFunctions(graph, functions), slots, std::move(initial)).material_inputs(true);
}
inline MaterialInputProgram CompileTranslucentInputs(const Json& graph, Emitter initial,
                                                     const std::map<std::string, std::string>& slots = {},
                                                     const Json& functions = Json())
{
    return GraphCompiler(InlineFunctions(graph, functions), slots, std::move(initial)).translucent_inputs();
}
inline std::string Resolve(const std::string& program, const std::map<std::string, std::pair<int, int>>& textures,
                           const std::map<std::string, int>& varyings = {
                               {"in_texcoord10", 0}, {"in_texcoord11", 1}, {"in_texcoord0", 2}})
{
    for (const auto& texture : textures)
        if (texture.second.first < 0 || texture.second.first > 63 || texture.second.second < 0 ||
            texture.second.second > 31)
            throw std::runtime_error("texture/sampler binding outside cache");
    for (const auto& varying : varyings)
        if (varying.second < 0 || varying.second > 127)
            throw std::runtime_error("varying location outside binding range");
    std::regex re("\\{(TEX|SAMP|VAR|LAST):([^{}]*)\\}");
    std::string result;
    std::size_t end = 0;
    for (auto i = std::sregex_iterator(program.begin(), program.end(), re); i != std::sregex_iterator(); ++i)
    {
        auto m = *i;
        result += program.substr(end, m.position() - end);
        auto kind = m[1].str(), name = m[2].str();
        int binding = 0;
        if (kind == "VAR")
        {
            if (!varyings.count(name))
                throw std::runtime_error("shader has no interpolant " + name);
            binding = varyings.at(name);
        }
        else if (kind == "LAST")
        {
            for (auto& varying : varyings)
                binding = std::max(binding, varying.second);
        }
        else
        {
            if (!textures.count(name))
                throw std::runtime_error("shader binds no texture parameter " + name);
            binding =
                (kind == "TEX" ? 328 : 1352) + 8 * (kind == "TEX" ? textures.at(name).first : textures.at(name).second);
        }
        result += std::to_string(binding);
        end = m.position() + m.length();
    }
    result += program.substr(end);
    return result;
}
}
}

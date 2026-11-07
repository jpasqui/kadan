// Portable material expression emitter. The offsets and operations are the
// existing material_graph.py contract; they do not imply complete game lighting.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace P4MapKit
{
namespace CustomMaterial
{
struct Value
{
    std::string text;
    int size;
    bool operator==(const Value& x) const
    {
        return text == x.text && size == x.size;
    }
};
inline std::string Lit(double x)
{
    if (!std::isfinite(x))
        throw std::runtime_error("non-finite material constant");
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::setprecision(9) << x;
    return s.str();
}
inline std::string Comp(int n)
{
    if (n < 1 || n > 4)
        throw std::runtime_error("invalid component count");
    return std::string("xyzw").substr(0, n);
}
inline Value Constant(std::vector<double> x)
{
    if (x.empty() || x.size() > 4)
        throw std::runtime_error("constant needs 1..4 components");
    int n = int(x.size());
    x.resize(4, 0);
    std::string s = "{";
    for (int i = 0; i < 4; i++)
        s += (i ? ", " : "") + Lit(x[i]);
    return {s + "}", n};
}
inline int Size(std::initializer_list<Value> x)
{
    int n = 1;
    for (auto& v : x)
        n = std::max(n, v.size);
    return n;
}
struct Emitter
{
    bool depth = false, two_sided = false, uses_texture = false, uses_primitive = false, clips = false;
    bool masked_material =
        false; // a masked graph admitted by its caller (Kadan's own programs, with their own mask passes)
    std::map<std::string, std::pair<unsigned, int>> material_parameters;
    unsigned material_buffer = 0;
    bool uses_material_parameters = false, owned_uniforms = false;
    int temps = 0;
    std::vector<std::string> lines;
    std::map<std::string, Value> cache;
    std::string handle;
    std::array<std::string, 3> frame;
    explicit Emitter(bool d = false, bool t = false) : depth(d), two_sided(t)
    {
    }
    std::string temp()
    {
        if (++temps > 16384)
            throw std::runtime_error("material register budget exceeded");
        return "V" + std::to_string(temps);
    }
    void emit(const std::string& s)
    {
        if (lines.size() > 100000)
            throw std::runtime_error("material instruction budget exceeded");
        lines.push_back(s + ";");
    }
    static std::string src(Value a, int n)
    {
        if (a.size == 1)
            return a.text + ".x";
        if (a.size < n)
            throw std::runtime_error("insufficient components");
        return a.text;
    }
    Value op(const std::string& name, int n, std::initializer_list<Value> args, bool sat = false)
    {
        auto d = temp();
        std::string s = name + ".F32" + (sat ? ".SAT" : "") + " " + d + (n == 4 ? "" : "." + Comp(n));
        for (auto& a : args)
            s += ", " + src(a, n);
        emit(s);
        return {d, n};
    }
    Value per_component(const std::string& name, Value a)
    {
        auto d = temp();
        for (int i = 0; i < a.size; i++)
            emit(name + ".F32 " + d + "." + Comp(4)[i] + ", " + a.text + "." + Comp(4)[a.size > 1 ? i : 0]);
        return {d, a.size};
    }
    Value swizzle(Value a, std::string channels)
    {
        if (channels.empty() || channels.size() > 4)
            throw std::runtime_error("mask has no channel on");
        std::string sw;
        for (char c : channels)
        {
            auto k = std::string("rgba").find(c);
            if (k == std::string::npos || ((int)k >= a.size && a.size != 1))
                throw std::runtime_error("invalid material channel");
            sw += Comp(4)[a.size == 1 ? 0 : k];
        }
        int n = int(sw.size());
        sw.resize(4, sw.back());
        auto d = temp();
        emit("MOV.F " + d + "." + Comp(n) + ", " + a.text + "." + sw);
        return {d, n};
    }
    Value append(Value a, Value b)
    {
        int n = a.size + b.size;
        if (n > 4)
            throw std::runtime_error("AppendVector makes more than 4 components");
        auto d = temp();
        auto sw = Comp(a.size);
        sw.resize(4, sw.back());
        emit("MOV.F " + d + "." + Comp(a.size) + ", " + a.text + "." + sw);
        sw = "xxxx";
        for (int i = 0; i < b.size; i++)
            sw[a.size + i] = Comp(4)[i];
        emit("MOV.F " + d + "." + Comp(n).substr(a.size) + ", " + b.text + "." + sw);
        return {d, n};
    }
    Value neg(Value a)
    {
        if (a.text[0] == '-')
            return {a.text.substr(1), a.size};
        if (a.text[0] == '{')
            return op("MUL", a.size, {a, Constant({-1})});
        return {"-" + a.text, a.size};
    }
    Value sub(int n, Value a, Value b)
    {
        return op("ADD", n, {a, neg(b)});
    }
    Value abs(Value a)
    {
        return op("MAX", a.size, {a, neg(a)});
    }
    Value sqrt(Value a)
    {
        return per_component("RCP", per_component("RSQ", op("MAX", a.size, {a, Constant({1e-20})})));
    }
    Value dot(int n, Value a, Value b)
    {
        auto d = temp();
        emit((n == 1 ? "MUL" : "DP" + std::to_string(n)) + ".F32 " + d + ".x, " + src(a, n) + ", " + src(b, n));
        return {d, 1};
    }
    Value cross(Value a, Value b)
    {
        auto ta = op("MOV", 3, {a}), tb = op("MOV", 3, {b});
        auto d = temp();
        emit("MUL.F32 " + d + ".xyz, " + ta.text + ".yzxy, " + tb.text + ".zxyy");
        emit("MAD.F32 " + d + ".xyz, -" + ta.text + ".zxyy, " + tb.text + ".yzxy, " + d);
        return {d, 3};
    }
    Value normalize(Value a)
    {
        auto d = temp();
        emit("DP" + std::to_string(a.size) + ".F32 " + d + ".x, " + a.text + ", " + a.text);
        emit("MAX.F " + d + ".x, " + d + ".x, {1e-16, 0, 0, 0}.x");
        emit("RSQ.F32 " + d + ".x, " + d + ".x");
        return op("MUL", a.size, {a, {d, 1}});
    }
    Value view(const std::string& name, int n)
    {
        static const std::map<std::string, int> offsets = {{"sv_to_translated_world", 704},
                                                           {"pre_view_translation", 1120},
                                                           {"pre_exposure", 2180},
                                                           {"game_time", 2440},
                                                           {"sun_color", 2496},
                                                           {"sun_direction", 2512},
                                                           {"world_camera_origin", 1072},
                                                           {"view_rect_min", 2080},
                                                           {"view_size", 2096},
                                                           {"culling_sign", 2428},
                                                           {"delta_time", 2448},
                                                           {"material_texture_mip_bias", 2452}};
        auto key = "view:" + name;
        if (!cache.count(key))
        {
            auto d = temp();
            emit("LDC." +
                 std::string(n == 1   ? "F32"
                             : n == 2 ? "F32X2"
                                      : "F32X4") +
                 " " + d + "." + Comp(n) + ", buf0[" + std::to_string(offsets.at(name)) + "]");
            cache[key] = {d, n};
        }
        return cache[key];
    }
    Value attrib(const std::string& name, int n)
    {
        auto key = "attrib:" + name;
        if (!cache.count(key))
        {
            auto d = temp();
            emit("MOV.F " + d + ", fragment.attrib[{VAR:" + name + "}]");
            cache[key] = {d, 4};
        }
        return {cache[key].text, n};
    }
    Value translated_world()
    {
        if (!cache.count("tw"))
        {
            std::vector<std::string> rows;
            for (int i = 0; i < 4; i++)
            {
                auto r = temp();
                emit("LDC.F32X4 " + r + ", buf0[" + std::to_string(704 + 16 * i) + "]");
                rows.push_back(r);
            }
            auto p = temp();
            emit("MUL.F32 " + p + ", fragment.position.y, " + rows[1]);
            emit("MAD.F32 " + p + ", fragment.position.x, " + rows[0] + ", " + p);
            emit("MAD.F32 " + p + ", fragment.position.z, " + rows[2] + ", " + p);
            emit("ADD.F32 " + p + ", " + p + ", " + rows[3]);
            auto w = temp();
            emit("RCP.F32 " + w + ".x, " + p + ".w");
            emit("MUL.F32 " + p + ".xyz, " + p + ", " + w + ".x");
            cache["tw"] = {p, 3};
        }
        return cache["tw"];
    }
    Value world_position()
    {
        if (!cache.count("wp"))
        {
            auto p = translated_world(), v = view("pre_view_translation", 3);
            cache["wp"] = op("ADD", 3, {p, {"-" + v.text, 3}});
        }
        return cache["wp"];
    }
    Value camera_vector()
    {
        if (!cache.count("cv"))
        {
            auto p = translated_world();
            cache["cv"] = normalize(op("MUL", 3, {p, Constant({-1})}));
        }
        return cache["cv"];
    }
    Value face()
    {
        if (!cache.count("sign"))
        {
            auto t = temp(), s = temp();
            emit("SGT.S " + t + ".x, fragment.facing, {0, 0, 0, 0}");
            emit("MOV.U.CC RC.x, -" + t);
            emit("IF NE.x");
            emit("MOV.S " + t + ".w, {1, 0, 0, 0}.x");
            emit("ELSE");
            emit("MOV.S " + t + ".w, {-1, 0, 0, 0}.x");
            emit("ENDIF");
            emit("I2F.S " + s + ".x, " + t + ".w");
            cache["sign"] = {s, 1};
            clips = true;
        }
        return cache["sign"];
    }
    Value facing_sign(Value a)
    {
        return two_sided ? op("MUL", a.size, {a, two_sided_sign()}) : a;
    }
    Value normal()
    {
        if (depth)
            throw std::runtime_error("depth pass has no normal");
        if (!cache.count("n"))
            cache["n"] = facing_sign(normalize(attrib("in_texcoord11", 3)));
        return cache["n"];
    }
    Value pixel_normal()
    {
        return cache.count("pn") ? cache["pn"] : normal();
    }
    void clip(Value a, double threshold)
    {
        auto t = temp();
        emit("SLT.F32 " + t + ".x, " + src(a, 1) + ", {" + Lit(threshold) + ", 0, 0, 0}");
        emit("TRUNC.U.CC HC.x, " + t);
        emit("IF NE.x");
        emit("MOV.U.CC RC.x, {1, 0, 0, 0}");
        emit("KIL NE.x");
        emit("ENDIF");
        clips = true;
    }
    std::array<std::string, 3> tangent_frame()
    {
        if (depth)
            throw std::runtime_error("depth pass has no tangent frame");
        if (frame[0].empty())
        {
            auto x = attrib("in_texcoord10", 3).text, z = attrib("in_texcoord11", 4).text, t = temp(), b = temp();
            emit("MUL.F32 " + t + ".xyz, " + z + ".zxyw, " + x + ".yzxw");
            emit("MAD.F32 " + b + ".xyz, " + z + ".yzxw, " + x + ".zxyw, -" + t);
            emit("MUL.F32 " + b + ".xyz, " + z + ".w, " + b);
            frame = {{x, b, z}};
        }
        return frame;
    }
    Value tangent_to_world(Value n)
    {
        auto f = tangent_frame();
        if (n.size < 3)
            n = op("MOV", 3, {n});
        auto w = op("MUL", 3, {{f[0], 3}, swizzle(n, "r")});
        w = op("MAD", 3, {{f[1], 3}, swizzle(n, "g"), w});
        w = op("MAD", 3, {{f[2], 3}, swizzle(n, "b"), w});
        return facing_sign(normalize(w));
    }
    Value transform_vector(Value a, std::string source, std::string target)
    {
        if (source == target)
            return op("MOV", 3, {a});
        if (!((source == "WORLD" && target == "TANGENT") || (source == "TANGENT" && target == "WORLD")))
            throw std::runtime_error("unsupported direction transform");
        auto f = tangent_frame();
        if (source == "TANGENT")
        {
            auto r = op("MUL", 3, {{f[0], 3}, swizzle(a, "r")});
            r = op("MAD", 3, {{f[1], 3}, swizzle(a, "g"), r});
            return op("MAD", 3, {{f[2], 3}, swizzle(a, "b"), r});
        }
        auto x = dot(3, a, {f[0], 3}), b = dot(3, a, {f[1], 3}), z = dot(3, a, {f[2], 3});
        return append(append(x, b), z);
    }
    Value unpack_normal(Value a)
    {
        auto d = temp(), t = temp();
        emit("MAD.F32 " + d + ".xy, " + a.text + ", {2, -1, 0, 0}.x, {2, -1, 0, 0}.y");
        emit("DP2.F32 " + t + ".x, " + d + ", " + d);
        emit("ADD.F32.SAT " + t + ".x, -" + t + ".x, {1, 0, 0, 0}.x");
        emit("RSQ.F32 " + t + ".x, " + t + ".x");
        emit("RCP.F32 " + d + ".z, " + t + ".x");
        emit("MOV.F " + d + ".w, {1, 0, 0, 0}.x");
        return {d, 4};
    }
    Value uv(int index = 0)
    {
        if (!index)
            return attrib("in_texcoord0", 2);
        if (index == 1 && !depth)
            return swizzle(attrib("in_texcoord0", 4), "ba");
        throw std::runtime_error("texture coordinate is unavailable in this pass");
    }
    Value sample(Value a, const std::string& parameter, bool viewMipBias = false)
    {
        if (handle != parameter)
        {
            uses_texture = true;
            emit("LDC.U64 D1.x, buf14[{SAMP:" + parameter + "}]");
            emit("LDC.U64 D0.x, buf14[{TEX:" + parameter + "}]");
            emit("OR.U64 D0.x, D0, D1");
            handle = parameter;
        }
        auto d = temp();
        if (viewMipBias)
        {
            auto bias = view("material_texture_mip_bias", 1);
            auto uv = append(swizzle(a, "rg"), bias);
            emit("TXB.F " + d + ", " + uv.text + ".xyzz, handle(D0.x), 2D");
        }
        else
            emit("TEX.F " + d + ", " + (a.size == 2 ? a.text : src(a, 2)) + ", handle(D0.x), 2D");
        return {d, 4};
    }
    Value parameter(const std::string& name, int size)
    {
        auto binding = material_parameters.find(name);
        if (binding == material_parameters.end() || binding->second.second < size || material_buffer > 14)
            throw std::runtime_error("material parameter binding absent");
        auto key = "parameter:" + name;
        if (!cache.count(key))
        {
            auto d = temp();
            auto width = binding->second.second;
            uses_material_parameters = true;
            emit("LDC." + std::string(width == 1 ? "F32" : "F32X4") + " " + d + "." + Comp(width) + ", buf" +
                 std::to_string(material_buffer) + "[" + std::to_string(binding->second.first) + "]");
            cache[key] = {d, width};
        }
        return {cache[key].text, size};
    }
    Value primitive(const std::string& name, int n)
    {
        if (depth)
            throw std::runtime_error("depth pass has no per-object data");
        static const std::map<std::string, int> offsets = {{"determinant_sign", 76}, {"object_position", 80},
                                                           {"object_radius", 92},    {"actor_position", 288},
                                                           {"object_bounds", 304},   {"object_orientation", 336}};
        auto key = "primitive:" + name;
        if (!cache.count(key))
        {
            auto d = temp();
            emit("LDC." + std::string(n == 1 ? "F32" : "F32X4") + " " + d + "." + Comp(n) + ", buf1[" +
                 std::to_string(offsets.at(name)) + "]");
            cache[key] = {d, n};
            uses_primitive = true;
        }
        return cache[key];
    }
    Value two_sided_sign()
    {
        if (!two_sided)
            return Constant({1});
        if (!cache.count("tss"))
        {
            auto v = view("culling_sign", 1), p = primitive("determinant_sign", 1);
            auto flip = op("MUL", 1, {v, p});
            cache["tss"] = op("MUL", 1, {flip, face()});
        }
        return cache["tss"];
    }
    Value camera_tangent()
    {
        if (!cache.count("cvt"))
        {
            auto f = tangent_frame();
            auto v = camera_vector();
            auto d = temp();
            emit("DP3.F32 " + d + ".x, " + f[0] + ", " + v.text);
            emit("DP3.F32 " + d + ".y, " + f[1] + ", " + v.text);
            cache["cvt"] = {d, 2};
        }
        return cache["cvt"];
    }
    Value pixel_position()
    {
        if (!cache.count("pp"))
            cache["pp"] = sub(2, {"fragment.position", 4}, view("view_rect_min", 2));
        return cache["pp"];
    }
    Value poly(int n, Value x, const std::vector<double>& c)
    {
        auto a = Constant({c[0]});
        for (std::size_t i = 1; i < c.size(); i++)
            a = op("MAD", n, {a, x, Constant({c[i]})});
        return a;
    }
    Value flipped(int n, Value m, Value a, double about)
    {
        auto v = op("MAD", n, {a, Constant({-2}), Constant({about})});
        return op("MAD", n, {m, v, a});
    }
    Value negated(int n, Value m, Value a)
    {
        return op("MUL", n, {a, op("MAD", n, {m, Constant({-2}), Constant({1})})});
    }
    Value acos(Value x, bool fast = false)
    {
        auto a = abs(x);
        auto root = sqrt(op("ADD", x.size, {neg(a), Constant({1})}, true));
        auto p = poly(x.size, a,
                      fast ? std::vector<double>{-.156583, 1.5707963267949}
                           : std::vector<double>{-.0012624911, .0066700901, -.0170881256, .0308918810, -.0501743046,
                                                 .0889789874, -.2145988016, 1.5707963050});
        auto r = op("MUL", x.size, {p, root});
        return flipped(x.size, op("SLT", x.size, {x, Constant({0})}), r, 3.14159265358979);
    }
    Value atan_ratio(int n, Value small, Value big, bool fast)
    {
        auto t = op("MUL", n, {small, per_component("RCP", big)});
        auto p = poly(n, op("MUL", n, {t, t}),
                      fast ? std::vector<double>{.0872929, -.301895, 1}
                           : std::vector<double>{.0028662257, -.0161657367, .0429096138, -.0752896400, .1065626393,
                                                 -.1420889944, .1999355085, -.3333314528, 1});
        return op("MUL", n, {p, t});
    }
    Value atan(Value x, bool fast = false)
    {
        auto a = abs(x), one = Constant({1});
        auto small = op("MIN", x.size, {a, one}), big = op("MAX", x.size, {a, one});
        auto p = atan_ratio(x.size, small, big, fast);
        p = flipped(x.size, op("SGE", x.size, {a, one}), p, 1.5707963267949);
        return negated(x.size, op("SLT", x.size, {x, Constant({0})}), p);
    }
    Value atan2(Value y, Value x, bool fast = false)
    {
        int n = Size({x, y});
        auto ax = abs(x), ay = abs(y);
        auto big = op("MAX", n, {op("MAX", n, {ax, ay}), Constant({1e-30})});
        auto t = atan_ratio(n, op("MIN", n, {ax, ay}), big, fast);
        t = flipped(n, op("SGT", n, {ay, ax}), t, 1.5707963267949);
        t = flipped(n, op("SLT", n, {x, Constant({0})}), t, 3.14159265358979);
        return negated(n, op("SLT", n, {y, Constant({0})}), t);
    }
    std::string program() const
    {
        std::string s =
            "!!NVfp5.0\nOPTION NV_internal;\nOPTION NV_bindless_texture;\nCBUFFER buf0[] = { program.buffer[0] };\n";
        if (uses_material_parameters && material_buffer != 0 && material_buffer != 14 &&
            !(material_buffer == 1 && uses_primitive))
            s += "CBUFFER buf" + std::to_string(material_buffer) + "[] = { program.buffer[" +
                 std::to_string(material_buffer) + "] };\n";
        if (uses_primitive)
            s += "CBUFFER buf1[] = { program.buffer[1] };\n";
        if (uses_texture)
            s += "CBUFFER buf14[] = { program.buffer[14] };\n";
        s +=
            "ATTRIB fragment_position = fragment.position;\nATTRIB fragment_attrib[] = { fragment.attrib[0..{LAST:}] };\n";
        if (temps > 0)
        {
            s += "TEMP ";
            for (int i = 1; i <= temps; i++)
                s += (i > 1 ? ", " : "") + std::string("V") + std::to_string(i);
            s += ";\n";
        }
        if (uses_texture)
            s += "LONG TEMP D0, D1;\n";
        if (clips)
            s += "TEMP RC;\nSHORT TEMP HC;\n";
        s += "OUTPUT result_color0 = result.color;\n";
        for (auto& l : lines)
            s += l + "\n";
        return s + "END\n";
    }
};
}
}

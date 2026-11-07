// Uniform order model (part of material_uniform_order.h): the translator from a graph to the uniform
// expressions and textures UE's compiler gives it, and the predicted uniform set.
#pragma once
#include "material_uniform_order_graph.h"
namespace P4MapKit
{
namespace Material
{
// ----- the translator -----

namespace UniformOrder
{
enum : int
{
    F = 15,
    F1 = 1,
    F2 = 2,
    F3 = 4,
    F4 = 8
};
inline int VectorType(int n)
{
    return n == 1 ? F1 : n == 2 ? F2 : n == 3 ? F3 : n == 4 ? F4 : -1;
}
inline int Components(int t)
{
    return t == F || t == F1 ? 1 : t == F2 ? 2 : t == F3 ? 3 : t == F4 ? 4 : 0;
}
inline int ArithType(int a, int b)
{
    if (a == b)
        return a;
    if (a == F)
        return b;
    if (b == F)
        return a;
    return std::max(a, b);
}
struct Unsupported : std::runtime_error
{
    explicit Unsupported(const std::string& kind) : std::runtime_error(kind)
    {
    }
};
// A graph the Python model cannot read at all (it would stop with an error), as opposed to Unsupported.
struct ModelError : std::runtime_error
{
    explicit ModelError(const std::string& why) : std::runtime_error(why)
    {
    }
};

enum class ChunkKind
{
    Uniform,
    Const,
    Code,
    Texture
};
struct Chunk
{
    ChunkKind Kind = ChunkKind::Code;
    std::string Text;
    int Type = F;
    bool HasValue = false;
    std::vector<double> Value;
    std::string TextureKind; // a texture's type ('Standard2D', 'Cube')
    bool NameNone = true;
    std::string Name;
    std::string Key; // a texture's identity: its parameter name, or (None, Texture, SamplerSource)
    Json Origin;     // the graph link whose value it is, null when not set
    bool IsUniform() const
    {
        return Kind == ChunkKind::Uniform || Kind == ChunkKind::Const;
    }
};
using ChunkPtr = std::shared_ptr<Chunk>;
inline ChunkPtr Make(ChunkKind kind, const std::string& text = "", int type = F)
{
    auto c = std::make_shared<Chunk>();
    c->Kind = kind;
    c->Text = text;
    c->Type = type;
    return c;
}
inline ChunkPtr Const(const std::vector<double>& values, int t)
{
    std::vector<double> vals = values;
    while (vals.size() < 4)
        vals.push_back(0.0);
    if (t == F)
        vals = std::vector<double>(4, values.at(0));
    std::string text = "(";
    for (std::size_t i = 0; i < vals.size(); ++i)
        text += (i ? ", " : "") + PyG(vals[i]);
    text += ")";
    auto c = Make(ChunkKind::Const, text, t);
    c->HasValue = true;
    c->Value = values;
    return c;
}
inline bool Fold(char op, const ChunkPtr& a, const ChunkPtr& b, std::vector<double>& out)
{
    if (!a->HasValue || !b->HasValue)
        return false;
    auto x = a->Value, y = b->Value;
    if (x.size() != y.size() && !(x.size() == 1 || y.size() == 1))
        return false;
    const auto n = std::max(x.size(), y.size());
    if (x.size() == 1)
        x = std::vector<double>(n, x[0]);
    if (y.size() == 1)
        y = std::vector<double>(n, y[0]);
    out.clear();
    for (std::size_t i = 0; i < std::min(x.size(), y.size()); ++i)
    {
        const double p = x[i], q = y[i];
        out.push_back(op == '+' ? p + q : op == '-' ? p - q : op == '*' ? p * q : (q != 0.0 ? p / q : 0.0));
    }
    return true;
}

inline const std::vector<std::string>& TextureOutputs()
{
    static const std::vector<std::string> v = {"RGB", "R", "G", "B", "A", "RGBA"};
    return v;
}
inline const std::vector<std::string>* OutputMasks(const std::string& type)
{
    static const std::vector<std::string> parameter = {"RGB", "R", "G", "B", "A"};
    if (type == "VectorParameter" || type == "VertexColor" || type == "ParticleColor")
        return &parameter;
    if (type == "TextureSample" || type == "TextureSampleParameter2D" || type == "TextureSampleParameterCube")
        return &TextureOutputs();
    return nullptr;
}
inline int CodeType(const std::string& kind)
{
    static const std::map<std::string, int> types = {{"CameraVectorWS", F3},
                                                     {"PixelNormalWS", F3},
                                                     {"VertexNormalWS", F3},
                                                     {"ReflectionVectorWS", F3},
                                                     {"WorldPosition", F3},
                                                     {"CameraPositionWS", F3},
                                                     {"ObjectPositionWS", F3},
                                                     {"ActorPositionWS", F3},
                                                     {"ParticlePositionWS", F3},
                                                     {"ObjectOrientation", F3},
                                                     {"SkyAtmosphereLightDirection", F3},
                                                     {"SkyAtmosphereLightDiskLuminance", F3},
                                                     {"VertexColor", F4},
                                                     {"ParticleColor", F4},
                                                     {"DynamicParameter", F4},
                                                     {"ScreenPosition", F2},
                                                     {"ViewSize", F2},
                                                     {"TextureCoordinate", F2},
                                                     {"Time", F},
                                                     {"PixelDepth", F},
                                                     {"SceneDepth", F},
                                                     {"EyeAdaptation", F},
                                                     {"TwoSidedSign", F},
                                                     {"ObjectRadius", F},
                                                     {"PrecomputedAOMask", F},
                                                     {"PerInstanceCustomData", F},
                                                     {"SceneColor", F3},
                                                     {"SceneTexture", F4},
                                                     {"ParticleSubUVProperties", F4},
                                                     {"ReflectionCapturePassSwitch", F},
                                                     {"TransformPosition", F3}};
    const auto found = types.find(kind);
    return found == types.end() ? -1 : found->second;
}
inline const char* Unary(const std::string& kind)
{
    static const std::map<std::string, const char*> names = {
        {"Saturate", "saturate"}, {"Floor", "floor"},     {"Ceil", "ceil"}, {"Frac", "frac"},
        {"Abs", "abs"},           {"SquareRoot", "sqrt"}, {"Sign", "sign"}, {"Logarithm2", "log2"},
        {"Round", "round"},       {"Truncate", "trunc"}};
    const auto found = names.find(kind);
    return found == names.end() ? nullptr : found->second;
}
inline char BinaryNode(const std::string& kind)
{
    return kind == "Add" ? '+' : kind == "Subtract" ? '-' : kind == "Multiply" ? '*' : kind == "Divide" ? '/' : 0;
}
inline int PropertyType(const std::string& name)
{
    static const std::map<std::string, int> types = {{"Normal", F3},
                                                     {"EmissiveColor", F3},
                                                     {"BaseColor", F3},
                                                     {"Metallic", F1},
                                                     {"Specular", F1},
                                                     {"Roughness", F1},
                                                     {"Anisotropy", F1},
                                                     {"Opacity", F1},
                                                     {"OpacityMask", F1},
                                                     {"Tangent", F3},
                                                     {"WorldPositionOffset", F3},
                                                     {"SubsurfaceColor", F3},
                                                     {"ClearCoat", F1},
                                                     {"ClearCoatRoughness", F1},
                                                     {"AmbientOcclusion", F1},
                                                     {"Refraction", F2},
                                                     {"PixelDepthOffset", F1}};
    const auto found = types.find(name);
    return found == types.end() ? F2 : found->second;
}
inline std::vector<double> PropertyDefault(const std::string& name)
{
    static const std::map<std::string, std::vector<double>> values = {{"Normal", {0, 0, 1}},
                                                                      {"EmissiveColor", {0, 0, 0}},
                                                                      {"BaseColor", {0, 0, 0}},
                                                                      {"Metallic", {0}},
                                                                      {"Specular", {0.5}},
                                                                      {"Roughness", {0.5}},
                                                                      {"Anisotropy", {0}},
                                                                      {"Opacity", {1}},
                                                                      {"OpacityMask", {1}},
                                                                      {"Tangent", {1, 0, 0}},
                                                                      {"WorldPositionOffset", {0, 0, 0}},
                                                                      {"SubsurfaceColor", {1, 1, 1}},
                                                                      {"ClearCoat", {1}},
                                                                      {"ClearCoatRoughness", {0.1}},
                                                                      {"AmbientOcclusion", {1}},
                                                                      {"Refraction", {1, 0}},
                                                                      {"PixelDepthOffset", {0}}};
    const auto found = values.find(name);
    return found == values.end() ? std::vector<double>{0, 0} : found->second;
}
inline bool InSet(const std::string& v, std::initializer_list<const char*> set)
{
    for (const auto s : set)
        if (v == s)
            return true;
    return false;
}
inline bool Subsurface(const std::string& s)
{
    return InSet(s, {"MSM_Subsurface", "MSM_PreintegratedSkin", "MSM_SubsurfaceProfile", "MSM_TwoSidedFoliage"});
}
inline bool TranslucentBlend(const std::string& b)
{
    return InSet(
        b, {"BLEND_Translucent", "BLEND_Additive", "BLEND_Modulate", "BLEND_AlphaComposite", "BLEND_AlphaHoldout"});
}

// A prop as Python's p.get(key, default) reads a number.
inline double PropNumber(const Json& props, const std::string& key, double fallback)
{
    if (!props.Has(key))
        return fallback;
    const auto& v = props.At(key);
    if (v.kind == Json::Number)
        return v.number;
    if (v.kind == Json::Boolean)
        return v.boolean ? 1.0 : 0.0;
    throw ModelError("prop " + key + " is not a number");
}
inline std::string PropText(const Json& props, const std::string& key, const std::string& fallback,
                            bool* none = nullptr)
{
    if (none)
        *none = false;
    if (!props.Has(key))
        return fallback;
    const auto& v = props.At(key);
    if (v.kind == Json::String)
        return v.string;
    if (v.kind == Json::Null)
    {
        if (none)
            *none = true;
        return std::string();
    }
    throw ModelError("prop " + key + " is not text");
}
// Python truthiness of a prop value.
inline bool PropTruthy(const Json& props, const std::string& key)
{
    if (!props.Has(key))
        return false;
    const auto& v = props.At(key);
    switch (v.kind)
    {
        case Json::Null:
            return false;
        case Json::Boolean:
            return v.boolean;
        case Json::Number:
            return v.number != 0;
        case Json::String:
            return !v.string.empty();
        case Json::Array:
            return !v.array.empty();
        case Json::Object:
            return !v.object.empty();
    }
    return false;
}
// value in (True, 'True', 'true', 1)
inline bool SwitchOn(const Json& v)
{
    return (v.kind == Json::Boolean && v.boolean) || (v.kind == Json::Number && v.number == 1.0) ||
           (v.kind == Json::String && (v.string == "True" || v.string == "true"));
}
// A texture's identity key: its name, or the tuple (None, Texture, SamplerSource).
inline std::string TextureKey(const Json& props)
{
    const auto part = [&](const char* key)
    {
        return props.Has(key) ? props.At(key).Dump() : std::string("null");
    };
    return "T\x1f" + part("Texture") + "\x1f" + part("SamplerSource");
}

struct TextureName
{
    bool None = true;
    std::string Name;
    bool operator==(const TextureName& o) const
    {
        return None == o.None && (None || Name == o.Name);
    }
};
struct Prediction
{
    std::vector<std::string> Vectors, Scalars;
    std::vector<std::pair<std::string, std::vector<TextureName>>> Textures; // by kind, in first-access order
    std::map<std::string, Json> Origins; // uniform text -> its first link (null: none)
    const std::vector<TextureName>* TexturesOf(const std::string& kind) const
    {
        for (const auto& item : Textures)
            if (item.first == kind)
                return &item.second;
        return nullptr;
    }
};

class Translator
{
public:
    const CanonGraph& Graph;
    std::string Frequency = "pixel";
    int Texcoords = 0;
    std::vector<std::string> Scalars, Vectors;
    std::vector<std::pair<std::string, std::vector<TextureName>>> Textures;
    std::map<std::string, std::vector<std::string>> TextureKeys;
    std::map<std::string, Json> Origins;
    std::map<std::string, ChunkPtr> Cache;
    explicit Translator(const CanonGraph& graph) : Graph(graph)
    {
    }

    void Access(const ChunkPtr& c)
    {
        if (!c)
            return;
        if (c->Kind == ChunkKind::Uniform)
        {
            auto& target = c->Type == F ? Scalars : Vectors;
            if (std::find(target.begin(), target.end(), c->Text) == target.end())
            {
                target.push_back(c->Text);
                Origins[c->Text] = c->Origin;
            }
        }
        else if (c->Kind == ChunkKind::Texture)
        {
            auto& keys = TextureKeys[c->TextureKind];
            if (std::find(keys.begin(), keys.end(), c->Key) == keys.end())
            {
                keys.push_back(c->Key);
                std::vector<TextureName>* list = nullptr;
                for (auto& item : Textures)
                    if (item.first == c->TextureKind)
                        list = &item.second;
                if (!list)
                {
                    Textures.emplace_back(c->TextureKind, std::vector<TextureName>());
                    list = &Textures.back().second;
                }
                TextureName name;
                name.None = c->NameNone;
                name.Name = c->Name;
                list->push_back(name);
            }
        }
    }
    void Printf(std::initializer_list<ChunkPtr> args)
    {
        std::vector<ChunkPtr> all(args);
        for (auto i = all.rbegin(); i != all.rend(); ++i)
            Access(*i);
    }
    static const Chunk& Need(const ChunkPtr& c)
    {
        if (!c)
            throw ModelError("an input the model needs is not connected");
        return *c;
    }

    ChunkPtr Binary(char op, const ChunkPtr& a, const ChunkPtr& b)
    {
        Access(a);
        Access(b);
        const int t = ArithType(Need(a).Type, Need(b).Type);
        if (a->IsUniform() && b->IsUniform())
        {
            auto c =
                Make(a->Kind == ChunkKind::Const && b->Kind == ChunkKind::Const ? ChunkKind::Const : ChunkKind::Uniform,
                     "(" + a->Text + " " + op + " " + b->Text + ")", t);
            c->HasValue = Fold(op, a, b, c->Value);
            return c;
        }
        return Make(ChunkKind::Code, "", t);
    }
    ChunkPtr Call(const std::string& name, const std::vector<ChunkPtr>& args, int t)
    {
        for (auto i = args.rbegin(); i != args.rend(); ++i)
            Access(*i);
        bool uniform = true, constant = true;
        for (const auto& a : args)
        {
            uniform &= Need(a).IsUniform();
            constant &= a->Kind == ChunkKind::Const;
        }
        if (uniform)
        {
            std::string text = name + "(";
            for (std::size_t i = 0; i < args.size(); ++i)
                text += (i ? ", " : "") + args[i]->Text;
            return Make(constant ? ChunkKind::Const : ChunkKind::Uniform, text + ")", t);
        }
        return Make(ChunkKind::Code, "", t);
    }
    ChunkPtr Mask(const ChunkPtr& c, const std::string& chans)
    {
        if (chans.empty())
            return c;
        if (Need(c).Kind == ChunkKind::Texture)
            return c;
        const int n = int(chans.size());
        const int t = n == 1 ? F : VectorType(n);
        Access(c);
        if (c->IsUniform())
        {
            std::string letters;
            for (const char ch : chans)
                letters += c->Type == F ? 'r' : char(std::tolower(static_cast<unsigned char>(ch)));
            auto m = Make(c->Kind, c->Text + "." + letters, t);
            if (c->HasValue)
            {
                auto vals = c->Value;
                for (int i = 0; i < 4; ++i)
                    vals.push_back(0.0);
                m->HasValue = true;
                for (const char ch : chans)
                {
                    const auto at = std::string("RGBA").find(ch);
                    if (at == std::string::npos)
                        throw ModelError("channel outside RGBA");
                    m->Value.push_back(c->Type == F ? vals[0] : vals[at]);
                }
            }
            return m;
        }
        return Make(ChunkKind::Code, "", t);
    }
    ChunkPtr ForceCast(const ChunkPtr& c, int t)
    {
        if (Need(c).Kind == ChunkKind::Uniform)
        {
            Access(c);
            return Make(ChunkKind::Code, "", t);
        }
        return c;
    }

    static std::string Key(const std::string& freq, const std::string& id, const std::string& rest)
    {
        return freq + '\x1f' + id + '\x1f' + rest;
    }
    static std::string IndexText(double index)
    {
        return PyG(index);
    }
    const CanonNode& NodeAt(const std::string& id) const
    {
        const auto* n = Graph.Nodes.Find(id);
        if (!n)
            throw ModelError("a link to a missing node " + id);
        return *n;
    }

    ChunkPtr NodeValue(const std::string& id, double index)
    {
        const auto key = Key(Frequency, id, IndexText(index));
        const auto found = Cache.find(key);
        if (found != Cache.end())
            return found->second;
        auto c = Compile(id, index);
        if (c && c->Origin.kind == Json::Null)
        {
            c->Origin = index != 0 ? Json(std::vector<Json>{Json(id), Json(""), Json(index)}) : MakeLink(id, "");
        }
        Cache[key] = c;
        return c;
    }
    ChunkPtr Pin(const std::string& id, double index, const std::string& chans)
    {
        auto c = NodeValue(id, index);
        if (chans.empty())
            return c;
        auto m = Mask(c, chans);
        if (m->Origin.kind == Json::Null)
            m->Origin = index != 0 ? Json(std::vector<Json>{Json(id), Json(chans), Json(index)}) : MakeLink(id, chans);
        return m;
    }
    ChunkPtr Whole(const std::string& id, double index)
    {
        const auto* outputs = OutputMasks(NodeAt(id).Type);
        if (index != 0 || !outputs || (*outputs)[0].empty() || (*outputs)[0] == "RGBA")
            return Pin(id, index,
                       outputs && index >= 0 && index < double(outputs->size()) ? (*outputs)[std::size_t(index)]
                                                                                : std::string());
        const auto at = [&](const char* name)
        {
            return double(std::find(outputs->begin(), outputs->end(), name) - outputs->begin());
        };
        if (std::find(outputs->begin(), outputs->end(), "RGBA") != outputs->end())
            return Pin(id, at("RGBA"), "RGBA");
        if (std::find(outputs->begin(), outputs->end(), "RGB") != outputs->end() &&
            std::find(outputs->begin(), outputs->end(), "A") != outputs->end())
        {
            const auto key = Key(Frequency, id, "all");
            const auto found = Cache.find(key);
            if (found != Cache.end())
                return found->second;
            auto a = Pin(id, at("RGB"), "RGB");
            auto b = Pin(id, at("A"), "A");
            auto c = Append(a, b);
            Cache[key] = c;
            return c;
        }
        return Pin(id, index, (*outputs)[0]);
    }
    ChunkPtr Link(const Json& link)
    {
        if (!LinkNamesNode(link))
            return nullptr;
        const auto id = LinkNode(link);
        const auto chans = LinkChannels(link);
        const double index = LinkIndex(link);
        if (chans.empty())
            return Whole(id, index);
        std::string ordered;
        for (const char c : std::string("RGBA"))
            if (chans.find(c) != std::string::npos)
                ordered += c;
        if (chans != ordered)
        {
            const auto key = Key(Frequency, id, IndexText(index) + "\x1fswizzle\x1f" + chans);
            const auto found = Cache.find(key);
            if (found != Cache.end())
                return found->second;
            ChunkPtr acc;
            for (const char ch : chans)
            {
                auto part = Link(Json(std::vector<Json>{Json(id), Json(std::string(1, ch)), Json(index)}));
                acc = acc ? Append(acc, part) : part;
            }
            Cache[key] = acc;
            return acc;
        }
        static const std::vector<std::string> plain = {"", "R", "G", "B", "A"};
        const auto* masks = OutputMasks(NodeAt(id).Type);
        const auto& outputs = masks ? *masks : plain;
        const auto position = std::find(outputs.begin(), outputs.end(), chans);
        if (index == 0 && position != outputs.end())
            return Pin(id, double(position - outputs.begin()), chans);
        if (index != 0 && index > 0 && index < double(outputs.size()) && outputs[std::size_t(index)] == chans)
            return Pin(id, index, chans);
        const auto key = Key(Frequency, id, IndexText(index) + "\x1fmask\x1f" + chans);
        const auto found = Cache.find(key);
        if (found != Cache.end())
            return found->second;
        auto c = Mask(Whole(id, index), chans);
        Cache[key] = c;
        return c;
    }
    ChunkPtr Append(const ChunkPtr& a, const ChunkPtr& b)
    {
        const int n = Components(Need(a).Type) + Components(Need(b).Type);
        Access(b);
        Access(a);
        const int t = VectorType(n) < 0 ? F4 : VectorType(n);
        if (a->IsUniform() && b->IsUniform())
            return Make(a->Kind == ChunkKind::Const && b->Kind == ChunkKind::Const ? ChunkKind::Const
                                                                                   : ChunkKind::Uniform,
                        "append" + std::to_string(Components(a->Type)) + "(" + a->Text + ", " + b->Text + ")", t);
        return Make(ChunkKind::Code, "", t);
    }
    ChunkPtr Inp(const CanonNode& node, const std::string& name)
    {
        const auto* link = node.Inputs.Find(name);
        if (link && LinkNamesNode(*link))
            return Link(*link);
        return nullptr;
    }
    ChunkPtr Texcoord(double index)
    {
        if (Frequency == "pixel")
            Texcoords = std::max(Texcoords, int(index) + 1);
        return Make(ChunkKind::Code, "", F2);
    }
    ChunkPtr Dot(const ChunkPtr& a, const ChunkPtr& b)
    {
        if (Need(a).IsUniform() && Need(b).IsUniform())
        {
            const auto kind =
                a->Kind == ChunkKind::Uniform || b->Kind == ChunkKind::Uniform ? ChunkKind::Uniform : ChunkKind::Const;
            Printf({a, b});
            if (a->Type == F && b->Type == F)
                return Make(kind, "(" + a->Text + " * " + b->Text + ")", F);
            return Make(kind, "dot(" + a->Text + ", " + b->Text + ")", F);
        }
        Printf({a, b});
        return Make(ChunkKind::Code, "", F);
    }
    ChunkPtr Custom(const CanonNode& node, const std::string& kind, const Json& p)
    {
        const auto code = p.Has("Code") && p.At("Code").kind == Json::String ? p.At("Code").string : std::string();
        std::vector<std::string> names;
        if (p.Has("Inputs") && p.At("Inputs").kind == Json::Array)
            for (const auto& item : p.At("Inputs").array)
                names.push_back(item.Has("InputName") && item.At("InputName").kind == Json::String
                                    ? item.At("InputName").string
                                    : std::string());
        const auto output = PropText(p, "OutputType", "CMOT_Float1");
        const int type = output == "CMOT_Float2" ? F2 : output == "CMOT_Float3" ? F3 : output == "CMOT_Float4" ? F4 : F;
        if (kind == "KadanGameCustom")
        {
            const bool stencil = code.find("SceneStencilTexture") != std::string::npos;
            const bool connected = (code.find("SceneColorCopyTexture") != std::string::npos && !stencil) ||
                                   (code.find("ResolvedView.C4GlobalClippingPlane") != std::string::npos && !stencil);
            if (!connected)
                return Const(std::vector<double>(std::size_t(Components(type)), 0.0), type);
        }
        std::vector<ChunkPtr> compiled;
        for (const auto& name : names)
            if (!name.empty())
                compiled.push_back(Inp(node, name));
        for (const auto& c : compiled)
            Access(c);
        return Make(ChunkKind::Code, "", type);
    }
    template <class F_>
    static ChunkPtr Or(ChunkPtr c, F_ fallback)
    {
        return c ? c : fallback();
    }

    ChunkPtr Compile(const std::string& id, double /*index*/)
    {
        const auto& node = NodeAt(id);
        const auto& kind = node.Type;
        const auto& p = node.Props;
        if (const char op = BinaryNode(kind))
        {
            auto a =
                Or(Inp(node, "A"), [&]
                   { return Const({PropNumber(p, "ConstA", kind == "Add" || kind == "Subtract" ? 0.0 : 1.0)}, F); });
            auto b = Or(Inp(node, "B"), [&] { return Const({PropNumber(p, "ConstB", 1.0)}, F); });
            return Binary(op, a, b);
        }
        if (kind == "OneMinus")
        {
            auto one = Const({1.0}, F);
            return Binary('-', one, Inp(node, "Input"));
        }
        if (kind == "Constant")
            return Const({PropNumber(p, "R", 0.0)}, F);
        if (kind == "Constant2Vector")
            return Const({PropNumber(p, "R", 0.0), PropNumber(p, "G", 0.0)}, F2);
        if (kind == "Constant3Vector" || kind == "Constant4Vector")
        {
            const std::size_t n = kind == "Constant3Vector" ? 3 : 4;
            std::vector<double> c;
            if (!p.Has("Constant"))
                c = std::vector<double>(n, 0.0);
            else
            {
                const auto& v = p.At("Constant");
                if (v.kind == Json::Object)
                    for (std::size_t i = 0; i < n; ++i)
                        c.push_back(PropNumber(v, std::string(1, "RGBA"[i]), 0.0));
                else if (v.kind == Json::Array)
                    for (std::size_t i = 0; i < n && i < v.array.size(); ++i)
                        c.push_back(v.array[i].Numeric());
                else
                    throw ModelError("constant vector is not a list");
            }
            return Const(c, n == 3 ? F3 : F4);
        }
        if (kind == "ScalarParameter")
            return Make(ChunkKind::Uniform, PropText(p, "ParameterName", ""), F);
        if (kind == "VectorParameter")
            return Make(ChunkKind::Uniform, PropText(p, "ParameterName", ""), F4);
        if (kind == "StaticSwitchParameter" || kind == "StaticSwitch")
        {
            const Json value = p.Has("DefaultValue") ? p.At("DefaultValue") : Json(false);
            return Inp(node, SwitchOn(value) ? "A" : "B");
        }
        if (const char* name = Unary(kind))
        {
            auto x = Or(Inp(node, "Input"), [&] { return Inp(node, "X"); });
            return Call(name, {x}, Need(x).Type);
        }
        if (kind == "Sine" || kind == "Cosine")
        {
            auto x = Inp(node, "Input");
            const double period = PropNumber(p, "Period", 1.0);
            if (period != 0 && period > 0)
                x = Binary('*', x, Const({2.0 * 3.141592653589793 / period}, F));
            return Call(kind == "Sine" ? "sin" : "cos", {x}, Need(x).Type);
        }
        if (kind == "Min" || kind == "Max")
        {
            auto a = Or(Inp(node, "A"), [&] { return Const({PropNumber(p, "ConstA", 0.0)}, F); });
            auto b = Or(Inp(node, "B"), [&] { return Const({PropNumber(p, "ConstB", 1.0)}, F); });
            return Call(kind == "Min" ? "min" : "max", {a, b}, Need(a).Type);
        }
        if (kind == "Clamp")
        {
            auto lo = Or(Inp(node, "Min"), [&] { return Const({PropNumber(p, "MinDefault", 0.0)}, F); });
            auto hi = Or(Inp(node, "Max"), [&] { return Const({PropNumber(p, "MaxDefault", 1.0)}, F); });
            auto x = Inp(node, "Input");
            const auto mode = PropText(p, "ClampMode", "CMODE_Clamp");
            if (mode == "CMODE_ClampMin")
                return Call("max", {x, lo}, Need(x).Type);
            if (mode == "CMODE_ClampMax")
                return Call("min", {x, hi}, Need(x).Type);
            return Call("clamp", {x, lo, hi}, Need(x).Type);
        }
        if (kind == "Power")
        {
            auto base = Inp(node, "Base");
            auto exp = Or(Inp(node, "Exponent"), [&] { return Const({PropNumber(p, "ConstExponent", 2.0)}, F); });
            Printf({base, exp});
            return Make(ChunkKind::Code, "", Need(base).Type);
        }
        if (kind == "DotProduct")
        {
            auto a = Inp(node, "A");
            auto b = Inp(node, "B");
            return Dot(a, b);
        }
        if (kind == "Normalize")
        {
            auto v = Inp(node, "VectorInput");
            return Binary('/', v, Call("sqrt", {Dot(v, v)}, F));
        }
        if (kind == "Length")
        {
            auto x = Inp(node, "Input");
            return Call("length", {x}, F);
        }
        if (kind == "Distance")
        {
            auto a = Inp(node, "A");
            auto b = Inp(node, "B");
            return Call("length", {Binary('-', a, b)}, F);
        } // Length(Sub(A, B))
        if (kind == "AppendVector")
        {
            auto a = Inp(node, "A");
            auto b = Inp(node, "B");
            return Append(a, b);
        }
        if (kind == "Panner")
        {
            auto time = Or(Inp(node, "Time"), [&] { return Make(ChunkKind::Code, "", F); });
            auto speed = Inp(node, "Speed");
            auto sx = speed ? Mask(speed, "R") : Const({PropNumber(p, "SpeedX", 0.0)}, F);
            auto sy = speed ? Mask(speed, "G") : Const({PropNumber(p, "SpeedY", 0.0)}, F);
            auto a1 = Binary('*', time, sx);
            auto a2 = Binary('*', time, sy);
            if (PropTruthy(p, "bFractionalPart"))
            {
                a1 = Call("frac", {a1}, a1->Type);
                a2 = Call("frac", {a2}, a2->Type);
            }
            auto coord = Or(Inp(node, "Coordinate"), [&] { return Texcoord(PropNumber(p, "ConstCoordinate", 0)); });
            auto both = Append(a1, a2);
            return Binary('+', both, coord);
        }
        if (kind == "ComponentMask")
        {
            auto x = Inp(node, "Input");
            std::string chans;
            for (const char c : std::string("RGBA"))
                if (PropTruthy(p, std::string(1, c)))
                    chans += c;
            return Mask(x, chans);
        }
        if (kind == "SmoothStep")
        {
            auto x = Or(Inp(node, "Min"), [&] { return Const({PropNumber(p, "ConstMin", 0.0)}, F); });
            auto y = Or(Inp(node, "Max"), [&] { return Const({PropNumber(p, "ConstMax", 1.0)}, F); });
            auto a = Or(Inp(node, "Value"), [&] { return Const({PropNumber(p, "ConstValue", 0.0)}, F); });
            Printf({x, y, a});
            return Make(ChunkKind::Code, "", ArithType(x->Type, y->Type));
        }
        if (kind == "LinearInterpolate")
        {
            auto x = Or(Inp(node, "A"), [&] { return Const({PropNumber(p, "ConstA", 0.0)}, F); });
            auto y = Or(Inp(node, "B"), [&] { return Const({PropNumber(p, "ConstB", 1.0)}, F); });
            auto a = Or(Inp(node, "Alpha"), [&] { return Const({PropNumber(p, "ConstAlpha", 0.5)}, F); });
            if (x == y)
                return x;
            if (x->Kind == ChunkKind::Const && y->Kind == ChunkKind::Const && x->Type == y->Type &&
                x->HasValue == y->HasValue && (!x->HasValue || x->Value == y->Value))
                return x;
            const int t = ArithType(x->Type, y->Type);
            const int alphaType = t == a->Type ? t : F1;
            if (alphaType == F1 && a->Kind == ChunkKind::Const && a->HasValue && !a->Value.empty())
            {
                if (a->Value[0] == 0.0)
                    return x;
                if (a->Value[0] == 1.0)
                    return y;
            }
            Printf({x, y, a});
            return Make(ChunkKind::Code, "", t);
        }
        if (kind == "If")
        {
            auto a = Inp(node, "A");
            auto b = Or(Inp(node, "B"), [&] { return Const({PropNumber(p, "ConstB", 0.0)}, F); });
            auto agb = Inp(node, "AGreaterThanB");
            auto aeb = Inp(node, "AEqualsB");
            auto alb = Inp(node, "ALessThanB");
            auto thr = Const({PropNumber(p, "EqualsThreshold", 0.00001)}, F);
            if (aeb)
            {
                const int t = ArithType(Need(agb).Type, ArithType(aeb->Type, Need(alb).Type));
                auto cagb = ForceCast(agb, t);
                auto caeb = ForceCast(aeb, t);
                auto calb = ForceCast(alb, t);
                Printf({a, b, thr, a, b, cagb, calb, caeb});
                return Make(ChunkKind::Code, "", t);
            }
            const int t = ArithType(Need(agb).Type, Need(alb).Type);
            auto cagb = ForceCast(agb, t);
            auto calb = ForceCast(alb, t);
            Printf({a, b, cagb, calb});
            return Make(ChunkKind::Code, "", t);
        }
        if (kind == "TextureSampleParameter2D" || kind == "TextureSampleParameterCube" || kind == "TextureSample")
        {
            const std::string textureKind = kind == "TextureSampleParameterCube" ? "Cube" : "Standard2D";
            ChunkPtr tex;
            if (kind == "TextureSample" && Inp(node, "TextureObject"))
                tex = Inp(node, "TextureObject");
            else
            {
                tex = Make(ChunkKind::Texture);
                tex->TextureKind = textureKind;
                bool none = true;
                std::string name;
                if (kind != "TextureSample")
                    name = PropText(p, "ParameterName", "", &none), none = none || !p.Has("ParameterName");
                tex->NameNone = none;
                tex->Name = name;
                tex->Key = none ? TextureKey(p) : "N\x1f" + name;
            }
            std::vector<ChunkPtr> mips;
            for (const char* n : {"MipValue", "CoordinatesDX", "CoordinatesDY"})
                mips.push_back(Inp(node, n));
            auto coords = Or(Inp(node, "Coordinates"), [&] { return Texcoord(PropNumber(p, "ConstCoordinate", 0)); });
            Access(tex);
            Access(coords);
            for (const auto& m : mips)
                Access(m);
            return Make(ChunkKind::Code, "", F4);
        }
        if (kind == "TextureObjectParameter" || kind == "TextureObject")
        {
            auto tex = Make(ChunkKind::Texture);
            tex->TextureKind = "Standard2D";
            bool none = true;
            const auto name = PropText(p, "ParameterName", "", &none);
            tex->NameNone = none || !p.Has("ParameterName");
            tex->Name = name;
            tex->Key = !tex->NameNone && !name.empty() ? "N\x1f" + name : TextureKey(p);
            return tex;
        }
        if (kind == "TextureProperty")
        {
            auto tex = Inp(node, "TextureObject");
            Access(tex);
            return Make(ChunkKind::Code, "", F2);
        }
        if (kind == "TextureCoordinate")
        {
            auto c = Texcoord(PropNumber(p, "CoordinateIndex", 0));
            const double u = PropNumber(p, "UTiling", 1.0), v = PropNumber(p, "VTiling", 1.0);
            if (std::fabs(u - v) > 1e-8)
                return Binary('*', c, Const({u, v}, F2));
            if (std::fabs(1.0 - u) > 1e-8)
                return Binary('*', c, Const({u}, F));
            return c;
        }
        if (kind == "Custom" || kind == "KadanGameCustom" || kind == "KadanReflectionCustom")
            return Custom(node, kind, p);
        if (kind == "KadanSceneDepth")
            return Const({1e6}, F);
        if (kind == "KadanSceneColor")
            return Const({0.0, 0.0, 0.0}, F3);
        if (kind == "ShadowReplace")
        {
            auto d = Inp(node, "Default");
            auto s = Inp(node, "Shadow");
            Printf({d, s});
            return Make(ChunkKind::Code, "", Need(d).Type);
        }
        if (kind == "Transform")
        {
            auto x = Inp(node, "Input");
            Access(x);
            return Make(ChunkKind::Code, "", Need(x).Type);
        }
        if (kind == "PerInstanceCustomData")
        {
            auto d = Or(Inp(node, "DefaultValue"), [&] { return Const({PropNumber(p, "ConstDefaultValue", 0.0)}, F); });
            Access(d);
            return Make(ChunkKind::Code, "", F);
        }
        if (kind == "CollectionParameter")
            return Make(ChunkKind::Code, "", F);
        const int code = CodeType(kind);
        if (code >= 0)
        {
            for (const auto& input : node.Inputs.Items)
                Access(Inp(node, input.first));
            return Make(ChunkKind::Code, "", code);
        }
        throw Unsupported(kind);
    }

    ChunkPtr Property(const std::string& name, bool active, const std::string& frequency)
    {
        Frequency = frequency;
        const int t = PropertyType(name);
        const auto* link = Graph.Outputs.Find(name);
        ChunkPtr c;
        if (!active || !link || !(link->kind == Json::Array && !link->array.empty()))
            c = Const(PropertyDefault(name), t != F1 ? VectorType(Components(t)) : F);
        else
            c = Link(*link);
        if (!c)
            return nullptr;
        if (name == "BaseColor")
            c = ForceCast(c, t);
        return ForceCast(c, t);
    }
};

inline std::string SettingText(const Json& graph, const char* key, const char* fallback)
{
    return graph.Has(key) && graph.At(key).kind == Json::String ? graph.At(key).string : std::string(fallback);
}

inline bool PropertyActive(const std::string& name, const CanonGraph& graph)
{
    const auto& s = graph.Settings;
    const auto domain = SettingText(s, "domain", "MD_Surface"), blend = SettingText(s, "blend_mode", "BLEND_Opaque"),
               shading = SettingText(s, "shading_model", "MSM_DefaultLit");
    const Json render = s.Has("render_settings") && s.At("render_settings").kind == Json::Object
                            ? s.At("render_settings")
                            : Json::MakeObject();
    const auto tlm = SettingText(render, "TranslucencyLightingMode", "TLM_VolumetricNonDirectional");
    const bool lit = shading != "MSM_Unlit", translucent = TranslucentBlend(blend);
    const bool nondirectional = tlm == "TLM_VolumetricNonDirectional" || tlm == "TLM_VolumetricPerVertexNonDirectional",
               volumetric = tlm.compare(0, 14, "TLM_Volumetric") == 0;
    const bool customized = name.compare(0, 12, "CustomizedUV") == 0;
    if (domain == "MD_PostProcess")
        return name == "EmissiveColor" || (PropTruthy(render, "BlendableOutputAlpha") && name == "Opacity");
    if (domain == "MD_LightFunction")
        return name == "EmissiveColor";
    if (domain == "MD_UI")
        return name == "EmissiveColor" || name == "WorldPositionOffset" ||
               (name == "OpacityMask" && blend == "BLEND_Masked") ||
               (name == "Opacity" && translucent && blend != "BLEND_Modulate") || customized;
    if (domain == "MD_DeferredDecal")
    {
        static const std::map<std::string, std::string> table = {
            {"DBM_Translucent", "EmissiveColor Normal Metallic Specular BaseColor Roughness Opacity"},
            {"DBM_Stain", "EmissiveColor Normal Metallic Specular BaseColor Roughness Opacity"},
            {"DBM_Normal", "Normal Opacity"},
            {"DBM_Emissive", "EmissiveColor Opacity"},
            {"DBM_DBuffer_ColorNormalRoughness", "Normal BaseColor Roughness Metallic Specular Opacity EmissiveColor"},
            {"DBM_DBuffer_Color", "BaseColor Opacity EmissiveColor"},
            {"DBM_DBuffer_C4_WaterBox", "BaseColor Opacity EmissiveColor"},
            {"DBM_DBuffer_ColorNormal", "BaseColor Normal Opacity EmissiveColor"},
            {"DBM_DBuffer_ColorRoughness", "BaseColor Roughness Metallic Specular Opacity EmissiveColor"},
            {"DBM_DBuffer_NormalRoughness", "Normal Roughness Metallic Specular Opacity"},
            {"DBM_DBuffer_Normal", "Normal Opacity"},
            {"DBM_DBuffer_Roughness", "Roughness Metallic Specular Opacity"},
            {"DBM_DBuffer_Emissive", "EmissiveColor Opacity"},
            {"DBM_DBuffer_EmissiveAlphaComposite", "EmissiveColor Opacity"},
            {"DBM_AlphaComposite", "EmissiveColor Metallic Specular BaseColor Roughness Opacity"},
            {"DBM_DBuffer_AlphaComposite", "EmissiveColor Metallic Specular BaseColor Roughness Opacity"},
            {"DBM_AmbientOcclusion", "AmbientOcclusion"}};
        const auto mode = SettingText(render, "DecalBlendMode", "DBM_Translucent");
        const auto found = table.find(mode);
        bool listed = false;
        if (found != table.end())
        {
            std::istringstream words(found->second);
            std::string word;
            while (words >> word)
                listed |= word == name;
        }
        return customized || name == "WorldPositionOffset" || listed;
    }
    if (name == "Refraction")
        return translucent && blend != "BLEND_AlphaHoldout" && blend != "BLEND_Modulate";
    if (name == "Opacity")
        return (translucent && blend != "BLEND_Modulate") || Subsurface(shading);
    if (name == "OpacityMask")
        return blend == "BLEND_Masked";
    if (name == "BaseColor" || name == "AmbientOcclusion")
        return lit;
    if (name == "Specular" || name == "Roughness" || name == "Metallic")
        return lit && (!translucent || !volumetric);
    if (name == "Anisotropy" || name == "Tangent")
        return (shading == "MSM_DefaultLit" || shading == "MSM_ClearCoat") && (!translucent || !volumetric);
    if (name == "Normal")
    {
        const auto* refraction = graph.Outputs.Find("Refraction");
        return (lit && (!translucent || !nondirectional)) ||
               (translucent && refraction && refraction->kind == Json::Array && !refraction->array.empty());
    }
    if (name == "SubsurfaceColor")
        return InSet(shading, {"MSM_Subsurface", "MSM_PreintegratedSkin", "MSM_TwoSidedFoliage", "MSM_Cloth"});
    if (name == "ClearCoat")
        return InSet(shading, {"MSM_ClearCoat", "MSM_Hair", "MSM_Cloth", "MSM_Eye"});
    if (name == "ClearCoatRoughness")
        return InSet(shading, {"MSM_ClearCoat", "MSM_Eye"});
    if (name == "EmissiveColor")
        return blend != "BLEND_AlphaHoldout";
    if (name == "PixelDepthOffset")
        return !translucent;
    return true;
}
} // UniformOrder

// The uniform expression set UE 4.26 makes from the graph (material_uniform_order.predict).
inline UniformOrder::Prediction PredictUniformSet(const CanonGraph& graph)
{
    using namespace UniformOrder;
    Translator tr(graph);
    const auto& s = graph.Settings;
    const auto domain = SettingText(s, "domain", "MD_Surface"), blend = SettingText(s, "blend_mode", "BLEND_Opaque"),
               shading = SettingText(s, "shading_model", "MSM_DefaultLit");
    const auto linked = [&](const char* pin)
    {
        const auto* link = graph.Outputs.Find(pin);
        return link && link->kind == Json::Array && !link->array.empty();
    };
    const auto prop = [&](const std::string& name, const std::string& frequency)
    {
        bool active = PropertyActive(name, graph);
        if ((name == "Opacity" || name == "OpacityMask") && !(domain != "MD_Surface" || blend != "BLEND_Opaque" ||
                                                              (shading != "MSM_Unlit" && shading != "MSM_DefaultLit")))
            active = false;
        tr.Property(name, active, frequency);
    };
    for (const char* name : {"Normal", "EmissiveColor", "BaseColor", "Metallic", "Specular", "Roughness", "Anisotropy",
                             "Opacity", "OpacityMask", "Tangent"})
        prop(name, "pixel");
    prop("WorldPositionOffset", "vertex");
    if (domain == "MD_Surface" && Subsurface(shading))
    {
        prop("SubsurfaceColor", "pixel");
        tr.Frequency = "pixel";
        tr.ForceCast(Make(ChunkKind::Uniform, "__SubsurfaceProfile", F), F1);
    }
    prop("ClearCoat", "pixel");
    prop("ClearCoatRoughness", "pixel");
    prop("AmbientOcclusion", "pixel");
    if (TranslucentBlend(blend))
    {
        prop("Refraction", "pixel");
        tr.Frequency = "pixel";
        tr.ForceCast(Make(ChunkKind::Uniform, "RefractionDepthBias", F), F1);
    }
    if (linked("WorldPositionOffset"))
    {
        for (auto i = tr.Cache.begin(); i != tr.Cache.end();)
            if (i->first.compare(0, 7, "vertex\x1f") == 0)
                i = tr.Cache.erase(i);
            else
                ++i;
        prop("WorldPositionOffset", "vertex");
    }
    prop("PixelDepthOffset", "pixel");
    if (graph.Outputs.Has("ClearCoatBottomNormal"))
    {
        tr.Frequency = "pixel";
        tr.Link(*graph.Outputs.Find("ClearCoatBottomNormal"));
    }
    for (int index = 0; index < std::min(tr.Texcoords, 8); ++index)
    {
        const auto name = "CustomizedUV" + std::to_string(index);
        tr.Frequency = "vertex";
        const auto* link = graph.Outputs.Find(name);
        if (link && link->kind == Json::Array && !link->array.empty())
            tr.ForceCast(tr.Link(*link), F2);
    }
    Prediction out;
    out.Vectors = tr.Vectors;
    out.Scalars = tr.Scalars;
    for (const auto& item : tr.Textures)
        if (!item.second.empty())
            out.Textures.push_back(item);
    out.Origins = tr.Origins;
    return out;
}
}
} // P4MapKit::Material

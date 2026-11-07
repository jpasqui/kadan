// The effect masters' camera fade, recovered from the original base-pass VS.
// Shader-word and instruction-shape evidence admits this graph only; it does
// not claim equivalence of other vertex work or in-game renderer validation.
#pragma once
#include "material_source_import.h"
#include "material_engine_hashes.h"

namespace P4MapKit
{
namespace Material
{
struct MaterialCameraFadeVariant
{
    bool Matched = false, Near = false, Far = false;
};
inline MaterialCameraFadeVariant MatchMaterialCameraFadeWords(SymbolicArena& arena, const std::vector<Value>& words)
{
    const auto parameter = [&](const char* name)
    {
        return source(arena, "ScalarParameter", name);
    };
    const auto scale = arena.op("SUB", {1.0, parameter("CameraFadeSizeScaleMin")});
    if (words.size() == 1 && same(words[0], arena.op("MUL", {0.0, arena.op("MAX", {0.0, scale})})))
        return {true, false, false};
    MaterialCameraFadeVariant result;
    std::set<const Expression*> wanted{scale.expression.get()}, actual;
    for (const auto& word : words)
    {
        if (!symbolic(word))
            return result;
        actual.insert(word.expression.get());
    }
    for (int fade = 0; fade < 2; ++fade)
    {
        const auto start = parameter(fade ? "CameraFarFadeStart" : "CameraNearFadeStart"),
                   end = parameter(fade ? "CameraFarFadeEnd" : "CameraNearFadeEnd");
        if (!actual.count(end.expression.get()))
            continue;
        if (fade)
            result.Far = true;
        else
            result.Near = true;
        wanted.insert(end.expression.get());
        wanted.insert(arena.op("SUB", {start, end}).expression.get());
    }
    result.Matched = (result.Near || result.Far) && wanted == actual;
    return result;
}
inline std::set<int> MaterialCameraFadeWords(const std::string& text, int buffer)
{
    std::set<int> out;
    const std::regex load("LDC\\.(\\w+) R\\d+(?:\\.(\\w+))?, buf" + std::to_string(buffer) + "\\[(\\d+)\\]");
    for (std::sregex_iterator i(text.begin(), text.end(), load), end; i != end; ++i)
    {
        const auto at = std::stoul((*i)[3].str());
        if (at > 0x7fffffffu)
            throw Refused("vertex Material word exceeds its bound", "material camera fade words");
        if ((*i)[1].str() == "F32")
            out.insert(int(at));
        else
            for (char c : (*i)[2].matched ? (*i)[2].str() : "xyzw")
            {
                const auto k = std::string("xyzw").find(c);
                if (k == std::string::npos || at + 4 * k > 0x7fffffffu)
                    throw Refused("vertex Material load has an invalid component", "material camera fade words");
                out.insert(int(at + 4 * k));
            }
    }
    return out;
}
inline bool MaterialCameraFadeInstructionShape(const std::string& text)
{
    auto at = text.find("DIV.F32.SAT ");
    if (at == std::string::npos)
        return false;
    at = text.find("FLR.F ", at);
    return at != std::string::npos && text.find("MAX.F ", at) != std::string::npos;
}
inline Json AddMaterialCameraFadeGraph(Json graph, const MaterialCameraFadeVariant& fades,
                                       const FMaterialUniformSet& uniforms)
{
    if (!fades.Matched)
        return graph;
    auto nodes = graph.At("nodes");
    std::size_t count = 0;
    const auto node = [&](const std::string& kind, const Json& inputs, const Json& props)
    {
        std::string name;
        do
        {
            name = kind + "_fade" + std::to_string(count++);
        } while (nodes.Has(name));
        nodes[name] = Object({{"type", kind}, {"inputs", inputs}, {"props", props}});
        return Array({name, ""});
    };
    const auto one = [&]()
    {
        return node("Constant", Json::MakeObject(), Object({{"R", 1.0}}));
    };
    const auto parameter = [&](const std::string& name)
    {
        double value = 0;
        for (const auto& scalar : uniforms.Scalars)
            if (!scalar.NameIsNone && scalar.Name == name)
            {
                float decoded;
                const auto bits = scalar.DefaultBits[0];
                std::memcpy(&decoded, &bits, 4);
                value = double(decoded);
                break;
            }
        return node("ScalarParameter", Json::MakeObject(), Object({{"ParameterName", name}, {"DefaultValue", value}}));
    };
    const auto binary = [&](const std::string& kind, const Json& a, const Json& b)
    {
        return node(kind, Object({{"A", a}, {"B", b}}), Json::MakeObject());
    };
    const auto unary = [&](const std::string& kind, const Json& input)
    {
        return node(kind, Object({{"Input", input}}), Json::MakeObject());
    };
    const auto leaf = [&](const std::string& kind)
    {
        return node(kind, Json::MakeObject(), Json::MakeObject());
    };
    const auto scale = parameter("CameraFadeSizeScaleMin");
    std::vector<Json> factors;
    for (int fade = 0; fade < 2; ++fade)
    {
        if (!(fade ? fades.Far : fades.Near))
        {
            factors.push_back(one());
            continue;
        }
        // Explicit temporaries retain Python's node insertion and operand order.
        const auto particle = leaf("ParticlePositionWS"), camera = leaf("CameraPositionWS");
        const auto distance = binary("Distance", particle, camera);
        const auto end = parameter(fade ? "CameraFarFadeEnd" : "CameraNearFadeEnd"),
                   start = parameter(fade ? "CameraFarFadeStart" : "CameraNearFadeStart");
        const auto numerator = binary("Subtract", distance, end), denominator = binary("Subtract", start, end),
                   ratio = binary("Divide", numerator, denominator);
        factors.push_back(unary("Saturate", ratio));
    }
    const auto w = [&]()
    {
        const auto constant = one(), product = binary("Multiply", factors[0], factors[1]);
        return binary("Subtract", constant, product);
    };
    const auto first = w(), floor = unary("Floor", first), constant = one(),
               minimum = binary("Subtract", constant, scale), limit = binary("Max", floor, minimum);
    const auto factor = binary("Multiply", fades.Near || fades.Far ? first : w(), limit);
    const auto particle = leaf("ParticlePositionWS"), world = leaf("WorldPosition"),
               offset = binary("Subtract", particle, world), result = binary("Multiply", offset, factor);
    graph["nodes"] = nodes;
    graph["outputs"]["WorldPositionOffset"] = result;
    return graph;
}
inline MaterialCameraFadeVariant ReadMaterialCameraFadeVariant(const std::string& text, const BindingContext& context)
{
    int material = -1, view = -1;
    for (const auto& buffer : context.Buffers)
    {
        if (buffer.second == "Material")
            material = buffer.first;
        if (buffer.second == "View")
            view = buffer.first;
        if (starts(buffer.second, "MaterialCollection") &&
            text.find("buf" + std::to_string(buffer.first) + "[") != std::string::npos)
            return {};
    }
    if (material < 0 || (view >= 0 && text.find("buf" + std::to_string(view) + "[2440]") != std::string::npos))
        return {};
    SymbolicArena arena;
    Binder binder(arena, context);
    std::vector<Value> words;
    for (int at : MaterialCameraFadeWords(text, material))
        words.push_back(binder.material_word(at));
    const auto variant = MatchMaterialCameraFadeWords(arena, words);
    return variant.Matched && (!(variant.Near || variant.Far) || MaterialCameraFadeInstructionShape(text))
               ? variant
               : MaterialCameraFadeVariant{};
}
inline Json RecoverMaterialCameraFadeGraph(const MaterialOriginalOwnerSource& owner,
                                           const MaterialOriginalArchiveAccess& archive, Json graph)
{
    if (graph.kind != Json::Object || !graph.Has("nodes") || !graph.Has("outputs") ||
        graph.At("outputs").Has("WorldPositionOffset") || !archive.MapIndices || !archive.Payload || !archive.Frequency)
        return graph;
    const FMaterialShaderReference* selected = nullptr;
    for (const auto factory :
         {0x1adefad780cd16c5ull, 0x6d728306d56d412full, 0x00080c76633abf12ull, EngineHash::LocalVertexFactory})
    {
        for (const auto& ref : owner.References)
        {
            if (ref.Frequency != 0 || ref.VertexFactoryHash != factory ||
                !MaterialParticleBasePassVertexType(ref.TypeHash))
                continue;
            if (!selected || ref.TypeHash == 0xe99e2d4408e39e36ull)
                selected = &ref;
            if (ref.TypeHash == 0xe99e2d4408e39e36ull)
                break;
        }
        if (selected)
            break;
    }
    if (!selected)
        return graph;
    // An unavailable/unrecognized VS leaves its existing pixel recovery intact.
    // A successful match still requires the separate compiler comparison.
    try
    {
        const auto indices = archive.MapIndices(owner.Map.ResourceHash);
        if (selected->ResourceIndex < 0 || std::size_t(selected->ResourceIndex) >= indices.size())
            return graph;
        const auto index = indices[std::size_t(selected->ResourceIndex)];
        if (archive.Frequency(index) != 0)
            return graph;
        const auto payload = archive.Payload(index);
        const auto header = ReadOriginalParticleVertexBindings(payload);
        const auto text = ReadOriginalMaterialProgramText(payload, header);
        BindingContext context;
        context.Uniforms = owner.Map.Uniforms;
        for (const auto& buffer : selected->UniformBufferHashes)
        {
            const auto known = MaterialKnownBufferNames().find(buffer.second);
            if (known != MaterialKnownBufferNames().end())
                context.Buffers[int(buffer.first)] = known->second;
        }
        for (const auto& buffer : selected->NamedBuffers)
        {
            const auto found = context.Buffers.find(int(buffer.first));
            if (found != context.Buffers.end() && found->second != buffer.second)
                return graph;
            context.Buffers[int(buffer.first)] = buffer.second;
        }
        return AddMaterialCameraFadeGraph(graph, ReadMaterialCameraFadeVariant(text, context), owner.Map.Uniforms);
    }
    catch (const std::exception&)
    {
        return graph;
    }
}
}
} // P4MapKit::Material

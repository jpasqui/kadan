// Kadan's GLSL_SWITCH format, the portable half (stage 8.5 of tools/navigation/OWN_SHADER_MAPS.md): UE's SPIR-V of
// one shader to the game's program (our SPIR-V front end, with the game's uniform buffer layouts), and the program
// with its bindings to the game's payload (our Maxwell compiler, own_payload.h). The plugin's format module runs
// UE's preprocessor, DXC and resource table mapping around it.
#pragma once
#include "KadanMaxwell/spirv_front.h"
#include "own_game_layouts.h"
#include "own_payload.h"
#include <cctype>
#include <string>
#include <vector>

namespace P4MapKit { namespace CustomMaterial { namespace OwnSwitch {

struct Varying { std::string name; int location = 0; };

struct Program {
    std::string assembly;
    OwnPayload::Stage stage = OwnPayload::Stage::Pixel;
    spirv_front::Interface iface;
    std::vector<Varying> inputs, outputs;   // user varyings, in the game's names (in_texcoord10_centroid)
    unsigned inout_mask = 0;                // vertex attributes read; render targets written (0x8000: depth)
};

constexpr unsigned BuiltInFragDepth = 22;

// "in.var.TEXCOORD10_centroid" to the game's "in_texcoord10_centroid" (UE's HLSL cross compiler's names).
inline std::string Semantic(const std::string& name) {
    for (const char* prefix : {"in.var.", "out.var."})
        if (name.compare(0, std::string(prefix).size(), prefix) == 0) return name.substr(std::string(prefix).size());
    return name;
}
inline std::string Lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
// Element e of an array varying with semantic TEXCOORD3 is TEXCOORD(3 + e), as HLSL numbers semantics.
inline std::string ElementSemantic(const std::string& semantic, unsigned e) {
    if (!e) return semantic;
    std::size_t end = semantic.size();
    while (end && std::isdigit(static_cast<unsigned char>(semantic[end - 1]))) --end;
    std::size_t tail = end;                 // a suffix such as "_centroid" follows the number
    for (std::size_t i = 0; i < semantic.size(); ++i)
        if (std::isdigit(static_cast<unsigned char>(semantic[i]))) {
            std::size_t j = i;
            while (j < semantic.size() && std::isdigit(static_cast<unsigned char>(semantic[j]))) ++j;
            if (j == semantic.size() || semantic[j] == '_') { end = j; tail = i; break; }
            i = j;
        }
    if (tail == end) return semantic + std::to_string(e);
    return semantic.substr(0, tail) + std::to_string(std::stoul(semantic.substr(tail, end - tail)) + e) + semantic.substr(end);
}
inline bool NumberedAs(const std::string& semantic, const std::string& stem, unsigned& index) {
    if (semantic.compare(0, stem.size(), stem) != 0 || semantic.size() == stem.size()) return false;
    index = 0;
    for (std::size_t i = stem.size(); i < semantic.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(semantic[i]))) return false;
        index = index * 10 + unsigned(semantic[i] - '0');
    }
    return index < 32;
}

// The game's struct layouts by member name: blocks it lays out differently from stock UE 4.26 (View).
inline spirv_front::Options GameOptions() {
    spirv_front::Options o;
    o.drop_clip_distance = true;            // the game's renderer uses no clip distances
    o.vertex_attribute_semantics = true;    // ATTRIBUTEn is vertex attribute n in the game's vertex declarations
    o.split_loose_integers = true;          // loose uints in a 'u' array of their own, as the game's compiler packs them
    o.resources_by_first_use = true;        // texture and sampler slots in the order the code reads them, as HLSLcc numbers them
    o.fmod_truncates = true;                // HLSL's fmod truncates; DXC writes it as OpFMod
    o.remap = [](const std::string& block, const std::string& member, std::uint32_t) -> std::optional<std::uint32_t> {
        const auto& layouts = GameLayoutOffsets();
        const auto it = layouts.find({block, member});
        if (it == layouts.end()) return std::nullopt;
        return it->second;
    };
    return o;
}

inline Program Translate(const std::vector<std::uint32_t>& words) {
    const auto options = GameOptions();
    auto r = spirv_front::Translate(words, options);
    Program p;
    p.assembly = std::move(r.assembly);
    p.iface = std::move(r.iface);
    p.stage = p.iface.stage == spirv_front::Stage::Vertex ? OwnPayload::Stage::Vertex
            : p.iface.stage == spirv_front::Stage::Geometry ? OwnPayload::Stage::Geometry : OwnPayload::Stage::Pixel;
    for (const auto& v : p.iface.inputs) {
        if (!v.builtin.empty()) continue;
        const auto semantic = Semantic(v.name);
        unsigned index = 0;
        if (p.stage == OwnPayload::Stage::Vertex && NumberedAs(semantic, "ATTRIBUTE", index)) p.inout_mask |= 1u << index;
        else for (unsigned e = 0; e < v.count; ++e) {
            // A pixel program declares only the inputs it reads, as the game's do (the others keep their locations).
            if (p.stage == OwnPayload::Stage::Pixel && !p.iface.read_inputs.count(v.location + int(e))) continue;
            p.inputs.push_back({"in_" + Lower(ElementSemantic(semantic, e)), v.location + int(e)});
        }
    }
    for (const auto& v : p.iface.outputs) {
        if (!v.builtin.empty()) {
            if (p.stage == OwnPayload::Stage::Pixel && std::stoul(v.builtin) == BuiltInFragDepth) p.inout_mask |= 0x8000;
            continue;
        }
        const auto semantic = Semantic(v.name);
        unsigned index = 0;
        if (p.stage == OwnPayload::Stage::Pixel && NumberedAs(semantic, "SV_Target", index)) p.inout_mask |= 1u << index;
        else for (unsigned e = 0; e < v.count; ++e) p.outputs.push_back({"out_" + Lower(ElementSemantic(semantic, e)), v.location + int(e)});
    }
    return p;
}

// The size of a bound uniform buffer as the game's shader maps record it: its struct's constant data in the game's
// layout, else the program's own block (a material's own buffers, parameter collections) rounded to 16.
inline std::uint32_t BufferSize(const spirv_front::Block& b) {
    const auto& sizes = GameConstantSizes();
    const auto it = sizes.find(b.name);
    if (it != sizes.end()) return it->second;
    return (b.size + 15u) & ~15u;
}

// A resource member's index in the game's struct (its resource table entries name resources by index), else UE's.
inline std::uint32_t GameResourceIndex(const std::string& name, std::uint32_t ue) {
    const auto& table = GameResourceIndices();
    const auto it = table.find(name);
    return it == table.end() ? ue : it->second;
}

// The game's layout hash of a uniform buffer struct its renderer lays out differently from stock UE (the runtime
// may compare a bound buffer's hash with the program's); UE's own hash otherwise.
inline std::uint32_t GameLayoutHash(const std::string& name, std::uint32_t ue) {
    if (!ue) return 0;
    const auto& game = GameLayoutHashes();
    const auto it = game.find(name);
    return it == game.end() ? ue : it->second;
}

}}}  // namespace P4MapKit::CustomMaterial::OwnSwitch

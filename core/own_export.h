#pragma once
// A creator material's whole shader map of Kadan's own, from its graph and how it is used (stage 7 of
// tools/navigation/OWN_SHADER_MAPS.md): what Kadan's export writes in place of standing a material on a game material.
// The pixel programs are compiled from the graph (own_opaque.h, own_masked.h, own_translucent.h); the vertex and
// geometry programs are Kadan's own, compiled once and carried in the plugin (modding/mapkit/own_shaders,
// own_shader_payloads.h); the map is written by own_map_writer.h. The result is the inline block, which goes in place
// of the cooked material's own (InsertBlock), and the library holding the map, which goes in the mod's pak (Tsuchi
// opens a mod's library when it mounts the mod).
//
// What is built: lit and unlit opaque, masked lit, lit translucent (Surface lighting) and unlit translucent materials
// on static meshes, used with static lighting (the lightmap versions: the cooked material says
// bUsedWithStaticLighting); lit and unlit opaque also on instanced static meshes. Anything else is refused with what
// is missing, so Review can say it.
#include "own_masked.h"
#include "own_translucent.h"
#include "own_map_writer.h"
#include "own_shader_payloads.h"
#include "cooked_export_rebuild.h"
namespace P4MapKit { namespace CustomMaterial { namespace OwnExport {

// The game's environment lighting collection (MPC_Env), which the lit opaque programs read as the game's do.
inline Bytes EnvironmentCollection() { return OwnMap::FromHex("8293e1744a3ab441a4ae43495fcbb518"); }

struct Usage {
    bool static_lighting = true;    // the cooked material is used with static lighting (bUsedWithStaticLighting)
    bool instanced = false;         // and with instanced static meshes (bUsedWithInstancedStaticMeshes)
};

// The vertex and geometry programs a map may need, by name (the files of modding/mapkit/own_shaders, without
// .payload).
inline const std::vector<std::string>& VertexProgramNames() {
    static const std::vector<std::string> all = {
        "static_mesh_vs_1uv", "static_mesh_vs_lightmap_1uv", "static_mesh_vs_lightmap_df_1uv",
        "instanced_vs_1uv", "instanced_vs_lightmap_1uv", "instanced_vs_lightmap_df_1uv",
        "translucent_vs_1uv", "translucent_vs_lightmap_1uv", "translucent_vs_lightmap_df_1uv", "translucent_fog_vs_1uv",
        "depth_only_vs", "shadow_vs", "shadow_loose_vs", "shadow_perspective_vs", "shadow_perspective_loose_vs",
        "shadow_for_gs_vs", "shadow_for_gs_onepass_vs", "onepass_point_shadow_gs"};
    return all;
}

inline Bytes DecodeBase64(const char* text) {
    Bytes out; std::uint32_t bits = 0; int count = 0;
    for (const char* c = text; *c && *c != '='; ++c) {
        int v = *c >= 'A' && *c <= 'Z' ? *c - 'A' : *c >= 'a' && *c <= 'z' ? *c - 'a' + 26 :
                *c >= '0' && *c <= '9' ? *c - '0' + 52 : *c == '+' ? 62 : *c == '/' ? 63 : -1;
        if (v < 0) throw std::runtime_error("own shaders: bad base64");
        bits = (bits << 6) | std::uint32_t(v); count += 6;
        if (count >= 8) { count -= 8; out.push_back(std::uint8_t(bits >> count)); }
    }
    return out;
}

// The programs the plugin carries (own_shader_payloads.h), each checked against its recorded size and SHA-1.
inline const std::map<std::string, Bytes>& ShippedVertexPrograms() {
    static const std::map<std::string, Bytes> all = [] {
        std::map<std::string, Bytes> out;
        for (const auto& p : OwnShaders::kPayloads) {
            std::string text;
            for (std::size_t i = 0; i < p.count; ++i) text += p.pieces[i];
            auto bytes = DecodeBase64(text.c_str());
            std::string digest;
            for (auto c : Sha1(bytes)) { static const char* hex = "0123456789abcdef"; digest += hex[c >> 4]; digest += hex[c & 15]; }
            if (bytes.size() != p.size || digest != p.sha1)
                throw std::runtime_error(std::string("own shaders: the carried program ") + p.name + " is damaged");
            out[p.name] = std::move(bytes);
        }
        for (const auto& name : VertexProgramNames())
            if (!out.count(name)) throw std::runtime_error("own shaders: the plugin carries no program " + name);
        return out;
    }();
    return all;
}

enum class Kind { LitOpaque, UnlitOpaque, MaskedLit, LitTranslucent, UnlitTranslucent };

// What is not built yet for a graph and usage, or empty.
inline std::string NotBuilt(const Json& graph, const Usage& usage) {
    const auto blend = Text(graph,"blend_mode"), shading = Text(graph,"shading_model");
    if (Text(graph,"domain") != "MD_Surface") return "Only Surface materials can be exported yet.";
    if (!usage.static_lighting) return "Kadan's own shaders are built for materials used with static lighting.";
    for (const auto& node : Get(graph,"nodes").object)
        if (Text(node.second,"type") == "TextureCoordinate" && Number(Get(node.second,"props"),"CoordinateIndex",0) != 0)
            return "Only the first texture coordinates can be read yet. Set Coordinate Index to 0.";
    if (shading != "MSM_DefaultLit" && shading != "MSM_Unlit") return "Use Default Lit or Unlit shading.";
    if (blend == "BLEND_Opaque") return {};
    if (usage.instanced) return "Only opaque materials can be used on instanced meshes yet. Put this material on a plain static mesh.";
    if (blend == "BLEND_Masked") return shading == "MSM_DefaultLit" ? std::string() : "Masked materials need Default Lit shading.";
    if (blend == "BLEND_Translucent")
        return Flag(graph,"two_sided") ? "Two-sided translucent materials cannot be exported yet. Turn off Two Sided." : std::string();
    return "Use the Opaque, Masked or Translucent blend mode.";
}

inline Kind KindOf(const Json& graph) {
    const auto blend = Text(graph,"blend_mode");
    const bool unlit = Text(graph,"shading_model") == "MSM_Unlit";
    if (blend == "BLEND_Masked") return Kind::MaskedLit;
    if (blend == "BLEND_Translucent") return unlit ? Kind::UnlitTranslucent : Kind::LitTranslucent;
    return unlit ? Kind::UnlitOpaque : Kind::LitOpaque;
}

struct Result {
    OwnMap::OwnMapResult map;       // the inline block, the library, the map and id hashes, the programs in order
    std::uint32_t material_layout = 0;
    std::size_t versions = 0, programs = 0;
};

// The map and library for a creator graph. vertex: the vertex and geometry programs by name (VertexProgramNames).
// texture_indices: each texture parameter's index in the cooked material's referenced textures (else list order).
inline Result Build(const Json& graph, const Usage& usage, const std::map<std::string, Bytes>& vertex,
                    const std::map<std::string, std::int32_t>& texture_indices = {}) {
    const auto issue = NotBuilt(graph, usage);
    if (!issue.empty()) throw std::runtime_error(issue);
    const auto kind = KindOf(graph);
    const auto textures = OwnOpaque::GraphTextures(graph);
    const auto uniforms = OwnOpaque::Uniforms(graph, textures);
    const auto layout = OwnOpaque::MaterialLayoutHash(uniforms);
    const auto own = Json::Parse(OwnOpaque::UniformFacts(uniforms));
    const bool opaque = kind == Kind::LitOpaque || kind == Kind::UnlitOpaque || kind == Kind::MaskedLit;
    const auto facts = OwnMap::MaterialFacts(own, texture_indices,
                                             opaque ? std::vector<Bytes>{EnvironmentCollection()} : std::vector<Bytes>{});
    std::map<std::string, Bytes> payloads = vertex;
    std::vector<OwnMap::Version> versions;
    if (opaque) {
        const auto compile = [&](bool instanced) {
            return kind == Kind::UnlitOpaque ? OwnOpaque::CompileUnlit(graph, {}, textures, layout, instanced)
                                             : OwnOpaque::Compile(graph, {}, textures, layout, instanced);
        };
        for (const auto& p : compile(false)) payloads[p.policy->name] = p.compiled.payload;
        if (kind == Kind::MaskedLit) {
            for (const auto& p : OwnMasked::Compile(graph, textures, layout)) payloads[p.pass->name] = p.compiled.payload;
            versions = OwnMap::MaskedVersions(facts);
        } else {
            versions = OwnMap::LitVersions(facts);
        }
        if (usage.instanced) {
            for (const auto& p : compile(true)) payloads[std::string("instanced:") + p.policy->name] = p.compiled.payload;
            const auto instanced = OwnMap::InstancedVersions(OwnMap::LitVersions(facts));
            versions.insert(versions.end(), instanced.begin(), instanced.end());
        }
    } else if (kind == Kind::LitTranslucent) {
        for (const auto& p : OwnTranslucent::Compile(graph, textures, layout)) payloads[p.policy->name] = p.compiled.payload;
        versions = OwnMap::TranslucentLitVersions(facts);
    } else {
        const auto programs = OwnTranslucent::CompileUnlit(graph, textures, layout);
        payloads["translucent_unlit_ps"] = programs.at(0).compiled.payload;    // one program for both versions
        versions = OwnMap::TranslucentUnlitVersions(facts);
    }
    Result out;
    out.map = OwnMap::BuildVersions(versions, payloads, facts);
    out.material_layout = layout;
    out.versions = versions.size();
    out.programs = OwnMap::ProgramsOf(versions).size();
    return out;
}

// The same with the programs the plugin carries.
inline Result Build(const Json& graph, const Usage& usage, const std::map<std::string, std::int32_t>& texture_indices = {}) {
    return Build(graph, usage, ShippedVertexPrograms(), texture_indices);
}

// One library holding every material's map; a map two materials share is stored once.
inline Bytes Library(const std::vector<const Result*>& results) {
    std::vector<std::pair<Bytes, std::vector<Bytes>>> maps;
    for (const auto* r : results) {
        if (std::any_of(maps.begin(), maps.end(), [&](const auto& m) { return m.first == r->map.mapHash; })) continue;
        maps.push_back({r->map.mapHash, r->map.programs});
    }
    return OwnMap::Library(maps);
}

// The cooked material (header and body as the editor's cook writes them, the body ending in the package tag) with
// our block as its inline shader maps. The export is cooked with no shader map for the target (the count, 0, is its
// last four bytes), so the block takes those four bytes' place; the package's offsets and sizes are written again.
inline bool InsertBlock(const Bytes& header, const Bytes& body, const std::string& name, const Bytes& block,
                        Export::RewrittenPackage& out, std::string& error) {
    const auto package = ReadPackageHeader(header, body.size());
    if (!package.Error.Code.empty()) { error = "The cooked material could not be read (" + package.Error.Code + ")."; return false; }
    std::size_t index = package.Exports.size();
    for (std::size_t i = 0; i < package.Exports.size(); ++i)
        if (package.Exports[i].Name == name && package.Exports[i].Outer == 0) {
            if (index != package.Exports.size()) { error = "The cooked material has two exports named " + name + "."; return false; }
            index = i;
        }
    if (index == package.Exports.size()) { error = "The cooked package has no material named " + name + "."; return false; }
    const auto& material = package.Exports[index];
    if (material.Size < 8 || material.Offset + material.Size > body.size()) { error = "The cooked material's data is out of range."; return false; }
    const auto end = material.Offset + material.Size;
    if (body[end - 4] || body[end - 3] || body[end - 2] || body[end - 1]) {
        error = "The cooked material already has shader maps; Kadan writes its own in their place."; return false;
    }
    Bytes replaced(body.begin() + material.Offset, body.begin() + end - 4);
    replaced.insert(replaced.end(), block.begin(), block.end());
    return Export::RebuildCookedExports(header, body, package.Names, {{index, replaced}}, out, error);
}
} } }

// The version lists and facts own_material.py gives, for the own map writer.
// Part of own_map_writer.h, which includes every part.
#pragma once
#include "own_map_build.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace OwnMap
{
// ----- the version lists and facts own_material.py gives -----------------------------------------------------------
// The bytes of constants a program reading the Material buffer lists for it: 16 per vector program, 16 per four
// scalar programs, 0 when there are none.
inline std::uint32_t ConstantBytes(const UniformFacts& f)
{
    return std::uint32_t(16 * f.vectorPrograms.size() + 16 * ((f.scalarPrograms.size() + 3) / 4));
}
// Unlit opaque (stage 4): the static mesh base pass without a lightmap.
inline std::vector<Version> UnlitVersions()
{
    return {Version{"TBasePassVSFNoLightMapPolicy", 0, {{0, "View"}, {1, "Primitive"}, {3, "LocalVF"}},
                "static_mesh_vs_1uv", {}, {}},
        Version{"TBasePassPSFNoLightMapPolicy", 3, {{0, "View"}}, "unlit_ps_1uv", {}, {}}};
}
constexpr std::uint32_t COLLECTION_SIZE = 112; // MPC_Env's uniform buffer
// Lit opaque used with static lighting (stage 5): every static lighting policy, the pixel programs reading the
// Material buffer and one parameter collection.
inline std::vector<Version> LitVersions(const UniformFacts& facts)
{
    const auto constants = ConstantBytes(facts);
    std::vector<Version> out;
    const std::vector<std::pair<std::string, std::string>> vs{{"TBasePassVSFNoLightMapPolicy", "static_mesh_vs_1uv"},
        {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy", "static_mesh_vs_1uv"},
        {"TBasePassVSTLightMapPolicyHQ", "static_mesh_vs_lightmap_1uv"},
        {"TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQ", "static_mesh_vs_lightmap_df_1uv"}};
    for (const auto& [cls, program] : vs)
    {
        Version v{cls, 0, {{0, "View"}, {1, "Primitive"}, {3, "LocalVF"}}, program, {}, {std::nullopt}};
        if (cls.find("LightMapPolicyHQ") != std::string::npos)
            v.buffers[4] = "PrecomputedLightingBuffer";
        out.push_back(v);
    }
    for (const char* base : {"TBasePassPSFNoLightMapPolicy", "TBasePassPSFPrecomputedVolumetricLightmapLightingPolicy",
             "TBasePassPSTLightMapPolicyHQ", "TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQ"})
    {
        const bool lightmap = std::string(base).find("LightMapPolicyHQ") != std::string::npos;
        std::map<std::uint16_t, std::string> buffers = lightmap
            ? std::map<std::uint16_t, std::string>{{0, "View"}, {1, "Primitive"}, {3, "PrecomputedLightingBuffer"},
                  {4, "ReflectionCapture"}, {7, "LightmapResourceCluster"}}
            : std::map<std::uint16_t, std::string>{{0, "View"}, {1, "Primitive"}, {3, "ReflectionCapture"}};
        const std::uint16_t material = lightmap ? 6 : 5, collection = lightmap ? 5 : 4;
        for (const char* sky : {"", "Skylight"})
        {
            const std::string cls = std::string(base) + sky;
            out.push_back(
                Version{cls, 3, buffers, cls, Binding{material, constants}, {Binding{collection, COLLECTION_SIZE}}});
        }
    }
    return out;
}
// Lit translucent (Surface) used with static lighting (stage 6): seven policies, no parameter collection.
inline std::vector<Version> TranslucentLitVersions(const UniformFacts& facts)
{
    const auto constants = ConstantBytes(facts);
    const std::vector<std::pair<std::string, std::string>> vs{{"TBasePassVSFNoLightMapPolicy", "translucent_vs_1uv"},
        {"TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy", "translucent_vs_1uv"},
        {"TBasePassVSFSelfShadowedTranslucencyPolicy", "translucent_vs_1uv"},
        {"TBasePassVSFSelfShadowedVolumetricLightmapPolicy", "translucent_vs_1uv"},
        {"TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy", "translucent_vs_1uv"},
        {"TBasePassVSTLightMapPolicyHQ", "translucent_vs_lightmap_1uv"},
        {"TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQ", "translucent_vs_lightmap_df_1uv"}};
    std::vector<Version> out;
    for (const auto& [cls, program] : vs)
    {
        Version v{cls, 0, {{0, "View"}, {1, "Primitive"}, {2, "LocalVF"}}, program, {}, {}};
        if (cls.find("LightMapPolicyHQ") != std::string::npos)
            v.buffers[3] = "PrecomputedLightingBuffer";
        out.push_back(v);
    }
    for (const auto& [vsClass, program] : vs)
    {
        (void)program;
        const std::string base = "TBasePassPS" + vsClass.substr(std::string("TBasePassVS").size());
        std::map<std::uint16_t, std::string> buffers;
        std::uint16_t material;
        if (base.find("LightMapPolicyHQ") != std::string::npos)
        {
            buffers = {{0, "View"}, {1, "Primitive"}, {3, "PrecomputedLightingBuffer"}, {4, "ReflectionCapture"},
                {6, "LightmapResourceCluster"}};
            material = 5;
        }
        else if (base.find("CachedPoint") != std::string::npos)
        {
            buffers = {{0, "View"}, {1, "Primitive"}, {3, "IndirectLightingCache"}, {4, "ReflectionCapture"}};
            material = 5;
        }
        else
        {
            buffers = {{0, "View"}, {1, "Primitive"}, {3, "ReflectionCapture"}};
            material = 4;
        }
        for (const char* sky : {"", "Skylight"})
        {
            const std::string cls = base + sky;
            out.push_back(Version{cls, 3, buffers, cls, Binding{material, constants}, {}});
        }
    }
    return out;
}
// Unlit translucent fogged per vertex: one vertex shader and the pixel shader with and without the sky light.
inline std::vector<Version> TranslucentUnlitVersions(const UniformFacts& facts)
{
    const auto constants = ConstantBytes(facts);
    return {Version{"TBasePassVSFNoLightMapPolicy", 0, {{1, "View"}, {2, "Primitive"}, {3, "LocalVF"}},
                "translucent_fog_vs_1uv", {}, {}},
        Version{"TBasePassPSFNoLightMapPolicy", 3, {{0, "View"}}, "translucent_unlit_ps", Binding{2, constants}, {}},
        Version{"TBasePassPSFNoLightMapPolicySkylight", 3, {{0, "View"}}, "translucent_unlit_ps", Binding{2, constants},
            {}}};
}

inline std::string HashText(std::uint64_t h)
{
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

// Masked lit used with static lighting (stage 7): the lit opaque versions, then the depth-only and shadow depth vertex
// shaders, the one-pass point light geometry shader and the depth and shadow depth pixel shaders (classes known only
// by hash are given by it). Each object's class fields after the pass buffer slot are zeros, but for the loose bias
// parameter of two shadow depth vertex classes (buffer 104, offset 0, 16 bytes), whose loose parameter buffer the
// object lists.
inline std::vector<LooseBuffer> LooseBias()
{
    return {LooseBuffer{104, 16, {{0, 16}}}};
}
inline Bytes LooseTail()
{
    Bytes t(6, 0);
    Put16(t, 104);
    Put16(t, 0);
    Put16(t, 16);
    return t;
}
inline std::vector<Version> MaskedVersions(const UniformFacts& facts)
{
    const auto constants = ConstantBytes(facts);
    const std::vector<std::optional<Binding>> collections(facts.collections.size());
    auto out = LitVersions(facts);
    const std::map<std::uint16_t, std::string> shadowVs{
        {0, "View"}, {1, "Primitive"}, {2, "ShadowDepthPass"}, {3, "LocalVF"}},
        plainVs{{0, "View"}, {1, "Primitive"}, {2, "LocalVF"}};
    struct Row
    {
        const char* cls;
        std::uint64_t hashed;
        const std::map<std::uint16_t, std::string>* buffers;
        const char* program;
        std::uint16_t passSlot;
        Bytes tail;
        bool loose;
    };
    const std::vector<Row> vs{{"TDepthOnlyVS<false>", 0, &plainVs, "depth_only_vs", 0xFFFF, Bytes(), false},
        {nullptr, 0xa9009ee870e939cfULL, &shadowVs, "shadow_vs", 2, Bytes(12, 0), false},
        {nullptr, 0xcbe9c693cfb2c38bULL, &shadowVs, "shadow_loose_vs", 2, LooseTail(), true},
        {nullptr, 0xc63c98be7b628f29ULL, &shadowVs, "shadow_perspective_vs", 2, Bytes(12, 0), false},
        {nullptr, 0xaf3917912b8abd64ULL, &shadowVs, "shadow_perspective_loose_vs", 2, LooseTail(), true},
        {nullptr, 0xb44d45305ae55227ULL, &plainVs, "shadow_for_gs_vs", 0xFFFF, Bytes(12, 0), false},
        {nullptr, 0xf1e7faeb530ca58cULL, &plainVs, "shadow_for_gs_vs", 0xFFFF, Bytes(12, 0), false},
        {"TShadowDepthVSForGSVertexShadowDepth_OnePassPointLight", 0, &plainVs, "shadow_for_gs_onepass_vs", 0xFFFF,
            Bytes(12, 0), false}};
    for (const auto& r : vs)
    {
        Version v{r.cls ? r.cls : HashText(r.hashed), 0, *r.buffers, r.program, {}, collections};
        if (r.hashed)
            v.typeHash = r.hashed;
        v.passSlot = r.passSlot;
        v.tail = r.tail;
        if (r.loose)
            v.loose = LooseBias();
        out.push_back(v);
    }
    Version gs{"FOnePassPointShadowDepthGS", 4, {{0, "ShadowDepthPass"}}, "onepass_point_shadow_gs", {}, collections};
    gs.passSlot = 0;
    gs.tail = Bytes();
    out.push_back(gs);
    struct PixelRow
    {
        const char* cls;
        std::uint64_t hashed;
        std::map<std::uint16_t, std::string> buffers;
        std::uint16_t material, passSlot;
        Bytes tail;
    };
    const std::vector<PixelRow> ps{{"FDepthOnlyPS<false>", 0, {{0, "View"}}, 1, 0xFFFF, Bytes(6, 0)},
        {nullptr, 0x37a48ab631bfc57aULL, {{0, "View"}}, 1, 0xFFFF, Bytes(32, 0)},
        {nullptr, 0x7557e6252b46eb81ULL, {{0, "View"}}, 1, 0xFFFF, Bytes(32, 0)},
        {nullptr, 0x4bbfae84d2dbe82aULL, {{0, "View"}, {1, "ShadowDepthPass"}}, 2, 1, Bytes(32, 0)}};
    for (const auto& r : ps)
    {
        const std::string name = r.cls ? r.cls : HashText(r.hashed);
        Version v{name, 3, r.buffers, name, Binding{r.material, constants}, collections};
        if (r.hashed)
            v.typeHash = r.hashed;
        v.passSlot = r.passSlot;
        v.tail = r.tail;
        out.push_back(v);
    }
    return out;
}

// Instanced static meshes (stage 7): a lit material's versions again for FInstancedStaticMeshVertexFactory, in a mesh
// map of their own. The vertex objects bind the instanced factory's buffer three slots past the last, list the two
// loose parameter buffers (the seven instancing parameters at base 104, the instance offset at 117) and carry the
// instanced factory's parameter object; the pixel programs are the lit ones with their inputs one location later
// (program "instanced:<class>").
inline std::vector<LooseBuffer> InstancedLoose()
{
    LooseBuffer a{104, 112, {}};
    for (std::uint16_t i = 0; i < 7; i++)
        a.parameters.push_back({std::uint16_t(16 * i), 16});
    return {a, LooseBuffer{117, 4, {{0, 4}}}};
}
// FInstancedStaticMeshVertexFactoryShaderParameters: the local one's 12 bytes, then (buffer 104, offset, 16 bytes) for
// InstancingFadeOutParams, ViewZCompareZero, ViewZCompareOne, ViewZConstant, InstancingOffset, WorldViewOriginZero
// and WorldViewOriginOne, two parameters unbound, then the instance offset (buffer 117, offset 0, 4 bytes).
inline Bytes InstancedVfParams()
{
    Bytes o(12, 0);
    for (std::uint16_t offset : {96, 0, 16, 32, 80, 48, 64})
    {
        Put16(o, 104);
        Put16(o, offset);
        Put16(o, 16);
    }
    o.insert(o.end(), 12, 0);
    Put16(o, 117);
    Put16(o, 0);
    Put16(o, 4);
    return o;
}
inline std::vector<Version> InstancedVersions(const std::vector<Version>& versions)
{
    static const std::map<std::string, std::string> programs{{"static_mesh_vs_1uv", "instanced_vs_1uv"},
        {"static_mesh_vs_lightmap_1uv", "instanced_vs_lightmap_1uv"},
        {"static_mesh_vs_lightmap_df_1uv", "instanced_vs_lightmap_df_1uv"}};
    std::vector<Version> out;
    for (auto v : versions)
    {
        v.vf = HashedName("FInstancedStaticMeshVertexFactory");
        if (v.frequency == 0)
        {
            Need(!v.buffers.empty(), "an instanced vertex version binds buffers");
            v.buffers[std::uint16_t(v.buffers.rbegin()->first + 3)] = "044188379042fabe";
            auto it = programs.find(v.program);
            Need(it != programs.end(), "no instanced vertex program for " + v.program);
            v.program = it->second;
            v.loose = InstancedLoose();
            v.vfParams = InstancedVfParams();
        }
        else
            v.program = "instanced:" + v.program;
        out.push_back(v);
    }
    return out;
}

}
}
} // namespace P4MapKit::CustomMaterial::OwnMap

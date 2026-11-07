#pragma once
// Lit translucent (Surface lighting) base pass pixel programs of Kadan's own, without a donor material (stage 6 of
// tools/navigation/OWN_SHADER_MAPS.md). The lighting is Kadan's translucent Surface emitter from 0.2.0
// (TranslucentSurfaceProgram), now with the two lightmap policies a baked static mesh asks for. What 0.2.0 read from
// its donor is written here from named facts: for each of the fourteen policies a translucent material used with
// static lighting carries, which uniform buffer struct sits in which slot, and which resource of which buffer the
// program reads in which texture, sampler and buffer-view slot. Written for one material texture and no parameter
// collection, each description's header equals the game's own translucent Surface program's
// (M_stg_Wall_Translucent_00_Master) byte for byte (own_translucent_tool check).
#include "own_opaque.h"
#include "native_custom_backend_translucent_lit.h"
namespace P4MapKit { namespace CustomMaterial { namespace OwnTranslucent {

using OwnOpaque::Use;
constexpr std::uint64_t kIndirectLightingCache=0xcafbf523acdd8b5cull;
// The translucent base pass buffer (slot 2 in every policy) and the indirect lighting cache, bound by layout.
constexpr std::uint32_t kTranslucentBasePassLayout=99356142u, kIndirectLightingCacheLayout=69011706u;
constexpr unsigned kClusterSlot=6;

struct Policy {
    const char* name;
    std::uint64_t type_hash;
    bool lightmap, sky, cached;
    unsigned material;                                // the Material buffer's slot
    std::vector<Use> textures, samplers, srvs;        // the scene's resources
    std::array<std::uint8_t,3> counts;                // texture slots, sampler slots, uniform buffers (one material texture)
};

// The translucent base pass buffer's resources each policy reads (the directional shadow map at 0, the forward
// lights' data at 4 to 6, the sky and reflection cube maps at 16 to 21, the volumetric fog volume at 28) and the
// View's (the volumetric lightmap at 2 to 10, the shared clamped sampler at 43, the sky irradiance at 51); with a
// lightmap, the lightmap's textures and sampler in the lightmap resource cluster (slot 6). No parameter collection:
// the programs read none, so the Material buffer follows the scene's buffers.
inline const std::vector<Policy>& Policies() {
    static const std::vector<Policy> all = {
        {"TBasePassPSFNoLightMapPolicy",0x4507f03a199c6526ull,false,false,false,4,{{0,2,8},{0,11,7},{2,0,6},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,21,3}},{{2,4,5},{2,5,4},{2,6,3}},{{9,5,5}}},
        {"TBasePassPSFNoLightMapPolicySkylight",0xa8d53bcb8fca168dull,false,true,false,4,{{0,2,10},{0,11,9},{2,0,8},{2,16,4},{2,18,3},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,17,4},{2,19,5},{2,21,3}},{{0,51,0},{2,4,7},{2,5,6},{2,6,5}},{{11,7,5}}},
        {"TBasePassPSFSelfShadowedTranslucencyPolicy",0x9134506f7bff0ffaull,false,false,false,4,{{0,2,8},{0,11,7},{2,0,6},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,21,3}},{{2,4,5},{2,5,4},{2,6,3}},{{9,5,5}}},
        {"TBasePassPSFSelfShadowedTranslucencyPolicySkylight",0xba9b0d30dcc48e73ull,false,true,false,4,{{0,2,10},{0,11,9},{2,0,8},{2,16,4},{2,18,3},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,17,4},{2,19,5},{2,21,3}},{{0,51,0},{2,4,7},{2,5,6},{2,6,5}},{{11,7,5}}},
        {"TBasePassPSFPrecomputedVolumetricLightmapLightingPolicy",0x666403914542b619ull,false,false,false,4,{{0,2,15},{0,3,14},{0,4,13},{0,5,12},{0,6,11},{0,7,10},{0,8,9},{0,9,8},{0,11,7},{2,0,6},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,21,3}},{{2,4,5},{2,5,4},{2,6,3}},{{16,5,5}}},
        {"TBasePassPSFPrecomputedVolumetricLightmapLightingPolicySkylight",0x18a82ebde5d3d122ull,false,true,false,4,{{0,2,18},{0,3,17},{0,4,16},{0,5,15},{0,6,14},{0,7,13},{0,8,12},{0,9,11},{0,10,10},{0,11,9},{2,0,8},{2,16,4},{2,18,3},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,17,4},{2,19,5},{2,21,3}},{{0,51,0},{2,4,7},{2,5,6},{2,6,5}},{{19,7,5}}},
        {"TBasePassPSFSelfShadowedVolumetricLightmapPolicy",0x787d13a1ace9957dull,false,false,false,4,{{0,2,15},{0,3,14},{0,4,13},{0,5,12},{0,6,11},{0,7,10},{0,8,9},{0,9,8},{0,11,7},{2,0,6},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,21,3}},{{2,4,5},{2,5,4},{2,6,3}},{{16,5,5}}},
        {"TBasePassPSFSelfShadowedVolumetricLightmapPolicySkylight",0x3c79a2188506dc5bull,false,true,false,4,{{0,2,18},{0,3,17},{0,4,16},{0,5,15},{0,6,14},{0,7,13},{0,8,12},{0,9,11},{0,10,10},{0,11,9},{2,0,8},{2,16,4},{2,18,3},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,17,4},{2,19,5},{2,21,3}},{{0,51,0},{2,4,7},{2,5,6},{2,6,5}},{{19,7,5}}},
        {"TBasePassPSFSelfShadowedCachedPointIndirectLightingPolicy",0xe38d98e8b45169dbull,false,false,true,5,{{0,2,8},{0,11,7},{2,0,6},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,21,3}},{{2,4,5},{2,5,4},{2,6,3}},{{9,5,6}}},
        {"TBasePassPSFSelfShadowedCachedPointIndirectLightingPolicySkylight",0x9f63684dbca4b663ull,false,true,true,5,{{0,2,10},{0,11,9},{2,0,8},{2,16,4},{2,18,3},{2,20,2},{2,28,1}},{{0,43,1},{2,1,2},{2,17,4},{2,19,5},{2,21,3}},{{0,51,0},{2,4,7},{2,5,6},{2,6,5}},{{11,7,6}}},
        {"TBasePassPSTLightMapPolicyHQ",0x6d90922c199fe04full,true,false,false,5,{{2,0,7},{2,20,3},{2,28,2},{6,0,1}},{{0,43,4},{2,1,2},{2,21,3},{6,9,1}},{{2,4,6},{2,5,5},{2,6,4}},{{8,6,6}}},
        {"TBasePassPSTLightMapPolicyHQSkylight",0xdbbfd1d594df3611ull,true,true,false,5,{{2,0,10},{2,16,6},{2,18,5},{2,20,4},{2,28,3},{6,0,2},{6,1,1}},{{0,43,6},{2,1,2},{2,17,4},{2,19,5},{2,21,3},{6,9,1}},{{0,51,0},{2,4,9},{2,5,8},{2,6,7}},{{11,8,6}}},
        {"TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQ",0x28b73d7ae30b556cull,true,false,false,5,{{2,0,8},{2,20,4},{2,28,3},{6,0,2},{6,3,1}},{{0,43,4},{2,1,2},{2,21,3},{6,9,1}},{{2,4,7},{2,5,6},{2,6,5}},{{9,6,6}}},
        {"TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQSkylight",0x661e2d85a2c9dbe6ull,true,true,false,5,{{2,0,11},{2,16,7},{2,18,6},{2,20,5},{2,28,4},{6,0,3},{6,1,2},{6,3,1}},{{0,43,6},{2,1,2},{2,17,4},{2,19,5},{2,21,3},{6,9,1}},{{0,51,0},{2,4,10},{2,5,9},{2,6,8}},{{12,8,6}}},
    };
    return all;
}

// Which struct the shader object lists in which slot (the translucent base pass buffer in 2 is the pass's own).
inline std::map<std::uint16_t,std::uint64_t> Structs(const Policy& p) {
    using namespace OwnOpaque;
    if (p.lightmap) return {{0,kView},{1,kPrimitive},{3,kPrecomputedLighting},{4,kReflectionCapture},{std::uint16_t(kClusterSlot),kLightmapCluster}};
    if (p.cached) return {{0,kView},{1,kPrimitive},{3,kIndirectLightingCache},{4,kReflectionCapture}};
    return {{0,kView},{1,kPrimitive},{3,kReflectionCapture}};
}

// The interpolants our translucent vertex shader writes, by location (own_shaders/static_mesh_base_pass.vert with
// TRANSLUCENT): the tangent's X, the tangent's Z, the texture coordinates and, with a lightmap, its coordinates.
inline std::vector<Varying> Inputs(const Policy& p) {
    std::vector<Varying> out{{"in_texcoord10_centroid",0},{"in_texcoord11_centroid",1},{"in_texcoord0",2}};
    if (p.lightmap) out.push_back({"in_texcoord4",3});
    return out;
}

// A policy's binding description with one material texture (the Material buffer's resource 0 in texture slot 0, its
// sampler, resource 1, in the first free sampler slot) and the material's layout hash.
inline ShaderBindings OneTextureBindings(const Policy& p, std::uint32_t material_layout) {
    ShaderBindings h;
    const unsigned buffers = p.lightmap ? kClusterSlot + 1 : p.material + 1;
    ResourceMap textures, samplers, srvs;
    for (const auto& u : p.textures) textures[u.buffer].push_back({u.resource,u.slot});
    for (const auto& u : p.samplers) samplers[u.buffer].push_back({u.resource,u.slot});
    for (const auto& u : p.srvs) srvs[u.buffer].push_back({u.resource,u.slot});
    textures[p.material].push_back({0,0});
    std::set<unsigned> taken; for (const auto& u : p.samplers) taken.insert(u.slot);
    unsigned material_sampler=0; while (taken.count(material_sampler)) ++material_sampler;
    samplers[p.material].push_back({1,material_sampler});
    taken.insert(material_sampler);
    h.default_sampler = std::int32_t(*taken.rbegin()+1);
    h.inputs = Inputs(p);
    for (const auto* table : {&p.textures,&p.samplers,&p.srvs}) for (const auto& u : *table) h.resource_bits |= 1u<<u.buffer;
    h.resource_bits |= 1u<<p.material;
    h.srv = BuildResourceTable(srvs,buffers);
    h.samplers = BuildResourceTable(samplers,buffers);
    h.uav = BuildResourceTable({},buffers);
    h.layout_hashes.assign(buffers,0);
    h.layout_hashes[0]=OwnOpaque::kViewLayout; h.layout_hashes[2]=kTranslucentBasePassLayout; h.layout_hashes[p.material]=material_layout;
    if (p.cached) h.layout_hashes[3]=kIndirectLightingCacheLayout;
    if (p.lightmap) h.layout_hashes[kClusterSlot]=OwnOpaque::kLightmapClusterLayout;
    h.textures = BuildResourceTable(textures,buffers);
    h.inout_mask = 1;
    h.counts = {{p.counts[0],p.counts[1],p.counts[2],std::uint8_t(p.sky?1:0),0}};
    return h;
}

inline FMaterialShaderReference Reference(const Policy& p) {
    FMaterialShaderReference r;
    r.TypeHash=p.type_hash; r.VertexFactoryHash=OwnOpaque::kLocalVertexFactory; r.Frequency=3;
    r.UniformBufferHashes=Structs(p);
    r.NamedBuffers[std::uint16_t(p.material)]="Material";
    return r;
}

struct Program { const Policy* policy; NativeBackend::PixelProgram compiled; ShaderBindings bindings; };

// Every policy's program for a creator's lit translucent graph: the scene bound as the game's translucent Surface
// programs bind it, the material's textures rebound, the sky light read from the View's sky irradiance.
inline std::vector<Program> Compile(const Json& graph, const std::vector<TextureParameter>& textures, std::uint32_t material_layout) {
    const auto issue = NativeBackend::TranslucentLitAuthoringIssue(graph,{},textures);
    if (!issue.empty()) throw std::runtime_error(issue);
    const auto uniforms = GraphUniforms(InlineFunctions(graph,Json()),textures);
    std::vector<Program> out;
    for (const auto& p : Policies()) try {
        const auto ref = Reference(p);
        auto rebound = RebindMaterialTextures(OneTextureBindings(p,material_layout),p.material,unsigned(textures.size()),material_layout);
        rebound.bindings.layout_hashes.at(p.material)=material_layout;
        auto bindings = ResolveMaterialBindings(rebound.bindings,p.material,{});
        for (std::size_t i=0;i<textures.size();++i) bindings.textures[textures[i].name]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
        const auto assembly = Lighting::TranslucentSurfaceProgram(graph,uniforms,rebound.bindings,ref,p.material,{},bindings.textures);
        // The header lists the interpolants the program reads.
        std::vector<Varying> read;
        for (const auto& v : rebound.bindings.inputs)
            if (assembly.find("fragment.attrib["+std::to_string(v.location)+"]")!=std::string::npos) read.push_back(v);
        rebound.bindings.inputs = read;
        auto compiled = p.sky
            ? NativeBackend::CompilePixelAssembly(assembly,rebound.bindings,NativeBackend::OwnTranslucentSkyIrradianceStorage(rebound.bindings,p.material))
            : NativeBackend::CompilePixelAssembly(assembly,rebound.bindings);
        out.push_back({&p,std::move(compiled),rebound.bindings});
    } catch (const std::exception& error) { throw std::runtime_error(std::string(p.name) + ": " + error.what()); }
    return out;
}

// Unlit translucent (fogged per vertex, UE's default): the two pixel versions the renderer asks of it, with and
// without the sky light (the same program), View in slot 0, the translucent pass buffer in 1 and the Material buffer in
// 2; the program reads the volumetric fog volume (pass resource 28) with the View's shared clamped sampler (43). The
// interpolants are our vertex fog vertex shader's (translucent_fog_vs_1uv). Written for one material texture, each
// header equals the game's (M_demo_Translucent_Unlit_00_Master) byte for byte (own_translucent_tool check-unlit).
struct UnlitPolicy { const char* name; std::uint64_t type_hash; };
inline const std::vector<UnlitPolicy>& UnlitPolicies() {
    static const std::vector<UnlitPolicy> all = {{"TBasePassPSFNoLightMapPolicy",0x4507f03a199c6526ull},
                                                 {"TBasePassPSFNoLightMapPolicySkylight",0xa8d53bcb8fca168dull}};
    return all;
}
constexpr unsigned kUnlitMaterialSlot=2;
inline ShaderBindings UnlitOneTextureBindings(std::uint32_t material_layout) {
    ShaderBindings h;
    const unsigned buffers = kUnlitMaterialSlot + 1;
    h.textures = BuildResourceTable({{1,{{28,1}}},{kUnlitMaterialSlot,{{0,0}}}},buffers);
    h.samplers = BuildResourceTable({{0,{{43,1}}},{kUnlitMaterialSlot,{{1,0}}}},buffers);
    h.srv = BuildResourceTable({},buffers);
    h.uav = BuildResourceTable({},buffers);
    h.layout_hashes = {OwnOpaque::kViewLayout,kTranslucentBasePassLayout,material_layout};
    h.resource_bits = 1u|2u|(1u<<kUnlitMaterialSlot);
    h.default_sampler = -1;
    h.inputs = {{"in_texcoord10_centroid",0},{"in_texcoord11_centroid",1},{"in_texcoord0",2},{"in_texcoord7",3}};
    h.inout_mask = 1;
    h.counts = {{2,2,3,0,0}};
    return h;
}
inline FMaterialShaderReference UnlitReference(const UnlitPolicy& p) {
    FMaterialShaderReference r;
    r.TypeHash=p.type_hash; r.VertexFactoryHash=OwnOpaque::kLocalVertexFactory; r.Frequency=3;
    r.UniformBufferHashes={{0,OwnOpaque::kView}};
    r.NamedBuffers[std::uint16_t(kUnlitMaterialSlot)]="Material";
    return r;
}
struct UnlitProgram { const UnlitPolicy* policy; NativeBackend::PixelProgram compiled; ShaderBindings bindings; };
inline std::vector<UnlitProgram> CompileUnlit(const Json& graph, const std::vector<TextureParameter>& textures, std::uint32_t material_layout) {
    const auto issue = NativeBackend::TranslucentAuthoringIssue(graph,{},textures);
    if (!issue.empty()) throw std::runtime_error(issue);
    const auto uniforms = GraphUniforms(InlineFunctions(graph,Json()),textures);
    std::vector<UnlitProgram> out;
    for (const auto& p : UnlitPolicies()) try {
        const auto original = UnlitOneTextureBindings(material_layout);
        NativeBackend::RequireTranslucentBindings(original,UnlitReference(p));
        auto rebound = RebindMaterialTextures(original,kUnlitMaterialSlot,unsigned(textures.size()),material_layout);
        rebound.bindings.layout_hashes[kUnlitMaterialSlot]=material_layout;
        auto bindings = ResolveMaterialBindings(rebound.bindings,kUnlitMaterialSlot,{});
        for (std::size_t i=0;i<textures.size();++i) bindings.textures[textures[i].name]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
        const auto assembly = NativeBackend::TranslucentUnlitProgram(graph,uniforms,original,kUnlitMaterialSlot,{});
        auto compiled = NativeBackend::ResolveAndCompilePixel(assembly,bindings.textures,bindings.varyings,rebound.bindings);
        out.push_back({&p,std::move(compiled),rebound.bindings});
    } catch (const std::exception& error) { throw std::runtime_error(std::string(p.name) + ": " + error.what()); }
    return out;
}
} } }

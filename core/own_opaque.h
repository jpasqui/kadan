#pragma once
// Lit opaque base pass pixel programs of Kadan's own, without a donor material (stage 5 of
// tools/navigation/OWN_SHADER_MAPS.md). The lighting is Kadan's opaque forward emitter (OpaqueMaterialProgram), as
// in 0.2.0. What 0.2.0 read from a game material is written here from named facts: for each lighting policy, which
// uniform buffer struct sits in which slot, and which resource of which buffer the program reads in which texture,
// sampler and buffer-view slot. Written for one material texture, each description's header equals the game's own
// lit opaque program's byte for byte (own_opaque_tool check); RebindMaterialTextures then places the material's own
// textures, as for any creator material.
#include "native_custom_backend_opaque.h"
#include "material_engine_hashes.h"
namespace P4MapKit { namespace CustomMaterial { namespace OwnOpaque {

constexpr std::uint64_t kView=EngineHash::ViewUniformBuffer, kPrimitive=0x60929d69929b2b59ull,
    kReflectionCapture=0xf135e29855d4d0beull, kPrecomputedLighting=0x365a69720e7582b9ull,
    kLightmapCluster=0x198d8af2b3acf133ull, kLocalVertexFactory=EngineHash::LocalVertexFactory;
// Layout hashes of the scene buffers the programs bind by layout (View, the opaque base pass buffer, the lightmap
// resource cluster); the others are bound by the object's buffer list and carry 0.
constexpr std::uint32_t kViewLayout=375850487u, kBasePassLayout=222823682u, kLightmapClusterLayout=193792908u;
// Slot 2 is the opaque base pass buffer in every policy.
constexpr unsigned kViewSlot=0, kPrimitiveSlot=1, kBasePassSlot=2;

struct Use { unsigned buffer, resource, slot; };
struct Policy {
    const char* name;                 // the shader class
    std::uint64_t type_hash;
    bool lightmap, sky;
    unsigned material, collection;    // the Material buffer's slot, the parameter collection's slot
    std::map<std::uint16_t,std::uint64_t> structs;   // slot -> struct, as the shader object lists them
    std::vector<Use> textures, samplers, srvs;       // the scene's resources
    std::array<std::uint8_t,3> counts;                // texture slots, sampler slots, uniform buffers (one material texture)
    // and one storage buffer when the program reads the sky irradiance (the sky light variants)
};

// The opaque base pass buffer's resources each policy reads (the forward lights' data, shadow maps, the
// volumetric fog volume at 28) and the View's (the volumetric lightmap's textures at 2 to 10, the sky irradiance
// at 51, the shared clamped sampler at 43). Without a lightmap, View 11 too; with one, the lightmap's textures and
// sampler in the lightmap resource cluster (slot 7).
inline const std::vector<Policy>& Policies() {
    static const std::vector<Policy> all = [] {
        const std::map<std::uint16_t,std::uint64_t> plain={{0,kView},{1,kPrimitive},{3,kReflectionCapture}};
        const std::map<std::uint16_t,std::uint64_t> mapped={{0,kView},{1,kPrimitive},{7,kLightmapCluster},{3,kPrecomputedLighting},{4,kReflectionCapture}};
        std::vector<Policy> out;
        out.push_back({"TBasePassPSFNoLightMapPolicy",0x4507f03a199c6526ull,false,false,5,4,plain,
            {{0,2,12},{0,11,11},{2,20,6},{2,28,5},{2,49,4},{2,52,3},{2,56,2},{2,58,1}},
            {{0,43,2},{2,21,4},{2,53,1}}, {{2,4,9},{2,5,8},{2,6,7}}, {{12,5,6}}});
        out.push_back({"TBasePassPSFNoLightMapPolicySkylight",0xa8d53bcb8fca168dull,false,true,5,4,plain,
            {{0,2,14},{0,11,13},{2,16,8},{2,18,7},{2,20,6},{2,28,5},{2,49,4},{2,52,3},{2,56,2},{2,58,1}},
            {{0,43,2},{2,17,5},{2,19,6},{2,21,4},{2,53,1}}, {{0,51,0},{2,4,11},{2,5,10},{2,6,9}}, {{14,7,6}}});
        out.push_back({"TBasePassPSFPrecomputedVolumetricLightmapLightingPolicy",0x666403914542b619ull,false,false,5,4,plain,
            {{0,2,19},{0,3,18},{0,4,17},{0,5,16},{0,6,15},{0,7,14},{0,8,13},{0,9,12},{0,11,11},{2,20,6},{2,28,5},{2,49,4},{2,52,3},{2,56,2},{2,58,1}},
            {{0,43,2},{2,21,4},{2,53,1}}, {{2,4,9},{2,5,8},{2,6,7}}, {{19,5,6}}});
        out.push_back({"TBasePassPSFPrecomputedVolumetricLightmapLightingPolicySkylight",0x18a82ebde5d3d122ull,false,true,5,4,plain,
            {{0,2,22},{0,3,21},{0,4,20},{0,5,19},{0,6,18},{0,7,17},{0,8,16},{0,9,15},{0,10,14},{0,11,13},{2,16,8},{2,18,7},{2,20,6},{2,28,5},{2,49,4},{2,52,3},{2,56,2},{2,58,1}},
            {{0,43,2},{2,17,5},{2,19,6},{2,21,4},{2,53,1}}, {{0,51,0},{2,4,11},{2,5,10},{2,6,9}}, {{22,7,6}}});
        out.push_back({"TBasePassPSTLightMapPolicyHQ",0x6d90922c199fe04full,true,false,6,5,mapped,
            {{2,20,8},{2,28,7},{2,49,6},{2,52,5},{2,56,4},{2,58,3},{7,0,2},{7,2,1}},
            {{0,43,5},{2,21,4},{2,53,2},{7,9,0}}, {{2,4,11},{2,5,10},{2,6,9}}, {{12,6,7}}});
        out.push_back({"TBasePassPSTLightMapPolicyHQSkylight",0xdbbfd1d594df3611ull,true,true,6,5,mapped,
            {{2,16,11},{2,18,10},{2,20,9},{2,28,8},{2,49,7},{2,52,6},{2,56,5},{2,58,4},{7,0,3},{7,1,2},{7,2,1}},
            {{0,43,7},{2,17,5},{2,19,6},{2,21,4},{2,53,2},{7,9,0}}, {{0,51,0},{2,4,14},{2,5,13},{2,6,12}}, {{15,8,7}}});
        out.push_back({"TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQ",0x28b73d7ae30b556cull,true,false,6,5,mapped,
            {{2,20,9},{2,28,8},{2,49,7},{2,52,6},{2,56,5},{2,58,4},{7,0,3},{7,2,2},{7,3,1}},
            {{0,43,5},{2,21,4},{2,53,2},{7,9,0}}, {{2,4,12},{2,5,11},{2,6,10}}, {{13,6,7}}});
        out.push_back({"TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQSkylight",0x661e2d85a2c9dbe6ull,true,true,6,5,mapped,
            {{2,16,12},{2,18,11},{2,20,10},{2,28,9},{2,49,8},{2,52,7},{2,56,6},{2,58,5},{7,0,4},{7,1,3},{7,2,2},{7,3,1}},
            {{0,43,7},{2,17,5},{2,19,6},{2,21,4},{2,53,2},{7,9,0}}, {{0,51,0},{2,4,15},{2,5,14},{2,6,13}}, {{16,8,7}}});
        return out;
    }();
    return all;
}

// The interpolants our static mesh vertex shader writes, by location (own_shaders/static_mesh_base_pass.vert): a
// program may read any of them, so the tangent's X (location 0) is there for normal maps. The instanced vertex shader
// (INSTANCED) writes the per-instance parameters at 2, so the rest come one location later.
inline std::vector<Varying> Inputs(const Policy& p, bool tangent_x, bool instanced=false) {
    std::vector<Varying> out;
    const int after = instanced ? 1 : 0;
    if (tangent_x) out.push_back({"in_texcoord10_centroid",0});
    out.push_back({"in_texcoord11_centroid",1});
    out.push_back({"in_texcoord0",2+after});
    if (p.lightmap) out.push_back({"in_texcoord4",3+after});
    out.push_back({"in_texcoord7",(p.lightmap?4:3)+after});
    return out;
}

// A policy's binding description with one material texture (the Material buffer's resource 0 in texture slot 0,
// its sampler, resource 1, in the first free sampler slot) and the material's layout hash.
inline ShaderBindings OneTextureBindings(const Policy& p, std::uint32_t material_layout, bool tangent_x=false,
    bool instanced=false) {
    ShaderBindings h;
    const unsigned buffers = p.lightmap ? 8u : 6u;
    ResourceMap textures, samplers, srvs;
    for (const auto& u : p.textures) textures[u.buffer].push_back({u.resource,u.slot});
    for (const auto& u : p.samplers) samplers[u.buffer].push_back({u.resource,u.slot});
    for (const auto& u : p.srvs) srvs[u.buffer].push_back({u.resource,u.slot});
    textures[p.material].push_back({0,0});
    std::set<unsigned> taken; for (const auto& u : p.samplers) taken.insert(u.slot);
    unsigned material_sampler=0; while (taken.count(material_sampler)) ++material_sampler;
    samplers[p.material].push_back({1,material_sampler});
    taken.insert(material_sampler);
    h.default_sampler = std::int32_t(*taken.rbegin()+1);  // past the last sampler slot taken
    h.inputs = Inputs(p,tangent_x,instanced);
    for (const auto* table : {&p.textures,&p.samplers,&p.srvs}) for (const auto& u : *table) h.resource_bits |= 1u<<u.buffer;
    h.resource_bits |= 1u<<p.material;
    h.srv = BuildResourceTable(srvs,buffers);
    h.samplers = BuildResourceTable(samplers,buffers);
    h.uav = BuildResourceTable({},buffers);
    h.layout_hashes.assign(buffers,0);
    h.layout_hashes[kViewSlot]=kViewLayout; h.layout_hashes[kBasePassSlot]=kBasePassLayout; h.layout_hashes[p.material]=material_layout;
    if (p.lightmap) h.layout_hashes[7]=kLightmapClusterLayout;
    h.textures = BuildResourceTable(textures,buffers);
    h.inout_mask = 1;
    h.counts = {{p.counts[0],p.counts[1],p.counts[2],std::uint8_t(p.sky?1:0),0}};
    return h;
}

// The shader reference facts the emitter reads: the class, the factory and the uniform buffers by slot.
inline FMaterialShaderReference Reference(const Policy& p) {
    FMaterialShaderReference r;
    r.TypeHash=p.type_hash; r.VertexFactoryHash=kLocalVertexFactory; r.Frequency=3;
    r.UniformBufferHashes=p.structs;
    r.NamedBuffers[std::uint16_t(p.material)]="Material";
    r.NamedBuffers[std::uint16_t(p.collection)]="MaterialCollection0";
    return r;
}

// The material's 2D textures as the graph names them: each texture parameter once, in the graph's order, with its
// sampler source (0 the texture's own sampler, 1 and 2 the world group's shared wrap and clamp samplers).
inline std::vector<TextureParameter> GraphTextures(const Json& graph) {
    std::vector<TextureParameter> out;
    std::set<std::string> seen;
    for (const auto& node : Get(graph,"nodes").object) {
        const auto type = Text(node.second,"type");
        if (type!="TextureSampleParameter2D" && type!="TextureObjectParameter") continue;
        const auto& props = Get(node.second,"props");
        const auto name = Text(props,"ParameterName");
        if (!seen.insert(name).second) continue;
        const auto source = props.Has("SamplerSource") ? props.At("SamplerSource").Text() : std::string("SSM_FromTextureAsset");
        out.push_back({name, std::uint8_t(source=="SSM_Wrap_WorldGroupSettings" ? 1 : source=="SSM_Clamp_WorldGroupSettings" ? 2 : 0)});
    }
    return out;
}

// The material's parameters as the programs read them: vectors at 16 bytes each, then scalars four to a float4
// (GraphUniforms), each computed by a preshader that reads its parameter.
inline OwnedUniforms Uniforms(const Json& graph, const std::vector<TextureParameter>& textures) {
    return GraphUniforms(InlineFunctions(graph,Json()),textures);
}

// The Material uniform buffer's layout hash for those parameters (FUniformExpressionSet::CreateBufferStruct, no static
// slot), as own_uniforms.py writes the layout.
inline std::uint32_t MaterialLayoutHash(const OwnedUniforms& u) {
    const auto layout = UniformLayout(unsigned(u.vectors.size()),unsigned(u.scalars.size()),unsigned(u.textures.size()));
    return LayoutHash(layout.first,0xFFu,layout.second);
}

// The facts own_uniforms.py writes the map's uniform expression set from: each parameter's name and default (as
// float bits) and each texture's name and sampler source, in the order the programs read them.
inline std::string UniformFacts(const OwnedUniforms& u) {
    auto bits = [](double value) { const float f = float(value); std::uint32_t b; std::memcpy(&b,&f,4); return double(b); };
    auto parameters = [&](const std::vector<UniformParameter>& list) {
        auto out = Json::MakeArray();
        for (const auto& p : list) {
            auto item = Json::MakeObject(); item["name"] = p.name;
            auto values = Json::MakeArray();
            for (auto v : p.defaults) values.array.push_back(Json(bits(v)));
            item["default"] = values;
            out.array.push_back(item);
        }
        return out;
    };
    auto textures = Json::MakeArray();
    for (const auto& t : u.textures) {
        auto item = Json::MakeObject(); item["name"] = t.name; item["sampler"] = Json(int(t.sampler_source));
        textures.array.push_back(item);
    }
    auto out = Json::MakeObject();
    out["vectors"] = parameters(u.vectors); out["scalars"] = parameters(u.scalars); out["textures"] = textures;
    return out.Dump();
}

struct Program { const Policy* policy; NativeBackend::PixelProgram compiled; ShaderBindings bindings; };

// Every lit policy's program for a creator graph: the scene bound as the game's lit programs bind it, the
// material's textures rebound, the sky light read from the View's sky irradiance (slot from our own description).
inline std::vector<Program> Compile(const Json& graph, const std::map<std::string,std::string>& aliases,
    const std::vector<TextureParameter>& textures, std::uint32_t material_layout, bool instanced=false) {
    const auto issue = NativeBackend::OpaqueAuthoringIssue(graph,aliases,textures,true);
    if (!issue.empty()) throw std::runtime_error(issue);
    if (Text(graph,"shading_model")!="MSM_DefaultLit") throw std::runtime_error("own opaque programs are lit");
    const auto uniforms = GraphUniforms(InlineFunctions(graph,Json()),textures);
    std::vector<Program> out;
    for (const auto& p : Policies()) {
        const auto ref = Reference(p);
        // Our vertex shader always writes the tangent's X; the header lists it only when the program reads it.
        auto rebound = RebindMaterialTextures(OneTextureBindings(p,material_layout,true,instanced),p.material,unsigned(textures.size()),material_layout);
        rebound.bindings.layout_hashes.at(p.material)=material_layout;   // the Material buffer keeps its layout without textures
        auto bindings = ResolveMaterialBindings(rebound.bindings,p.material,{});
        for (std::size_t i=0;i<textures.size();++i) bindings.textures[textures[i].name]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
        Emitter emitter(false,Flag(graph,"two_sided")); emitter.owned_uniforms=true; emitter.masked_material=true;
        emitter.material_buffer=p.material; emitter.material_parameters=uniforms.bindings;
        const auto assembly = Lighting::OpaqueMaterialProgram(graph,rebound.bindings,ref.UniformBufferHashes,ref.TypeHash,
            NativeBackend::CreatorOpaqueDecalResponse(graph),aliases,bindings.textures,Json(),true,
            Lighting::OpaqueDirectionalResponse::SurfaceShadow,std::move(emitter));
        if (assembly.find("fragment.attrib[0]")==std::string::npos)
            rebound.bindings.inputs.erase(rebound.bindings.inputs.begin());
        auto compiled = Lighting::OriginalIndirectPolicy(ref.TypeHash).sky
            ? NativeBackend::CompilePixelAssembly(assembly,rebound.bindings,NativeBackend::OwnSkyIrradianceStorage(rebound.bindings,p.material))
            : NativeBackend::CompilePixelAssembly(assembly,rebound.bindings);
        out.push_back({&p,std::move(compiled),rebound.bindings});
    }
    return out;
}

// Unlit opaque: the same policies and headers, each program the material's emissive under the vertex and volumetric
// fog (as 0.2.0 wrote unlit opaque programs on a lit game material). The renderer asks an unlit material for the
// policy without a lightmap only; the other versions keep the map the lit map's shape.
inline std::vector<Program> CompileUnlit(const Json& graph, const std::map<std::string,std::string>& aliases,
    const std::vector<TextureParameter>& textures, std::uint32_t material_layout, bool instanced=false) {
    const auto issue = NativeBackend::OpaqueAuthoringIssue(graph,aliases,textures);
    if (!issue.empty()) throw std::runtime_error(issue);
    if (Text(graph,"shading_model")!="MSM_Unlit") throw std::runtime_error("own unlit opaque programs need an unlit graph");
    const auto uniforms = GraphUniforms(InlineFunctions(graph,Json()),textures);
    std::vector<Program> out;
    for (const auto& p : Policies()) {
        const auto ref = Reference(p);
        auto rebound = RebindMaterialTextures(OneTextureBindings(p,material_layout,true,instanced),p.material,unsigned(textures.size()),material_layout);
        rebound.bindings.layout_hashes.at(p.material)=material_layout;
        auto bindings = ResolveMaterialBindings(rebound.bindings,p.material,{});
        for (std::size_t i=0;i<textures.size();++i) bindings.textures[textures[i].name]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
        Emitter emitter(false,Flag(graph,"two_sided")); emitter.owned_uniforms=true;
        emitter.material_buffer=p.material; emitter.material_parameters=uniforms.bindings;
        Lighting::PolicyEmitter geometryPolicy(emitter,rebound.bindings);
        const auto geometry=Lighting::EmitOriginalForwardGeometry(geometryPolicy,rebound.bindings,ref.UniformBufferHashes);
        Lighting::InstallOriginalForwardGeometryCache(emitter,geometry);
        auto inputs=CompileTranslucentInputs(graph,std::move(emitter),aliases);
        Lighting::PolicyEmitter policy(inputs.emitter,rebound.bindings); policy.buffers=geometryPolicy.buffers;
        const auto fog=policy.ApplyVolumetricFog(geometry.worldPosition,geometry.vertexFog);
        const auto emission=inputs.emitter.op("MAX",3,{inputs.pins.at("EmissiveColor"),Lighting::F32({0})});
        const auto color=Lighting::FogComposite(inputs.emitter,emission,fog,policy.Load(0,2180,1));
        const auto outputColor=inputs.emitter.append(color,Lighting::F32({0}));
        inputs.emitter.emit("MOV.F32 result_color0, "+outputColor.text);
        const auto assembly=Lighting::ForwardReuseTemporaries(Resolve(policy.Program(),bindings.textures,Lighting::OpaquePolicyVaryings(rebound.bindings)));
        if (assembly.find("fragment.attrib[0]")==std::string::npos)
            rebound.bindings.inputs.erase(rebound.bindings.inputs.begin());
        auto compiled = NativeBackend::CompilePixelAssembly(assembly,rebound.bindings);
        out.push_back({&p,std::move(compiled),rebound.bindings});
    }
    return out;
}
} } }

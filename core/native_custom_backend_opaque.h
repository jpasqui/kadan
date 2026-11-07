#pragma once
// Creator-owned opaque programs use the selected original forward policy.
// Scene resource roles and directional variants come from actual source metadata.
#include "mod_folders.h"
#include "native_custom_backend_translucent.h"
#include "material_engine_hashes.h"
#include "native_custom_backend_policy.h"
#include "custom_material_opaque_policy.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
inline Lighting::OriginalDecalResponse CreatorOpaqueDecalResponse(const Json& graph) {
    const auto decal=Text(Get(graph,"render_settings"),"MaterialDecalResponse");
    if (decal=="MDR_Color") return Lighting::OriginalDecalResponse::Color;
    if (decal=="MDR_ColorRoughness") return Lighting::OriginalDecalResponse::ColorRoughness;
    throw std::runtime_error("For this opaque material, set Decal Response to Color or Color Roughness.");
}
inline std::string OpaqueAuthoringIssue(const Json& graph,
    const std::map<std::string,std::string>& aliases = {},
    const std::vector<TextureParameter>& textures = {}, bool allowMask = false) {
    // allowMask: a masked material (Kadan's own programs, OwnOpaque, with its own depth and shadow passes)
    try {
        const auto blend = Text(graph,"blend_mode");
        if (Text(graph,"domain") != "MD_Surface" || (blend != "BLEND_Opaque" && !(allowMask && blend == "BLEND_Masked")) ||
            (Text(graph,"shading_model") != "MSM_Unlit" && Text(graph,"shading_model") != "MSM_DefaultLit"))
            throw std::runtime_error("Choose an opaque Surface material with Unlit or Default Lit shading.");
        const bool lit=Text(graph,"shading_model")=="MSM_DefaultLit";
        if (lit) (void)CreatorOpaqueDecalResponse(graph);
        const std::set<std::string> supported={"BaseColor","Metallic","Specular","Roughness","AmbientOcclusion","EmissiveColor","Normal"};
        for (const auto& pin : Get(graph,"outputs").object)
            if (pin.second.kind!=Json::Null && (!(supported.count(pin.first) || (allowMask && pin.first=="OpacityMask")) ||
                (!lit && pin.first!="EmissiveColor")))
                throw std::runtime_error("The opaque material compiler cannot export the connected " + pin.first + " input yet.");
        if (!allowMask && Get(graph,"clip").kind != Json::Null) throw std::runtime_error("An opaque material cannot have a mask.");
        const auto uniforms = GraphUniforms(InlineFunctions(graph,Json()),textures);
        Emitter emitter(false,Flag(graph,"two_sided")); emitter.owned_uniforms=true;
        emitter.material_parameters=uniforms.bindings; emitter.material_buffer=5;
        if (lit) (void)CompileMaterialInputsWithEmitter(graph,std::move(emitter),aliases);
        else (void)CompileTranslucentInputs(graph,std::move(emitter),aliases);
        return {};
    } catch (const std::exception& error) { return error.what(); }
}
inline Lighting::OriginalDecalResponse ReadOpaqueDecalResponse(const std::vector<FMaterialCookedRecord>& chain) {
    if (chain.empty() || chain.front().Class!="Material") throw std::runtime_error("The original opaque master is missing.");
    std::string decal; const auto field=chain.front().Properties.Field("MaterialDecalResponse");
    if (!field || !MaterialEnum(*field,decal) || (decal!="MDR_Color" && decal!="MDR_ColorRoughness"))
        throw std::runtime_error("The original opaque master must serialize a supported decal response.");
    return decal=="MDR_Color" ? Lighting::OriginalDecalResponse::Color : Lighting::OriginalDecalResponse::ColorRoughness;
}
inline bool IsReplacedOpaquePixel(const FMaterialShaderReference& ref) {
    return ref.Frequency==3 && KnownBasePass(ref.TypeHash) &&
        (ref.VertexFactoryHash==EngineHash::LocalVertexFactory || ref.VertexFactoryHash==EngineHash::InstancedStaticMeshVertexFactory);
}
inline void RequireOpaqueRetainedUniforms(const FMaterialShaderReference& ref) {
    if (IsReplacedOpaquePixel(ref)) return;
    for (const auto& role:ref.NamedBuffers) if (role.second=="Material")
        throw std::runtime_error("An original retained shader consumes the replaced Material uniform buffer.");
}
inline void RequireOpaqueRetainedVertex(const FMaterialShaderReference& ref,Bytes vertex) {
    if (ref.Frequency!=0 || vertex.size()<6 || vertex[4]!=0x53 || vertex[5]!=0x56)
        throw std::runtime_error("The original retained vertex payload has another shader stage.");
    vertex[5]=0x50; (void)ReadPixelBindings(vertex); // Actual common legacy schema after checking VS marker.
}
inline TranslucentCompilation CompileCreatorOpaque(const Json& graph,const std::string& material,
    const std::string& gameMaterial,const TranslucentSource& selected,
    const TranslucentSourceResolver& resolver,const ShaderArchive& archive,
    const std::map<std::string,std::string>& aliases = {},const std::vector<TextureParameter>& textures = {}) {
    const auto issue=OpaqueAuthoringIssue(graph,aliases,textures);
    if (!issue.empty()) throw std::runtime_error(issue);
    if (!AssetPath(material,true) || !P4MapKit::Export::ModFolderPackage(material))
        throw std::runtime_error("The generated material must belong to this mod.");
    const auto chain=TranslucentParentChain(gameMaterial,selected,resolver);
    FMaterialCookedSettings settings; FReadError error;
    if (!ReadMaterialCookedSettings(chain,settings,error)) throw std::runtime_error(error.Code);
    if (settings.Domain!="MD_Surface" || settings.BlendMode!="BLEND_Opaque" || settings.TwoSided || settings.ShadingModel!="MSM_DefaultLit")
        throw std::runtime_error("The opaque forward source has other render settings.");
    const bool lit=Text(graph,"shading_model")=="MSM_DefaultLit";
    if (lit) (void)ReadOpaqueDecalResponse(chain);
    const auto decalResponse=lit ? CreatorOpaqueDecalResponse(graph) : Lighting::OriginalDecalResponse::Color;
    const auto package=ReadPackageHeader(selected.header,selected.body.size());
    const auto leaf=gameMaterial.substr(gameMaterial.find_last_of('/')+1); std::size_t root=~std::size_t(0);
    for (std::size_t i=0;i<package.Exports.size();++i) if (package.Exports[i].Name==leaf) {
        if (root!=~std::size_t(0)) throw std::runtime_error("The material source has several root exports."); root=i;
    }
    FMaterialResourceBoundary boundary;
    if (!DiscoverMaterialResourceBoundary(package,selected.body,root,boundary,error)) throw std::runtime_error(error.Code);
    FMaterialShaderMapEnvelope map;
    if (!ReadMaterialShaderMapEnvelope(selected.body,boundary.ResourceStart,boundary.ExportEnd,map,error)) throw std::runtime_error(error.Code);
    std::vector<FMaterialShaderReference> refs;
    if (!ReadMaterialShaderReferences(map,refs,error)) throw std::runtime_error(error.Code);
    const auto indices=archive.Map(Bytes(map.ResourceHash.begin(),map.ResourceHash.end()));
    TranslucentCompilation result; result.compiled.source_references=refs; result.uniforms=GraphUniforms(InlineFunctions(graph,Json()),textures);
    const auto frozen=ReplaceOwnedUniforms(map,result.uniforms,true);
    auto& output=result.compiled.material; output.material=material; output.game_material=gameMaterial;
    output.map_id_hash=Bytes(map.CookedIdHash.begin(),map.CookedIdHash.end());
    output.map_shaders=unsigned(indices.size()); output.edits=frozen.edits;
    std::map<unsigned,FMaterialShaderReference> done;
    for (const auto& ref:refs) {
        if (ref.ResourceIndex<0 || std::size_t(ref.ResourceIndex)>=indices.size() || archive.Frequency(indices[ref.ResourceIndex])!=ref.Frequency)
            throw std::runtime_error("The material shader map and archive disagree.");
        RequireOpaqueRetainedUniforms(ref);
        if (ref.Frequency==0) RequireOpaqueRetainedVertex(ref,archive.Payload(indices[ref.ResourceIndex]));
        if (!IsReplacedOpaquePixel(ref)) continue;
        const auto prior=done.find(unsigned(ref.ResourceIndex));
        if (prior!=done.end()) {
            if (prior->second.TypeHash!=ref.TypeHash || prior->second.UniformBufferHashes!=ref.UniformBufferHashes ||
                prior->second.NamedBuffers!=ref.NamedBuffers || prior->second.LooseTextures!=ref.LooseTextures || prior->second.LooseSamplers!=ref.LooseSamplers)
                throw std::runtime_error("The original opaque pixel policy is ambiguous.");
            continue;
        }
        done.emplace(unsigned(ref.ResourceIndex),ref);
        unsigned materialBuffer=32;
        for (const auto& binding:ref.NamedBuffers) if (binding.second=="Material") {
            if (materialBuffer!=32) throw std::runtime_error("The Material uniform buffer is ambiguous."); materialBuffer=binding.first;
        }
        if (materialBuffer>=32) throw std::runtime_error("The Material uniform buffer is missing.");
        const auto payload=archive.Payload(indices[ref.ResourceIndex]);
        const auto original=ReadPixelBindings(payload);
        if (materialBuffer>=original.layout_hashes.size() || original.layout_hashes[materialBuffer]!=Read32(map.Frozen,472))
            throw std::runtime_error("The original Material frozen/header layout disagrees.");
        auto rebound=RebindMaterialTextures(original,materialBuffer,unsigned(textures.size()),frozen.layout_hash,false,ref.LooseTextures,ref.LooseSamplers);
        rebound.bindings.layout_hashes.at(materialBuffer)=frozen.layout_hash;
        auto bindings=ResolveMaterialBindings(rebound.bindings,materialBuffer,{});
        for (std::size_t i=0;i<textures.size();++i) bindings.textures[textures[i].name]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
        Emitter emitter(false,Flag(graph,"two_sided")); emitter.owned_uniforms=true;
        emitter.material_buffer=materialBuffer; emitter.material_parameters=result.uniforms.bindings;
        PixelProgram program;
        if (lit) {
            const auto proof=ReadOpaqueDirectionalSourceProof(payload,ref);
            const auto directional=proof.Response()==OriginalOpaqueDirectionalResponse::SeparateSpecularShadow
                ? Lighting::OpaqueDirectionalResponse::SeparateSpecularShadow : Lighting::OpaqueDirectionalResponse::SurfaceShadow;
            const auto assembly=Lighting::OpaqueMaterialProgram(graph,rebound.bindings,ref.UniformBufferHashes,ref.TypeHash,decalResponse,
                aliases,bindings.textures,Json(),true,directional,std::move(emitter));
            program=Lighting::OriginalIndirectPolicy(ref.TypeHash).sky
                ? CompilePixelAssembly(assembly,rebound.bindings,ReadOpaqueSkyIrradianceStorageProof(payload,ref.UniformBufferHashes,materialBuffer))
                : CompilePixelAssembly(assembly,rebound.bindings);
        } else {
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
            program=CompilePixelAssembly(Lighting::ForwardReuseTemporaries(Resolve(policy.Program(),bindings.textures,Lighting::OpaquePolicyVaryings(rebound.bindings))),rebound.bindings);
        }
        output.shaders.push_back({unsigned(ref.ResourceIndex),program.payload}); result.compiled.programs.push_back(program);
    }
    if (output.shaders.empty()) throw std::runtime_error("The opaque template has no static-mesh base pass.");
    (void)WriteShaderSet3({output}); return result;
}
} } }

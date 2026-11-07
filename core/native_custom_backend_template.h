#pragma once
#include "native_custom_backend.h"
#include "material_engine_hashes.h"
#include "native_custom_backend_archive.h"
#include "custom_material_bindings.h"
#include "native_custom_backend_depth.h"
#include "custom_material_texture_list.h"
#include "material_shader_map.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
// Exact FHashedName engine type metadata for diagnostic templates. Unknown
// policy types require a schema update and render evidence, never a name guess.
inline bool KnownBasePass(std::uint64_t hash){
    static const std::set<std::uint64_t> known={
        0x4507f03a199c6526ull,0xa8d53bcb8fca168dull, // FNoLightMapPolicy
        0x666403914542b619ull,0x18a82ebde5d3d122ull, // VolumetricLightmap
        0x6d90922c199fe04full,0xdbbfd1d594df3611ull, // LightMapHQ
        0x28b73d7ae30b556cull,0x661e2d85a2c9dbe6ull // DistanceFieldShadowsHQ
    };return known.count(hash)!=0;
}
struct TemplateCompilation {SetMaterial material;std::vector<PixelProgram> programs;std::vector<FMaterialShaderReference> source_references;};
struct TemplateOptions {std::vector<TextureParameter> textures;bool own_texture_list=false,share_sampler=false;};
// ResourceStart/ExportEnd are the validated package reader's exact boundaries;
// no magic/hash search is performed. Archive ResourceHash selects bytes while
// CookedIdHash is the Tsuchi runtime set identity.
inline TemplateCompilation CompileDiagnosticTemplate(const Json& graph,const RenderRequest& request,
    const std::string& material,const std::string& gameMaterial,
    const Bytes& originalMaterial,std::size_t resourceStart,std::size_t exportEnd,
    const ShaderArchive& archive,const std::map<std::string,std::string>& textureParams={},const TemplateOptions& options={},const Json& functions=Json()){
    auto admission=GraphAvailability(graph,request,textureParams,functions);if(!admission.available)throw std::runtime_error(admission.code+": "+admission.reason);
    const bool masked=request.blend=="Masked";
    if(masked&&request.mesh=="instanced-static")throw std::runtime_error("masked-instanced-dither-unqualified: the instanced LOD dither program is not ported");
    if(request.custom_texture_list!=options.own_texture_list||(options.own_texture_list&&request.texture_count!=options.textures.size()))throw std::runtime_error("custom texture render settings mismatch");
    FMaterialShaderMapEnvelope envelope;FReadError error;
    if(!ReadMaterialShaderMapEnvelope(originalMaterial,resourceStart,exportEnd,envelope,error))throw std::runtime_error("material-template: "+error.Code);
    std::vector<FMaterialShaderReference> refs;if(!ReadMaterialShaderReferences(envelope,refs,error))throw std::runtime_error("material-template: "+error.Code);
    auto indices=archive.Map(Bytes(envelope.ResourceHash.begin(),envelope.ResourceHash.end()));
    std::map<unsigned,FMaterialShaderReference> selected;bool hasBase=false;
    for(const auto& ref:refs){
        if(ref.ResourceIndex<0||std::size_t(ref.ResourceIndex)>=indices.size()||archive.Frequency(indices[ref.ResourceIndex])!=ref.Frequency)throw std::runtime_error("material-reference/archive association mismatch");
        if(ref.Frequency!=3)continue;
        const std::uint64_t factory=request.mesh=="instanced-static"?EngineHash::InstancedStaticMeshVertexFactory:EngineHash::LocalVertexFactory;
        if(ref.VertexFactoryHash!=factory)continue;
        if(!KnownBasePass(ref.TypeHash)&&!masked)continue;
        if(!KnownBasePass(ref.TypeHash)&&!KnownMaskPass(ref.TypeHash))throw std::runtime_error("pixel-pass-schema-unqualified: exact pass type metadata is required");
        hasBase=hasBase||KnownBasePass(ref.TypeHash);
        auto old=selected.find(unsigned(ref.ResourceIndex));
        if(old!=selected.end()&&(KnownBasePass(old->second.TypeHash)!=KnownBasePass(ref.TypeHash)||old->second.NamedBuffers!=ref.NamedBuffers||old->second.LooseTextures!=ref.LooseTextures||old->second.LooseSamplers!=ref.LooseSamplers))throw std::runtime_error("ambiguous pixel-pass binding association");
        selected.emplace(unsigned(ref.ResourceIndex),ref);
    }
    if(selected.empty()||!hasBase)throw std::runtime_error("base-pass-schema-unqualified: template has no admitted pixel shader type");
    std::vector<std::string> names;TextureListResult newTextures;
    if(options.own_texture_list){
        std::map<std::size_t,std::pair<std::string,std::uint32_t>> identities;
        for(const auto& name:envelope.ScriptNameIdentities){if(name.second.second<0)throw std::runtime_error("negative frozen FName number");identities.emplace(name.first,std::make_pair(name.second.first,unsigned(name.second.second)));}
        newTextures=ReplaceTextureList(envelope,options.textures,identities);
        for(const auto& texture:options.textures)names.push_back(texture.name);
    }else for(const auto& texture:envelope.Uniforms.Textures){
        if(texture.Parameter.NameIsNone||texture.Parameter.Association!=2||texture.Parameter.LayerIndex!=-1||texture.TextureType!=0||texture.SamplerSource!=0)throw std::runtime_error("material texture parameter schema unsupported");
        names.push_back(texture.Parameter.Name);
    }
    auto compiledGraph=CompileGraph(graph,textureParams,false,functions);
    auto depthGraph=masked?CompileGraph(graph,textureParams,true,functions):CompileResult{};
    TemplateCompilation out;out.material.material=material;out.material.game_material=gameMaterial;
    out.material.map_id_hash.assign(envelope.CookedIdHash.begin(),envelope.CookedIdHash.end());out.material.map_shaders=unsigned(indices.size());
    if(options.own_texture_list)out.material.edits=newTextures.edits;
    for(const auto& item:selected){
        int materialBuffer=-1;for(const auto& binding:item.second.NamedBuffers)if(binding.second=="Material"){if(materialBuffer!=-1)throw std::runtime_error("ambiguous Material uniform buffer");materialBuffer=binding.first;}
        if(materialBuffer<0)throw std::runtime_error("Material uniform buffer binding absent");
        auto originalPayload=archive.Payload(indices[item.first]);auto bindings=ReadPixelBindings(originalPayload);
        std::string assembly=KnownBasePass(item.second.TypeHash)?compiledGraph.program:KeepMaskDepthMetadata(depthGraph.program,OriginalAssemblyMetadata(originalPayload,bindings));
        ResolvedBindings resolved;
        if(options.own_texture_list){
            auto rebound=RebindMaterialTextures(bindings,unsigned(materialBuffer),unsigned(options.textures.size()),newTextures.layout_hash,options.share_sampler,item.second.LooseTextures,item.second.LooseSamplers);
            bindings=std::move(rebound.bindings);
            resolved=ResolveMaterialBindings(bindings,unsigned(materialBuffer),{});
            for(std::size_t i=0;i<names.size();++i)resolved.textures[names[i]]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
        }else resolved=ResolveMaterialBindings(bindings,unsigned(materialBuffer),names);
        auto program=ResolveAndCompilePixel(assembly,resolved.textures,resolved.varyings,bindings);
        out.material.shaders.push_back({item.first,program.payload});out.programs.push_back(std::move(program));
    }
    (void)WriteShaderSet3({out.material});return out;
}
} } }

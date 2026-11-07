#pragma once
// Complete own-program research candidate. Publication stays independently held.
#include "mod_folders.h"
#include "native_custom_backend_policy.h"
#include "material_engine_hashes.h"
#include "native_custom_backend_template.h"
#include "custom_material_opaque_policy.h"
#include "material_source_data.h"
#include "material_resource_boundary.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
struct OpaqueResearchSource { Bytes header,body; };
using OpaqueResearchSourceResolver=std::function<bool(const std::string&,OpaqueResearchSource&)>;
struct OpaqueResearchCompilation {
    TemplateCompilation compiled;
    Json recipe=Json::MakeObject();
    std::string recipe_digest;
    float inherited_clip=0.3333f;
    std::string source_class;
    // Compile success is never a publication, renderer or variant qualification.
    const bool production_admitted=false;
};
inline OpaqueResearchCompilation CompileOpaqueResearchTemplate(const Json& creatorGraph,
    const std::string& creatorInstance,const std::string& gameMaterial,
    const OpaqueResearchSource& selected,const OpaqueResearchSourceResolver& resolve,
    const ShaderArchive& archive,const std::string& mesh="static")
{
    if(!AssetPath(creatorInstance,true)||!P4MapKit::Export::ModFolderPackage(creatorInstance)||
       !AssetPath(gameMaterial,false)||gameMaterial.compare(0,14,"/Game/Carrot4/")!=0)
        throw std::runtime_error("opaque-research-material-identity");
    if(mesh!="static")throw std::runtime_error("opaque-research-factory-unqualified");
    const auto archiveSha256=archive.SourceSha256();
    if(Flag(creatorGraph,"two_sided")||Get(creatorGraph,"clip").kind!=Json::Null)
        throw std::runtime_error("opaque-research-requires-single-sided-opaque");
    // First rendered recipe deliberately excludes runtime parameters, textures,
    // collections and functions. A sealed scene/Sky SRT is never rebound here.
    static const std::set<std::string> Constants={"Constant","Constant2Vector","Constant3Vector","Constant4Vector"};
    for(const auto& node:Get(creatorGraph,"nodes").object)
        if(!Constants.count(Text(node.second,"type")))throw std::runtime_error("opaque-research-constant-graph-required");
    std::map<std::string,std::string> sourcePins;
    const auto parse=[&](const OpaqueResearchSource& source,const std::string& expected){
        if(source.header.empty()||source.header.size()>(16u<<20)||source.body.empty()||source.body.size()>(64u<<20))
            throw std::runtime_error("opaque-research-source-bound");
        Material::MaterialSourceAsset asset;asset.Package=ReadPackageHeader(source.header,source.body.size());
        if(!asset.Package.Error.Code.empty()||PropertyIdentity(asset.Package.Package)!=PropertyIdentity(expected))throw std::runtime_error("opaque-research-source-package-mismatch");
        Bytes identity=source.header;Append(identity,source.body);const auto hash=Hex(Sha256(identity));
        const auto canonical=asset.Package.Package;auto prior=sourcePins.find(canonical);if(prior!=sourcePins.end()&&prior->second!=hash)throw std::runtime_error("opaque-research-source-changed");
        sourcePins[canonical]=hash;asset.Body=std::make_shared<const Bytes>(source.body);return asset;
    };
    const auto source=parse(selected,gameMaterial);
    const Material::MaterialSourceAssetResolver parentResolver=[&](const std::string& package,Material::MaterialSourceAsset& asset){
        OpaqueResearchSource raw;if(!resolve||!resolve(package,raw))return false;asset=parse(raw,package);return true;
    };
    const auto chain=Material::ReadOriginalMaterialParentChain(source,parentResolver);
    FMaterialCookedSettings settings;FReadError error;
    if(!ReadMaterialCookedSettings(chain,settings,error))throw std::runtime_error(error.Code);
    if(settings.Domain!="MD_Surface"||settings.BlendMode!="BLEND_Opaque"||settings.ShadingModel!="MSM_DefaultLit"||settings.TwoSided)
        throw std::runtime_error("opaque-research-original-settings-incompatible");
    std::string decalName;const auto decal=chain.front().Properties.Field("MaterialDecalResponse");
    if(!decal||!MaterialEnum(*decal,decalName)||(decalName!="MDR_Color"&&decalName!="MDR_ColorRoughness"))
        throw std::runtime_error("opaque-research-serialized-decal-response-required");
    const auto decalResponse=decalName=="MDR_Color"?Lighting::OriginalDecalResponse::Color:Lighting::OriginalDecalResponse::ColorRoughness;
    // An omitted root field uses the stock cooked reader's constructor default.
    // That compatibility fact is recorded; it does not prove an ARM64 constructor.
    Json compatibility=Json::MakeObject();
    for(const auto* field:{"MaterialDomain","BlendMode","ShadingModel","TwoSided"})
        compatibility[field]=Json(chain.front().Properties.Field(field)?"serialized":"stock-constructor-compatibility");
    std::size_t exportIndex=source.Package.Exports.size();std::string selectedClass;
    for(std::size_t i=0;i<source.Package.Exports.size();++i){std::string type;if(!source.Package.Reference(source.Package.Exports[i].Class,type))throw std::runtime_error("opaque-research-source-class");
        const auto fullType=type;
        type=type.substr(type.find_last_of('.')==std::string::npos?0:type.find_last_of('.')+1);
        if(type=="Material"||type=="MaterialInstanceConstant"){if(exportIndex!=source.Package.Exports.size())throw std::runtime_error("opaque-research-ambiguous-source-owner");exportIndex=i;selectedClass=fullType;}}
    if(exportIndex==source.Package.Exports.size())throw std::runtime_error("opaque-research-source-owner-absent");
    FMaterialResourceBoundary boundary;
    if(!DiscoverMaterialResourceBoundary(source.Package,selected.body,exportIndex,boundary,error))throw std::runtime_error(error.Code);
    FMaterialShaderMapEnvelope map;std::vector<FMaterialShaderReference> refs;
    if(!ReadMaterialShaderMapEnvelope(selected.body,boundary.ResourceStart,boundary.ExportEnd,map,error)||!ReadMaterialShaderReferences(map,refs,error))throw std::runtime_error(error.Code);
    const auto indices=archive.Map(Bytes(map.ResourceHash.begin(),map.ResourceHash.end()));
    std::map<unsigned,FMaterialShaderReference> picked;
    for(const auto& ref:refs){
        if(ref.ResourceIndex<0||std::size_t(ref.ResourceIndex)>=indices.size()||archive.Frequency(indices[ref.ResourceIndex])!=ref.Frequency)
            throw std::runtime_error("opaque-research-archive-association");
        if(ref.Frequency!=3||ref.VertexFactoryHash!=EngineHash::LocalVertexFactory||!KnownBasePass(ref.TypeHash))continue;
        auto prior=picked.find(unsigned(ref.ResourceIndex));
        if(prior!=picked.end()&&(prior->second.TypeHash!=ref.TypeHash||prior->second.UniformBufferHashes!=ref.UniformBufferHashes))
            throw std::runtime_error("opaque-research-ambiguous-pass");
        picked.emplace(unsigned(ref.ResourceIndex),ref);
    }
    if(picked.empty())throw std::runtime_error("opaque-research-no-local-base-pass");
    OpaqueResearchCompilation result;result.compiled.material.material=creatorInstance;result.compiled.material.game_material=source.Package.Package;
    result.compiled.material.map_id_hash.assign(map.CookedIdHash.begin(),map.CookedIdHash.end());result.compiled.material.map_shaders=unsigned(indices.size());
    Json passes=Json::MakeArray();
    for(const auto& item:picked){
        const auto payload=archive.Payload(indices[item.first]);const auto binding=ReadPixelBindings(payload);
        const auto proof=ReadOpaqueDirectionalSourceProof(payload,item.second);
        const auto response=proof.Response()==OriginalOpaqueDirectionalResponse::SeparateSpecularShadow?
            Lighting::OpaqueDirectionalResponse::SeparateSpecularShadow:Lighting::OpaqueDirectionalResponse::SurfaceShadow;
        const auto assembly=Lighting::OpaqueMaterialProgram(creatorGraph,binding,item.second.UniformBufferHashes,item.second.TypeHash,decalResponse,{}, {},Json(),true,response);
        auto program=Lighting::OriginalIndirectPolicy(item.second.TypeHash).sky?
            CompilePixelAssembly(assembly,binding,ReadSkyIrradianceStorageProof(payload,item.second.UniformBufferHashes)):
            CompilePixelAssembly(assembly,binding);
        result.compiled.material.shaders.push_back({item.first,program.payload});
        Json pass=Json::MakeObject();pass["place"]=Json(int(item.first));
        // Full numeric type identity uses a fixed-width byte hash, not a double.
        Bytes typeBytes;for(unsigned i=0;i<8;++i)typeBytes.push_back(std::uint8_t(item.second.TypeHash>>(8*(7-i))));pass["type_hash"]=Json(Hex(typeBytes));
        pass["source_program_sha256"]=Json(proof.AssemblySha256());pass["source_payload_sha256"]=Json(Hex(Sha256(payload)));
        pass["directional_response"]=Json(proof.Response()==OriginalOpaqueDirectionalResponse::SeparateSpecularShadow?"SeparateSpecularShadow":"SurfaceShadow");
        pass["own_payload_sha256"]=Json(Hex(Sha256(program.payload)));passes.array.push_back(std::move(pass));result.compiled.programs.push_back(std::move(program));
    }
    const auto set=WriteShaderSet3({result.compiled.material});
    auto& recipe=result.recipe;recipe["kind"]=Json("p4-opaque-research-candidate");recipe["version"]=Json(1);
    recipe["creator_graph"]=creatorGraph;recipe["material"]=Json(creatorInstance);recipe["game_material"]=Json(source.Package.Package);
    recipe["map_id_hash"]=Json(Hex(result.compiled.material.map_id_hash));recipe["archive_sha256"]=Json(archiveSha256);
    recipe["source_settings_compatibility"]=compatibility;recipe["serialized_decal_response"]=Json(decalName);
    recipe["source_packages_sha256"]=Json::MakeObject();for(const auto& pin:sourcePins)recipe["source_packages_sha256"][pin.first]=Json(pin.second);
    recipe["passes"]=std::move(passes);recipe["set_sha256"]=Json(Hex(Sha256(set)));recipe["mesh"]=Json(mesh);
    recipe["production_admitted"]=Json(false);recipe["held_dependencies"]=Json::MakeArray();
    for(const auto* reason:{"creator-instance-fresh-cook-readback","actual-selected-mesh-permutation","game-rendered-lighting-acceptance"})recipe["held_dependencies"].array.push_back(Json(reason));
    result.inherited_clip=settings.Clip;recipe["source_inherited_clip"]=Json(double(settings.Clip));
    result.source_class=selectedClass;recipe["source_class"]=Json(selectedClass);
    result.recipe_digest=RecipeDigest(recipe);return result;
}
} } }

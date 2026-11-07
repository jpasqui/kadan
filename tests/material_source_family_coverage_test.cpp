// Actual source regression: a scoped master-only universe cannot stand in for
// the original family's shader-owner coverage. No UE, asset writes or copies.
#include "../core/material_masters.h"
#include "../core/native_custom_backend_archive.h"
#include <fstream>
#include <iostream>
namespace M=P4MapKit::Material;
namespace C=P4MapKit::CustomMaterial;
static int checks=0;
static void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
static C::Bytes File(const std::string& path,std::uint64_t bound){
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("missing source: "+path);
    stream.seekg(0,std::ios::end);const auto size=stream.tellg();
    if(size<=0||std::uint64_t(size)>bound)throw std::runtime_error("read bound: "+path);
    stream.seekg(0);C::Bytes bytes(std::size_t(size),0);
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("read failed: "+path);
    return bytes;
}
static M::Json JsonFile(const std::string& path){const auto bytes=File(path,2u*1024u*1024u);return M::Json::Parse(std::string(bytes.begin(),bytes.end()));}
static bool PinsAndSettings(const M::Json& a,const M::Json& b){
    M::MaterialMasterFingerprint x{a,nullptr,{}},y{b,nullptr,{}};
    if(a.At("outputs").object.size()!=b.At("outputs").object.size())return false;
    for(const auto& pin:a.At("outputs").object)
        if(!b.At("outputs").Has(pin.first)||x.Of(pin.second)!=y.Of(b.At("outputs").At(pin.first)))return false;
    for(const auto* key:{"domain","blend_mode","two_sided","shading_model","clip","tangent_space_normal"})
        if(a.At(key).Dump()!=b.At(key).Dump())return false;
    return true;
}
static bool References(const M::Json& value,const std::string& package){
    if(value.kind==M::Json::String)return value.Text()==package||value.Text().compare(0,package.size()+1,package+".")==0;
    if(value.kind==M::Json::Object)for(const auto& field:value.object)if(References(field.second,package))return true;
    if(value.kind==M::Json::Array)for(const auto& field:value.array)if(References(field,package))return true;
    return false;
}
int main(int argc,char** argv){try{
    if(argc!=8)throw std::runtime_error("archive, source root, canonical shadow, canonical silhouette, native114, native116, output required");
    const auto archiveBytes=File(argv[1],1ull<<31);C::NativeBackend::ShaderArchive archive(archiveBytes);const std::string root=argv[2];
    std::set<std::string> allowed;
    const auto readRaw=[&](const std::string& package,M::MaterialOriginalImportRawAsset& raw){
        if(package.compare(0,6,"/Game/")!=0)return false;
        const std::string stem=root+"/Carrot4/Content/"+package.substr(6);
        std::ifstream exists(stem+".uasset",std::ios::binary);if(!exists)return false;
        raw={std::make_shared<const C::Bytes>(File(stem+".uasset",64u*1024u*1024u)),std::make_shared<const C::Bytes>(File(stem+".uexp",64u*1024u*1024u))};return true;
    };
    M::MaterialMasterSources sources;
    sources.Raw=[&](const std::string& package,M::MaterialOriginalImportRawAsset& raw){return allowed.count(package)&&readRaw(package,raw);};
    M::MaterialOriginalArchiveAccess access;
    access.MapIndices=[&](const std::array<std::uint8_t,20>& hash){return archive.Map(C::Bytes(hash.begin(),hash.end()));};
    access.Payload=[&](std::uint32_t index){return archive.Payload(index);};
    access.Frequency=[&](std::uint32_t index){return std::uint8_t(archive.Frequency(index));};
    const std::vector<std::vector<std::string>> members={{
        "/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master",
        "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow",
        "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve",
        "/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow",
        "/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow"},{
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master",
        "/Game/Carrot4/Characters/Teki/Dodoro/MI_chr_Dodoro_00_Silhouette",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Object_00",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Player_00",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Teki_00"}};
    for(const auto& family:members)for(const auto& member:family)allowed.insert(member);
    allowed.insert("/Game/Carrot4/MasterMaterial/Texture/T_Base_M");allowed.insert("/Game/Carrot4/MasterMaterial/Texture/T_Base_D");
    Check(allowed.size()==12,"positive source resolver admits exactly twelve declared packages");
    const auto native=JsonFile(argv[5]),nativeWithoutSamplers=JsonFile(argv[6]);
    auto report=M::Json::MakeObject();report["families"]=M::Json::MakeArray();
    for(std::size_t i=0;i<members.size();++i){
        const auto& familyMembers=members[i];const auto& master=familyMembers.front();
        const auto sparse=M::RecoverMaterialMaster(master,{},sources,access);
        const auto full=M::RecoverMaterialMaster(master,familyMembers,sources,access);
        M::MaterialMasterSources fullCorpus;fullCorpus.Raw=readRaw;
        const auto broad=M::RecoverMaterialMaster(master,familyMembers,fullCorpus,access);
        Check(broad.HasGraph&&broad.Refused.empty()&&broad.RefusedMerge.empty(),"independent full historical corpus family recovers without refusal");
        Check(full.Merge.Graph.Dump()==broad.Merge.Graph.Dump(),"twelve-source merged graph ALL fields equal unrestricted original corpus recovery");
        Check(sparse.HasGraph&&full.HasGraph,"both original source universes recover");
        Check(sparse.Refused.empty()&&full.Refused.empty()&&full.RefusedMerge.empty(),"full coverage has no owner or merge refusal");
        Check(sparse.Users.size()==1&&full.Users.size()==5,"missing four member rows reproduces scoped coverage loss");
        Check(sparse.Merge.Graph.At("nodes").object.size()==(i?36u:5u),"sparse source universe explains native 114 counts");
        Check(full.Merge.Graph.At("nodes").object.size()==(i?52u:11u),"all actual family owners restore historical 11/52 graph");
        Check(native.At("materials").array.at(i).At("package").Text()==master,"native receipt selected same master");
        Check(native.At("materials").array.at(i).At("source_recipe").Dump()==sparse.Merge.Graph.Dump(),"sparse producer reproduces every native114 recipe field/edge/code/pin/setting");
        const auto canonicalRecord=JsonFile(argv[3+i]);const auto canonical=canonicalRecord.Has("graph")?canonicalRecord.At("graph"):canonicalRecord;
        Check(!PinsAndSettings(sparse.Merge.Graph,canonical),"RED: sparse graph is refused as complete historical family");
        Check(PinsAndSettings(full.Merge.Graph,canonical),"complete actual family gives historical pins/settings");
        M::MaterialSourceAsset actualMaster;Check(sources.Resolver()(master,actualMaster),"actual master source resolves");
        const auto actualOwner=M::ReadOriginalMaterialOwnerSource(actualMaster,sources.Resolver(),sources.Collections(actualMaster));
        auto historicalWithSourceUsage=canonical;
        for(const auto& flag:actualOwner.User.Cooked.Usage){
            if(!historicalWithSourceUsage.Has("render_settings"))historicalWithSourceUsage["render_settings"]=M::Json::MakeObject();
            historicalWithSourceUsage["render_settings"][flag]=true;
        }
        Check(historicalWithSourceUsage.Dump()==full.Merge.Graph.Dump(),
            "ALL historical node properties/edges/Custom code/pins/settings equal full recipe, plus only actual original true usage flags");
        M::MaterialMasterSources noSamplers;
        noSamplers.Raw=[&](const std::string& package,M::MaterialOriginalImportRawAsset& raw){
            if(package=="/Game/Carrot4/MasterMaterial/Texture/T_Base_M" || package=="/Game/Carrot4/MasterMaterial/Texture/T_Base_D")return false;
            return sources.Raw(package,raw);
        };
        const auto withoutSamplers=M::RecoverMaterialMaster(master,familyMembers,noSamplers,access);
        Check(withoutSamplers.HasGraph,"missing sampler source currently yields a partial source recipe");
        Check(withoutSamplers.Merge.Graph.Dump()==nativeWithoutSamplers.At("materials").array.at(i).At("source_recipe").Dump(),
            "absent original texture source reproduces every native116 recipe field exactly");
        Check(!PinsAndSettings(withoutSamplers.Merge.Graph,canonical),"RED: missing original sampler metadata cannot pass historical family contract");
        const std::string texturePackage=i?"/Game/Carrot4/MasterMaterial/Texture/T_Base_D":"/Game/Carrot4/MasterMaterial/Texture/T_Base_M";
        M::MaterialSourceAsset unavailable;
        Check(!noSamplers.Resolver()(texturePackage,unavailable),"the exact original default texture was excluded from the RED source universe");
        M::MaterialSourceAsset texture;Check(sources.Resolver()(texturePackage,texture),"actual original texture source resolves");
        std::string textureClass,sampler;P4MapKit::FDecodedValue properties;P4MapKit::FReadError error;
        Check(M::MaterialSourceReadObject(texture,{"Texture2D"},textureClass,properties)&&
            P4MapKit::ReadMaterialSamplerType(textureClass,properties,sampler,error),"sampler is decoded from real typed texture fields");
        Check(sampler==(i?"SAMPLERTYPE_Color":"SAMPLERTYPE_LinearColor"),"original source color-space/compression establishes the sampler");
        bool sampled=false;
        for(const auto& node:full.Merge.Graph.At("nodes").object)if(node.second.At("type").Text()=="TextureSampleParameter2D"){
            Check(node.second.At("props").At("SamplerType").Text()==sampler,"full recipe retains exact source-derived sampler");sampled=true;
        }
        Check(sampled,"the actual family samples its source-bound texture");
        auto row=M::Json::MakeObject();row["master"]=master;row["sparse_nodes"]=double(sparse.Merge.Graph.At("nodes").object.size());
        row["twelve_source_and_full_corpus_graphs_equal"]=true;
        row["full_nodes"]=double(full.Merge.Graph.At("nodes").object.size());row["users"]=M::Json::MakeArray();row["shader_owners"]=M::Json::MakeArray();
        for(const auto& member:familyMembers){
            M::MaterialSourceAsset asset;Check(sources.Resolver()(member,asset),"exact source package exists");
            const auto chain=M::ReadOriginalMaterialParentChain(asset,sources.Resolver());
            Check(!chain.empty()&&chain.front().Package==master&&chain.back().Package==member,"original parent chain binds member to master");
            Check(std::find(full.Users.begin(),full.Users.end(),member)!=full.Users.end(),"every named member proved by merged graph");
            auto item=M::Json::MakeObject();item["package"]=member;item["class"]=chain.back().Class;item["shader_owner"]=full.OwnerOf.at(member);
            M::MaterialOriginalImportRawAsset bytes;Check(sources.Raw(member,bytes),"raw source retained");
            item["header_sha256"]=C::Hex(C::Sha256(*bytes.Header));item["body_sha256"]=C::Hex(C::Sha256(*bytes.Body));row["users"].array.push_back(item);
        }
        for(const auto& owner:full.Recovered){auto item=M::Json::MakeObject();item["package"]=owner.Package;const auto graph=owner.Graph.Dump();
            const auto old=std::find_if(broad.Recovered.begin(),broad.Recovered.end(),[&](const M::MaterialMasterOwner& other){return other.Package==owner.Package;});
            Check(old!=broad.Recovered.end()&&old->Graph.Dump()==graph,"every twelve-source owner graph ALL properties/refs/edges/settings equal full corpus recovery");
            M::MaterialSourceAsset asset;Check(sources.Resolver()(owner.Package,asset),"proved shader owner source resolves");
            const auto original=M::ReadOriginalMaterialOwnerSource(asset,sources.Resolver(),sources.Collections(asset));
            item["missing_assets"]=M::Json::MakeArray();
            for(const auto& missing:original.User.MissingAssets){
                Check(!References(owner.Graph,missing)&&!References(full.Merge.Graph,missing),
                    "any missing optimized-out source default must be absent from ALL actual owner/merged graph properties");
                item["missing_assets"].array.push_back(missing);
            }
            item["nodes"]=double(owner.Graph.At("nodes").object.size());item["graph_sha256"]=C::Hex(C::Sha256(C::Bytes(graph.begin(),graph.end())));
            row["shader_owners"].array.push_back(item);}
        report["families"].array.push_back(row);
    }
    report["checks"]=double(checks);report["passed"]=true;report["native_repair_proven"]=false;
    std::ofstream output(argv[7],std::ios::binary|std::ios::trunc);output<<report.Dump()<<'\n';if(!output)throw std::runtime_error("report write failed");
    std::cout<<checks<<" actual-source family-coverage checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

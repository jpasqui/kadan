#include "../core/material_binding_metadata.h"
#include <fstream>
#include <iostream>
namespace M=P4MapKit::Material;namespace C=P4MapKit::CustomMaterial;
static unsigned Checks=0;
static void Check(bool value,const char* why){++Checks;if(!value)throw std::runtime_error(why);}
template<class F>static void Refuses(F action,const char* why){bool held=false;try{action();}catch(const M::Refused&){held=true;}Check(held,why);}
static C::Bytes File(const std::string& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("explicit source missing");in.seekg(0,std::ios::end);const auto size=in.tellg();if(size<=0||std::uint64_t(size)>64u*1024u*1024u)throw std::runtime_error("source bound");in.seekg(0);C::Bytes out(std::size_t(size),0);if(!in.read(reinterpret_cast<char*>(out.data()),size))throw std::runtime_error("source read");return out;}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("explicit original source root required");const std::string root=argv[1];
    const std::string owner="/Game/Carrot4/MasterMaterial/eft/M_eft_Water01_alcfr_Master";
    const std::string env="/Game/Carrot4/MasterMaterial/ParameterCollections/MPC_Env",camera="/Game/Carrot4/MasterMaterial/ParameterCollections/MPC_Camera";
    std::set<std::string> missing{env,camera};std::map<std::string,M::MaterialOriginalImportRawAsset> cache;
    std::string substitute,corrupt;unsigned reads=0;
    M::MaterialOriginalImportRawResolver raw=[&](const std::string& package,M::MaterialOriginalImportRawAsset& out){
        ++reads;if(missing.count(package))return false;
        const auto key=package==substitute?camera:package;auto found=cache.find(key);
        if(found==cache.end()){
            const auto stem=root+"/Carrot4/Content/"+key.substr(6);std::ifstream exists(stem+".uasset",std::ios::binary);if(!exists)return false;
            found=cache.emplace(key,M::MaterialOriginalImportRawAsset{std::make_shared<const C::Bytes>(File(stem+".uasset")),std::make_shared<const C::Bytes>(File(stem+".uexp"))}).first;
        }
        out=found->second;if(package==corrupt){auto header=std::make_shared<C::Bytes>(*out.Header);header->at(0)^=1;out.Header=header;}return true;
    };
    const auto requests=M::DiscoverOriginalMaterialBindingMetadata(owner,raw);std::set<std::string> requested;
    for(const auto& request:requests){
        Check(request.NativeClass=="/Script/Engine.MaterialParameterCollection","only the actual missing cached collection prerequisites are requested");
        Check(request.Owner==owner&&request.OwnerSourceSha256.size()==64&&request.Package==M::MaterialSourcePackage(request.Object),"metadata request has exact owner/source/object custody");
        Check(request.References.size()==1,"each actual cached entry has one typed hard import witness");
        const auto& reference=request.References.front();
        Check(reference.SourcePackage==owner&&reference.SourceSha256==request.OwnerSourceSha256&&reference.ImportIndex<0&&reference.Object==request.Object&&reference.NativeClass==request.NativeClass,"exact raw import identity retained");
        Check(reference.Property==std::string("CachedExpressionData.ParameterCollectionInfos[")+(request.Package==env?"0":"1")+"].ParameterCollection","cached field and index retained verbatim");
        requested.insert(request.Package);
    }
    Check(requested==std::set<std::string>{env,camera},"raw metadata includes the two actual cached collections, with no shader-use claim");
    missing.erase(camera);const auto partial=M::DiscoverOriginalMaterialBindingMetadata(owner,raw);
    Check(partial.size()==1&&partial.front().Package==env,"an already available Camera does not substitute for missing Env");
    missing.clear();Check(M::DiscoverOriginalMaterialBindingMetadata(owner,raw).empty(),"available current raw collection metadata is reused without requests");
    substitute=env;Refuses([&]{M::DiscoverOriginalMaterialBindingMetadata(owner,raw);},"same-class different raw collection cannot borrow canonical identity");substitute.clear();
    corrupt=env;Refuses([&]{M::DiscoverOriginalMaterialBindingMetadata(owner,raw);},"corrupt present metadata is refused rather than treated as absent");corrupt.clear();
    M::MaterialUsedResourceDetail::FReader reader(raw);const auto* source=reader.Read(owner);const auto* envSource=reader.Read(env);const auto* cameraSource=reader.Read(camera);
    Check(source&&envSource&&cameraSource,"actual owner and two collections are decoded");
    Check(envSource->SourceSha256=="a4bcc90d13740c8bc429ec521267d802b60d2173103a01f84bb4ebaf082b4626","exact pristine Env source pair");
    const auto bound=M::ReadOriginalMaterialOwnerSource(source->Asset,reader.Resolver(),{cameraSource->Asset,envSource->Asset});
    Check(bound.Map.Uniforms.Collections.size()==1&&bound.User.Collections.size()==1&&bound.User.Collections[0].Available&&bound.User.Collections[0].Package==env,"the real uniform GUID selects Env despite reversed collection order");
    const auto unbound=M::ReadOriginalMaterialOwnerSource(source->Asset,reader.Resolver(),{cameraSource->Asset});
    Check(unbound.User.Collections.size()==1&&!unbound.User.Collections[0].Available,"unused Camera raw metadata cannot supply the shader Env binding");
    auto changed=bound.Map;changed.Uniforms.Collections[0][0]^=1;
    const auto changedUser=M::ReadOriginalMaterialUserData(source->Asset,changed,reader.Resolver(),{cameraSource->Asset,envSource->Asset});
    Check(changedUser.Collections.size()==1&&!changedUser.Collections[0].Available,"changed shader StateId never gains a position-based binding");
    std::cout<<Checks<<" collection metadata controls passed / "<<reads<<" raw reads / zero archive or graph calls; raw prerequisites grant no shader-use or native-import authority\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

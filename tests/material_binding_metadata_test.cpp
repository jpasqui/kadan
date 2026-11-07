#include "../core/material_binding_metadata.h"
#include <fstream>
#include <iostream>
namespace M=P4MapKit::Material;namespace C=P4MapKit::CustomMaterial;
static unsigned Checks=0;
static void Check(bool value){++Checks;if(!value)throw std::runtime_error("binding metadata control failed");}
template<class F>static void Refuses(F f){bool held=false;try{f();}catch(const M::Refused&){held=true;}Check(held);}
static C::Bytes File(const std::string& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("explicit source missing");in.seekg(0,std::ios::end);const auto n=in.tellg();if(n<=0||std::uint64_t(n)>64u*1024u*1024u)throw std::runtime_error("source bound");in.seekg(0);C::Bytes out(std::size_t(n),0);if(!in.read(reinterpret_cast<char*>(out.data()),n))throw std::runtime_error("source read");return out;}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("explicit extracted game source root required");const std::string root=argv[1];
    const std::string texture="/Game/Carrot4/MasterMaterial/Unique/Frozen/T_Frozen_00_N";
    bool absent=true,corrupt=false;unsigned reads=0;
    M::MaterialOriginalImportRawResolver raw=[&](const std::string& package,M::MaterialOriginalImportRawAsset& out){
        ++reads;if(package==texture&&absent)return false;
        const auto stem=root+"/Carrot4/Content/"+package.substr(6);std::ifstream exists(stem+".uasset",std::ios::binary);if(!exists)return false;
        auto header=std::make_shared<C::Bytes>(File(stem+".uasset"));if(package==texture&&corrupt)header->at(0)^=1;
        out={header,std::make_shared<const C::Bytes>(File(stem+".uexp"))};return true;
    };
    for(const auto* name:{"MI_Frozen_Masked_00","MI_Frozen_Masked_00_Dither"}){
        const auto owner=std::string("/Game/Carrot4/MasterMaterial/Unique/Frozen/")+name;
        const auto requests=M::DiscoverOriginalMaterialBindingMetadata(owner,raw);
        Check(requests.size()==1);const auto& request=requests.at(0);
        Check(request.Owner==owner&&request.OwnerSourceSha256.size()==64&&request.Package==texture&&request.Object==texture+".T_Frozen_00_N"&&request.NativeClass=="/Script/Engine.Texture2D");
        Check(!request.References.empty());
        for(const auto& ref:request.References)Check(ref.ImportIndex<0&&ref.SourceSha256.size()==64&&!ref.Property.empty()&&ref.Object==request.Object&&ref.NativeClass==request.NativeClass);
        absent=false;Check(M::DiscoverOriginalMaterialBindingMetadata(owner,raw).empty());
        corrupt=true;Refuses([&]{M::DiscoverOriginalMaterialBindingMetadata(owner,raw);});corrupt=false;absent=true;
    }
    Refuses([&]{M::DiscoverOriginalMaterialBindingMetadata("/Game/Missing",raw);});
    std::cout<<Checks<<" metadata prerequisite checks passed / "<<reads<<" raw reads / zero archive or graph calls; no asset-import authority\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

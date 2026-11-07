// Standalone read-only probe used by native_lighting_registry_parity.py.
#include "core/lighting_registry.h"
#include "core/lighting_package.h"
#include "core/custom_material_hash.h"
#include "core/export_contract.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace P4MapKit;
using namespace P4MapKit::LightingRegistry;
static Bytes File(const std::string& Path) {
    std::ifstream F(Path,std::ios::binary|std::ios::ate);const auto N=F.tellg();
    if(!F || N<0 || N>64*1024*1024) throw std::runtime_error("lighting-probe-file-bound");
    Bytes B(static_cast<std::size_t>(N));F.seekg(0);F.read(reinterpret_cast<char*>(B.data()),N);
    if(!F) throw std::runtime_error("lighting-probe-file-unreadable");return B;
}
static std::string Id(const Guid& G) {std::ostringstream O;O<<std::hex<<std::uppercase<<std::setfill('0');
    for(auto W:G) O<<std::setw(8)<<W;return O.str();}
template<class A> static void Array(std::ostream& O,const A& V) {O<<'[';bool First=true;
    for(auto Item:V) {if(!First) O<<',';First=false;O<<Item;}O<<']';}
static void Ids(std::ostream& O,const std::vector<Guid>& G) {O<<'[';bool First=true;
    for(const auto& V:G) {if(!First) O<<',';First=false;O<<Export::Quote(Id(V));}O<<']';}
static void Texture(std::ostream& O,const TextureRef& V) {if(V.Object.empty()) O<<"null";else O<<Export::Quote(V.Object);}
static void Light(std::ostream& O,const LightMap& V) {
    if(!V.Present) {O<<"null";return;}O<<"{\"kind\":\"2D\",\"light_guids\":";Ids(O,V.Lights);
    O<<",\"textures\":[";Texture(O,V.Textures[0]);O<<',';Texture(O,V.Textures[1]);O<<']';
    O<<",\"sky_occlusion\":";Texture(O,V.Textures[2]);O<<",\"ao_mask\":";Texture(O,V.Textures[3]);
    O<<",\"scale_vectors\":[";for(unsigned I=0;I<4;++I) {if(I) O<<',';Array(O,V.Scale[I]);}
    O<<"],\"add_vectors\":[";for(unsigned I=0;I<4;++I) {if(I) O<<',';Array(O,V.Add[I]);}
    O<<"],\"coordinate_scale\":";Array(O,V.CoordinateScale);O<<",\"coordinate_bias\":";Array(O,V.CoordinateBias);
    O<<",\"shadow_channel_valid\":";Array(O,V.Channels);O<<",\"inv_uniform_penumbra_size\":";Array(O,V.Penumbra);O<<'}';
}
static void Shadow(std::ostream& O,const ShadowMap& V) {
    if(!V.Present) {O<<"null";return;}O<<"{\"kind\":\"2D\",\"light_guids\":";Ids(O,V.Lights);
    O<<",\"texture\":";Texture(O,V.Texture);O<<",\"coordinate_scale\":";Array(O,V.CoordinateScale);
    O<<",\"coordinate_bias\":";Array(O,V.CoordinateBias);O<<",\"channel_valid\":";Array(O,V.Channels);
    O<<",\"inv_uniform_penumbra_size\":";Array(O,V.Penumbra);O<<'}';
}
static void HeaderCases(const Bytes& H,std::size_t BodySize) {
    LightingRegistry::Package R;FReadError E;
    if(!LightingRegistry::ReadPackageHeader(H,BodySize,R,E)) throw std::runtime_error(E.Code);
    const auto Names=R.Inventory.Names;const auto OriginalUE4=R.UE4Version;
    for(std::size_t N=0;N<H.size();++N) {
        Bytes Cut(H.begin(),H.begin()+N);
        if(LightingRegistry::ReadPackageHeader(Cut,BodySize,R,E)||R.Inventory.Names!=Names||R.UE4Version!=OriginalUE4)
            throw std::runtime_error("header-truncation-output-custody");
    }
    for(std::size_t At:{std::size_t(8),std::size_t(12),std::size_t(16)}) {
        auto Bad=H;Bad[At]^=1;
        if(LightingRegistry::ReadPackageHeader(Bad,BodySize,R,E)||R.Inventory.Names!=Names)
            throw std::runtime_error("header-version-refusal");
    }
    if(H[20]>=3) {
        for(std::size_t At:{std::size_t(40),std::size_t(60),std::size_t(80)}) {
            auto Bad=H;Bad[At]^=1;
            if(LightingRegistry::ReadPackageHeader(Bad,BodySize,R,E)) throw std::runtime_error("custom-version-refusal");
        }
        auto Bad=H;std::copy(Bad.begin()+24,Bad.begin()+40,Bad.begin()+44);
        if(LightingRegistry::ReadPackageHeader(Bad,BodySize,R,E)) throw std::runtime_error("duplicate-custom-version-refusal");
    }
}
int main(int Argc,char** Argv) {
    try {
        if(Argc!=3 && Argc!=4) throw std::runtime_error("header and canonical owner package required");
        const std::string Path=Argv[1],Owner=Argv[2];const auto Dot=Path.find_last_of('.');
        const auto H=File(Path),B=File(Path.substr(0,Dot)+".uexp");LightingRegistry::Package Qualified;FReadError HeaderError;
        if(Argc==4 && std::string(Argv[3])=="--header-cases") HeaderCases(H,B.size());
        if(!LightingRegistry::ReadPackageHeader(H,B.size(),Qualified,HeaderError)) throw std::runtime_error(HeaderError.Code);
        auto P=Qualified.Inventory;
        if(P.Package!="None" && !P.Package.empty() && P.Package!=Owner) throw std::runtime_error("package-owner-differs");
        P.Package=Owner;const FPackageExport* RegistryExport=nullptr;
        for(const auto& X:P.Exports) {std::string Class;if(!P.Reference(X.Class,Class)) throw std::runtime_error("invalid-class-reference");
            if(Class=="/Script/Engine.MapBuildDataRegistry") {if(RegistryExport) throw std::runtime_error("multiple-registries");RegistryExport=&X;}}
        if(!RegistryExport) throw std::runtime_error("registry-absent");
        const auto Tags=ReadPropertyEnvelopes(B,P.Names,RegistryExport->Offset,RegistryExport->Offset+RegistryExport->Size);
        if(!Tags.Error.Code.empty()) throw std::runtime_error(Tags.Error.Code);
        const auto Resolver=[&](std::int32_t Index,std::string& Object,std::string& Class) {
            if(Index<=0 || std::size_t(Index)>P.Exports.size()) return false;
            const auto& X=P.Exports[std::size_t(Index-1)];
            if(!P.Reference(Index,Object)||!P.Reference(X.Class,Class)) return false;
            Object=Owner+"."+Object;return true;
        };
        Registry R;FReadError E;if(!Read(B,Tags.End,RegistryExport->Offset+RegistryExport->Size,Resolver,R,E))
            throw std::runtime_error(E.Code+"@"+std::to_string(E.Offset));
        std::cout<<std::setprecision(9)<<std::showpoint<<"{\"canonical_sha256\":"<<Export::Quote(CustomMaterial::Hex(CustomMaterial::Sha256(R.Canonical)))
            <<",\"meshes\":{";bool First=true;
        for(const auto& Pair:R.Meshes) {if(!First) std::cout<<',';First=false;
            std::cout<<Export::Quote(Id(Pair.first))<<":{\"lightmap\":";Light(std::cout,Pair.second.Light);
            std::cout<<",\"shadowmap\":";Shadow(std::cout,Pair.second.Shadow);
            std::cout<<",\"irrelevant_lights\":";Ids(std::cout,Pair.second.IrrelevantLights);
            std::cout<<",\"per_instance\":[";bool FirstInstance=true;
            for(const auto& V:Pair.second.PerInstance) {if(!FirstInstance) std::cout<<',';FirstInstance=false;
                std::cout<<"{\"lightmap_uv_bias\":["<<V[0]<<','<<V[1]<<"],\"shadowmap_uv_bias\":["<<V[2]<<','<<V[3]<<"]}";}
            std::cout<<"]}";}
        std::cout<<"},\"light_records\":{";First=true;
        for(const auto& Item:R.LightRecords) {
            if(!First) std::cout<<',';First=false;const auto& V=Item.second;Bytes Depth;
            for(const auto W:V.Depth) {Depth.push_back(std::uint8_t(W));Depth.push_back(std::uint8_t(W>>8));}
            std::cout<<Export::Quote(Id(Item.first))<<":{\"shadow_map_channel\":"<<V.ShadowChannel<<",\"world_to_light\":";
            Array(std::cout,V.WorldToLight);std::cout<<",\"depth_map_size\":["<<V.DepthWidth<<','<<V.DepthHeight
                <<"],\"depth_sha256\":"<<Export::Quote(CustomMaterial::Hex(CustomMaterial::Sha256(Depth)))<<'}';
        }
        std::cout<<"},\"light_volumes\":"<<R.LightVolumes<<",\"volumetric_lightmaps\":"<<R.VolumetricLightmaps
            <<",\"lights\":"<<R.Lights<<",\"reflection_captures\":"<<R.Reflections<<",\"sky_atmospheres\":"<<R.Skies<<"}\n";
        return 0;
    } catch(const std::exception& E) {std::cerr<<E.what()<<'\n';return 1;}
}

#include "../core/creator_mesh_package.h"
#include "../core/static_mesh_facts.h"
#include "../core/mesh_nav_collision.h"
#include "../core/mesh_cooked_readback.h"
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace P4MapKit;using Bytes=std::vector<std::uint8_t>;
static bool File(const std::string& Name,Bytes& Out) {
    std::ifstream In(Name,std::ios::binary|std::ios::ate);if(!In) return false;const auto Size=In.tellg();
    if(Size<0||Size>256*1024*1024) return false;Out.resize(std::size_t(Size));In.seekg(0);if(Size) In.read(reinterpret_cast<char*>(Out.data()),Size);return bool(In);
}
static bool Save(const std::string& Name,const Bytes& Data) {std::ofstream Out(Name,std::ios::binary);Out.write(reinterpret_cast<const char*>(Data.data()),std::streamsize(Data.size()));return bool(Out);}
static void Geometry(const std::vector<PhysicsBody::Vec3>& Vertices,const std::vector<std::array<std::uint32_t,3>>& Triangles) {
    std::cout<<std::scientific<<"{\"vertices\":[";bool First=true;for(const auto& V:Vertices) {if(!First) std::cout<<',';First=false;std::cout<<'['<<V[0]<<','<<V[1]<<','<<V[2]<<']';}
    std::cout<<"],\"triangles\":[";First=true;for(const auto& T:Triangles) {if(!First) std::cout<<',';First=false;std::cout<<'['<<T[0]<<','<<T[1]<<','<<T[2]<<']';}std::cout<<"]}";
}
int main(int Argc,char** Argv) {
    if(Argc!=4 && Argc!=6) return 2;
    const std::string Name=Argv[1];const auto Dot=Name.rfind('.');if(Dot==std::string::npos) return 2;const auto Stem=Name.substr(0,Dot);
    Bytes H,B;std::array<Bytes,3> Sidecars;if(!File(Name,H)||!File(Stem+".uexp",B)) return 2;
    FReadError Error;CreatorMeshPackage::Header Profile;FByteBulkContext Context;
    const auto Fail=[&]() {std::cerr<<Error.Offset<<' '<<Error.Code<<'\n';return 1;};
    if(!CreatorMeshPackage::Read(H,B.size(),Profile,Error)||!CreatorMeshPackage::BulkContext(H,B.size(),Context,Error)) return Fail();
    const std::array<std::string,3> Suffix{{".ubulk",".uptnl",".m.ubulk"}};
    for(std::size_t I=0;I<3;++I) if(File(Stem+Suffix[I],Sidecars[I])) Context.FileSizes[I+1]=std::int64_t(Sidecars[I].size());
    const FStaticMeshBulkRead At=[&](EBulkSource Source,std::uint64_t Offset,std::uint8_t* To,std::size_t Size) {
        if(Source<EBulkSource::Body || Source>EBulkSource::Mapped) return false;
        const auto& Data=Source==EBulkSource::Body?B:Sidecars[std::size_t(Source)-2];
        if(Offset>Data.size() || Size>Data.size()-Offset) return false;if(Size) std::memcpy(To,Data.data()+std::size_t(Offset),Size);return true;
    };
    const PhysicsBody::ReadBulk Bulk=[&](const FByteBulkRange& Range,Bytes& Out) {
        if(Range.Size>64*1024*1024) return false;Out.resize(std::size_t(Range.Size));return At(Range.Source,Range.Offset,Out.data(),Out.size());
    };
    FCookedStaticMeshFacts Facts;if(!ReadCookedStaticMeshFacts(Profile.Inventory,B,Argv[2],Argv[3],Context,At,Facts,Error)) return Fail();
    PhysicsBody::Body Physics;if(!Facts.BodyIndex || !PhysicsBody::ReadBody(B,Facts.BodyNativeOffset,Facts.BodyExport.Offset+Facts.BodyExport.Size,Profile.Inventory.Names,Context,Bulk,Physics,Error)) return Fail();
    MeshNavCollision::Facts Nav;const auto NavIndex=Facts.Render.NavCollisionIndex;
    if(NavIndex) {
        if(NavIndex<1 || std::size_t(NavIndex)>Profile.Inventory.Exports.size()) return 2;
        const auto& Export=Profile.Inventory.Exports[std::size_t(NavIndex)-1];std::string Class;
        if(Export.Outer!=Facts.ExportIndex || !Profile.Inventory.Reference(Export.Class,Class) || Class!="/Script/NavigationSystem.NavCollision") return 2;
        FPropertyDecoder Decoder(Profile.Inventory,B);std::size_t Position=Export.Offset;FDecodedValue Properties;
        if(!Decoder.TypedProperties(Position,Export.Offset+Export.Size,Properties,0,"NavCollision")) {Error=Decoder.Error;return Fail();}
        if(!MeshNavCollision::Read(B,Position,Export.Offset+Export.Size,Profile.Inventory,Context,Bulk,Nav,Error)) return Fail();
    }
    if(Profile.Inventory.Exports.size()!=std::size_t(2+(NavIndex!=0))) {std::cerr<<"Unexpected mesh package exports\n";return 1;}
    MeshCookedReadback::Facts Complete;const std::array<std::int64_t,3> Sizes{{Context.FileSizes[1],Context.FileSizes[2],Context.FileSizes[3]}};
    if(!MeshCookedReadback::Read(H,B,Argv[2],Argv[3],Sizes,At,Complete,Error)) return Fail();
    if(Argc==6) {
        Export::RewrittenPackage Converted;if(!CreatorMeshPackage::RelabelPhysX(H,B,Converted,Error)) return Fail();
        if(!Save(Argv[4],Converted.Header)||!Save(Argv[5],Converted.Body)||!Save(std::string(Argv[4])+".canonical",Complete.Canonical)) return 2;
    }
    std::cout<<std::setprecision(std::numeric_limits<float>::max_digits10)<<"{\"valid\":true,\"canonical_bytes\":"<<Complete.Canonical.size()<<",\"stock_version\":"<<Profile.UE4<<",\"lods\":"<<Facts.Render.LODs.size()<<",\"materials\":"<<Facts.Materials.size()<<",\"physics\":[";
    bool First=true;for(const auto& F:Physics.Formats) for(const auto& M:F.Data.TriMeshes) {
        if(!First) std::cout<<',';First=false;Geometry(M.Vertices,M.Triangles);
    }
    std::cout<<"],\"nav_tri\":";Geometry(Nav.Tri.Vertices,Nav.Tri.Triangles);std::cout<<",\"nav_convex\":";Geometry(Nav.Convex.Vertices,Nav.Convex.Triangles);std::cout<<"}\n";return 0;
}

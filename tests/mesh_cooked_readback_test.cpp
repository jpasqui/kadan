#include "../core/mesh_cooked_readback.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
using namespace P4MapKit;using Bytes=std::vector<std::uint8_t>;
static std::size_t Checks=0;static void Check(bool V) {++Checks;if(!V) {std::cerr<<"mesh readback failed "<<Checks<<'\n';std::exit(1);}}
static Bytes File(const char* P) {std::ifstream In(P,std::ios::binary);Check(bool(In));return {std::istreambuf_iterator<char>(In),std::istreambuf_iterator<char>()};}
int main(int Argc,char** Argv) {
    if(Argc!=5) return 2;const auto H=File(Argv[1]),B=File(Argv[2]);const std::string Owner=Argv[3],Object=Argv[4];
    MeshCookedReadback::Facts Good;FReadError Error;const std::array<std::int64_t,3> Sizes{{-1,-1,-1}};
    Check(MeshCookedReadback::Read(H,B,Owner,Object,Sizes,{},Good,Error));Check(!Good.Canonical.empty());
    const auto Refuse=[&](const Bytes& Header,const Bytes& Body,const std::string& Package=std::string()) {
        auto Kept=Good;Check(!MeshCookedReadback::Read(Header,Body,Package.empty()?Owner:Package,Object,Sizes,{},Kept,Error));Check(!Error.Code.empty());Check(Kept.Canonical==Good.Canonical);
    };
    Refuse(H,B,"/Game/Wrong/Package");
    for(std::size_t N=0;N<B.size();++N) Refuse(H,{B.begin(),B.begin()+N});
    Export::RewrittenPackage Converted;Check(CreatorMeshPackage::RelabelPhysX(H,B,Converted,Error));MeshCookedReadback::Facts After;
    Check(MeshCookedReadback::Read(Converted.Header,Converted.Body,Owner,Object,Sizes,{},After,Error));Check(After.Canonical==Good.Canonical);
    // Author an explicit PC label variant of the retained Generic package;
    // this is a storage relocation fixture, not an actual new native cook.
    auto Dispatch=H;std::fill(Dispatch.begin()+8,Dispatch.begin()+16,0);Export::RewrittenPackage PC;std::string Why;
    Check(Export::RewriteCookedNames(Dispatch,B,{{"PhysXGeneric","PhysXPC"}},PC,Why));std::copy(H.begin()+8,H.begin()+16,PC.Header.begin()+8);
    Check(MeshCookedReadback::Read(PC.Header,B,Owner,Object,Sizes,{},After,Error));Check(After.Canonical==Good.Canonical);
    Check(CreatorMeshPackage::RelabelPhysX(PC.Header,B,Converted,Error));Check(Converted.Changed);
    Check(MeshCookedReadback::Read(Converted.Header,B,Owner,Object,Sizes,{},After,Error));Check(After.Canonical==Good.Canonical);
    // Opaque or malformed native tails do not silently become a matching token.
    auto Bad=B;Bad[Good.Mesh.BodyNativeOffset+24]=2;Refuse(H,Bad);
    Bad=B;Bad[Good.Mesh.BodyNativeOffset+4]^=1;Check(MeshCookedReadback::Read(H,Bad,Owner,Object,Sizes,{},After,Error));Check(After.Canonical!=Good.Canonical);
    const auto& Mesh=Good.Mesh;Bad=B;Bad[Mesh.MeshExport.Offset+Mesh.MeshExport.Size-1]^=1;
    auto Retained=Good;const bool Decoded=MeshCookedReadback::Read(H,Bad,Owner,Object,Sizes,{},After,Error);Check(!Decoded||After.Canonical!=Good.Canonical);
    Check(!MeshCookedReadback::Read(H,B,Owner,Object,Sizes,{},Retained,Error,[](){return true;}));Check(Retained.Canonical==Good.Canonical);
    std::cout<<"mesh cooked readback: "<<Checks<<" checks passed, "<<Good.Canonical.size()<<" canonical bytes\n";return 0;
}

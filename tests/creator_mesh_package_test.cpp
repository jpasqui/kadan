#include "../core/creator_mesh_package.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
using namespace P4MapKit;using Bytes=std::vector<std::uint8_t>;
static std::size_t Checks=0;
static void Check(bool V) {++Checks;if(!V) {std::cerr<<"creator mesh package failed "<<Checks<<'\n';std::exit(1);}}
static Bytes File(const char* P) {std::ifstream In(P,std::ios::binary);Check(bool(In));return {std::istreambuf_iterator<char>(In),std::istreambuf_iterator<char>()};}
static void Patch(Bytes& B,std::size_t At,std::uint32_t V) {for(unsigned I=0;I<4;++I) B[At+I]=std::uint8_t(V>>(I*8));}
int main(int Argc,char** Argv) {
    if(Argc!=3) return 2;const auto H=File(Argv[1]),B=File(Argv[2]);CreatorMeshPackage::Header Good;FReadError E;
    Check(CreatorMeshPackage::Read(H,B.size(),Good,E));Check(Good.UE3==864 && Good.UE4==522 && !Good.Retoc);Check(Good.Versions.size()>=8);
    FByteBulkContext Context;Check(CreatorMeshPackage::BulkContext(H,B.size(),Context,E));Check(Context.HasBulkStart);
    for(std::size_t N=0;N<H.size();++N) {
        CreatorMeshPackage::Header Kept=Good;Check(!CreatorMeshPackage::Read({H.begin(),H.begin()+N},B.size(),Kept,E));Check(Kept.Versions==Good.Versions);
    }
    const auto Refuse=[&](const Bytes& Input) {auto Kept=Good;Check(!CreatorMeshPackage::Read(Input,B.size(),Kept,E));Check(Kept.Inventory.Names==Good.Inventory.Names);};
    for(const auto At:{8u,12u,16u}) {auto Bad=H;Patch(Bad,At,1);Refuse(Bad);}
    const auto N=Good.Versions.size();for(std::size_t I=0;I<N;++I) {
        auto Bad=H;Patch(Bad,40+I*20,UINT32_MAX);Refuse(Bad);
        Bad=H;std::copy(H.begin()+24,H.begin()+40,Bad.begin()+24+I*20);
        if(I) Refuse(Bad);
    }
    // Missing relevant versions and obsolete Chaos-tail gates cannot be treated
    // as the same profile merely because the summary still says UE4.26.
    for(std::size_t I=0;I<N;++I) {auto Bad=H;Bad[24+I*20]^=128;Refuse(Bad);}
    Export::RewrittenPackage Rewrite;Check(CreatorMeshPackage::RelabelPhysX(H,B,Rewrite,E));Check(Rewrite.Body==B);
    CreatorMeshPackage::Header After;Check(CreatorMeshPackage::Read(Rewrite.Header,Rewrite.Body.size(),After,E));Check(After.Versions==Good.Versions && After.UE3==Good.UE3 && After.UE4==Good.UE4);
    Check(std::find(After.Inventory.Names.begin(),After.Inventory.Names.end(),"PhysXPC")==After.Inventory.Names.end());
    Export::RewrittenPackage Second;Check(CreatorMeshPackage::RelabelPhysX(Rewrite.Header,B,Second,E));Check(!Second.Changed && Second.Header==Rewrite.Header);
    auto BadBody=B;BadBody.back()^=1;Check(!CreatorMeshPackage::RelabelPhysX(H,BadBody,Second,E));Check(Second.Header==Rewrite.Header);
    std::cout<<"creator mesh package: "<<Checks<<" checks passed\n";return 0;
}

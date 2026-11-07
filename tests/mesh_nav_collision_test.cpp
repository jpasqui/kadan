#include "../core/mesh_nav_collision.h"
#include <cstdlib>
#include <iostream>
using namespace P4MapKit;using Bytes=std::vector<std::uint8_t>;
static std::size_t Checks=0;
static void Check(bool V) {++Checks;if(!V) {std::cerr<<"mesh nav failed "<<Checks<<'\n';std::exit(1);}}
static void Word(Bytes& B,std::uint32_t W) {for(unsigned I=0;I<4;++I) B.push_back(std::uint8_t(W>>(I*8)));}
static void Real(Bytes& B,float F) {std::uint32_t W=0;std::memcpy(&W,&F,4);Word(B,W);}
static void Patch(Bytes& B,std::size_t At,std::uint32_t W) {for(unsigned I=0;I<4;++I) B[At+I]=std::uint8_t(W>>(I*8));}
int main() {
    // Independent v4 native archive: one triangle and one convex-index group.
    Bytes Payload{1};
    for(unsigned G=0;G<2;++G) {Word(Payload,3);for(const auto V:std::vector<PhysicsBody::Vec3>{{0,0,0},{1,0,0},{0,1,0}}) for(float F:V) Real(Payload,F);Word(Payload,3);for(unsigned I=0;I<3;++I) Word(Payload,I);}
    Word(Payload,1);Word(Payload,0);
    Bytes Tail;Word(Tail,0);Word(Tail,0xa237f237);Word(Tail,4);for(unsigned W:{1u,2u,3u,4u}) Word(Tail,W);
    Word(Tail,1);Word(Tail,1);Word(Tail,0);Word(Tail,0);Word(Tail,BulkInline|BulkSingleUse);Word(Tail,std::uint32_t(Payload.size()));Word(Tail,std::uint32_t(Payload.size()));Word(Tail,0);Word(Tail,0);
    const auto PayloadAt=Tail.size();Tail.insert(Tail.end(),Payload.begin(),Payload.end());Word(Tail,0);
    FPackageInventory Inventory;Inventory.Names={"NavCollision_X"};FByteBulkContext Context;Context.FileSizes[0]=std::int64_t(Tail.size());
    const PhysicsBody::ReadBulk Read=[&](const FByteBulkRange& R,Bytes& Out) {if(R.Source!=EBulkSource::Body||R.Offset>Tail.size()||R.Size>Tail.size()-R.Offset) return false;Out={Tail.begin()+std::size_t(R.Offset),Tail.begin()+std::size_t(R.Offset+R.Size)};return true;};
    MeshNavCollision::Facts Good;FReadError Error;Check(MeshNavCollision::Read(Tail,0,Tail.size(),Inventory,Context,Read,Good,Error));Check(Good.Serialized==Payload);Check(Good.Tri.Triangles.size()==1&&Good.Convex.Vertices.size()==3);Check(Good.ConvexStarts==std::vector<std::uint32_t>{0});
    const auto Refuse=[&](const Bytes& B,const PhysicsBody::ReadBulk& Reader=PhysicsBody::ReadBulk()) {
        const PhysicsBody::ReadBulk Current=[&](const FByteBulkRange& R,Bytes& Out) {if(R.Source!=EBulkSource::Body||R.Offset>B.size()||R.Size>B.size()-R.Offset) return false;Out={B.begin()+std::size_t(R.Offset),B.begin()+std::size_t(R.Offset+R.Size)};return true;};
        auto Kept=Good;Check(!MeshNavCollision::Read(B,0,B.size(),Inventory,Context,Reader?Reader:Current,Kept,Error));Check(!Error.Code.empty());Check(Kept.Serialized==Good.Serialized);
    };
    for(std::size_t N=0;N<Tail.size();++N) Refuse({Tail.begin(),Tail.begin()+N});
    for(const auto At:{0u,4u,8u,28u,32u,36u,40u}) {auto Bad=Tail;Patch(Bad,At,0xffffffff);Refuse(Bad);}
    auto Bad=Tail;Bad[PayloadAt]=0;Refuse(Bad);Bad=Tail;Patch(Bad,PayloadAt+5,0x7f800000);Refuse(Bad);Bad=Tail;Patch(Bad,PayloadAt+45,3);Refuse(Bad);
    Bad=Tail;Patch(Bad,Tail.size()-4,17);Refuse(Bad);Bad=Tail;Bad.push_back(0);Refuse(Bad);
    Refuse(Tail,[](const FByteBulkRange&,Bytes&){return false;});
    for(unsigned Count:{0u,1u,2u,0xffffffffu}) {Bad=Tail;Patch(Bad,PayloadAt+1,Count);Refuse(Bad);}
    std::cout<<"mesh nav collision: "<<Checks<<" checks passed\n";return 0;
}

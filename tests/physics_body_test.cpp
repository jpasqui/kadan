#include "../core/physics_body.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
using namespace P4MapKit;using namespace P4MapKit::PhysicsBody;
static std::size_t Checks=0;
static void Check(bool V) {++Checks;if(!V) {std::cerr<<"physics test failed at check "<<Checks<<'\n';std::exit(1);}}
static void Word(Bytes& B,std::uint32_t V,bool Big=false) {for(int I=0;I<4;++I) B.push_back(std::uint8_t(V>>(8*(Big?3-I:I))));}
static void Float(Bytes& B,float V,bool Big=false) {std::uint32_t W=0;std::memcpy(&W,&V,4);Word(B,W,Big);}
static void Patch(Bytes& B,std::size_t At,std::uint32_t V) {for(int I=0;I<4;++I) B[At+I]=std::uint8_t(V>>(8*I));}
struct Fixture {Bytes Data;std::size_t VertexAt=0,TriangleAt=0,TreeAt=0,UVAt=0;};
static Fixture Make(unsigned Width=2,bool UV=false) {
    Fixture F;auto& B=F.Data;B.push_back(1);Word(B,0);Word(B,0);Word(B,1);
    const std::string Magic("NXS\x01MESH",8);B.insert(B.end(),Magic.begin(),Magic.end());
    Word(B,14);Word(B,0);Word(B,Width==1?4:Width==2?8:0);Word(B,3);Word(B,1);F.VertexAt=B.size();
    for(const Vec3 V:std::vector<Vec3>{{0,0,0},{1,0,0},{0,1,0}}) for(auto X:V) Float(B,X);
    F.TriangleAt=B.size();for(unsigned I=0;I<3;++I) for(unsigned C=0;C<Width;++C) B.push_back(std::uint8_t(I>>(C*8)));
    F.TreeAt=B.size();B.insert(B.end(),{'R','T','R','E'});Word(B,1,true);
    for(unsigned I=0;I<16;++I) Float(B,I>=4&&I<8?1.0f:0.0f,true);
    for(const auto V:{4u,1u,1u,4u,1u,0u}) Word(B,V,true);
    for(unsigned Axis=0;Axis<6;++Axis) for(unsigned N=0;N<4;++N)
        Float(B,N? (Axis<3?std::numeric_limits<float>::max():-std::numeric_limits<float>::max()):(Axis<3?0.0f:1.0f),true);
    for(unsigned N=0;N<4;++N) Word(B,N?1:1,true);
    Float(B,0.00001f);for(const auto V:{0.0f,0.0f,0.0f,1.0f,1.0f,0.0f}) Float(B,V);Word(B,1);B.push_back(7);F.UVAt=B.size();
    Word(B,UV?3:0);if(UV) for(unsigned I=0;I<3;++I) Word(B,I);
    Word(B,UV?3:0);if(UV) for(const Vec3 V:std::vector<Vec3>{{0,0,0},{1,0,0},{0,1,0}}) for(auto X:V) Float(B,X);
    Word(B,UV?1:0);if(UV) {Word(B,3);for(const auto V:{0.0f,0.0f,1.0f,0.0f,0.0f,1.0f}) Float(B,V);}
    return F;
}
static Bytes File(const char* Name) {std::ifstream In(Name,std::ios::binary);Check(bool(In));return {std::istreambuf_iterator<char>(In),std::istreambuf_iterator<char>()};}
int main(int Argc,char** Argv) {
    if(Argc==2) {
        Cook Result;FReadError Error;if(!ReadCook(File(Argv[1]),Result,Error)) {std::cerr<<Error.Offset<<' '<<Error.Code<<'\n';return 2;}
        std::cout<<"{\"meshes\":"<<Result.TriMeshes.size()<<",\"vertices\":"<<(Result.TriMeshes.empty()?0:Result.TriMeshes[0].Vertices.size())<<",\"triangles\":"<<(Result.TriMeshes.empty()?0:Result.TriMeshes[0].Triangles.size())<<",\"pages\":"<<(Result.TriMeshes.empty()?0:Result.TriMeshes[0].Pages.size())<<",\"bytes\":"<<Result.Serialized.size()<<"}\n";return 0;
    }
    for(unsigned Width:{1u,2u,4u}) for(bool UV:{false,true}) {
        const auto F=Make(Width,UV);Cook Result;FReadError Error;
        Check(ReadCook(F.Data,Result,Error));Check(Result.TriMeshes.size()==1);Check(Result.TriMeshes[0].Triangles[0]==std::array<std::uint32_t,3>{{0,1,2}});Check(Result.Serialized==F.Data);
        Check(Result.UVChannels.size()==std::size_t(UV));const auto Sentinel=Result.Serialized;
        for(std::size_t N=0;N<F.Data.size();++N) {Check(!ReadCook({F.Data.begin(),F.Data.begin()+N},Result,Error));Check(Result.Serialized==Sentinel);}
        auto Bad=F.Data;Bad.push_back(0);Check(!ReadCook(Bad,Result,Error));
        Bad=F.Data;Bad[0]=0;Check(!ReadCook(Bad,Result,Error));
        Bad=F.Data;Patch(Bad,1,1);Check(!ReadCook(Bad,Result,Error));Check(Error.Code=="physics-convex-stream-unqualified");
        Bad=F.Data;Patch(Bad,21,15);Check(!ReadCook(Bad,Result,Error));
        Bad=F.Data;Patch(Bad,F.VertexAt,0x7f800000);Check(!ReadCook(Bad,Result,Error));
        Bad=F.Data;Bad[F.TriangleAt]=3;Check(!ReadCook(Bad,Result,Error));
        Bad=F.Data;Patch(Bad,F.TreeAt+4,1);Check(!ReadCook(Bad,Result,Error));
        Bad=F.Data;const auto Child=F.TreeAt+96+96;Bad[Child+3]=0;Check(!ReadCook(Bad,Result,Error)); // root self-cycle
        Bad=F.Data;Bad[Child+3]=3;Check(!ReadCook(Bad,Result,Error)); // leaf claims two triangles
        Bad=F.Data;Patch(Bad,F.UVAt,0x7fffffff);Check(!ReadCook(Bad,Result,Error));
        if(UV) {Bad=F.Data;Patch(Bad,F.UVAt+4,3);Check(!ReadCook(Bad,Result,Error));}
        Bytes Tail;Word(Tail,0);for(unsigned V:{1u,2u,3u,4u}) Word(Tail,V);Word(Tail,1);Word(Tail,1);Word(Tail,1);Word(Tail,0);Word(Tail,0);
        Word(Tail,BulkInline|BulkSingleUse);Word(Tail,std::uint32_t(F.Data.size()));Word(Tail,std::uint32_t(F.Data.size()));Word(Tail,123);Word(Tail,0);Tail.insert(Tail.end(),F.Data.begin(),F.Data.end());
        FByteBulkContext Context;Context.FileSizes[0]=std::int64_t(Tail.size());
        const ReadBulk Reader=[&](const FByteBulkRange& Range,Bytes& Data) {if(Range.Source!=EBulkSource::Body || Range.Offset>Tail.size() || Range.Size>Tail.size()-Range.Offset) return false;Data={Tail.begin()+std::size_t(Range.Offset),Tail.begin()+std::size_t(Range.Offset+Range.Size)};return true;};
        Body Read;Check(ReadBody(Tail,0,Tail.size(),{"PhysXPC"},Context,Reader,Read,Error));Check(Read.Formats[0].Data.Serialized==F.Data);
        for(std::size_t N=0;N<Tail.size();++N) {Body Kept=Read;Check(!ReadBody(Tail,0,N,{"PhysXPC"},Context,Reader,Kept,Error));Check(Kept.Formats[0].Data.Serialized==F.Data);}
        Check(!ReadBody(Tail,0,Tail.size(),{"Chaos"},Context,Reader,Read,Error));Check(!ReadBody(Tail,0,Tail.size(),{"PhysXPC"},Context,{},Read,Error));
        Tail.push_back(0);Check(!ReadBody(Tail,0,Tail.size(),{"PhysXPC"},Context,Reader,Read,Error));
    }
    std::cout<<"physics body: "<<Checks<<" checks passed\n";return 0;
}

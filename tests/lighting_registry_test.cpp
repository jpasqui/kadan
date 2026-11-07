#include "core/lighting_registry.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace P4MapKit::LightingRegistry;
static void U(Bytes& B,std::uint32_t V) {Detail::Word(B,V);}
static void F(Bytes& B,float V) {std::uint32_t W=0;std::memcpy(&W,&V,4);U(B,W);}
static void Fs(Bytes& B,unsigned N,float V=0.25f) {for(unsigned I=0;I<N;++I) F(B,V+I);}
static void G(Bytes& B,unsigned Id) {U(B,Id);U(B,0);U(B,0);U(B,0);}
static void Ids(Bytes& B) {U(B,1);G(B,19);}
static void Box(Bytes& B) {Fs(B,6);B.push_back(1);}
static void Blob(Bytes& B,unsigned Count,unsigned Width=1) {U(B,Count);for(unsigned I=0;I<Count*Width;++I) B.push_back(std::uint8_t(I+1));}
static void Layer(Bytes& B) {Blob(B,2);U(B,4);B.insert(B.end(),{'P','F','X',0});}
static void MeshData(Bytes& B,unsigned Ref) {
    U(B,2);Ids(B);U(B,Ref);U(B,0);U(B,0);U(B,0);
    for(unsigned I=0;I<4;++I) {Fs(B,4);Fs(B,4,0.5f);}
    Fs(B,2);Fs(B,2);for(unsigned I=0;I<4;++I) U(B,I%2);Fs(B,4);U(B,0);
    U(B,2);Ids(B);U(B,Ref);Fs(B,2);Fs(B,2);for(unsigned I=0;I<4;++I) U(B,I%2);Fs(B,4);
    Ids(B);U(B,16);U(B,1);Fs(B,4);
}
static Bytes Fixture(unsigned Ref=1,bool Reverse=false) {
    Bytes B;U(B,0);B.push_back(1);B.push_back(0);U(B,2);
    G(B,Reverse?2:1);MeshData(B,Ref);G(B,Reverse?1:2);MeshData(B,Ref);
    U(B,1);G(B,3);U(B,1);U(B,1);Box(B);F(B,10);U(B,9);Blob(B,1,129);Blob(B,0,129);
    U(B,1);G(B,4);U(B,1);Box(B);U(B,1);U(B,2);U(B,3);Layer(B);U(B,4);U(B,5);U(B,6);U(B,7);
    for(unsigned I=0;I<11;++I) Layer(B);Blob(B,1,12);Blob(B,1,4);
    U(B,1);G(B,5);U(B,1);Fs(B,16);U(B,2);U(B,3);Blob(B,6,2);
    U(B,1);G(B,6);U(B,32);F(B,1);F(B,2);Blob(B,7);U(B,Ref);
    U(B,2);G(B,Reverse?8:7);G(B,Reverse?7:8);return B;
}
static ResolveTexture Resolver(unsigned Valid) {return [Valid](std::int32_t Index,std::string& Object,std::string& Class) {
    if(Index!=std::int32_t(Valid)) return false;Object="/Game/Mods/proof/Own_BuiltData.Atlas";
    Class="/Script/Engine.LightMapTexture2D";return true;
};}
int main(int Argc,char** Argv) {
    Registry R;P4MapKit::FReadError E;const auto B=Fixture();
    assert(Read(B,0,B.size(),Resolver(1),R,E));assert(R.Meshes.size()==2 && R.Textures.size()==1);
    assert(R.LightVolumes==1 && R.VolumetricLightmaps==1 && R.Lights==1 && R.Reflections==1 && R.Skies==2);
    assert(R.Meshes.begin()->second.Light.Textures[0].Object=="/Game/Mods/proof/Own_BuiltData.Atlas");
    assert(R.Meshes.begin()->second.PerInstance.size()==1);
    const auto Canonical=R.Canonical;
    auto Relocated=Fixture(17,true);assert(Read(Relocated,0,Relocated.size(),Resolver(17),R,E));assert(R.Canonical==Canonical);
    // Every truncated span must preserve the previously verified output.
    for(std::size_t N=0;N<B.size();++N) {assert(!Read(B,0,N,Resolver(1),R,E));assert(R.Canonical==Canonical);}
    auto Bad=B;Bad.push_back(0);assert(!Read(Bad,0,Bad.size(),Resolver(1),R,E));
    Bad=B;Bad[4]=3;assert(!Read(Bad,0,Bad.size(),Resolver(1),R,E)); // server stripped
    Bad=B;Bad[5]=1;assert(!Read(Bad,0,Bad.size(),Resolver(1),R,E)); // unknown class strip
    Bad=B;Bad[0]=1;assert(!Read(Bad,0,Bad.size(),Resolver(1),R,E)); // object GUID variant
    assert(!Read(B,0,B.size(),Resolver(9),R,E)); // unknown reference
    Bad=B;Bad[50]=255;Bad[51]=255;Bad[52]=255;Bad[53]=255;
    assert(!Read(Bad,0,Bad.size(),Resolver(1),R,E)); // imported rather than owned texture
    Bad=B; // duplicate mesh GUID after the first entry
    Bytes M;MeshData(M,1);const auto SecondGuid=10+16+M.size();
    Bad[SecondGuid]=1;assert(!Read(Bad,0,Bad.size(),Resolver(1),R,E));
    Bad=B; // Change an actual lighting float while keeping a valid archive.
    const auto FloatAt=10+16+4+4+16+16;
    Bad[FloatAt+2]^=1;assert(Read(Bad,0,Bad.size(),Resolver(1),R,E));assert(R.Canonical!=Canonical);
    Bad=B;Bad[FloatAt]=0;Bad[FloatAt+1]=0;Bad[FloatAt+2]=128;Bad[FloatAt+3]=127;
    assert(!Read(Bad,0,Bad.size(),Resolver(1),R,E)); // nonfinite coefficient
    if(Argc==2) {std::ofstream File(Argv[1],std::ios::binary);File.write(reinterpret_cast<const char*>(B.data()),std::streamsize(B.size()));assert(File.good());}
    std::cout<<"lighting registry typed/order/reference/truncation/content refusals PASS\n";
}

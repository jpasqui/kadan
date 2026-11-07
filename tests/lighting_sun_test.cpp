#include "core/lighting_sun.h"
#include <cassert>
#include <iostream>
using namespace P4MapKit::LightingRegistry;
static void U(Bytes& B,std::uint32_t V) {Detail::Word(B,V);}
static void F(Bytes& B,float V=0) {std::uint32_t N=0;std::memcpy(&N,&V,4);U(B,N);}
static void Fs(Bytes& B,unsigned N) {for(unsigned I=0;I<N;++I) F(B);}
static Guid Id(unsigned N) {return {{N,2,3,4}};}
static void G(Bytes& B,unsigned N) {for(const auto W:Id(N)) U(B,W);}
static void Guids(Bytes& B,unsigned N) {U(B,1);G(B,N);}
static ResolveTexture Resolve=[](std::int32_t Index,std::string& Object,std::string& Class) {
    if(Index!=1) return false;Object="/Game/Mods/Own_BuiltData.Own_BuiltData.Shadow";
    Class="/Script/Engine.ShadowMapTexture2D";return true;
};
struct Built {Bytes Body;std::size_t Start=9,End=0,Channel=0,Matrix=0;};
static Built Fixture(bool DuplicateDirection=false,bool Overlap=false,bool TargetPresent=false) {
    Built V;auto& B=V.Body;B.assign(V.Start,0xab);
    U(B,0);B.push_back(1);B.push_back(0);U(B,1);G(B,77);
    U(B,2);Guids(B,Overlap?91:88);for(unsigned I=0;I<4;++I) U(B,0);Fs(B,32+4);for(unsigned I=0;I<4;++I) U(B,0);Fs(B,4);U(B,0);
    U(B,2);Guids(B,91);U(B,1);Fs(B,4);for(unsigned I=0;I<4;++I) U(B,I==0?1:0);Fs(B,4);
    U(B,2);G(B,91);G(B,TargetPresent?99:92);U(B,0);U(B,0);
    U(B,0);U(B,0);U(B,2);G(B,91);V.Channel=B.size();U(B,0);V.Matrix=B.size();
    for(unsigned I=0;I<16;++I) F(B,I==2?1.0f:0.0f);
    U(B,1);U(B,1);U(B,1);B.push_back(0);B.push_back(60); // half bits 0x3c00
    G(B,93);U(B,1);for(unsigned I=0;I<16;++I) F(B,DuplicateDirection&&I==2?1.0f:0.0f);
    U(B,DuplicateDirection?1:0);U(B,DuplicateDirection?1:0);U(B,DuplicateDirection?1:0);
    if(DuplicateDirection) {B.push_back(0);B.push_back(60);}
    U(B,0);U(B,0);V.End=B.size();G(B,91);B.push_back(0xcd);return V;
}
int main() {
    auto Source=Fixture();Bytes Out{1,2,3};SunAdoption Report;Report.References=123;P4MapKit::FReadError E;
    Registry Before;assert(Read(Source.Body,Source.Start,Source.End,Resolve,Before,E));
    assert(Before.LightRecords.size()==2 && Before.LightRecords.find(Id(91))->second.Depth==std::vector<std::uint16_t>{0x3c00});
    assert(AdoptSun(Source.Body,Source.Start,Source.End,Resolve,Id(99),{{0,0,0}},Out,Report,E));
    assert(Report.BakeGuid==Id(91) && Report.GameGuid==Id(99) && Report.References==3 && Report.ShadowMeshes==1 && !Report.AlreadyAdopted);
    assert(Out.size()==Source.Body.size() && Out.front()==0xab && Out.back()==0xcd);
    assert(std::equal(Out.begin()+Source.End,Out.end(),Source.Body.begin()+Source.End)); // raw GUID outside approved fields stays identical
    Registry After;assert(Read(Out,Source.Start,Source.End,Resolve,After,E));assert(!After.StationaryGuidOffsets.count(Id(91)));
    assert(After.StationaryGuidOffsets.find(Id(99))->second.size()==3 && After.LightRecords.count(Id(99)));
    assert(After.Meshes.begin()->second.Light.Lights.front()==Id(88)); // static-light references unchanged
    const auto Valid=Out;const auto ValidReport=Report;
    assert(AdoptSun(Valid,Source.Start,Source.End,Resolve,Id(99),{{0,0,0}},Out,Report,E));
    assert(Report.AlreadyAdopted && Out==Valid);
    const auto Refusal=[&](const Built& V,const Guid& Target,const std::array<float,3>& Rotation) {
        Out=Valid;Report=ValidReport;assert(!AdoptSun(V.Body,V.Start,V.End,Resolve,Target,Rotation,Out,Report,E));
        assert(Out==Valid && Report.References==ValidReport.References && Report.BakeGuid==ValidReport.BakeGuid);
    };
    Refusal(Fixture(true),Id(99),{{0,0,0}}); // two lights with the same direction
    Refusal(Fixture(false,true),Id(99),{{0,0,0}}); // GUID also identifies a static light
    Refusal(Fixture(false,false,true),Id(99),{{0,0,0}}); // target already elsewhere in registry
    Refusal(Source,Guid{},{{0,0,0}});Refusal(Source,Id(99),{{0,90,0}});
    Refusal(Source,Id(99),{{std::numeric_limits<float>::infinity(),0,0}});
    auto Bad=Source;Bad.Body[Bad.Channel]=1;Refusal(Bad,Id(99),{{0,0,0}});
    Bad=Source;Bad.Body[Bad.Matrix+8]=0;Bad.Body[Bad.Matrix+9]=0;Bad.Body[Bad.Matrix+10]=128;Bad.Body[Bad.Matrix+11]=127;
    Refusal(Bad,Id(99),{{0,0,0}}); // nonfinite matrix
    for(std::size_t End=Source.Start;End<Source.End;++End) {Bad=Source;Bad.End=End;Refusal(Bad,Id(99),{{0,0,0}});}
    std::cout<<"stationary sun exact-field adoption/preservation/refusals PASS\n";
}

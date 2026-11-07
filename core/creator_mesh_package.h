// Explicit latest UE4.26 stock/retoc dispatch for creator mesh inspection.
// A supported storage profile is not creator custody or game compatibility.
#pragma once
#include "physics_body.h"
#include "export_package.h"
#include <map>
namespace P4MapKit { namespace CreatorMeshPackage {
using Bytes=std::vector<std::uint8_t>;using Guid=std::array<std::uint32_t,4>;
struct Header {FPackageInventory Inventory;std::int32_t UE3=0,UE4=0;std::map<Guid,std::int32_t> Versions;bool Retoc=false;};
inline bool BulkContext(const Bytes& Input,std::size_t BodySize,FByteBulkContext& Out,FReadError& Error) {
    FByteReader R(Input,20);std::size_t N=0,Size=0;std::string Text;
    if(!R.Count(N,1000)||!R.Skip(N*20)||!R.Count(Size,Input.size())||!R.String(Text)||!R.Skip(72)||!R.Count(N,100)||!R.Skip(N*8)) {Error=R.Error;return false;}
    for(unsigned I=0;I<2;++I) if(!R.Skip(10)||!R.String(Text)) {Error=R.Error;return false;}
    std::uint32_t Compression=0;if(!R.U32(Compression)||!R.Count(N,0)||Compression || !R.Skip(4)||!R.Count(N,100)) {
        if(R.Error.Code.empty()) R.Reject("creator-mesh-compressed-summary");Error=R.Error;return false;
    }
    for(std::size_t I=0;I<N;++I) if(!R.String(Text)) {Error=R.Error;return false;}
    std::uint64_t Bulk=0;if(!R.Skip(4)||!R.Unsigned(8,Bulk)) {Error=R.Error;return false;}
    if(Size!=Input.size() || Bulk<Input.size() || Bulk>Input.size()+BodySize || Bulk>std::uint64_t(INT64_MAX)) {Error={R.Tell(),"creator-mesh-bulk-start-bound"};return false;}
    FByteBulkContext Pending;Pending.HeaderSize=Input.size();Pending.FileSizes[0]=std::int64_t(BodySize);Pending.HasBulkStart=true;Pending.BulkStart=std::int64_t(Bulk);
    Out=Pending;Error={};return true;
}
inline bool Read(const Bytes& Input,std::size_t BodySize,Header& Out,FReadError& Error) {
    Header Pending;FByteReader R(Input);std::uint32_t Magic=0;std::int32_t Legacy=0,License=0;std::size_t Count=0;
    if(!R.U32(Magic)||!R.I32(Legacy)||!R.I32(Pending.UE3)||!R.I32(Pending.UE4)||!R.I32(License)||!R.Count(Count,1000)) {Error=R.Error;return false;}
    if(Magic!=0x9e2a83c1 || Legacy!=-7 || License || !((Pending.UE3==864&&Pending.UE4==522)||(Pending.UE3==0&&Pending.UE4==0))) {Error={0,"creator-mesh-summary-unqualified"};return false;}
    for(std::size_t I=0;I<Count;++I) {
        Guid Key{};std::int32_t Version=0;for(auto& W:Key) if(!R.U32(W)) {Error=R.Error;return false;}
        if(!R.I32(Version)) {Error=R.Error;return false;}
        if(Version<0 || !Pending.Versions.emplace(Key,Version).second) {Error={R.Tell(),"creator-mesh-custom-version-invalid"};return false;}
    }
    Pending.Retoc=!Count && Pending.UE3==0 && Pending.UE4==0;
    // These native serialization branches are independently retained in stock
    // fixture/header evidence. In particular Release1 disables obsolete Chaos
    // level-set tails; no trailing count may silently be ignored.
    const std::map<Guid,std::int32_t> Required={
        {{0x12f88b9f,0x88754afc,0xa67cd90c,0x383abd29},44}, // Rendering
        {{0x29e575dd,0xe0a34627,0x9d10d276,0x232cdcea},17}, // AnimPhys
        {{0x35f94a83,0xe258406c,0xa31809f5,0x9610247c},40}, // ExternalPhysics
        {{0x601d1886,0xac644f84,0xaa16d3de,0x0deac7d6},43}, // FortniteMain
        {{0x9c54d522,0xa8264fbe,0x94210746,0x61b482d0},38}, // Release
        {{0xcffc743f,0x43b04480,0x939114df,0x171d2073},37}, // Framework
        {{0xe4b068ed,0xf49442e9,0xa231da0b,0x2e46bb41},40}, // Editor
        {{0xe7086368,0x6b234c58,0x84391b70,0x16265e91},1}}; // FortniteRelease
    if(!Pending.Retoc) for(const auto& V:Required) {
        const auto It=Pending.Versions.find(V.first);
        if(It==Pending.Versions.end() || It->second!=V.second) {Error={R.Tell(),"creator-mesh-custom-profile-unqualified"};return false;}
    }
    Bytes Dispatch=Input;std::fill(Dispatch.begin()+8,Dispatch.begin()+16,0);
    Pending.Inventory=P4MapKit::ReadPackageHeader(Dispatch,BodySize);
    if(!Pending.Inventory.Error.Code.empty()) {Error=Pending.Inventory.Error;return false;}
    if(!(Pending.Inventory.Flags&0x80000000u) || (Pending.Inventory.Flags&0x2000u)) {Error={0,"creator-mesh-cook-flags-unqualified"};return false;}
    Pending.Inventory.UE3Version=Pending.UE3;Pending.Inventory.UE4Version=Pending.UE4;Out=std::move(Pending);Error={};return true;
}
inline bool RelabelPhysX(const Bytes& Input,const Bytes& Body,Export::RewrittenPackage& Out,FReadError& Error) {
    Header Original;if(!Read(Input,Body.size(),Original,Error)) return false;
    Bytes Dispatch=Input;std::fill(Dispatch.begin()+8,Dispatch.begin()+16,0);
    Export::RewrittenPackage Pending;std::string Why;
    if(!Export::RelabelPhysXCook(Dispatch,Body,Pending,Why)) {Error={0,Why};return false;}
    std::copy(Input.begin()+8,Input.begin()+16,Pending.Header.begin()+8);
    Header Rewritten;if(!Read(Pending.Header,Pending.Body.size(),Rewritten,Error)) return false;
    if(Rewritten.UE3!=Original.UE3 || Rewritten.UE4!=Original.UE4 || Rewritten.Versions!=Original.Versions || Pending.Body!=Body) {Error={0,"creator-mesh-relabel-preservation"};return false;}
    Out=std::move(Pending);Error={};return true;
}
}}

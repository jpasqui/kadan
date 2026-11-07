// Explicit package-header dispatch for the qualified latest UE4.26 lighting tail.
// The original summary/custom versions remain retained; generic package reader
// support is unchanged. No older/future registry serialization is inferred.
#pragma once
#include "lighting_registry.h"
namespace P4MapKit { namespace LightingRegistry {
struct CustomVersion {Guid Id{};std::int32_t Version=0;};
struct Package {
    FPackageInventory Inventory;
    std::int32_t UE3Version=0,UE4Version=0,LicenseeVersion=0;
    std::vector<CustomVersion> CustomVersions;
    bool RetocLatest=false;
};
inline bool ReadPackageHeader(const Bytes& Header,std::size_t BodySize,Package& Out,FReadError& Error) {
    Package Pending;FByteReader R(Header);std::uint32_t Magic=0;std::int32_t Legacy=0;std::size_t N=0;
    if(!R.U32(Magic)||!R.I32(Legacy)||!R.I32(Pending.UE3Version)||!R.I32(Pending.UE4Version)||
       !R.I32(Pending.LicenseeVersion)||!R.Count(N,1000)) {Error=R.Error;return false;}
    if(Magic!=0x9e2a83c1 || Legacy!=-7 || Pending.LicenseeVersion!=0 ||
       !((Pending.UE3Version==0 && Pending.UE4Version==0) ||
         (Pending.UE3Version==864 && Pending.UE4Version==522))) {
        Error={0,"lighting-package-summary-version-unqualified"};return false;
    }
    const std::map<Guid,std::int32_t> Required={
        {{0x12f88b9f,0x88754afc,0xa67cd90c,0x383abd29},44},
        {{0xb02b49b5,0xbb2044e9,0xa30432b7,0x52e40360},3},
        {{0x6b266cec,0x1ec74b8f,0xa30be4d9,0x0942fc07},1}};
    std::set<Guid> Seen;
    for(std::size_t I=0;I<N;++I) {
        CustomVersion V;for(auto& W:V.Id) if(!R.U32(W)) {Error=R.Error;return false;}
        if(!R.I32(V.Version)) {Error=R.Error;return false;}
        if(V.Version<0 || !Seen.insert(V.Id).second) {Error={R.Tell(),"lighting-invalid-custom-version"};return false;}
        const auto Found=Required.find(V.Id);
        if(Found!=Required.end() && V.Version!=Found->second) {
            Error={R.Tell(),"lighting-custom-version-unqualified"};return false;
        }
        Pending.CustomVersions.push_back(V);
    }
    Pending.RetocLatest=N==0 && Pending.UE3Version==0 && Pending.UE4Version==0;
    if(!Pending.RetocLatest)
        for(const auto& V:Required) if(!Seen.count(V.first)) {Error={R.Tell(),"lighting-custom-version-absent"};return false;}
    // The 522 package records have the same qualified summary/name/import/export
    // layouts. Adapt only their dispatch words after recording/qualifying them;
    // raw source Header remains untouched and its actual hash remains authoritative.
    Bytes Dispatch=Header;std::fill(Dispatch.begin()+8,Dispatch.begin()+16,0);
    Pending.Inventory=P4MapKit::ReadPackageHeader(Dispatch,BodySize);
    if(!Pending.Inventory.Error.Code.empty()) {Error=Pending.Inventory.Error;return false;}
    if(Pending.Inventory.Flags&0x2000u) {Error={0,"lighting-unversioned-properties-unqualified"};return false;}
    Pending.Inventory.UE3Version=Pending.UE3Version;Pending.Inventory.UE4Version=Pending.UE4Version;
    Out=std::move(Pending);Error={};return true;
}
}}

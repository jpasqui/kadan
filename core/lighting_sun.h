// Exact parser-recorded stationary-light GUID transform. Source authority is
// deliberately separate: caller rotation/GUID values are facts, not admission.
#pragma once
#include "lighting_registry.h"

namespace P4MapKit { namespace LightingRegistry {
struct SunAdoption {
    Guid BakeGuid{}, GameGuid{};
    std::size_t References=0, ShadowMeshes=0;
    bool AlreadyAdopted=false;
};
inline bool AdoptSun(const Bytes& Body,std::size_t Start,std::size_t End,const ResolveTexture& Resolve,
    const Guid& GameGuid,const std::array<float,3>& Rotation,Bytes& Out,SunAdoption& Report,FReadError& Error) {
    const auto Refuse=[&](const char* Why) {Error={Start,Why};return false;};
    if(Body.size()>64*1024*1024 || GameGuid==Guid{}) return Refuse("lighting-sun-invalid-target");
    for(const auto F:Rotation) if(!std::isfinite(F)) return Refuse("lighting-sun-nonfinite-rotation");
    Registry Before;if(!Read(Body,Start,End,Resolve,Before,Error)) return false;
    const double Radians=3.14159265358979323846/180.0;
    const double Pitch=double(Rotation[0])*Radians,Yaw=double(Rotation[1])*Radians;
    const std::array<double,3> Want{{std::cos(Pitch)*std::cos(Yaw),std::cos(Pitch)*std::sin(Yaw),std::sin(Pitch)}};
    std::vector<Guid> Found;
    for(const auto& Item:Before.LightRecords) {
        const auto& V=Item.second;if(V.DepthWidth<=0) continue;
        const std::array<double,3> Axis{{V.WorldToLight[2],V.WorldToLight[6],V.WorldToLight[10]}};
        const double Length=std::sqrt(Axis[0]*Axis[0]+Axis[1]*Axis[1]+Axis[2]*Axis[2]);
        if(Length<1e-12 || (Axis==std::array<double,3>{{0,0,1}} && V.WorldToLight[14]==0)) continue;
        const double Dot=(Axis[0]*Want[0]+Axis[1]*Want[1]+Axis[2]*Want[2])/Length;
        if(std::abs(Dot)>0.99) Found.push_back(Item.first);
    }
    if(Found.size()!=1) return Refuse("lighting-sun-direction-not-unique");
    const Guid Old=Found.front();const auto& Light=Before.LightRecords.find(Old)->second;
    if(Old==Guid{}) return Refuse("lighting-sun-null-source-guid");
    if(Light.ShadowChannel!=0) return Refuse("lighting-sun-channel-not-zero");
    const auto References=Before.StationaryGuidOffsets.find(Old);
    if(References==Before.StationaryGuidOffsets.end() || References->second.empty())
        return Refuse("lighting-sun-references-missing");
    SunAdoption Pending;Pending.BakeGuid=Old;Pending.GameGuid=GameGuid;Pending.References=References->second.size();
    for(const auto& Item:Before.Meshes)
        if(Item.second.Shadow.Present && std::find(Item.second.Shadow.Lights.begin(),Item.second.Shadow.Lights.end(),Old)!=Item.second.Shadow.Lights.end())
            ++Pending.ShadowMeshes;
    if(Before.NonStationaryGuids.count(Old)) return Refuse("lighting-sun-guid-domain-overlap");
    if(Old==GameGuid) {Pending.AlreadyAdopted=true;Out=Body;Report=Pending;Error={};return true;}
    if(Before.AllGuids.count(GameGuid)) return Refuse("lighting-sun-target-guid-already-present");
    // The old GUID may simultaneously identify a static light or another domain.
    // The stationary rewrite must not split one identity across two lights.
    Bytes Candidate=Body;
    for(const auto At:References->second) {
        if(At<Start || At>End || 16>End-At) return Refuse("lighting-sun-reference-span-invalid");
        for(unsigned W=0;W<4;++W) for(unsigned B=0;B<4;++B) {
            if(Candidate[At+4*W+B]!=std::uint8_t(Old[W]>>(8*B))) return Refuse("lighting-sun-reference-bytes-differ");
            Candidate[At+4*W+B]=std::uint8_t(GameGuid[W]>>(8*B));
        }
    }
    Registry After;if(!Read(Candidate,Start,End,Resolve,After,Error)) return false;
    const auto Replaced=After.StationaryGuidOffsets.find(GameGuid);
    if(After.StationaryGuidOffsets.count(Old) || Replaced==After.StationaryGuidOffsets.end() ||
       Replaced->second!=References->second || After.LightRecords.count(Old) || !After.LightRecords.count(GameGuid))
        return Refuse("lighting-sun-reparse-reference-mismatch");
    // Reverse only the approved fields and independently parse again. This
    // equality includes full volume/VLM/reflection/sky payloads and float bits.
    Bytes Reversed=Candidate;
    for(const auto At:Replaced->second) for(unsigned W=0;W<4;++W) for(unsigned B=0;B<4;++B)
        Reversed[At+4*W+B]=std::uint8_t(Old[W]>>(8*B));
    Registry Restored;if(Reversed!=Body || !Read(Reversed,Start,End,Resolve,Restored,Error) || Restored.Canonical!=Before.Canonical)
        return Refuse("lighting-sun-unapproved-content-change");
    Out=std::move(Candidate);Report=Pending;Error={};return true;
}
}}

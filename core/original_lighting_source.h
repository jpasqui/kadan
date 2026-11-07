// Source-bound original lighting facts. No scene defaults, fitted lights, world
// transforms, runtime preset selection, or rendering admission are supplied.
#pragma once
#include "lighting_package.h"
#include "property_decoder.h"
#include "level_native.h"
#include "custom_material_hash.h"
#include <set>

namespace P4MapKit { namespace OriginalLighting {
using Bytes=std::vector<std::uint8_t>;
struct PropertyFact { FPropertyEnvelope Envelope; FDecodedValue Value; };
struct ObjectFact {
    std::int32_t Export=0,Outer=0,Template=0;
    std::string Name,Path,Class,TemplatePath;
    std::size_t Offset=0,Size=0,TailOffset=0,TailSize=0;
    std::string TailSha256;
    bool ActorMembershipKnown=false,LiveActor=false,NativeObjectIdOnly=false;
    std::vector<PropertyFact> Properties;
    std::vector<std::string> MissingFields,Unresolved,Dependencies;
};
class PackageFacts {
    std::string Package_,HeaderSha_,BodySha_,SourceSha_;
    std::int32_t UE3Version_=0,UE4Version_=0;
    bool RetocLatest_=false;
    std::vector<ObjectFact> Objects_;
    std::vector<std::int32_t> Actors_;
    std::vector<std::string> Dependencies_;
    friend bool ReadPackageFacts(const std::string&,const Bytes&,const Bytes&,PackageFacts&,FReadError&);
public:
    const std::string& Package() const {return Package_;}
    const std::string& HeaderSha256() const {return HeaderSha_;}
    const std::string& BodySha256() const {return BodySha_;}
    const std::string& SourceDigest() const {return SourceSha_;}
    std::int32_t UE3Version() const {return UE3Version_;}
    std::int32_t UE4Version() const {return UE4Version_;}
    bool RetocLatestSummary() const {return RetocLatest_;}
    const std::vector<ObjectFact>& Objects() const {return Objects_;}
    const std::vector<std::int32_t>& Actors() const {return Actors_;}
    const std::vector<std::string>& Dependencies() const {return Dependencies_;}
    // Stored facts cannot establish absent native defaults, runtime selection,
    // transforms/sockets, or dynamic Rendering/EnvParam curve evaluation.
    bool OriginalLightingAuthority() const {return false;}
    bool Matches(const Bytes& Header,const Bytes& Body) const {
        if(Header.size()>64*1024*1024||Body.size()>64*1024*1024) return false;
        return CustomMaterial::Hex(CustomMaterial::Sha256(Header))==HeaderSha_ &&
               CustomMaterial::Hex(CustomMaterial::Sha256(Body))==BodySha_;
    }
};
namespace Detail {
inline std::string Short(const std::string& Class) {
    const auto At=Class.find_last_of('.');return At==std::string::npos?Class:Class.substr(At+1);
}
inline bool Selected(const std::string& Class) {
    const auto C=Short(Class);
    static const std::set<std::string> Names={"DirectionalLightComponent","SkyLightComponent",
        "PointLightComponent","SpotLightComponent","RectLightComponent","SphereReflectionCaptureComponent",
        "BoxReflectionCaptureComponent","WorldSettings","LightmassImportanceVolume","Level","World"};
    return (Names.count(C)!=0&&Class=="/Script/Engine."+C) ||
        Class=="/Script/Carrot4.MyWorldSettings" || Class=="/Script/Carrot4.EnvController" ||
        Class=="/Script/Carrot4.CarrotSkySphere" ||
        Class=="/Game/Carrot4/Core/Graphics/System/BP_EnvController.BP_EnvController_C" ||
        Class=="/Game/Carrot4/Terrain/Sky/BP_CarrotSkySphere.BP_CarrotSkySphere_C" ||
        (Class.compare(0,16,"/Script/Carrot4.")==0 && C.compare(0,8,"EnvParam")==0);
}
inline std::vector<std::string> Expected(const std::string& Class) {
    const auto C=Short(Class);
    if(C=="WorldSettings"||C=="MyWorldSettings") return {"LightmassSettings"};
    if(C=="BP_EnvController_C"||C=="EnvController") return {"EnvParamSceneDay","EnvParamSceneNight"};
    if(C=="EnvParamScene") return {"EnvParamLighting","EnvParamShadow","EnvParamProjectionShadow","EnvParamSky",
        "EnvParamMPC","EnvParamEffect","EnvParamSkyAtmosphere","EnvParamFog","EnvParamWorldSettings","EnvParamOptimize",
        "EnvParamBloom","EnvParamDOF","EnvParamLensEffect","EnvParamOrimaLight","EnvParamCameraLight","EnvParamCameraDOF","EnvParamDemoLight"};
    if(C=="EnvParamWorldSettings") return {"LightmassSettings","VisibilityCellSize","VisibilityAggressiveness","bPrecomputeVisibility","BackgroundColor"};
    if(C=="EnvParamLighting") return {"DirectionalLightMobility","DirectionalLightIntensity","DirectionalLightColor",
        "bDirectionalLightColorSRGB","DirectionalLightAltitudeAngle","DirectionalLightAzimuthAngle","bIgnoreDirectionLightAngleOffset",
        "DirectionalLightShadowOpacity","DirectionalLightIndirectIntensity","SunFlareOpacity","StaticDirectionalLightIntensity",
        "StaticDirectionalLightColor","StaticDirectionalLightIndirectIntensity","SpotLight","SkyLightSourceType","SkyLightCubemap",
        "SkyLightIntensity","SkyLightColor","SkyLightIndirectIntensity","bEnableLowerHemisphereSolidColor","LowerHemisphereSolidColor",
        "IndirectDiffuseLightingColor","IndirectDiffuseIntensity","IndirectDiffuseVolumetricLightmapIntensity",
        "IndirectSubsurfaceVolumetricLightmapIntensity","bIndirectSpecularDirectionaLightColor","bIndirectSpecularColorUseDiffuse",
        "IndirectSpecularIntensity","AmbientDiffuseColor","ReflectionCaptureDirectionalLightIntensity"};
    if(C=="SphereReflectionCaptureComponent"||C=="BoxReflectionCaptureComponent")
        return {"Brightness","CaptureOffset","RelativeLocation","RelativeRotation","RelativeScale3D","AttachParent","AttachSocketName"};
    if(C=="SkyLightComponent") return {"Intensity","IndirectLightingIntensity","LightColor","Mobility","SourceType",
        "Cubemap","SourceCubemapAngle","bLowerHemisphereIsBlack","LowerHemisphereColor","SkyDistanceThreshold","bCaptureEmissiveOnly"};
    if(C.find("LightComponent")!=std::string::npos) return {"Intensity","IndirectLightingIntensity","LightColor","Mobility",
        "CastShadows","CastStaticShadows","CastDynamicShadows","bAffectsWorld","LightGuid","RelativeLocation",
        "RelativeRotation","RelativeScale3D","AttachParent","AttachSocketName","LightSourceAngle"};
    return {};
}
inline void Dependencies(const FDecodedValue& V,std::set<std::string>& Out,std::size_t Depth=0) {
    if(Depth>64) return;
    if((V.Kind==FDecodedValue::EKind::HardReference&&V.ReferenceIndex) || V.Kind==FDecodedValue::EKind::SoftPath)
        if(!V.Text.empty()) Out.insert(V.Text);
    for(const auto& F:V.Fields) Dependencies(F.second,Out,Depth+1);
    for(const auto& I:V.Items) Dependencies(I,Out,Depth+1);
}
inline bool CanonicalPackage(const std::string& P) {
    if(P.compare(0,6,"/Game/")!=0||P.size()>4096||P.back()=='/'||P.find("..")!=std::string::npos||P.find("//")!=std::string::npos) return false;
    for(unsigned char C:P) if(C<32||C=='\\'||C==':'||C=='.') return false;
    Bytes Encoded;const auto Length=std::uint32_t(P.size()+1);
    for(unsigned I=0;I<4;++I) Encoded.push_back(std::uint8_t(Length>>(8*I)));
    Encoded.insert(Encoded.end(),P.begin(),P.end());Encoded.push_back(0);
    FByteReader Reader(Encoded);std::string Verified;
    return Reader.String(Verified)&&Reader.Tell()==Encoded.size()&&Verified==P;
}
}
// Package identity comes from the installed-user inventory capability, not a
// machine filename. Header/body must be the exact immutable original pair.
inline bool ReadPackageFacts(const std::string& Package,const Bytes& Header,const Bytes& Body,
    PackageFacts& Out,FReadError& Error) {
    Error={};
    const auto Fail=[&](std::size_t At,const std::string& Why){Error={At,Why};return false;};
    if(!Detail::CanonicalPackage(Package)) return Fail(0,"original-lighting-package-identity-invalid");
    if(Header.size()>64*1024*1024||Body.size()>64*1024*1024) return Fail(0,"original-lighting-package-size-limit");
    LightingRegistry::Package P;
    if(!LightingRegistry::ReadPackageHeader(Header,Body.size(),P,Error)) return false;
    auto& I=P.Inventory;
    PackageFacts Result;Result.Package_=Package;
    Result.UE3Version_=P.UE3Version;Result.UE4Version_=P.UE4Version;Result.RetocLatest_=P.RetocLatest;
    Result.HeaderSha_=CustomMaterial::Hex(CustomMaterial::Sha256(Header));
    Result.BodySha_=CustomMaterial::Hex(CustomMaterial::Sha256(Body));
    Bytes Joined=Header;Joined.insert(Joined.end(),Body.begin(),Body.end());
    Result.SourceSha_=CustomMaterial::Hex(CustomMaterial::Sha256(Joined));
    std::set<std::int32_t> Live;bool HasLevel=false;
    std::set<std::string> PackageDependencies;
    FPropertyDecoder Decoder(I,Body);
    for(std::size_t N=0;N<I.Exports.size();++N) {
        const auto& E=I.Exports[N];std::string Class;
        if(!I.Reference(E.Class,Class)) return Fail(E.Offset,"original-lighting-class-reference-invalid");
        if(!Detail::Selected(Class)) continue;
        ObjectFact O;O.Export=std::int32_t(N+1);O.Outer=E.Outer;O.Template=E.Template;
        O.Name=E.Name;O.Class=Class;O.Offset=E.Offset;O.Size=E.Size;
        if(!I.Reference(O.Export,O.Path)) return Fail(E.Offset,"original-lighting-export-reference-invalid");
        if(!I.Reference(E.Template,O.TemplatePath)) return Fail(E.Offset,"original-lighting-template-reference-invalid");
        if(E.Template) O.Unresolved.push_back("serialized-template-inheritance-not-resolved");
        O.Unresolved.push_back("absent-native-class-defaults-not-proved");
        const auto Tags=ReadPropertyEnvelopes(Body,I.Names,E.Offset,E.Offset+E.Size);
        if(!Tags.Error.Code.empty()) {Error=Tags.Error;return false;}
        std::set<std::pair<std::string,std::size_t>> Keys;
        std::set<std::string> Deps;
        Deps.insert(Class);
        if(!O.TemplatePath.empty()) Deps.insert(O.TemplatePath);
        for(const auto& Tag:Tags.Properties) {
            if(!Keys.insert({PropertyIdentity(Tag.Name),Tag.ArrayIndex}).second)
                return Fail(Tag.TagOffset,"original-lighting-duplicate-property");
            PropertyFact F;F.Envelope=Tag;
            if(!Decoder.TypedProperty(Tag,F.Value,0,Detail::Short(Class))) {Error=Decoder.Error;return false;}
            Detail::Dependencies(F.Value,Deps);O.Properties.push_back(std::move(F));
        }
        for(const auto& Name:Detail::Expected(Class)) if(Keys.count({PropertyIdentity(Name),0})==0) O.MissingFields.push_back(Name);
        O.TailOffset=Tags.End;O.TailSize=E.Offset+E.Size-Tags.End;
        const Bytes Tail(Body.begin()+std::ptrdiff_t(Tags.End),Body.begin()+std::ptrdiff_t(E.Offset+E.Size));
        O.TailSha256=CustomMaterial::Hex(CustomMaterial::Sha256(Tail));
        const auto C=Detail::Short(Class);
        if(C=="Level") {
            if(HasLevel) return Fail(E.Offset,"original-lighting-multiple-levels");
            const auto Native=ReadLevelNative(Body,Tags.End,E.Offset+E.Size);
            if(!Native.Error.Code.empty()) {Error=Native.Error;return false;}
            for(auto Ref:Native.Actors) {
                if(Ref<0||std::size_t(Ref)>I.Exports.size()||(Ref>0&&!Live.insert(Ref).second))
                    return Fail(Tags.End,"original-lighting-invalid-level-actor-list");
            }
            Result.Actors_=Native.Actors;HasLevel=true;
        } else if(C=="World") {
            const auto Native=ReadWorldNative(Body,Tags.End,E.Offset+E.Size);
            if(!Native.Error.Code.empty()) {Error=Native.Error;return false;}
            std::vector<std::int32_t> Refs=Native.ExtraReferencedObjects;
            Refs.insert(Refs.end(),Native.StreamingLevels.begin(),Native.StreamingLevels.end());Refs.push_back(Native.PersistentLevel);
            for(auto Ref:Refs) {std::string Path;if(!I.Reference(Ref,Path)) return Fail(Tags.End,"original-lighting-invalid-world-reference");if(Ref) Deps.insert(Path);}
        } else {
            FByteReader R(Body,Tags.End,E.Offset+E.Size);
            O.NativeObjectIdOnly=LevelNative::ObjectId(R)&&R.Tell()==R.Limit();
            if(!O.NativeObjectIdOnly) O.Unresolved.push_back("native-tail-not-decoded");
        }
        O.Dependencies.assign(Deps.begin(),Deps.end());PackageDependencies.insert(Deps.begin(),Deps.end());
        Result.Objects_.push_back(std::move(O));
    }
    for(auto& O:Result.Objects_) {
        O.ActorMembershipKnown=HasLevel;
        O.LiveActor=HasLevel&&(Live.count(O.Export)!=0||(O.Outer>0&&Live.count(O.Outer)!=0));
        if(!HasLevel) O.Unresolved.push_back("level-actor-membership-unavailable");
        O.Unresolved.push_back("runtime-selection-transform-and-rendering-authority-unproved");
    }
    Result.Dependencies_.assign(PackageDependencies.begin(),PackageDependencies.end());
    Out=std::move(Result);return true;
}
}}

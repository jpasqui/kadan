#pragma once
// Narrow storage readback for a newly constructed parameter-free creator MIC.
// A source/cook custody proof and game acceptance are separate requirements.
#include "mod_folders.h"
#include "material_cooked.h"
#include "export_package.h"
#include "custom_material_hash.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
struct FreshOpaqueInstanceCook {
    std::string package,summary_folder,parent,header_sha256,body_sha256;
    std::size_t property_end=0,export_end=0;
};
struct FreshOpaqueInstanceProperties {
    float InheritedClip=0.333333f;
    std::vector<std::string> CachedTextures;
};
inline FreshOpaqueInstanceCook ReadFreshOpaqueInstanceCook(const Bytes& header,const Bytes& body,
    const std::string& expectedPackage,const std::string& expectedParent,const FreshOpaqueInstanceProperties& expected={})
{
    if(!std::isfinite(expected.InheritedClip)||expected.CachedTextures.size()>4096)throw std::runtime_error("fresh-instance-generated-properties-bound");
    if(!P4MapKit::Export::ModFolderPackage(expectedPackage)||expectedParent.compare(0,14,"/Game/Carrot4/")!=0)
        throw std::runtime_error("fresh-instance-package-domain");
    Export::RewrittenPackage checked;std::string storageError;
    if(!Export::RewriteCookedNames(header,body,{},checked,storageError))throw std::runtime_error("fresh-instance-storage: "+storageError);
    const auto package=ReadPackageHeader(header,body.size());
    // Stock Windows cook uses FolderName="None". Namespace custody comes from
    // the actual engine owner/cook path and later container census, not this field.
    if(!package.Error.Code.empty()||(package.Package!="None"&&PropertyIdentity(package.Package)!=PropertyIdentity(expectedPackage))||package.Exports.size()!=1)
        throw std::runtime_error("fresh-instance-owner-identity");
    const auto& record=package.Exports.front();std::string type;
    if(!package.Reference(record.Class,type)||type!="/Script/Engine.MaterialInstanceConstant"||record.Outer!=0||
       PropertyIdentity(record.Name)!=PropertyIdentity(expectedPackage.substr(expectedPackage.find_last_of('/')+1)))
        throw std::runtime_error("fresh-instance-export-class");
    if(record.Offset>body.size()||record.Size>body.size()-record.Offset)throw std::runtime_error("fresh-instance-export-bounds");
    auto at=record.Offset;const auto end=at+record.Size;FDecodedValue properties;FPropertyDecoder decoder(package,body);
    if(!decoder.TypedProperties(at,end,properties))throw std::runtime_error("fresh-instance-properties: "+decoder.Error.Code);
    const auto parent=properties.Field("Parent");
    if(!parent||parent->Kind!=FDecodedValue::EKind::HardReference||parent->ReferenceIndex>=0||
       PropertyIdentity(parent->Text)!=PropertyIdentity(expectedParent+"."+expectedParent.substr(expectedParent.find_last_of('/')+1)))
        throw std::runtime_error("fresh-instance-parent-identity");
    // No base override or parameter list is admitted by this first recipe. A
    // future typed values producer needs its own independent cook/readback.
    for(const auto& field:properties.Fields){
        if(field.first=="Parent")continue;
        if(field.first=="bHasStaticPermutationResource"){
            bool value=true;if(!MaterialBool(field.second,value)||value)throw std::runtime_error("fresh-instance-static-permutation");continue;
        }
        if(field.first=="ScalarParameterValues"||field.first=="VectorParameterValues"||field.first=="TextureParameterValues"||field.first=="FontParameterValues"||field.first=="TextureStreamingData"){
            if(field.second.Kind!=FDecodedValue::EKind::Array||!field.second.Items.empty())throw std::runtime_error("fresh-instance-parameter-list");continue;
        }
        if(field.first=="BasePropertyOverrides"){
            if(field.second.Kind!=FDecodedValue::EKind::Object)throw std::runtime_error("fresh-instance-base-overrides");
            // Stock UpdateOverridableBaseProperties caches inactive inherited
            // values. Exact constructor/cook custody supplies the expectation.
            for(const auto& entry:field.second.Fields){
                if(entry.first.compare(0,10,"bOverride_")==0||entry.first=="TwoSided"||entry.first=="DitheredLODTransition"||entry.first=="bCastDynamicShadowAsMasked"){
                    static const std::set<std::string> flags={"bOverride_OpacityMaskClipValue","bOverride_BlendMode","bOverride_ShadingModel","bOverride_TwoSided","bOverride_DitheredLODTransition","bOverride_CastDynamicShadowAsMasked","TwoSided","DitheredLODTransition","bCastDynamicShadowAsMasked"};
                    bool value=true;if(!flags.count(entry.first)||!MaterialBool(entry.second,value)||value)throw std::runtime_error("fresh-instance-active-base-override");
                }else if(entry.first=="OpacityMaskClipValue"){
                    float value=0;if(!MaterialNumber(entry.second,value)||MaterialFloatBits(value)!=MaterialFloatBits(expected.InheritedClip))throw std::runtime_error("fresh-instance-inherited-clip-mismatch");
                }else if(entry.first=="BlendMode"||entry.first=="ShadingModel"){
                    std::string value;if(!MaterialEnum(entry.second,value)||value!=(entry.first=="BlendMode"?"BLEND_Opaque":"MSM_DefaultLit"))throw std::runtime_error("fresh-instance-inherited-settings-mismatch");
                }else throw std::runtime_error("fresh-instance-base-field-unqualified");
            }
            continue;
        }
        if(field.first=="CachedReferencedTextures"){
            if(field.second.Kind!=FDecodedValue::EKind::Array||field.second.Items.size()!=expected.CachedTextures.size())throw std::runtime_error("fresh-instance-generated-texture-cache");
            for(std::size_t i=0;i<expected.CachedTextures.size();++i){const auto& value=field.second.Items[i];
                if(value.Kind!=FDecodedValue::EKind::HardReference||value.ReferenceIndex>=0||expected.CachedTextures[i].compare(0,14,"/Game/Carrot4/")!=0||PropertyIdentity(value.Text)!=PropertyIdentity(expected.CachedTextures[i]))throw std::runtime_error("fresh-instance-generated-texture-cache-mismatch");
            }
            continue;
        }
        throw std::runtime_error("fresh-instance-unqualified-field: "+field.first);
    }
    // UObject's absent lazy GUID word only. bHasStaticPermutationResource=false
    // skips the entire native platform-dependent shader-map serialization.
    if(end-at!=4||body[at]!=0||body[at+1]!=0||body[at+2]!=0||body[at+3]!=0)
        throw std::runtime_error("fresh-instance-native-tail-unqualified");
    if(!expected.CachedTextures.empty()&&!properties.Field("CachedReferencedTextures"))throw std::runtime_error("fresh-instance-generated-texture-cache-omitted");
    FreshOpaqueInstanceCook result;result.package=expectedPackage;result.summary_folder=package.Package;result.parent=parent->Text;
    result.header_sha256=Hex(Sha256(header));result.body_sha256=Hex(Sha256(body));result.property_end=at;result.export_end=end;return result;
}
} } }

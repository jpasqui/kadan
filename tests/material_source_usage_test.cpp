#include "../core/material_source_data.h"
#include <iostream>
using namespace P4MapKit;
using namespace P4MapKit::Material;
static void Check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static FMaterialCookedRecord Record(const char* package,const char* type){FMaterialCookedRecord out;out.Package=package;out.Class=type;out.Properties.Kind=FDecodedValue::EKind::Object;return out;}
int main(){try{
    auto master=Record("/Game/ActualMaster","Material");
    master.Properties.Fields.push_back({"bUsedWithSkeletalMesh",FDecodedValue::Atom(FDecodedValue::EKind::Boolean,"true")});
    master.Properties.Fields.push_back({"bUsedWithInstancedStaticMeshes",FDecodedValue::Atom(FDecodedValue::EKind::Boolean,"true")});
    master.Properties.Fields.push_back({"bUsedWithParticleSprites",FDecodedValue::Atom(FDecodedValue::EKind::Boolean,"false")});
    master.Properties.Fields.push_back({"bFullyRough",FDecodedValue::Atom(FDecodedValue::EKind::Boolean,"true")});
    FMaterialCookedSettings cooked;FReadError error;Check(ReadMaterialCookedSettings({master},cooked,error),"source settings decode");
    auto settings=MaterialSourceSettings(cooked);const auto& render=settings.At("render_settings");
    Check(render.At("bUsedWithSkeletalMesh").Dump()=="true","actual skeletal usage lost");
    Check(render.At("bUsedWithInstancedStaticMeshes").Dump()=="true","actual instanced usage lost");
    Check(render.At("bFullyRough").Dump()=="true","other renderer setting changed");
    Check(!render.Has("bUsedWithParticleSprites")&&!render.Has("bUsedWithMeshParticles"),"false/absent usage invented");
    Check(cooked.Usage.size()==2,"unrelated usage enabled");
    auto leaf=Record("/Game/ActualLeaf","MaterialInstanceConstant");
    FDecodedValue overrides;overrides.Kind=FDecodedValue::EKind::Object;
    overrides.Fields.push_back({"OpacityMaskClipValue",FDecodedValue::Atom(FDecodedValue::EKind::Number,"0.3333")});
    leaf.Properties.Fields.push_back({"BasePropertyOverrides",overrides});
    leaf.Properties.Fields.push_back({"bUsedWithMeshParticles",FDecodedValue::Atom(FDecodedValue::EKind::Boolean,"true")});
    Check(ReadMaterialCookedSettings({master,leaf},cooked,error),"source parent chain decode");
    Check(!MaterialSourceSettings(cooked).At("render_settings").Has("bUsedWithMeshParticles"),"instance invented a master usage");
    auto invalid=master;invalid.Properties.Fields[0].second=FDecodedValue::Atom(FDecodedValue::EKind::Number,"1");
    Check(!ReadMaterialCookedSettings({invalid},cooked,error)&&error.Code=="material-usage-type","malformed source usage accepted");
    auto empty=Record("/Game/NoUsage","Material");Check(ReadMaterialCookedSettings({empty},cooked,error),"empty source usage decode");
    Check(!MaterialSourceSettings(cooked).Has("render_settings"),"absent source settings invented");
    std::cout<<"10 source-owned material usage checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

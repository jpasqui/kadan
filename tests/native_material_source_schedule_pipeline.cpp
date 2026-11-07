#include "../core/material_source_schedule_pipeline.h"
#include <iostream>
using namespace P4MapKit::Material;
static void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
template<class F>static void Held(F action,const char* name){bool refused=false;try{action();}catch(const Refused&){refused=true;}Check(refused,name);}
int main(){try{
    MaterialRecoveryPlan plan;plan.Lines={"MOV v.xyz, {1,2,2,0}","MOV old.xyz, v","MUL v.xyz, v, {2}.x","MOV unrelated.x, program.buffer[99][65532]","DP3 length.x, v, v","RSQ inverse.x, length.x","MUL normal.xyz, v, inverse.x"};
    MaterialOriginalSourceAnchors source;source.Surface["stop"].line=2;source.Surface["Specular"]=MaterialAnchor::Constant(2,.5);source.Surface["NormalWorld"]=MaterialAnchor::Register(6,"normal","xyz");source.Surface["Normal"]=MaterialAnchor::Register(6,"v","xyz");
    MaterialScheduledAnchor normal;Check(FindOriginalDelayedMaterialNormal(ParseMaterialAnchorInstructions(plan.Lines),source.Surface,normal),"checked schedule");source.Surface["Normal"]=normal.ValueAnchor;source.Scheduled["Normal"]=normal;
    int callbacks=0;plan.ConfigureBinding=[&](SymbolicArena&,BindingContext&){++callbacks;};const auto bound=BindOriginalMaterialSourceOutputs(plan,source,{"Specular","Normal"});
    Check(callbacks==1,"binding callback before single binder");Check(bound.Pins==std::vector<std::string>{"Specular","Normal"}&&bound.Values.size()==2,"ordered held and scheduled pins");Check(bound.Values[0][0].number()==.5&&std::abs(bound.Values[1][0].number()-1./3.)<1e-7,"exact prefix and post-normalizer values");Check(bound.KillCount==0&&bound.AssumedEngineFields.empty(),"unrelated unknown lighting input skipped");
    auto broken=source;broken.Scheduled["Normal"].ScheduledLine=6;Held([&]{BindOriginalMaterialSourceOutputs(plan,broken,{"Normal"});},"scheduled end identity");
    broken=source;broken.Scheduled["Normal"].ValueAnchor.line=1;Held([&]{BindOriginalMaterialSourceOutputs(plan,broken,{"Normal"});},"shared boundary identity");
    broken=source;broken.Scheduled["Normal"].Indices={2,5,4,6};Held([&]{BindOriginalMaterialSourceOutputs(plan,broken,{"Normal"});},"nonmonotonic schedule");
    broken=source;broken.Scheduled["Normal"].Indices={1,4,5,6};Held([&]{BindOriginalMaterialSourceOutputs(plan,broken,{"Normal"});},"schedule before prefix");
    broken=source;broken.Surface["Normal"].register_name="other";Held([&]{BindOriginalMaterialSourceOutputs(plan,broken,{"Normal"});},"source anchor register identity");
    Held([&]{BindOriginalMaterialSourceOutputs(plan,source,{"Normal","Normal"});},"duplicate outputs");Held([&]{BindOriginalMaterialSourceOutputs(plan,source,{});},"empty outputs");
    auto absent=source;absent.Surface.erase("Normal");Held([&]{BindOriginalMaterialSourceOutputs(plan,absent,{"Normal"});},"unassociated schedule");
    auto unsafe=source;unsafe.Scheduled.clear();unsafe.Surface["Normal"]=MaterialAnchor::Register(6,"v","xyz");Held([&]{BindOriginalMaterialSourceOutputs(plan,unsafe,{"Normal"});},"late overwrite remains refused without proof");
    auto invalid=plan;invalid.Settings=Json::MakeArray();Held([&]{BindOriginalMaterialSourceOutputs(invalid,source,{"Normal"});},"typed settings");
    std::cout<<"15 semantic checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

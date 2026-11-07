#include "../core/material_source_anchors.h"
#include <iostream>
using namespace P4MapKit::Material;
static void Check(bool condition,const char* label){if(!condition)throw std::runtime_error(label);}
int main(){try{
    const std::vector<std::string> lines={"MOV v.xyz, {1,2,2,0}","MOV old.xyz, v","MUL v.xyz, v, {2}.x","MOV unrelated.x, program.buffer[99][65532]","DP3 length.x, v, v","RSQ inverse.x, length.x","MUL normal.xyz, v, inverse.x"};
    auto code=ParseMaterialAnchorInstructions(lines);MaterialAnchors found;found["stop"].line=2;found["NormalWorld"]=MaterialAnchor::Register(6,"normal","xyz");found["Normal"]=MaterialAnchor::Register(6,"v","xyz");MaterialScheduledAnchor selected;
    Check(FindOriginalDelayedMaterialNormal(code,found,selected),"normalizer recognized");
    Check(selected.Indices==std::vector<std::size_t>{2,4,5,6}&&selected.ScheduledLine==7&&selected.ValueAnchor.line==2,"exact dependency slice");
    auto arena=std::make_shared<SymbolicArena>();SymbolicMachine prefix(lines,arena);prefix.run(2);const auto values=ReadScheduledMaterialValue(lines,selected,prefix);
    Check(values.size()==3&&std::abs(values[0].number()-1./3.)<1e-7&&std::abs(values[1].number()-2./3.)<1e-7&&std::abs(values[2].number()-2./3.)<1e-7,"newly written normalized value");
    Check(prefix.reg.find("normal")==prefix.reg.end()&&prefix.reg.at("v")[0].number()==1.,"prefix unchanged");
    auto held=found;held["Normal"]=MaterialAnchor::Register(6,"old","xyz");Check(!FindOriginalDelayedMaterialNormal(code,held,selected),"held normal preferred");
    auto arbitrary=code;arbitrary[4]=AnchorInstruction("MUL length.x, v.x, v.x");Check(!FindOriginalDelayedMaterialNormal(arbitrary,found,selected),"requires DP3 normalizer");
    auto modified=code;modified[2]=AnchorInstruction("ADD v.xyz, v, {1}");Check(!FindOriginalDelayedMaterialNormal(modified,found,selected),"unsupported scheduled op refuses");
    auto conditional=code;conditional[2]=AnchorInstruction("MUL v.xyz (NE.x), v, {2}");Check(!FindOriginalDelayedMaterialNormal(conditional,found,selected),"conditional writes refuse");
    auto branch=code;branch[3]=AnchorInstruction("IF CC.x");Check(!FindOriginalDelayedMaterialNormal(branch,found,selected),"branch normalizer refuses");
    auto overwritten=code;overwritten[5]=AnchorInstruction("MOV length.x, {1}");Check(!FindOriginalDelayedMaterialNormal(overwritten,found,selected),"length overwrite invalidates identity");
    auto width=found;width["NormalWorld"].comps="xy";Check(!FindOriginalDelayedMaterialNormal(code,width,selected),"exact xyz required");
    auto early=found;early["stop"].line=7;Check(!FindOriginalDelayedMaterialNormal(code,early,selected),"late-only contract");
    MaterialAnchors stop;stop["stop"].line=3;const std::vector<std::string> markers={"MOV R0, {1}","IF CC.x","IF CC.y","MOV R1, {2}"};
    Check(NormalizeOriginalMaterialAnchorStop(markers,stop).at("stop").line==1&&stop.at("stop").line==3,"consecutive IF marker boundary");
    std::cout<<"13 semantic checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

#include "../core/material_skin_source.h"
#include <iostream>
using namespace P4MapKit::Material;
static unsigned checks=0;static void Check(bool pass,const char* message){++checks;if(!pass)throw EvalError(message);}
int main(){try{
    auto settings=Object({{"domain","MD_Surface"},{"blend_mode","BLEND_Opaque"},{"shading_model","MSM_PreintegratedSkin"}});
    std::vector<std::string> lines={"MOV.F R0.x, {0.4}.x","TXF.F R5.x, R9, handle(R10.x), 2D","MUL.F R6.x, R5.x, {0.5}.x","ADD.F R7.y, R6.x, {0.1}.x","MIN.F R8.w, R0.x, R7.y","MOV.F.SAT R11.z, R8.w","MAX.F R12.z, R11.z, {0.01}.x"};
    MaterialAnchors input;input["stop"].line=1;const auto code=ParseMaterialAnchorInstructions(lines);
    const auto result=RecoverOriginalMaterialSkinAnchors(code,input,settings);
    Check(result.at("AmbientOcclusion").register_name=="R0"&&result.at("AmbientOcclusion").comps=="x"&&result.at("AmbientOcclusion").line==4,"unique held skin AO across scalar fetch arithmetic");
    Check(!result.count("SubsurfaceColor")&&!result.count("Opacity"),"skin engine screen occlusion cannot synthesize inactive material pins");
    auto literal=lines;literal[4]="MIN.F R8.w, {0.75}.x, R7.y";const auto constant=RecoverOriginalMaterialSkinAnchors(ParseMaterialAnchorInstructions(literal),input,settings);
    Check(constant.at("AmbientOcclusion").has_constant&&constant.at("AmbientOcclusion").constant==.75,"exact literal material AO distinct from screen fetch");
    auto control=lines;control.insert(control.begin()+4,{"IF NE.x","ENDIF"});Check(!RecoverOriginalMaterialSkinAnchors(ParseMaterialAnchorInstructions(control),input,settings).count("AmbientOcclusion"),"scalar dependency crossing control markers is unproved");
    auto unknown=lines;unknown[2]="SIN.F R6.x, R5.x";Check(!RecoverOriginalMaterialSkinAnchors(ParseMaterialAnchorInstructions(unknown),input,settings).count("AmbientOcclusion"),"unrecognized fetch carrier remains unavailable");
    auto overwritten=lines;overwritten[3]="MOV.F R7.y, {0.1}.x";Check(!RecoverOriginalMaterialSkinAnchors(ParseMaterialAnchorInstructions(overwritten),input,settings).count("AmbientOcclusion"),"latest scalar definition controls fetch dependency");
    auto duplicate=lines;duplicate.insert(duplicate.end(),lines.begin()+4,lines.end());input["AmbientOcclusion"]=MaterialAnchor::Constant(1,.2);const auto ambiguous=RecoverOriginalMaterialSkinAnchors(ParseMaterialAnchorInstructions(duplicate),input,settings);
    Check(ambiguous.at("AmbientOcclusion").has_constant&&ambiguous.at("AmbientOcclusion").constant==.2,"ambiguous skin carriers preserve independently supplied original anchor");
    settings["blend_mode"]="BLEND_Masked";Check(RecoverOriginalMaterialSkinAnchors(code,input,settings).at("AmbientOcclusion").register_name=="R0","masked skin source contract");
    settings["blend_mode"]="BLEND_Translucent";Check(RecoverOriginalMaterialSkinAnchors(code,input,settings).at("AmbientOcclusion").has_constant,"other skin policies are not admitted by opaque AO rule");
    settings["blend_mode"]="BLEND_Opaque";settings["shading_model"]="MSM_DefaultLit";Check(RecoverOriginalMaterialSkinAnchors(code,input,settings).at("AmbientOcclusion").has_constant,"ordinary shading model does not inherit custom skin rule");
    std::cout<<"native original skin source checks="<<checks<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}

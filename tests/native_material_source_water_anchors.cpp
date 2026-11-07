#include "../core/material_source_water_anchors.h"
#include <iostream>
using namespace P4MapKit::Material;
static void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
template<class F>static void Held(F action,const char* name){bool refused=false;try{action();}catch(const Refused&){refused=true;}catch(const EvalError&){refused=true;}Check(refused,name);}
int main(){try{
    MaterialPreparedProgram program;program.Lines={"MOV B.xyz, {0.3,0.4,0.5,0}","MOV M.x, {0.2}","MOV S.x, {0.5}","MOV A.w, {0.25}.x","MAD d.xyz, -S.x, {0.08}.x, B","MAD spec.xyz, M.x, d, {0.04}.x","MUL opacity.w, A.w, {2}.x","MOV result_color0.w, opacity.w"};
    auto settings=Json::MakeObject();settings["domain"]="MD_Surface";settings["blend_mode"]="BLEND_Translucent";settings["shading_model"]="MSM_DefaultLit";settings["two_sided"]=false;
    const auto source=PrepareOriginalMaterialLateOpacityAnchors(program,settings,"FLocalVertexFactory");
    Check(source.Surface.at("stop").line==4,"lighting boundary retained");Check(source.Scheduled.at("Opacity").Indices==std::vector<std::size_t>{6},"only late opacity dependency");
    auto arena=std::make_shared<SymbolicArena>();SymbolicMachine prefix(program.Lines,arena);prefix.run(4);const auto opacity=ReadScheduledMaterialValue(program.Lines,source.Scheduled.at("Opacity"),prefix);Check(opacity.size()==1&&opacity[0].number()==.5,"exact late alpha value");Check(prefix.reg.count("opacity")==0,"material prefix unchanged");
    auto saturated=program;saturated.Lines.back()="MOV.SAT result_color0.w, opacity.w";Held([&]{PrepareOriginalMaterialLateOpacityAnchors(saturated,settings,"FLocalVertexFactory");},"transformed final output");
    auto conditional=program;conditional.Lines[6]="MUL opacity.w (NE.x), A.w, {2}.x";Held([&]{PrepareOriginalMaterialLateOpacityAnchors(conditional,settings,"FLocalVertexFactory");},"predicated dependency");
    auto duplicate=program;duplicate.Lines.push_back("MOV result_color0.w, {1}.x");Held([&]{PrepareOriginalMaterialLateOpacityAnchors(duplicate,settings,"FLocalVertexFactory");},"unique final output");
    auto transformed=program;transformed.Lines.back()="MOV result_color0.w, -opacity.w";Held([&]{PrepareOriginalMaterialLateOpacityAnchors(transformed,settings,"FLocalVertexFactory");},"unchanged output component");
    auto held=program;held.Lines[6]="MOV unrelated.w, {1}.x";held.Lines.back()="MOV result_color0.w, A.w";const auto ordinary=PrepareOriginalMaterialLateOpacityAnchors(held,settings,"FLocalVertexFactory");Check(ordinary.Scheduled.empty()&&ordinary.Surface.at("Opacity").register_name=="A","ordinary held opacity preserved");
    std::cout<<"9 semantic checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

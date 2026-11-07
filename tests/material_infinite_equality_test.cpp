#include "../core/material_lifter.h"
#include "../core/material_infinite_equality.h"
#include <iostream>
namespace M=P4MapKit::Material;
static int checked=0;
static void Require(bool value){++checked;if(!value)throw std::runtime_error("infinite equality assertion failed");}
int main(){try{
    M::SymbolicArena arena;M::Lifter lift(arena);const auto value=arena.mk("COMP",{arena.mk("SRC",{M::Value("ScalarParameter"),M::Value("Input")}),M::Value(0)});const M::Value plus(std::numeric_limits<double>::infinity()),minus(-std::numeric_limits<double>::infinity());M::Json output;
    Require(M::LiftOriginalInfiniteEquality(lift,"F.SEQ",{value,plus},{M::Value(1.)},{M::Value(0.)},output));
    const auto& node=lift.nodes.At(output.array[0].Text());Require(node.At("type").Text()=="Custom");Require(node.At("props").At("Code").Text()=="return asuint(Value) == 0x7f800000u ? Equal : Unequal;");Require(node.At("props").At("OutputType").Text()=="CMOT_Float1");
    Require(M::LiftOriginalInfiniteEquality(lift,"F.SNE",{minus,value},{M::Value(1.)},{M::Value(0.)},output));Require(lift.nodes.At(output.array[0].Text()).At("props").At("Code").Text()=="return asuint(Value) == 0xff800000u ? Equal : Unequal;");
    for(const auto& kind:{"F.SLT","F.SGT","U.SEQ"})Require(!M::LiftOriginalInfiniteEquality(lift,kind,{value,plus},{M::Value(1.)},{M::Value(0.)},output));
    Require(!M::LiftOriginalInfiniteEquality(lift,"F.SEQ",{value,M::Value(1.)},{M::Value(1.)},{M::Value(0.)},output));
    Require(!M::LiftOriginalInfiniteEquality(lift,"F.SEQ",{value,M::Value(std::numeric_limits<double>::quiet_NaN())},{M::Value(1.)},{M::Value(0.)},output));
    Require(!M::LiftOriginalInfiniteEquality(lift,"F.SEQ",{plus,minus},{M::Value(1.)},{M::Value(0.)},output));
    bool held=false;try{M::LiftOriginalInfiniteEquality(lift,"F.SEQ",{value,plus},{M::Value(1.),M::Value(2.)},{M::Value(0.)},output);}catch(const M::Refused& error){held=error.name=="material infinite equality shape";}Require(held);
    held=false;try{lift.lift({plus});}catch(const M::Refused& error){held=error.name=="nonfinite graph property";}Require(held);
    auto graph=M::Object({{"format",2},{"domain","MD_UI"},{"blend_mode","BLEND_Translucent"},{"shading_model","MSM_Unlit"},{"two_sided",false},{"tangent_space_normal",true},{"clip",.3333},{"nodes",lift.nodes},{"outputs",M::Object({{"Opacity",output}})}});M::ValidateGraph(graph);Require(true);
    std::cout<<checked<<" infinite equality assertions PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

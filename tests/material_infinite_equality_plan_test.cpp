#include "../core/material_infinite_equality_plan.h"
#include <iostream>
namespace M=P4MapKit::Material;
static int checked=0;
static void Require(bool value){++checked;if(!value)throw std::runtime_error("infinite equality plan assertion failed");}
int main(){try{
    M::SymbolicArena arena;const auto input=arena.mk("COMP",{arena.mk("SRC",{M::Value("ScalarParameter"),M::Value("Input")}),M::Value(0)});const M::Value inf(std::numeric_limits<double>::infinity());
    const auto first=arena.select(arena.op("T.NE",{arena.op("F.SEQ",{input,inf})}),M::Value(1.),M::Value(0.));
    const auto second=arena.select(arena.op("T.EQ",{arena.op("F.SEQ",{input,inf})}),M::Value(2.),first);
    M::Lifter ordinary(arena);bool held=false;try{ordinary.lift({second});}catch(const M::Refused& error){held=error.name=="nonfinite graph property";}Require(held);
    M::Lifter lift(arena);Require(M::PrimeOriginalInfiniteEqualityChoices(lift,{second,second})==2);const auto root=lift.lift({second});Require(lift.nodes.At(root.array[0].Text()).At("type").Text()=="Custom");Require(M::PrimeOriginalInfiniteEqualityChoices(lift,{second})==2);
    const auto unsupported=arena.select(arena.op("T.NE",{arena.op("F.SLT",{input,inf})}),M::Value(1.),M::Value(0.));M::Lifter strict(arena);Require(M::PrimeOriginalInfiniteEqualityChoices(strict,{unsupported})==0);held=false;try{strict.lift({unsupported});}catch(const M::Refused& error){held=error.name=="nonfinite graph property";}Require(held);
    bool chained=false;M::MaterialRecoveryPlan plan;plan.NormalSamples=[&](M::Lifter&,const std::vector<M::Value>&){chained=true;return std::set<std::size_t>{42};};const auto facts=M::InstallOriginalInfiniteEqualityPlan(plan);M::Lifter configured(arena);Require(plan.NormalSamples(configured,{second})==std::set<std::size_t>{42});Require(chained);Require(facts->Choices==2);Require(!facts->RendererProven&&!facts->ProductionAdmitted);
    M::Lifter conflict(arena);conflict.memo[M::ValueKey(first)+";"]=M::Array({"forged",""});held=false;try{M::PrimeOriginalInfiniteEqualityChoices(conflict,{first});}catch(const M::Refused& error){held=error.name=="material infinite equality memo";}Require(held);
    const auto input2=arena.mk("COMP",{arena.mk("SRC",{M::Value("ScalarParameter"),M::Value("Other")}),M::Value(0)});
    const auto a=arena.op("TRUNC.U",{arena.op("F.SEQ",{input,inf})}),b=arena.op("TRUNC.U",{arena.op("F.SEQ",{input2,inf})});
    const auto conjunction=arena.select(arena.op("T.NE",{arena.op("I.AND",{a,b})}),M::Value(7.),M::Value(3.));
    M::Lifter composed(arena);Require(M::PrimeOriginalInfiniteEqualityChoices(composed,{conjunction})==2);const auto composedRoot=composed.lift({conjunction});Require(composed.nodes.At(composedRoot.array[0].Text()).At("type").Text()=="Custom");
    Require(composed.nodes.At(composedRoot.array[0].Text()).At("props").At("Code").Text()=="return asuint(Value) == 0x7f800000u ? Equal : Unequal;");
    const auto disjunction=arena.select(arena.op("T.NE",{arena.op("I.OR",{a,b})}),M::Value(7.),M::Value(3.));M::Lifter orLift(arena);Require(M::PrimeOriginalInfiniteEqualityChoices(orLift,{disjunction})==0);held=false;try{orLift.lift({disjunction});}catch(const M::Refused& error){held=error.name=="nonfinite graph property";}Require(held);
    std::cout<<checked<<" infinite equality plan assertions PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

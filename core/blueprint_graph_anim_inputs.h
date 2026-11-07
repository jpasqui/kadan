// Value computations wired into animation graph pins, from their original bytecode.
#pragma once
#include "blueprint_graph.h"
namespace P4MapKit { namespace Blueprint { namespace GraphDetail {
class AnimationInputs : public Function {
    const std::string NodeName;
protected:
    void Feed(std::size_t I,const Wire& To) override {
        if(To.first!="@") { Function::Feed(I,To); return; }
        I=Unalias(I); User="@"; const auto V=Literal(I);
        if(V.Kind!=J::K::Null) { G.Charge(To.second.size()+V.S().size()); Constants[To.second]=V; return; }
        G.Link(Source(I),To);
    }
    std::size_t Assignment(std::size_t I,std::size_t Position) override {
        const auto K=Kids(I); const auto Target=K.at(0);
        if(Op(Target)=="EX_ArrayGetByRef") {
            const auto TK=Kids(Target); const auto Whole=TK.at(0); const auto At=Literal(TK.at(1));
            if(Op(Whole)!="EX_StructMemberContext"||At.Kind==J::K::Null||Var(Kids(Whole).at(0))!=NodeName)
                throw Unsupported("an input function that writes to an array element it does not name plainly");
            const auto Member=N(Whole)["operands"]["property"]["path"].Items.back().S();
            Feed(K.at(1),{"@",Member+"_"+At.S()}); Pins.Items.push_back(Member); return 1;
        }
        if(Op(Target)=="EX_StructMemberContext") {
            const auto Whole=Kids(Target).at(0);
            if(Var(Whole)!=NodeName||Op(Whole)!="EX_InstanceVariable")
                throw Unsupported("an input function that writes to something other than its node");
            const auto Member=N(Target)["operands"]["property"]["path"].Items.back().S();
            Feed(K.at(1),{"@",Member}); Pins.Items.push_back(Member); return 1;
        }
        return Function::Assignment(I,Position);
    }
    std::size_t Statement(std::size_t I,std::size_t Position,bool InitialPush) override {
        const auto CallI=(Op(I)=="EX_Context"||Op(I)=="EX_Context_FailSilent")?Kids(I).at(1):I;
        const auto Path=Str(Optional(Optional(N(CallI)["operands"],"function"),"path"));
        if(Call(Op(CallI))&&Path.size()>=10&&Path.substr(Path.size()-10)==".Array_Set") {
            const auto A=Arguments(CallI);
            if(A.size()<3) throw Error("animation-array-set-arguments");
            const auto Whole=A[0]; const auto At=Literal(A[1]);
            if(Op(Whole)=="EX_StructMemberContext"&&At.Kind!=J::K::Null&&Var(Kids(Whole).at(0))==NodeName) {
                const auto Member=N(Whole)["operands"]["property"]["path"].Items.back().S();
                Feed(A[2],{"@",Member+"_"+At.S()}); Pins.Items.push_back(Member); return 1;
            }
        }
        return Function::Statement(I,Position,InitialPush);
    }
public:
    J Pins=J::Array(),Constants=J::Object();
    AnimationInputs(GraphOwner& Owner,const J& Record,Graph& G,const std::string& Name):Function(Owner,Record,G),NodeName(Name) { Leading=false; }
    AnimationInputs& RunFrom(std::int64_t Offset) {
        if(!Positions.count(Offset)) throw Error("animation-entry-offset");
        auto Position=Positions.at(Offset); int Hops=0;
        while(Op(Roots.at(Position))=="EX_Jump"&&Hops<8) {
            const auto Target=Address(Roots[Position]); if(!Positions.count(Target)) throw Error("animation-jump-offset");
            Position=Positions.at(Target); ++Hops;
        }
        while(Position<Roots.size()) {
            if(Is(Op(Roots[Position]),{"EX_Jump","EX_Return","EX_EndOfScript"})) break;
            Position+=Statement(Roots[Position],Position,false);
        }
        if(!ExecAt.empty()) throw Unsupported("an input function that does more than compute values");
        return *this;
    }
};
} } }

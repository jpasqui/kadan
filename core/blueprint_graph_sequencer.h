// Animation/sequence event endpoints, bound parameters and literal payloads.
#pragma once
#include "blueprint_graph.h"
#include <memory>
namespace P4MapKit { namespace Blueprint {
namespace SequencerDetail {
using namespace GraphDetail;
class Inspection : public Function {
public:
    Inspection(GraphOwner& Owner,const J& Record,Graph& G):Function(Owner,Record,G){}
    using Function::Roots; using Function::N; using Function::Op; using Function::Kids;
    using Function::Var; using Function::Resolve; using Function::Literal; using Function::Params;
};
inline std::string Ref(const J& R) { const auto Outer=Str(Optional(R,"outer")); return (Outer.empty()?"":Outer+".")+R["name"].S(); }
}
inline J BlueprintSequencerEvents(const J& Document,GraphSignatures* Signatures,const J& Root,
    const std::function<J(const std::vector<std::string>&)>& Placed) {
    using namespace GraphDetail;
    std::map<std::string,const J*> ByRef; const auto& Class=Document["class"];
    if(Class.Truth()) ByRef[SequencerDetail::Ref(Class)]=&Class;
    for(const auto& N:Document["functions"].Keys()) ByRef[SequencerDetail::Ref(Document["functions"][N])]=&Document["functions"][N];
    for(const auto& R:Document["objects"].Items) ByRef[SequencerDetail::Ref(R)]=&R;
    const auto RootRef=SequencerDetail::Ref(Root); J Out=J::Array();
    J EmptyReflection=J::Object({{"functions",J::Array()},{"structures",J::Array()},{"enums",J::Array()}}); GraphSignatures Empty(EmptyReflection); std::unique_ptr<GraphOwner> Owner;
    for(const auto& Record:Document["objects"].Items) {
        const auto Held=Optional(Record,"values"); const auto Channel=Optional(Held,"EventChannel"); J Keys=Optional(Channel,"KeyValues");
        if(!Keys.Truth()&&Optional(Held,"Event").Kind==J::K::Object) Keys=J::Array({Held["Event"]}); if(!Keys.Truth()) continue;
        std::vector<std::string> Path; const J* Here=&Record; std::set<std::string> Seen;
        while(Here&&SequencerDetail::Ref(*Here)!=RootRef) { const auto R=SequencerDetail::Ref(*Here); if(!Seen.insert(R).second) throw Error("sequencer-outer-cycle"); Path.insert(Path.begin(),(*Here)["name"].S()); const auto Outer=Str(Optional(*Here,"outer")); const auto Found=ByRef.find(Outer); Here=Outer.empty()||Found==ByRef.end()?nullptr:Found->second; }
        if(!Here) continue;
        for(std::size_t Number=0;Number<Keys.Items.size();++Number) {
            const auto Entry=Leaf(Str(Optional(Optional(Keys[Number],"Ptrs"),"Function"))); const auto& Functions=Document["functions"]; if(!Functions.Has(Entry)||!Functions[Entry]["script"].Truth()) continue;
            if(!Owner) Owner.reset(new GraphOwner(Document,Signatures?*Signatures:Empty)); Graph G(Entry,"function"); SequencerDetail::Inspection Work(*Owner,Functions[Entry],G);
            std::size_t CallI=SIZE_MAX; for(const auto I:Work.Roots) if(Call(Work.Op(I))) { CallI=I; break; }
            J Item=J::Object({{"path",Placed(Path)},{"key",J(std::uint64_t(Number))},{"entry",Entry},{"endpoint",J()},{"bound",J()},{"payload",J::Object()}});
            if(CallI!=SIZE_MAX) { const auto Target=Work.Resolve(CallI,-1); const auto Found=Signatures&&!Target.first.empty()?Signatures->Get(Target.first):J(); Item["endpoint"]=Target.second; std::vector<J> Parameters; if(Found.Kind!=J::K::Null) for(const auto& P:Found[0].Items) if(!P["ret"].B()) Parameters.push_back(P); std::vector<std::size_t> Arguments; for(const auto C:Work.Kids(CallI)) if(Work.Op(C)!="EX_EndFunctionParms") Arguments.push_back(C);
                for(std::size_t I=0;I<std::min(Arguments.size(),Parameters.size());++I) { const auto A=Arguments[I]; const auto& P=Parameters[I]; if(Work.Params.count(Work.Var(A))) Item["bound"]=P["name"]; else { const auto V=Work.Literal(A); if(V.Kind!=J::K::Null) Item["payload"][P["name"].S()]=V; } }
            } Out.Items.push_back(Item);
        }
    }
    auto Number=[](const J& Item){ const auto Entry=Item["entry"].S(); const auto At=Entry.rfind('_'); const auto Tail=At==std::string::npos?Entry:Entry.substr(At+1); if(Tail.empty()||Tail.find_first_not_of("0123456789")!=std::string::npos) return std::uint64_t(0); try { return std::uint64_t(std::stoull(Tail)); } catch(const std::exception&) { throw Error("sequencer-entry-number"); } };
    std::stable_sort(Out.Items.begin(),Out.Items.end(),[&](const J& A,const J& B){return Number(A)<Number(B);}); return Out;
}
} }

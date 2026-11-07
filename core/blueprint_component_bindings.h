// Component event targets come from the cooked binding object. Function names
// can retain a component's old name after a rename, so they are not identities.
#pragma once
#include "blueprint_json.h"
namespace P4MapKit { namespace Blueprint {
inline bool RecoverComponentBindings(const J& Document,J& Plan)
{
    const auto& Class=Document["class"];
    if(!Class["values"].Has("DynamicBindingObjects")) return false;
    const auto& References=Class["values"]["DynamicBindingObjects"];
    if(References.Kind!=J::K::Array||!Plan.Has("graphs")) return false;
    J Changed=Plan["graphs"]; std::set<std::string> Objects,Functions;
    for(const auto& Reference:References.Items)
    {
        if(Reference.Kind!=J::K::String||!Objects.insert(Reference.S()).second) return false;
        const J* Object=nullptr;
        for(const auto& Candidate:Document["objects"].Items)
            if(Candidate["outer"].Kind==J::K::String&&Candidate["outer"].S()+"."+Candidate["name"].S()==Reference.S()) Object=&Candidate;
        if(!Object||(*Object)["cls"].S()!="/Script/Engine.ComponentDelegateBinding"||(*Object)["outer"].S()!=Class["name"].S()) return false;
        const auto& Values=(*Object)["values"];
        if(Values.Kind!=J::K::Object||Values.Fields.size()!=1||!Values.Has("ComponentDelegateBindings")||Values["ComponentDelegateBindings"].Kind!=J::K::Array) return false;
        for(const auto& Binding:Values["ComponentDelegateBindings"].Items)
        {
            if(Binding.Kind!=J::K::Object||Binding.Fields.size()!=3) return false;
            for(const auto* Key:{"ComponentPropertyName","DelegatePropertyName","FunctionNameToBind"})
                if(!Binding.Has(Key)||Binding[Key].Kind!=J::K::String||Binding[Key].S().empty()||Binding[Key].S()=="None") return false;
            const auto Name=Binding["FunctionNameToBind"].S();
            if(!Functions.insert(Name).second||!Document["functions"].Has(Name)) return false;
            J* Event=nullptr;
            for(auto& Graph:Changed.Items) for(auto& Node:Graph["nodes"].Items)
                if(Node.Has("t")&&Node["t"].S()=="bound_event"&&Node.Has("fn")&&Node["fn"].S()==Name)
                { if(Event) return false; Event=&Node; }
            if(!Event) return false;
            (*Event)["component"]=Binding["ComponentPropertyName"];
            (*Event)["delegate_property"]=Binding["DelegatePropertyName"];
        }
    }
    // The compiler creates exactly one binding row per bound-event node. Do
    // not admit a source list that would gain additional inferred bindings.
    for(const auto& Graph:Changed.Items) for(const auto& Node:Graph["nodes"].Items)
        if(Node.Has("t")&&Node["t"].S()=="bound_event"&&(!Node.Has("fn")||!Functions.count(Node["fn"].S()))) return false;
    Plan["graphs"]=std::move(Changed);
    auto& Unhandled=Plan["class_values_not_handled"].Items;
    Unhandled.erase(std::remove_if(Unhandled.begin(),Unhandled.end(),[](const J& Field){return Field.S()=="DynamicBindingObjects";}),Unhandled.end());
    return true;
}
}} // namespace P4MapKit::Blueprint

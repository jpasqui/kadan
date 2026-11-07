// Header-only native planner facade. Canonical plans contain logical asset paths;
// source digests remain cache metadata and are checked again before consumption.
#pragma once
#include "blueprint_builder.h"
#include "blueprint_anim.h"
#include "blueprint_import_identities.h"
#include "blueprint_component_bindings.h"
#include "blueprint_reference_roles.h"

namespace P4MapKit { namespace Blueprint {
inline J BlueprintPlanAdmissionIssues(const J& Plan,const J& Evidence)
{
    J Issues=J::Object();
    for(const auto& Key:std::vector<std::string>{"graphs_skipped","unread","problems","skipped_variables","components_not_placed","class_values_not_handled"})
        if(Plan.Has(Key)&&Plan[Key].Truth()) Issues[Key]=Plan[Key];
    J Inside=J::Array();
    for(const auto& Record:Evidence["inside"].Items)
    {
        if(Record.Kind!=J::K::Array||Record.Items.size()!=3||Record[std::size_t(0)].Kind!=J::K::Array||Record[1].Kind!=J::K::String) throw Error("native-plan-admission-evidence");
        const auto Key=Record[1].S();
        if((Key=="unread"||Key=="problem"||Key=="notes")&&Record[2].Truth()) Inside.Items.push_back(Record);
    }
    if(Inside.Truth()) Issues["inside"]=std::move(Inside);
    return Issues;
}
class NativeBlueprintPlan;
NativeBlueprintPlan BuildNativeBlueprintPlan(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,const std::string& PackageName,GraphSignatures& Signatures,bool WithGraphs);
NativeBlueprintPlan BuildNativeBlueprintPlanWithIdentities(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,const std::string& PackageName,GraphSignatures& Signatures,const FVerifiedImportIdentities& Identities,bool WithGraphs);
class NativeBlueprintPlan
{
    std::string PackageName,Import,Evidence,HeaderDigest,BodyDigest;
    J IndexRecord;
    J Issues;
    FBlueprintReferenceRoleFacts SourceRoles;
    std::size_t HeaderSize,BodySize;
    static std::string Hash(const std::vector<std::uint8_t>& Bytes)
    { return CustomMaterial::Hex(CustomMaterial::Sha256(Bytes)); }
    friend NativeBlueprintPlan BuildNativeBlueprintPlan(const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&,const std::string&,GraphSignatures&,bool);
    friend NativeBlueprintPlan BuildNativeBlueprintPlanWithIdentities(const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&,const std::string&,GraphSignatures&,const FVerifiedImportIdentities&,bool);
    NativeBlueprintPlan(const std::string& Name,const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,const J& Plan,FBlueprintReferenceRoleFacts Roles)
        :PackageName(Name),HeaderDigest(Hash(Header)),BodyDigest(Hash(Body)),SourceRoles(std::move(Roles)),HeaderSize(Header.size()),BodySize(Body.size())
    {
        const auto Parts=SplitPlan(Plan);
        if(Parts["import"]["package"].S()!=Name) throw Error("native-plan-package-mismatch");
        IndexRecord=J::Object({{"package",Plan["package"]},{"kind",Plan["kind"]},{"parent",Plan["parent"]},{"shell",Plan["shell"]},{"needs",Plan["needs"]},{"graphs",Plan.Has("graphs")&&Plan["graphs"].Truth()}});
        Issues=BlueprintPlanAdmissionIssues(Plan,Parts["evidence"]);
        Import=PlanText(Parts["import"]); Evidence=PlanText(Parts["evidence"]);
    }
public:
    const std::string& Package() const { return PackageName; }
    const std::string& ImportText() const { return Import; }
    const std::string& EvidenceText() const { return Evidence; }
    const J& IndexEntry() const { return IndexRecord; }
    const J& AdmissionIssues() const { return Issues; }
    const FBlueprintReferenceRoleFacts& ReferenceRoleFacts() const { return SourceRoles; }
    const std::string& HeaderSha256() const { return HeaderDigest; }
    const std::string& BodySha256() const { return BodyDigest; }
    bool MatchesSource(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body) const
    { return Header.size()==HeaderSize&&Body.size()==BodySize&&Hash(Header)==HeaderDigest&&Hash(Body)==BodyDigest; }
    void RequireSource(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body) const
    { if(!MatchesSource(Header,Body)) throw Error("native-plan-source-changed"); }
};
namespace NativePlanDetail {
// Internal authoring transform. Only the raw-byte factories below may attach
// source facts or construct NativeBlueprintPlan; supplied JSON grants nothing.
inline J BuildDocument(const FPackageInventory& Package,const std::vector<std::uint8_t>& Body,const J& Document,GraphSignatures& Signatures,bool WithGraphs,const FWidgetGeneratedMemberFacts* WidgetMembers=nullptr)
{
    // Signatures read class identity/properties and function declarations only.
    // Keep sequencer/animation stored object values in this request, not the cache.
    J SignatureDocument=J::Object({{"package",Document["package"]},{"kind",Document["kind"]},{"class",Document["class"]},{"functions",Document["functions"]}});
    if(SignatureDocument["class"].Kind==J::K::Object) { SignatureDocument["class"].Fields.erase("values"); SignatureDocument["class"].Order.erase(std::remove(SignatureDocument["class"].Order.begin(),SignatureDocument["class"].Order.end(),"values"),SignatureDocument["class"].Order.end()); }
    Signatures.PutDocument(Package.Package,SignatureDocument); Signatures.Touched.clear();
    PlanBuilder Builder(Package,Body,Document,&Signatures,WidgetMembers); auto Plan=Builder.Build(); if(!Plan.Truth()) throw Error("package-has-no-blueprint-or-level-sequence");
    if(Document["kind"].Kind==J::K::String&&Document["kind"].S()=="AnimBlueprint")
    {
        const J* Default=nullptr; for(const auto& Record:Document["objects"].Items) if(Record["name"].Dump()==Document["class"]["class_default_object"].Dump()) { Default=&Record; break; }
        if(!Default) throw Error("animation-default-object-missing"); const auto Index=(*Default)["index"].U(); if(!Index||Index>Package.Exports.size()) throw Error("animation-default-export-index");
        ValueText Printer(Package,Body); const auto Held=Printer.Values(Package.Exports[std::size_t(Index-1)]); Plan["anim"]=RecoverBlueprintAnimation(Document,Held["held"],&Signatures);
        const auto Reconstructed=AnimDetail::Model(Document).ReconstructedClassFields(Plan);
        auto& Unhandled=Plan["class_values_not_handled"].Items;
        Unhandled.erase(std::remove_if(Unhandled.begin(),Unhandled.end(),[&](const J& Field){return Reconstructed.count(Field.S())!=0;}),Unhandled.end());
    }
    if(WithGraphs&&Plan["functions"].Truth())
    {
        const auto Made=RecoverBlueprintGraphs(Document,Signatures); Plan["graphs"]=Made["graphs"]; Plan["graphs_skipped"]=Made["skipped"];
        GraphDetail::GraphOwner Owner(Document,Signatures);
        for(auto& Variable:Plan["variables"].Items) if(Variable["type"].Has("c")&&Variable["type"]["c"].S()=="mcdelegate")
        {
            const auto Name=Variable["name"].S()+"__DelegateSignature"; if(!Document["functions"].Has(Name)) continue; J Pins=J::Array();
            for(const auto& Parameter:Signatures.OfRecord(Document["functions"][Name]).Items)
            { const bool Out=Parameter["out"].B()&&!Parameter["ref"].B(); if(Parameter["name"].S()=="__WorldContext"||Out) continue; Pins.Items.push_back(J::Object({{"name",Parameter["name"]},{"type",Owner.PinFull(Parameter["prop"])},{"ref",Parameter["ref"]},{"const",Parameter["const"]}})); }
            Variable["inputs"]=Pins; Plan["graphs_skipped"].Fields.erase(Name); Plan["graphs_skipped"].Order.erase(std::remove(Plan["graphs_skipped"].Order.begin(),Plan["graphs_skipped"].Order.end(),Name),Plan["graphs_skipped"].Order.end());
        }
    }
    if(Document["class"].Truth()) RecoverComponentBindings(Document,Plan);
    if(!Package.RecoveredImportReferences.empty()) {
        std::set<std::string> Paths;
        for (const auto& Identity : Package.RecoveredImportReferences)
        {
            const auto& Path = Identity.second;
            const auto Dot = Path.find('.');
            if (Dot == std::string::npos || Path.compare(0, 6, "/Game/") != 0)
                continue;
            const auto Owner = Path.substr(0, Dot);
            if (!ImportIdentityDetail::PackagePath(Owner) ||
                Path.substr(Dot + 1) != Owner.substr(Owner.rfind('/') + 1))
                continue;
            // Only a primary game asset may opt into empty game-reference
            // fallback. Generated classes, CDOs and nested exports retain their
            // typed recovered identities and must resolve through declarations.
            Paths.insert(Path);
        }
        J References=J::Array(); for(const auto& Path:Paths) References.Items.push_back(Path); Plan["reference_only"]=References;
    }
    J Reads=J::Array(); for(const auto& Name:Signatures.Touched) if(Name!=Package.Package) Reads.Items.push_back(Name); Plan["reads"]=Reads; CheckPlanNumbers(Plan); return Plan;
}
} // namespace NativePlanDetail
inline J BuildBlueprintPlanDocument(const FPackageInventory& Package,const std::vector<std::uint8_t>& Body,GraphSignatures& Signatures,bool WithGraphs=true)
{ return NativePlanDetail::BuildDocument(Package,Body,ReadBlueprintDocument(Package,Body),Signatures,WithGraphs); }
inline J BuildBlueprintPlanDocument(const FBlueprintClassSource& Source,const std::vector<std::uint8_t>& Body,GraphSignatures& Signatures,bool WithGraphs=true)
{
    const auto Members=FWidgetGeneratedMemberFacts::Read(Source,Body);
    return NativePlanDetail::BuildDocument(Source.Inventory(),Body,Members.Document(),Signatures,WithGraphs,&Members);
}
inline NativeBlueprintPlan BuildNativeBlueprintPlan(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,const std::string& PackageName,GraphSignatures& Signatures,bool WithGraphs=true)
{
    if(PackageName.empty()||PackageName[0]!='/'||PackageName.find('.')!=std::string::npos||PackageName.find('\\')!=std::string::npos) throw Error("native-plan-logical-package-required");
    const auto Source=FBlueprintClassSource::Read(Header,Body,PackageName);
    auto Roles=ReadBlueprintReferenceRoleFacts(Source,Header,Body);
    const auto Plan=BuildBlueprintPlanDocument(Source,Body,Signatures,WithGraphs);
    return NativeBlueprintPlan(PackageName,Header,Body,Plan,std::move(Roles));
}
inline NativeBlueprintPlan BuildNativeBlueprintPlanWithIdentities(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,const std::string& PackageName,GraphSignatures& Signatures,const FVerifiedImportIdentities& Identities,bool WithGraphs=true)
{
    if(Identities.Package()!=PackageName) throw Error("import-identities-source-package");
    const auto Source=FBlueprintClassSource::ReadWithIdentities(Header,Body,PackageName,Identities);
    auto Roles=ReadBlueprintReferenceRoleFacts(Source,Header,Body);
    const auto Plan=BuildBlueprintPlanDocument(Source,Body,Signatures,WithGraphs);
    return NativeBlueprintPlan(PackageName,Header,Body,Plan,std::move(Roles));
}
inline J NativeBlueprintPlanIndex(const std::map<std::string,J>& Plans)
{
    J Object=J::Object(); for(const auto& Plan:Plans) { if(Plan.first!=Plan.second["package"].S()) throw Error("native-plan-index-package-mismatch"); Object[Plan.first]=Plan.second; }
    const auto Ordered=PlanOrder(Object); J Rows=J::Array(),Graphs=J::Array(),Needs=J::Object(); for(const auto& Name:Ordered.Items) { const auto& Plan=Plans.at(Name.S()); Rows.Items.push_back(J::Object({{"package",Name},{"kind",Plan["kind"]},{"parent",Plan["parent"]},{"shell",Plan["shell"]}})); Needs[Name.S()]=Plan["needs"]; if(Plan.Has("graphs")&&Plan["graphs"].Truth()) Graphs.Items.push_back(Name); }
    std::sort(Graphs.Items.begin(),Graphs.Items.end(),[](const J& A,const J& B){return A.S()<B.S();}); return J::Object({{"format","p4-blueprint-plans"},{"version",1},{"order",Rows},{"with_graphs",Graphs},{"needs",Needs}});
}
inline J NativeBlueprintPlanIndex(const std::map<std::string,NativeBlueprintPlan>& Plans)
{
    std::map<std::string,J> Entries; for(const auto& Plan:Plans) Entries.emplace(Plan.first,Plan.second.IndexEntry());
    return NativeBlueprintPlanIndex(Entries);
}
}} // namespace P4MapKit::Blueprint

// Source-generated UMG fields are compiler products, not authored variables.
// Only a class read from immutable package bytes may supply this witness.
#pragma once
#include "blueprint_deep.h"
#include "blueprint_plan.h"

namespace P4MapKit { namespace Blueprint {
class FWidgetGeneratedMemberFacts
{
    J DocumentValue,ProofValue;
    std::map<std::string,J> Properties;
    static bool Same(const J& A,const J& B)
    {
        if(A.Kind!=B.Kind||A.Bit!=B.Bit||A.Text!=B.Text||A.Items.size()!=B.Items.size()||A.Fields.size()!=B.Fields.size())return false;
        for(std::size_t N=0;N<A.Items.size();++N)if(!Same(A.Items[N],B.Items[N]))return false;
        for(const auto& Item:A.Fields){const auto Found=B.Fields.find(Item.first);if(Found==B.Fields.end()||!Same(Item.second,Found->second))return false;}
        return true;
    }
    static std::string Full(const FPackageInventory& Package,const std::string& Path)
    { return Path.empty()||Path[0]=='/'?Path:Package.Package+'.'+Path; }
    static std::string EditorAnimationName(std::string Name)
    { if(Name.size()>5&&Name.compare(Name.size()-5,5,"_INST")==0)Name.resize(Name.size()-5);return Name; }
    // Supported FName equality subset only: ASCII letters are case-insensitive.
    // Preserve every original spelling in the witness; no Unicode/locale fold.
    static std::string NameKey(std::string Name)
    {for(auto& C:Name)if(C>='A'&&C<='Z')C=char(C-'A'+'a');return Name;}
    explicit FWidgetGeneratedMemberFacts(const FBlueprintClassSource& Source,const std::vector<std::uint8_t>& Body)
        :DocumentValue(ReadBlueprintDocument(Source,Body))
    {
        if(!Source.MatchesBody(Body))throw Error("widget-member-source-body-changed");
        const auto& Package=Source.Inventory();const auto& Class=Source.ClassStructure()["class"];
        if(Source.ClassStructure()["kind"].Kind!=J::K::String||Source.ClassStructure()["kind"].S()!="WidgetBlueprint")return;
        const auto ClassIndex=Class["index"].U();
        if(Class["cls"].S()!="/Script/UMG.WidgetBlueprintGeneratedClass"||!ClassIndex||ClassIndex>Package.Exports.size()||
            Package.Exports[std::size_t(ClassIndex-1)].Outer!=0)throw Error("widget-member-class-owner");
        const auto Reference=[&](std::int32_t Index){std::string Out;if(!Package.Reference(Index,Out))throw Error("widget-member-export-reference");return Out;};
        const auto& ClassValues=DocumentValue["class"]["values"];
        const J* Cdo=nullptr;
        for(const auto& Object:DocumentValue["objects"].Items)
            if(Object["name"].Dump()==Class["class_default_object"].Dump())Cdo=&Object;
        if(!Cdo||(*Cdo)["cls"].S()!=Full(Package,Class["name"].S())||((*Cdo)["flags"].U()&0x10)==0)
            throw Error("widget-member-source-cdo");
        std::int32_t Tree=0;std::set<std::int32_t> Animations;
        for(std::size_t N=0;N<Package.Exports.size();++N)
        {
            const auto& Export=Package.Exports[N];if(Export.Outer!=std::int32_t(ClassIndex))continue;
            const auto Kind=Full(Package,Reference(Export.Class));
            if(Kind=="/Script/UMG.WidgetTree")
            {
                if(Tree||!ClassValues.Has("WidgetTree")||ClassValues["WidgetTree"].Kind!=J::K::String||
                    ClassValues["WidgetTree"].S()!=Reference(std::int32_t(N+1)))throw Error("widget-member-tree-reference");
                Tree=std::int32_t(N+1);
            }
            if(Kind=="/Script/UMG.WidgetAnimation")
            {
                bool Listed=false;
                if(ClassValues.Has("Animations"))for(const auto& Item:ClassValues["Animations"].Items)
                    if(Item.Kind==J::K::String&&Item.S()==Reference(std::int32_t(N+1)))Listed=true;
                if(Listed)Animations.insert(std::int32_t(N+1));
            }
        }
        J TreeObjects=J::Array();
        for(std::size_t N=0;Tree&&N<Package.Exports.size();++N)
        {
            const auto Index=std::int32_t(N+1);auto Owner=Index;std::set<std::int32_t> Seen;J Path=J::Array();bool Owned=false;
            while(Owner>0&&std::size_t(Owner)<=Package.Exports.size())
            {
                if(!Seen.insert(Owner).second||Seen.size()>512)throw Error("widget-member-owner-cycle");
                const auto& Export=Package.Exports[std::size_t(Owner-1)];Path.Items.insert(Path.Items.begin(),Export.Name);
                if(Owner==Tree){Owned=true;break;}if(Owner==std::int32_t(ClassIndex))break;Owner=Export.Outer;
            }
            if(!Owned)continue;const auto& Export=Package.Exports[N];
            TreeObjects.Items.push_back(J::Object({{"path",Path},{"class",Full(Package,Reference(Export.Class))},
                {"source_export",J(std::uint64_t(Index))},{"source_ref",Full(Package,Reference(Index))},
                {"source_outer",Full(Package,Reference(Export.Outer))},{"source_object_flags",PlanFlags(std::uint64_t(Export.Flags))}}));
        }
        J Members=J::Array();std::set<std::string> Names;
        for(const auto& Property:Class["properties"].Items)
        {
            if(Property["type"].S()!="ObjectProperty"||Property["array_dim"].I()!=1||Property["element_size"].I()!=8||
                !Property.Has("class")||Property["class"].Kind!=J::K::String)continue;
            const auto Name=Property["name"].S();std::int32_t Match=0;std::size_t Candidates=0;std::string Role;std::vector<std::string> Path;
            for(std::size_t N=0;N<Package.Exports.size();++N)
            {
                const auto Index=std::int32_t(N+1);const auto& Export=Package.Exports[N];
                const bool Animation=Animations.count(Index)!=0;
                if(NameKey(Animation?EditorAnimationName(Export.Name):Export.Name)!=NameKey(Name))continue;
                std::vector<std::string> Here;std::set<std::int32_t> Seen;auto Owner=Index;bool InTree=false;
                while(Owner>0&&std::size_t(Owner)<=Package.Exports.size())
                {
                    if(!Seen.insert(Owner).second||Seen.size()>512)throw Error("widget-member-owner-cycle");
                    const auto& Object=Package.Exports[std::size_t(Owner-1)];Here.insert(Here.begin(),Object.Name);
                    if(Owner==Tree){InTree=Index!=Tree;break;}
                    if(Owner==std::int32_t(ClassIndex))break;
                    Owner=Object.Outer;
                }
                if(!Animation&&!InTree)continue;
                if(++Candidates>1)throw Error("widget-member-ambiguous-export");
                if(Full(Package,Reference(Export.Class))!=Full(Package,Property["class"].S()))continue;
                Match=Index;Role=Animation?"animation":"tree";Path=Animation?std::vector<std::string>{EditorAnimationName(Export.Name)}:Here;
            }
            if(!Match)continue;
            if(!Names.insert(NameKey(Name)).second)throw Error("widget-member-duplicate-field");
            const auto& Export=Package.Exports[std::size_t(Match-1)];J Placed=J::Array();for(const auto& Part:Path)Placed.Items.push_back(Part);
            J Field=Property;Field["class"]=Full(Package,Property["class"].S());Field["role"]=Role;Field["path"]=Placed;
            Field["source_export"]=J(std::uint64_t(Match));Field["source_ref"]=Full(Package,Reference(Match));
            Field["source_outer"]=Full(Package,Reference(Export.Outer));Field["source_object_name"]=Export.Name;
            Field["source_object_flags"]=PlanFlags(std::uint64_t(Export.Flags));
            Field["cdo_value"]=J();Field["cdo_value_name"]=J();
            for(const auto& Value:(*Cdo)["values"].Fields)if(NameKey(Value.first)==NameKey(Name))
            {
                if(Field["cdo_value_name"].Truth())throw Error("widget-member-cdo-name-ambiguous");
                Field["cdo_value_name"]=Value.first;Field["cdo_value"]=Value.second;
            }
            if(Field["cdo_value"].Kind!=J::K::Null&&Field["cdo_value"].Kind!=J::K::String)
                throw Error("widget-member-cdo-object-reference");
            if(Field["cdo_value"].Kind==J::K::String)Field["cdo_value"]=Full(Package,Field["cdo_value"].S());
            Members.Items.push_back(Field);Properties.emplace(Name,Property);
        }
        ProofValue=J::Object({{"format","widget-generated-members-v1"},{"package",Package.Package},
            {"source_header_sha256",Source.HeaderSha256()},{"source_body_sha256",Source.BodySha256()},
            {"source_class",Full(Package,Class["name"].S())},{"source_class_export",J(ClassIndex)},
            {"source_cdo",Full(Package,Class["class_default_object"].S())},{"tree_objects",TreeObjects},{"members",Members}});
    }
public:
    static FWidgetGeneratedMemberFacts Read(const FBlueprintClassSource& Source,const std::vector<std::uint8_t>& Body)
    {return FWidgetGeneratedMemberFacts(Source,Body);}
    const J& Document()const{return DocumentValue;}
    const J& Proof()const{return ProofValue;}
    bool Covers(const J& Property)const
    {const auto Found=Properties.find(Property["name"].S());return Found!=Properties.end()&&Found->second.Dump()==Property.Dump();}
    bool MatchesClaim(const J& Claim)const{return Same(ProofValue,Claim);}
};
}} // namespace P4MapKit::Blueprint

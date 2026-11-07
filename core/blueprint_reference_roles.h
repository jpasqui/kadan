// Source facts only. This does NOT authorize skipping an import dependency.
// Unsupported bytes/schema/context keep coverage incomplete. Callers must still
// prove current target identity, a native refusal, no destination and policy.
#pragma once
#include "blueprint_converted_header_reservation.h"
#include "blueprint_values.h"
#include "component_tail_relocation.h"

namespace P4MapKit { namespace Blueprint {
enum class ESourceReferenceRole { DeclaredObjectValue, ObjectLiteral, Required, UnqualifiedValue };
struct FSourceReferenceUse
{
    std::string Path, Site, DeclaredClass;
    ESourceReferenceRole Role = ESourceReferenceRole::Required;
};
namespace ReferenceRoleDetail { class Reader; }
class FBlueprintReferenceRoleFacts;
FBlueprintReferenceRoleFacts ReadBlueprintReferenceRoleFacts(const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&);
FBlueprintReferenceRoleFacts ReadBlueprintReferenceRoleFacts(const FBlueprintClassSource&,
    const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&);
FBlueprintReferenceRoleFacts ReadBlueprintReferenceRoleFacts(const FBlueprintClassSource&,
    const FImportIdentityBytes&,const FImportIdentityBytes&,const FVerifiedConvertedHeaderReservation&,
    const FImportIdentityBytes&,const FImportIdentityBytes&);
class FBlueprintReferenceRoleFacts
{
    friend class ReferenceRoleDetail::Reader;
    friend FBlueprintReferenceRoleFacts ReadBlueprintReferenceRoleFacts(const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&);
    FBlueprintReferenceRoleFacts() = default;
    std::string PackageValue, HeaderDigest, BodyDigest, OriginalDigest, MetadataDigest;
    std::vector<FSourceReferenceUse> UsesValue;
    std::vector<std::string> ProblemsValue;
    // Complete byte/reference census. Individual uses can still explicitly be
    // UnqualifiedValue, which always refuses the target-specific query below.
    bool CompleteValue = false;
public:
    FBlueprintReferenceRoleFacts(const FBlueprintReferenceRoleFacts&) = default;
    FBlueprintReferenceRoleFacts(FBlueprintReferenceRoleFacts&&) = default;
    FBlueprintReferenceRoleFacts& operator=(const FBlueprintReferenceRoleFacts&) = default;
    FBlueprintReferenceRoleFacts& operator=(FBlueprintReferenceRoleFacts&&) = default;
    bool Complete() const { return CompleteValue; }
    const std::string& Package() const { return PackageValue; }
    const std::string& HeaderSha256() const { return HeaderDigest; }
    const std::string& BodySha256() const { return BodyDigest; }
    const std::vector<FSourceReferenceUse>& Uses() const { return UsesValue; }
    const std::vector<std::string>& Problems() const { return ProblemsValue; }
    bool RequiresOriginalSource()const{return !OriginalDigest.empty();}
    const std::string& OriginalZenSha256()const{return OriginalDigest;}
    const std::string& OriginalMetadataSha256()const{return MetadataDigest;}
    bool MatchesSource(const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body) const
    {
        return !RequiresOriginalSource()&&HeaderDigest == CustomMaterial::Hex(CustomMaterial::Sha256(Header)) &&
            BodyDigest == CustomMaterial::Hex(CustomMaterial::Sha256(Body));
    }
    // A witness-backed fact cannot be consumed as current using H+B alone.
    bool MatchesSource(const FImportIdentityBytes& Header,const FImportIdentityBytes& Body,
        const FImportIdentityBytes& Original,const FImportIdentityBytes& Metadata)const
    {return RequiresOriginalSource()&&HeaderDigest==CustomMaterial::Hex(CustomMaterial::Sha256(Header))&&
        BodyDigest==CustomMaterial::Hex(CustomMaterial::Sha256(Body))&&OriginalDigest==CustomMaterial::Hex(CustomMaterial::Sha256(Original))&&
        MetadataDigest==CustomMaterial::Hex(CustomMaterial::Sha256(Metadata));}
    bool HasOnlyQualifiedValueUses(const std::string& TargetPackage, const std::string& ExactClass) const
    {
        if (!CompleteValue || !ProblemsValue.empty() || TargetPackage.compare(0,6,"/Game/") ||
            TargetPackage.find('.') != std::string::npos || ExactClass.empty()) return false;
        bool Found = false;
        for (const auto& Use : UsesValue)
            if (Use.Path == TargetPackage || Use.Path.compare(0,TargetPackage.size()+1,TargetPackage+'.') == 0)
            {
                Found = true;
                if ((Use.Role != ESourceReferenceRole::DeclaredObjectValue && Use.Role != ESourceReferenceRole::ObjectLiteral) ||
                    Use.DeclaredClass != ExactClass) return false;
            }
        return Found;
    }
};
namespace ReferenceRoleDetail {
using Bytes = std::vector<std::uint8_t>;
class Reader
{
    friend FBlueprintReferenceRoleFacts P4MapKit::Blueprint::ReadBlueprintReferenceRoleFacts(const FBlueprintClassSource&,
        const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&);
    friend FBlueprintReferenceRoleFacts P4MapKit::Blueprint::ReadBlueprintReferenceRoleFacts(const FBlueprintClassSource&,
        const FImportIdentityBytes&,const FImportIdentityBytes&,const FVerifiedConvertedHeaderReservation&,
        const FImportIdentityBytes&,const FImportIdentityBytes&);
    const Bytes& Header;
    const Bytes& Body;
    FPackageInventory Package;
    const FBlueprintClassSource& Source;
    const FVerifiedConvertedHeaderReservation* Reservation=nullptr;
    const Bytes* Original=nullptr;const Bytes* Metadata=nullptr;
    FBlueprintReferenceRoleFacts Out;
    std::size_t Nodes = 0;
    static const J& Optional(const J& V,const std::string& Key)
    { static const J Empty; return V.Kind==J::K::Object&&V.Has(Key)?V[Key]:Empty; }
    static void Require(bool Good,const std::string& Why) { if(!Good) throw Error("reference-role-"+Why); }
    void Use(const std::string& Path,ESourceReferenceRole Role,const std::string& Site,const std::string& Class="")
    { if(!Path.empty()) { Require(++Nodes<=1000000,"use-budget"); Out.UsesValue.push_back({Path,Site,Class,Role}); } }
    std::string Resolve(std::int32_t Index) const
    { std::string P; Require(Package.Reference(Index,P),"invalid-reference"); return P; }
    std::string ImportClass(std::int32_t Index) const
    {
        if(Index>=0) return "";
        const auto Slot=std::size_t(-std::int64_t(Index)-1);
        Require(Slot<Package.Imports.size(),"import-class-bounds");
        const auto& I=Package.Imports[Slot]; return I.ClassPackage+'.'+I.Class;
    }
    void Index(std::int32_t I,ESourceReferenceRole Role,const std::string& Site,const std::string& Class="")
    { if(I) Use(Resolve(I),Role,Site,Class); }
    void HeaderCoverage()
    {
        const auto Parsed=ReadReferenceHeader(Source,Header,Body,[&](const std::string& Path,const std::string& Site)
            {Use(Path,ESourceReferenceRole::Required,Site);});
        for(const auto& Span:Parsed.Spans) if(!Span.Decoded)
            Require(Reservation&&Span.Offset==Parsed.PreloadEnd&&Span.Offset==Reservation->Begin()&&
                Span.Offset+Span.Size==Header.size()&&Reservation->End()==Header.size(),"uncovered-header-bytes");
    }
    void SchemaUses(const J& Schema,const std::string& Site,std::size_t Depth=0)
    {
        Require(Depth<24,"schema-depth");
        for(const auto& Key:{"class","meta_class","interface","struct","enum","signature"})
        { const auto& V=Optional(Schema,Key); if(V.Kind==J::K::String) Use(V.S(),ESourceReferenceRole::Required,Site+'.'+Key); }
        for(const auto& Key:{"inner","key","value","underlying"})
        { const auto& V=Optional(Schema,Key); if(V.Kind==J::K::Object) SchemaUses(V,Site+'.'+Key,Depth+1); }
    }
    void NativeUses(const J& Record)
    {
        const auto Site=Record["name"].S();
        for(const auto& Key:{"super","class_within","generated_by","class_default_object","event_graph_function"})
        { const auto& V=Optional(Record,Key); if(V.Kind==J::K::String) Use(V.S(),ESourceReferenceRole::Required,Site+'.'+Key); }
        for(const auto& V:Optional(Record,"children").Items) if(V.Kind==J::K::String) Use(V.S(),ESourceReferenceRole::Required,Site+".children");
        for(const auto& V:Optional(Record,"function_map").Fields) if(V.second.Kind==J::K::String) Use(V.second.S(),ESourceReferenceRole::Required,Site+".function_map");
        for(const auto& V:Optional(Record,"interfaces").Items) if(V["cls"].Kind==J::K::String) Use(V["cls"].S(),ESourceReferenceRole::Required,Site+".interfaces");
        for(const auto& V:Optional(Record,"properties").Items) SchemaUses(V,Site+'.'+V["name"].S());
    }
    ESourceReferenceRole LiteralRole(const J& Script,std::size_t I)
    {
        const auto& Ops=Script["instructions"].Items; std::size_t Depth=0;
        auto Parent=Ops[I]["parent"]; auto Role=ESourceReferenceRole::ObjectLiteral;
        while(Parent.Kind!=J::K::Null)
        {
            Require(++Depth<64&&Parent.U()<I,"invalid-opcode-parent"); I=std::size_t(Parent.U());
            const auto Token=Ops[I]["token"].I();
            // A syntactic ObjectConst in a class/struct context cannot authorize
            // a missing object. Other unproven expression contexts also refuse.
            if(Token==0x12||Token==0x13||Token==0x2e||Token==0x2f||Token==0x52||Token==0x54||Token==0x55)
                return ESourceReferenceRole::Required;
            if(Token!=0x04&&Token!=0x1b&&Token!=0x45&&Token!=0x1c&&Token!=0x46&&Token!=0x63&&Token!=0x68)
                Role=ESourceReferenceRole::UnqualifiedValue;
            Parent=Ops[I]["parent"];
        }
        return Role;
    }
    std::size_t ScriptOperand(const J& V,ESourceReferenceRole Role,const std::string& Site,std::size_t Depth=0)
    {
        Require(Depth<64,"operand-depth");
        if(V.Kind==J::K::Object&&V.Has("index")&&V.Has("path"))
        {
            const auto N=V["index"].I(); Require(N>=INT32_MIN&&N<=INT32_MAX,"operand-index");
            const auto I=std::int32_t(N); Require((I?J(Resolve(I)):J()).Dump()==V["path"].Dump(),"operand-identity");
            Index(I,Role,Site,Role==ESourceReferenceRole::ObjectLiteral?ImportClass(I):"");
            return 1;
        }
        if(V.Kind==J::K::Object&&V.Has("owner_index")&&V.Has("owner"))
        {
            const auto N=V["owner_index"].I(); Require(N>=INT32_MIN&&N<=INT32_MAX,"field-index");
            Index(std::int32_t(N),ESourceReferenceRole::Required,Site+".field-owner"); return 1;
        }
        std::size_t Found=0;
        for(const auto& K:V.Fields) Found+=ScriptOperand(K.second,ESourceReferenceRole::Required,Site+'.'+K.first,Depth+1);
        for(const auto& K:V.Items) Found+=ScriptOperand(K,ESourceReferenceRole::Required,Site+"[]",Depth+1);
        return Found;
    }
    void ScriptUses(const J& Function)
    {
        const auto& Script=Optional(Function,"script"); if(Script.Kind==J::K::Null) return;
        std::size_t Found=0;
        for(std::size_t I=0;I<Script["instructions"].Items.size();++I)
        {
            const auto& Op=Script["instructions"].Items[I];
            for(const auto& Operand:Op["operands"].Fields)
                Found+=ScriptOperand(Operand.second,Op["token"].I()==0x20&&Operand.first=="object"?
                    LiteralRole(Script,I):ESourceReferenceRole::Required,
                    Function["name"].S()+'.'+Op["opcode"].S()+'.'+Operand.first);
        }
        Require(Found==Script["references"].Items.size(),"unclassified-script-reference");
    }
    const J* FindSchema(const J& Properties,const std::string& Name)
    {
        const J* Found=nullptr;
        for(const auto& P:Properties.Items) if(P["name"].S()==Name)
        { Require(!Found,"duplicate-declared-property"); Found=&P; }
        return Found;
    }
    static bool PlainObjectFlags(const J& Schema)
    {
        // ExportObject, InstancedReference, ContainsInstancedReference and
        // PersistentInstance are structural ownership, including on a container.
        return (std::stoull(Schema["flags"].S(),nullptr,16)&UINT64_C(0x0002008000080008))==0;
    }
    void Element(const std::string& Kind,ValueReader& R,const J* Schema,const std::string& Site,std::size_t Depth,
        bool ParentQualified=true)
    {
        Require(Depth<24&&++Nodes<=1000000,"value-budget");
        if(Schema) Require((*Schema)["type"].S()==Kind,"stored-schema-type-mismatch");
        if(Kind=="ObjectProperty"||Kind=="InterfaceProperty"||Kind=="ClassProperty")
        {
            const auto I=std::int32_t(R.I(4));
            const auto Class=Schema?Optional(*Schema,"class"):J();
            bool Qualified=ParentQualified&&Schema&&Kind=="ObjectProperty"&&Class.Kind==J::K::String;
            if(Qualified)
            {
                Qualified=PlainObjectFlags(*Schema)&&ImportClass(I)==Class.S();
            }
            Index(I,Qualified?ESourceReferenceRole::DeclaredObjectValue:
                Kind=="ClassProperty"?ESourceReferenceRole::Required:ESourceReferenceRole::UnqualifiedValue,
                Site,Qualified?Class.S():"");
            return;
        }
        static const std::map<std::string,std::size_t> Widths{{"IntProperty",4},{"Int8Property",1},{"Int16Property",2},{"Int64Property",8},
            {"UInt16Property",2},{"UInt32Property",4},{"UInt64Property",8},{"FloatProperty",4},{"DoubleProperty",8},{"ByteProperty",1}};
        const auto At=Widths.find(Kind); if(At!=Widths.end()) { R.Take(At->second); return; }
        if(Kind=="NameProperty") { R.Name(); return; }
        if(Kind=="StrProperty") { R.String(); return; }
        if(Kind=="BoolProperty") { Require(R.U(1)<=1,"noncanonical-bool-element"); return; }
        // Native structs, text, delegates, weak/lazy pointers and unproven soft
        // reference codecs are intentionally not inferred from their byte shape.
        throw Error("reference-role-unsupported-value-kind: "+Kind);
    }
    void Tagged(const FPropertyEnvelope& P,const J* Schema,const std::string& Site)
    {
        if(Schema)
        {
            Require((*Schema)["type"].S()==P.Type,"stored-schema-type-mismatch");
            Require(P.ArrayIndex<(*Schema)["array_dim"].U(),"static-array-index");
        }
        ValueReader R(Body,Package.Names,P.PayloadOffset,P.PayloadOffset+P.Size);
        if(P.Type=="BoolProperty") Require(P.Size==0,"bool-payload");
        else if(P.Type=="ArrayProperty"||P.Type=="SetProperty"||P.Type=="MapProperty")
        {
            Require(Schema,"container-without-source-schema");
            if(P.Type!="ArrayProperty") Require(R.I(4)==0,"container-removals");
            if(P.Type=="MapProperty") Require(P.Detail.size()==2&&P.Detail[0]==(*Schema)["key"]["type"].S()&&
                P.Detail[1]==(*Schema)["value"]["type"].S(),"map-inner-type");
            else Require(P.Detail.size()==1&&P.Detail[0]==(*Schema)["inner"]["type"].S(),"container-inner-type");
            const auto Number=R.Count(100000);
            for(std::size_t N=0;N<Number;++N)
            {
                if(P.Type=="MapProperty")
                {
                    const auto& K=(*Schema)["key"]; const auto& V=(*Schema)["value"];
                    Element(P.Detail[0],R,&K,Site+".key",1,PlainObjectFlags(*Schema));
                    Element(P.Detail[1],R,&V,Site+".value",1,PlainObjectFlags(*Schema));
                }
                else
                {
                    const auto& I=(*Schema)["inner"];
                    Element(P.Detail[0],R,&I,Site+"[]",1,PlainObjectFlags(*Schema));
                }
            }
        }
        else if(P.Type=="EnumProperty")
        {
            // FEnumProperty::SerializeItem serializes one FName for a normal
            // load/save archive, independently of the enum's numeric backing.
            // This is the TAGGED codec only, never an unversioned numeric guess.
            Require(P.Detail.size()==1,"enum-detail");
            if(Schema)
            {
                const auto& Enum=Optional(*Schema,"enum");
                const auto Dot=Enum.Kind==J::K::String?Enum.S().find_last_of('.'):std::string::npos;
                const std::string Expected=Enum.Kind==J::K::String?Enum.S().substr(Dot==std::string::npos?0:Dot+1):"None";
                Require(P.Detail[0]==Expected,"stored-enum-type-mismatch");
            }
            R.Name();
        }
        else if(P.Type=="ByteProperty"&&P.Size==8) { R.Name(); }
        else Element(P.Type,R,Schema,Site,0);
        R.Done();
    }
    void ExportValues(std::size_t I,bool Cdo,bool Code)
    {
        const auto& E=Package.Exports[I];
        const auto Tags=ReadPropertyEnvelopes(Body,Package.Names,E.Offset,E.Offset+E.Size);
        Require(Tags.Error.Code.empty(),"invalid-tags: "+Tags.Error.Code);
        std::set<std::pair<std::string,std::size_t>> Seen;
        for(const auto& P:Tags.Properties)
        {
            Require(Seen.insert({P.Name,P.ArrayIndex}).second,"duplicate-stored-property");
            const J* Schema=Cdo?FindSchema(Source.ClassStructure()["class"]["properties"],P.Name):nullptr;
            const auto Site=E.Name+'.'+P.Name+'['+std::to_string(P.ArrayIndex)+']';
            try {Tagged(P,Schema,Site);}
            catch(const std::bad_alloc&){throw;}
            catch(const std::exception& Why)
            {
                std::string Details;for(const auto& D:P.Detail){if(!Details.empty())Details+=",";Details+=D;}
                std::string Declared=Schema?Schema->Dump():"<not-in-own-source>";
                if(Declared.size()>2048)Declared=Declared.substr(0,2048)+"<truncated>";
                throw Error(std::string(Why.what())+" at "+Site+" tag="+P.Type+" detail="+Details+
                    " payload="+std::to_string(P.PayloadOffset)+"+"+std::to_string(P.Size)+" declared="+Declared);
            }
        }
        if(Code) return; // Exact native ClassReader already consumes the rest.
        const auto End=E.Offset+E.Size;
        // Do not promote a four-zero-byte unknown native Serialize tail. The
        // ordinary BP CDO follows SerializeDefaultObject's tagged contract;
        // exact UObject has no additional native payload. All other classes need
        // the already qualified exact component-tail codec below.
        const bool PlainClass=(Cdo&&Source.ClassStructure()["class"]["cls"].S()=="/Script/Engine.BlueprintGeneratedClass")||
            Resolve(E.Class)=="/Script/CoreUObject.Object";
        bool Plain=PlainClass&&End-Tags.End==4;
        if(Plain) for(auto At=Tags.End;At<End;++At) if(Body[At]) Plain=false;
        if(!Plain)
        {
            const auto Tail=ReadPointerFreeComponentTail(Resolve(E.Class),Body,Tags.End,End);
            Require(Tail.Error.Code.empty()&&Tail.End==End,"unknown-native-tail: "+E.Name);
        }
    }
    Reader(const FBlueprintClassSource& S,const Bytes& H,const Bytes& B,
        const FVerifiedConvertedHeaderReservation* R=nullptr,const Bytes* O=nullptr,const Bytes* M=nullptr)
        :Header(H),Body(B),Source(S),Reservation(R),Original(O),Metadata(M)
    {
        Out.HeaderDigest=CustomMaterial::Hex(CustomMaterial::Sha256(H));
        Out.BodyDigest=CustomMaterial::Hex(CustomMaterial::Sha256(B));
    }
    FBlueprintReferenceRoleFacts Read()
    {
        try
        {
            Require(Source.MatchesSource(Header,Body),"class-source-changed");
            if(Reservation)
            {
                Require(Original&&Metadata&&Reservation->MatchesSource(Header,Body,*Original,*Metadata)&&
                    Reservation->Package()==Source.RawPackage(),"converted-original-source-changed");
                Out.OriginalDigest=Reservation->OriginalZenSha256();Out.MetadataDigest=Reservation->OriginalMetadataSha256();
            }
            Package=Source.Inventory();
            Out.PackageValue=Package.Package; Require(Package.Package.compare(0,6,"/Game/")==0&&Package.Package==Source.RawPackage(),"source-package");
            HeaderCoverage();
            for(std::size_t I=0;I<Package.Imports.size();++I)
                Use(Package.Imports[I].ClassPackage,ESourceReferenceRole::Required,"import["+std::to_string(I+1)+"].class-package");
            const auto& Class=Source.ClassStructure()["class"]; Require(Class.Kind==J::K::Object,"no-generated-class");
            const auto ClassIndex=Class["index"].U(); const auto Cdo=Class["class_default_object"];
            Require(Cdo.Kind==J::K::String,"missing-class-default-object"); bool FoundCdo=false;
            NativeUses(Class);
            for(const auto& F:Source.ClassStructure()["functions"].Fields) { NativeUses(F.second); ScriptUses(F.second); }
            for(std::size_t I=0;I<Package.Exports.size();++I)
            {
                const auto& E=Package.Exports[I]; const auto Site="export["+std::to_string(I+1)+"]";
                Index(E.Class,ESourceReferenceRole::Required,Site+".class"); Index(E.Super,ESourceReferenceRole::Required,Site+".super");
                Index(E.Template,ESourceReferenceRole::Required,Site+".template"); Index(E.Outer,ESourceReferenceRole::Required,Site+".outer");
                const auto Type=Resolve(E.Class);
                const bool IsClass=I+1==ClassIndex,IsFunction=Type=="/Script/CoreUObject.Function";
                if(IsClass) Require(Type=="/Script/Engine.BlueprintGeneratedClass"||Type=="/Script/Engine.AnimBlueprintGeneratedClass"||
                    Type=="/Script/UMG.WidgetBlueprintGeneratedClass","generated-class-identity");
                Require(IsClass||IsFunction||Type.find("BlueprintGeneratedClass")==std::string::npos,"unqualified-native-class-tail");
                const bool IsCdo=Cdo.Kind==J::K::String&&Cdo.S()==Resolve(std::int32_t(I+1))&&E.Class==std::int32_t(ClassIndex);
                Require(IsCdo==((E.Flags&0x10)!=0),"class-default-object-identity");
                if(IsCdo) { Require(!FoundCdo,"duplicate-class-default-object"); FoundCdo=true; }
                ExportValues(I,IsCdo,IsClass||IsFunction);
            }
            Require(FoundCdo,"class-default-object-not-in-source");
            bool Footer=false;
            for(const auto& Span:Package.BodySpans) if(Span.Kind!="export-payload")
            {
                Require(!Footer&&Span.Offset+Span.Size==Body.size()&&Span.Size==4&&Body[Span.Offset]==0xc1&&Body[Span.Offset+1]==0x83&&
                    Body[Span.Offset+2]==0x2a&&Body[Span.Offset+3]==0x9e,"uncovered-body-bytes");
                Footer=true;
            }
            Require(Footer,"missing-package-footer");
            Out.CompleteValue=true;
        }
        catch(const std::bad_alloc&) { throw; }
        catch(const std::exception& E) { Out.ProblemsValue.push_back(E.what()); Out.CompleteValue=false; }
        return std::move(Out);
    }
};
}
inline FBlueprintReferenceRoleFacts ReadBlueprintReferenceRoleFacts(const FBlueprintClassSource& Source,
    const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body)
{ return ReferenceRoleDetail::Reader(Source,Header,Body).Read(); }
inline FBlueprintReferenceRoleFacts ReadBlueprintReferenceRoleFacts(const FBlueprintClassSource& Source,
    const FImportIdentityBytes& Header,const FImportIdentityBytes& Body,const FVerifiedConvertedHeaderReservation& Reservation,
    const FImportIdentityBytes& Original,const FImportIdentityBytes& Metadata)
{ return ReferenceRoleDetail::Reader(Source,Header,Body,&Reservation,&Original,&Metadata).Read(); }
inline FBlueprintReferenceRoleFacts ReadBlueprintReferenceRoleFacts(const std::vector<std::uint8_t>& Header,
    const std::vector<std::uint8_t>& Body)
{
    try
    {
        const auto Source=FBlueprintClassSource::Read(Header,Body);
        return ReadBlueprintReferenceRoleFacts(Source,Header,Body);
    }
    catch(const std::bad_alloc&) { throw; }
    catch(const std::exception& E)
    {
        FBlueprintReferenceRoleFacts Out;
        Out.HeaderDigest=CustomMaterial::Hex(CustomMaterial::Sha256(Header));
        Out.BodyDigest=CustomMaterial::Hex(CustomMaterial::Sha256(Body));
        Out.ProblemsValue.push_back(E.what()); return Out;
    }
}
}} // namespace P4MapKit::Blueprint

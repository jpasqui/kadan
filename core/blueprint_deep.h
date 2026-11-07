// Read-only bpread value projection for native planning. Opaque evidence is
// never authoring text. Legacy comparison mode is explicit and may not be used
// to qualify a struct-map key: the authoring reader uses captured schemas.
#pragma once
#include "blueprint_values.h"
#include "blueprint_class.h"
#include "blueprint_class_source.h"

namespace P4MapKit { namespace Blueprint {
class DeepValues
{
    const FPackageInventory& Package;
    const std::vector<std::uint8_t>& Body;
    bool LegacyComparison;
    std::size_t Nodes=0;
    std::vector<std::string> Owners;
    struct Layout { std::string Format; std::vector<std::string> Members; };
    static const std::map<std::string,Layout>& Layouts()
    {
        static const std::map<std::string,Layout> Values{
            {"Vector",{"fff",{"X","Y","Z"}}},{"Rotator",{"fff",{"Pitch","Yaw","Roll"}}},{"Vector2D",{"ff",{"X","Y"}}},
            {"Vector4",{"ffff",{"X","Y","Z","W"}}},{"Quat",{"ffff",{"X","Y","Z","W"}}},{"LinearColor",{"ffff",{"R","G","B","A"}}},
            {"Color",{"BBBB",{"B","G","R","A"}}},{"IntPoint",{"ii",{"X","Y"}}},{"IntVector",{"iii",{"X","Y","Z"}}},
            {"Guid",{"IIII",{}}},{"Box",{"ffffffB",{}}},{"Box2D",{"ffffB",{}}},{"Plane",{"ffff",{"X","Y","Z","W"}}},
            {"Matrix",{"ffffffffffffffff",{}}},{"FrameNumber",{"i",{}}},{"DateTime",{"q",{}}},{"Timespan",{"q",{}}}};
        return Values;
    }
    static std::size_t Width(char Kind) { return Kind=='B'?1:Kind=='q'?8:4; }
    J Scalar(const std::string& Kind,ValueReader& Reader)
    {
        static const std::map<std::string,int> Types{{"IntProperty",4},{"Int64Property",8},{"UInt32Property",-4},{"UInt64Property",-8},{"Int8Property",1},{"Int16Property",2},{"UInt16Property",-2}};
        const auto Found=Types.find(Kind); if(Found!=Types.end()) return Found->second>0?J(Reader.I(std::size_t(Found->second))):J(Reader.U(std::size_t(-Found->second)));
        if(Kind=="FloatProperty"||Kind=="DoubleProperty") return Floating(Reader.F(Kind=="DoubleProperty"));
        throw Error("not a scalar "+Kind);
    }
    static bool IsScalar(const std::string& Kind)
    { static const std::set<std::string> Types{"IntProperty","Int64Property","UInt32Property","UInt64Property","Int8Property","Int16Property","UInt16Property","FloatProperty","DoubleProperty"}; return Types.count(Kind)!=0; }
    J Reference(ValueReader& Reader)
    { const auto Index=Reader.I(4); std::string Path; if(!Package.Reference(std::int32_t(Index),Path)) throw Error("Invalid object reference"); return Index?J(Path):J(); }
    J Soft(ValueReader& Reader)
    { const auto Asset=Reader.Name(),Sub=Reader.String(); return J::Object({{"asset",Asset},{"sub",Sub}}); }
    static J Enum(const std::vector<std::string>& Names,std::int64_t Value)
    { return Value>=0&&std::uint64_t(Value)<Names.size()?J(Names[std::size_t(Value)]):J(Value); }
    J Channel(ValueReader& Reader)
    {
        const auto Before=Reader.U(1),After=Reader.U(1); const auto Size=Reader.I(4),Count=Reader.I(4);
        if(Size!=4||Count<0||std::uint64_t(Count)>(Reader.End-Reader.At)/4) throw Error("float channel times");
        J Times=J::Array(),Values=J::Array(); for(std::int64_t N=0;N<Count;++N) Times.Items.push_back(J(Reader.I(4)));
        const auto ValueSize=Reader.I(4),Number=Reader.I(4); if(ValueSize!=28||Number!=Count||std::uint64_t(Count)*28+16!=Reader.End-Reader.At) throw Error("float channel values");
        for(std::int64_t N=0;N<Count;++N)
        {
            const auto V=Reader.F(),A=Reader.F(),L=Reader.F(),AW=Reader.F(),LW=Reader.F(); const auto W=Reader.U(1); Reader.Take(3); const auto I=Reader.U(1),T=Reader.U(1),P=Reader.U(1); Reader.Take(1);
            Values.Items.push_back(J::Object({{"Value",Floating(V)},{"Tangent",J::Object({{"ArriveTangent",Floating(A)},{"LeaveTangent",Floating(L)},{"ArriveTangentWeight",Floating(AW)},{"LeaveTangentWeight",Floating(LW)},{"TangentWeightMode",Enum({"RCTWM_WeightedNone","RCTWM_WeightedArrive","RCTWM_WeightedLeave","RCTWM_WeightedBoth"},std::int64_t(W))}})},
                {"InterpMode",Enum({"RCIM_Linear","RCIM_Constant","RCIM_Cubic","RCIM_None"},std::int64_t(I))},{"TangentMode",Enum({"RCTM_Auto","RCTM_User","RCTM_Break","RCTM_None"},std::int64_t(T))},{"PaddingByte",J(P)}}));
        }
        const auto Default=Reader.F(); const auto Has=Reader.I(4),Numerator=Reader.I(4),Denominator=Reader.I(4);
        const std::vector<std::string> Extrap{"RCCE_Cycle","RCCE_CycleWithOffset","RCCE_Oscillate","RCCE_Linear","RCCE_Constant","RCCE_None"};
        return J::Object({{"PreInfinityExtrap",Enum(Extrap,std::int64_t(Before))},{"PostInfinityExtrap",Enum(Extrap,std::int64_t(After))},{"Times",Times},{"Values",Values},{"DefaultValue",Floating(Default)},{"bHasDefaultValue",bool(Has)},{"TickResolution",J::Object({{"Numerator",J(Numerator)},{"Denominator",J(Denominator)}})}});
    }
    J Structure(const std::string& Type,std::size_t Start,std::size_t End,std::size_t Depth)
    {
        ValueReader Reader(Body,Package.Names,Start,End); const auto Found=Layouts().find(Type);
        if(Found!=Layouts().end())
        {
            const auto& Spec=Found->second; std::size_t Size=0; for(const auto C:Spec.Format) Size+=Width(C);
            if(Size==End-Start)
            {
                J Values=Spec.Members.empty()?J::Array():J::Object();
                for(std::size_t N=0;N<Spec.Format.size();++N)
                { const auto C=Spec.Format[N]; const auto Value=C=='f'?Floating(Reader.F()):C=='I'?J(Reader.U(4)):C=='B'?J(Reader.U(1)):J(Reader.I(Width(C))); if(Spec.Members.empty()) Values.Items.push_back(Value); else Values[Spec.Members[N]]=Value; }
                return Values;
            }
        }
        if(Type=="SoftObjectPath"||Type=="SoftClassPath") return Soft(Reader);
        if(Type=="GameplayTag") return J::Object({{"tag",Reader.Name()}});
        if(Type=="GameplayTagContainer") { J Tags=J::Array(); const auto Count=Reader.Count(); for(std::size_t N=0;N<Count;++N) Tags.Items.push_back(Reader.Name()); return J::Object({{"tags",Tags}}); }
        if(Type=="MovieSceneFloatChannel") return Channel(Reader);
        if(Type=="MovieSceneFrameRange")
        { if(End-Start!=10) throw Error("frame range size"); const std::vector<std::string> Bounds{"Exclusive","Inclusive","Open"}; const auto Low=Reader.I(1),LowFrame=Reader.I(4),High=Reader.I(1),HighFrame=Reader.I(4); return J::Object({{"LowerBound",J::Array({Enum(Bounds,Low),J(LowFrame)})},{"UpperBound",J::Array({Enum(Bounds,High),J(HighFrame)})}}); }
        if((Type=="MovieSceneTrackIdentifier"||Type=="MovieSceneSequenceID")&&End-Start==4) return J(Reader.U(4));
        if(Depth>24) throw Error("depth"); Owners.push_back(Type); struct Scope { std::vector<std::string>& Values; ~Scope(){Values.pop_back();} } Guard{Owners};
        auto Value=Properties(Reader,Depth); if(Reader.At!=End) Value["__native_tail"]=CustomMaterial::Hex(std::vector<std::uint8_t>(Body.begin()+Reader.At,Body.begin()+End)); return Value;
    }
    J Element(const std::string& Kind,ValueReader& Reader)
    {
        if(IsScalar(Kind)) return Scalar(Kind,Reader);
        if(Kind=="NameProperty"||Kind=="EnumProperty") return Reader.Name();
        if(Kind=="StrProperty") return Reader.String();
        if(Kind=="ObjectProperty"||Kind=="InterfaceProperty") return Reference(Reader);
        if(Kind=="SoftObjectProperty") return Soft(Reader);
        if(Kind=="ByteProperty") return J(Reader.U(1));
        if(Kind=="BoolProperty") return J(bool(Reader.U(1)));
        throw Error("array of "+Kind);
    }
    J MapElement(const std::string& Kind,ValueReader& Reader,std::size_t Depth,bool GuidKey)
    {
        if(Kind=="StructProperty")
        {
            if(GuidKey) { J Out=J::Array(); for(int N=0;N<4;++N) Out.Items.push_back(J(Reader.U(4))); return Out; }
            const auto Start=Reader.At;
            if(LegacyComparison)
            {
                try { auto Value=Properties(Reader,Depth); bool Errors=false; for(const auto& Field:Value.Fields) if(Field.second.Kind==J::K::Object&&Field.second.Has("error")) Errors=true; if(!Errors) return Value; } catch(const Error&) {}
                Reader.At=Start; J Out=J::Array(); for(int N=0;N<4;++N) Out.Items.push_back(J(Reader.U(4))); return Out;
            }
            return Properties(Reader,Depth);
        }
        if(IsScalar(Kind)||Kind=="NameProperty"||Kind=="EnumProperty"||Kind=="StrProperty"||Kind=="ObjectProperty"||Kind=="SoftObjectProperty") return Element(Kind,Reader);
        throw Error("map of "+Kind);
    }
public:
    DeepValues(const FPackageInventory& InPackage,const std::vector<std::uint8_t>& InBody,bool ComparisonOnly=false):Package(InPackage),Body(InBody),LegacyComparison(ComparisonOnly)
    { if(!Package.Error.Code.empty()) throw Error(Package.Error.Code); }
    std::string Digest(std::size_t Start,std::size_t End) const
    { return CustomMaterial::Hex(CustomMaterial::Sha256(std::vector<std::uint8_t>(Body.begin()+Start,Body.begin()+End))).substr(0,16); }
    J Properties(ValueReader& Reader,std::size_t Depth=0)
    {
        if(Depth>32) throw Error("depth"); J Out=J::Object();
        for(std::size_t N=0;N<100000;++N)
        {
            if(++Nodes>4000000) throw Error("deep value node budget"); const auto Item=Reader.Tag(); if(Item.Name=="None") return Out;
            const auto Key=Item.Name+(Item.ArrayIndex?'['+std::to_string(Item.ArrayIndex)+']':"");
            try { Out[Key]=Value(Item,Depth+1); }
            catch(const Error& Failure)
            { J Detail; if(Item.Type=="BoolProperty") Detail=Item.BoolValue; else if(Item.Detail.size()==1) Detail=Item.Detail[0]; else if(Item.Detail.size()>1) { Detail=J::Array(); for(const auto& Name:Item.Detail) Detail.Items.push_back(Name); }
                Out[Key]=J::Object({{"opaque",Digest(Item.PayloadOffset,Item.PayloadOffset+Item.Size)},{"size",J(std::uint64_t(Item.Size))},{"type",Item.Type},{"detail",Detail},{"error",Failure.what()}}); }
        }
        throw Error("property limit");
    }
    J Value(const FPropertyEnvelope& Item,std::size_t Depth=0)
    {
        ValueReader Reader(Body,Package.Names,Item.PayloadOffset,Item.PayloadOffset+Item.Size); const auto& Kind=Item.Type;
        if(IsScalar(Kind)) return Scalar(Kind,Reader);
        if(Kind=="BoolProperty") return Item.BoolValue;
        if(Kind=="NameProperty"||Kind=="EnumProperty") return Reader.Name();
        if(Kind=="ByteProperty") return Item.Size==1?J(Reader.U(1)):J(Reader.Name());
        if(Kind=="ObjectProperty"||Kind=="InterfaceProperty") return Reference(Reader);
        if(Kind=="SoftObjectProperty") return Soft(Reader);
        if(Kind=="StrProperty") return Reader.String();
        if(Kind=="TextProperty") return J::Object({{"text",Digest(Reader.At,Reader.End)}});
        if(Kind=="FieldPathProperty") { J Path=J::Array(); const auto Count=Reader.Count(64); for(std::size_t N=0;N<Count;++N) Path.Items.push_back(Reader.Name()); return J::Object({{"field",Path},{"owner",Reference(Reader)}}); }
        if(Kind=="StructProperty") return Structure(Item.Detail.at(0),Reader.At,Reader.End,Depth);
        if(Kind=="ArrayProperty"||Kind=="SetProperty")
        {
            if(Kind=="SetProperty"&&Reader.Count()) throw Error("set removals"); const auto Count=Reader.Count(1000000); J Out=J::Array(); const auto& Inner=Item.Detail.at(0);
            if(Inner=="StructProperty")
            {
                const auto Tag=Reader.Tag(); if(Tag.Name=="None"||Tag.Type!="StructProperty") throw Error("array of structs without its inner tag"); const auto& Type=Tag.Detail.at(0); const auto Found=Layouts().find(Type); std::size_t Size=0;
                if(Found!=Layouts().end()) for(const auto C:Found->second.Format) Size+=Width(C);
                if(Size&&Size*Count==Tag.Size) { for(std::size_t N=0;N<Count;++N) Out.Items.push_back(Structure(Type,Tag.PayloadOffset+N*Size,Tag.PayloadOffset+(N+1)*Size,Depth)); return Out; }
                ValueReader InnerReader(Body,Package.Names,Tag.PayloadOffset,Tag.PayloadOffset+Tag.Size); for(std::size_t N=0;N<Count;++N) Out.Items.push_back(Properties(InnerReader,Depth)); return Out;
            }
            for(std::size_t N=0;N<Count;++N) Out.Items.push_back(Element(Inner,Reader)); return Out;
        }
        if(Kind=="MapProperty")
        {
            if(Reader.I(4)==0)
            {
                try
                {
                    const bool GuidKey=Item.Name=="BindingIdToReferences"&&!Owners.empty()&&Owners.back()=="LevelSequenceBindingReferences";
                    J Pairs=J::Array(); const auto Count=Reader.Count(1000000);
                    for(std::size_t N=0;N<Count;++N) { const auto Key=MapElement(Item.Detail.at(0),Reader,Depth,GuidKey); const auto Value=MapElement(Item.Detail.at(1),Reader,Depth,false); Pairs.Items.push_back(J::Array({Key,Value})); }
                    if(Reader.At==Reader.End) { if(!LegacyComparison) { for(const auto& Pair:Pairs.Items) if(Pair.Items[0].Kind==J::K::Object&&Pair.Items[0].Has("error")) throw Error("unqualified map key"); } return J::Object({{"map",Pairs}}); }
                }
                catch(const Error&) {}
            }
            return J::Object({{"opaque",Digest(Item.PayloadOffset,Item.PayloadOffset+Item.Size)},{"size",J(std::uint64_t(Item.Size))},{"map",J::Array({Item.Detail.at(0),Item.Detail.at(1)})}});
        }
        return J::Object({{"opaque",Digest(Item.PayloadOffset,Item.PayloadOffset+Item.Size)},{"size",J(std::uint64_t(Item.Size))},{"type",Kind}});
    }
    J Export(const FPackageExport& Export)
    { ValueReader Reader(Body,Package.Names,Export.Offset,Export.Offset+Export.Size); return Properties(Reader); }
};
namespace DeepReadDetail {
// Internal hydration only; source-bound plan/facts entry points never accept a
// caller document. Their immutable FBlueprintClassSource owns this actual read.
inline J Hydrate(const FPackageInventory& Package,const std::vector<std::uint8_t>& Body,J Doc,bool ComparisonOnly)
{
    DeepValues Deep(Package,Body,ComparisonOnly);
    if(Doc["class"].Kind==J::K::Object) Doc["class"]["values"]=Deep.Export(Package.Exports[std::size_t(Doc["class"]["index"].U()-1)]);
    for(auto& Object:Doc["objects"].Items)
    {
        const auto& Export=Package.Exports[std::size_t(Object["index"].U()-1)]; ValueReader Reader(Body,Package.Names,Export.Offset,Export.Offset+Export.Size); Object["values"]=Deep.Properties(Reader);
        if(!(Reader.End-Reader.At==4&&Body[Reader.At]==0&&Body[Reader.At+1]==0&&Body[Reader.At+2]==0&&Body[Reader.At+3]==0))
        {
            const auto Size=Reader.End-Reader.At;
            Object["tail"]=Size<=64?CustomMaterial::Hex(std::vector<std::uint8_t>(Body.begin()+Reader.At,Body.begin()+Reader.End)):std::to_string(Size)+" bytes";
        }
    }
    return Doc;
}
}
inline J ReadBlueprintDocument(const FPackageInventory& Package,const std::vector<std::uint8_t>& Body,bool ComparisonOnly=false)
{ return DeepReadDetail::Hydrate(Package,Body,ReadClassStructure(Package,Body),ComparisonOnly); }
inline J ReadBlueprintDocument(const FBlueprintClassSource& Source,const std::vector<std::uint8_t>& Body,bool ComparisonOnly=false)
{
    if(!Source.MatchesBody(Body)) throw Error("native-class-source-body-changed");
    return DeepReadDetail::Hydrate(Source.Inventory(),Body,Source.ClassStructure(),ComparisonOnly);
}
}} // namespace P4MapKit::Blueprint

// Source-bound package adapter for actual native post-process values.
#pragma once
#include "postprocess_cook.h"
#include "cooked_export_rebuild.h"
namespace P4MapKit { namespace PostProcessCook {

inline bool TranslateClass(std::vector<std::uint8_t>& Header, std::vector<std::uint8_t>& Body, std::string& Error)
{
    const auto Before = P4MapKit::ReadPackage(Header, Body);
    if (!Before.Error.Code.empty()) { Error = Before.Error.Code; return false; }
    const std::string Storage = "P4RecoveredPostProcessComponent";
    const std::string Native = "PostProcessComponent";
    std::int32_t EnginePackage = 0;
    bool Found = false;
    for (std::size_t Index = 0; Index < Before.Imports.size(); ++Index)
    {
        const auto& Import = Before.Imports[Index];
        if (!Import.Outer && Import.Name == "/Script/Engine" && Import.Class == "Package") EnginePackage = -std::int32_t(Index) - 1;
        if (Import.Name == Storage || Import.Name == "Default__" + Storage)
        {
            std::string Outer;
            if (Import.Outer >= 0 || !Before.Reference(Import.Outer, Outer) || Outer != "/Script/P4StubsCore")
            { Error = "postprocess-cook-recovered-class-namespace-differs"; return false; }
            Found = true;
        }
        if (Import.Class == Storage)
        {
            if (Import.ClassPackage != "/Script/P4StubsCore")
            { Error = "postprocess-cook-recovered-class-package-differs"; return false; }
            Found = true;
        }
    }
    if (!Found) { Error.clear(); return true; }
    if (!EnginePackage) { Error = "postprocess-cook-native-engine-package-missing"; return false; }
    std::map<std::string, std::string> Rename;
    std::map<std::uint32_t, std::uint32_t> NameSlots;
    for (const auto& Pair : std::map<std::string, std::string>{{Storage, Native}, {"Default__" + Storage, "Default__" + Native}})
    {
        const auto Old = std::find(Before.Names.begin(), Before.Names.end(), Pair.first);
        if (Old == Before.Names.end()) continue;
        const auto New = std::find(Before.Names.begin(), Before.Names.end(), Pair.second);
        if (std::count(Before.Names.begin(), Before.Names.end(), Pair.first) != 1 ||
            (New != Before.Names.end() && std::count(Before.Names.begin(), Before.Names.end(), Pair.second) != 1))
        { Error = "postprocess-cook-name-identity-collision"; return false; }
        if (New == Before.Names.end()) Rename.emplace(Pair);
        else NameSlots.emplace(std::uint32_t(Old - Before.Names.begin()), std::uint32_t(New - Before.Names.begin()));
    }
    const auto EngineName = std::find(Before.Names.begin(), Before.Names.end(), "/Script/Engine");
    if (EngineName == Before.Names.end()) { Error = "postprocess-cook-native-engine-name-missing"; return false; }
    auto Candidate = Header;
    bool Table = false;
    for (const auto& Span : Before.HeaderSpans) if (Span.Kind == "imports")
    {
        Table = true;
        for (std::size_t Index = 0; Index < Before.Imports.size(); ++Index)
        {
            const auto& Import = Before.Imports[Index];
            const auto At = Span.Offset + Index * 28;
            if (Import.Name == Storage || Import.Name == "Default__" + Storage)
                P4MapKit::Export::PackageRewriteDetail::Put(Candidate, At + 16, EnginePackage);
            if (Import.Class == Storage)
                P4MapKit::Export::PackageRewriteDetail::Put(Candidate, At, std::uint32_t(EngineName - Before.Names.begin()));
            for (const std::size_t Field : {std::size_t(8), std::size_t(20)})
            {
                std::uint64_t Name = 0, Number = 0;
                if (!P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At + Field, Name) ||
                    !P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At + Field + 4, Number))
                { Error = "postprocess-cook-import-name-unreadable"; return false; }
                const auto To = NameSlots.find(std::uint32_t(Name));
                if (To == NameSlots.end()) continue;
                if (Number) { Error = "postprocess-cook-numbered-class-identity"; return false; }
                P4MapKit::Export::PackageRewriteDetail::Put(Candidate, At + Field, To->second);
            }
        }
    }
    if (!Table) { Error = "postprocess-cook-import-table-missing"; return false; }
    P4MapKit::Export::RewrittenPackage Rewritten; std::string Why;
    if (!P4MapKit::Export::RewriteCookedNames(Candidate, Body, Rename, Rewritten, Why))
    { Error = Why; return false; }
    if (Rewritten.Body != Body) { Error = "postprocess-cook-translation-changed-export-bytes"; return false; }
    const auto After = P4MapKit::ReadPackage(Rewritten.Header, Rewritten.Body);
    if (!After.Error.Code.empty()) { Error = After.Error.Code; return false; }
    for (const auto& Import : After.Imports)
        if (Import.Name == Storage || Import.Name == "Default__" + Storage || Import.Class == Storage)
        { Error = "postprocess-cook-class-identity-not-restored"; return false; }
    Header = std::move(Rewritten.Header); Error.clear(); return true;
}

namespace
{
using Tag=P4MapKit::FPropertyEnvelope;
const char* RecoveredPP="/Script/P4StubsCore.P4RecoveredPostProcessComponent";
const char* OriginalPP="/Script/Engine.PostProcessComponent";
bool IsPP(const P4MapKit::FPackageInventory& Package,const P4MapKit::FPackageExport& Export,const char* Class)
{std::string Name;return Package.Reference(Export.Class,Name)&&Name==Class;}
const Tag* Find(const std::vector<Tag>& Tags,const char* Name)
{for(const auto& Item:Tags)if(Item.Name==Name&&Item.ArrayIndex==0)return &Item;return nullptr;}
bool SettingsTag(const Tag* Property,const char* Struct)
{return Property&&Property->Type=="StructProperty"&&Property->Detail==std::vector<std::string>{Struct}&&Property->ArrayIndex==0;}
Bytes Slice(const Bytes& Data,const Tag& Property,bool PayloadOnly)
{return Bytes(Data.begin()+(PayloadOnly?Property.PayloadOffset:Property.TagOffset),Data.begin()+Property.PayloadOffset+Property.Size);}
}

inline bool HasFork(const Bytes& Header,const Bytes& Body,bool& NeedsMerge,std::string& Error)
{
    NeedsMerge=false;const auto Package=P4MapKit::ReadPackage(Header,Body);
    if(!Package.Error.Code.empty()){Error=Package.Error.Code;return false;}
    for(const auto& Export:Package.Exports)if(IsPP(Package,Export,RecoveredPP))
    {
        const auto Tags=P4MapKit::ReadPropertyEnvelopes(Body,Package.Names,Export.Offset,Export.Offset+Export.Size);
        if(!Tags.Error.Code.empty()){Error="postprocess-cook-native-export-tags-invalid";return false;}
        for(const auto& Property:Tags.Properties)if(Property.Name=="ForkSettings")NeedsMerge=true;
    }
    Error.clear();return true;
}

// What the game's file says of each of its post-process components: the identity and order of the
// component's tags and of its Settings members, never a value. Read from the game's file, or from a
// record of it kept with an imported level.
struct FSourceComponent
{
    std::vector<FSourceTag> Tags;
    bool HasSettings=false;
    FSourceTag Settings;
    std::vector<FSourceTag> Members;
};
using FSourceComponents=std::map<std::string,FSourceComponent>;

inline bool ReadSourceComponents(const Bytes& SourceHeader,const Bytes& SourceBody,FSourceComponents& Out,std::string& Error)
{
    Out.clear();const auto Source=P4MapKit::ReadPackage(SourceHeader,SourceBody);
    if(!Source.Error.Code.empty()){Error="postprocess-cook-original-package-invalid";return false;}
    for(std::size_t N=0;N<Source.Exports.size();++N)if(IsPP(Source,Source.Exports[N],OriginalPP))
    {
        std::string Path;if(!Source.Reference(std::int32_t(N+1),Path)||Out.count(Path))
        {Error="postprocess-cook-original-object-identity-invalid";return false;}
        const auto& Original=Source.Exports[N];
        const auto OriginalTags=P4MapKit::ReadPropertyEnvelopes(SourceBody,Source.Names,Original.Offset,Original.Offset+Original.Size);
        if(!OriginalTags.Error.Code.empty()){Error="postprocess-cook-original-object-tags-invalid";return false;}
        FSourceComponent Component;
        for(const auto& Property:OriginalTags.Properties)Component.Tags.emplace_back(Property);
        const Tag* Settings=Find(OriginalTags.Properties,"Settings");
        if(Settings&&!SettingsTag(Settings,"PostProcessSettings")){Error="postprocess-cook-original-Settings-wrapper-invalid";return false;}
        if(Settings)
        {
            const auto Members=P4MapKit::ReadPropertyEnvelopes(SourceBody,Source.Names,Settings->PayloadOffset,Settings->PayloadOffset+Settings->Size);
            if(!Members.Error.Code.empty()||Members.End!=Settings->PayloadOffset+Settings->Size)
            {Error="postprocess-cook-original-Settings-member-tags-invalid";return false;}
            Component.HasSettings=true;Component.Settings=FSourceTag(*Settings);
            for(const auto& Member:Members.Properties)Component.Members.emplace_back(Member);
        }
        Out.emplace(Path,std::move(Component));
    }
    Error.clear();return true;
}

// Source is the game's tag identity and order (ReadSourceComponents or its kept record). Without it a
// component with serialized extension fields is refused: the order is never made up.
inline bool TranslatePackage(Bytes& Header,Bytes& Body,const FSourceComponents* Source,bool RelocationSafe,std::string& Error)
{
    bool Needed=false;if(!HasFork(Header,Body,Needed,Error))return false;
    if(!Needed)return TranslateClass(Header,Body,Error);
    if(!Source){Error="postprocess-cook-original-tag-order-required";return false;}
    if(!RelocationSafe){Error="postprocess-cook-native-export-relocation-not-qualified";return false;}
    const auto Native=P4MapKit::ReadPackage(Header,Body);
    std::vector<std::string> Names=Native.Names;std::map<std::size_t,Bytes> Replacements;
    for(std::size_t N=0;N<Native.Exports.size();++N)
    {
        const auto& Export=Native.Exports[N];if(!IsPP(Native,Export,RecoveredPP))continue;
        const auto Tags=P4MapKit::ReadPropertyEnvelopes(Body,Native.Names,Export.Offset,Export.Offset+Export.Size);
        const Tag* Fork=Find(Tags.Properties,"ForkSettings");if(!Fork)continue;
        if(!SettingsTag(Fork,"P4RecoveredPostProcessForkSettings"))
        {Error="postprocess-cook-native-fork-wrapper-invalid";return false;}
        const Tag* Stock=Find(Tags.Properties,"Settings");
        if(Stock&&!SettingsTag(Stock,"PostProcessSettings")){Error="postprocess-cook-native-Settings-wrapper-invalid";return false;}
        std::string Path;if(!Native.Reference(std::int32_t(N+1),Path)||!Source->count(Path))
        {Error="postprocess-cook-original-object-not-found";return false;}
        const FSourceComponent& Original=Source->at(Path);
        if(Original.HasSettings&&(Original.Settings.Type!="StructProperty"||Original.Settings.Detail!=std::vector<std::string>{"PostProcessSettings"}||Original.Settings.ArrayIndex!=0))
        {Error="postprocess-cook-original-Settings-wrapper-invalid";return false;}
        Bytes Empty;if(!P4MapKit::PostProcessCook::Detail::Name(Empty,Names,"None"))
        {Error="postprocess-cook-terminator-identity-invalid";return false;}
        const auto StockBytes=Stock?Slice(Body,*Stock,true):Empty;
        P4MapKit::PostProcessCook::FLogicalSettings Merged;std::string Why;
        if(!P4MapKit::PostProcessCook::Merge(Names,StockBytes,Slice(Body,*Fork,true),Original.Members,Merged,Why))
        {Error=Why;return false;}
        Names=std::move(Merged.Names);
        std::map<std::string,Bytes> Fields;
        for(const auto& Property:Tags.Properties)if(Property.Name!="ForkSettings"&&Property.Name!="Settings")
            Fields.emplace(P4MapKit::PostProcessCook::Detail::Key(Property),Slice(Body,Property,false));
        if(Original.HasSettings)
        {
            Bytes Rebuilt;Tag Value;
            if(!P4MapKit::PostProcessCook::Detail::Tag(Rebuilt,Names,Original.Settings,Value,Merged.Payload))
            {Error="postprocess-cook-Settings-tag-identity-invalid";return false;}
            Fields.emplace(P4MapKit::PostProcessCook::Detail::Key(Original.Settings),std::move(Rebuilt));
        }
        if(Fields.size()!=Original.Tags.size())
        {Error="postprocess-cook-original-native-object-member-set-differs";return false;}
        Bytes Rebuilt;
        for(const auto& OriginalProperty:Original.Tags)
        {
            const auto Field=Fields.find(P4MapKit::PostProcessCook::Detail::Key(OriginalProperty));
            if(Field==Fields.end()){Error="postprocess-cook-original-native-object-member-missing";return false;}
            Rebuilt.insert(Rebuilt.end(),Field->second.begin(),Field->second.end());
        }
        if(!P4MapKit::PostProcessCook::Detail::Name(Rebuilt,Names,"None"))
        {Error="postprocess-cook-object-terminator-invalid";return false;}
        Rebuilt.insert(Rebuilt.end(),Body.begin()+Tags.End,Body.begin()+Export.Offset+Export.Size);
        Replacements.emplace(N,std::move(Rebuilt));
    }
    P4MapKit::Export::RewrittenPackage Candidate;std::string Why;
    if(!P4MapKit::Export::RebuildCookedExports(Header,Body,Names,Replacements,Candidate,Why))
    {Error=Why;return false;}
    if(!TranslateClass(Candidate.Header,Candidate.Body,Error))return false;
    Header=std::move(Candidate.Header);Body=std::move(Candidate.Body);Error.clear();return true;
}

// The game's file itself as the source of tag identity and order.
inline bool TranslatePackage(Bytes& Header,Bytes& Body,const Bytes* SourceHeader,const Bytes* SourceBody,
    bool RelocationSafe,std::string& Error)
{
    bool Needed=false;if(!HasFork(Header,Body,Needed,Error))return false;
    if(!Needed)return TranslateClass(Header,Body,Error);
    if(!SourceHeader||!SourceBody){Error="postprocess-cook-original-tag-order-required";return false;}
    if(!RelocationSafe){Error="postprocess-cook-native-export-relocation-not-qualified";return false;}
    FSourceComponents Source;if(!ReadSourceComponents(*SourceHeader,*SourceBody,Source,Error))return false;
    return TranslatePackage(Header,Body,&Source,RelocationSafe,Error);
}
} }

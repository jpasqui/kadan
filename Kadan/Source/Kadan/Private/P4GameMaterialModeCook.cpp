// The game's own modes in a cooked material (P4GameMaterialModeCook.h).
#include "P4GameMaterialModeCook.h"
#include "P4MaterialGameSettings.h"
#include "P4MaterialInstanceGameCook.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "P4MapKitCore/postprocess_cook.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "P4MapKitCore/export_package.h"

namespace
{
using Bytes = std::vector<uint8>;

// What the game's UMaterial holds of a material's C4 settings that stock Unreal's cannot: the game's decal mode by
// name, and the game's render-state flags of UMaterial (bC4ReverseDepthTest, bC4XluDepthWrite and the two separate
// translucency passes), set ones only, by the field name the game's Material.h gives them.
struct FGameFields
{
    const char* DecalMode = nullptr;
    TArray<FString> Flags;

    bool Any() const { return DecalMode || Flags.Num(); }
    bool Owns(const std::string& Tag) const
    {
        return (DecalMode && Tag == "DecalBlendMode") || Flags.Contains(UTF8_TO_TCHAR(Tag.c_str()));
    }
};

FGameFields GameFields(const UMaterial* Material)
{
    FGameFields Out;
    const UP4MaterialGameSettings* Settings = Material ? UP4MaterialGameSettings::Find(Material) : nullptr;
    if (!Settings)
        return Out;
    if (Material->MaterialDomain == MD_DeferredDecal && Settings->C4DecalBlendMode == C4DBM_DBuffer_C4_WaterBox)
        Out.DecalMode = "DBM_DBuffer_C4_WaterBox";
    for (TFieldIterator<FBoolProperty> It(UP4MaterialGameSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper);
        It; ++It)
        if (It->HasMetaData(TEXT("C4MaterialField")) && It->GetPropertyValue_InContainer(Settings))
            Out.Flags.Add(It->GetName());
    return Out;
}

// The package's materials with fields of the game's, by their path in the package (as a cooked export names it).
TMap<FString, FGameFields> GameModes(UPackage* Package)
{
    TMap<FString, FGameFields> Out;
    if (!Package)
        return Out;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, false);
    for (UObject* Object : Objects)
    {
        FGameFields Fields = GameFields(Cast<UMaterial>(Object));
        if (!Fields.Any())
            continue;
        FString Path = Object->GetPathName(Package);
        Path.ReplaceInline(TEXT(":"), TEXT("."));
        Out.Add(Path, MoveTemp(Fields));
    }
    return Out;
}
} //namespace

bool P4PackageHasGameMaterialModes(UPackage* Package)
{
    return GameModes(Package).Num() > 0 || P4PackageHasMaterialInstanceC4(Package);
}

bool P4TranslateGameMaterialModeCook(
    UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    using namespace P4MapKit::PostProcessCook::Detail;
    if (!P4TranslateMaterialInstanceC4Cook(Package, Header, Body, Error))
    {
        return false;
    }
    const TMap<FString, FGameFields> Wanted = GameModes(Package);
    if (!Wanted.Num())
    {
        Error.Reset();
        return true;
    }
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Native.Error.Code.c_str());
        return false;
    }
    auto Names = Native.Names;
    std::map<std::size_t, Bytes> Replacements;
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
    {
        const auto& Export = Native.Exports[At];
        std::string Path;
        if (!Native.Reference(int32(At + 1), Path))
        {
            Error = TEXT("game-material-mode-cook-export-unnamed");
            return false;
        }
        const FGameFields* Fields = Wanted.Find(UTF8_TO_TCHAR(Path.c_str()));
        if (!Fields)
            continue;
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Body, Native.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Tags.Error.Code.empty())
        {
            Error = UTF8_TO_TCHAR(Tags.Error.Code.c_str());
            return false;
        }
        // Every tag but the ones the game's fields replace (the stand-in's DecalBlendMode, absent when the stand-in
        // is stock's default), then the game's.
        Bytes Rebuilt;
        std::size_t From = Export.Offset;
        for (const auto& Tag : Tags.Properties)
            if (Fields->Owns(Tag.Name))
            {
                Rebuilt.insert(Rebuilt.end(), Body.begin() + From, Body.begin() + Tag.TagOffset);
                From = Tag.PayloadOffset + Tag.Size;
            }
        Rebuilt.insert(Rebuilt.end(), Body.begin() + From, Body.begin() + Tags.End - 8);
        if (Fields->DecalMode)
        {
            P4MapKit::PostProcessCook::FSourceTag Metadata;
            Metadata.Name = "DecalBlendMode";
            Metadata.Type = "ByteProperty";
            Metadata.Detail = {"EDecalBlendMode"};
            Bytes Value;
            if (!Name(Value, Names, Fields->DecalMode) || !Tag(Rebuilt, Names, Metadata, {}, Value))
            {
                Error = TEXT("game-material-mode-cook-tag-invalid");
                return false;
            }
        }
        for (const FString& Flag : Fields->Flags)
        {
            P4MapKit::PostProcessCook::FSourceTag Metadata;
            Metadata.Name = TCHAR_TO_UTF8(*Flag);
            Metadata.Type = "BoolProperty";
            P4MapKit::FPropertyEnvelope Set;
            Set.BoolValue = true;
            if (!Tag(Rebuilt, Names, Metadata, Set, {}))
            {
                Error = TEXT("game-material-mode-cook-tag-invalid");
                return false;
            }
        }
        if (!Name(Rebuilt, Names, "None"))
        {
            Error = TEXT("game-material-mode-cook-tag-invalid");
            return false;
        }
        Rebuilt.insert(Rebuilt.end(), Body.begin() + Tags.End, Body.begin() + Export.Offset + Export.Size);
        Replacements.emplace(At, std::move(Rebuilt));
    }
    if (int32(Replacements.size()) != Wanted.Num())
    {
        Error = TEXT("game-material-mode-cook-material-not-serialized");
        return false;
    }
    P4MapKit::Export::RewrittenPackage Candidate;
    std::string Why;
    if (!P4MapKit::Export::RebuildCookedExports(Header, Body, Names, Replacements, Candidate, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    // Read the written package again: the material holds the game's value once, and nothing else moved.
    const auto Checked = P4MapKit::ReadPackage(Candidate.Header, Candidate.Body);
    if (!Checked.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Checked.Error.Code.c_str());
        return false;
    }
    for (const auto& Pair : Replacements)
    {
        const auto& Export = Checked.Exports[Pair.first];
        std::string Path;
        if (!Checked.Reference(int32(Pair.first + 1), Path))
        {
            Error = TEXT("game-material-mode-cook-export-unnamed");
            return false;
        }
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Candidate.Body, Checked.Names, Export.Offset, Export.Offset + Export.Size);
        const FGameFields& Fields = Wanted.FindChecked(UTF8_TO_TCHAR(Path.c_str()));
        int32 Modes = 0;
        int32 Flags = 0;
        bool Wrong = false;
        for (const auto& Tag : Tags.Properties)
            if (Fields.DecalMode && Tag.Name == "DecalBlendMode")
            {
                std::uint64_t Index = 0;
                if (Tag.Size != 8 ||
                    !P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate.Body, Tag.PayloadOffset, Index, 4) ||
                    Index >= Checked.Names.size() || Tag.Detail.size() != 1 || Tag.Detail[0] != "EDecalBlendMode")
                    Wrong = true;
                else if (Checked.Names[std::size_t(Index)] == Fields.DecalMode)
                    ++Modes;
            }
            else if (Fields.Owns(Tag.Name))
            {
                Wrong = Wrong || Tag.Type != "BoolProperty" || !Tag.BoolValue || Tag.Size != 0;
                ++Flags;
            }
        if (!Tags.Error.Code.empty() || Wrong || Modes != (Fields.DecalMode ? 1 : 0) || Flags != Fields.Flags.Num())
        {
            Error = TEXT("game-material-mode-cook-readback-differs");
            return false;
        }
    }
    Header = std::move(Candidate.Header);
    Body = std::move(Candidate.Body);
    Error.Reset();
    return true;
}

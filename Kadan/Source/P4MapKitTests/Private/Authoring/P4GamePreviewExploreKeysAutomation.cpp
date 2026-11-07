#if P4_MAPKIT_TESTS
#include "P4GamePreviewTemplate.h"
#include "P4GamePreviewTestGame.h"
#include "P4GamePreviewWorld.h"
#include "P4KadanContainerIndex.h"
#include "P4KadanOriginalConversion.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
// The rule a new level's scene used before: eight fixed explore-rate keys, a fixed format version, the
// SD_Area parent and SD_/APN_ name prefixes.
const TCHAR* const OldKeys[] = {TEXT("Otakara"), TEXT("Survivor"), TEXT("SurvivorLeaf"), TEXT("TekiDay"),
    TEXT("OnyonCarry"), TEXT("Kinkai"), TEXT("ShizaiStation"), TEXT("ShizaiTateana")};
constexpr uint64 OldVersion = 8626647386ull;
const FString OldParent(TEXT("/Game/Carrot4/GameScene/Game/SD_Area.SD_Area"));

// The keys and version of one placement counts package, as the game cooked them.
bool ReadCounts(const P4GamePreviewTestGame::FPackage& Package, TArray<FString>& Keys, uint64& Version)
{
    using namespace P4MapKit;
    Keys.Reset();
    Version = 0;
    const auto Properties = P4GamePreviewTestGame::Properties(Package, Package.Asset);
    for (const auto& Info : Properties.Properties)
    {
        if (Info.Name != "ActorPlacedNumInfo")
            continue;
        const auto Members = ReadPropertyEnvelopes(
            Package.Body, Package.Inventory.Names, Info.PayloadOffset, Info.PayloadOffset + Info.Size);
        for (const auto& Member : Members.Properties)
        {
            FByteReader Reader(Package.Body, Member.PayloadOffset, Member.PayloadOffset + Member.Size);
            if (Member.Name == "AssetVersion" && !Reader.Unsigned(8, Version))
                return false;
            if (Member.Name != "ExploreRateActorNum")
                continue;
            std::int32_t Removed = 0;
            std::int32_t Count = 0;
            if (!Reader.I32(Removed) || !Reader.I32(Count) || Removed != 0 || Count < 0 || Count > 64)
                return false;
            // Enum keys are cooked as names; each value is an int32.
            for (std::int32_t Index = 0; Index < Count; ++Index)
            {
                std::string Key;
                std::int32_t Value = 0;
                if (!Reader.Name(Package.Inventory.Names, Key) || !Reader.I32(Value))
                    return false;
                FString Name(UTF8_TO_TCHAR(Key.c_str()));
                const int32 Scope = Name.Find(TEXT("::"));
                Keys.Add(Scope == INDEX_NONE ? Name : Name.Mid(Scope + 2));
            }
            if (Reader.Tell() != Member.PayloadOffset + Member.Size)
                return false;
        }
    }
    return Version != 0;
}
} // namespace

// The explore-rate categories from the count map's own enum, and the parent scene, counts class, version
// and names from the preview template, agree with the fixed rule they replace and with every placement
// counts asset the game has.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GamePreviewExploreKeysTest, "P4MapKit.Game.PreviewExploreKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GamePreviewExploreKeysTest::RunTest(const FString&)
{
    using namespace P4GamePreviewTestGame;
    FP4KadanImportSettings Settings;
    FString Error;
    FP4GamePreviewTemplate Template;
    if (!GameSettings(Settings, Error) || !P4ReadGamePreviewTemplate(Settings, Template, Error))
    {
        AddError(Error);
        return false;
    }
    UClass* CountsClass = LoadObject<UClass>(nullptr, *Template.CountsClass);
    FStructProperty* Info = CountsClass ? FindFProperty<FStructProperty>(CountsClass, TEXT("ActorPlacedNumInfo"))
                                        : nullptr;
    FMapProperty* Map = Info ? FindFProperty<FMapProperty>(Info->Struct, TEXT("ExploreRateActorNum")) : nullptr;
    FEnumProperty* EnumKey = Map ? CastField<FEnumProperty>(Map->KeyProp) : nullptr;
    FByteProperty* ByteKey = Map ? CastField<FByteProperty>(Map->KeyProp) : nullptr;
    const UEnum* Enum = EnumKey ? EnumKey->GetEnum() : ByteKey ? ByteKey->Enum : nullptr;
    if (!TestNotNull(TEXT("the counts class's key enum is reflected"), Enum))
        return false;
    const TArray<FString> Categories = P4GamePreviewExploreCategories(Enum);
    TArray<FString> Old;
    for (const TCHAR* Key : OldKeys)
        Old.Add(Key);
    TestEqual(TEXT("the enum rule gives the old eight keys in order"), FString::Join(Categories, TEXT(",")),
        FString::Join(Old, TEXT(",")));
    TestEqual(TEXT("the template's counts version is the old fixed version"), Template.CountsVersion, OldVersion);
    TestEqual(TEXT("the template's parent scene is the old SD_Area"), Template.ParentScene, OldParent);
    TestEqual(TEXT("the template's scene class is the old one"), Template.SceneClass,
        FString(TEXT("/Script/Carrot4.GameSceneData")));
    TestEqual(TEXT("the template's counts class is the old one"), Template.CountsClass,
        FString(TEXT("/Script/Carrot4.ActorPlacedNumAsset")));
    for (const uint32 Area : {5u, 7u, 8u, 9u})
    {
        const FString Name = FString::Printf(TEXT("Area%03u"), Area);
        TestEqual(TEXT("renamed scene"),
            P4GamePreviewRename(FPackageName::GetShortName(Template.WorldScene), Template.WorldToken, Name),
            TEXT("SD_") + Name);
        TestEqual(TEXT("renamed counts"),
            P4GamePreviewRename(FPackageName::GetShortName(Template.Counts), Template.WorldToken, Name),
            TEXT("APN_") + Name + TEXT("_P"));
    }

    // Every placement counts asset of the game holds exactly these keys and this version.
    FP4KadanContainerIndex Index;
    TArray<FP4KadanContainerPackage> Packages;
    if (!P4KadanReadContainerIndex(Settings, Index, Error) || !Index.Enumerate(Packages, Error))
    {
        AddError(Error);
        return false;
    }
    TArray<FP4KadanContainerPackage> Counts;
    for (const FP4KadanContainerPackage& Entry : Packages)
        if (!Entry.IsMap && FPackageName::GetShortName(Entry.Package).StartsWith(TEXT("APN_")))
            Counts.Add(Entry);
    TArray<FString> Headers;
    for (const FP4KadanContainerPackage& Entry : Counts)
        Headers.Add(Entry.Path);
    const FString Into = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Kadan/Preview/BootstrapSource"));
    if (!P4KadanConvertMountedOriginals(Settings, Headers, Into, Error))
    {
        AddError(Error);
        return false;
    }
    int32 Agreed = 0;
    for (const FP4KadanContainerPackage& Entry : Counts)
    {
        FPackage Package;
        TArray<FString> Keys;
        uint64 Version = 0;
        if (!TestTrue(TEXT("reads ") + Entry.Package,
                ReadConverted(Into / Entry.Path, Entry.Package, Package, Error) && ReadCounts(Package, Keys, Version)))
            continue;
        Keys.Sort();
        TArray<FString> Expected = Categories;
        Expected.Sort();
        const bool Same = Keys == Expected && Version == Template.CountsVersion;
        TestTrue(TEXT("keys and version of ") + Entry.Package, Same);
        Agreed += Same ? 1 : 0;
    }
    AddInfo(FString::Printf(TEXT("%d of %d placement counts assets agree"), Agreed, Counts.Num()));
    TestTrue(TEXT("the game's placement counts assets were read"), Counts.Num() > 100);
    return true;
}
#endif

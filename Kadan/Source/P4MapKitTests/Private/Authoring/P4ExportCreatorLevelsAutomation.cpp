#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"

namespace
{
// An empty creator level in memory, removed again when the test ends.
struct FCreatorLevel
{
    UPackage* Package = nullptr;
    UWorld* World = nullptr;

    FCreatorLevel(const FString& PackageName)
    {
        Package = CreatePackage(*PackageName);
        World = UWorld::CreateWorld(EWorldType::Editor, false, *FPackageName::GetShortName(PackageName), Package);
        if (!World)
            return;
        World->SetFlags(RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(World);
    }

    ~FCreatorLevel()
    {
        if (World)
        {
            FAssetRegistryModule::AssetDeleted(World);
            World->DestroyWorld(false);
        }
        Package->ClearFlags(RF_Public | RF_Standalone);
        Package->SetDirtyFlag(false);
        Package->MarkPendingKill();
    }
};

// The levels the manifest gives the one edited map.
TArray<FString> ManifestLevels(const FP4ExportModResult& Exported, const FString& Id)
{
    TArray<FString> Levels;
    const auto Manifest = P4ExportDriverTest::ReadJson(
        Exported.Directory / TEXT("romfs/tsuchi/mods") / Id / TEXT("mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    if (!Manifest.IsValid() || !Manifest->TryGetArrayField(TEXT("maps"), Maps) || Maps->Num() != 1)
        return Levels;
    const TArray<TSharedPtr<FJsonValue>>* Named = nullptr;
    if ((*Maps)[0]->AsObject()->TryGetArrayField(TEXT("levels"), Named))
        for (const TSharedPtr<FJsonValue>& Level : *Named)
            Levels.Add(Level->AsString());
    return Levels;
}
} // namespace

// A creator level that streams another creator level: the map loads the parent, and the parent brings its
// child. Both ship, but only the parent is the map's level (Tsuchi refuses a level that comes twice).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorStreamingChildTest, "P4MapKit.Export.CreatorStreamingChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorStreamingChildTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("test.creatorstreaming"), TEXT("Creator streaming"), RequestData, Names))
        return false;
    const FString ParentName = Names.PackageRoot / TEXT("Maps/StreamParent");
    const FString ChildName = Names.PackageRoot / TEXT("Maps/StreamChild");
    FCreatorLevel Parent(ParentName);
    FCreatorLevel Child(ChildName);
    if (!TestTrue(TEXT("Both creator levels are created"), Parent.World && Child.World))
        return false;
    ULevelStreamingAlwaysLoaded* Streaming = NewObject<ULevelStreamingAlwaysLoaded>(
        Parent.World, NAME_None, RF_Transactional);
    Streaming->SetWorldAssetByPackageName(FName(*ChildName));
    Parent.World->AddStreamingLevel(Streaming);
    RequestData.MapPackage = Cave000;
    for (const FString& Package : {ParentName, ChildName})
    {
        FP4ExportRoot Root;
        Root.Path = Package;
        RequestData.Roots.Add(Root);
    }
    FString Error;
    FP4ExportModResult Exported;
    if (!TestTrue(TEXT("The parent and its streaming child export"), P4ExportMod(RequestData, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    TArray<FString> Shipped = Exported.Levels;
    Shipped.Sort();
    TestTrue(TEXT("Both levels ship in the container"), Shipped == TArray<FString>({ChildName, ParentName}));
    TestTrue(TEXT("The map loads the parent only, and the parent brings its child"),
        ManifestLevels(Exported, RequestData.Mod.Id) == TArray<FString>{ParentName});
    return !HasAnyErrors();
}

// A creator level kept outside the mod's folder ships at its own path and the map names it there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorLevelOwnPathTest, "P4MapKit.Export.CreatorLevelOwnPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorLevelOwnPathTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("test.creatorlevelpath"), TEXT("Creator level path"), RequestData, Names))
        return false;
    const FString LevelName = TEXT("/Game/KadanReviewLevels/OwnPathLevel");
    FCreatorLevel Level(LevelName);
    if (!TestNotNull(TEXT("The creator level is created"), Level.World))
        return false;
    const FP4ExportAvailability Availability = P4ExportAssetAvailability(Level.World, RequestData.Mod);
    if (!TestTrue(
            TEXT("Review accepts the level at its own path"), Availability.Route == EP4ExportAssetRoute::CreatorLevel))
        return false;
    RequestData.MapPackage = Cave000;
    FP4ExportRoot Root;
    Root.Path = LevelName;
    RequestData.Roots.Add(Root);
    FString Error;
    FP4ExportModResult Exported;
    if (!TestTrue(TEXT("The level at its own path exports"), P4ExportMod(RequestData, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("The level ships at the path it has"), Exported.Packages == TArray<FString>{LevelName});
    TestTrue(TEXT("The map names the level at that path"),
        ManifestLevels(Exported, RequestData.Mod.Id) == TArray<FString>{LevelName});
    return !HasAnyErrors();
}
#endif

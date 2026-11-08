#if P4_MAPKIT_TESTS
#include "P4ModDescriptor.h"
#include "P4ExportDriver.h"
#include "P4KadanBlueprintPlans.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/World.h"
#include "Factories/WorldFactory.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ModContentPathTest, "P4MapKit.Kadan.ModContentPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ModContentPathTest::RunTest(const FString&)
{
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("content.path.test");
    Mod.Name = TEXT("Content Path Test");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!TestTrue(TEXT("Mod folder name"), P4ModDerivedNames(Mod, Names, Error)))
        return false;
    const FString& Root = Names.PackageRoot;
    TestTrue(TEXT("The mod's own folder"), P4IsModContentPath(Root, Root + TEXT("/Maps/L_Room")));
    TestTrue(TEXT("A folder next to the game's"),
        P4IsModContentPath(Root, TEXT("/Game/Carrot4/Characters/Teki/Dirigibug/dirigibug")));
    TestEqual(TEXT("The mod's folder is its ID with underscores for dots"), Root, FString(TEXT("/Game/Mods/content_path_test")));
    TestTrue(TEXT("A folder of the creator's in /Game/Mods"), P4IsModContentPath(Root, TEXT("/Game/Mods/MyStuff/T_Leaf")));
    TestFalse(TEXT("Kadan's live preview folder"), P4IsModContentPath(Root, TEXT("/Game/KadanPreview/kadan-preview-x/T_Leaf")));
    TestFalse(TEXT("Engine content"), P4IsModContentPath(Root, TEXT("/Engine/EngineMaterials/DefaultMaterial")));
    // Kadan's live preview folder is told with the mod's own folder.
    const FString Preview = P4ModContentPathIssue(Root, TEXT("/Game/KadanPreview/kadan-preview-x/Creature_Test"));
    AddInfo(TEXT("preview folder: ") + Preview);
    TestTrue(TEXT("The live preview folder is told with the mod's own folder"), Preview.Contains(Root));
    TestTrue(TEXT("A hand-made folder in /Game/Mods has no issue"),
        P4ModContentPathIssue(Root, TEXT("/Game/Mods/content_path/Creature_Test")).IsEmpty());
    TestTrue(TEXT("The mod's own folder has no issue"), P4ModContentPathIssue(Root, Root + TEXT("/Creature_Test")).IsEmpty());
    TestTrue(TEXT("A folder of its own outside /Game/Mods has no issue"),
        P4ModContentPathIssue(Root, TEXT("/Game/ContentPath/Creature_Test")).IsEmpty());
    // A module the engine does not have is told as one of a later engine, with the Blueprints that use it.
    const FString Later = P4KadanUnloadedModuleNote(
        TEXT("KadanNoSuchModule"), {TEXT("/Game/Test/BP_ShakeB"), TEXT("/Game/Test/BP_ShakeA")});
    AddInfo(TEXT("unloaded module: ") + Later);
    TestTrue(TEXT("A module no plugin has is one of a later engine"),
        Later.StartsWith(TEXT("BP_ShakeA, BP_ShakeB use the KadanNoSuchModule module of a later Unreal Engine")));

    // A level of the creator's next to the game's folders ships where it is. The same level in another
    // mod's folder does not.
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Outside = TEXT("/Game/__KadanContentPath_") + Token, Other = TEXT("/Game/Mods/__other_") + Token;
    UWorld* Here = nullptr;
    UWorld* There = nullptr;
    ON_SCOPE_EXIT
    {
        // A new level has a scene of its own. It is taken down before its package is unloaded.
        for (UWorld* World : {Here, There})
            if (IsValid(World) && World->bIsWorldInitialized)
                World->DestroyWorld(false);
        TArray<UPackage*> Packages;
        for (const FString& Package : {Outside + TEXT("/L_Outside"), Other + TEXT("/L_Other")})
            if (UPackage* Loaded = FindPackage(nullptr, *Package))
            {
                Loaded->SetDirtyFlag(false);
                Packages.Add(Loaded);
            }
        UPackageTools::UnloadPackages(Packages);
    };
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UWorldFactory* Factory = NewObject<UWorldFactory>();
    Here = Cast<UWorld>(Tools.CreateAsset(TEXT("L_Outside"), Outside, UWorld::StaticClass(), Factory));
    There = Cast<UWorld>(Tools.CreateAsset(TEXT("L_Other"), Other, UWorld::StaticClass(), Factory));
    if (!TestNotNull(TEXT("A level outside the mod's folder"), Here) ||
        !TestNotNull(TEXT("A level in another mod's folder"), There))
        return false;
    const FP4ExportAvailability Ships = P4ExportAssetAvailability(Here, Mod);
    TestTrue(TEXT("The level outside the mod's folder ships: ") + Ships.Reason,
        Ships.Route == EP4ExportAssetRoute::CreatorLevel);
    // One project can hold several mods that ship the same assets, so a level in another mod's folder ships too.
    const FP4ExportAvailability Shared = P4ExportAssetAvailability(There, Mod);
    TestTrue(TEXT("The level in another mod's folder ships too: ") + Shared.Reason,
        Shared.Route == EP4ExportAssetRoute::CreatorLevel);
    return true;
}
#endif

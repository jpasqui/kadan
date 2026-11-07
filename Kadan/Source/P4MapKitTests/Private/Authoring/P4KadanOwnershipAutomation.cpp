#include "P4CreatorSource.h"
#include "P4ExportDriver.h"
#include "P4ExportSelection.h"
#include "P4KadanGameContent.h"
#include "AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

// Whose asset an asset is (decided 2026-10-16): the game's own imported assets are the game's, every other
// asset is the creator's however it was made, and Review refuses a creator asset identical to a game asset.
// Runs in a project with an imported game map, such as a copy of a kept fixture (kadan_fixture.py).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanOwnershipTest, "P4MapKit.Kadan.AssetOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanOwnershipTest::RunTest(const FString&)
{
    FARFilter Filter;
    Filter.ClassNames.Add(UTexture2D::StaticClass()->GetFName());
    Filter.PackagePaths.Add(TEXT("/Game/Carrot4"));
    Filter.bRecursivePaths = true;
    TArray<FAssetData> Found;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Found);
    UTexture2D* Game = nullptr;
    for (const FAssetData& Data : Found)
    {
        UTexture2D* Candidate = Cast<UTexture2D>(Data.GetAsset());
        FString Origin;
        if (Candidate && Candidate->Source.IsValid() && Candidate->Source.GetNumMips() > 0 &&
            P4HasIntrinsicGameSource(Candidate, &Origin) && Origin == Candidate->GetOutermost()->GetName())
        {
            Game = Candidate;
            break;
        }
    }
    if (!Game)
    {
        AddWarning(TEXT("P4MapKit test not run. Run it in a project with an imported game map."));
        return true;
    }
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("ownership.test");
    Mod.Name = TEXT("Ownership Test");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString NameError;
    if (!TestTrue(TEXT("The test mod has a content folder"), P4ModDerivedNames(Mod, Names, NameError)))
    {
        AddError(NameError);
        return false;
    }

    FString Why;
    TestFalse(TEXT("The game's own texture is the game's"), P4IsCreatorAsset(Game, &Why));
    TestTrue(TEXT("It says it is the game's own asset"), Why.Contains(TEXT("game's own asset")));
    TestFalse(TEXT("The game's own texture is not exported"), P4ExportAssetAvailability(Game, Mod).IsAvailable());

    UPackage* Package = CreatePackage(*(Names.PackageRoot + TEXT("/T_Copy")));
    UTexture2D* Copy = DuplicateObject<UTexture2D>(Game, Package, TEXT("T_Copy"));
    ON_SCOPE_EXIT
    {
        Copy->ClearFlags(RF_Public | RF_Standalone);
        Copy->Rename(
            *MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("T_Copy_Released")).ToString(),
            GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Copy->MarkPendingKill();
        Package->SetDirtyFlag(false);
        Package->MarkPendingKill();
    };
    Copy->SetFlags(RF_Public | RF_Standalone);
    TestTrue(TEXT("A copy of a game texture is the creator's"), P4IsCreatorAsset(Copy, &Why));
    FString Same;
    TestTrue(TEXT("An unchanged copy holds exactly the game's content"),
        P4FindGameContentMatch(Copy, Same) && Same == Game->GetOutermost()->GetName());
    const FP4ExportAvailability Unchanged = P4ExportAssetAvailability(Copy, Mod);
    TestFalse(TEXT("Review refuses an unchanged copy"), Unchanged.IsAvailable());
    TestTrue(TEXT("It names the game's texture"), Unchanged.Reason.Contains(Game->GetOutermost()->GetName()));
    if (!Unchanged.Reason.Contains(Game->GetOutermost()->GetName()))
        AddInfo(TEXT("Unchanged copy: ") + Unchanged.Reason);
    TestTrue(TEXT("The panel's live status does not compare content"),
        P4ExportAssetAvailabilityAtAGlance(Copy, Mod).Reason != Unchanged.Reason);

    uint8* Pixels = Copy->Source.LockMip(0);
    if (!TestNotNull(TEXT("The copy's pixels can be changed"), Pixels))
        return false;
    Pixels[0] ^= 0x5A;
    Copy->Source.UnlockMip(0);
    TestFalse(TEXT("A changed copy no longer matches the game"), P4FindGameContentMatch(Copy, Same));
    const FP4ExportAvailability Changed = P4ExportAssetAvailability(Copy, Mod);
    TestTrue(TEXT("A changed copy is exported as the creator's texture"),
        Changed.Route == EP4ExportAssetRoute::CreatorTexture);
    if (!Changed.IsAvailable())
        AddInfo(TEXT("Changed copy: ") + Changed.Reason);

    UPackage* MadePackage = CreatePackage(*(Names.PackageRoot + TEXT("/T_Made")));
    UTexture2D* Made = NewObject<UTexture2D>(MadePackage, TEXT("T_Made"), RF_Public | RF_Standalone);
    ON_SCOPE_EXIT
    {
        Made->ClearFlags(RF_Public | RF_Standalone);
        Made->Rename(
            *MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("T_Made_Released")).ToString(),
            GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Made->MarkPendingKill();
        MadePackage->SetDirtyFlag(false);
        MadePackage->MarkPendingKill();
    };
    TArray<uint8> Red;
    Red.Init(0, 4 * 4 * 4);
    for (int32 I = 0; I < Red.Num(); I += 4)
    {
        Red[I + 2] = 255;
        Red[I + 3] = 255;
    }
    Made->Source.Init(4, 4, 1, 1, TSF_BGRA8, Red.GetData());
    TestFalse(TEXT("A texture made without a supported import has no creator source record"), P4HasCreatorSource(Made));
    TestTrue(TEXT("It is still the creator's"), P4IsCreatorAsset(Made, &Why));
    TestFalse(TEXT("It matches no game texture"), P4FindGameContentMatch(Made, Same));
    TestTrue(TEXT("It is exported as the creator's texture"),
        P4ExportAssetAvailability(Made, Mod).Route == EP4ExportAssetRoute::CreatorTexture);
    return true;
}

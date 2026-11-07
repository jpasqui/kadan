#if P4_MAPKIT_TESTS
#include "P4KadanTestSeams.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4OriginalLevelImport.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "NavModifierVolume.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "Components/BrushComponent.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AreaBrushRecoveryTest, "P4MapKit.NativeImport.ActualAreaNavigationBrushes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AreaBrushRecoveryTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_AREA_BRUSHES")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned original Area001 sources."));
        return true;
    }
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(
            TEXT("The actual retained class-source registry is readable"),
            P4KadanScanGame(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME")), Sources, Problems)))
        return false;
    FP4KadanImportSourceScope SourceScope(Sources);
    // This scan supplies class defaults; the selected map is in the retained
    // converted cache. Custody's physical-file check requires absolute paths.
    const FString Header = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() /
        TEXT("Kadan/ConvertedOriginals/Carrot4/Content/Carrot4/Maps/Main/Area/Area001/Area001_P.umap"));
    const auto Result = P4ProbeOriginalGameBackedFamiliesForTest(Header, FPaths::ChangeExtension(Header, TEXT("uexp")));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(P4OriginalLevelImportJson(Result),
                                  *(Folder / TEXT("Area001-brush-family-proof.json")),
                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UWorld* World = Result.Asset;
    TStrongObjectPtr<UWorld> Hold(World);
    ON_SCOPE_EXIT
    {
        Hold.Reset();
        P4DiscardOriginalFamilyProbeForTest(World);
    };
    if (!TestTrue(TEXT("The original source runs through exact family construction: ") + Result.ErrorDetail,
                  Result.IsValid() && World))
        return false;
    TestFalse(TEXT("The isolated diagnostic cannot replace or publish the creator's map"),
              Result.Created || Result.Saved || Result.BaselineKnown);
    int32 Count = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
        if (auto* Volume = Cast<ANavModifierVolume>(Actor))
        {
            ++Count;
            TestTrue(TEXT("Every actual modifier has its reconstructed polygon model: ") + Volume->GetName(),
                     Volume->Brush && Volume->Brush->Polys && Volume->Brush->Polys->Element.Num() > 0 &&
                         Volume->GetBrushComponent() && Volume->GetBrushComponent()->Brush == Volume->Brush);
        }
    TestEqual(TEXT("All 176 original Area001 navigation modifier volumes are editable source actors"), Count, 176);
    AddInfo(
        FString::Printf(TEXT("Area001 exact source projection: %d editable actors; %d navigation brushes; reason %s"),
                        Result.EditableOriginalActorCount, Count, *Result.GameBackedReason));
    return !HasAnyErrors();
}
#endif

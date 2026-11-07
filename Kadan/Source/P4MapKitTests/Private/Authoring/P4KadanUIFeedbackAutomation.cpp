#if P4_MAPKIT_TESTS
#include "P4KadanEditor.h"
#include "P4KadanImport.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanImportFeedbackTest, "P4MapKit.Kadan.ImportFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanImportFeedbackTest::RunTest(const FString&)
{
    FP4KadanImportResult Result;
    Result.Ok = true;
    Result.Imported.Add(TEXT("/Game/Maps/Example"), TEXT("World"));
    Result.KeptAsTheGames.Add(TEXT("/Game/Actors/UnsupportedBlueprint"));
    Result.ReferenceOnly.Add(TEXT("/Game/Actors/UnsupportedBlueprint"));
    Result.Warnings.Add(TEXT("The Blueprint remains the game's original reference."));
    FString Message = P4KadanImportCompletionMessage(Result);
    TestTrue(TEXT("The map is reported imported even when its Blueprint stays in the game"), Message.StartsWith(TEXT("Kadan imported 1 package.")));
    TestTrue(TEXT("Kept references are explicitly uneditable here"), Message.Contains(TEXT("stay in the game and cannot be edited here")));
    TestTrue(TEXT("Successful imports with reference notes stay successful"), Message.Contains(TEXT("import finished with notes")));
    TestFalse(TEXT("An unimported original reference is not counted as an imported asset"), Message.Contains(TEXT("imported asset")));
    TestFalse(TEXT("The UI never claims dependent maps were left out"), Message.Contains(TEXT("left out")));
    Result.KeptAsTheGames.Add(TEXT("/Game/Actors/UnsupportedBlueprint.UnsupportedBlueprint"));
    TestTrue(TEXT("One asset named by package and by object path counts once"),
        P4KadanImportCompletionMessage(Result).Contains(TEXT(" 1 reference uses the game's original assets.")));
    TestEqual(TEXT("An object path names the same package as its package path"),
        P4KadanNotePackage(TEXT("/Game/Actors/UnsupportedBlueprint.UnsupportedBlueprint")), FString(TEXT("/Game/Actors/UnsupportedBlueprint")));

    Result.Imported.Add(TEXT("/Game/Assets/ReferenceMesh"), TEXT("StaticMesh"));
    Result.ReferenceOnly.Add(TEXT("/Game/Assets/ReferenceMesh"));
    Message = P4KadanImportCompletionMessage(Result);
    TestTrue(TEXT("Imported reference-only content is counted separately"), Message.Contains(TEXT("1 imported asset is reference-only")));
    Result.Cancelled = true;
    TestTrue(TEXT("Cancellation takes precedence over otherwise successful partial work"),
        P4KadanImportCompletionMessage(Result).StartsWith(TEXT("The import was canceled. Completed assets are kept. Click Import to continue.")));
    Result = FP4KadanImportResult();
    Result.Problems.Add(TEXT("Source file could not be read."));
    TestTrue(TEXT("Fatal failure never claims an import succeeded"), P4KadanImportCompletionMessage(Result).StartsWith(TEXT("No new packages were imported.")));
    TestTrue(TEXT("A failed import names the problem that stopped it"),
        P4KadanImportCompletionMessage(Result).Contains(TEXT("No new packages were imported. Source file could not be read.")));
    TestFalse(TEXT("The notice no longer points at notes below it"), P4KadanImportCompletionMessage(Result).Contains(TEXT("below")));
    Result.Problems.Add(TEXT("Second problem."));
    TestTrue(TEXT("Further problems are left to the Output Log"),
        P4KadanImportCompletionMessage(Result).Contains(TEXT("1 more issue is in the Output Log.")));
    Result.Ok = true;
    Result.Problems.Reset();
    TestTrue(TEXT("A completed resume with no new packages is clearly described"), P4KadanImportCompletionMessage(Result).StartsWith(TEXT("Import finished. No new packages were imported.")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanStayedInGameReasonTest, "P4MapKit.Kadan.StayedInGameReason",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanStayedInGameReasonTest::RunTest(const FString&)
{
    // The shape of a fresh-project import of the lit translucent donor without repak (2026-10-18).
    const FString Instance = TEXT("/Game/Carrot4/Characters/Teki/Dodoro/MI_chr_Dodoro_00_M02_LODAll");
    const FString Dither = TEXT("/Game/Carrot4/MasterMaterial/chr/MI_chr_Teki_Dodoro_Translucent_00_Dither");
    const FString Master = TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Teki_Dodoro_Translucent_00_Master");
    const FString Repak = TEXT("Kadan needs repak.exe to read the game's shader library, and no repak is set. Kadan looks for it in its Tools folder.");
    FP4KadanImportResult Result;
    Result.GameReferenceReasons.Add(Instance, TEXT("Not imported because ") + Dither + TEXT(" did not import."));
    Result.GameReferenceReasons.Add(Dither, TEXT("Not imported because ") + Master + TEXT(" did not import. Canonical header/export ranges are verified."));
    Result.GameReferenceReasons.Add(Master, Repak);
    TestEqual(TEXT("The chain is followed to the package refused for its own reason"), P4KadanStayedInGameReason(Result, Instance),
        TEXT("M_chr_Teki_Dodoro_Translucent_00_Master stayed in the game. ") + Repak);
    TestEqual(TEXT("A package refused for its own reason gives that reason alone"), P4KadanStayedInGameReason(Result, Master), Repak);
    TestTrue(TEXT("A package with no recorded reason gives nothing"), P4KadanStayedInGameReason(Result, TEXT("/Game/Other")).IsEmpty());
    Result.GameReferenceReasons.Add(Master, TEXT("Not imported because ") + Instance + TEXT(" did not import."));
    TestTrue(TEXT("A cycle of reasons ends without a reason"), P4KadanStayedInGameReason(Result, Instance).IsEmpty());
    return !HasAnyErrors();
}
#endif

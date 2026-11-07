#if P4_MAPKIT_TESTS
#include "P4AuthoringRows.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanScriptImportPolicy,
    "P4MapKit.Kadan.ScriptImportUsesPanelPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanScriptImportPolicy::RunTest(const FString&)
{
    // Reject before reading game inputs. The report still identifies the actual settings passed
    // to the importer, so this checks the script/panel contract without a game fixture or cook.
    const FString Report = FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json");
    TArray<uint8> Before;
    const bool HadReport = FFileHelper::LoadFileToArray(Before, *Report, FILEREAD_Silent);
    ON_SCOPE_EXIT
    {
        if (HadReport) FFileHelper::SaveArrayToFile(Before, *Report);
        else IFileManager::Get().Delete(*Report);
    };
    const FString Missing = FPaths::ProjectSavedDir() / TEXT("Kadan/ScriptPolicyAbsent") /
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Reply = UP4AuthoringTools::NativeKadanImportMapFromContainers(Missing,
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P"), FString(), FString());
    TSharedPtr<FJsonObject> Json;
    if (!TestTrue(TEXT("The script import retains its failure report"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Reply), Json) && Json.IsValid())) return false;
    bool BestEffort = false, Ok = true;
    TestTrue(TEXT("Scripted map import uses the panel's game-backed-level policy"),
        Json->TryGetBoolField(TEXT("allow_game_backed_levels"), BestEffort) && BestEffort);
    TestTrue(TEXT("Missing source is still refused, not reported as a successful import"),
        Json->TryGetBoolField(TEXT("ok"), Ok) && !Ok);
    return !HasAnyErrors();
}
#endif

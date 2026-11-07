// Private, explicitly requested repair of one controlled survey layer. Current saved hashes detect
// concurrent writes; only a full actual-game cook comparison can authorize a repaired baseline.
#include "P4LevelContent.h"
#include "P4KadanTestSeams.h"
#include "P4OriginalLevelImport.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedPlayerDefaultsTest, "P4MapKit.NativeImport.RepairRetainedPlayerDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedPlayerDefaultsTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FString ManifestFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PLAYER_REPAIR"));
    FString Text, Format, Header, Body, SourceDigest, SavedDigest;
    double Version = 0;
    TSharedPtr<FJsonObject> Manifest;
    if (!TestTrue(TEXT("The explicit private repair manifest has its exact format/version and digests"),
        !ManifestFile.IsEmpty() && IFileManager::Get().FileSize(*ManifestFile) >= 0 &&
        IFileManager::Get().FileSize(*ManifestFile) <= 64 * 1024 && FFileHelper::LoadFileToString(Text, *ManifestFile) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Manifest) && Manifest.IsValid() &&
        Manifest->TryGetStringField(TEXT("format"), Format) && Format == TEXT("p4-retained-player-defaults-repair") &&
        Manifest->TryGetNumberField(TEXT("version"), Version) && Version == 1 &&
        Manifest->TryGetStringField(TEXT("header"), Header) && Manifest->TryGetStringField(TEXT("body"), Body) &&
        Manifest->TryGetStringField(TEXT("source_sha256"), SourceDigest) && SourceDigest.Len() == 64 &&
        Manifest->TryGetStringField(TEXT("saved_sha256"), SavedDigest) && SavedDigest.Len() == 64)) return false;
    AddInfo(TEXT("Saved SHA256 is a race snapshot, not historical proof that this layer was never edited. "
        "The exact actual-game comparison must pass before any saved file or baseline can change."));
    const auto Result = P4RepairRetainedPlayerDefaultsForTest(Header, Body, SourceDigest, SavedDigest);
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("retained-player-defaults-repair.json");
    TestTrue(TEXT("The complete comparison receipt is retained"), FFileHelper::SaveStringToFile(
        P4OriginalLevelImportJson(Result), *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(TEXT("Full repair/comparison receipt: ") + Receipt);
    if (!Result.IsValid()) AddError(Result.ErrorDetail);
    TestTrue(TEXT("The exact original Player layer passed its complete actual-game cook comparison"),
        Result.IsValid() && Result.DifferencesKnown && Result.ComparedKnown && Result.Compared > 0 && Result.Differences == 0);
    TestTrue(TEXT("Only the proven layer was saved with its corrected baseline"), Result.IsValid() && Result.Saved);
    return !HasAnyErrors();
}
#endif

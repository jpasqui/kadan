#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4LevelChanges.h"
#include "P4LevelContent.h"
#include "P4OriginalLevelImport.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PartialLayerUpgradeTest, "P4MapKit.Kadan.UpgradeSelectedPartialLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PartialLayerUpgradeTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_UPGRADE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned round-two partial-layer fixture."));
        return true;
    }
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_REPAK"));
    Settings.AllowGameBackedLevels = true;
    if (!TestTrue(TEXT("Original game sources and tools are explicitly supplied"),
            !Settings.GameFolder.IsEmpty() && !Settings.GamePaks.IsEmpty() && FPaths::FileExists(Settings.Retoc) &&
                FPaths::FileExists(Settings.Repak)))
        return false;
    TArray<FString> Layers = {TEXT("Cave009/Cave009_F00/Sublevels/Cave009_F00_Objects"),
        TEXT("Cave013/Cave013_F02/Sublevels/Cave013_F02_Objects"),
        TEXT("Cave014/Cave014_F03/Sublevels/Cave014_F03_SoundOption"),
        TEXT("Cave014/Cave014_F03/Sublevels/Cave014_F03_Objects"),
        TEXT("Cave014/Cave014_F03/Sublevels/Cave014_F03_Teki"),
        TEXT("Cave016/Cave016_F15/Sublevels/Cave016_F15_GameArea"),
        TEXT("Cave018/Cave018_F00/Sublevels/Cave018_F00_GameArea"),
        TEXT("Cave023/Cave023_F04/Sublevels/Cave023_F04_Objects"),
        TEXT("Cave024/Cave024_F03/Sublevels/Cave024_F03_Objects"),
        TEXT("Cave026/Cave026_F01/Sublevels/Cave026_F01_GameArea"),
        TEXT("Cave026/Cave026_F02/Sublevels/Cave026_F02_GameArea"),
        TEXT("Cave035/Cave035_F03/Sublevels/Cave035_F03_Teki"), TEXT("Cave035/Cave035_F09/Sublevels/Cave035_F09_Teki"),
        TEXT("/Game/Carrot4/Maps/Main/Area/Area002/Sublevels/Area002_Objects"),
        TEXT("/Game/Carrot4/Maps/Main/Area/Area500/Sublevels/Area500_Hero_Objects"),
        TEXT("/Game/Carrot4/Maps/Main/Area/Area500/Sublevels/Area500_Objects"),
        TEXT("/Game/Carrot4/Maps/Main/Area/Area010/Sublevels/Area010_GameArea"),
        TEXT("/Game/Carrot4/Maps/Madori/Ddb/DDB_AI003/Sublevels/DDB_AI003_Landscape"),
        TEXT("/Game/Carrot4/Maps/Main/Area/Area002/Sublevels/Area002_Hero_Objects")};
    const FString Selection = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_UPGRADE_SELECTION"));
    if (!Selection.IsEmpty())
    {
        FString Text;
        TArray<TSharedPtr<FJsonValue>> Packages;
        if (!TestTrue(TEXT("The explicit original-layer selection is readable and bounded"),
                FFileHelper::LoadFileToString(Text, *Selection) &&
                    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Packages) && Packages.Num() > 0 &&
                    Packages.Num() <= 64))
            return false;
        Layers.Reset();
        for (const auto& Package : Packages)
        {
            const FString Name = Package.IsValid() ? Package->AsString() : FString();
            if (!TestTrue(TEXT("Each selected original layer is an explicit game package"),
                    Name.StartsWith(TEXT("/Game/")) && FPackageName::IsValidLongPackageName(Name)))
                return false;
            Layers.Add(Name);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Measurements;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    for (const FString& Layer : Layers)
    {
        const FString Only = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_UPGRADE_ONLY"));
        if (!Only.IsEmpty() && !Layer.EndsWith(Only))
            continue;
        const FString Package = Layer.StartsWith(TEXT("/Game/")) ? Layer
                                                                 : TEXT("/Game/Carrot4/Maps/Madori/Cave/") + Layer;
        const double Began = FPlatformTime::Seconds();
        auto Plan = P4KadanPlanImport(Settings, TSet<FString>{Package}, false);
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_UPGRADE_LEVELS_ONLY")) == TEXT("1"))
            Plan.Assets.RemoveAll(
                [&](const TSharedPtr<FP4KadanSourceAsset>& Asset) { return !Asset || Asset->Package != Package; });
        UWorld* Existing = LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
        TSet<FName> ExistingOmitted;
        FString ExistingSource, ExistingWhy;
        const bool ExistingPartial = Existing &&
            P4ReadGameBackedLevelCustody(Existing, ExistingOmitted, ExistingSource, ExistingWhy);
        if (Existing && !ExistingPartial)
        {
            bool Verified = false;
            for (const auto& Asset : Plan.Assets)
                if (Asset && Asset->Package == Package)
                {
                    const auto Resumed = P4ResumeOriginalLevelNative(Existing, Asset->Header, Asset->Body);
                    TSharedPtr<FJsonObject> Status;
                    bool Unchanged = false;
                    Verified = Resumed.IsValid() &&
                        FJsonSerializer::Deserialize(
                            TJsonReaderFactory<>::Create(P4LevelChangesJson(P4AnalyzeOriginalLevelChanges(Existing))),
                            Status) &&
                        Status.IsValid() && Status->TryGetBoolField(TEXT("unchanged"), Unchanged) && Unchanged;
                }
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("package"), Package);
            Row->SetBoolField(TEXT("already_complete_unchanged"), Verified);
            Row->SetBoolField(TEXT("ok"), Verified);
            Row->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Began);
            Measurements.Add(MakeShared<FJsonValueObject>(Row));
            TestTrue(
                TEXT("An already completed layer resumes against the current source and cold baseline: ") + Package,
                Verified);
            continue;
        }
        // RunImport reacquires its selected dependency closure. Resume keeps
        // retained dependencies while the partial world is reconstructed.
        const bool RetainDependencies = FPlatformMisc::GetEnvironmentVariable(
                                            TEXT("P4_NATIVE_PARTIAL_UPGRADE_LEVELS_ONLY")) == TEXT("1");
        const auto Imported = P4KadanRunImport(Settings, Plan, RetainDependencies);
        FP4OriginalLevelImportResult Rebuilt;
        if (RetainDependencies && ExistingPartial && Imported.Imported.Contains(Package))
        {
            TSet<FName> Unavailable;
            for (const auto& Refusal : Imported.GameReferenceReasons)
                Unavailable.Add(FName(*Refusal.Key));
            for (const auto& Asset : Plan.Assets)
                if (Asset && Asset->Package == Package)
                    Rebuilt = P4ReimportOriginalLevelWithGameReferencesNative(
                        Existing, Asset->Header, Asset->Body, true, Unavailable);
            TestTrue(TEXT("The selected partial world is explicitly reconstructed without rebuilding dependencies"),
                Rebuilt.IsValid() && Rebuilt.Saved);
        }
        auto Row = MakeShared<FJsonObject>();
        if (RetainDependencies && ExistingPartial)
        {
            TSharedPtr<FJsonObject> Native;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P4OriginalLevelImportJson(Rebuilt)), Native))
                Row->SetObjectField(TEXT("explicit_world_reconstruction"), Native);
        }
        Row->SetStringField(TEXT("package"), Package);
        Row->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Began);
        Row->SetBoolField(TEXT("ok"), Imported.Ok && !Imported.Cancelled);
        TArray<TSharedPtr<FJsonValue>> Problems;
        for (const FString& Problem : Imported.Problems)
            Problems.Add(MakeShared<FJsonValueString>(Problem));
        Row->SetArrayField(TEXT("problems"), Problems);
        TSharedPtr<FJsonObject> Report;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Imported.Report), Report) && Report.IsValid())
        {
            const auto Assets = Report->GetObjectField(TEXT("assets"));
            if (Assets->HasField(Package))
                Row->SetObjectField(TEXT("level"), Assets->GetObjectField(Package));
            TArray<TSharedPtr<FJsonValue>> Refusals;
            for (const auto& Asset : Assets->Values)
            {
                const auto Value = Asset.Value->AsObject();
                bool Valid = false;
                if (Value.IsValid() && Value->TryGetBoolField(TEXT("valid"), Valid) && !Valid)
                {
                    auto Refusal = MakeShared<FJsonObject>();
                    Refusal->SetStringField(TEXT("package"), Asset.Key);
                    Refusal->SetObjectField(TEXT("reply"), Value);
                    Refusals.Add(MakeShared<FJsonValueObject>(Refusal));
                }
            }
            Row->SetArrayField(TEXT("refusals"), Refusals);
        }
        UWorld* World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
        TSet<FName> Omitted;
        FString Source, Why;
        const bool Partial = World && P4ReadGameBackedLevelCustody(World, Omitted, Source, Why);
        Row->SetBoolField(TEXT("game_backed"), Partial);
        Row->SetNumberField(TEXT("omitted_actors"), Omitted.Num());
        Row->SetBoolField(TEXT("world_exists"), World != nullptr);
        if (World && !Imported.Imported.Contains(Package))
        {
            TSharedPtr<FJsonObject> Delta;
            if (FJsonSerializer::Deserialize(
                    TJsonReaderFactory<>::Create(P4LevelChangesJson(P4AnalyzeOriginalLevelChanges(World))), Delta) &&
                Delta.IsValid())
                Row->SetObjectField(TEXT("failed_open_delta"), Delta);
        }
        Measurements.Add(MakeShared<FJsonValueObject>(Row));
        auto Out = MakeShared<FJsonObject>();
        Out->SetArrayField(TEXT("layers"), Measurements);
        FString Json;
        FJsonSerializer::Serialize(Out, TJsonWriterFactory<>::Create(&Json));
        if (!TestTrue(TEXT("Each actual source upgrade records its result"),
                FFileHelper::SaveStringToFile(Json, *(Folder / TEXT("PartialUpgrades.json")),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
            return false;
        AddInfo(FString::Printf(TEXT("Partial layer %s: %.1fs; saved source=%d; remaining game-owned actors=%d"),
            *Package, FPlatformTime::Seconds() - Began, Imported.Imported.Contains(Package), Omitted.Num()));
        const FString ExpectedText = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_EXPECT_GAME_OWNED_ACTORS"));
        if (ExpectedText.IsEmpty())
            TestTrue(TEXT("The original layer is reconstructed by the normal native pipeline: ") + Package,
                World && !Partial && Imported.Imported.Contains(Package));
        else
            TestTrue(TEXT("The reconstructed world retains no more than the explicitly expected game-owned actors: ") +
                    Package,
                World && Imported.Imported.Contains(Package) && Omitted.Num() <= FCString::Atoi(*ExpectedText) &&
                    (Omitted.Num() == 0 || Partial));
    }
    auto Out = MakeShared<FJsonObject>();
    Out->SetArrayField(TEXT("layers"), Measurements);
    FString Json;
    FJsonSerializer::Serialize(Out, TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("Completed and resumed layer results are retained"),
        FFileHelper::SaveStringToFile(
            Json, *(Folder / TEXT("PartialUpgrades.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("The requested original-layer fixture selected at least one actual layer"), Measurements.Num() > 0);
    return !HasAnyErrors();
}
#endif

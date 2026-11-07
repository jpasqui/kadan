#include "CoreMinimal.h"
#include "P4KadanTestSeams.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanImport.h"
#include "P4OriginalClassComparison.h"
#include "P4OriginalLevelImport.h"
#include "P4LevelChanges.h"
#include "P4OriginalRecord.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "Components/PostProcessComponent.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/MetaData.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "FileHelpers.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeClassEnumValues, "P4MapKit.NativeImport.ClassEnumCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeClassEnumValues::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FStructProperty* Settings = FindFProperty<FStructProperty>(
        UPostProcessComponent::StaticClass(), TEXT("Settings"));
    const FByteProperty* Method = Settings ? FindFProperty<FByteProperty>(Settings->Struct, TEXT("DepthOfFieldMethod"))
                                           : nullptr;
    if (!TestTrue(TEXT("The installed deprecated depth-of-field schema and original enum are available"),
            Method && Method->Enum))
        return false;
    FString Kept;
    int32 Count = 0;
    const FString Known = TEXT("{\"Settings\":{\"DepthOfFieldMethod\":\"") + Method->Enum->GetNameStringByIndex(0) +
        TEXT("\"}}");
    TestTrue(TEXT("A known native choice decodes"),
        P4LevelContent::P4UnheldClassValuesForTest(UPostProcessComponent::StaticClass(), Known, Kept, Count));
    TestEqual(TEXT("Known choices remain completely compared"), Kept, Known);
    TestEqual(TEXT("Known choices count no unsupported values"), Count, 0);
    const FString Missing = TEXT(
        "{\"Settings\":{\"DepthOfFieldMethod\":\"P4MissingNativeChoice\",\"BloomIntensity\":3.0}}");
    TestTrue(TEXT("The actual struct's unsupported named choice decodes"),
        P4LevelContent::P4UnheldClassValuesForTest(UPostProcessComponent::StaticClass(), Missing, Kept, Count));
    TestEqual(TEXT("Exactly one unsupported choice is reported"), Count, 1);
    TestEqual(TEXT("Independent authored values remain exact"), Kept,
        FString(TEXT("{\"Settings\":{\"BloomIntensity\":3.0}}")));
    const FString Numeric = TEXT("{\"Settings\":{\"DepthOfFieldMethod\":999}}");
    TestTrue(TEXT("A numeric source control decodes"),
        P4LevelContent::P4UnheldClassValuesForTest(UPostProcessComponent::StaticClass(), Numeric, Kept, Count));
    TestEqual(TEXT("The compatibility rule never excuses arbitrary numeric data"), Kept, Numeric);
    TestEqual(TEXT("A numeric control is still compared"), Count, 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StrictNativeClassRoundTrip, "P4MapKit.NativeImport.StrictClassRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StrictNativeClassRoundTrip::RunTest(const FString&)
{
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_STRICT_CLASS"));
    if (Header.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in: P4_NATIVE_STRICT_CLASS names an actual original Blueprint source."));
        return true;
    }
    const auto Result = P4CompareOriginalClassNative(Header, FPaths::ChangeExtension(Header, TEXT("uexp")));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StrictNative");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TSharedPtr<FJsonObject> Report;
    if (!FJsonSerializer::Deserialize(
            TJsonReaderFactory<TCHAR>::Create(P4OriginalClassComparisonJson(Result)), Report) ||
        !Report)
        return false;
    Report->SetBoolField(TEXT("defaults_readable"), Result.DefaultsReadable);
    TArray<TSharedPtr<FJsonValue>> Unreadable;
    for (const FString& Node : Result.UnreadableDefaultNodes)
        Unreadable.Add(MakeShared<FJsonValueString>(Node));
    Report->SetArrayField(TEXT("unreadable_default_nodes"), Unreadable);
    FString Text;
    FJsonSerializer::Serialize(Report.ToSharedRef(), TJsonWriterFactory<TCHAR>::Create(&Text));
    TestTrue(TEXT("Actual class comparison is retained"),
        FFileHelper::SaveStringToFile(Text, *(Folder / (FPaths::GetBaseFilename(Header) + TEXT(".class.json"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("The actual original class is readable: ") + Result.ErrorDetail, Result.IsValid());
    TestTrue(TEXT("Every native default payload is understood"), Result.DefaultsReadable);
    TestTrue(TEXT("Cooked default properties and object families match the original"), Result.Same);
    for (const FString& Node : Result.UnreadableDefaultNodes)
        AddInfo(Node);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StrictNativePartialRefusal, "P4MapKit.NativeImport.StrictPartialRefusal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StrictNativePartialRefusal::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_LEVEL_HEADER"));
    if (Header.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Opt-in: a retained source level with a free destination is required for strict partial-layer refusal."));
        return true;
    }
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Actual original level sources scan"),
            P4KadanScanGame(FPaths::GetPath(Header), Sources, Problems) && Problems.Num() == 0))
        return false;
    const auto* Source = Sources.FindByPredicate(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Row) { return FPaths::IsSamePath(Row->Header, Header); });
    if (!TestTrue(TEXT("Requested source is an actual world"), Source && (*Source)->Kind == TEXT("World")))
        return false;
    if (!TestFalse(TEXT("The isolated partial-layer destination is free"),
            FPackageName::DoesPackageExist((*Source)->Package) || FindPackage(nullptr, *(*Source)->Package)))
        return false;
    const auto Imported = P4ImportGameBackedLevelNative(
        Header, (*Source)->Body, false, {}, TEXT("Controlled strict-native refusal fixture"), false);
    UWorld* World = Imported.Asset;
    TStrongObjectPtr<UWorld> Hold(World);
    ON_SCOPE_EXIT
    {
        Hold.Reset();
        P4DiscardOriginalFamilyProbeForTest(World);
    };
    if (!TestTrue(TEXT("Actual source creates an unsaved partial layer"),
            Imported.IsValid() && World && Imported.GameBacked && !Imported.Saved))
        return false;
    const FString Custody = World->GetOutermost()->GetMetaData()->GetValue(World, TEXT("P4GameBackedLevelCustody"));
    const bool Dirty = World->GetOutermost()->IsDirty();
    const auto Direct = P4ImportOriginalLevelNative(Header, (*Source)->Body, false);
    TestFalse(TEXT("Direct complete-native import cannot reuse a partial baseline"), Direct.IsValid());
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPaths::GetPath(Header);
    Settings.AllowGameBackedLevels = false;
    FP4KadanImportPlan Plan;
    Plan.Assets.Add(*Source);
    Plan.Scanned = Sources;
    for (const bool Resume : {false, true})
    {
        const auto Result = P4KadanRunImport(Settings, Plan, Resume);
        TestFalse(TEXT("Strict native caller refuses partial layers with either resume policy"), Result.Ok);
        TestTrue(TEXT("The exact partial layer remains in its original package"),
            UWorld::FindWorldInPackage(World->GetOutermost()) == World);
        TestEqual(TEXT("Source custody remains byte-identical after refusal"),
            FString(World->GetOutermost()->GetMetaData()->GetValue(World, TEXT("P4GameBackedLevelCustody"))), Custody);
        TestEqual(TEXT("Refusal does not dirty or save the partial map"), World->GetOutermost()->IsDirty(), Dirty);
        TestFalse(TEXT("No refusal publishes a source map file"), FPackageName::DoesPackageExist((*Source)->Package));
        const auto Changes = P4AnalyzeOriginalLevelChanges(World);
        TestTrue(TEXT("Refusal preserves the exact editable baseline"), Changes.IsValid() && Changes.Unchanged);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StrictNativeMapRoundTrip, "P4MapKit.NativeImport.StrictMapRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StrictNativeMapRoundTrip::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_STRICT_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in: P4_NATIVE_STRICT_MAP names a source level absent from this owned test project."));
        return true;
    }
    if (!TestFalse(TEXT("A strict native proof starts without the selected level"),
            FPackageName::DoesPackageExist(Map) || FindPackage(nullptr, *Map) != nullptr))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    Settings.AllowGameBackedLevels = false;
    if (!TestTrue(TEXT("Actual source inputs exist"),
            !Settings.GameFolder.IsEmpty() && IFileManager::Get().DirectoryExists(*Settings.GameFolder)))
        return false;
    const auto Result = P4KadanImportMap(Settings, Map, true);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StrictNative");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TestTrue(TEXT("The strict import report is retained"),
        FFileHelper::SaveStringToFile(Result.Report,
            *(Folder / (FPackageName::GetShortName(Map) + TEXT(".import.json"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TSharedPtr<FJsonObject> Report;
    if (!TestTrue(TEXT("The actual import report is valid JSON"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Result.Report), Report) && Report))
        return false;
    TestFalse(
        TEXT("Partial map fallback was explicitly disabled"), Report->GetBoolField(TEXT("allow_game_backed_levels")));
    const auto* Assets = Report->GetObjectField(TEXT("assets"))->Values.Find(Map);
    if (!TestNotNull(TEXT("The selected source level has an actual import reply"), Assets))
        return false;
    const auto Reply = (*Assets)->AsObject();
    TestFalse(
        TEXT("No partial editor projection can satisfy native acceptance"), Reply->GetBoolField(TEXT("game_backed")));
    TestTrue(TEXT("Normal native map import succeeds"), Result.Ok && Reply->GetBoolField(TEXT("valid")));
    for (const FString& Problem : Result.Problems)
        AddInfo(Problem);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StrictNativeMapColdReload, "P4MapKit.NativeImport.StrictMapColdReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StrictNativeMapColdReload::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_STRICT_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Opt-in: reopen the complete native source map produced by StrictMapRoundTrip in a separate editor process."));
        return true;
    }
    if (!TestTrue(TEXT("An actual saved native map is available"), FPackageName::DoesPackageExist(Map)))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The ordinary editor loader cold-opens the native map"), World))
        return false;
    FP4OriginalRecord Record;
    FString Error;
    if (!TestTrue(TEXT("Cold reload retains complete-native source identity"),
            P4ReadOriginalRecord(World, Record, Error) && Record.Kind == TEXT("level") &&
                Record.Fidelity == TEXT("exact") && Record.ComparisonSchema == TEXT("level-lines-v1")))
        return false;
    const auto Before = P4AnalyzeOriginalLevelChanges(World);
    TestTrue(TEXT("Cold map reload preserves the exact editable baseline: ") + Before.ErrorDetail,
        Before.IsValid() && Before.Unchanged);
    const TArray<AActor*> Actors = World->PersistentLevel->Actors;
    for (AActor* Actor : Actors)
        if (IsValid(Actor) && !Actor->IsChildActor() && !Actor->IsEditorOnly())
            Actor->RerunConstructionScripts();
    const auto After = P4AnalyzeOriginalLevelChanges(World);
    TestTrue(
        TEXT("Ordinary constructor replay preserves original component aliases and baseline: ") + After.ErrorDetail,
        After.IsValid() && After.Unchanged);
    return !HasAnyErrors();
}
#endif

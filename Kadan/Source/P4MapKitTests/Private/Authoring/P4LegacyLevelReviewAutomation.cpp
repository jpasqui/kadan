#if P4_MAPKIT_TESTS
#include "P4ExportNavigation.h"
#include "P4KadanSaveMod.h"
#include "P4LevelChanges.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LegacyLevelReview,
    "P4MapKit.Export.LegacyLevelReview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4LegacyLevelReview::RunTest(const FString&)
{
    const FString Folder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_LEGACY_LEVEL_FOLDER"));
    const FString ReportFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_LEGACY_REVIEW_REPORT"));
    const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_ROUND_C_GAME_PAKS"));
    const FString Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    const FString Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (!TestTrue(TEXT("The legacy review inputs are configured"),
        !Folder.IsEmpty() && !ReportFile.IsEmpty() && !GamePaks.IsEmpty() &&
        !Retoc.IsEmpty() && !Repak.IsEmpty()))
    {
        return false;
    }

    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.umap"), true, false);
    Files.Sort();
    if (!TestTrue(TEXT("The released importer saved original levels"), Files.Num() > 0))
    {
        return false;
    }

    TArray<FP4ExportRoot> Roots;
    TArray<TSharedPtr<FJsonValue>> Levels;
    for (const FString& File : Files)
    {
        FString Package = File;
        if (!FPaths::MakePathRelativeTo(Package, *FPaths::ProjectContentDir()))
        {
            AddError(TEXT("A level is outside this project's Content folder."));
            return false;
        }
        Package = TEXT("/Game/") + Package.LeftChop(5);
        Package.ReplaceInline(TEXT("\\"), TEXT("/"));
        UWorld* World = LoadObject<UWorld>(nullptr,
            *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
        if (!TestNotNull(TEXT("A released original level loads. ") + Package, World))
        {
            return false;
        }
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(World);
        TSharedRef<FJsonObject> Level = MakeShared<FJsonObject>();
        Level->SetStringField(TEXT("package"), Package);
        Level->SetBoolField(TEXT("valid"), Changes.IsValid());
        Level->SetBoolField(TEXT("unchanged"), Changes.Unchanged);
        Level->SetStringField(TEXT("error"), Changes.ErrorDetail);
        FString NavigationWhy;
        Level->SetBoolField(TEXT("navigation"),
            Changes.IsValid() && P4EditChangesNavigation(Changes, NavigationWhy));
        Level->SetStringField(TEXT("navigation_why"), NavigationWhy);
        TSharedPtr<FJsonObject> Lines;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P4LevelChangesJson(Changes)), Lines);
        if (Lines.IsValid())
        {
            Level->SetObjectField(TEXT("lines"), Lines.ToSharedRef());
        }
        Levels.Add(MakeShared<FJsonValueObject>(Level));
        FP4ExportRoot Root;
        Root.Kind = EP4ExportRootKind::Package;
        Root.Path = Package;
        Roots.Add(Root);
    }

    FP4ExportModRequest Request;
    Request.Mod.Id = TEXT("legacy.level.review");
    Request.Mod.Name = TEXT("Legacy level review");
    Request.Mod.Version = TEXT("1.0.0");
    Request.ProjectDirectory = FPaths::ProjectDir();
    Request.ExtractedGameDirectory = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    Request.GamePaksDirectory = GamePaks;
    Request.OutputDirectory = FPaths::ProjectSavedDir() / TEXT("Kadan/LegacyReviewOutput");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("Kadan/LegacyReviewWork");
    Request.Tools.Retoc = Retoc;
    Request.Tools.Repak = Repak;
    Request.Roots = Roots;
    FP4KadanModReview Review;
    FString Error;
    const bool Reviewed = P4KadanReviewMod(Request, nullptr, Review, Error);
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("reviewed"), Reviewed);
    Report->SetStringField(TEXT("error"), Error);
    Report->SetArrayField(TEXT("levels"), Levels);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FP4ExportSelectionRow& Row : Review.Creator.Rows)
    {
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("package"), Row.Package);
        Entry->SetStringField(TEXT("reason"), Row.Reason);
        Entry->SetNumberField(TEXT("disposition"), int32(Row.Disposition));
        Entry->SetNumberField(TEXT("issue"), int32(Row.Issue));
        Rows.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("rows"), Rows);
    TArray<TSharedPtr<FJsonValue>> Blockers;
    for (const FString& Blocker : Review.Creator.Blockers)
    {
        Blockers.Add(MakeShared<FJsonValueString>(Blocker));
    }
    Report->SetArrayField(TEXT("blockers"), Blockers);
    FString Serialized;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Serialized));
    if (!TestTrue(TEXT("The legacy export review writes its report"),
        FFileHelper::SaveStringToFile(Serialized, *ReportFile)))
    {
        return false;
    }
    TestTrue(TEXT("The released project can run the current export review"), Reviewed);
    return !HasAnyErrors();
}
#endif

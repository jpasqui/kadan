#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "P4AuthoringRows.h"
#include "P4ExportDriver.h"
#include "P4KadanSaveMod.h"
#include "Serialization/JsonSerializer.h"

FString UP4AuthoringTools::NativeExportMod(const FString& ModId, const FString& Roots, const FString& MapPackage,
    const FString& GamePaks, const FString& OutputDirectory, const FString& WorkDirectory, const FString& Retoc,
    const FString& Repak, const FString& GameFiles)
{
    FP4ExportModRequest Request;
    FP4ExportModResult Result;
    FString Error;
    Request.ProjectDirectory = FPaths::ProjectDir();
    Request.GamePaksDirectory = GamePaks;
    Request.OutputDirectory = OutputDirectory;
    Request.WorkDirectory = WorkDirectory;
    Request.MapPackage = MapPackage;
    Request.ExtractedGameDirectory = GameFiles;
    Request.Tools.Retoc = Retoc;
    Request.Tools.Repak = Repak;
    bool Published = false;
    if (P4ReadModDescriptor(Request.ProjectDirectory, ModId, Request.Mod, Error))
    {
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FString> Paths;
        Roots.ParseIntoArray(Paths, TEXT(","), true);
        for (FString Path : Paths)
        {
            Path.TrimStartAndEndInline();
            TArray<FAssetData> Assets;
            Registry.GetAssetsByPackageName(FName(*Path), Assets);
            FP4ExportRoot Root;
            Root.Path = Path;
            Root.Kind = Assets.Num() ? EP4ExportRootKind::Package : EP4ExportRootKind::Folder;
            Request.Roots.Add(Root);
        }
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        Published = P4KadanExportMod(Request, World, Result, Error);
    }
    const auto Strings = [](const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const FString& Value : Values)
            Out.Add(MakeShared<FJsonValueString>(Value));
        return Out;
    };
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("valid"), Published);
    Json->SetStringField(TEXT("error"), Error);
    Json->SetBoolField(TEXT("published"), Result.Published);
    Json->SetStringField(TEXT("directory"), Result.Directory);
    Json->SetArrayField(TEXT("packages"), Strings(Result.Packages));
    Json->SetArrayField(TEXT("levels"), Strings(Result.Levels));
    Json->SetArrayField(TEXT("placements"), Strings(Result.Placements));
    Json->SetArrayField(TEXT("rebuilt_lighting"), Strings(Result.RebuiltLighting));
    Json->SetArrayField(TEXT("baked_lighting"), Strings(Result.BakedLighting));
    Json->SetArrayField(TEXT("baked_actors"), Strings(Result.BakedActors));
    Json->SetArrayField(TEXT("unbaked_actors"), Strings(Result.UnbakedActors));
    {
        TArray<TSharedPtr<FJsonValue>> Replaced;
        for (int32 Index = 0; Index < Result.Replacements.Num(); ++Index)
        {
            TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
            One->SetStringField(TEXT("package"), Result.Replacements[Index]);
            One->SetStringField(
                TEXT("file"), Result.ReplacementFiles.IsValidIndex(Index) ? Result.ReplacementFiles[Index] : FString());
            One->SetStringField(
                TEXT("kind"), Result.ReplacementKinds.IsValidIndex(Index) ? Result.ReplacementKinds[Index] : FString());
            One->SetStringField(TEXT("class"),
                Result.ReplacementClasses.IsValidIndex(Index) ? Result.ReplacementClasses[Index] : FString());
            One->SetStringField(TEXT("generated_class"),
                Result.ReplacementGeneratedClasses.IsValidIndex(Index) ? Result.ReplacementGeneratedClasses[Index]
                                                                       : FString());
            One->SetStringField(TEXT("native_parent"),
                Result.ReplacementNativeParents.IsValidIndex(Index) ? Result.ReplacementNativeParents[Index]
                                                                    : FString());
            Replaced.Add(MakeShared<FJsonValueObject>(One));
        }
        Json->SetArrayField(TEXT("replacements"), Replaced);
    }
    {
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (const FP4PlacementRow& Row : Result.PlacementRows)
        {
            TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
            One->SetStringField(TEXT("actor"), Row.Actor);
            One->SetStringField(TEXT("class"), Row.Class);
            One->SetStringField(TEXT("id"), FString::Printf(TEXT("%llu"), Row.Id));
            TArray<FString> Blobs;
            Row.Blobs.GetKeys(Blobs);
            One->SetArrayField(TEXT("blobs"), Strings(Blobs));
            Rows.Add(MakeShared<FJsonValueObject>(One));
        }
        Json->SetArrayField(TEXT("placement_rows"), Rows);
        TArray<TSharedPtr<FJsonValue>> GameRows;
        for (const FP4GameRowEdit& Row : Result.GameRowEdits)
        {
            TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
            One->SetStringField(TEXT("actor"), Row.Actor);
            One->SetStringField(TEXT("list"), Row.List);
            One->SetStringField(TEXT("id"), FString::Printf(TEXT("%llu"), Row.Id));
            One->SetBoolField(TEXT("removed"), Row.Removed);
            GameRows.Add(MakeShared<FJsonValueObject>(One));
        }
        Json->SetArrayField(TEXT("game_row_edits"), GameRows);
    }
    Json->SetArrayField(TEXT("files"), Strings(Result.Files));
    Json->SetArrayField(TEXT("refused"), Strings(Result.Refused));
    Json->SetArrayField(TEXT("told"), Strings(Result.Told));
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

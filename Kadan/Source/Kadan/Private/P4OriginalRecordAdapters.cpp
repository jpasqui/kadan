#include "P4AuthoringRows.h"
#include "P4Common.h"
#include "P4NativeConstructionPreview.h"
#include "P4OriginalRecord.h"
#include "P4OriginalLevelImport.h"
#include "P4LevelContent.h"
#include "P4LevelChanges.h"
#include "P4StaticMeshContent.h"
#include "P4TextureContent.h"
#include "P4SkinnedContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstanceComparison.h"
#include "P4NativeHash.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString Reply(const FP4OriginalStatus& Status, const FString& Error)
{
    auto Json = MakeShared<FJsonObject>();
    P4OriginalStatusFields(Status, *Json);
    Json->SetBoolField(TEXT("valid"), Error.IsEmpty());
    Json->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Json);
    return Out;
}
} //namespace
FP4OriginalDiagnostic P4DiagnoseOriginalAsset(UObject* Asset)
{
    // The asset as it is, without what the editor previews of the game's native classes.
    FP4SuspendNativePreviews Previews;
    FP4OriginalDiagnostic Result;
    Result.Provenance = P4QueryOriginalAsset(Asset);
    if (!Result.Provenance.Error.IsEmpty())
    {
        Result.Error = Result.Provenance.Error;
        Result.Comparison = EP4OriginalComparison::Unreadable;
        return Result;
    }
    if (!Result.Provenance.IsOriginal())
        return Result;
    const FString& Schema = Result.Provenance.Record.ComparisonSchema;
    if (Schema == TEXT("level-lines-v1") || Schema == TEXT("game-backed-level-lines-v1"))
    {
        TSet<FName> Omitted;
        FString Source;
        if (Schema == TEXT("game-backed-level-lines-v1") &&
            !P4ReadGameBackedLevelCustody(Cast<UWorld>(Asset), Omitted, Source, Result.Error))
        {
            Result.Comparison = EP4OriginalComparison::Unreadable;
            return Result;
        }
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(Cast<UWorld>(Asset));
        if (!Changes.IsValid())
        {
            Result.Error = Changes.ErrorDetail.IsEmpty() ? P4LevelChangesErrorText(Changes) : Changes.ErrorDetail;
            Result.Comparison = EP4OriginalComparison::Unreadable;
            return Result;
        }
        Result.CurrentDigest = Changes.State;
        Result.Comparison = Changes.Unchanged ? EP4OriginalComparison::Equal : EP4OriginalComparison::Different;
        return Result;
    }
    else if (Schema == TEXT("object-lines-v1") &&
        Asset->GetClass()->GetPathName() == TEXT("/Script/Engine.AnimSequence"))
    {
        Result.Error = TEXT("original-animation-comparison-needs-source-verification");
        return Result;
    }
    else if (Schema == TEXT("object-lines-v1") || Schema == TEXT("animation-object-lines-v2"))
    {
        if (!P4LegacyEmptyVectorFieldState(Asset, Result.Provenance.Record, Result.CurrentDigest, Result.Error))
        {
            if (!Result.Error.IsEmpty())
            {
                Result.Comparison = EP4OriginalComparison::Unreadable;
                return Result;
            }
            Result.CurrentDigest = P4OriginalObjectAssetState(Asset);
        }
    }
    else if (Schema == TEXT("mesh-state-v1"))
        Result.CurrentDigest = P4StaticMeshState(Cast<UStaticMesh>(Asset));
    else if (Schema == TEXT("texture-state-v1"))
        Result.CurrentDigest = P4TextureState(Cast<UTexture>(Asset));
    else if (Schema == TEXT("material-graph-v2"))
    {
        Result.Error = TEXT("original-material-comparison-baseline-missing");
        return Result;
    }
    else if (Schema == TEXT("material-native-state-v1"))
    {
        // Older recovered graphs have source recipe identity but no native
        // comparison baseline. A diagnostic must not create one from today's
        // potentially edited graph or reinterpret the recipe MD5 as that state.
        const FString& Baseline = Result.Provenance.Record.ComparisonDigest;
        bool Valid = Baseline.Len() == 64;
        for (TCHAR C : Baseline)
            if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f'))))
                Valid = false;
        if (!Valid)
        {
            Result.Error = TEXT("original-material-comparison-baseline-missing");
            return Result;
        }
        Result.CurrentDigest = P4MaterialComparisonState(Cast<UMaterial>(Asset), Result.Error);
        if (!Result.Error.IsEmpty())
        {
            Result.Comparison = EP4OriginalComparison::Unreadable;
            return Result;
        }
    }
    else if (Schema == TEXT("material-instance-native-state-v1"))
    {
        const auto& R = Result.Provenance.Record;
        bool Baseline = R.ComparisonDigest.Len() == 64;
        for (TCHAR C : R.ComparisonDigest)
            if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f'))))
                Baseline = false;
        if (Asset->GetClass() != UMaterialInstanceConstant::StaticClass() || R.Version != 2 || !R.ReadOnly ||
            !P4ExactNativeString(R.Kind, TEXT("material-instance")) ||
            !P4ExactNativeString(R.SourceDigestKind, TEXT("material-instance-v2-md5")) || !Baseline)
        {
            Result.Error = TEXT("original-material-instance-native-baseline-not-qualified");
            return Result;
        }
        FP4MaterialInstanceNativeEvidence Evidence;
        Result.CurrentDigest = P4MaterialInstanceComparisonState(
            CastChecked<UMaterialInstanceConstant>(Asset), Result.Error, &Evidence);
        if (!Result.Error.IsEmpty())
        {
            Result.Comparison = EP4OriginalComparison::Unreadable;
            return Result;
        }
        if (!P4ExactNativeString(Evidence.RecipeMd5, R.SourceDigest))
        {
            Result.CurrentDigest.Reset();
            Result.Error = TEXT("original-material-instance-source-recipe-differs");
            Result.Comparison = EP4OriginalComparison::Unreadable;
            return Result;
        }
    }
    else
    {
        Result.Error = TEXT("original-comparison-schema-not-supported");
        return Result;
    }
    if (Result.CurrentDigest.IsEmpty())
    {
        Result.Error = TEXT("original-content-unreadable");
        Result.Comparison = EP4OriginalComparison::Unreadable;
        return Result;
    }
    Result.Comparison = P4ExactNativeString(Result.CurrentDigest, Result.Provenance.Record.ComparisonDigest)
        ? EP4OriginalComparison::Equal
        : EP4OriginalComparison::Different;
    return Result;
}
FString UP4AuthoringTools::NativeOriginalImportStatus(UObject* Asset)
{
    const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
    return Reply(Status, Status.Error);
}
FString UP4AuthoringTools::NativeOriginalImportDiagnostic(UObject* Asset)
{
    const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(Asset);
    auto Json = MakeShared<FJsonObject>();
    P4OriginalStatusFields(Diagnostic.Provenance, *Json);
    Json->SetBoolField(TEXT("valid"), Diagnostic.Error.IsEmpty());
    Json->SetStringField(TEXT("error"), Diagnostic.Error);
    Json->SetBoolField(TEXT("compared"),
        Diagnostic.Comparison == EP4OriginalComparison::Equal ||
            Diagnostic.Comparison == EP4OriginalComparison::Different);
    Json->SetBoolField(TEXT("unchanged"), Diagnostic.Comparison == EP4OriginalComparison::Equal);
    Json->SetStringField(TEXT("state"), Diagnostic.CurrentDigest);
    Json->SetStringField(TEXT("state_kind"), Diagnostic.Provenance.Record.ComparisonSchema);
    FString Out = P4CondensedJson(Json);
    return Out;
}
FString UP4AuthoringTools::NativeMigrateOriginalImportRecord(UObject* Asset)
{
    const FP4OriginalStatus Before = P4QueryOriginalAsset(Asset);
    FString Error;
    const bool Migrated = P4MigrateOriginalRecord(
        Asset,
        [&]()
        {
            FString Now;
            if (Before.Record.ComparisonSchema == TEXT("level-lines-v1") ||
                Before.Record.ComparisonSchema == TEXT("game-backed-level-lines-v1"))
                Now = P4LevelState(Cast<UWorld>(Asset));
            else if (Before.Record.ComparisonSchema == TEXT("animation-object-lines-v2") ||
                (Before.Record.ComparisonSchema == TEXT("object-lines-v1") &&
                    Asset->GetClass()->GetPathName() != TEXT("/Script/Engine.AnimSequence")))
            {
                const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(Asset);
                if (Diagnostic.Error.IsEmpty())
                    Now = Diagnostic.CurrentDigest;
            }
            else if (Before.Record.ComparisonSchema == TEXT("mesh-state-v1"))
                Now = P4StaticMeshState(Cast<UStaticMesh>(Asset));
            else if (Before.Record.ComparisonSchema == TEXT("texture-state-v1"))
                Now = P4TextureState(Cast<UTexture>(Asset));
            return !Now.IsEmpty() && Now == Before.Record.ComparisonDigest;
        },
        Error);
    return Reply(Migrated ? P4QueryOriginalAsset(Asset) : Before, Error);
}

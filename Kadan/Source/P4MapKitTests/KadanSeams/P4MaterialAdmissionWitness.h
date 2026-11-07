#pragma once
#include "P4KadanImportSources.h"
#include "P4LevelContent.h"
#include "P4MaterialInstanceComparison.h"
#include "P4MaterialAdmissionParentWitness.h"
#include "P4NativeHash.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

// Opt-in evidence for one disposable fixture. Never participates in admission.
namespace P4MICAdmissionWitness
{
inline bool Enabled(const FString& Package)
{
    const FString Target = TEXT("/Game/Carrot4/Effects/Cmn/Materials/MI_eft_Cmn_Awa00_alcfr");
    return P4ExactNativeString(Package, Target) &&
        P4ExactNativeString(
            FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_ADMISSION_WITNESS_PACKAGE")), Target);
}
inline FString Full(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}
inline bool Lines(FJsonObject& Out, const FString& Name, const TArray<FString>& Values, int32 MaxBytes = 65536)
{
    int64 Bytes = 0;
    for (const auto& Line : Values)
        Bytes += FTCHARToUTF8(*Line).Length();
    const bool Complete = Values.Num() <= 512 && Bytes <= MaxBytes;
    Out.SetNumberField(Name + TEXT("_count"), Values.Num());
    Out.SetNumberField(Name + TEXT("_utf8_bytes"), Bytes);
    Out.SetBoolField(Name + TEXT("_complete"), Complete);
    if (Complete)
    {
        TArray<TSharedPtr<FJsonValue>> Json;
        for (const auto& Line : Values)
            Json.Add(MakeShared<FJsonValueString>(Line));
        Out.SetArrayField(Name, Json);
    }
    return Complete;
}
inline void Marker(FJsonObject& Out, const FString& Name, const FString& Text)
{
    const bool Complete = FTCHARToUTF8(*Text).Length() <= 16384;
    Out.SetBoolField(Name + TEXT("_complete"), Complete);
    if (Complete)
        Out.SetStringField(Name, Text);
}
inline void Delta(FJsonObject& Out, const FString& Prefix, const TArray<FString>& Before, const TArray<FString>& After)
{
    TArray<FString> Removed = Before, Added;
    for (const auto& Line : After)
    {
        const int32 At = Removed.IndexOfByPredicate(
            [&](const FString& Value) { return P4ExactNativeString(Value, Line); });
        if (At == INDEX_NONE)
            Added.Add(Line);
        else
            Removed.RemoveAt(At);
    }
    Lines(Out, Prefix + TEXT("_removed"), Removed, 16384);
    Lines(Out, Prefix + TEXT("_added"), Added, 16384);
}
inline bool Capture(UObject* Asset, const FString& Phase, const TSharedRef<FJsonObject>& Out)
{
    if (!IsValid(Asset) || !Enabled(Asset->GetOutermost()->GetName()))
        return true;
    const bool Admission = Phase == TEXT("admission-edited-negative") || Phase == TEXT("admission-unchanged-positive");
    if (!Admission && Phase != TEXT("before-first-run") && Phase != TEXT("after-negative-restore"))
        return false;
    const FString Folder = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_OUT")));
    FString Saved = Full(FPaths::ProjectSavedDir());
    Saved.RemoveFromEnd(TEXT("/"));
    if (!Folder.StartsWith(Saved + TEXT("/"), ESearchCase::CaseSensitive) ||
        !IFileManager::Get().DirectoryExists(*Folder))
        return false;
    static int32 Captures = 0;
    if (++Captures > 8)
        return false;
    const FString Stem = Phase + (Admission ? TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) : FString());
    UPackage* Package = Asset->GetOutermost();
    Out->SetStringField(TEXT("asset"), Asset->GetPathName());
    Out->SetStringField(TEXT("phase"), Phase);
    Out->SetBoolField(TEXT("dirty_before_observation"), Package->IsDirty());
    const FString OriginalMarker = Package->GetMetaData()->GetValue(Asset, TEXT("P4OriginalRecord"));
    Marker(*Out, TEXT("original_record"), OriginalMarker);
    if (auto* Instance = Cast<UMaterialInstanceConstant>(Asset))
        if (IsValid(Instance->Parent))
        {
            Out->SetStringField(TEXT("parent"), Instance->Parent->GetPathName());
            Out->SetBoolField(TEXT("parent_dirty_before_observation"), Instance->Parent->GetOutermost()->IsDirty());
            Marker(*Out, TEXT("parent_record"),
                Instance->Parent->GetOutermost()->GetMetaData()->GetValue(Instance->Parent, TEXT("P4OriginalRecord")));
            auto* Parent = Cast<UMaterial>(Instance->Parent);
            if (!Parent || !CaptureParent(Parent, Folder, Stem, *Out))
                return false;
        }
    TArray<UObject*> Roots;
    GetObjectsWithOuter(Package, Roots, false);
    TArray<TSharedPtr<FJsonValue>> RootJson;
    for (int32 I = 0; I < FMath::Min(Roots.Num(), 64); ++I)
        if (IsValid(Roots[I]))
        {
            auto Root = MakeShared<FJsonObject>();
            Root->SetStringField(TEXT("path"), Roots[I]->GetPathName().Left(1024));
            Root->SetStringField(TEXT("class"), Roots[I]->GetClass()->GetPathName().Left(1024));
            Root->SetBoolField(TEXT("is_asset"), Roots[I]->IsAsset());
            Root->SetBoolField(TEXT("is_metadata"), Roots[I]->IsA<UMetaData>());
            RootJson.Add(MakeShared<FJsonValueObject>(Root));
        }
    Out->SetNumberField(TEXT("root_count"), Roots.Num());
    Out->SetBoolField(TEXT("roots_complete"), Roots.Num() <= 64);
    Out->SetArrayField(TEXT("roots"), RootJson);
    TArray<FString> Raw;
    FString RawState;
    {
        TArray<TSharedPtr<FP4KadanSourceAsset>> Empty;
        FP4KadanImportSourceScope Strict(Empty);
        RawState = P4OriginalObjectAssetStateAndLines(Asset, Raw);
    }
    Out->SetStringField(TEXT("raw_state"), RawState);
    Out->SetBoolField(TEXT("raw_state_available"), !RawState.IsEmpty());
    const bool Complete = Lines(*Out, TEXT("raw_lines"), Raw);
    if (Admission)
    {
        // The extra cook above is diagnostic only. The exact production decision
        // and its diagnostic digest were already evaluated before Capture.
        auto Projected = Raw;
        FString Error;
        const bool ProjectedOk = P4ProjectOriginalMaterialInstanceBindings(Asset, Projected, Error);
        Out->SetBoolField(TEXT("projection_ok"), ProjectedOk);
        Out->SetStringField(TEXT("projection_error"), Error);
        Delta(*Out, TEXT("projection"), Raw, Projected);
        for (const TCHAR* Earlier : {TEXT("before-first-run"), TEXT("after-negative-restore")})
        {
            const FString File = Folder / (FString(Earlier) + TEXT(".json"));
            const int64 Bytes = IFileManager::Get().FileSize(*File);
            FString Text;
            TSharedPtr<FJsonObject> Prior;
            const TArray<TSharedPtr<FJsonValue>>* PriorJson = nullptr;
            if (Bytes < 0 || Bytes > 131072 || !FFileHelper::LoadFileToString(Text, *File) ||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Prior) || !Prior.IsValid() ||
                !Prior->TryGetArrayField(TEXT("raw_lines"), PriorJson))
                continue;
            TArray<FString> Before;
            bool Valid = PriorJson->Num() <= 512;
            for (const auto& Line : *PriorJson)
            {
                FString Value;
                if (!Line->TryGetString(Value))
                {
                    Valid = false;
                    break;
                }
                Before.Add(Value);
            }
            if (Valid)
                Delta(*Out, Earlier, Before, Raw);
        }
    }
    Out->SetBoolField(TEXT("dirty_after_observation"), Package->IsDirty());
    Out->SetBoolField(TEXT("marker_unchanged_by_observation"),
        P4ExactNativeString(OriginalMarker, Package->GetMetaData()->GetValue(Asset, TEXT("P4OriginalRecord"))));
    FString Text;
    if (!FJsonSerializer::Serialize(Out, TJsonWriterFactory<>::Create(&Text)) || FTCHARToUTF8(*Text).Length() > 131072)
        return false;
    const FString File = Folder / (Stem + TEXT(".json"));
    const bool Written = !IFileManager::Get().FileExists(*File) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return Written && Complete && !RawState.IsEmpty();
}
} //namespace P4MICAdmissionWitness

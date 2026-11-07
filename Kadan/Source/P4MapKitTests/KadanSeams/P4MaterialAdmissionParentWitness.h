#pragma once
#include "P4MaterialGraph.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace P4MICAdmissionWitness
{
inline FString RangeHash(const TArray<uint8>& Bytes, int64 Begin, int64 End)
{
    FString Hash;
    if (Begin < 0 || End < Begin || End > Bytes.Num())
        return Hash;
    P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Update)
        {
            for (int64 At = Begin; At < End; At += 65536)
                if (!Update(Bytes.GetData() + At, int32(FMath::Min<int64>(65536, End - At))))
                    return false;
            return true;
        },
        Hash);
    return Hash;
}
inline FString RangeHex(const TArray<uint8>& Bytes, int64 Begin, int64 End)
{
    if (Begin < 0 || End < Begin || End > Bytes.Num())
        return FString();
    const TCHAR* Hex = TEXT("0123456789abcdef");
    FString Out;
    for (int64 I = Begin; I < FMath::Min<int64>(End, Begin + 256); ++I)
    {
        Out.AppendChar(Hex[Bytes[I] >> 4]);
        Out.AppendChar(Hex[Bytes[I] & 15]);
    }
    return Out;
}
inline bool CaptureParent(UMaterial* Parent, const FString& Folder, const FString& Stem, FJsonObject& Summary)
{
    if (!IsValid(Parent))
        return false;
    const FString Marker = Parent->GetOutermost()->GetMetaData()->GetValue(Parent, TEXT("P4OriginalRecord"));
    const bool WasDirty = Parent->GetOutermost()->IsDirty();
    // Observe actual production comparison before the additional field/frame read.
    const auto Actual = P4DiagnoseOriginalAsset(Parent);
    const TCHAR* Comparison = Actual.Comparison == EP4OriginalComparison::Equal ? TEXT("Equal")
        : Actual.Comparison == EP4OriginalComparison::Different                 ? TEXT("Different")
        : Actual.Comparison == EP4OriginalComparison::Unreadable                ? TEXT("Unreadable")
                                                                                : TEXT("NotCompared");
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("actual_comparison_before_frame"), Comparison);
    Out->SetStringField(TEXT("actual_error_before_frame"), Actual.Error);
    Out->SetStringField(TEXT("actual_state_before_frame"), Actual.CurrentDigest);
    Out->SetStringField(TEXT("recorded_state"), Actual.Provenance.Record.ComparisonDigest);
    Out->SetBoolField(TEXT("dirty_before_frame"), WasDirty);
    FP4MaterialNativeStateFrame Frame;
    FString FrameError;
    const FString State = P4MaterialComparisonStateAndFrame(Parent, Frame, FrameError);
    Out->SetStringField(TEXT("frame_state"), State);
    Out->SetStringField(TEXT("frame_error"), FrameError);
    Out->SetBoolField(TEXT("frame_complete"), Frame.Complete);
    Out->SetBoolField(TEXT("frame_matches_actual_comparison"), P4ExactNativeString(State, Actual.CurrentDigest));
    Out->SetBoolField(TEXT("dirty_after_frame"), Parent->GetOutermost()->IsDirty());
    Out->SetBoolField(TEXT("marker_unchanged"),
        P4ExactNativeString(Marker, Parent->GetOutermost()->GetMetaData()->GetValue(Parent, TEXT("P4OriginalRecord"))));
    Summary.SetStringField(TEXT("parent_actual_comparison_before_frame"), Comparison);
    Summary.SetStringField(TEXT("parent_actual_state_before_frame"), Actual.CurrentDigest);
    Summary.SetStringField(TEXT("parent_recorded_state"), Actual.Provenance.Record.ComparisonDigest);
    Summary.SetBoolField(
        TEXT("parent_frame_matches_actual_comparison"), P4ExactNativeString(State, Actual.CurrentDigest));
    if (!Frame.Complete || Frame.Bytes.Num() > 4 * 1024 * 1024 || Frame.Fields.Num() > 8192 ||
        FTCHARToUTF8(*Frame.Graph).Length() > 1024 * 1024)
        return false;
    const FString BytesHash = RangeHash(Frame.Bytes, 0, Frame.Bytes.Num());
    Out->SetStringField(TEXT("native_bytes_sha256"), BytesHash);
    Out->SetBoolField(TEXT("native_bytes_match_state"), P4ExactNativeString(BytesHash, State));
    Out->SetStringField(TEXT("graph"), Frame.Graph);
    Out->SetStringField(TEXT("anisotropy_expression"),
        Parent->Anisotropy.Expression ? Parent->Anisotropy.Expression->GetPathName() : TEXT("none"));
    Out->SetStringField(TEXT("tangent_expression"),
        Parent->Tangent.Expression ? Parent->Tangent.Expression->GetPathName() : TEXT("none"));
    TArray<TSharedPtr<FJsonValue>> Fields;
    int64 End = 0;
    for (const auto& Field : Frame.Fields)
    {
        if (Field.Begin != End || Field.End < Field.Begin || Field.End > Frame.Bytes.Num())
            return false;
        End = Field.End;
        const FString Hash = RangeHash(Frame.Bytes, Field.Begin, Field.End);
        if (Hash.IsEmpty())
            return false;
        TArray<TSharedPtr<FJsonValue>> Row{MakeShared<FJsonValueString>(Field.Object),
            MakeShared<FJsonValueString>(Field.Property), MakeShared<FJsonValueNumber>(double(Field.Begin)),
            MakeShared<FJsonValueNumber>(double(Field.End)), MakeShared<FJsonValueString>(Hash)};
        Fields.Add(MakeShared<FJsonValueArray>(Row));
    }
    if (End != Frame.Bytes.Num())
        return false;
    Out->SetArrayField(TEXT("fields"), Fields);
    Out->SetStringField(TEXT("field_columns"), TEXT("object, property, begin, end, exact_range_sha256"));
    const FString BeforeMeta = Folder / TEXT("before-first-run.parent.json"),
                  BeforeBytes = Folder / TEXT("before-first-run.parent.bin");
    const int64 MetaBytes = IFileManager::Get().FileSize(*BeforeMeta),
                NativeBytes = IFileManager::Get().FileSize(*BeforeBytes);
    if (Stem != TEXT("before-first-run") && MetaBytes > 0 && MetaBytes <= 2 * 1024 * 1024 && NativeBytes >= 0 &&
        NativeBytes <= 4 * 1024 * 1024)
    {
        FString Text;
        TSharedPtr<FJsonObject> Before;
        TArray<uint8> Bytes;
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        FString ExpectedHash;
        if (FFileHelper::LoadFileToString(Text, *BeforeMeta) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Before) && Before.IsValid() &&
            Before->TryGetArrayField(TEXT("fields"), Rows) && Rows->Num() <= 8192 &&
            Before->TryGetStringField(TEXT("native_bytes_sha256"), ExpectedHash) &&
            FFileHelper::LoadFileToArray(Bytes, *BeforeBytes) &&
            P4ExactNativeString(RangeHash(Bytes, 0, Bytes.Num()), ExpectedHash))
        {
            TArray<TSharedPtr<FJsonValue>> Changed;
            int32 Changes = 0;
            bool Valid = true;
            for (int32 I = 0; I < FMath::Max(Rows->Num(), Fields.Num()); ++I)
            {
                const TArray<TSharedPtr<FJsonValue>>* Prior = nullptr;
                FString Object, Property, Hash;
                int64 Begin = 0, Last = 0;
                if (I < Rows->Num())
                {
                    if (!(*Rows)[I]->TryGetArray(Prior) || Prior->Num() != 5)
                    {
                        Valid = false;
                        break;
                    }
                    Object = (*Prior)[0]->AsString();
                    Property = (*Prior)[1]->AsString();
                    Begin = int64((*Prior)[2]->AsNumber());
                    Last = int64((*Prior)[3]->AsNumber());
                    Hash = (*Prior)[4]->AsString();
                    if (Begin < 0 || Last < Begin || Last > Bytes.Num() ||
                        !P4ExactNativeString(RangeHash(Bytes, Begin, Last), Hash))
                    {
                        Valid = false;
                        break;
                    }
                }
                const bool Same = I < Rows->Num() && I < Frame.Fields.Num() &&
                    P4ExactNativeString(Object, Frame.Fields[I].Object) &&
                    P4ExactNativeString(Property, Frame.Fields[I].Property) &&
                    P4ExactNativeString(Hash, Fields[I]->AsArray()[4]->AsString());
                if (Same)
                    continue;
                ++Changes;
                if (Changed.Num() >= 128)
                    continue;
                auto Change = MakeShared<FJsonObject>();
                Change->SetNumberField(TEXT("ordinal"), I);
                if (I < Rows->Num())
                {
                    Change->SetStringField(TEXT("before_object"), Object);
                    Change->SetStringField(TEXT("before_property"), Property);
                    Change->SetStringField(TEXT("before_sha256"), Hash);
                    Change->SetNumberField(TEXT("before_bytes"), Last - Begin);
                    Change->SetStringField(TEXT("before_hex"), RangeHex(Bytes, Begin, Last));
                    Change->SetBoolField(TEXT("before_hex_complete"), Last - Begin <= 256);
                }
                if (I < Frame.Fields.Num())
                {
                    const auto& Now = Frame.Fields[I];
                    Change->SetStringField(TEXT("after_object"), Now.Object);
                    Change->SetStringField(TEXT("after_property"), Now.Property);
                    Change->SetStringField(TEXT("after_sha256"), Fields[I]->AsArray()[4]->AsString());
                    Change->SetNumberField(TEXT("after_bytes"), Now.End - Now.Begin);
                    Change->SetStringField(TEXT("after_hex"), RangeHex(Frame.Bytes, Now.Begin, Now.End));
                    Change->SetBoolField(TEXT("after_hex_complete"), Now.End - Now.Begin <= 256);
                }
                Changed.Add(MakeShared<FJsonValueObject>(Change));
            }
            Out->SetBoolField(TEXT("baseline_frame_valid"), Valid);
            Out->SetNumberField(TEXT("changed_field_count"), Changes);
            Out->SetBoolField(TEXT("changed_fields_complete"), Valid && Changes <= 128);
            Out->SetArrayField(TEXT("changed_fields"), Changed);
            Summary.SetNumberField(TEXT("parent_changed_field_count"), Changes);
            Summary.SetBoolField(TEXT("parent_changed_fields_complete"), Valid && Changes <= 128);
        }
    }
    FString Text;
    if (!FJsonSerializer::Serialize(Out, TJsonWriterFactory<>::Create(&Text)) ||
        FTCHARToUTF8(*Text).Length() > 2 * 1024 * 1024)
        return false;
    const FString Meta = Folder / (Stem + TEXT(".parent.json")), Binary = Folder / (Stem + TEXT(".parent.bin"));
    if (IFileManager::Get().FileExists(*Meta) || IFileManager::Get().FileExists(*Binary) ||
        !P4ExactNativeString(State, BytesHash))
        return false;
    if (!FFileHelper::SaveArrayToFile(Frame.Bytes, *Binary) ||
        !FFileHelper::SaveStringToFile(Text, *Meta, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return false;
    Summary.SetStringField(TEXT("parent_frame_metadata"), Meta);
    Summary.SetStringField(TEXT("parent_frame_binary"), Binary);
    Summary.SetStringField(TEXT("parent_frame_bytes_sha256"), BytesHash);
    return true;
}
} //namespace P4MICAdmissionWitness

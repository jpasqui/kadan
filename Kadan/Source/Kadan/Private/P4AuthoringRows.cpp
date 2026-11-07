// The authoring rows' entry points: canonical values, references, writes and table JSON.

#include "P4AuthoringRows.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4OriginalImportSave.h"
#include "DataTableUtils.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "P4PlaceActors.h"
#include "P4MapKitCore/quality.h"
#include "ScopedTransaction.h"
#include "P4TextValues.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/PackageName.h"
#include "P4NativeHash.h"
#include "Engine/DataAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "P4ImportReferences.h"
#include "UObject/MetaData.h"
#include "Misc/Paths.h"
#include "P4OriginalDecode.h"
#include "P4OriginalMeshStaging.h"
#include "P4StaticMeshBuild.h"
#include "P4TextureBuild.h"
#include "P4TextureContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4StaticMeshContent.h"
#include "P4LevelContent.h"
#include "P4LightingBake.h"
#include "P4KadanImport.h"
#include "P4NativeValueStaging.h"
#include "P4AuthoringRowsInternal.h"

using namespace P4AuthoringRowsDetail;

FString UP4AuthoringTools::NativeCanonicalAssetValues(UObject* Asset, const FString& Json)
{
    FString Error, Canonical;
    TSharedPtr<FJsonObject> Fields;
    const bool Valid = P4CanonicalAsset(Asset, Json, Fields, Canonical, Error, true);
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Valid);
    Result->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Result);
    if (Valid)
    {
        Out.RemoveFromEnd(TEXT("}"));
        Out += TEXT(",\"values\":") + Canonical + TEXT("}");
    }
    return Out;
}

FString UP4AuthoringTools::NativeAssetReferences(UObject* Asset, const FString& Json)
{
    FString Error, Canonical;
    TSharedPtr<FJsonObject> Fields;
    TArray<TSharedPtr<FJsonValue>> References;
    const bool Valid = P4CanonicalAsset(Asset, Json, Fields, Canonical, Error, false);
    if (Valid)
        for (const auto& Field : Fields->Values)
            P4CollectAssetReferences(Asset->GetClass()->FindPropertyByName(FName(*Field.Key)), Field.Value, References);
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Valid);
    Result->SetStringField(TEXT("error"), Error);
    Result->SetArrayField(TEXT("references"), References);
    FString Out = P4CondensedJson(Result);
    return Out;
}

bool P4PrepareExactPrivateProperties(UObject* Scratch, const FString& Values, FString& Canonical, FString& Error)
{
    Canonical.Empty();
    if (!IsInGameThread() || !IsValid(Scratch) || !Scratch->HasAnyFlags(RF_Transient) ||
        Scratch->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
        Scratch->GetOuter() != GetTransientPackage())
        return P4AdmissionFail(
            Error, TEXT("PrivateProperties"), TEXT("disposable transient editor-thread object required"));
    TStrongObjectPtr<UObject> Guard(Scratch);
    TSharedPtr<FJsonObject> Fields, Checked;
    FString Prepared;
    if (!P4TableJsonTokens(Values, Error) ||
        !FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Values), Fields, FJsonSerializer::EFlags::StoreNumbersAsStrings) ||
        !Fields.IsValid())
        return P4AdmissionFail(Error, TEXT("PrivateProperties"), TEXT("exact field object required"));
    if (!P4CanonicalFields(Scratch->GetClass(), Fields, TEXT("PrivateProperties"), Error, 0, false, true) ||
        !P4ExactJson(MakeShared<FJsonValueObject>(Fields), Prepared) ||
        !FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Prepared), Checked, FJsonSerializer::EFlags::StoreNumbersAsStrings))
        return false;
    // Complete validation precedes the first scratch mutation.
    for (auto& Field : Checked->Values)
    {
        auto* Property = Scratch->GetClass()->FindPropertyByName(FName(*Field.Key));
        if (!ExactIntegerJson(Property, Field.Value))
            return P4AdmissionFail(Error, Field.Key, TEXT("unsupported or invalid exact private value"));
    }
    for (const auto& Field : Fields->Values)
    {
        TSharedPtr<FJsonValue> A, B;
        if (!P4ReadbackValue(Scratch->GetClass()->FindPropertyByName(FName(*Field.Key)), Field.Value, Field.Value, A, B,
                Field.Key, Error))
            return false;
    }
    for (const auto& Field : Fields->Values)
    {
        auto* Property = Scratch->GetClass()->FindPropertyByName(FName(*Field.Key));
        if (!P4WriteAssetValue(Property, Property->ContainerPtrToValuePtr<void>(Scratch), Field.Value, Scratch))
            return P4AdmissionFail(Error, Field.Key, TEXT("private property write failed"));
    }
    for (const auto& Field : Fields->Values)
    {
        TSharedPtr<FJsonObject> Actual;
        const auto Json = UP4AuthoringTools::PropertyJson(Scratch, FName(*Field.Key));
        if (Json.IsEmpty() ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"value\":") + Json + TEXT("}")), Actual,
                FJsonSerializer::EFlags::StoreNumbersAsStrings))
            return P4AdmissionFail(Error, Field.Key, TEXT("independent private property readback unavailable"));
        TSharedPtr<FJsonValue> A, B;
        if (!P4ReadbackValue(Scratch->GetClass()->FindPropertyByName(FName(*Field.Key)), Field.Value,
                Actual->TryGetField(TEXT("value")), A, B, Field.Key, Error))
            return false;
    }
    Canonical = Prepared;
    return true;
}

bool UP4AuthoringTools::NativeApplyAssetValues(UObject* Asset, const FString& Json)
{
    FString Error, Canonical;
    TSharedPtr<FJsonObject> Fields;
    if (!P4CanonicalAsset(Asset, Json, Fields, Canonical, Error, true))
        return false;
    TStrongObjectPtr<UObject> TargetGuard(Asset), Scratch(NewNativeAsset(Asset->GetClass()));
    if (!Scratch.IsValid() || !CopyNativeAssetValues(Scratch.Get(), Asset))
        return false;
    for (const auto& Field : Fields->Values)
    {
        auto Property = Asset->GetClass()->FindPropertyByName(FName(*Field.Key));
        if (!P4WriteAssetValue(
                Property, Property->ContainerPtrToValuePtr<void>(Scratch.Get()), Field.Value, Scratch.Get()))
            return false;
    }
    const auto Matches = [&](UObject* Target)
    {
        TSharedPtr<FJsonObject> Verdict;
        return P4ParseJson(NativeAssetReadback(Target, Canonical), Verdict) && Verdict->GetBoolField(TEXT("valid"));
    };
    if (!Matches(Scratch.Get()))
        return false;
    TStrongObjectPtr<UObject> Before(NewNativeAsset(Asset->GetClass()));
    if (!Before.IsValid() || !CopyNativeAssetValues(Before.Get(), Asset))
        return false;
    const bool WasDirty = Asset->GetOutermost()->IsDirty();
    FScopedTransaction Transaction(
        NSLOCTEXT("P4MapKit", "ImportAsset", "Import native asset values"), !Asset->HasAnyFlags(RF_Transient));
    if (CopyNativeAssetValues(Asset, Scratch.Get()) && Matches(Asset))
        return true;
    CopyNativeAssetValues(Asset, Before.Get());
    Asset->GetOutermost()->SetDirtyFlag(WasDirty);
    Transaction.Cancel();
    return false;
}

FString UP4AuthoringTools::TableJson(UDataTable* Table)
{
    return Table
        ? Table->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs | EDataTableExportFlags::UseSimpleText)
        : FString();
}
bool UP4AuthoringTools::ImportTableJson(UDataTable* Table, const FString& Json)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    FString Error;
    if (!P4CanonicalRows(Table, Json, Rows, Error))
        return false;
    // A manually reflected struct with a native RowBase super but no constructor
    // has no RowBase vtable. UE's post-import virtual callback would crash.
    if (!Table->RowStruct->GetCppStructOps())
        return false;
    for (auto& Row : Rows)
    {
        if (!Row.IsValid() || Row->Type != EJson::Object)
            return false;
        const auto Object = Row->AsObject();
        for (auto& Field : Object->Values)
            if (Field.Key != TEXT("Name") &&
                !ExactIntegerJson(Table->RowStruct->FindPropertyByName(FName(*Field.Key)), Field.Value))
                return false;
    }
    // UE4's generic import factory can choose CSV for an existing asset with no source filename.
    // Use the DataTable JSON parser directly, retaining native defaults for omitted properties.
    // UE4's parser rejects an empty array without clearing existing rows.
    if (Rows.Num() == 0)
    {
        const FScopedTransaction Transaction(
            NSLOCTEXT("P4MapKit", "ImportTable", "Import original table values"), !Table->HasAnyFlags(RF_Transient));
        Table->Modify();
        Table->EmptyTable();
        Table->MarkPackageDirty();
        return true;
    }
    FString ExactJson;
    if (!P4ExactJson(MakeShared<FJsonValueArray>(Rows), ExactJson))
        return false;
    auto Scratch = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
    Scratch->RowStruct = Table->RowStruct;
    Scratch->bIgnoreMissingFields = true;
    const TArray<FString> Problems = Scratch->CreateTableFromJSONString(ExactJson);
    if (Problems.Num() != 0)
        return false;
    TSharedPtr<FJsonObject> Readback;
    if (!P4ParseJson(NativeTableReadback(Scratch, Json), Readback) || !Readback->GetBoolField(TEXT("valid")))
        return false;
    const FScopedTransaction Transaction(
        NSLOCTEXT("P4MapKit", "ImportTable", "Import original table values"), !Table->HasAnyFlags(RF_Transient));
    Table->Modify();
    Table->EmptyTable();
    for (const auto& Row : Scratch->GetRowMap())
        Table->AddRow(Row.Key, *reinterpret_cast<const FTableRowBase*>(Row.Value));
    Table->MarkPackageDirty();
    return true;
}

FString UP4AuthoringTools::NativeCanonicalTableRows(UDataTable* Table, const FString& Json)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    FString Error, Canonical;
    bool Valid = P4CanonicalRows(Table, Json, Rows, Error);
    if (Valid)
    {
        // Keep the canonical exact-number transport before converting its copy
        // to UE4's importer syntax (signed uint64 bits, FText literals, maps).
        Valid = P4ExactJson(MakeShared<FJsonValueArray>(Rows), Canonical);
        for (auto& Row : Rows)
        {
            if (!Valid)
                break;
            for (auto& Field : Row->AsObject()->Values)
            {
                if (Field.Key == TEXT("Name"))
                    continue;
                if (!ExactIntegerJson(Table->RowStruct->FindPropertyByName(FName(*Field.Key)), Field.Value))
                {
                    Valid = false;
                    Error = Row->AsObject()->GetStringField(TEXT("Name")) + TEXT(".") + Field.Key +
                        TEXT(": unsupported or invalid exact native value");
                    break;
                }
            }
        }
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Valid);
    Result->SetStringField(TEXT("error"), Error);
    FString Metadata = P4CondensedJson(Result);
    if (Valid)
    {
        Metadata.RemoveFromEnd(TEXT("}"));
        Metadata += TEXT(",\"rows\":") + Canonical + TEXT("}");
    }
    return Metadata;
}

#include "P4AuthoringRows.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4NativeHash.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "Components/SceneComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/StructOnScope.h"
#include "Materials/MaterialInterface.h"
#include "P4TextValues.h"
#include "P4ImportReferences.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GCObject.h"
#include "Serialization/StructuredArchive.h"
#include "Misc/OutputDevice.h"
#include "P4PropertyValuesInternal.h"

namespace P4PropertyValuesDetail
{
bool SupportedNativeAssetClass(UClass* Class)
{
    // Original count and placement assets derive directly from UObject.
    return Class &&
        (Class->IsChildOf(UDataAsset::StaticClass()) ||
            Class->GetPathName() == TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacedNumAsset") ||
            Class->GetPathName() == TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacementInfoAsset"));
}

void EnableOriginalFields(UStruct* Struct, int32 Depth = 0)
{
    if (!Struct || Depth > 16 || Struct->GetOutermost()->GetName() != TEXT(P4_GAME_SCRIPT_PACKAGE))
        return;
    for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
    {
        if (Field->GetOwnerStruct()->GetOutermost()->GetName() != TEXT(P4_GAME_SCRIPT_PACKAGE) ||
            Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        Field->SetPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
        Field->ClearPropertyFlags(
            CPF_EditConst | CPF_BlueprintReadOnly | CPF_DisableEditOnInstance | CPF_DisableEditOnTemplate);
        if (auto Child = CastField<FStructProperty>(*Field))
            EnableOriginalFields(Child->Struct, Depth + 1);
    }
}
FString Quote(const FString& Text)
{
    FString Json;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
    Writer->WriteArrayStart();
    Writer->WriteValue(Text);
    Writer->WriteArrayEnd();
    Writer->Close();
    return Json.Mid(1, Json.Len() - 2);
}

bool JsonValue(FProperty* Property, const void* Value, FString& Out, bool Single)
{
    if (!Single && Property->ArrayDim > 1)
    {
        TArray<FString> Slots;
        for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
        {
            FString Slot;
            if (!JsonValue(Property, static_cast<const uint8*>(Value) + Index * Property->ElementSize, Slot, true))
                return false;
            Slots.Add(Slot);
        }
        Out = TEXT("[") + FString::Join(Slots, TEXT(",")) + TEXT("]");
        return true;
    }
    if (auto Enum = CastField<FEnumProperty>(Property))
    {
        const int64 Number = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
        if (!Enum->GetEnum() || !Enum->GetEnum()->IsValidEnumValue(Number))
            return false;
        const FName Entry = Enum->GetEnum()->GetNameByValue(Number);
        Out = Quote(Entry.ToString());
        return true;
    }
    if (auto Byte = CastField<FByteProperty>(Property))
        if (Byte->Enum)
        {
            const int64 Number = Byte->GetPropertyValue(Value);
            if (!Byte->Enum->IsValidEnumValue(Number))
                return false;
            Out = Quote(Byte->Enum->GetNameByValue(Number).ToString());
            return true;
        }
    if (auto Number = CastField<FNumericProperty>(Property))
    {
        // ExportText is exact for signed/unsigned 64-bit integers; FJsonValueNumber isn't.
        if (Number->IsInteger())
            Number->ExportTextItem(Out, Value, nullptr, nullptr, PPF_None, nullptr);
        else
        {
            const double NumberValue = Number->GetFloatingPointPropertyValue(Value);
            if (!FMath::IsFinite(NumberValue))
                return false;
            Out = FString::Printf(TEXT("%.17g"), NumberValue);
            if (!Out.Contains(TEXT(".")) && !Out.Contains(TEXT("e")) && !Out.Contains(TEXT("E")))
                Out += TEXT(".0");
        }
        return true;
    }
    if (auto Bool = CastField<FBoolProperty>(Property))
    {
        Out = Bool->GetPropertyValue(Value) ? TEXT("true") : TEXT("false");
        return true;
    }
    if (auto Name = CastField<FNameProperty>(Property))
    {
        Out = Quote(Name->GetPropertyValue(Value).ToString());
        return true;
    }
    if (auto String = CastField<FStrProperty>(Property))
    {
        const FString Native = String->GetPropertyValue(Value);
        if (HasEmbeddedNul(Native))
            return false;
        Out = Quote(Native);
        return true;
    }
    if (auto Text = CastField<FTextProperty>(Property))
    {
        return P4TextRuntimeJson(Text->GetPropertyValue(Value), Out);
    }
    if (auto Soft = CastField<FSoftObjectProperty>(Property))
    {
        Out = Quote(Soft->GetPropertyValue(Value).ToSoftObjectPath().ToString());
        return true;
    }
    if (auto Object = CastField<FObjectPropertyBase>(Property))
    {
        auto Asset = Object->GetObjectPropertyValue(Value);
        Out = Quote(P4ImportObjectPath(Asset));
        return true;
    }
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        TArray<FString> Parts;
        for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
        {
            if (Field->HasAnyPropertyFlags(CPF_Transient))
                continue;
            FString Child;
            if (!JsonValue(*Field, Field->ContainerPtrToValuePtr<void>(Value), Child))
                return false;
            Parts.Add(Quote(Field->GetName()) + TEXT(":") + Child);
        }
        Out = TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
        return true;
    }
    if (auto Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Value);
        TArray<FString> Parts;
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
        {
            FString Child;
            if (!JsonValue(Array->Inner, Helper.GetRawPtr(Index), Child))
                return false;
            Parts.Add(Child);
        }
        Out = TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
        return true;
    }
    if (auto Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Helper(Map, Value);
        TArray<FString> Pairs;
        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
        {
            if (!Helper.IsValidIndex(Index))
                continue;
            FString Key, Child;
            if (!JsonValue(Map->KeyProp, Helper.GetKeyPtr(Index), Key) ||
                !JsonValue(Map->ValueProp, Helper.GetValuePtr(Index), Child))
                return false;
            Pairs.Add(TEXT("[") + Key + TEXT(",") + Child + TEXT("]"));
        }
        Pairs.Sort(); // Map insertion/rehash order is not authored data.
        Out = TEXT("[") + FString::Join(Pairs, TEXT(",")) + TEXT("]");
        return true;
    }
    // An unsupported type is an error, never silently dropped.
    return false;
}

FString PropertySchema(FProperty* Property, int32 Depth = 0)
{
    if (!Property || Depth > 16)
        return FString();
    FString Part = TEXT("{\"name\":") + Quote(Property->GetName()) + TEXT(",\"kind\":") +
        Quote(Property->GetClass()->GetName()) + TEXT(",\"array_dim\":") + FString::FromInt(Property->ArrayDim);
    Part += TEXT(",\"editable\":") +
        FString(Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst)
                ? TEXT("true")
                : TEXT("false"));
    if (auto Soft = CastField<FSoftObjectProperty>(Property))
        if (Soft->PropertyClass)
            Part += TEXT(",\"class\":") + Quote(Soft->PropertyClass->GetPathName());
    if (auto Object = CastField<FObjectPropertyBase>(Property))
        if (Object->PropertyClass && !CastField<FSoftObjectProperty>(Property))
            Part += TEXT(",\"class\":") + Quote(Object->PropertyClass->GetPathName());
    if (auto Class = CastField<FClassProperty>(Property))
        if (Class->MetaClass)
            Part += TEXT(",\"meta_class\":") + Quote(Class->MetaClass->GetPathName());
    if (auto Array = CastField<FArrayProperty>(Property))
    {
        const FString Inner = PropertySchema(Array->Inner, Depth + 1);
        if (Inner.IsEmpty())
            return FString();
        Part += TEXT(",\"inner\":") + Quote(Array->Inner->GetClass()->GetName()) + TEXT(",\"inner_schema\":") + Inner;
    }
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        TArray<FString> Fields;
        for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
        {
            if (Field->HasAnyPropertyFlags(CPF_Transient))
                continue;
            const FString Child = PropertySchema(*Field, Depth + 1);
            if (Child.IsEmpty())
                return FString();
            Fields.Add(Child);
        }
        Part += TEXT(",\"struct\":") + Quote(Struct->Struct->GetPathName()) + TEXT(",\"fields\":[") +
            FString::Join(Fields, TEXT(",")) + TEXT("]");
    }
    if (auto Map = CastField<FMapProperty>(Property))
    {
        const FString Key = PropertySchema(Map->KeyProp, Depth + 1);
        const FString Value = PropertySchema(Map->ValueProp, Depth + 1);
        if (Key.IsEmpty() || Value.IsEmpty())
            return FString();
        Part += TEXT(",\"key_schema\":") + Key + TEXT(",\"value_schema\":") + Value;
    }
    UEnum* Enumeration = nullptr;
    if (auto Enum = CastField<FEnumProperty>(Property))
        Enumeration = Enum->GetEnum();
    else if (auto Byte = CastField<FByteProperty>(Property))
        Enumeration = Byte->Enum;
    if (Enumeration)
    {
        TArray<FString> Names, Numbers;
        for (int32 Index = 0; Index < Enumeration->NumEnums(); ++Index)
        {
            Names.Add(Quote(Enumeration->GetNameByIndex(Index).ToString()));
            Numbers.Add(FString::Printf(TEXT("%lld"), Enumeration->GetValueByIndex(Index)));
        }
        Part += TEXT(",\"enum\":") + Quote(Enumeration->GetPathName()) + TEXT(",\"enum_values\":[") +
            FString::Join(Names, TEXT(",")) + TEXT("]") + TEXT(",\"enum_numbers\":[") +
            FString::Join(Numbers, TEXT(",")) + TEXT("]");
    }
    return Part + TEXT("}");
}
} // namespace P4PropertyValuesDetail

using namespace P4PropertyValuesDetail;

UP4ActorCountsFactory::UP4ActorCountsFactory()
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacedNumAsset"));
}

UClass* UP4ActorCountsFactory::ResolveSupportedClass()
{
    SupportedClass = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacedNumAsset"));
    return SupportedClass;
}

FText UP4ActorCountsFactory::GetDisplayName() const
{
    return FText::FromString(TEXT("Pikmin 4 Actor Counts"));
}

UObject* UP4ActorCountsFactory::FactoryCreateNew(
    UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UClass* Native = ResolveSupportedClass();
    if (!Native || Class != Native || !Parent)
        return nullptr;
    UP4AuthoringTools::EnableOriginalAssetDetails();
    return NewObject<UObject>(Parent, Native, Name, Flags);
}

FString UP4AuthoringTools::PropertyJson(UObject* Object, FName Name)
{
    FResolvedEditProperty Resolved;
    if (!ResolveProperty(Object, Name, false, Resolved))
        return FString();
    FString Result;
    if (!JsonValue(Resolved.Property, Resolved.Value, Result, Resolved.Single))
        return FString();
    return Result;
}

FString UP4AuthoringTools::NativeTableRowsJson(UDataTable* Table)
{
    if (!Table || !Table->RowStruct)
        return FString();
    TArray<FString> Rows;
    for (const auto& Row : Table->GetRowMap())
    {
        TArray<FString> Fields;
        Fields.Add(TEXT("\"Name\":") + Quote(Row.Key.ToString()));
        for (TFieldIterator<FProperty> Field(Table->RowStruct); Field; ++Field)
        {
            if (Field->HasAnyPropertyFlags(CPF_Transient))
                continue;
            // A real property named Name would conflict with the generic JSON row key.
            if (Field->GetFName() == FName(TEXT("Name")))
                return FString();
            FString Child;
            if (!JsonValue(*Field, Field->ContainerPtrToValuePtr<void>(Row.Value), Child))
                return FString();
            Fields.Add(Quote(Field->GetName()) + TEXT(":") + Child);
        }
        Rows.Add(TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}"));
    }
    // The table's own order: the order its rows are saved in and the game reads them in.
    return TEXT("[") + FString::Join(Rows, TEXT(",")) + TEXT("]");
}

UDataTable* UP4AuthoringTools::NewNativeTable(UScriptStruct* RowStruct)
{
    if (!RowStruct || !RowStruct->IsChildOf(FTableRowBase::StaticStruct()))
        return nullptr;
    auto Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
    Table->RowStruct = RowStruct;
    return Table;
}

FString UP4AuthoringTools::NativeTableSchema(UDataTable* Table)
{
    if (!Table || !Table->RowStruct)
        return FString();
    TArray<FString> Fields;
    for (TFieldIterator<FProperty> Field(Table->RowStruct); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        const FString Part = PropertySchema(*Field);
        if (Part.IsEmpty())
            return FString();
        Fields.Add(Part);
    }
    return TEXT("{\"row_struct\":") + Quote(Table->RowStruct->GetPathName()) + TEXT(",\"fields\":[") +
        FString::Join(Fields, TEXT(",")) + TEXT("]}");
}

FString UP4AuthoringTools::NativeAssetSchema(UObject* Asset)
{
    // Schema inspection also serves original actor components. Asset creation
    // and mutation retain their separate native asset-class restrictions.
    if (!Asset || (!SupportedNativeAssetClass(Asset->GetClass()) && !Asset->IsA<UActorComponent>()))
        return FString();
    TArray<FString> Fields;
    for (TFieldIterator<FProperty> Field(Asset->GetClass()); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        const FString Part = PropertySchema(*Field);
        if (Part.IsEmpty())
            return FString();
        Fields.Add(Part);
    }
    return TEXT("{\"class\":") + Quote(Asset->GetClass()->GetPathName()) + TEXT(",\"fields\":[") +
        FString::Join(Fields, TEXT(",")) + TEXT("]}");
}

FString UP4AuthoringTools::NativeAssetValuesJson(UObject* Asset)
{
    if (!Asset || !SupportedNativeAssetClass(Asset->GetClass()))
        return FString();
    TArray<FString> Fields;
    for (TFieldIterator<FProperty> Field(Asset->GetClass()); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        FString Value;
        if (!JsonValue(*Field, Field->ContainerPtrToValuePtr<void>(Asset), Value))
            return FString();
        Fields.Add(Quote(Field->GetName()) + TEXT(":") + Value);
    }
    return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}");
}

int32 UP4AuthoringTools::EnableOriginalAssetDetails()
{
    int32 Count = 0;
    for (const TCHAR* Path :
        {TEXT(P4_GAME_SCRIPT_PACKAGE ".GameSceneData"), TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacedNumAsset"),
            TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacementInfoAsset"),
            TEXT(P4_GAME_SCRIPT_PACKAGE ".OnyonCombineEftColorData")})
        if (auto Class = FindObject<UClass>(nullptr, Path))
        {
            EnableOriginalFields(Class);
            ++Count;
        }
    // Factory CDOs can precede optional recovered modules during editor startup.
    GetMutableDefault<UP4ActorCountsFactory>()->ResolveSupportedClass();
    return Count;
}

UObject* UP4AuthoringTools::NewNativeAsset(UClass* Class)
{
    if (!SupportedNativeAssetClass(Class) || Class->HasAnyClassFlags(CLASS_Abstract))
        return nullptr;
    return NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient);
}

UObject* UP4AuthoringTools::CreateNativeAsset(UClass* Class, const FString& Package)
{
    if (!SupportedNativeAssetClass(Class) || Class->HasAnyClassFlags(CLASS_Abstract) ||
        !P4IsValidContentPackageIgnoringCase(Package))
        return nullptr;
    const FString Name = FPackageName::GetLongPackageAssetName(Package);
    UPackage* Outer = CreatePackage(*Package);
    if (auto Existing = FindObject<UObject>(Outer, *Name))
        return Existing->GetClass() == Class ? Existing : nullptr;
    auto Asset = NewObject<UObject>(Outer, Class, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
    if (!Asset)
        return nullptr;
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    return Asset;
}

bool UP4AuthoringTools::CopyNativeAssetValues(UObject* Target, UObject* Source)
{
    if (!Target || !Source || Target == Source || !SupportedNativeAssetClass(Target->GetClass()) ||
        Target->GetClass() != Source->GetClass() || Target->HasAnyFlags(RF_ClassDefaultObject) ||
        NativeAssetValuesJson(Source).IsEmpty())
        return false;
    Target->Modify();
    for (TFieldIterator<FProperty> Field(Target->GetClass()); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        Field->CopyCompleteValue(
            Field->ContainerPtrToValuePtr<void>(Target), Field->ContainerPtrToValuePtr<void>(Source));
    }
    Target->PostEditChange();
    Target->MarkPackageDirty();
    return true;
}

bool UP4AuthoringTools::EnsureActorRoot(AActor* Actor)
{
    if (!Actor || Actor->HasAnyFlags(RF_ClassDefaultObject))
        return false;
    if (Actor->GetRootComponent())
        return true;
    // Native game constructors aren't executed by a reflection stub. Supply UE's
    // ordinary transform root only when reconstruction hasn't already supplied one.
    Actor->Modify();
    auto Root = NewObject<USceneComponent>(Actor, TEXT("DefaultSceneRoot"), RF_Transactional);
    Actor->AddInstanceComponent(Root);
    Actor->SetRootComponent(Root);
    Root->RegisterComponent();
    Actor->MarkPackageDirty();
    return true;
}

bool UP4AuthoringTools::RefreshActorDefaults(AActor* DefaultActor)
{
    if (!DefaultActor || !DefaultActor->HasAnyFlags(RF_ClassDefaultObject))
        return false;
    auto Class = Cast<UBlueprintGeneratedClass>(DefaultActor->GetClass());
    if (!Class)
        return false;
    // UE caches the native-property differences at compile time. Preset imports
    // then change the CDO; refresh that cache before the first actor is constructed.
    Class->UpdateCustomPropertyListForPostConstruction();
    return true;
}

UP4MaterialExportData* UP4AuthoringTools::MaterialExportSettings(UMaterialInterface* Material, bool Create)
{
    if (!Material)
        return nullptr;
    auto Data = Cast<UP4MaterialExportData>(Material->GetAssetUserDataOfClass(UP4MaterialExportData::StaticClass()));
    if (!Data && Create)
    {
        Material->Modify();
        Data = NewObject<UP4MaterialExportData>(Material, NAME_None, RF_Transactional);
        Material->AddAssetUserData(Data);
        Material->MarkPackageDirty();
    }
    return Data;
}

bool UP4AuthoringTools::SetMaterialProvenance(
    UMaterialInterface* Material, const FString& Representation, const FString& OriginalMaterial, const FString& Notes)
{
    if (Representation != TEXT("Authored") && Representation != TEXT("Reconstructed") &&
        Representation != TEXT("Fallback"))
        return false;
    auto Data = MaterialExportSettings(Material, true);
    if (!Data)
        return false;
    if (Data->Representation != Representation || Data->OriginalMaterial != OriginalMaterial ||
        Data->ReconstructionNotes != Notes)
    {
        Data->Modify();
        Data->Representation = Representation;
        Data->OriginalMaterial = OriginalMaterial;
        Data->ReconstructionNotes = Notes;
        Material->MarkPackageDirty();
    }
    return true;
}

TArray<FString> UP4AuthoringTools::NativeAssetDependencies(UObject* Asset)
{
    if (!Asset)
        return {};
    UPackage* Package = Asset->GetOutermost();
    // Never traverse referenced packages: the Python planner checks their provenance.
    // A placed actor shares its package with the whole world. Read only that actor
    // and its components; unrelated geometry is not a runtime light dependency.
    struct FDependencies : FArchiveUObject
    {
        UPackage* Owner;
        TSet<FString> Packages;
        explicit FDependencies(UPackage* InOwner) : Owner(InOwner)
        {
            ArIsObjectReferenceCollector = true;
            SetIsPersistent(true);
            ArIgnoreOuterRef = true;
            ArIsFilterEditorOnly = true;
        }
        using FArchiveUObject::operator<<;
        virtual FArchive& operator<<(UObject*& Object) override
        {
            const FString StagedPath = P4ImportStagedPath(Object);
            if (!StagedPath.IsEmpty())
                Packages.Add(FPackageName::ObjectPathToPackageName(StagedPath));
            else if (Object && !Object->IsEditorOnly() && Object->GetOutermost() != Owner &&
                !Object->HasAnyFlags(RF_Transient))
                Packages.Add(Object->GetOutermost()->GetName());
            return *this;
        }
        virtual FArchive& operator<<(FSoftObjectPath& Path) override
        {
            if (!Path.IsNull())
                Packages.Add(Path.GetLongPackageName());
            return *this;
        }
        virtual FArchive& operator<<(FSoftObjectPtr& Value) override
        {
            FSoftObjectPath Path = Value.ToSoftObjectPath();
            return *this << Path;
        }
    } Collector(Package);
    TArray<UObject*> Objects;
    GetObjectsWithOuter(
        Cast<AActor>(Asset) || Asset->HasAnyFlags(RF_Transient) ? Asset : static_cast<UObject*>(Package), Objects,
        true);
    if (auto Blueprint = Cast<UBlueprint>(Asset))
        if (Blueprint->GeneratedClass)
        {
            Objects.AddUnique(Blueprint->GeneratedClass);
            // Read an existing CDO; exporting dependencies must not construct one.
            if (UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject(false))
                Objects.AddUnique(Defaults);
        }
    Objects.AddUnique(Asset);
    for (UObject* Object : Objects)
        if (Object && !Object->IsEditorOnly() && (Object == Asset || !Object->HasAnyFlags(RF_Transient)))
        {
            Object->Serialize(Collector);
            // DataTable::Serialize visits its rows only for loading/saving.
            // Read each live struct with this non-mutating collector explicitly.
            if (auto Table = Cast<UDataTable>(Object))
                if (Table->RowStruct)
                    for (const auto& Row : Table->GetRowMap())
                        Table->RowStruct->SerializeBin(Collector, Row.Value);
        }
    Collector.Packages.Remove(Package->GetName());
    TArray<FString> Result = Collector.Packages.Array();
    Result.Sort();
    return Result;
}

FString UP4AuthoringTools::NativePlacementInfo(AActor* Actor)
{
    if (!IsInGameThread() || !IsValid(Actor) || Actor->HasAnyFlags(RF_ClassDefaultObject))
        return FString();
    bool Native = false;
    TArray<FString> Ancestors;
    for (UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
    {
        Ancestors.Add(Quote(Class->GetPathName()));
        if (Class->GetOutermost()->GetName() == TEXT(P4_GAME_SCRIPT_PACKAGE) &&
            (Class->GetName() == TEXT("GActor") || Class->GetName() == TEXT("GenBase")))
            Native = true;
    }
    if (!Native || !Actor->GetClass()->FindPropertyByName(TEXT("GenerateInfo")))
        return FString();
    const FString Identity = TEXT("{\"guid\":") +
        Quote(Actor->GetActorGuid().ToString(EGuidFormats::DigitsWithHyphens)) + TEXT(",\"ancestors\":[") +
        FString::Join(Ancestors, TEXT(",")) + TEXT("]");
    const auto Refuse = [&](const FString& Error) { return Identity + TEXT(",\"error\":") + Quote(Error) + TEXT("}"); };
    UClass* AssetClass = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacementInfoAsset"));
    auto List = AssetClass ? CastField<FArrayProperty>(AssetClass->FindPropertyByName(TEXT("ActorGeneratorList")))
                           : nullptr;
    auto Entry = List ? CastField<FStructProperty>(List->Inner) : nullptr;
    if (!Entry)
        return Refuse(TEXT("Original ActorGeneratorList schema is missing"));
    FStructOnScope Memory(Entry->Struct);
    uint8* Row = Memory.GetStructMemory();
    TArray<FString> Fields;
    for (TFieldIterator<FProperty> Field(Entry->Struct); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        void* Destination = Field->ContainerPtrToValuePtr<void>(Row);
        bool Mapped = false;
        if (FProperty* Source = Actor->GetClass()->FindPropertyByName(Field->GetFName()))
        {
            if (!Source->SameType(*Field) || Source->ArrayDim != Field->ArrayDim)
                return Refuse(TEXT("Original generator field has a different native actor type: ") + Field->GetName());
            Field->CopyCompleteValue(Destination, Source->ContainerPtrToValuePtr<void>(Actor));
            Mapped = true;
        }
        if (auto Soft = CastField<FSoftObjectProperty>(*Field))
            if (Field->GetFName() == TEXT("SoftRefActorClass"))
            {
                if (!Soft->PropertyClass || !Actor->GetClass()->GetClass()->IsChildOf(Soft->PropertyClass) ||
                    Field->ArrayDim != 1)
                    return Refuse(TEXT("Original SoftRefActorClass type is unqualified"));
                if (auto SoftClass = CastField<FSoftClassProperty>(*Field))
                    if (!SoftClass->MetaClass || !Actor->GetClass()->IsChildOf(SoftClass->MetaClass))
                        return Refuse(TEXT("Original SoftRefActorClass metaclass is unqualified"));
                Soft->SetPropertyValue(Destination, FSoftObjectPtr(FSoftObjectPath(Actor->GetClass())));
                Mapped = true;
            }
        if (auto Struct = CastField<FStructProperty>(*Field))
        {
            if (Field->GetFName() == TEXT("InitTransform") || Field->GetFName() == TEXT("Transform"))
            {
                if (Struct->Struct != TBaseStructure<FTransform>::Get() || Field->ArrayDim != 1)
                    return Refuse(TEXT("Original generator transform type is unqualified: ") + Field->GetName());
                *static_cast<FTransform*>(Destination) = Actor->GetActorTransform();
                Mapped = true;
            }
            else if (Field->GetFName() == TEXT("SoftRefActorClass"))
            {
                if (Struct->Struct != TBaseStructure<FSoftObjectPath>::Get() || Field->ArrayDim != 1)
                    return Refuse(TEXT("Original SoftRefActorClass struct type is unqualified"));
                *static_cast<FSoftObjectPath*>(Destination) = FSoftObjectPath(Actor->GetClass());
                Mapped = true;
            }
        }
        if (!Mapped)
            return Refuse(
                TEXT(
                    "Original generator field has no qualified actor source; constructor defaults are not recovered: ") +
                Field->GetName());
        FString Value;
        if (!JsonValue(*Field, Destination, Value))
            return Refuse(TEXT("Original generator field needs an exact adapter: ") + Field->GetName());
        Fields.Add(Quote(Field->GetName()) + TEXT(":") + Value);
    }
    return Identity + TEXT(",\"row\":{") + FString::Join(Fields, TEXT(",")) + TEXT("}}");
}

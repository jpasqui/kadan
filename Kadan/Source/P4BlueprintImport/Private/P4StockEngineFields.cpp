#include "P4StockEngineFields.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Actor.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Materials/MaterialInterface.h"
#include "P4BlueprintPlan.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"

namespace
{
bool DerivedFrom(const UObject* Object, const TCHAR* Path)
{
    for (const UClass* Class = Object ? Object->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
        if (Class->GetPathName() == Path) return true;
    return false;
}
UObject* Holder(const UObject* Object)
{
    if (const AActor* Actor = Cast<AActor>(Object)) return Actor->GetRootComponent();
    return const_cast<UObject*>(Object);
}
UP4StockEngineFields* Data(const UObject* Object)
{
    IInterface_AssetUserData* Interface = Cast<IInterface_AssetUserData>(Holder(Object));
    return Interface ? Cast<UP4StockEngineFields>(Interface->GetAssetUserDataOfClass(UP4StockEngineFields::StaticClass())) : nullptr;
}
bool Valid(const FP4StockEngineField& Field)
{
    if (Field.Name == TEXT("C4MaterialMeshType")) return true;
    if (Field.Name == TEXT("C4CollisionMaterial")) return !Field.Material || IsValid(Field.Material);
    if (Field.Name == TEXT("C4TerrainTagContainer")) return Field.Tags.Num() <= 256;
    if (Field.Name == TEXT("C4Stencil"))
    {
        const UEnum* Enum = FindObject<UEnum>(nullptr, TEXT("/Script/Engine.EC4Stencil"));
        return Enum && Enum->GetValueByNameString(Field.Choice.ToString()) != INDEX_NONE;
    }
    return false;
}
FString StencilChoices()
{
    const UEnum* Enum = FindObject<UEnum>(nullptr, TEXT("/Script/Engine.EC4Stencil"));
    if (!Enum) return TEXT(" (no /Script/Engine.EC4Stencil)");
    TArray<FString> Names;
    for (int32 At = 0; At < Enum->NumEnums(); ++At) Names.Add(Enum->GetNameByIndex(At).ToString());
    return TEXT(" (choices ") + FString::Join(Names, TEXT(", ")) + TEXT(")");
}

}

bool P4CanStoreStockEngineField(const UObject* Target, FName Name)
{
    if (!Target || Target->GetClass()->FindPropertyByName(Name)) return false;
    const bool Primitive = Target->IsA<UPrimitiveComponent>();
    const bool Landscape = DerivedFrom(Target, TEXT("/Script/Landscape.LandscapeProxy"));
    if (Name == TEXT("C4MaterialMeshType")) return Primitive;
    if (Name == TEXT("C4Stencil")) return Primitive || Landscape;
    if (Name == TEXT("C4CollisionMaterial")) return Landscape;
    return Name == TEXT("C4TerrainTagContainer") && DerivedFrom(Target, TEXT("/Script/Landscape.LandscapeHeightfieldCollisionComponent"));
}

TArray<FP4StockEngineField> P4GetStockEngineFields(const UObject* Target)
{
    UP4StockEngineFields* Stored = Data(Target);
    return Stored ? (Target->IsA<AActor>() ? Stored->ActorFields : Stored->Fields) : TArray<FP4StockEngineField>();
}

bool P4SetStockEngineField(UObject* Target, const FP4StockEngineField& Field, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Target) || !P4CanStoreStockEngineField(Target, Field.Name) || !Valid(Field))
    {
        // Say which half failed and what was offered, so a refusal in a map report can be acted on.
        Error = TEXT("stock-engine-field-target-or-value-invalid: ") + Field.Name.ToString() +
            (Target && P4CanStoreStockEngineField(Target, Field.Name) ? TEXT(" value ") + Field.Choice.ToString() + StencilChoices() + TEXT(" on ")
                : TEXT(" target ")) + (Target ? Target->GetClass()->GetName() : TEXT("none"));
        return false;
    }
    UObject* Owner = Holder(Target);
    IInterface_AssetUserData* Interface = Cast<IInterface_AssetUserData>(Owner);
    if (!Interface) { Error = TEXT("stock-engine-field-has-no-data-owner: ") + Target->GetPathName(); return false; }
    UP4StockEngineFields* Stored = Data(Target);
    Owner->Modify();
    if (!Stored || Stored->GetOuter() != Owner)
    {
        UP4StockEngineFields* Copy = NewObject<UP4StockEngineFields>(Owner, NAME_None,
            RF_Transactional | (Owner->IsTemplate() ? RF_ArchetypeObject | RF_Public : RF_NoFlags));
        if (Stored) { Copy->Fields = Stored->Fields; Copy->ActorFields = Stored->ActorFields; }
        Interface->AddAssetUserData(Copy); Stored = Copy;
    }
    Stored->Modify();
    TArray<FP4StockEngineField>& StoredFields = Target->IsA<AActor>() ? Stored->ActorFields : Stored->Fields;
    FP4StockEngineField* Prior = StoredFields.FindByPredicate([&](const FP4StockEngineField& Item) { return Item.Name == Field.Name; });
    if (Prior) *Prior = Field; else StoredFields.Add(Field);
    Target->MarkPackageDirty(); Error.Reset(); return true;
}

bool P4OwnStockEngineFields(UPackage* Package, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Package)) { Error = TEXT("stock-engine-field-invalid-owner-package"); return false; }
    TArray<UObject*> Objects; GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
    {
        if (!IsValid(Object)) continue;
        UP4StockEngineFields* Stored = Data(Object); UObject* Owner = Holder(Object);
        if (!Stored) continue;
        if (Stored->GetOuter() == Owner)
        {
            // A class's template data is public like the engine's own component templates, so that
            // level instances made from it may name it as their archetype.
            if (Owner->IsTemplate() && !Stored->HasAnyFlags(RF_Public))
            { Stored->SetFlags(RF_Public); Owner->MarkPackageDirty(); }
            // Data instanced from a template another package keeps private (classes imported before
            // that rule) gets a fresh record of its own, so this package can be saved.
            const UObject* Archetype = Stored->GetArchetype();
            if (!Archetype || Archetype->HasAnyFlags(RF_ClassDefaultObject | RF_Public) || Archetype->GetOutermost() == Package) continue;
        }
        if (!IsValid(Owner) || Owner->GetOutermost() != Package)
        { Error = TEXT("stock-engine-field-data-owner-outside-package: ") + Object->GetPathName(); return false; }
        IInterface_AssetUserData* Interface = Cast<IInterface_AssetUserData>(Owner);
        if (!Interface) { Error = TEXT("stock-engine-field-owner-interface-missing"); return false; }
        UP4StockEngineFields* Copy = NewObject<UP4StockEngineFields>(Owner, NAME_None,
            RF_Transactional | (Owner->IsTemplate() ? RF_ArchetypeObject | RF_Public : RF_NoFlags));
        Copy->Fields = Stored->Fields; Copy->ActorFields = Stored->ActorFields;
        Owner->Modify(); Interface->AddAssetUserData(Copy); Owner->MarkPackageDirty();
        // A replaced record of this package is no longer saved with it.
        if (Stored->GetOutermost() == Package) Stored->SetFlags(RF_Transient);
    }
    Error.Reset(); return true;
}

EP4StockEngineField P4ApplyStockEngineFieldPlan(UObject* Target, const TSharedPtr<FJsonObject>& Record, FString& Error)
{
    FString Name, Type, Text; double Index = -1;
    if (!Record.IsValid() || !Record->TryGetStringField(TEXT("name"), Name) || !P4CanStoreStockEngineField(Target, FName(*Name)))
        return EP4StockEngineField::NotApplicable;
    FP4StockEngineField Field; Field.Name = FName(*Name);
    bool Ok = Record->TryGetStringField(TEXT("type"), Type) && Record->TryGetNumberField(TEXT("index"), Index) && Index == 0 &&
        Record->TryGetStringField(TEXT("text"), Text);
    if (Field.Name == TEXT("C4MaterialMeshType")) Ok = Ok && Type == TEXT("IntProperty") && LexTryParseString(Field.Integer, *Text) && FString::FromInt(Field.Integer) == Text;
    else if (Field.Name == TEXT("C4Stencil")) { Ok = Ok && Type == TEXT("EnumProperty"); Field.Choice = FName(*Text); }
    else if (Field.Name == TEXT("C4CollisionMaterial"))
    {
        FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(FP4StockEngineField::StaticStruct(), TEXT("Material"));
        TArray<FString> Refused;
        Ok = Ok && Type == TEXT("ObjectProperty") && Property && P4Plan::ImportText(Property, Text, &Field.Material, Target, Refused) && !Refused.Num();
    }
    else
    {
        // Import through the SDK's real struct, never through an engine object's missing bytes.
        FString Struct;
        UScriptStruct* Schema = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.C4TerrainTagContainer"));
        const FArrayProperty* Array = Schema ? FindFProperty<FArrayProperty>(Schema, TEXT("TerrainTags")) : nullptr;
        const FStructProperty* Row = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
        const FUInt32Property* Number = Row ? FindFProperty<FUInt32Property>(Row->Struct, TEXT("TerrainTag")) : nullptr;
        Ok = Ok && Type == TEXT("StructProperty") && Record->TryGetStringField(TEXT("struct"), Struct) && Struct == TEXT("C4TerrainTagContainer") && Array && Number;
        if (Ok)
        {
            void* Value = FMemory::Malloc(Schema->GetStructureSize(), Schema->GetMinAlignment()); Schema->InitializeStruct(Value);
            const TCHAR* End = Schema->ImportText(*Text, Value, Target, PPF_None, nullptr, TEXT("Kadan stock engine field"));
            FScriptArrayHelper Rows(Array, Array->ContainerPtrToValuePtr<void>(Value));
            Ok = End && !*End && Rows.Num() <= 256;
            if (Ok) for (int32 At = 0; At < Rows.Num(); ++At) Field.Tags.Add(Number->GetPropertyValue_InContainer(Rows.GetRawPtr(At)));
            Schema->DestroyStruct(Value); FMemory::Free(Value);
        }
    }
    if (!Ok || !P4SetStockEngineField(Target, Field, Error))
    { if (Error.IsEmpty()) Error = TEXT("stock-engine-field-plan-type-or-value-invalid: ") + Name; return EP4StockEngineField::Refused; }
    return EP4StockEngineField::Stored;
}

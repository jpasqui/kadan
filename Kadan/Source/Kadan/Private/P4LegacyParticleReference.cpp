#include "P4LegacyParticleReference.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4CreatorSource.h"
#include "P4LevelContent.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "Dom/JsonObject.h"
#include "Particles/ParticleSystem.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

namespace
{
FString ExpectedIdentityRecord(const UObject* Asset)
{
    // Exact literal record emitted by the historical reference producer, including
    // its explicitly absent source/comparison evidence and single known refusal.
    auto Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 2);
    Json->SetStringField(TEXT("owner"), TEXT("p4mapkit-original"));
    Json->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
    Json->SetStringField(TEXT("class"), TEXT("/Script/Engine.ParticleSystem"));
    Json->SetStringField(TEXT("kind"), TEXT("particle-reference"));
    Json->SetStringField(TEXT("source_digest"), FString());
    Json->SetStringField(TEXT("source_digest_kind"), TEXT("not-recovered"));
    Json->SetStringField(TEXT("comparison_digest"), FString());
    Json->SetStringField(TEXT("comparison_schema"), TEXT("particle-identity-reference-v1"));
    Json->SetStringField(TEXT("fidelity"), TEXT("identity-only"));
    Json->SetBoolField(TEXT("reference_only"), true);
    Json->SetBoolField(TEXT("read_only"), true);
    Json->SetStringField(TEXT("legacy_key"), FString());
    Json->SetStringField(TEXT("legacy_evidence"), FString());
    auto Note = MakeShared<FJsonObject>();
    Note->SetStringField(TEXT("kind"), TEXT("unsupported-particle-module"));
    Note->SetStringField(TEXT("object"), Asset->GetPathName());
    Note->SetStringField(TEXT("property"), FString());
    Note->SetStringField(TEXT("game_value"),
        TEXT("asset-import-class-not-in-the-project: /Script/Engine.ParticleModuleLocationPrimitiveC4Sphere"));
    Note->SetStringField(TEXT("action"), TEXT("not-held"));
    Json->SetArrayField(TEXT("values"), {MakeShared<FJsonValueObject>(Note)});
    FString Text = P4CondensedJson(Json);
    return Text;
}

bool ExactValue(const FProperty* Property, const void* Value, const void* Default, int32 Depth)
{
    if (!Property || Depth > 32 || !Property->Identical(Value, Default, PPF_None))
        return false;
    if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
        return !Number->IsFloatingPoint() || FMemory::Memcmp(Value, Default, Property->ElementSize) == 0;
    if (CastField<FBoolProperty>(Property) || CastField<FEnumProperty>(Property) ||
        CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property))
        return true;
    if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
        return !Object->GetObjectPropertyValue(Value) && !Object->GetObjectPropertyValue(Default);
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Current(Array, Value), Canonical(Array, Default);
        // This original producer has no arrays or object payload. Unknown future
        // nonempty native defaults cannot authorize discarding any saved values.
        return !Current.Num() && !Canonical.Num();
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        const FString Path = Struct->Struct ? Struct->Struct->GetPathName() : FString();
        if (Path != TEXT("/Script/CoreUObject.Vector") && Path != TEXT("/Script/CoreUObject.Rotator") &&
            Path != TEXT("/Script/CoreUObject.Box") && Path != TEXT("/Script/CoreUObject.Color"))
            return false;
        // These stock structs serialize their complete reflected scalar members.
        // Check float words too; native math equality alone treats +/-zero alike.
        for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
            for (int32 Index = 0; Index < It->ArrayDim; ++Index)
                if (!ExactValue(*It, It->ContainerPtrToValuePtr<void>(Value, Index),
                        It->ContainerPtrToValuePtr<void>(Default, Index), Depth + 1))
                    return false;
        return true;
    }
    return false; // No unsupported delegate, map, set, text or custom native struct.
}

bool DefaultFields(const UObject* Asset, FString& Error)
{
    const UObject* Canonical = UParticleSystem::StaticClass()->GetDefaultObject();
    if (!IsValid(Canonical))
    {
        Error = TEXT("legacy-particle-native-defaults-unavailable");
        return false;
    }
    for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
    {
        // Include inherited and editor-only persistent fields. No thumbnail,
        // default value, array, bounds or stored derived property is ignored.
        if (It->HasAnyPropertyFlags(CPF_Transient))
            continue;
        for (int32 Index = 0; Index < It->ArrayDim; ++Index)
            if (!ExactValue(*It, It->ContainerPtrToValuePtr<void>(Asset, Index),
                    It->ContainerPtrToValuePtr<void>(Canonical, Index), 0))
            {
                Error = TEXT("legacy-particle-current-field-not-canonical: ") + It->GetName();
                return false;
            }
    }
    return true;
}

bool Identity(UObject* Asset, FString& Marker, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Asset) || Asset->GetClass() != UParticleSystem::StaticClass() ||
        !Asset->IsAsset() ||
        Asset->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_Transient))
    {
        Error = TEXT("legacy-particle-current-identity-not-qualified");
        return false;
    }
    UPackage* Package = Asset->GetOutermost();
    const FString Name = Package->GetName();
    if (!P4IsValidContentPackage(Name) || Package->IsDirty() ||
        !P4ExactNativeString(Asset->GetPathName(), P4ObjectPathOf(Name)) || P4HasCreatorSource(Asset) ||
        P4HasCopiedGameData(Asset))
    {
        Error = TEXT("legacy-particle-current-owner-not-qualified");
        return false;
    }
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
        if (IsValid(Object) && Object != Asset &&
            (Object != Package->MetaData || Object->GetOuter() != Package ||
                Object->GetClass() != UMetaData::StaticClass()))
        {
            Error = TEXT("legacy-particle-current-extra-object");
            return false;
        }
    const auto* Metadata = UMetaData::GetMapForObject(Asset);
    if (!Metadata || Metadata->Num() != 1 || !Metadata->Contains(TEXT("P4OriginalRecord")))
    {
        Error = TEXT("legacy-particle-current-metadata-not-qualified");
        return false;
    }
    Marker = Metadata->FindChecked(TEXT("P4OriginalRecord"));
    if (!P4ExactNativeString(Marker, ExpectedIdentityRecord(Asset)))
    {
        Error = TEXT("legacy-particle-current-record-not-qualified");
        return false;
    }
    return true;
}
} //namespace

bool P4ProveCanonicalLegacyParticleReference(UObject* Asset, FString& Error)
{
    Error.Reset();
    FString Before;
    if (!Identity(Asset, Before, Error) || !DefaultFields(Asset, Error))
        return false;
    UPackage* Package = Asset->GetOutermost();
    TArray<FString> Lines;
    const FString State = P4OriginalObjectAssetStateAndLines(Asset, Lines);
    if (State.Len() != 64 || !P4LegacyParticleReference::CanonicalCookLines(Asset->GetName(), Lines))
    {
        Error = TEXT("legacy-particle-current-cook-not-canonical");
        return false;
    }
    FString After;
    if (Asset->GetOutermost() != Package || !Identity(Asset, After, Error) || !P4ExactNativeString(Before, After) ||
        !DefaultFields(Asset, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("legacy-particle-state-changed-during-proof");
        return false;
    }
    return true;
}

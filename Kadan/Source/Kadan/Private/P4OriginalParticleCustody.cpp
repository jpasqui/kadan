#include "P4RecoveredParticleSystem.h"
#include "P4Common.h"
#include "P4OriginalParticlePreflight.h"
#include "P4KadanImportSources.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/particle_cutout.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Distributions/DistributionFloat.h"
#include "Distributions/DistributionVector.h"
#include "Engine/Texture2D.h"
#include "DerivedDataCacheInterface.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/StructOnScope.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "Serialization/ArchiveUObject.h"

UObject* P4FindOriginalParticleObject(UParticleSystem* System, const FString& SourceRelative)
{
    if (!System || SourceRelative.IsEmpty() || SourceRelative.Len() > 4096 || SourceRelative.Contains(TEXT("/")) ||
        SourceRelative.Contains(TEXT(":")) || SourceRelative.Contains(TEXT("\\")))
        return nullptr;
    TArray<FString> Parts;
    SourceRelative.ParseIntoArray(Parts, TEXT("."), false);
    if (Parts.Num() < 2 || Parts.Num() > 128 || !Parts[0].Equals(System->GetName(), ESearchCase::CaseSensitive))
        return nullptr;
    UObject* Object = System;
    for (int32 Index = 1; Index < Parts.Num(); ++Index)
    {
        if (Parts[Index].IsEmpty())
            return nullptr;
        UObject* Child = FindObjectFast<UObject>(Object, FName(*Parts[Index]));
        if (!Child || Child->GetOuter() != Object || Child->GetOutermost() != System->GetOutermost() ||
            !Child->GetName().Equals(Parts[Index], ESearchCase::CaseSensitive))
            return nullptr;
        Object = Child;
    }
    return Object->IsIn(System) ? Object : nullptr;
}

// Source values the editor's own rebuilds replace on every load: the baked tables of
// reconstructed curves (a float curve is always rebaked on load) and the resource data of
// GPU emitters (rebuilt from the curves by the emitter's load). Custody holds the game's
// exact bytes and puts them back while the curves and emitter inputs are unchanged.
namespace P4OriginalParticleHeld
{
struct FCurve
{
    TWeakObjectPtr<UObject> Module;
    FP4ParticleRawDistributionPlace Place;
    TArray<uint8> Table;
};
// The GPU emitter fields the emitter build writes: its resource data and its emitter info.
const TCHAR* const GpuFields[] = {TEXT("ResourceData"), TEXT("EmitterInfo")};
struct FGpu
{
    TWeakObjectPtr<UParticleModuleTypeDataGpu> Module;
    TArray<uint8> Fields[2];
};
// Fields the engine's load or emitter build works out again from other values: a dynamic parameter's
// update flags and the peak particle counts. The game's file keeps what Nintendo's engine worked out,
// which this engine's rules can put differently.
struct FDerivedField
{
    const TCHAR* Class;
    const TCHAR* Field;
};
const FDerivedField DerivedFields[] = {
    {TEXT("/Script/Engine.ParticleModuleParameterDynamic"), TEXT("UpdateFlags")},
    {TEXT("/Script/Engine.ParticleModuleParameterDynamic"), TEXT("bUsesVelocity")},
    {TEXT("/Script/Engine.ParticleLODLevel"), TEXT("PeakActiveParticles")},
    {TEXT("/Script/Engine.ParticleEmitter"), TEXT("PeakActiveParticles")},
};
struct FDerived
{
    TWeakObjectPtr<UObject> Object;
    FString Field, Game;
};
struct FSystem
{
    TArray<FCurve> Curves;
    TArray<FGpu> Gpu;
    TArray<FDerived> Derived;
};
TMap<TWeakObjectPtr<UParticleSystem>, FSystem>& Held()
{
    static TMap<TWeakObjectPtr<UParticleSystem>, FSystem> Map;
    return Map;
}

bool Fail(FString& Error, const FString& Why)
{
    Error = Why;
    return false;
}
bool StructBytes(const UScriptStruct* Struct, const void* Value, TArray<uint8>& Bytes)
{
    // Object references are written as their paths, so emitter info keeps its module links.
    Bytes.Reset();
    FMemoryWriter Memory(Bytes, true);
    Memory.SetWantBinaryPropertySerialization(true);
    FObjectAndNameAsStringProxyArchive Writer(Memory, false);
    Struct->SerializeBin(Writer, const_cast<void*>(Value));
    return !Writer.IsError() && !Memory.IsError();
}
bool ReadStructBytes(const UScriptStruct* Struct, void* Value, const TArray<uint8>& Bytes)
{
    // A failed or partial read never leaves a half-written value.
    FStructOnScope Scratch(Struct);
    FMemoryReader Memory(Bytes, true);
    Memory.SetWantBinaryPropertySerialization(true);
    FObjectAndNameAsStringProxyArchive Reader(Memory, false);
    Struct->SerializeBin(Reader, Scratch.GetStructMemory());
    if (Reader.IsError() || Memory.IsError() || Memory.Tell() != Bytes.Num())
        return false;
    Struct->CopyScriptStruct(Value, Scratch.GetStructMemory());
    return true;
}
bool BytesDigest(const TArray<uint8>& Bytes, FString& Digest)
{
    return P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Take)
        {
            for (int32 At = 0; At < Bytes.Num(); At += 65536)
                if (!Take(Bytes.GetData() + At, FMath::Min(65536, Bytes.Num() - At)))
                    return false;
            return true;
        },
        Digest);
}
const FStructProperty* TableField(const FStructProperty* Raw)
{
    const auto* Table = Raw ? FindFProperty<FStructProperty>(Raw->Struct, TEXT("Table")) : nullptr;
    return Table && Table->Struct && Table->ArrayDim == 1 ? Table : nullptr;
}
const FStructProperty* GpuField(int32 Index)
{
    const auto* Field = FindFProperty<FStructProperty>(UParticleModuleTypeDataGpu::StaticClass(), GpuFields[Index]);
    return Field && Field->Struct && Field->ArrayDim == 1 ? Field : nullptr;
}
UObject* Distribution(const FStructProperty* Raw, void* Value)
{
    const auto* Reference = FindFProperty<FObjectProperty>(Raw->Struct, TEXT("Distribution"));
    return Reference ? Reference->GetObjectPropertyValue_InContainer(Value) : nullptr;
}
// The object's own persistent values, with object references as paths. Bake bookkeeping
// and a GPU emitter's built resource data are left out: they follow from the rest.
void Describe(UObject* Object, FArchive& Writer)
{
    FString Name = Object->GetClass()->GetPathName() + TEXT("|") + Object->GetName();
    Writer << Name;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
            continue;
        const FName Field = It->GetFName();
        if (Field == TEXT("bIsDirty") || Field == TEXT("bBakedDataSuccesfully") ||
            ((Field == TEXT("ResourceData") || Field == TEXT("EmitterInfo")) &&
                Object->IsA<UParticleModuleTypeDataGpu>()))
            continue;
        FString Label = It->GetName();
        Writer << Label;
        for (int32 Index = 0; Index < It->ArrayDim; ++Index)
        {
            FStructuredArchiveFromArchive Structured(Writer);
            It->SerializeItem(Structured.GetSlot(), It->ContainerPtrToValuePtr<void>(Object, Index), nullptr);
        }
    }
}
template <typename TFill>
bool DigestOf(TFill Fill, FString& Digest)
{
    TArray<uint8> Bytes;
    FMemoryWriter Memory(Bytes, true);
    Memory.SetWantBinaryPropertySerialization(true);
    FObjectAndNameAsStringProxyArchive Writer(Memory, false);
    Fill(static_cast<FArchive&>(Writer));
    return !Writer.IsError() && BytesDigest(Bytes, Digest);
}
bool CurveDigest(UObject* Curve, FString& Digest)
{
    return Curve && DigestOf([&](FArchive& Writer) { Describe(Curve, Writer); }, Digest);
}
// Everything the emitter's GPU build reads: its top level modules and their curves.
bool GpuInputs(UParticleSystem* System, UParticleModuleTypeDataGpu* Module, FString& Digest)
{
    // Every emitter whose first level of detail draws with the module builds it, in the system's order (a
    // game system can give one GPU module to two emitters). The inputs are all of theirs, so the module's
    // built data is the source's only while all of them are.
    TArray<UObject*> Inputs;
    for (UParticleEmitter* Emitter : System->Emitters)
        if (Emitter && Emitter->LODLevels.Num() && Emitter->LODLevels[0] &&
            Emitter->LODLevels[0]->TypeDataModule == Module)
        {
            UParticleLODLevel* Lod = Emitter->LODLevels[0];
            Inputs.Add(Lod->RequiredModule);
            Inputs.Add(Lod->SpawnModule);
            for (UParticleModule* One : Lod->Modules)
                Inputs.Add(One);
            Inputs.Add(Lod->TypeDataModule);
        }
    if (!Inputs.Num())
        return false;
    return DigestOf(
        [&](FArchive& Writer)
        {
            for (UObject* Input : Inputs)
            {
                if (!Input)
                {
                    FString None = TEXT("none");
                    Writer << None;
                    continue;
                }
                Describe(Input, Writer);
                P4ForEachParticleRawDistribution(Input,
                    [&](const FP4ParticleRawDistributionPlace&, const FStructProperty* Raw, void* Value)
                    {
                        if (UObject* Curve = Distribution(Raw, Value))
                            Describe(Curve, Writer);
                        return true;
                    });
            }
        },
        Digest);
}
TArray<UParticleModuleTypeDataGpu*> GpuModules(UParticleSystem* System)
{
    TArray<UParticleModuleTypeDataGpu*> Found;
    for (UParticleEmitter* Emitter : System->Emitters)
        if (Emitter && Emitter->LODLevels.Num() && Emitter->LODLevels[0])
            if (auto* Module = Cast<UParticleModuleTypeDataGpu>(Emitter->LODLevels[0]->TypeDataModule))
                if (Module->GetClass() == UParticleModuleTypeDataGpu::StaticClass())
                    Found.AddUnique(Module);
    return Found;
}
FString Relative(UParticleSystem* System, UObject* Object)
{
    // The source inventory spelling of the object: asset name and outer names joined by dots.
    TArray<FString> Names;
    for (UObject* At = Object; At && At != System->GetOutermost(); At = At->GetOuter())
        Names.Insert(At->GetName(), 0);
    return FString::Join(Names, TEXT("."));
}
FProperty* DerivedProperty(UObject* Object, const FString& Field)
{
    if (!Object)
        return nullptr;
    for (const FDerivedField& One : DerivedFields)
    {
        const UClass* Class = FindObject<UClass>(nullptr, One.Class);
        if (Field == One.Field && Class && Object->IsA(Class))
            return Class->FindPropertyByName(One.Field);
    }
    return nullptr;
}
FString DerivedText(UObject* Object, FProperty* Property)
{
    FString Text;
    Property->ExportTextItem(Text, Property->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_None);
    return Text;
}
bool SetDerivedText(UObject* Object, FProperty* Property, const FString& Text)
{
    const TCHAR* End = Property->ImportText(*Text, Property->ContainerPtrToValuePtr<void>(Object), PPF_None, Object);
    return End && !*End && DerivedText(Object, Property) == Text;
}
bool SetTableValue(const FStructProperty* Raw, void* Value, const TArray<uint8>& Bytes)
{
    const auto* Table = TableField(Raw);
    return Table && ReadStructBytes(Table->Struct, Table->ContainerPtrToValuePtr<void>(Value), Bytes);
}
void MarkClean(UObject* Curve)
{
    if (auto* Float = Cast<UDistributionFloat>(Curve))
        Float->bIsDirty = false;
    else if (auto* Vector = Cast<UDistributionVector>(Curve))
        Vector->bIsDirty = false;
}

// Puts back each held table whose curve is unchanged. A changed curve is the creator's
// and keeps the editor's own bake.
bool ApplyCurves(UParticleSystem* System, const TSharedPtr<FJsonObject>& Document, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Document->TryGetArrayField(TEXT("curves"), Rows))
        return true;
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Object, Field, Member, Curve, Encoded;
        double Index = -1;
        if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row ||
            !(*Row)->TryGetStringField(TEXT("object"), Object) || !(*Row)->TryGetStringField(TEXT("field"), Field) ||
            !(*Row)->TryGetStringField(TEXT("member"), Member) || !(*Row)->TryGetNumberField(TEXT("index"), Index) ||
            Index < 0 || Index > 65535 || !(*Row)->TryGetStringField(TEXT("curve"), Curve) ||
            !(*Row)->TryGetStringField(TEXT("table"), Encoded))
            return Fail(Error, TEXT("particle-custody-curve-row-unreadable"));
        UObject* Module = P4FindOriginalParticleObject(System, Object);
        FP4ParticleRawDistributionPlace Place;
        Place.Field = Field;
        Place.Member = Member;
        Place.Index = int32(Index);
        const FStructProperty* Raw = nullptr;
        void* RawValue = nullptr;
        TArray<uint8> Table;
        FString Current;
        if (!Module || !P4FindParticleRawDistribution(Module, Place, Raw, RawValue) || !FBase64::Decode(Encoded, Table))
            return Fail(Error, TEXT("particle-custody-curve-place-differs: ") + Object + TEXT(".") + Field);
        UObject* Made = Distribution(Raw, RawValue);
        if (!Made || !CurveDigest(Made, Current) || !P4ExactNativeString(Current, Curve))
            continue;
        if (!SetTableValue(Raw, RawValue, Table))
            return Fail(Error, TEXT("particle-custody-curve-table-unreadable: ") + Object + TEXT(".") + Field);
        MarkClean(Made);
    }
    return true;
}
// Puts back each held GPU emitter build whose inputs are unchanged.
bool ApplyGpu(UParticleSystem* System, const TSharedPtr<FJsonObject>& Document, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Document->TryGetArrayField(TEXT("gpu"), Rows))
        return true;
    if (!GpuField(0) || !GpuField(1))
        return Fail(Error, TEXT("particle-custody-gpu-field-unavailable"));
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Object, Inputs, Encoded[2], Current;
        TArray<uint8> Bytes[2];
        if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row ||
            !(*Row)->TryGetStringField(TEXT("object"), Object) || !(*Row)->TryGetStringField(TEXT("inputs"), Inputs) ||
            !(*Row)->TryGetStringField(TEXT("resource"), Encoded[0]) ||
            !(*Row)->TryGetStringField(TEXT("emitter"), Encoded[1]) || !FBase64::Decode(Encoded[0], Bytes[0]) ||
            !FBase64::Decode(Encoded[1], Bytes[1]))
            return Fail(Error, TEXT("particle-custody-gpu-row-unreadable"));
        auto* Module = Cast<UParticleModuleTypeDataGpu>(P4FindOriginalParticleObject(System, Object));
        if (!Module || Module->GetClass() != UParticleModuleTypeDataGpu::StaticClass())
            return Fail(Error, TEXT("particle-custody-gpu-module-differs: ") + Object);
        if (!GpuInputs(System, Module, Current) || !P4ExactNativeString(Current, Inputs))
            continue;
        for (int32 Index = 0; Index < 2; ++Index)
            if (!ReadStructBytes(
                    GpuField(Index)->Struct, GpuField(Index)->ContainerPtrToValuePtr<void>(Module), Bytes[Index]))
                return Fail(Error, TEXT("particle-custody-gpu-build-unreadable: ") + Object);
    }
    return true;
}
// Puts back each held derived value where the engine still works out what it did at import. Where it
// works out something else, the values it is made from were edited and the engine's value stands.
bool ApplyDerived(UParticleSystem* System, const TSharedPtr<FJsonObject>& Document, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Document->TryGetArrayField(TEXT("derived"), Rows))
        return true;
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
        FString Object, Field, Game, Engine;
        if (!Row.IsValid() || !Row->TryGetStringField(TEXT("object"), Object) ||
            !Row->TryGetStringField(TEXT("field"), Field) || !Row->TryGetStringField(TEXT("game"), Game) ||
            !Row->TryGetStringField(TEXT("engine"), Engine))
            return Fail(Error, TEXT("particle-custody-derived-row-unreadable"));
        UObject* Made = P4FindOriginalParticleObject(System, Object);
        FProperty* Property = DerivedProperty(Made, Field);
        if (!Property || !P4ExactNativeString(DerivedText(Made, Property), Engine))
            continue;
        if (!SetDerivedText(Made, Property, Game))
            return Fail(Error, TEXT("particle-custody-derived-value-unwritable: ") + Object + TEXT(".") + Field);
    }
    return true;
}
// Called by the capture after the cutouts are restored, which rebuilds the emitters.
bool Capture(UParticleSystem* System, TSharedRef<FJsonObject> Document, FString& Error)
{
    FSystem Source;
    if (!Held().RemoveAndCopyValue(System, Source))
        return true;
    TArray<TSharedPtr<FJsonValue>> Curves, Gpu;
    for (const FCurve& One : Source.Curves)
    {
        UObject* Module = One.Module.Get();
        const FStructProperty* Raw = nullptr;
        void* RawValue = nullptr;
        TArray<uint8> Now;
        FString Digest;
        if (!Module || !P4FindParticleRawDistribution(Module, One.Place, Raw, RawValue) || !TableField(Raw))
            return Fail(Error, TEXT("particle-custody-curve-place-unavailable"));
        // The import keeps the source table. Any rebake before here is an error, not a value.
        if (!StructBytes(TableField(Raw)->Struct, TableField(Raw)->ContainerPtrToValuePtr<void>(RawValue), Now) ||
            Now != One.Table)
            return Fail(Error,
                TEXT("particle-custody-curve-table-changed-during-import: ") + Module->GetPathName() + TEXT(".") +
                    One.Place.Field);
        if (!CurveDigest(Distribution(Raw, RawValue), Digest))
            return Fail(Error, TEXT("particle-custody-curve-unavailable"));
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("object"), Relative(System, Module));
        Row->SetStringField(TEXT("field"), One.Place.Field);
        Row->SetStringField(TEXT("member"), One.Place.Member);
        Row->SetNumberField(TEXT("index"), One.Place.Index);
        Row->SetStringField(TEXT("curve"), Digest);
        Row->SetStringField(TEXT("table"), FBase64::Encode(One.Table));
        Curves.Add(MakeShared<FJsonValueObject>(Row));
    }
    if (Source.Gpu.Num() && (!GpuField(0) || !GpuField(1)))
        return Fail(Error, TEXT("particle-custody-gpu-field-unavailable"));
    for (const FGpu& One : Source.Gpu)
    {
        UParticleModuleTypeDataGpu* Module = One.Module.Get();
        FString Inputs;
        if (!Module || !GpuInputs(System, Module, Inputs))
            return Fail(Error, TEXT("particle-custody-gpu-build-not-restored"));
        for (int32 Index = 0; Index < 2; ++Index)
            if (!ReadStructBytes(
                    GpuField(Index)->Struct, GpuField(Index)->ContainerPtrToValuePtr<void>(Module), One.Fields[Index]))
                return Fail(Error, TEXT("particle-custody-gpu-build-not-restored"));
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("object"), Relative(System, Module));
        Row->SetStringField(TEXT("inputs"), Inputs);
        Row->SetStringField(TEXT("resource"), FBase64::Encode(One.Fields[0]));
        Row->SetStringField(TEXT("emitter"), FBase64::Encode(One.Fields[1]));
        Gpu.Add(MakeShared<FJsonValueObject>(Row));
    }
    // A derived value the engine works out as the game's file has it needs nothing kept.
    TArray<TSharedPtr<FJsonValue>> Derived;
    for (const FDerived& One : Source.Derived)
    {
        UObject* Made = One.Object.Get();
        FProperty* Property = DerivedProperty(Made, One.Field);
        if (!Property)
            return Fail(Error, TEXT("particle-custody-derived-place-unavailable"));
        const FString Engine = DerivedText(Made, Property);
        if (P4ExactNativeString(Engine, One.Game))
            continue;
        if (!SetDerivedText(Made, Property, One.Game))
            return Fail(Error, TEXT("particle-custody-derived-value-unwritable: ") + Made->GetPathName());
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("object"), Relative(System, Made));
        Row->SetStringField(TEXT("field"), One.Field);
        Row->SetStringField(TEXT("game"), One.Game);
        Row->SetStringField(TEXT("engine"), Engine);
        Derived.Add(MakeShared<FJsonValueObject>(Row));
    }
    if (Derived.Num())
        Document->SetArrayField(TEXT("derived"), Derived);
    if (Curves.Num())
        Document->SetArrayField(TEXT("curves"), Curves);
    if (Gpu.Num())
        Document->SetArrayField(TEXT("gpu"), Gpu);
    return true;
}
} //namespace P4OriginalParticleHeld

bool P4HoldOriginalParticleSource(
    UParticleSystem* System, const TArray<TPair<UObject*, FP4ParticleRawDistributionPlace>>& Curves, FString& Error)
{
    using namespace P4OriginalParticleHeld;
    if (!IsInGameThread() || !System)
        return Fail(Error, TEXT("particle-custody-hold-system-missing"));
    // Entries of systems that are gone are dropped here, so the map stays bounded.
    for (auto It = Held().CreateIterator(); It; ++It)
        if (!It.Key().IsValid())
            It.RemoveCurrent();
    FSystem Source;
    for (const auto& Pair : Curves)
    {
        const FStructProperty* Raw = nullptr;
        void* RawValue = nullptr;
        FCurve One;
        if (!Pair.Key || !P4FindParticleRawDistribution(Pair.Key, Pair.Value, Raw, RawValue) || !TableField(Raw) ||
            !StructBytes(TableField(Raw)->Struct, TableField(Raw)->ContainerPtrToValuePtr<void>(RawValue), One.Table))
            return Fail(Error, TEXT("particle-custody-hold-curve-table-unreadable"));
        One.Module = Pair.Key;
        One.Place = Pair.Value;
        Source.Curves.Add(MoveTemp(One));
    }
    for (UParticleModuleTypeDataGpu* Module : GpuModules(System))
    {
        FGpu One;
        One.Module = Module;
        for (int32 Index = 0; Index < 2; ++Index)
            if (!GpuField(Index) ||
                !StructBytes(
                    GpuField(Index)->Struct, GpuField(Index)->ContainerPtrToValuePtr<void>(Module), One.Fields[Index]))
                return Fail(Error, TEXT("particle-custody-hold-gpu-build-unreadable"));
        Source.Gpu.Add(MoveTemp(One));
    }
    // Before the load lifecycle runs, the objects hold the game's values for the fields it works out.
    TArray<UObject*> Objects;
    GetObjectsWithOuter(System, Objects, true);
    for (UObject* Object : Objects)
        for (const FDerivedField& Field : DerivedFields)
        {
            const UClass* Class = FindObject<UClass>(nullptr, Field.Class);
            FProperty* Property = Class && Object->IsA(Class) ? Class->FindPropertyByName(Field.Field) : nullptr;
            if (!Property)
                continue;
            FDerived One;
            One.Object = Object;
            One.Field = Field.Field;
            One.Game = DerivedText(Object, Property);
            Source.Derived.Add(MoveTemp(One));
        }
    Held().Add(System, MoveTemp(Source));
    Error.Reset();
    return true;
}

namespace P4OriginalParticleCustody
{
const TCHAR* const Key = TEXT("P4OriginalParticleCutoutCustody");
const TCHAR* const NoteKind = TEXT("particle-native-source-custody");
constexpr int32 MaximumText = 32 * 1024 * 1024;
constexpr int32 MaximumModules = 4096;

bool Fail(FString& Error, const TCHAR* Why)
{
    Error = Why;
    return false;
}
bool Hex(const FString& Value, int32 Count)
{
    if (Value.Len() != Count)
        return false;
    for (TCHAR C : Value)
        if (!FChar::IsHexDigit(C))
            return false;
    return true;
}
FString FloatWord(float Value)
{
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, 4);
    return FString::Printf(TEXT("%08x"), Bits);
}
bool Geometry(const TArray<uint8>& Tail, P4MapKit::ParticleCutout::FRequiredTail& Decoded, FString& Error)
{
    std::string Why;
    if (!P4MapKit::ParticleCutout::Read(Tail.GetData(), Tail.Num(), Decoded, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    return true;
}
bool SourceHierarchy(const P4MapKit::FPackageInventory& Package, int32 Index, UObject* Object, UParticleSystem* System)
{
    TSet<int32> Seen;
    while (Index)
    {
        if (Index < 1 || std::size_t(Index) > Package.Exports.size() || Seen.Contains(Index) || Seen.Num() >= 128 ||
            !Object)
            return false;
        Seen.Add(Index);
        const auto& Export = Package.Exports[std::size_t(Index - 1)];
        std::string Class;
        if (!Package.Reference(Export.Class, Class) ||
            !Object->GetName().Equals(UTF8_TO_TCHAR(Export.Name.c_str()), ESearchCase::CaseSensitive))
            return false;
        if (!Export.Outer)
            return Object == System && Class == "/Script/Engine.ParticleSystem" &&
                Object->GetOuter() == System->GetOutermost();
        if (!Object->GetClass()->GetPathName().Equals(UTF8_TO_TCHAR(Class.c_str()), ESearchCase::CaseSensitive))
            return false;
        Object = Object->GetOuter();
        Index = Export.Outer;
    }
    return false;
}
bool Signature(UParticleModuleRequired* Module, FString& Out, FString& Error)
{
    if (!Module || Module->GetClass() != UParticleModuleRequired::StaticClass() || Module->SubImages_Horizontal < 1 ||
        Module->SubImages_Vertical < 1 || int64(Module->SubImages_Horizontal) * Module->SubImages_Vertical > 131072 ||
        (Module->BoundingMode != BVC_FourVertices && Module->BoundingMode != BVC_EightVertices) ||
        Module->OpacitySourceMode < OSM_Alpha || Module->OpacitySourceMode > OSM_BlueChannel ||
        !FMath::IsFinite(Module->AlphaThreshold))
        return Fail(Error, TEXT("particle-custody-native-module-parameters-unqualified"));
    FString Texture, Guid;
    if (Module->CutoutTexture)
    {
#if WITH_EDITORONLY_DATA
        if (!Module->CutoutTexture->Source.IsValid() || !Module->CutoutTexture->Source.GetId().IsValid())
            return Fail(Error, TEXT("particle-custody-actual-texture-source-unavailable"));
        Texture = Module->CutoutTexture->GetPathName();
        Guid = Module->CutoutTexture->Source.GetId().ToString(EGuidFormats::Digits);
#else
        return Fail(Error, TEXT("particle-custody-editor-source-required"));
#endif
    }
    Out = FString::Printf(TEXT("%d/%d/%d/%s/%d/%s/%s"), Module->SubImages_Horizontal, Module->SubImages_Vertical,
        int32(Module->BoundingMode), *FloatWord(Module->AlphaThreshold), int32(Module->OpacitySourceMode), *Texture,
        *Guid);
    return true;
}
bool Shape(UParticleModuleRequired* Module, const P4MapKit::ParticleCutout::FRequiredTail& Decoded, FString& Error)
{
    const std::size_t Expected = Module->CutoutTexture
        ? std::size_t(Module->SubImages_Horizontal) * Module->SubImages_Vertical * Module->GetNumBoundingVertices() * 2
        : 0;
    if (Decoded.CoordinateBits.size() == Expected)
        return true;
    Error = FString::Printf(
        TEXT(
            "particle-custody-native-geometry-frame-count-differs: %s source words %d expected %d texture %s frames %dx%d vertices %d"),
        *Module->GetPathName(), int32(Decoded.CoordinateBits.size()), int32(Expected),
        Module->CutoutTexture ? *Module->CutoutTexture->GetPathName() : TEXT("none"), Module->SubImages_Horizontal,
        Module->SubImages_Vertical, Module->GetNumBoundingVertices());
    return false;
}
struct FPropertySnapshot
{
    const FProperty* Property = nullptr;
    void* Storage = nullptr;
    explicit FPropertySnapshot(const FProperty* InProperty, UObject* Owner) : Property(InProperty)
    {
        Storage = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Storage);
        Property->CopyCompleteValue(Storage, Property->ContainerPtrToValuePtr<void>(Owner));
    }
    ~FPropertySnapshot()
    {
        Property->DestroyValue(Storage);
        FMemory::Free(Storage);
    }
};
bool Refresh(UParticleModuleRequired* Module, const TArray<uint8>& Tail, FString& Error)
{
    FString Current;
    P4MapKit::ParticleCutout::FRequiredTail Decoded;
    if (!Signature(Module, Current, Error) || !Geometry(Tail, Decoded, Error) || !Shape(Module, Decoded, Error))
        return false;
    // Empty/no-texture geometry already has the stock empty array after ordinary load. The
    // original importer can use its bounded public Serialize path for this same qualified case.
    if (!Module->CutoutTexture)
        return P4ApplyOriginalParticleRequiredTail(Module, Tail, Error);
    const FString CacheKey = FSubUVDerivedData::GetDDCKeyString(Module->CutoutTexture->Source.GetId(),
        Module->SubImages_Horizontal, Module->SubImages_Vertical, int32(Module->BoundingMode), Module->AlphaThreshold,
        int32(Module->OpacitySourceMode));
    TArray<uint8> NativeArray;
    NativeArray.Append(Tail.GetData() + 12, Tail.Num() - 12);
    // Exactly the installed CacheDerivedData representation: raw FVector2D words, no array header.
    GetDerivedDataCacheRef().Put(*CacheKey, NativeArray, Module->GetPathName(), true);
    TArray<uint8> Published;
    if (!GetDerivedDataCacheRef().GetSynchronous(*CacheKey, Published, Module->GetPathName()) ||
        Published != NativeArray)
    {
        // The module's own PostLoad just built this key from the editor's texture pixels and
        // may still be writing it. The cache's asynchronous put stage skips a second put of a
        // key in flight, so the read returned the editor's build. Let pending writes land and
        // put the original bytes again. The comparison below stays exact.
        GetDerivedDataCacheRef().WaitForQuiescence(false);
        GetDerivedDataCacheRef().Put(*CacheKey, NativeArray, Module->GetPathName(), true);
        Published.Reset();
    }
    if (!GetDerivedDataCacheRef().GetSynchronous(*CacheKey, Published, Module->GetPathName()) ||
        Published != NativeArray)
    {
        Error = FString::Printf(
            TEXT("particle-custody-public-cache-publication-differs: %s key %s expected bytes %d actual %d"),
            *Module->GetPathName(), *CacheKey, NativeArray.Num(), Published.Num());
        return false;
    }
    TArray<TUniquePtr<FPropertySnapshot>> Before;
    for (TFieldIterator<FProperty> It(Module->GetClass()); It; ++It)
        if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
            Before.Add(MakeUnique<FPropertySnapshot>(*It, Module));
    UPackage* Package = Module->GetOutermost();
    const bool WasDirty = Package->IsDirty();
    ON_SCOPE_EXIT
    {
        Package->SetDirtyFlag(WasDirty);
    };
    // Public virtual lifecycle releases/flushes the previous GPU buffer, consumes the native
    // cache key, initializes its own CPU/GPU data, then performs the real superclass callback.
    // A null property avoids Material/default-cutout or MaxDrawCount edits.
    FPropertyChangedEvent Event(nullptr);
    static_cast<UObject*>(Module)->PostEditChangeProperty(Event);
    bool SameProperties = true;
    for (const auto& Snapshot : Before)
        if (!Snapshot->Property->Identical(
                Snapshot->Storage, Snapshot->Property->ContainerPtrToValuePtr<void>(Module), PPF_None))
            SameProperties = false;
    if (!SameProperties)
    {
        for (const auto& Snapshot : Before)
            Snapshot->Property->CopyCompleteValue(
                Snapshot->Property->ContainerPtrToValuePtr<void>(Module), Snapshot->Storage);
        return Fail(Error, TEXT("particle-custody-native-refresh-changed-serialized-properties"));
    }
    return P4OriginalParticleRequiredTailMatches(Module, Tail, Error);
}
bool RebuildCurrent(UParticleModuleRequired* Module, FString& Error)
{
    TArray<uint8> Tail;
    Tail.SetNumZeroed(12);
    Tail[4] = 1;
    if (Module->CutoutTexture)
    {
        auto& Source = Module->CutoutTexture->Source;
        TArray64<uint8> Mip;
        if (Source.GetFormat() != TSF_BGRA8 || Source.GetSizeX() < Module->SubImages_Horizontal ||
            Source.GetSizeY() < Module->SubImages_Vertical ||
            int64(Source.GetSizeX()) * Source.GetSizeY() > MAX_int32 / 4 || !Source.GetMipData(Mip, 0) ||
            Mip.Num() != int64(Source.GetSizeX()) * Source.GetSizeY() * 4)
            return Fail(Error, TEXT("particle-custody-creator-source-pixels-unreadable"));
        FSubUVDerivedData Current;
        Current.Build(Module->CutoutTexture, Module->SubImages_Horizontal, Module->SubImages_Vertical,
            Module->BoundingMode, Module->AlphaThreshold, Module->OpacitySourceMode);
        const uint32 Count = Current.BoundingGeometry.Num();
        for (int32 I = 0; I < 4; ++I)
            Tail[8 + I] = uint8(Count >> (I * 8));
        Tail.Append(
            reinterpret_cast<const uint8*>(Current.BoundingGeometry.GetData()), Current.BoundingGeometry.Num() * 8);
        if (!P4ParticleRequiredTailRegeneratesFromTextureSource(Module, Tail, Error))
            return false;
    }
    // Public cache keys round AlphaThreshold to six decimal places. Rebuild on a changed exact
    // signature even if its rounded public key aliases the old original capture's key.
    return Refresh(Module, Tail, Error);
}
bool Parse(UParticleSystem* System, TSharedPtr<FJsonObject>& Document, FString& Text, FString& Error)
{
    if (!IsInGameThread() || !System || System->GetClass() != P4RecoveredParticleSystemClass())
        return Fail(Error, TEXT("particle-custody-native-asset-family-unqualified"));
    Text = System->GetOutermost()->GetMetaData()->GetValue(System, Key);
    if (Text.IsEmpty())
        return true;
    if (Text.Len() > MaximumText || !P4ParseJson(Text, Document))
        return Fail(Error, TEXT("particle-custody-metadata-unreadable"));
    double Schema = 0;
    FString Package, Digest;
    const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
    if (!Document->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 ||
        !Document->TryGetStringField(TEXT("package"), Package) || Package != System->GetOutermost()->GetName() ||
        !Document->TryGetStringField(TEXT("source_digest"), Digest) || !Hex(Digest, 64) ||
        !Document->TryGetArrayField(TEXT("modules"), Modules) || Modules->Num() < 1 || Modules->Num() > MaximumModules)
        return Fail(Error, TEXT("particle-custody-metadata-identity-unqualified"));
    return true;
}
bool Apply(UParticleSystem* System, const TSharedPtr<FJsonObject>& Document, int32& Restored, FString& Error)
{
    Restored = 0;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Document->TryGetArrayField(TEXT("modules"), Rows))
        return Fail(Error, TEXT("particle-custody-module-array-missing"));
    // Tables first: the cutout refresh below rebuilds the emitters, which reads them.
    if (!P4OriginalParticleHeld::ApplyCurves(System, Document, Error))
        return false;
    TSet<FName> Seen;
    TMap<FString, TArray<uint8>> ByCacheKey;
    struct FEligible
    {
        UParticleModuleRequired* Module;
        TArray<uint8> Tail;
    };
    TArray<FEligible> Eligible;
    TArray<UParticleModuleRequired*> Changed;
    for (const auto& Value : *Rows)
    {
        if (!Value.IsValid() || Value->Type != EJson::Object)
            return Fail(Error, TEXT("particle-custody-module-row-unreadable"));
        const auto Row = Value->AsObject();
        FString Relative, ExpectedSignature, Encoded, CapturedCacheKey;
        if (!Row->TryGetStringField(TEXT("object"), Relative) || Relative.IsEmpty() || Relative.Len() > 4096 ||
            Relative.Contains(TEXT("/")) || Relative.Contains(TEXT(":")) ||
            !Row->TryGetStringField(TEXT("signature"), ExpectedSignature) || ExpectedSignature.Len() > 8192 ||
            !Row->TryGetStringField(TEXT("tail"), Encoded) || Encoded.Len() > 12 * 1024 * 1024 ||
            !Row->TryGetStringField(TEXT("cache_key"), CapturedCacheKey) || CapturedCacheKey.Len() > 1024 ||
            (!CapturedCacheKey.IsEmpty() && !CapturedCacheKey.StartsWith(TEXT("SUBUV_"))) ||
            Seen.Contains(FName(*Relative)))
            return Fail(Error, TEXT("particle-custody-module-binding-unqualified"));
        Seen.Add(FName(*Relative));
        auto* Module = Cast<UParticleModuleRequired>(P4FindOriginalParticleObject(System, Relative));
        if (!Module || Module->GetClass() != UParticleModuleRequired::StaticClass())
            return Fail(Error, TEXT("particle-custody-original-module-family-differs"));
        TArray<uint8> Tail;
        P4MapKit::ParticleCutout::FRequiredTail Decoded;
        if (!FBase64::Decode(Encoded, Tail) || !Geometry(Tail, Decoded, Error))
            return Fail(Error, TEXT("particle-custody-original-native-tail-unreadable"));
        FString Current;
        if (!Signature(Module, Current, Error))
            return false;
        if (!CapturedCacheKey.IsEmpty())
        {
            const TArray<uint8>* Earlier = ByCacheKey.Find(CapturedCacheKey);
            if (Earlier && *Earlier != Tail)
                return Fail(Error, TEXT("particle-custody-same-native-cache-input-has-different-original-geometry"));
            if (!Earlier)
                ByCacheKey.Add(CapturedCacheKey, Tail);
        }
        // Creator source/parameter edits invalidate original native data. Do not seed their new key.
        if (!P4ExactNativeString(Current, ExpectedSignature))
        {
            Changed.Add(Module);
            continue;
        }
        const FString CurrentCacheKey = Module->CutoutTexture
            ? FSubUVDerivedData::GetDDCKeyString(Module->CutoutTexture->Source.GetId(), Module->SubImages_Horizontal,
                  Module->SubImages_Vertical, int32(Module->BoundingMode), Module->AlphaThreshold,
                  int32(Module->OpacitySourceMode))
            : FString();
        if (!P4ExactNativeString(CurrentCacheKey, CapturedCacheKey))
            return Fail(Error, TEXT("particle-custody-public-native-cache-key-differs"));
        if (!Shape(Module, Decoded, Error))
            return false;
        FEligible One;
        One.Module = Module;
        One.Tail = MoveTemp(Tail);
        Eligible.Add(MoveTemp(One));
    }
    for (const auto& One : Eligible)
    {
        if (!Refresh(One.Module, One.Tail, Error))
            return false;
        ++Restored;
    }
    for (auto* Module : Changed)
        if (!RebuildCurrent(Module, Error))
            return false;
    // Last: every refresh above rebuilt the GPU emitters from the curves.
    if (!P4OriginalParticleHeld::ApplyGpu(System, Document, Error) ||
        !P4OriginalParticleHeld::ApplyDerived(System, Document, Error))
        return false;
    Error.Reset();
    return true;
}
} //namespace P4OriginalParticleCustody

bool P4CaptureOriginalParticleCutouts(UParticleSystem* System, const FP4KadanSourceBinding& Source, FString& Error)
{
    using namespace P4OriginalParticleCustody;
    if (!IsInGameThread() || !System || System->GetClass() != P4RecoveredParticleSystemClass() ||
        Source.Package != System->GetOutermost()->GetName() || !P4KadanSourceBindingCurrent(Source, Error))
        return Fail(Error, TEXT("particle-custody-original-source-binding-unqualified"));
    const std::vector<uint8> Header(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num());
    const std::vector<uint8> Body(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
    const auto Package = P4MapKit::ReadPackage(Header, Body);
    if (!Package.Error.Code.empty() || FString(UTF8_TO_TCHAR(Package.Package.c_str())) != Source.Package)
        return Fail(Error, TEXT("particle-custody-original-package-unreadable"));
    auto Document = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Rows;
    bool Main = false;
    for (std::size_t Slot = 0; Slot < Package.Exports.size(); ++Slot)
    {
        const auto& Export = Package.Exports[Slot];
        std::string Class, Relative;
        if (!Package.Reference(Export.Class, Class) || !Package.Reference(int32(Slot + 1), Relative))
            return Fail(Error, TEXT("particle-custody-original-object-identity-unreadable"));
        if (!Export.Outer && Class == "/Script/Engine.ParticleSystem")
        {
            if (Main || FString(UTF8_TO_TCHAR(Relative.c_str())) != System->GetName())
                return Fail(Error, TEXT("particle-custody-original-main-differs"));
            Main = true;
        }
        if (Class != "/Script/Engine.ParticleModuleRequired")
            continue;
        auto* Module = Cast<UParticleModuleRequired>(
            P4FindOriginalParticleObject(System, UTF8_TO_TCHAR(Relative.c_str())));
        if (!Module || !SourceHierarchy(Package, int32(Slot + 1), Module, System))
            return Fail(Error, TEXT("particle-custody-original-module-not-held"));
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Tags.Error.Code.empty())
            return Fail(Error, TEXT("particle-custody-original-tags-unreadable"));
        TArray<uint8> Tail;
        Tail.Append(Body.data() + Tags.End, int32(Export.Offset + Export.Size - Tags.End));
        P4MapKit::ParticleCutout::FRequiredTail Decoded;
        FString Current;
        if (!Signature(Module, Current, Error) || !Geometry(Tail, Decoded, Error) || !Shape(Module, Decoded, Error))
            return false;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("object"), UTF8_TO_TCHAR(Relative.c_str()));
        Row->SetStringField(TEXT("signature"), Current);
        Row->SetStringField(TEXT("tail"), FBase64::Encode(Tail));
        const FString CacheKey = Module->CutoutTexture
            ? FSubUVDerivedData::GetDDCKeyString(Module->CutoutTexture->Source.GetId(), Module->SubImages_Horizontal,
                  Module->SubImages_Vertical, int32(Module->BoundingMode), Module->AlphaThreshold,
                  int32(Module->OpacitySourceMode))
            : FString();
        Row->SetStringField(TEXT("cache_key"), CacheKey);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        if (Rows.Num() > MaximumModules)
            return Fail(Error, TEXT("particle-custody-module-count-unbounded"));
    }
    if (!Main || !Rows.Num())
        return Fail(Error, TEXT("particle-custody-original-particle-family-missing"));
    Document->SetNumberField(TEXT("schema"), 1);
    Document->SetStringField(TEXT("package"), Source.Package);
    Document->SetStringField(TEXT("source_digest"), Source.SourceDigest);
    Document->SetArrayField(TEXT("modules"), Rows);
    int32 Restored = 0;
    if (!Apply(System, Document, Restored, Error) || Restored != Rows.Num())
        return false;
    if (!P4OriginalParticleHeld::Capture(System, Document, Error))
        return false;
    FString Text;
    if (!FJsonSerializer::Serialize(
            Document, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)) ||
        Text.Len() > MaximumText || !P4KadanSourceBindingCurrent(Source, Error))
        return Fail(Error, TEXT("particle-custody-original-source-changed"));
    System->GetOutermost()->GetMetaData()->SetValue(System, Key, *Text);
    Error.Reset();
    return true;
}

bool P4OriginalParticleCutoutCustodyNote(UParticleSystem* System, FP4OriginalValueNote& Note, FString& Error)
{
    using namespace P4OriginalParticleCustody;
    TSharedPtr<FJsonObject> Document;
    FString Text, Digest;
    if (!Parse(System, Document, Text, Error) || !Document.IsValid() || !P4HashUtf8(Text, Digest))
        return Fail(Error, TEXT("particle-custody-note-source-unavailable"));
    Note = FP4OriginalValueNote();
    Note.Kind = NoteKind;
    Note.Object = System->GetName();
    Note.Property = TEXT("SourceGeometryMetadataSHA256");
    Note.GameValue = Digest;
    Note.Action = TEXT("held-as-exact-native-source");
    Error.Reset();
    return true;
}

bool P4IsCopiedParticleCustody(UParticleSystem* System)
{
    FP4OriginalRecord Own;
    FString Ignored;
    return System && System->GetOutermost()->GetMetaData()->HasValue(System, TEXT("P4CopiedParticleCustodyFrom")) &&
        !P4ReadOriginalRecord(System, Own, Ignored);
}

bool P4RestoreOriginalParticleCutouts(UParticleSystem* System, int32& Restored, FString& Error)
{
    using namespace P4OriginalParticleCustody;
    Restored = 0;
    TSharedPtr<FJsonObject> Document;
    FString Text;
    if (!Parse(System, Document, Text, Error))
        return false;
    if (!Document.IsValid())
    {
        Error.Reset();
        return true;
    }
    // A copy of a game particle system carries its original's custody, taken when the copy was made from a
    // bound original (P4KadanMutationGuards.cpp). It applies as the original's does: an unchanged module gets
    // the game's data and a module the creator changed is built again from what it has now.
    if (P4IsCopiedParticleCustody(System))
        return Apply(System, Document, Restored, Error);
    if (P4HasCreatorSource(System))
        return Fail(Error, TEXT("particle-custody-creator-source-not-original"));
    FP4OriginalRecord Record;
    FString Digest;
    // A partial original still owns its separately bound native source. Reference-only
    // notes describe other unheld fields; they do not invalidate unchanged curve/GPU inputs.
    // Applying this custody neither changes those notes nor qualifies an edited input.
    if (!P4ReadOriginalRecord(System, Record, Error) || Record.Package != System->GetOutermost()->GetName() ||
        Record.Class != System->GetClass()->GetPathName() || Record.SourceDigestKind != TEXT("header-body-sha256-v1") ||
        !P4ExactNativeString(Record.SourceDigest, Document->GetStringField(TEXT("source_digest"))) ||
        !P4HashUtf8(Text, Digest))
        return Fail(Error, TEXT("particle-custody-complete-original-record-differs"));
    int32 Bindings = 0;
    for (const auto& Note : Record.Values)
        if (Note.Kind == NoteKind && Note.Object == System->GetName() &&
            Note.Property == TEXT("SourceGeometryMetadataSHA256") && P4ExactNativeString(Note.GameValue, Digest) &&
            Note.Action == TEXT("held-as-exact-native-source"))
            ++Bindings;
    if (Bindings != 1)
        return Fail(Error, TEXT("particle-custody-original-record-native-payload-binding-differs"));
    return Apply(System, Document, Restored, Error);
}

bool P4PrepareOriginalParticleState(UParticleSystem* System, FString& Error)
{
    using namespace P4OriginalParticleCustody;
    TSharedPtr<FJsonObject> Document;
    FString Text;
    if (!Parse(System, Document, Text, Error))
        return false;
    const auto Original = P4QueryOriginalAsset(System);
    if (!Original.Error.IsEmpty())
    {
        Error = Original.Error;
        return false;
    }
    if (Document.IsValid() && !Original.IsOriginal() && P4IsCopiedParticleCustody(System))
    {
        int32 Restored = 0;
        return P4RestoreOriginalParticleCutouts(System, Restored, Error);
    }
    // A fresh import captures/applies its native custody before stamping the original
    // record. Reading that new baseline must validate, but never reapply unbound data.
    if (!Document.IsValid() || !Original.IsOriginal() || P4HasCreatorSource(System))
    {
        Error.Reset();
        return true;
    }
    int32 Restored = 0;
    return P4RestoreOriginalParticleCutouts(System, Restored, Error);
}

namespace P4OriginalParticleHeld
{
// Turns references to one set of objects into references to another, in a struct's values.
struct FRemap : public FArchiveUObject
{
    const TMap<UObject*, UObject*>& Map;
    explicit FRemap(const TMap<UObject*, UObject*>& In) : Map(In)
    {
        ArIsObjectReferenceCollector = true;
        ArIsModifyingWeakAndStrongReferences = true;
    }
    virtual FArchive& operator<<(UObject*& Object) override
    {
        if (UObject* const* To = Map.Find(Object))
            Object = *To;
        return *this;
    }
};
} //namespace P4OriginalParticleHeld

bool P4RebindCopiedParticleCustody(UParticleSystem* Original, UParticleSystem* Copy, FString& Error)
{
    using namespace P4OriginalParticleCustody;
    TSharedPtr<FJsonObject> Document;
    FString Text;
    if (!Original || !Parse(Copy, Document, Text, Error))
        return false;
    if (!Document.IsValid())
    {
        Error.Reset();
        return true;
    }
    // The original's objects and the copy's at the same places: a GPU emitter's built data names its modules.
    TMap<UObject*, UObject*> Counterparts;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Original, Objects, true);
    Counterparts.Add(Original, Copy);
    for (UObject* Object : Objects)
    {
        const FString Relative = P4OriginalParticleHeld::Relative(Original, Object);
        if (UObject* Mine = P4FindOriginalParticleObject(
                Copy, Copy->GetName() + Relative.Mid(Original->GetName().Len())))
            Counterparts.Add(Object, Mine);
    }
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (Document->TryGetArrayField(TEXT("gpu"), Rows))
        for (const auto& Value : *Rows)
        {
            const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
            FString Object, Inputs, Game, Made;
            if (!Row.IsValid() || !Row->TryGetStringField(TEXT("object"), Object) ||
                !Row->TryGetStringField(TEXT("inputs"), Inputs) ||
                !Object.StartsWith(Copy->GetName() + TEXT("."), ESearchCase::CaseSensitive))
                return Fail(Error, TEXT("particle-custody-copy-gpu-row-unreadable"));
            const FString Theirs = Original->GetName() + Object.Mid(Copy->GetName().Len());
            auto* Source = Cast<UParticleModuleTypeDataGpu>(P4FindOriginalParticleObject(Original, Theirs));
            auto* Module = Cast<UParticleModuleTypeDataGpu>(P4FindOriginalParticleObject(Copy, Object));
            if (!Source || !Module)
                return Fail(Error, *(TEXT("particle-custody-copy-gpu-module-differs: ") + Object));
            // The game's build is the copy's only while the original's inputs are those it was taken with. Then the
            // copy's own inputs, which name its own objects, are what a later edit of the copy is told by.
            if (!P4OriginalParticleHeld::GpuInputs(Original, Source, Game) || !P4ExactNativeString(Game, Inputs))
            {
                Row->SetStringField(TEXT("inputs"), TEXT("original-inputs-differ"));
                continue;
            }
            if (!P4OriginalParticleHeld::GpuInputs(Copy, Module, Made))
                return Fail(Error, TEXT("particle-custody-copy-gpu-inputs-unavailable"));
            Row->SetStringField(TEXT("inputs"), Made);
            for (int32 Index = 0; Index < 2; ++Index)
            {
                const TCHAR* const Field = Index ? TEXT("emitter") : TEXT("resource");
                const FStructProperty* Property = P4OriginalParticleHeld::GpuField(Index);
                FString Encoded;
                TArray<uint8> Bytes, Rebound;
                if (!Property || !Row->TryGetStringField(Field, Encoded) || !FBase64::Decode(Encoded, Bytes))
                    return Fail(Error, TEXT("particle-custody-copy-gpu-build-unreadable"));
                FStructOnScope Scratch(Property->Struct);
                if (!P4OriginalParticleHeld::ReadStructBytes(Property->Struct, Scratch.GetStructMemory(), Bytes))
                    return Fail(Error, TEXT("particle-custody-copy-gpu-build-unreadable"));
                P4OriginalParticleHeld::FRemap Remap(Counterparts);
                Property->Struct->SerializeBin(Remap, Scratch.GetStructMemory());
                if (!P4OriginalParticleHeld::StructBytes(Property->Struct, Scratch.GetStructMemory(), Rebound))
                    return Fail(Error, TEXT("particle-custody-copy-gpu-build-unwritable"));
                Row->SetStringField(Field, FBase64::Encode(Rebound));
            }
        }
    FString Rebound;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Rebound);
    if (!FJsonSerializer::Serialize(Document.ToSharedRef(), Writer))
        return Fail(Error, TEXT("particle-custody-copy-unwritable"));
    Copy->GetOutermost()->GetMetaData()->SetValue(Copy, Key, *Rebound);
    Error.Reset();
    return true;
}

bool P4OriginalParticleCutoutCacheKeys(UParticleSystem* System, TArray<FString>& Keys, FString& Error)
{
    using namespace P4OriginalParticleCustody;
    TSharedPtr<FJsonObject> Document;
    FString Text, Digest;
    FP4OriginalRecord Record;
    if (!Parse(System, Document, Text, Error) || !Document.IsValid() || !P4HashUtf8(Text, Digest) ||
        !P4ReadOriginalRecord(System, Record, Error) || Record.ReferenceOnly ||
        !P4ExactNativeString(Record.SourceDigest, Document->GetStringField(TEXT("source_digest"))))
        return Fail(Error, TEXT("particle-custody-cache-keys-original-record-unavailable"));
    int32 Matched = 0;
    for (const auto& Note : Record.Values)
        if (Note.Kind == NoteKind && Note.Object == System->GetName() &&
            Note.Property == TEXT("SourceGeometryMetadataSHA256") && P4ExactNativeString(Note.GameValue, Digest) &&
            Note.Action == TEXT("held-as-exact-native-source"))
            ++Matched;
    if (Matched != 1)
        return Fail(Error, TEXT("particle-custody-cache-keys-payload-binding-differs"));
    TArray<FString> Candidate;
    for (const auto& Value : Document->GetArrayField(TEXT("modules")))
    {
        if (!Value.IsValid() || Value->Type != EJson::Object)
            return Fail(Error, TEXT("particle-custody-cache-key-row-unreadable"));
        FString CacheKey;
        if (!Value->AsObject()->TryGetStringField(TEXT("cache_key"), CacheKey) || CacheKey.Len() > 1024)
            return Fail(Error, TEXT("particle-custody-cache-key-unqualified"));
        if (!CacheKey.IsEmpty())
            Candidate.AddUnique(CacheKey);
    }
    Keys = MoveTemp(Candidate);
    Error.Reset();
    return true;
}

#include "P4OriginalParticlePreflight.h"
#include "P4LevelContentInternal.h"
#include "P4RecoveredParticleSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Color/ParticleModuleColorScaleOverLife.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionFloatUniformCurve.h"
#include "Distributions/DistributionVectorConstant.h"
#include "Distributions/DistributionVectorConstantCurve.h"
#include "Distributions/DistributionVectorUniform.h"
#include "Distributions/DistributionVectorUniformCurve.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace P4OriginalParticleCurves
{
// The reflected lookup table of FRawDistribution, read through its own properties.
struct FTable
{
    float TimeScale = 0.f, TimeBias = 0.f;
    TArray<float> Values;
    uint8 Op = 0, EntryCount = 0, EntryStride = 0, SubEntryStride = 0, LockFlag = 0;
};

bool ReadTable(const FStructProperty* Raw, const void* RawValue, FTable& Out)
{
    const auto* TableField = FindFProperty<FStructProperty>(Raw->Struct, TEXT("Table"));
    if (!TableField || TableField->ArrayDim != 1 || !TableField->Struct)
        return false;
    const UScriptStruct* Shape = TableField->Struct;
    const void* Table = TableField->ContainerPtrToValuePtr<void>(RawValue);
    const auto Float = [&](const TCHAR* Name, float& Value)
    {
        const auto* Field = FindFProperty<FFloatProperty>(Shape, Name);
        if (!Field)
            return false;
        Value = Field->GetPropertyValue_InContainer(Table);
        return true;
    };
    const auto Byte = [&](const TCHAR* Name, uint8& Value)
    {
        const auto* Field = FindFProperty<FByteProperty>(Shape, Name);
        if (!Field)
            return false;
        Value = Field->GetPropertyValue_InContainer(Table);
        return true;
    };
    const auto* ValuesField = FindFProperty<FArrayProperty>(Shape, TEXT("Values"));
    if (!ValuesField || !CastField<FFloatProperty>(ValuesField->Inner))
        return false;
    FScriptArrayHelper Values(ValuesField, ValuesField->ContainerPtrToValuePtr<void>(Table));
    Out.Values.SetNum(Values.Num());
    for (int32 Index = 0; Index < Values.Num(); ++Index)
        Out.Values[Index] = *reinterpret_cast<const float*>(Values.GetRawPtr(Index));
    return Float(TEXT("TimeScale"), Out.TimeScale) && Float(TEXT("TimeBias"), Out.TimeBias) &&
        Byte(TEXT("Op"), Out.Op) && Byte(TEXT("EntryCount"), Out.EntryCount) &&
        Byte(TEXT("EntryStride"), Out.EntryStride) && Byte(TEXT("SubEntryStride"), Out.SubEntryStride) &&
        Byte(TEXT("LockFlag"), Out.LockFlag);
}

// Why no stock bakeable distribution produces this table, or empty when one does.
FString Unproducible(const FTable& Table, bool Vector)
{
    const int32 PerEntry = Vector ? 3 : 1;
    if (Table.Op < RDO_None || Table.Op > RDO_Extreme)
        return TEXT("operation");
    const bool Ranged = Table.Op != RDO_None;
    if (Table.EntryCount < 1 || Table.EntryStride != (Ranged ? 2 : 1) * PerEntry ||
        Table.SubEntryStride != (Ranged ? PerEntry : 0))
        return TEXT("entry layout");
    if (Table.Values.Num() != int32(Table.EntryCount) * Table.EntryStride)
        return TEXT("value count");
    for (const float Value : Table.Values)
        if (!FMath::IsFinite(Value))
            return TEXT("value");
    if (!FMath::IsFinite(Table.TimeScale) || !FMath::IsFinite(Table.TimeBias) || Table.TimeScale < 0.f)
        return TEXT("time range");
    // Stock float distributions bake only as a single value or a random range.
    if (!Vector && Table.Op == RDO_Extreme)
        return TEXT("float extremes");
    // Only a vector uniform distribution bakes a lock flag, and it bakes a single entry.
    if (!Vector && Table.LockFlag)
        return TEXT("float lock");
    if (Vector && Table.LockFlag >= EDVLF_MAX)
        return TEXT("lock");
    if (Vector && Table.LockFlag && Table.EntryCount > 1 && Table.TimeScale > 0.f)
        return TEXT("locked curve");
    return FString();
}

template <typename T>
T* NewDistribution(UObject* Owner, const FString& Name)
{
    const FName Unique = MakeUniqueObjectName(Owner, T::StaticClass(), FName(*Name));
    return NewObject<T>(Owner, Unique, RF_Transactional | (Owner->GetFlags() & RF_Public));
}

template <typename T>
void Linear(T& Curve, int32 Point)
{
    Curve.Points[Point].InterpMode = CIM_Linear;
}

// The time of a table entry. A table of one entry keeps its time bias as its only key.
float EntryTime(const FTable& Table, int32 Entry)
{
    return Table.TimeScale > 0.f ? Table.TimeBias + float(Entry) / Table.TimeScale : Table.TimeBias;
}

// The editor curve whose bake is exactly this table. Time keys are the table's own sample
// times and every point interpolates linearly, as the table's lookup does. A single entry
// with a zero time bias is a constant or a uniform range, as the stock editor bakes those.
UObject* Make(UObject* Owner, const FString& Name, bool Vector, const FTable& Table)
{
    const int32 Count = Table.TimeScale > 0.f ? Table.EntryCount : 1;
    const bool Ranged = Table.Op != RDO_None;
    const bool Curve = Count > 1 || Table.TimeBias != 0.f;
    const auto At = [&](int32 Entry, int32 Range)
    { return Table.Values.GetData() + Entry * Table.EntryStride + Range * Table.SubEntryStride; };
    const auto Vec = [&](int32 Entry, int32 Range)
    {
        const float* V = At(Entry, Range);
        return FVector(V[0], V[1], V[2]);
    };
    if (!Vector)
    {
        if (!Curve && !Ranged)
        {
            auto* Made = NewDistribution<UDistributionFloatConstant>(Owner, Name);
            Made->Constant = At(0, 0)[0];
            return Made;
        }
        if (!Curve)
        {
            auto* Made = NewDistribution<UDistributionFloatUniform>(Owner, Name);
            Made->Min = At(0, 0)[0];
            Made->Max = At(0, 1)[0];
            return Made;
        }
        if (!Ranged)
        {
            auto* Made = NewDistribution<UDistributionFloatConstantCurve>(Owner, Name);
            for (int32 Entry = 0; Entry < Count; ++Entry)
                Linear(Made->ConstantCurve, Made->ConstantCurve.AddPoint(EntryTime(Table, Entry), At(Entry, 0)[0]));
            return Made;
        }
        auto* Made = NewDistribution<UDistributionFloatUniformCurve>(Owner, Name);
        for (int32 Entry = 0; Entry < Count; ++Entry)
            Linear(Made->ConstantCurve,
                Made->ConstantCurve.AddPoint(EntryTime(Table, Entry), FVector2D(At(Entry, 0)[0], At(Entry, 1)[0])));
        return Made;
    }
    if (Table.LockFlag || (!Curve && Ranged))
    {
        // The table's values already carry the lock, so applying it again changes nothing.
        auto* Made = NewDistribution<UDistributionVectorUniform>(Owner, Name);
        Made->Min = Vec(0, 0);
        Made->Max = Ranged ? Vec(0, 1) : Vec(0, 0);
        Made->LockedAxes = EDistributionVectorLockFlags(Table.LockFlag);
        for (auto& Mirror : Made->MirrorFlags)
            Mirror = EDVMF_Different;
        Made->bUseExtremes = Table.Op == RDO_Extreme;
        return Made;
    }
    if (!Curve)
    {
        auto* Made = NewDistribution<UDistributionVectorConstant>(Owner, Name);
        Made->Constant = Vec(0, 0);
        Made->LockedAxes = EDVLF_None;
        return Made;
    }
    if (!Ranged)
    {
        auto* Made = NewDistribution<UDistributionVectorConstantCurve>(Owner, Name);
        Made->LockedAxes = EDVLF_None;
        for (int32 Entry = 0; Entry < Count; ++Entry)
            Linear(Made->ConstantCurve, Made->ConstantCurve.AddPoint(EntryTime(Table, Entry), Vec(Entry, 0)));
        return Made;
    }
    auto* Made = NewDistribution<UDistributionVectorUniformCurve>(Owner, Name);
    for (auto& Lock : Made->LockedAxes)
        Lock = EDVLF_None;
    for (auto& Mirror : Made->MirrorFlags)
        Mirror = EDVMF_Different;
    Made->bUseExtremes = Table.Op == RDO_Extreme;
    for (int32 Entry = 0; Entry < Count; ++Entry)
        Linear(Made->ConstantCurve,
            Made->ConstantCurve.AddPoint(EntryTime(Table, Entry), FTwoVectors(Vec(Entry, 0), Vec(Entry, 1))));
    return Made;
}

// Keep the game's table: the curve is not dirty, so the editor does not rebake it now.
// The cook drops the curve object again, as the game's cook did, because its owner holds
// a baked table.
template <typename T>
void MarkBaked(T* Distribution)
{
    Distribution->bCanBeBaked = true;
    Distribution->bIsDirty = false;
    if (const auto* Baked = FindFProperty<FBoolProperty>(T::StaticClass(), TEXT("bBakedDataSuccesfully")))
        Baked->SetPropertyValue_InContainer(Distribution, true);
}

// Every entry of the table, sampled at its own time from the made curve, is the same float.
bool GivesBack(const UObject* Made, bool Vector, const FTable& Table)
{
    const int32 Count = Table.TimeScale > 0.f ? Table.EntryCount : 1;
    for (int32 Entry = 0; Entry < Count; ++Entry)
    {
        float Values[8] = {};
        const uint32 Written = Vector
            ? CastChecked<UDistributionVector>(Made)->InitializeRawEntry(EntryTime(Table, Entry), Values)
            : CastChecked<UDistributionFloat>(Made)->InitializeRawEntry(EntryTime(Table, Entry), Values);
        if (Written < Table.EntryStride)
            return false;
        for (int32 Value = 0; Value < Table.EntryStride; ++Value)
            if (FMemory::Memcmp(&Values[Value], &Table.Values[Entry * Table.EntryStride + Value], sizeof(float)) != 0)
                return false;
    }
    return true;
}

// The ranges a raw distribution keeps beside its table. The editor writes a vector's ranges
// from its curve every time the table is asked for, so a rebuilt curve must give them back.
struct FRanges
{
    float Min = 0.f, Max = 0.f;
    FVector MinVec = FVector::ZeroVector, MaxVec = FVector::ZeroVector;
};
bool ReadRanges(const FStructProperty* Raw, const void* Value, bool Vector, FRanges& Out)
{
    const auto* Min = FindFProperty<FFloatProperty>(Raw->Struct, TEXT("MinValue"));
    const auto* Max = FindFProperty<FFloatProperty>(Raw->Struct, TEXT("MaxValue"));
    if (!Min || !Max)
        return false;
    Out.Min = Min->GetPropertyValue_InContainer(Value);
    Out.Max = Max->GetPropertyValue_InContainer(Value);
    if (!Vector)
        return true;
    const auto* MinVec = FindFProperty<FStructProperty>(Raw->Struct, TEXT("MinValueVec"));
    const auto* MaxVec = FindFProperty<FStructProperty>(Raw->Struct, TEXT("MaxValueVec"));
    if (!MinVec || !MaxVec || MinVec->Struct != TBaseStructure<FVector>::Get() ||
        MaxVec->Struct != TBaseStructure<FVector>::Get())
        return false;
    Out.MinVec = *MinVec->ContainerPtrToValuePtr<FVector>(const_cast<void*>(Value));
    Out.MaxVec = *MaxVec->ContainerPtrToValuePtr<FVector>(const_cast<void*>(Value));
    return true;
}
bool Same(float A, float B)
{
    return FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
}
bool Same(const FVector& A, const FVector& B)
{
    return Same(A.X, B.X) && Same(A.Y, B.Y) && Same(A.Z, B.Z);
}
bool GivesRanges(const UObject* Made, bool Vector, const FRanges& Ranges)
{
    float Min = 0.f, Max = 0.f;
    if (!Vector)
    {
        CastChecked<UDistributionFloat>(Made)->GetOutRange(Min, Max);
        return Same(Min, Ranges.Min) && Same(Max, Ranges.Max);
    }
    const auto* Curve = CastChecked<UDistributionVector>(Made);
    FVector MinVec, MaxVec;
    Curve->GetOutRange(Min, Max);
    Curve->GetRange(MinVec, MaxVec);
    return Same(Min, Ranges.Min) && Same(Max, Ranges.Max) && Same(MinVec, Ranges.MinVec) && Same(MaxVec, Ranges.MaxVec);
}
void Discard(UObject* Curve)
{
    if (Curve)
    {
        Curve->ClearFlags(RF_Public | RF_Standalone);
        Curve->MarkPendingKill();
    }
}

// A single vector entry made with unlocked authored ranges. The table keeps the lock's result
// and the raw distribution keeps the authored ranges, so both must come back.
UObject* MakeAuthoredVector(
    UObject* Owner, const FString& Name, const FTable& Table, const FRanges& Ranges, uint8 Lock, bool Uniform)
{
    if (Uniform)
    {
        auto* Made = NewDistribution<UDistributionVectorUniform>(Owner, Name);
        Made->Min = Ranges.MinVec;
        Made->Max = Ranges.MaxVec;
        Made->LockedAxes = EDistributionVectorLockFlags(Lock);
        for (auto& Mirror : Made->MirrorFlags)
            Mirror = EDVMF_Different;
        Made->bUseExtremes = Table.Op == RDO_Extreme;
        return Made;
    }
    auto* Made = NewDistribution<UDistributionVectorConstant>(Owner, Name);
    Made->Constant = Ranges.MinVec;
    Made->LockedAxes = EDistributionVectorLockFlags(Lock);
    return Made;
}

FString PlaceName(const FP4ParticleRawDistributionPlace& Place, const FStructProperty* Raw)
{
    return Place.Member.IsEmpty()
        ? (Raw->ArrayDim > 1 ? FString::Printf(TEXT("%s_%d"), *Place.Field, Place.Index) : Place.Field)
        : FString::Printf(TEXT("%s_%d_%s"), *Place.Field, Place.Index, *Place.Member);
}

bool One(UObject* Owner, const FP4ParticleRawDistributionPlace& Place, const FStructProperty* Raw, void* RawValue,
    int32& Made, FString& Error, TArray<FP4ParticleRawDistributionPlace>* Places)
{
    const bool Vector = Raw->Struct == FRawDistributionVector::StaticStruct();
    const FString Where = Owner->GetPathName() + TEXT(".") + PlaceName(Place, Raw);
    const auto* Reference = FindFProperty<FObjectProperty>(Raw->Struct, TEXT("Distribution"));
    if (!Reference || Reference->ArrayDim != 1)
    {
        Error = TEXT("particle-native-distribution-field-unavailable: ") + Where;
        return false;
    }
    FTable Table;
    if (!ReadTable(Raw, RawValue, Table))
    {
        Error = TEXT("particle-native-distribution-table-unreadable: ") + Where;
        return false;
    }
    const bool Baked = Table.Values.Num() != 0 && Table.EntryCount != 0;
    if (UObject* Current = Reference->GetObjectPropertyValue_InContainer(RawValue))
    {
        // An authored curve the game kept stays exactly as it is. The game's cook drops every
        // bakeable curve whose owner holds a baked table, so a bakeable curve beside a baked
        // table is the editor constructor's default, which the source omitted. It is replaced.
        const auto* FloatCurve = Cast<UDistributionFloat>(Current);
        const auto* VectorCurve = Cast<UDistributionVector>(Current);
        const bool Bakeable = (FloatCurve && FloatCurve->CanBeBaked() && !FloatCurve->HasBakedSuccesfully()) ||
            (VectorCurve && VectorCurve->CanBeBaked() && !VectorCurve->HasBakedSuccesfully());
        if (!Bakeable || !Baked)
            return true;
    }
    // No table means the source has neither curve nor bake here, which the editor holds as null.
    if (!Baked)
        return true;
    const FString Why = Unproducible(Table, Vector);
    if (!Why.IsEmpty())
    {
        Error = TEXT("particle-native-distribution-table-unproducible: ") + Where + TEXT(" (") + Why + TEXT(")");
        return false;
    }
    FRanges Ranges;
    if (!ReadRanges(Raw, RawValue, Vector, Ranges))
    {
        Error = TEXT("particle-native-distribution-ranges-unreadable: ") + Where;
        return false;
    }
    const FString Name = TEXT("Distribution") + PlaceName(Place, Raw);
    UObject* Curve = Make(Owner, Name, Vector, Table);
    if (!Curve || !GivesBack(Curve, Vector, Table))
    {
        Discard(Curve);
        Error = TEXT("particle-native-distribution-table-not-reproduced: ") + Where;
        return false;
    }
    // The editor rewrites a vector's ranges from its curve whenever the table is read. A single
    // entry can keep the authored unlocked ranges under one of the stock axis locks.
    const int32 Count = Table.TimeScale > 0.f ? Table.EntryCount : 1;
    if (Vector && !GivesRanges(Curve, Vector, Ranges) && Count == 1)
    {
        UObject* Fitted = nullptr;
        for (const bool Uniform : {true, false})
        {
            if (!Uniform && Table.Op != RDO_None)
                continue;
            for (uint8 Lock = 0; Lock < EDVLF_MAX && !Fitted; ++Lock)
            {
                UObject* Candidate = MakeAuthoredVector(Owner, Name, Table, Ranges, Lock, Uniform);
                if (GivesBack(Candidate, Vector, Table) && GivesRanges(Candidate, Vector, Ranges))
                    Fitted = Candidate;
                else
                    Discard(Candidate);
            }
            if (Fitted)
                break;
        }
        if (Fitted)
        {
            Discard(Curve);
            Curve = Fitted;
        }
    }
    // A curve of many entries can peak between the table's samples, and the keys made at the sample times then
    // give lower ranges than the authored curve. Two more keys between the first two samples, which no sample
    // reads, give the authored ranges.
    if (Vector && Count > 1 && !GivesRanges(Curve, Vector, Ranges))
    {
        UObject* Fitted = Make(Owner, Name, Vector, Table);
        const float First = EntryTime(Table, 0), Step = EntryTime(Table, 1) - First;
        if (auto* Uniform = Cast<UDistributionVectorUniformCurve>(Fitted))
        {
            Linear(Uniform->ConstantCurve, Uniform->ConstantCurve.AddPoint(First + Step / 3.f,
                FTwoVectors(FVector(Ranges.Max), Ranges.MaxVec)));
            Linear(Uniform->ConstantCurve, Uniform->ConstantCurve.AddPoint(First + 2.f * Step / 3.f,
                FTwoVectors(FVector(Ranges.Min), Ranges.MinVec)));
        }
        else if (auto* Constant = Cast<UDistributionVectorConstantCurve>(Fitted))
        {
            Linear(Constant->ConstantCurve, Constant->ConstantCurve.AddPoint(First + Step / 3.f, Ranges.MaxVec));
            Linear(Constant->ConstantCurve, Constant->ConstantCurve.AddPoint(First + 2.f * Step / 3.f, Ranges.MinVec));
        }
        if (Fitted && GivesBack(Fitted, Vector, Table) && GivesRanges(Fitted, Vector, Ranges))
        {
            Discard(Curve);
            Curve = Fitted;
        }
        else
            Discard(Fitted);
    }
    // A curve whose ranges still differ is kept. The import's comparison tells them as values not held
    // when the table is the game's (P4AddParticleRangesNotHeld).
    if (Vector)
        MarkBaked(CastChecked<UDistributionVector>(Curve));
    else
        MarkBaked(CastChecked<UDistributionFloat>(Curve));
    Reference->SetObjectPropertyValue_InContainer(RawValue, Curve);
    if (Places)
        Places->Add(Place);
    ++Made;
    return true;
}
} //namespace P4OriginalParticleCurves

bool P4ForEachParticleRawDistribution(
    UObject* Owner, TFunctionRef<bool(const FP4ParticleRawDistributionPlace&, const FStructProperty*, void*)> Visit)
{
    if (!Owner)
        return false;
    const auto RawShape = [](const FStructProperty* Struct)
    {
        return Struct &&
            (Struct->Struct == FRawDistributionFloat::StaticStruct() ||
                Struct->Struct == FRawDistributionVector::StaticStruct());
    };
    for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
    {
        if (const auto* Struct = CastField<FStructProperty>(*It))
        {
            if (!RawShape(Struct))
                continue;
            for (int32 Index = 0; Index < Struct->ArrayDim; ++Index)
            {
                FP4ParticleRawDistributionPlace Place;
                Place.Field = Struct->GetName();
                Place.Index = Index;
                if (!Visit(Place, Struct, Struct->ContainerPtrToValuePtr<void>(Owner, Index)))
                    return false;
            }
        }
        else if (const auto* Array = CastField<FArrayProperty>(*It))
        {
            const auto* Inner = CastField<FStructProperty>(Array->Inner);
            if (!Inner || !Inner->Struct)
                continue;
            FScriptArrayHelper Items(Array, Array->ContainerPtrToValuePtr<void>(Owner));
            for (int32 Item = 0; Item < Items.Num(); ++Item)
                for (TFieldIterator<FProperty> Member(Inner->Struct); Member; ++Member)
                {
                    const auto* Raw = CastField<FStructProperty>(*Member);
                    if (!RawShape(Raw) || Raw->ArrayDim != 1)
                        continue;
                    FP4ParticleRawDistributionPlace Place;
                    Place.Field = Array->GetName();
                    Place.Index = Item;
                    Place.Member = Raw->GetName();
                    if (!Visit(Place, Raw, Raw->ContainerPtrToValuePtr<void>(Items.GetRawPtr(Item))))
                        return false;
                }
        }
    }
    return true;
}

bool P4FindParticleRawDistribution(
    UObject* Owner, const FP4ParticleRawDistributionPlace& Place, const FStructProperty*& Raw, void*& Value)
{
    Raw = nullptr;
    Value = nullptr;
    P4ForEachParticleRawDistribution(Owner,
        [&](const FP4ParticleRawDistributionPlace& At, const FStructProperty* Field, void* Data)
        {
            if (At.Index != Place.Index || !At.Field.Equals(Place.Field, ESearchCase::CaseSensitive) ||
                !At.Member.Equals(Place.Member, ESearchCase::CaseSensitive))
                return true;
            Raw = Field;
            Value = Data;
            return false;
        });
    return Raw && Value;
}

bool P4ReconstructOriginalParticleDistributions(
    UObject* Owner, int32& Made, FString& Error, TArray<FP4ParticleRawDistributionPlace>* Places)
{
    using namespace P4OriginalParticleCurves;
    Made = 0;
    Error.Reset();
    if (!IsInGameThread() || !Owner)
    {
        Error = TEXT("particle-native-distribution-owner-missing");
        return false;
    }
    // The cook drops a curve only in the places the engine's bake check accepts, so no
    // other place holds a stripped curve.
    const bool Complete = P4ForEachParticleRawDistribution(Owner,
        [&](const FP4ParticleRawDistributionPlace& Place, const FStructProperty* Raw, void* Value)
        { return One(Owner, Place, Raw, Value, Made, Error, Places); });
    if (!Complete && Error.IsEmpty())
        Error = TEXT("particle-native-distribution-owner-unreadable");
    return Complete;
}

bool P4PreflightOriginalParticleEditorBuild(UParticleSystem* System, FString& Error)
{
    if (!System)
    {
        Error = TEXT("particle-native-preflight-system-missing");
        return false;
    }
    TArray<UObject*> Inside;
    GetObjectsWithOuter(System, Inside, true);
    Inside.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
    TArray<TPair<UObject*, FP4ParticleRawDistributionPlace>> Made;
    for (UObject* Object : Inside)
    {
        if (!Object || !Object->IsA<UParticleModule>())
            continue;
        int32 Count = 0;
        TArray<FP4ParticleRawDistributionPlace> Places;
        if (!P4ReconstructOriginalParticleDistributions(Object, Count, Error, &Places))
            return false;
        for (const auto& Place : Places)
            Made.Emplace(Object, Place);
    }
    // Stock editor GPU compilation dereferences these curves unconditionally. After the
    // reconstruction a null one here means the source has no table for it either.
    for (const UParticleEmitter* Emitter : System->Emitters)
    {
        if (!Emitter)
            continue;
        for (const UParticleLODLevel* Lod : Emitter->LODLevels)
        {
            if (!Lod || !Lod->TypeDataModule ||
                Lod->TypeDataModule->GetClass() != UParticleModuleTypeDataGpu::StaticClass())
                continue;
            for (const UParticleModule* Module : Lod->Modules)
            {
                if (!Module)
                    continue;
                FString Field;
                if (Module->GetClass() == UParticleModuleColorOverLife::StaticClass())
                {
                    const auto* Color = CastChecked<UParticleModuleColorOverLife>(Module);
                    if (!Color->ColorOverLife.Distribution)
                        Field = TEXT("ColorOverLife.Distribution");
                    else if (!Color->AlphaOverLife.Distribution)
                        Field = TEXT("AlphaOverLife.Distribution");
                }
                else if (Module->GetClass() == UParticleModuleColorScaleOverLife::StaticClass())
                {
                    const auto* Color = CastChecked<UParticleModuleColorScaleOverLife>(Module);
                    if (!Color->ColorScaleOverLife.Distribution)
                        Field = TEXT("ColorScaleOverLife.Distribution");
                    else if (!Color->AlphaScaleOverLife.Distribution)
                        Field = TEXT("AlphaScaleOverLife.Distribution");
                }
                if (!Field.IsEmpty())
                {
                    Error = TEXT("particle-native-gpu-editor-distribution-unavailable: ") + Module->GetPathName() +
                        TEXT(".") + Field +
                        TEXT(
                            "; the source has neither the editor curve nor a baked table for this engine's GPU rebuild");
                    return false;
                }
            }
        }
    }
    if (!P4HoldOriginalParticleSource(System, Made, Error))
        return false;
    Error.Reset();
    return true;
}

namespace
{
void AddRanges(const FString& Path, const FString& Steps, const P4MapKit::FDecodedValue& Game,
    const P4MapKit::FDecodedValue& Mine, TArray<FString>& Out)
{
    using P4MapKit::FDecodedValue;
    const FDecodedValue* GameTable = Game.Field("Table");
    const FDecodedValue* MineTable = Mine.Field("Table");
    if (GameTable && MineTable)
    {
        if (!P4LevelContent::P4SameDecodedValue(*GameTable, *MineTable))
            return;
        for (const char* Member : {"MinValue", "MaxValue", "MinValueVec", "MaxValueVec"})
        {
            const FDecodedValue* Said = Game.Field(Member);
            const FDecodedValue* Made = Mine.Field(Member);
            if (Said && Made && P4LevelContent::P4SameDecodedValue(*Said, *Made))
                continue;
            if (Said || Made)
                Out.Add(FString::Printf(TEXT("%s %s.%s = %s"), *Path, *Steps, UTF8_TO_TCHAR(Member),
                    Said ? UTF8_TO_TCHAR(Said->Text.c_str()) : TEXT("0")));
        }
        return;
    }
    for (const auto& Field : Game.Fields)
        if (const FDecodedValue* Other = Mine.Field(Field.first))
        {
            const FString Name = UTF8_TO_TCHAR(Field.first.c_str());
            AddRanges(Path, Steps.IsEmpty() ? Name : Steps + TEXT(".") + Name, Field.second, *Other, Out);
        }
    for (int32 At = 0; At < int32(Game.Items.size()) && At < int32(Mine.Items.size()); ++At)
        AddRanges(Path, FString::Printf(TEXT("%s[%d]"), *Steps, At), Game.Items[At], Mine.Items[At], Out);
}
} // namespace

// The ranges beside a raw distribution's table are the editor's own work from its curve, written again at every
// cook, and some of the game's are older than their table. They do not choose what plays, which the table does, so
// when the table is the game's they are told and left out of the comparison. A table that differs stays a difference.
void P4AddParticleRangesNotHeld(const P4LevelContent::FFile& Game, const P4LevelContent::FFile& Mine, TArray<FString>& NotHeld)
{
    for (const P4LevelContent::FNode& Node : Game.Nodes)
    {
        const P4LevelContent::FNode* Made = Mine.Nodes.FindByPredicate(
            [&](const P4LevelContent::FNode& One) { return One.Path.Equals(Node.Path, ESearchCase::IgnoreCase); });
        if (!Made)
            continue;
        TArray<FString> Found;
        AddRanges(Node.Path, FString(), Node.Values, Made->Values, Found);
        for (const FString& Line : Found)
            NotHeld.AddUnique(Line);
    }
}

#include "P4StrictRequiredClasses.h"
#include "P4GamePaths.h"
#include "P4KadanImportSources.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
// Whether the Blueprint's import kept every value the editor's classes cannot hold as the game's, in its
// original record (P4OriginalRecord "values", the notes RetainOriginalValue writes), as a level or an asset
// keeps its own in P4NativeLevelValuesNotHeld and P4NativeObjectValuesNotHeld. Such a value is not a difference.
bool StrictClassValuesRetained(const UClass* Class, const TArray<FP4OriginalClassValueNotHeld>& Values, int32 Count)
{
    const UObject* Asset = Class ? Class->ClassGeneratedBy : nullptr;
    UMetaData* Meta = Asset ? Asset->GetOutermost()->GetMetaData() : nullptr;
    if (!Meta || !Count || Values.Num() != Count)
        return false;
    TSharedPtr<FJsonObject> Record;
    const TArray<TSharedPtr<FJsonValue>>* Notes = nullptr;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Meta->GetValue(Asset, TEXT("P4OriginalRecord"))),
            Record) ||
        !Record.IsValid() || !Record->TryGetArrayField(TEXT("values"), Notes))
        return false;
    const auto Path = [](FString Text)
    {
        Text.ReplaceInline(TEXT(":"), TEXT("."));
        return Text;
    };
    for (const auto& Value : Values)
    {
        // A note names the property the game's file sets; a value not held can be a member of it.
        FString Root = Value.Property;
        Root.Split(TEXT("."), &Root, nullptr);
        const bool Kept = Notes->ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Item)
        {
            const TSharedPtr<FJsonObject> Note = Item->AsObject();
            return Note.IsValid() && Note->GetStringField(TEXT("kind")) == TEXT("unsupported-value") &&
                Path(Note->GetStringField(TEXT("object"))).Equals(Path(Value.Object), ESearchCase::IgnoreCase) &&
                Note->GetStringField(TEXT("property")).Equals(Root, ESearchCase::IgnoreCase);
        });
        if (!Kept)
            return false;
    }
    return true;
}
bool StrictClassPackageCanonical(const FString& Package)
{
    return Package.Len() <= 4096 && P4IsValidContentPackage(Package);
}
bool StrictClassDigestValid(const FString& Digest)
{
    if (Digest.Len() != 64)
        return false;
    for (TCHAR Character : Digest)
        if (!FChar::IsHexDigit(Character))
            return false;
    return true;
}
void StrictClassProblem(TArray<FString>& Problems, const FString& Problem)
{
    if (Problems.Num() < 256)
        Problems.Add(Problem.Left(2048));
}
struct FStrictClassSnapshot
{
    FString HeaderPath, BodyPath;
    TWeakObjectPtr<UClass> Class;
    TWeakObjectPtr<UObject> DefaultObject;
};
bool StrictClassRead(const FString& Package,
    TFunctionRef<bool(const FString&, FP4KadanSourceBinding&, UClass*&, FString&)> ReadCurrent,
    FP4KadanSourceBinding& Source, UClass*& Class, FString& Reason)
{
    Class = nullptr;
    if (!ReadCurrent(Package, Source, Class, Reason))
    {
        if (Reason.IsEmpty())
            Reason = TEXT("Current source/class lookup failed");
        return false;
    }
    if (Source.Package != Package || !StrictClassDigestValid(Source.SourceDigest))
    {
        Reason = TEXT("Current class source identity or SHA256 is invalid");
        return false;
    }
    if (!P4KadanSourceBindingCurrent(Source, Reason))
        return false;
    if (!IsValid(Class) || !IsValid(Class->GetDefaultObject(false)))
    {
        Reason = TEXT("Persistent native class/default object is unavailable");
        return false;
    }
    return true;
}
bool StrictClassSameSnapshot(const FP4KadanSourceBinding& Source, const UClass* Class,
    const FStrictClassSnapshot& Snapshot, const FP4StrictRequiredClassProof& Proof, FString& Reason)
{
    if (Source.SourceDigest != Proof.SourceDigest || !FPaths::IsSamePath(Source.HeaderPath, Snapshot.HeaderPath) ||
        !FPaths::IsSamePath(Source.BodyPath, Snapshot.BodyPath))
    {
        Reason = TEXT("Required class source binding changed after comparison");
        return false;
    }
    if (!Snapshot.Class.IsValid() || !Snapshot.DefaultObject.IsValid() || Class != Snapshot.Class.Get() ||
        Class->GetDefaultObject(false) != Snapshot.DefaultObject.Get())
    {
        Reason = TEXT("Required native class/default object identity changed after comparison");
        return false;
    }
    return true;
}
} //namespace

FP4StrictRequiredClassPostflight P4EvaluateStrictRequiredClasses(const TArray<FString>& SelectedWorlds,
    const TArray<FP4StrictWorldClassInventory>& Inventories,
    TFunctionRef<bool(const FString&, FP4KadanSourceBinding&, UClass*&, FString&)> ReadCurrent,
    TFunctionRef<FP4OriginalClassComparisonResult(const FP4KadanSourceBinding&)> CompareNative,
    EP4OriginalClassDefaultObservation Observation)
{
    check(IsInGameThread());
    FP4StrictRequiredClassPostflight Result;
    TMap<FString, const FP4StrictWorldClassInventory*> ByWorld;
    TSet<FString> AmbiguousWorlds;
    for (const auto& Inventory : Inventories)
    {
        if (ByWorld.Contains(Inventory.WorldPackage))
            AmbiguousWorlds.Add(Inventory.WorldPackage);
        else
            ByWorld.Add(Inventory.WorldPackage, &Inventory);
    }
    TSet<FString> Selected;
    TMap<FString, int32> ClassIndices;
    TArray<FStrictClassSnapshot> Snapshots;
    bool Complete = true;
    for (const FString& World : SelectedWorlds)
    {
        if (Selected.Contains(World))
            continue;
        Selected.Add(World);
        FP4StrictWorldClassProof WorldProof;
        WorldProof.WorldPackage = World;
        const auto* Found = ByWorld.Find(World);
        const FP4StrictWorldClassInventory* Inventory = Found ? *Found : nullptr;
        if (Inventory)
        {
            WorldProof.Resumed = Inventory->Resumed;
            WorldProof.WorldSourceDigest = Inventory->WorldSourceDigest;
        }
        if (!StrictClassPackageCanonical(World) || !Inventory || AmbiguousWorlds.Contains(World) || !Inventory->Known ||
            !Inventory->Error.IsEmpty() || !StrictClassDigestValid(Inventory->WorldSourceDigest))
        {
            StrictClassProblem(WorldProof.Problems,
                TEXT(
                    "Required-class inventory is missing, ambiguous or not source-bound, including for resumed worlds"));
            if (Inventory && !Inventory->Error.IsEmpty())
                StrictClassProblem(WorldProof.Problems, Inventory->Error);
            Complete = false;
            Result.Worlds.Add(MoveTemp(WorldProof));
            continue;
        }
        TSet<FString> WorldClasses;
        for (const FString& Package : Inventory->RequiredClassPackages)
        {
            if (WorldClasses.Contains(Package))
                continue;
            WorldClasses.Add(Package);
            if (!StrictClassPackageCanonical(Package))
            {
                StrictClassProblem(WorldProof.Problems, TEXT("Noncanonical required class package: ") + Package);
                continue;
            }
            if (ClassIndices.Contains(Package))
                continue;
            const int32 Index = Result.Classes.Num();
            ClassIndices.Add(Package, Index);
            FP4StrictRequiredClassProof Proof;
            Proof.Package = Package;
            FStrictClassSnapshot Snapshot;
            FP4KadanSourceBinding Source;
            UClass* Class = nullptr;
            if (Observation == EP4OriginalClassDefaultObservation::TemporarilyBorrowedForCook)
                Proof.Reason = TEXT("Temporary source defaults cannot qualify persistent native class defaults");
            else if (StrictClassRead(Package, ReadCurrent, Source, Class, Proof.Reason))
            {
                Proof.SourceDigest = Source.SourceDigest;
                Snapshot.HeaderPath = Source.HeaderPath;
                Snapshot.BodyPath = Source.BodyPath;
                Snapshot.Class = Class;
                Snapshot.DefaultObject = Class->GetDefaultObject(false);
                const auto Comparison = CompareNative(Source);
                Proof.Compared = Comparison.Compared;
                Proof.ValuesNotHeld = Comparison.ValuesNotHeld;
                Proof.Differences = Comparison.Differences;
                Proof.DefaultFactsDigest = Comparison.DefaultFactsDigest;
                // Values the import kept as the game's are reported (ValuesNotHeld above) but are not a difference.
                auto Judged = Comparison;
                if (StrictClassValuesRetained(Class, Comparison.ValuesNotHeldDetails, Comparison.ValuesNotHeld))
                    Judged.ValuesNotHeld = 0;
                Proof.Qualified = Comparison.Package == Package &&
                    P4StrictOriginalClassDefaultsQualified(Judged, Observation, Proof.Reason);
                if (Comparison.Package != Package)
                    Proof.Reason = TEXT("Native comparison belongs to another class source package");
                else if (!Proof.Qualified && Comparison.ValuesNotHeldDetails.Num())
                {
                    // Name the values the editor's classes cannot hold, not only their count.
                    TArray<FString> Named;
                    for (const auto& Value : Comparison.ValuesNotHeldDetails)
                        Named.Add(FString::Printf(TEXT("%s.%s (%s: %s)"), *Value.Object, *Value.Property, *Value.Kind,
                            *Value.Reason));
                    Proof.Reason += TEXT(" - ") + FString::Join(Named, TEXT("; "));
                }
                else if (!Proof.Qualified && (Comparison.OnlyGameSays.Num() || Comparison.OnlyProjectSays.Num()))
                {
                    // Name the differing keys too: the game's alone, then the project's alone.
                    Proof.Reason += TEXT(" - game only: ") + FString::Join(Comparison.OnlyGameSays, TEXT("; ")) +
                        TEXT(" | project only: ") + FString::Join(Comparison.OnlyProjectSays, TEXT("; "));
                }
                FP4KadanSourceBinding After;
                UClass* AfterClass = nullptr;
                FString CurrentReason;
                if (!StrictClassRead(Package, ReadCurrent, After, AfterClass, CurrentReason) ||
                    !StrictClassSameSnapshot(After, AfterClass, Snapshot, Proof, CurrentReason) ||
                    !P4KadanSourceBindingCurrent(Source, CurrentReason))
                {
                    Proof.Qualified = false;
                    Proof.Reason = CurrentReason;
                }
            }
            Proof.Reason = Proof.Reason.Left(2048);
            Result.Classes.Add(MoveTemp(Proof));
            Snapshots.Add(MoveTemp(Snapshot));
        }
        WorldProof.RequiredClasses = WorldClasses.Num();
        Result.Worlds.Add(MoveTemp(WorldProof));
    }
    // Consume only after every unique comparison. Later cooks cannot hide a class
    // reinstance or changed binding behind an earlier cached successful observation.
    for (auto& World : Result.Worlds)
    {
        const auto* Found = ByWorld.Find(World.WorldPackage);
        if (!Found || World.Problems.Num())
        {
            Complete = false;
            continue;
        }
        TSet<FString> Consumed;
        for (const FString& Package : (*Found)->RequiredClassPackages)
        {
            if (Consumed.Contains(Package))
                continue;
            Consumed.Add(Package);
            const int32* Index = ClassIndices.Find(Package);
            if (!Index)
            {
                Complete = false;
                continue;
            }
            auto& Proof = Result.Classes[*Index];
            if (Proof.Qualified)
            {
                FP4KadanSourceBinding Current;
                UClass* Class = nullptr;
                FString Reason;
                const bool CurrentMatches = StrictClassRead(Package, ReadCurrent, Current, Class, Reason) &&
                    StrictClassSameSnapshot(Current, Class, Snapshots[*Index], Proof, Reason);
                if (!CurrentMatches)
                {
                    Proof.Qualified = false;
                    Proof.Reason = Reason.Left(2048);
                }
            }
            if (!Proof.Qualified)
                StrictClassProblem(World.Problems, Package + TEXT(": ") + Proof.Reason);
        }
        World.Qualified = World.Problems.Num() == 0;
        if (!World.Qualified)
            Complete = false;
    }
    // A shared class can fail a later world's consumption; propagate that failure to
    // earlier world receipts too, so no receipt retains a now-invalid cached success.
    for (auto& World : Result.Worlds)
    {
        const auto* Found = ByWorld.Find(World.WorldPackage);
        if (!Found)
            continue;
        for (const FString& Package : (*Found)->RequiredClassPackages)
        {
            const int32* Index = ClassIndices.Find(Package);
            if (Index && !Result.Classes[*Index].Qualified && World.Qualified)
            {
                StrictClassProblem(World.Problems, Package + TEXT(": ") + Result.Classes[*Index].Reason);
                World.Qualified = false;
                Complete = false;
            }
        }
    }
    Result.Qualified = Complete;
    if (!Complete)
        StrictClassProblem(
            Result.Problems, TEXT("Selected worlds do not have complete current native class-default proof"));
    return Result;
}

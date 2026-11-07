#include "P4OriginalActorAttachments.h"
#include "P4OriginalRecord.h"
#include "P4LevelContent.h"
#include "P4CreatorSource.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/LinkerLoad.h"
#include "UObject/ObjectVersion.h"
#include "Serialization/ArchiveProxy.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
// Use the loaded package's native export/name tables and its versioned tags. PostLoad may already have replayed
// construction before AssetLoaded; the saved property remains authoritative, including a saved creator override.
class FPropertyNames : public FArchiveProxy
{
    const TArray<FNameEntryId>& Names;

public:
    FPropertyNames(FArchive& File, const FLinkerLoad& Linker) : FArchiveProxy(File), Names(Linker.NameMap)
    {
        SetUE4Ver(Linker.UE4Ver());
        SetLicenseeUE4Ver(Linker.LicenseeUE4Ver());
        SetCustomVersions(Linker.GetCustomVersions());
    }
    virtual FArchive& operator<<(FName& Name) override
    {
        int32 Index = 0, Number = 0;
        InnerArchive << Index << Number;
        if (!Names.IsValidIndex(Index) || Number < 0)
        {
            SetError();
            Name = NAME_None;
        }
        else
            Name = FName::CreateFromDisplayId(Names[Index], Number);
        return *this;
    }
};
bool SavedLocation(UWorld* World, AActor* Actor, USceneComponent* Component, FVector& Value)
{
    const auto Fail = [&](const TCHAR* Why)
    {
#if P4_KADAN_TEST_SEAMS
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ATTACHMENT")) == TEXT("1"))
            UE_LOG(LogTemp, Display, TEXT("Saved attachment reader: %s"), Why);
#endif
        return false;
    };
    FLinkerLoad* Linker = World->GetLinker();
    if (!Linker || Component->GetOuter() != Actor || Linker->UE4Ver() != GPackageFileUE4Version)
        return Fail(TEXT("linker, direct outer or package version"));
    const FString Expected = FPackageName::LongPackageNameToFilename(
        World->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension());
    if (!FPaths::IsSamePath(
            FPaths::ConvertRelativePathToFull(Expected), FPaths::ConvertRelativePathToFull(Linker->Filename)))
        return Fail(TEXT("noncanonical filename"));
    int32 Found = INDEX_NONE;
    for (int32 Index = 0; Index < Linker->ExportMap.Num(); ++Index)
    {
        const FObjectExport& Export = Linker->ExportMap[Index];
        if (Export.ObjectName != Component->GetFName() || !Export.OuterIndex.IsExport())
            continue;
        const int32 Outer = Export.OuterIndex.ToExport();
        if (!Linker->ExportMap.IsValidIndex(Outer) || Linker->ExportMap[Outer].ObjectName != Actor->GetFName() ||
            !Linker->ExportMap[Outer].OuterIndex.IsExport() ||
            Linker->GetExportClassName(Index) != Component->GetClass()->GetFName())
            continue;
        const int32 Level = Linker->ExportMap[Outer].OuterIndex.ToExport();
        if (!Linker->ExportMap.IsValidIndex(Level) ||
            Linker->ExportMap[Level].ObjectName != World->PersistentLevel->GetFName() ||
            !Linker->ExportMap[Level].OuterIndex.IsExport())
            continue;
        const int32 Root = Linker->ExportMap[Level].OuterIndex.ToExport();
        if (!Linker->ExportMap.IsValidIndex(Root) || Linker->ExportMap[Root].ObjectName != World->GetFName())
            continue;
        if (Found != INDEX_NONE)
            return Fail(TEXT("duplicate export"));
        Found = Index;
    }
    if (Found == INDEX_NONE)
        return Fail(TEXT("missing component export"));
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Linker->Filename));
    if (!File)
        return Fail(TEXT("unreadable file"));
    FPackageFileSummary Summary;
    *File << Summary;
    if (File->IsError() || Summary.Guid != Linker->Summary.Guid || Summary.NameCount != Linker->Summary.NameCount ||
        Summary.NameOffset != Linker->Summary.NameOffset || Summary.ExportCount != Linker->Summary.ExportCount ||
        Summary.ExportOffset != Linker->Summary.ExportOffset || (Summary.PackageFlags & PKG_UnversionedProperties))
        return Fail(TEXT("summary identity or versioning"));
    const FObjectExport& Export = Linker->ExportMap[Found];
    if (Export.SerialSize <= 0 || Export.SerialSize > 64 * 1024 * 1024 || Export.SerialOffset < 0 ||
        Export.SerialOffset > File->TotalSize() - Export.SerialSize)
        return Fail(TEXT("export outside file"));
    const int64 End = Export.SerialOffset + Export.SerialSize;
    FPropertyNames Names(*File, *Linker);
    FArchive& Archive = Names;
    Archive.Seek(Export.SerialOffset);
    // The current editor's versioned tag header. Other package versions and
    // unversioned properties refuse above; no class/default inference is used.
    for (int32 Count = 0; Count < 4096 && Archive.Tell() < End; ++Count)
    {
        FName Name, Type, Struct;
        int32 Size = 0, Index = 0;
        Archive << Name;
        if (Archive.IsError() || Name.IsNone())
            return Fail(TEXT("tag end or invalid name"));
        Archive << Type << Size << Index;
        if (Type == FName(TEXT("StructProperty")))
        {
            FGuid Guid;
            Archive << Struct << Guid;
        }
        else if (Type == FName(TEXT("BoolProperty")))
        {
            uint8 Boolean = 0;
            Archive << Boolean;
        }
        else if (Type == FName(TEXT("ByteProperty")) || Type == FName(TEXT("EnumProperty")) ||
            Type == FName(TEXT("ArrayProperty")) || Type == FName(TEXT("SetProperty")))
        {
            FName Inner;
            Archive << Inner;
        }
        else if (Type == FName(TEXT("MapProperty")))
        {
            FName Key, Item;
            Archive << Key << Item;
        }
        uint8 HasGuid = 0;
        Archive << HasGuid;
        if (HasGuid > 1)
            return Fail(TEXT("property guid flag"));
        if (HasGuid)
        {
            FGuid Guid;
            Archive << Guid;
        }
        if (Archive.IsError() || Size < 0 || Archive.Tell() > End - Size)
            return Fail(TEXT("property extent"));
        if (Name == FName(TEXT("RelativeLocation")))
        {
            if (Type != FName(TEXT("StructProperty")) || Struct != FName(TEXT("Vector")) || Index != 0 || Size != 12)
                return Fail(TEXT("location schema"));
            Archive << Value;
            return !Archive.IsError() && !Value.ContainsNaN();
        }
        Archive.Seek(Archive.Tell() + Size);
    }
    return false;
}
struct FAttachment
{
    FName Actor;
    FString ActorClass, Component, ComponentClass, Parent, ParentClass;
    FName Socket;
    FVector Location, Scale;
    FRotator Rotation;
};
struct FSnapshot
{
    FString Source;
    FGuid Guid;
    TArray<FAttachment> Attachments;
};
TMap<FName, FSnapshot> Saved;
TSet<FName> Reloading;
FDelegateHandle LoadedHandle, SaveHandle, ReloadHandle, InitHandle;
constexpr int32 MaxWorlds = 512, MaxAttachmentsPerWorld = 1024, MaxSavedAttachments = 8192;

void Store(FName Package, FSnapshot&& Snapshot)
{
    Saved.Remove(Package);
    if (!Snapshot.Attachments.Num())
        return;
    int32 Total = Snapshot.Attachments.Num();
    for (const auto& Pair : Saved)
        Total += Pair.Value.Attachments.Num();
    // A long editor session must not retain an unbounded history. Eviction only
    // loses a transient repair opportunity; the saved package remains authoritative.
    while (Saved.Num() && (Saved.Num() >= MaxWorlds || Total > MaxSavedAttachments))
    {
        auto First = Saved.CreateIterator();
        Total -= First.Value().Attachments.Num();
        First.RemoveCurrent();
    }
    Saved.Add(Package, MoveTemp(Snapshot));
}

bool OriginalClass(UClass* Class)
{
    for (; Class && !Class->HasAnyClassFlags(CLASS_Native); Class = Class->GetSuperClass())
    {
        FP4OriginalRecord Record;
        FString Error;
        if (!Class->ClassGeneratedBy || P4HasCreatorSource(Class->ClassGeneratedBy) ||
            !P4ReadOriginalRecord(Class->ClassGeneratedBy, Record, Error) || Record.Kind != TEXT("blueprint") ||
            Record.Fidelity != TEXT("recovered"))
            return false;
    }
    return Class != nullptr;
}
void Capture(UWorld* World, bool FromSave = false)
{
    FP4OriginalRecord Record;
    FString Error;
    if (!World || !World->PersistentLevel || !P4ReadOriginalRecord(World, Record, Error) ||
        Record.Kind != TEXT("level") || P4HasCreatorSource(World))
        return;
    const FName Package = World->GetOutermost()->GetFName();
    if (!FromSave)
        if (const FSnapshot* Existing = Saved.Find(Package))
            if (Existing->Guid == World->GetOutermost()->GetGuid() && Existing->Source == Record.SourceDigest)
                return;
    const TSet<FName> Qualified = P4ImportedLevelActors(World);
    FSnapshot Snapshot;
    Snapshot.Source = Record.SourceDigest;
    Snapshot.Guid = World->GetOutermost()->GetGuid();
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!IsValid(Actor) || Actor->IsChildActor() || !Qualified.Contains(Actor->GetFName()) ||
            !OriginalClass(Actor->GetClass()))
            continue;
        TInlineComponentArray<USceneComponent*> Components(Actor);
        for (USceneComponent* Component : Components)
        {
            USceneComponent* Parent = Component ? Component->GetAttachParent() : nullptr;
            AActor* ParentActor = Parent ? Parent->GetOwner() : nullptr;
            // UE's construction replay detaches cross-actor attachments from an
            // SCS parent with KeepWorld, then recomputes their local coordinates.
            // Internal component trees and creator-owned parents are unrelated.
            if (!ParentActor || ParentActor == Actor || ParentActor->GetLevel() != World->PersistentLevel ||
                !Parent->IsCreatedByConstructionScript() || !Qualified.Contains(ParentActor->GetFName()) ||
                !OriginalClass(ParentActor->GetClass()) || Component->IsUsingAbsoluteLocation() ||
                Component->GetRelativeLocation().ContainsNaN())
                continue;
            FAttachment Item;
            Item.Actor = Actor->GetFName();
            Item.ActorClass = Actor->GetClass()->GetPathName();
            Item.Component = Component->GetPathName(Actor);
            Item.ComponentClass = Component->GetClass()->GetPathName();
            Item.Parent = Parent->GetPathName(World->PersistentLevel);
            Item.ParentClass = Parent->GetClass()->GetPathName();
            Item.Socket = Component->GetAttachSocketName();
            Item.Location = Component->GetRelativeLocation();
            if (!FromSave && !SavedLocation(World, Actor, Component, Item.Location))
                continue;
            Item.Rotation = Component->GetRelativeRotation();
            Item.Scale = Component->GetRelativeScale3D();
#if P4_KADAN_TEST_SEAMS
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ATTACHMENT")) == TEXT("1"))
                UE_LOG(LogTemp, Display,
                    TEXT("Attachment capture %s save=%d initialized=%d location %.12f %.12f %.12f"), *World->GetName(),
                    FromSave, World->bIsWorldInitialized, Item.Location.X, Item.Location.Y, Item.Location.Z);
#endif
            if (Snapshot.Attachments.Num() >= MaxAttachmentsPerWorld)
                return;
            Snapshot.Attachments.Add(MoveTemp(Item));
        }
    }
    if (Snapshot.Attachments.Num() || FromSave)
        Store(Package, MoveTemp(Snapshot));
}
void Restore(UWorld* World)
{
    if (!World || !World->PersistentLevel)
        return;
    FSnapshot Snapshot;
    if (!Saved.RemoveAndCopyValue(World->GetOutermost()->GetFName(), Snapshot))
    {
#if P4_KADAN_TEST_SEAMS
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ATTACHMENT")) == TEXT("1"))
            UE_LOG(LogTemp, Display, TEXT("Attachment restore has no saved snapshot: %s"), *World->GetPathName());
#endif
        return;
    }
#if P4_KADAN_TEST_SEAMS
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ATTACHMENT")) == TEXT("1"))
        UE_LOG(LogTemp, Display, TEXT("Attachment restore %s: %d entries guid=%d"), *World->GetPathName(),
            Snapshot.Attachments.Num(), World->GetOutermost()->GetGuid() == Snapshot.Guid);
#endif
    FP4OriginalRecord Record;
    FString Error;
    if (!P4ReadOriginalRecord(World, Record, Error) || Record.Kind != TEXT("level") ||
        Record.SourceDigest != Snapshot.Source || World->GetOutermost()->GetGuid() != Snapshot.Guid ||
        P4HasCreatorSource(World))
        return;
    const TSet<FName> Qualified = P4ImportedLevelActors(World);
    for (const FAttachment& Item : Snapshot.Attachments)
    {
        AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Item.Actor.ToString());
        USceneComponent* Component = Actor ? FindObject<USceneComponent>(Actor, *Item.Component) : nullptr;
        USceneComponent* Parent = Component ? Component->GetAttachParent() : nullptr;
        AActor* ParentActor = Parent ? Parent->GetOwner() : nullptr;
        if (!Actor || !Qualified.Contains(Item.Actor) || Actor->GetClass()->GetPathName() != Item.ActorClass ||
            !OriginalClass(Actor->GetClass()) || !Component ||
            Component->GetClass()->GetPathName() != Item.ComponentClass || !Parent || !ParentActor ||
            ParentActor->GetLevel() != World->PersistentLevel || !Qualified.Contains(ParentActor->GetFName()) ||
            !OriginalClass(ParentActor->GetClass()) || Parent->GetPathName(World->PersistentLevel) != Item.Parent ||
            Parent->GetClass()->GetPathName() != Item.ParentClass || !Parent->IsCreatedByConstructionScript() ||
            Component->GetAttachSocketName() != Item.Socket || Component->IsUsingAbsoluteLocation() ||
            Component->GetRelativeRotation() != Item.Rotation || Component->GetRelativeScale3D() != Item.Scale)
            continue;
        const FVector Current = Component->GetRelativeLocation();
#if P4_KADAN_TEST_SEAMS
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ATTACHMENT")) == TEXT("1"))
            UE_LOG(LogTemp, Display, TEXT("Attachment candidate saved %.12f %.12f current %.12f %.12f"),
                Item.Location.X, Item.Location.Y, Current.X, Current.Y);
#endif
        if (Current == Item.Location)
            continue;
        Parent->UpdateComponentToWorld();
        const FTransform Basis = Parent->GetSocketTransform(Item.Socket);
        if (!Basis.IsValid())
            continue;
        FVector TransformStep = Item.Location, PositionStep = Item.Location;
        bool RoundTrip = false;
        // These are exact results of the engine's float operations, not a
        // distance tolerance. Detach/reattach and two reconstructions can each round-trip.
        for (int32 Pass = 0; Pass < 4; ++Pass)
        {
            TransformStep =
                (FTransform(Item.Rotation, TransformStep, Item.Scale) * Basis).GetRelativeTransform(Basis).GetLocation();
            PositionStep = Basis.InverseTransformPosition(Basis.TransformPosition(PositionStep));
            RoundTrip |= Current == TransformStep || Current == PositionStep;
#if P4_KADAN_TEST_SEAMS
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ATTACHMENT")) == TEXT("1"))
                UE_LOG(LogTemp, Display, TEXT("Attachment roundtrip %d transform %.12f %.12f position %.12f %.12f"),
                    Pass, TransformStep.X, TransformStep.Y, PositionStep.X, PositionStep.Y);
#endif
        }
        if (RoundTrip)
        {
            // The ordinary movement setter itself converts through world space.
            // Restore the serialized local field, then refresh its transient pose.
            Component->SetRelativeLocation_Direct(Item.Location);
            Component->UpdateComponentToWorld();
            UE_LOG(LogTemp, Verbose, TEXT("Retained saved original attachment coordinates: %s"),
                *Component->GetPathName());
        }
    }
}
} // namespace
void P4RestoreOriginalActorAttachments(UWorld* World)
{
    Restore(World);
    if (World)
        for (ULevel* Level : World->GetLevels())
            if (Level)
                if (UWorld* Source = Cast<UWorld>(Level->GetOuter()))
                    if (Source != World)
                        Restore(Source);
}
void P4StartOriginalActorAttachments()
{
    if (LoadedHandle.IsValid())
        return;
    InitHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda(
        [](UWorld* World, const UWorld::InitializationValues) { Capture(World); });
    LoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddLambda(
        [](UObject* Asset)
        {
            if (auto* World = Cast<UWorld>(Asset))
            {
                // Asset load can already have reconstructed its spawner. Settle this
                // before an import snapshot can mistake engine rounding for an edit,
                // then retain the saved values for the later editor map activation.
                Capture(World);
                Restore(World);
                Capture(World);
            }
        });
    SaveHandle = FEditorDelegates::PostSaveWorld.AddLambda(
        [](uint32 Flags, UWorld* World, bool Success)
        {
            if (Success && !(Flags & (SAVE_KeepDirty | SAVE_Unversioned)))
                Capture(World, true);
        });
    ReloadHandle = FCoreUObjectDelegates::OnPackageReloaded.AddLambda(
        [](EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
        {
            if (Phase == EPackageReloadPhase::PostPackageFixup && Event && Event->GetNewPackage())
                Reloading.Add(Event->GetNewPackage()->GetFName());
            if (Phase == EPackageReloadPhase::PostBatchPostGC)
            {
                for (const FName Package : Reloading)
                    if (UPackage* Loaded = FindPackage(nullptr, *Package.ToString()))
                        Restore(UWorld::FindWorldInPackage(Loaded));
                Reloading.Empty();
            }
        });
}
void P4StopOriginalActorAttachments()
{
    FWorldDelegates::OnPreWorldInitialization.Remove(InitHandle);
    InitHandle.Reset();
    FCoreUObjectDelegates::OnAssetLoaded.Remove(LoadedHandle);
    FEditorDelegates::PostSaveWorld.Remove(SaveHandle);
    FCoreUObjectDelegates::OnPackageReloaded.Remove(ReloadHandle);
    LoadedHandle.Reset();
    SaveHandle.Reset();
    ReloadHandle.Reset();
    Saved.Empty();
    Reloading.Empty();
}

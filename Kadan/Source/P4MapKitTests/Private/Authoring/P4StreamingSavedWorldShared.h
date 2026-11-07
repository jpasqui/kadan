// The helpers that P4StreamingSavedWorldCoverageAutomation.cpp and its companion test files share.
#pragma once
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"

namespace P4StreamingSavedWorldCoverage
{
// These fixtures are real initialized UE worlds and native map packages.
// SetLoadedLevel/private accessors, reflection edits and fabricated state
// are intentionally unnecessary: the ordinary engine loads each child.
struct FP4StreamingSavedWorldFixture
{
    FString Base;
    TArray<UWorld*> Worlds;
    TArray<FString> Files;
    FP4StreamingSavedWorldFixture()
        : Base(TEXT("/Game/P4StreamingSavedCoverage/") + FGuid::NewGuid().ToString(EGuidFormats::Digits))
    {}
    ~FP4StreamingSavedWorldFixture()
    {
        // Remove only this fixture's streaming references before destroying
        // its worlds; do not unload another test's package or editor world.
        for (UWorld* World : Worlds)
        {
            const TArray<ULevel*> Levels = World->GetLevels();
            for (ULevel* Level : Levels)
                if (Level != World->PersistentLevel)
                    World->RemoveFromWorld(Level, false);
            const TArray<ULevelStreaming*> Streams = World->GetStreamingLevels();
            for (ULevelStreaming* Stream : Streams)
                World->RemoveStreamingLevel(Stream);
        }
        for (int32 I = Worlds.Num() - 1; I >= 0; --I)
        {
            Worlds[I]->DestroyWorld(false);
            GEngine->DestroyWorldContext(Worlds[I]);
        }
        for (const FString& File : Files)
            IFileManager::Get().Delete(*File, false, true);
    }
    UWorld* Make(const TCHAR* Name)
    {
        UPackage* Package = CreatePackage(*(Base + TEXT("/") + Name));
        UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(Name), Package);
        GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
        World->SetFlags(RF_Public | RF_Standalone);
        Package->GetMetaData(); // Materialize native metadata before snapshots.
        Worlds.Add(World);
        return World;
    }
    bool Save(UWorld* World)
    {
        const FString File = FPackageName::LongPackageNameToFilename(
            World->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension());
        Files.AddUnique(File);
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true))
            return false;
        return UPackage::SavePackage(World->GetOutermost(), World, RF_Public | RF_Standalone, *File, GError, nullptr,
            false, false, SAVE_NoError, nullptr, FDateTime::MinValue(), false);
    }
    ULevelStreamingDynamic* Add(UWorld* Parent, UWorld* Child, const TCHAR* Name)
    {
        ULevelStreamingDynamic* Stream = NewObject<ULevelStreamingDynamic>(Parent, FName(Name));
        Stream->SetWorldAssetByPackageName(Child->GetOutermost()->GetFName());
        Stream->SetShouldBeLoaded(false);
        Stream->SetShouldBeVisible(false);
        Stream->SetShouldBeVisibleInEditor(false);
        Stream->LevelTransform = FTransform(FRotator(4, 19, -7), FVector(137, -92, 33), FVector(1, 1, 1));
        Parent->AddStreamingLevel(Stream);
        return Stream;
    }
};

inline TArray<uint8> Bytes(UObject* Object)
{
    TArray<uint8> Out;
    FObjectWriter Writer(Object, Out, false, false, false, PPF_DuplicateVerbatim);
    return Out;
}

struct FObjectState
{
    UObject* Object;
    UObject* Outer;
    UClass* Class;
    FString Path;
    EObjectFlags Flags;
    TArray<uint8> Serialized;
    explicit FObjectState(UObject* In)
        : Object(In), Outer(In->GetOuter()), Class(In->GetClass()), Path(In->GetPathName()), Flags(In->GetFlags()),
          Serialized(Bytes(In))
    {}
    bool Same() const
    {
        return Object->GetOuter() == Outer && Object->GetClass() == Class && Object->GetPathName() == Path &&
            Object->GetFlags() == Flags && Bytes(Object) == Serialized;
    }
};
struct FWorldState
{
    UWorld* World;
    ULevel* Persistent;
    ULevel* Current;
    TArray<ULevel*> Levels;
    TArray<ULevelStreaming*> Streams;
    TArray<FObjectState> Objects;
    UPackage* Package;
    uint32 PackageFlags;
    bool Dirty;
    TArray<uint8> Metadata;
    static UWorld* CompleteFrameUpdates(UWorld* In)
    {
        // SaveMap queues or completes brush renderer updates. Compare the
        // entire authored graph after normal deferred work has completed,
        // rather than restoring pending component pointers from a snapshot.
        In->SendAllEndOfFrameUpdates();
        return In;
    }
    explicit FWorldState(UWorld* In)
        : World(CompleteFrameUpdates(In)), Persistent(In->PersistentLevel), Current(In->GetCurrentLevel()),
          Levels(In->GetLevels()), Streams(In->GetStreamingLevels()), Package(In->GetOutermost()),
          PackageFlags(Package->GetPackageFlags()), Dirty(Package->IsDirty()), Metadata(Bytes(Package->GetMetaData()))
    {
        Objects.Emplace(In);
        TArray<UObject*> Children;
        GetObjectsWithOuter(In, Children, true);
        for (UObject* Object : Children)
            Objects.Emplace(Object);
    }
    bool Same() const
    {
        CompleteFrameUpdates(World);
        if (World->PersistentLevel != Persistent || World->GetCurrentLevel() != Current ||
            World->GetLevels() != Levels || World->GetStreamingLevels() != Streams ||
            World->GetOutermost() != Package || Package->GetPackageFlags() != PackageFlags ||
            Package->IsDirty() != Dirty || Bytes(Package->GetMetaData()) != Metadata)
            return false;
        TArray<UObject*> Children;
        GetObjectsWithOuter(World, Children, true);
        if (Children.Num() + 1 != Objects.Num())
            return false;
        for (const FObjectState& Object : Objects)
            if (!Object.Same())
                return false;
        return true;
    }
};
struct FDiskState
{
    FString Directory;
    TArray<FString> Names;
    TMap<FString, TArray<uint8>> Payloads;
    explicit FDiskState(const FString& Base)
        : Directory(FPaths::GetPath(FPackageName::LongPackageNameToFilename(Base + TEXT("/Marker"))))
    {
        IFileManager::Get().FindFilesRecursive(Names, *Directory, TEXT("*"), true, false);
        Names.Sort();
        for (const FString& Name : Names)
        {
            TArray<uint8> Data;
            FFileHelper::LoadFileToArray(Data, *Name);
            Payloads.Add(Name, MoveTemp(Data));
        }
    }
    bool Same() const
    {
        TArray<FString> Now;
        IFileManager::Get().FindFilesRecursive(Now, *Directory, TEXT("*"), true, false);
        Now.Sort();
        if (Now != Names)
            return false;
        for (const FString& Name : Names)
        {
            TArray<uint8> Data;
            if (!FFileHelper::LoadFileToArray(Data, *Name) || Data != Payloads.FindChecked(Name))
                return false;
        }
        return true;
    }
};
inline FString SameTarget(ULevelStreaming* Stream, bool Visible)
{
    return FString::Printf(TEXT("{\"%s\":{\"target\":\"%s\",\"loaded\":true,\"visible\":%s,\"editor_visible\":%s}}"),
        *Stream->GetWorldAssetPackageName(), *Stream->GetWorldAssetPackageName(),
        Visible ? TEXT("true") : TEXT("false"), Visible ? TEXT("true") : TEXT("false"));
}
} //namespace P4StreamingSavedWorldCoverage
#endif

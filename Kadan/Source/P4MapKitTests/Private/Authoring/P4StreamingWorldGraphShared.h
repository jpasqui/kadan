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
#include "P4StreamingSavedWorldShared.h"
#include "AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Linker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"

namespace P4WorldGraphNativeBranches
{
using namespace P4StreamingSavedWorldCoverage;
#if PLATFORM_WINDOWS
struct FMapFileEvidence
{
    BY_HANDLE_FILE_INFORMATION Identity{};
    TArray<uint8> Contents;
    static bool ReadIdentity(const FString& Path, BY_HANDLE_FILE_INFORMATION& Out)
    {
        HANDLE Handle = CreateFileW(*Path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (Handle == INVALID_HANDLE_VALUE)
            return false;
        const bool Read = GetFileInformationByHandle(Handle, &Out) != 0;
        CloseHandle(Handle);
        return Read &&
            !(Out.dwFileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY));
    }
    static bool SameIdentity(const BY_HANDLE_FILE_INFORMATION& A, const BY_HANDLE_FILE_INFORMATION& B)
    {
        // Last access time can change during our own full-byte read.
        return A.dwVolumeSerialNumber == B.dwVolumeSerialNumber && A.nFileIndexHigh == B.nFileIndexHigh &&
            A.nFileIndexLow == B.nFileIndexLow && A.nFileSizeHigh == B.nFileSizeHigh &&
            A.nFileSizeLow == B.nFileSizeLow && A.dwFileAttributes == B.dwFileAttributes &&
            CompareFileTime(&A.ftCreationTime, &B.ftCreationTime) == 0 &&
            CompareFileTime(&A.ftLastWriteTime, &B.ftLastWriteTime) == 0;
    }
    bool Capture(const FString& Path)
    {
        BY_HANDLE_FILE_INFORMATION After{};
        return ReadIdentity(Path, Identity) && FFileHelper::LoadFileToArray(Contents, *Path) &&
            ReadIdentity(Path, After) && SameIdentity(Identity, After);
    }
    bool Same(const FString& Path) const
    {
        BY_HANDLE_FILE_INFORMATION Before{}, After{};
        TArray<uint8> Current;
        return ReadIdentity(Path, Before) && SameIdentity(Identity, Before) &&
            FFileHelper::LoadFileToArray(Current, *Path) && Current == Contents && ReadIdentity(Path, After) &&
            SameIdentity(Identity, After);
    }
};
#endif
struct FRegistryCustody
{
    FP4StreamingSavedWorldFixture& Fixture;
    FAutomationTestBase& Test;
    TArray<UWorld*> Assets;
    FRegistryCustody(FP4StreamingSavedWorldFixture& InFixture, FAutomationTestBase& InTest)
        : Fixture(InFixture), Test(InTest)
    {}
    ~FRegistryCustody()
    {
        // This phase runs only AFTER the inspection's complete unchanged
        // oracles. Native UObject inventory can rediscover a public live
        // asset even after its cached registry record has been deleted.
        for (UWorld* World : Fixture.Worlds)
        {
            const bool Owned = World->GetOutermost()->GetName().StartsWith(
                Fixture.Base + TEXT("/"), ESearchCase::CaseSensitive);
            Test.TestTrue(TEXT("Cleanup touches only this exact GUID-owned World package"), Owned);
            if (!Owned)
                continue;
            if (Assets.Contains(World))
                FAssetRegistryModule::AssetDeleted(World);
            World->ClearFlags(RF_Public | RF_Standalone);
            World->SetFlags(RF_Transient);
            Test.TestFalse(TEXT("Disposed owned World cannot reappear as a live UObject asset"), World->IsAsset());
        }
        // Consume the old fixture's deletion list here so its destructor
        // cannot silently retry a failed deletion and erase that evidence.
        const TArray<FString> Files = Fixture.Files;
        Fixture.Files.Empty();
        const FString Directory = FPaths::ConvertRelativePathToFull(
            FPaths::GetPath(FPackageName::LongPackageNameToFilename(Fixture.Base + TEXT("/Marker"))));
        for (const FString& File : Files)
        {
            const FString Absolute = FPaths::ConvertRelativePathToFull(File);
            UPackage* OwnedPackage = nullptr;
            for (UWorld* World : Fixture.Worlds)
                if (World->GetOutermost()->GetName().StartsWith(Fixture.Base + TEXT("/"), ESearchCase::CaseSensitive) &&
                    FPaths::IsSamePath(Absolute,
                        FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(
                            World->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension()))))
                    OwnedPackage = World->GetOutermost();
            const bool Owned = FPaths::IsUnderDirectory(Absolute, Directory) && OwnedPackage;
            Test.TestTrue(TEXT("Cleanup file is an exact recorded map below this GUID's resolved directory"), Owned);
            if (!Owned)
                continue;
            bool Deleted = !IFileManager::Get().FileExists(*Absolute);
            uint32 DeleteError = 0;
            int32 Attempts = 0;
#if PLATFORM_WINDOWS
            FMapFileEvidence Original;
            const bool Captured = Deleted || Original.Capture(Absolute);
            Test.TestTrue(
                *FString::Printf(
                    TEXT("Owned map cleanup captures native file identity and full original bytes: %s"), *Absolute),
                Captured);
            if (!Deleted && Captured)
            {
                const FName OriginalPackageName = OwnedPackage->GetFName();
                // Post-oracle only: detach this exact package's disk loader.
                ResetLoadersForSave(OwnedPackage, *Absolute);
                for (int32 Attempt = 0; Attempt < 5; ++Attempt)
                {
                    if (Attempt > 0)
                        FPlatformProcess::Sleep(0.05f);
                    const bool Unchanged = OwnedPackage->GetFName() == OriginalPackageName &&
                        OriginalPackageName.ToString().StartsWith(
                            Fixture.Base + TEXT("/"), ESearchCase::CaseSensitive) &&
                        FPaths::IsUnderDirectory(Absolute, Directory) &&
                        FPaths::IsSamePath(Absolute,
                            FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(
                                OriginalPackageName.ToString(), FPackageName::GetMapPackageExtension()))) &&
                        Original.Same(Absolute);
                    Test.TestTrue(
                        *FString::Printf(
                            TEXT(
                                "Before owned deletion attempt %d exact package/path/file identity/full bytes remain original: %s"),
                            Attempt + 1, *Absolute),
                        Unchanged);
                    if (!Unchanged)
                        break;
                    ++Attempts;
                    Deleted = IFileManager::Get().Delete(*Absolute, true, false, true);
                    DeleteError = Deleted ? 0 : FPlatformMisc::GetLastError();
                    if (Deleted || (DeleteError != ERROR_SHARING_VIOLATION && DeleteError != ERROR_LOCK_VIOLATION))
                        break;
                }
            }
#else
            if (!Deleted)
            {
                ResetLoadersForSave(OwnedPackage, *Absolute);
                ++Attempts;
                Deleted = IFileManager::Get().Delete(*Absolute, true, false, true);
                DeleteError = Deleted ? 0 : FPlatformMisc::GetLastError();
            }
#endif
            const bool Absent = !IFileManager::Get().FileExists(*Absolute);
            Test.TestTrue(
                *FString::Printf(
                    TEXT("Exact owned map deletion succeeds and leaves no file: %s; attempts=%d; native_error=%u"),
                    *Absolute, Attempts, DeleteError),
                Deleted && Absent);
            if (Deleted && Attempts > 1)
                Test.AddInfo(FString::Printf(
                    TEXT("Owned map cleanup recovered sharing/lock contention: %s; attempts=%d"), *Absolute, Attempts));
        }
        for (UWorld* World : Fixture.Worlds)
        {
            if (!World->GetOutermost()->GetName().StartsWith(Fixture.Base + TEXT("/"), ESearchCase::CaseSensitive))
                continue;
            const FString File = FPackageName::LongPackageNameToFilename(
                World->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension());
            if (IFileManager::Get().FileExists(*File))
                continue; // Retain real failure evidence.
            FAssetRegistryModule::PackageDeleted(World->GetOutermost());
            TArray<FAssetData> Rows;
            FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(World->GetOutermost()->GetFName(), Rows);
            Test.TestEqual(TEXT("Disposed exact owned package leaves no native registry inventory"), Rows.Num(), 0);
        }
    }
    bool SaveAndRegister(FP4StreamingSavedWorldFixture& InFixture, UWorld* World)
    {
        if (!InFixture.Save(World))
            return false;
        FAssetRegistryModule::AssetCreated(World);
        Assets.Add(World);
        TArray<FAssetData> Rows;
        FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(World->GetOutermost()->GetFName(), Rows);
        return Rows.Num() == 1 && Rows[0].ObjectPath == FName(*World->GetPathName()) &&
            Rows[0].AssetClass == UWorld::StaticClass()->GetFName();
    }
    TArray<uint8> Snapshot() const
    {
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes);
        for (UWorld* World : Assets)
        {
            // Every row originates in the real registry, never FAssetData
            // construction. Preserve all public inventory fields and tags.
            TArray<FAssetData> Rows;
            FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(World->GetOutermost()->GetFName(), Rows);
            Rows.Sort([](const FAssetData& A, const FAssetData& B)
                { return A.ObjectPath.ToString() < B.ObjectPath.ToString(); });
            int32 Count = Rows.Num();
            Writer << Count;
            for (const FAssetData& Row : Rows)
            {
                FString Path = Row.ObjectPath.ToString(), Package = Row.PackageName.ToString(),
                        Folder = Row.PackagePath.ToString();
                FString Name = Row.AssetName.ToString(), Class = Row.AssetClass.ToString();
                TArray<int32> Chunks = Row.ChunkIDs;
                uint32 Flags = Row.PackageFlags;
                Writer << Path << Package << Folder << Name << Class << Chunks << Flags;
                int32 Tags = Row.TagsAndValues.Num();
                Writer << Tags;
                for (const auto& Tag : Row.TagsAndValues)
                {
                    FString Key = Tag.Key.ToString(), Value = Tag.Value;
                    Writer << Key << Value;
                }
            }
        }
        return Bytes;
    }
};
struct FPreservedGraph
{
    TArray<FWorldState> Worlds;
    FDiskState Disk;
    const FRegistryCustody& Registry;
    TArray<uint8> RegistryBefore;
    FPreservedGraph(const FP4StreamingSavedWorldFixture& Fixture, const FRegistryCustody& InRegistry)
        : Disk(Fixture.Base), Registry(InRegistry), RegistryBefore(InRegistry.Snapshot())
    {
        for (UWorld* World : Fixture.Worlds)
            Worlds.Emplace(World);
    }
    void Check(FAutomationTestBase& Test) const
    {
        for (const FWorldState& State : Worlds)
            Test.TestTrue(
                TEXT(
                    "Inspection preserves full native world bytes, pointers, class, flags, metadata, dirty state and inventories"),
                State.Same());
        Test.TestTrue(TEXT("Inspection preserves exact owned source Content namespace and file bytes"), Disk.Same());
        Test.TestTrue(TEXT("Inspection preserves actual registry rows, package flags, chunk inventories and tags"),
            Registry.Snapshot() == RegistryBefore);
    }
};
inline TSharedPtr<FJsonObject> Verdict(FAutomationTestBase& Test, UWorld* World, const FString& Target)
{
    TSharedPtr<FJsonObject> Result;
    Test.TestTrue(TEXT("Native graph result is actual JSON"),
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeWorldStreamingGraph(World, Target)), Result) &&
            Result.IsValid());
    return Result;
}
inline bool ExactRefusal(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Result, const FString& Error)
{
    if (!Result.IsValid())
        return false;
    bool Valid = true;
    FString Actual;
    Test.TestTrue(TEXT("Native refusal states invalid"), Result->TryGetBoolField(TEXT("valid"), Valid) && !Valid);
    Test.TestTrue(TEXT("Native refusal reports exact independent reason"),
        Result->TryGetStringField(TEXT("error"), Actual) && Actual == Error);
    Test.TestFalse(TEXT("Refusal cannot publish a partial graph"), Result->HasField(TEXT("graph")));
    return !Test.HasAnyErrors();
}
} //namespace P4WorldGraphNativeBranches
#endif

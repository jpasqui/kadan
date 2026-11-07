#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "P4OriginalWorldSource.h"
#include "P4OriginalLevelImport.h"
#include "P4OriginalRecord.h"
#include "P4LevelChanges.h"
#include "P4NativeHash.h"
#include "P4RegistryInputLease.h"
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/PointLight.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace P4WorldSourceCustodyTest
{
FString Full(FString P)
{
    P = FPaths::ConvertRelativePathToFull(P);
    FPaths::NormalizeFilename(P);
    FPaths::CollapseRelativeDirectories(P);
    FPaths::NormalizeDirectoryName(P);
    return P;
}
struct FProof
{
    FAutomationTestBase& Test;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    explicit FProof(FAutomationTestBase& T) : Test(T)
    {
        Json->SetBoolField(TEXT("passed"), false);
        Json->SetStringField(TEXT("stage"), TEXT("preflight"));
    }
    ~FProof()
    {
        bool Passed = false;
        Json->TryGetBoolField(TEXT("passed"), Passed);
        Json->SetBoolField(TEXT("passed"), Passed && !Test.HasAnyErrors());
        FString Text;
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/WorldSourceCustody/actual.json");
        if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) ||
            !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
            !FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("Cannot retain world source custody proof"));
    }
};
bool Link(const FString& To, const FString& From)
{
#if PLATFORM_WINDOWS
    return CreateHardLinkW(*To, *From, nullptr) != 0;
#else
    return false;
#endif
}
} //namespace P4WorldSourceCustodyTest
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WorldSourceCustody, "P4MapKit.Kadan.WorldSourceCustodyLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WorldSourceCustody::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WORLD_SOURCE_CUSTODY")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Skipped world source custody lifetime; requires explicit actual source and owned fixture"));
        return true;
    }
    using namespace P4WorldSourceCustodyTest;
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"))),
                  Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Only the existing separate owned Env fixture is admitted"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(Full(FPaths::ProjectDir()), Repo / TEXT("private/native-postprocess-env/project"))))
        return false;
    FProof Proof(*this);
    FString Error;
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WORLD_SOURCE_CUSTODY_HEADER"));
    FP4OriginalWorldSourcePtr Actual;
    if (!TestTrue(TEXT("Actual selected map pair is immutable and canonical"),
            P4ReadOriginalWorldSource(Header, FString(), FString(), Actual, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Package = Actual->Package(), OriginalHeader = Actual->HeaderPath(), OriginalBody = Actual->BodyPath(),
                  SourceHash = Actual->SourceDigest();
    if (!TestTrue(TEXT("No existing map destination can be overwritten"),
            !FPackageName::DoesPackageExist(Package) && !FindPackage(nullptr, *Package)))
        return false;
    const FString Root = Full(FPaths::ProjectSavedDir() / TEXT("P4MapKit/WorldSourceCustody/owned-inputs"));
    if (!TestTrue(TEXT("Refuse stale or foreign control staging"),
            !IFileManager::Get().DirectoryExists(*Root) && !IFileManager::Get().FileExists(*Root)))
        return false;
    const FString Stage = Root / TEXT("staging"), Changed = Root / TEXT("changed"),
                  Short = FPackageName::GetShortName(Package);
    const FString StageH = Stage / (Short + TEXT(".umap")), StageB = Stage / (Short + TEXT(".uexp"));
    const FString ChangedH = Changed / (Short + TEXT(".umap")), ChangedB = Changed / (Short + TEXT(".uexp"));
    TArray<uint8> ChangedBytes = Actual->Body();
    ChangedBytes.Last() ^= 1;
    Actual.Reset();
    TStrongObjectPtr<UWorld> World;
    FString SavedFile, SavedHash;
    int64 SavedSize = 0;
    bool Created = false;
    TUniquePtr<P4RegistryLease::FInput> SavedLease;
    ON_SCOPE_EXIT
    {
        Actual.Reset();
        SavedLease.Reset();
        if (World.IsValid())
        {
            FAssetRegistryModule::AssetDeleted(World.Get());
            World->ClearFlags(RF_Public | RF_Standalone);
            World->DestroyWorld(false);
            World->MarkPendingKill();
            World.Reset();
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        }
        if (Created)
        {
            FString Now;
            int64 N = 0;
            const bool Unchanged = P4HashFile(SavedFile, 64 * 1024 * 1024, N, Now) && N == SavedSize &&
                P4ExactNativeString(Now, SavedHash);
            TestTrue(TEXT("Only unchanged newly created control map may be unlinked"),
                Unchanged && IFileManager::Get().Delete(*SavedFile, false, false));
        }
        bool Clean = true;
        for (const FString& File : {StageH, StageB, ChangedH, ChangedB})
            if (IFileManager::Get().FileExists(*File))
                Clean = IFileManager::Get().Delete(*File, false, false) && Clean;
        for (const FString& Dir : {Stage, Changed, Root})
            if (IFileManager::Get().DirectoryExists(*Dir))
                Clean = IFileManager::Get().DeleteDirectory(*Dir, false, false) && Clean;
        TestTrue(TEXT("Exact owned control inputs are released"), Clean && !IFileManager::Get().DirectoryExists(*Root));
        FP4OriginalWorldSourcePtr Final;
        TestTrue(TEXT("Original selected source bytes remain exact"),
            P4ReadOriginalWorldSource(OriginalHeader, OriginalBody, Package, Final, Error) &&
                P4ExactNativeString(Final->SourceDigest(), SourceHash));
    };
    if (!IFileManager::Get().MakeDirectory(*Stage, true) || !IFileManager::Get().MakeDirectory(*Changed, true) ||
        !Link(StageH, OriginalHeader) || !Link(StageB, OriginalBody) || !Link(ChangedH, OriginalHeader) ||
        !FFileHelper::SaveArrayToFile(ChangedBytes, *ChangedB))
        return false;
    const auto Imported = P4ImportGameBackedLevelNative(StageH, StageB, true, {},
        TEXT("Controlled source custody lifetime; original actor reconstruction deliberately unavailable"), false);
    if (!TestTrue(TEXT("Fresh control projection is saved without any source actor fidelity claim"),
            Imported.IsValid() && Imported.Created && Imported.Saved && Imported.GameBacked && Imported.Asset))
    {
        AddError(Imported.ErrorDetail);
        return false;
    }
    World.Reset(Imported.Asset);
    Created = true;
    if (!TestTrue(TEXT("Fresh saved control file exists and is pinned"),
            FPackageName::DoesPackageExist(Package, nullptr, &SavedFile) &&
                P4HashFile(SavedFile, 64 * 1024 * 1024, SavedSize, SavedHash)))
        return false;
    SavedLease = MakeUnique<P4RegistryLease::FInput>();
#if PLATFORM_WINDOWS
    if (!TestTrue(
            TEXT("Resume cannot silently save over the original file"), SavedLease->Open(std::wstring(*SavedFile))))
        return false;
#else
    return false;
#endif
    UMetaData* Meta = World->GetOutermost()->GetMetaData();
    const FString Baseline = Meta->GetValue(World.Get(), TEXT("P4NativeLevelBaseline")),
                  ImportedLines = Meta->GetValue(World.Get(), TEXT("P4NativeLevelImported"));
    const FString InitialCustody = Meta->GetValue(World.Get(), TEXT("P4GameBackedLevelCustody")),
                  InitialRecord = Meta->GetValue(World.Get(), TEXT("P4OriginalRecord"));
    const auto InitialNoop = P4ResumeOriginalLevelNative(World.Get(), StageH, StageB);
    if (!TestTrue(TEXT("Clean same-source resume preserves metadata, dirty state and saved file"),
            InitialNoop.IsValid() && !InitialNoop.Saved && !World->GetOutermost()->IsDirty() &&
                InitialCustody == Meta->GetValue(World.Get(), TEXT("P4GameBackedLevelCustody")) &&
                InitialRecord == Meta->GetValue(World.Get(), TEXT("P4OriginalRecord"))))
        return false;
    FActorSpawnParameters Spawn;
    Spawn.OverrideLevel = World->PersistentLevel;
    Spawn.Name = TEXT("CustodyCreatorLight");
    APointLight* Added = World->SpawnActor<APointLight>(FVector(10, 20, 30), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("A real creator edit exists before resume"), Added))
        return false;
    World->MarkPackageDirty();
    const FVector Location = Added->GetActorLocation();
    const FString OldCustody = Meta->GetValue(World.Get(), TEXT("P4GameBackedLevelCustody")),
                  OldRecord = Meta->GetValue(World.Get(), TEXT("P4OriginalRecord"));
    FP4OriginalRecord Before;
    if (!TestTrue(TEXT("Old actor baseline and source identity are genuine"),
            P4ReadOriginalRecord(World.Get(), Before, Error) && P4ExactNativeString(Before.SourceDigest, SourceHash)))
        return false;
    if (!IFileManager::Get().Delete(*StageH, false, false) || !IFileManager::Get().Delete(*StageB, false, false))
        return false;
    TSet<FName> Omitted;
    FString Digest;
    if (!TestFalse(TEXT("Lost literal old source paths remain a real refusal without selected authority"),
            P4ReadGameBackedLevelCustody(World.Get(), Omitted, Digest, Error)))
        return false;
    const auto Missing = P4ResumeOriginalLevelNative(World.Get(), StageH, StageB);
    const auto ChangedResult = P4ResumeOriginalLevelNative(World.Get(), ChangedH, ChangedB);
    FP4OriginalWorldSourcePtr Wrong;
    if (!TestFalse(TEXT("Wrong package cannot establish a World source witness"),
            P4ReadOriginalWorldSource(OriginalHeader, OriginalBody, Package + TEXT("_Other"), Wrong, Error)) ||
        !TestTrue(TEXT("Missing/altered pairs refuse before bookkeeping changes"),
            !Missing.IsValid() && !ChangedResult.IsValid() &&
                OldCustody == Meta->GetValue(World.Get(), TEXT("P4GameBackedLevelCustody")) &&
                OldRecord == Meta->GetValue(World.Get(), TEXT("P4OriginalRecord")) && World->GetOutermost()->IsDirty()))
        return false;
    const auto Resumed = P4ResumeOriginalLevelNative(World.Get(), OriginalHeader, OriginalBody);
    if (!TestTrue(TEXT("Byte-exact selected source relocates custody without saving or replacing creator edits"),
            Resumed.IsValid() && Resumed.Asset == World.Get() && !Resumed.Saved && !Resumed.Created &&
                World->GetOutermost()->IsDirty() && IsValid(Added) && Added->GetActorLocation() == Location &&
                Baseline == Meta->GetValue(World.Get(), TEXT("P4NativeLevelBaseline")) &&
                ImportedLines == Meta->GetValue(World.Get(), TEXT("P4NativeLevelImported"))))
    {
        AddError(Resumed.ErrorDetail);
        return false;
    }
    FP4OriginalRecord After;
    if (!TestTrue(TEXT("Source and comparison digests remain exactly the old baseline"),
            P4ReadOriginalRecord(World.Get(), After, Error) && After.SourceDigest == Before.SourceDigest &&
                After.ComparisonDigest == Before.ComparisonDigest &&
                After.ComparisonSchema == Before.ComparisonSchema && After.Fidelity == Before.Fidelity &&
                After.Values.Num() == Before.Values.Num() &&
                P4ReadGameBackedLevelCustody(World.Get(), Omitted, Digest, Error) &&
                P4ExactNativeString(Digest, SourceHash)))
        return false;
    const auto Changes = P4AnalyzeOriginalLevelChanges(World.Get());
    if (!TestTrue(TEXT("Normal future change analysis sees the creator addition without a global witness"),
            Changes.IsValid() && Changes.Added.Num() == 1 && Changes.Added.Contains(Added->GetName()) &&
                Changes.Removed.Num() == 0))
    {
        AddError(Changes.ErrorDetail);
        return false;
    }
    const FString NewCustody = Meta->GetValue(World.Get(), TEXT("P4GameBackedLevelCustody")),
                  NewRecord = Meta->GetValue(World.Get(), TEXT("P4OriginalRecord"));
    TSharedPtr<FJsonObject> BeforeCustody, AfterCustody;
    FString NewCustodyHash;
    if (!TestTrue(TEXT("Only the exact source location changes in actor custody"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OldCustody), BeforeCustody) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NewCustody), AfterCustody) && BeforeCustody &&
                AfterCustody &&
                FPaths::IsSamePath(
                    P4ResolveSourceCustodyPath(AfterCustody->GetStringField(TEXT("source_header"))), OriginalHeader) &&
                FPaths::IsSamePath(
                    P4ResolveSourceCustodyPath(AfterCustody->GetStringField(TEXT("source_body"))), OriginalBody) &&
                BeforeCustody->GetStringField(TEXT("source_sha256")) ==
                    AfterCustody->GetStringField(TEXT("source_sha256")) &&
                BeforeCustody->GetStringField(TEXT("reason")) == AfterCustody->GetStringField(TEXT("reason")) &&
                P4HashUtf8(NewCustody, NewCustodyHash)))
        return false;
    const auto& BeforeActors = BeforeCustody->GetArrayField(TEXT("actors"));
    const auto& AfterActors = AfterCustody->GetArrayField(TEXT("actors"));
    if (!TestEqual(
            TEXT("No original actor is added to or removed from custody"), BeforeActors.Num(), AfterActors.Num()))
        return false;
    for (int32 I = 0; I < BeforeActors.Num(); ++I)
    {
        const auto A = BeforeActors[I]->AsObject(), Z = AfterActors[I]->AsObject();
        if (!TestTrue(TEXT("Every original actor identity and editable status remains exact"),
                A->GetStringField(TEXT("name")) == Z->GetStringField(TEXT("name")) &&
                    A->GetStringField(TEXT("class")) == Z->GetStringField(TEXT("class")) &&
                    A->GetNumberField(TEXT("source_export")) == Z->GetNumberField(TEXT("source_export")) &&
                    A->GetBoolField(TEXT("editable")) == Z->GetBoolField(TEXT("editable"))))
            return false;
    }
    for (int32 I = 0; I < Before.Values.Num(); ++I)
    {
        const auto& A = Before.Values[I];
        const auto& Z = After.Values[I];
        if (!TestTrue(TEXT("Only the custody binding hash changes in original value notes"),
                A.Kind == Z.Kind && A.Object == Z.Object && A.Property == Z.Property && A.Action == Z.Action &&
                    (A.Kind == TEXT("game-backed-custody") ? Z.GameValue == NewCustodyHash
                                                           : A.GameValue == Z.GameValue)))
            return false;
    }
    const auto Again = P4ResumeOriginalLevelNative(World.Get(), OriginalHeader, OriginalBody);
    FString CurrentSaved;
    int64 N = 0;
    if (!TestTrue(TEXT("Same-path resume is metadata no-op and leaves old saved bytes intact"),
            Again.IsValid() && !Again.Saved &&
                NewCustody == Meta->GetValue(World.Get(), TEXT("P4GameBackedLevelCustody")) &&
                NewRecord == Meta->GetValue(World.Get(), TEXT("P4OriginalRecord")) && SavedLease->Current() &&
                P4HashFile(SavedFile, 64 * 1024 * 1024, N, CurrentSaved) && N == SavedSize &&
                P4ExactNativeString(CurrentSaved, SavedHash)))
        return false;
    Proof.Json->SetStringField(TEXT("source_sha256"), SourceHash);
    Proof.Json->SetStringField(TEXT("package"), Package);
    Proof.Json->SetBoolField(TEXT("no_implicit_save"), true);
    Proof.Json->SetBoolField(TEXT("creator_edits_and_baseline_preserved"), true);
    Proof.Json->SetStringField(TEXT("scope"),
        TEXT(
            "One actual source-backed projection custody relocation/refusal; no complete original actor/native map or renderer claim."));
    Proof.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Proof.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    return !HasAnyErrors();
}
#endif

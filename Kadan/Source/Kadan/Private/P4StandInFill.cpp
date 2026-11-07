#include "P4StandInFill.h"
#include "P4SkinnedContent.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "MeshDescription.h"
#include "RawMesh.h"
#include "RenderingThread.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
void TraceStandIn(const TCHAR* Step, const UObject* Object)
{
    if (!FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TRACE_STANDIN")).IsEmpty())
        UE_LOG(LogTemp, Display, TEXT("P4StandInFill %s: %s"), Step, Object ? *Object->GetPathName() : TEXT("none"));
}
} //namespace

FP4StandInFill::FP4StandInFill(UObject* Asset) : Target(Asset)
{
    if (!IsValid(Target))
    {
        TraceStandIn(TEXT("preflight invalid target"), Target);
        return;
    }
    if (!P4CanRestoreSkinnedPlaceholder(Target))
    {
        TraceStandIn(TEXT("preflight unsupported skeletal source"), Target);
        return;
    }
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(Target))
    {
        for (int32 LOD = 0; LOD < Mesh->GetNumSourceModels(); ++LOD)
        {
            const FMeshDescription* Description = Mesh->GetMeshDescription(LOD);
            // Generated LODs without an editable description need a separate qualified backup.
            if (!Description)
            {
                TraceStandIn(TEXT("preflight missing static mesh description"), Target);
                return;
            }
            FStaticSourceSnapshot Saved;
            Saved.Description = MakeUnique<FMeshDescription>(*Description);
            FStaticMeshSourceModel& Model = Mesh->GetSourceModel(LOD);
            if (!Model.RawMeshBulkData)
            {
                TraceStandIn(TEXT("preflight missing static raw bulk"), Target);
                return;
            }
            FMemoryWriter RawWriter(Saved.RawBulkData, true);
            Model.RawMeshBulkData->Serialize(RawWriter, Mesh);
            if (RawWriter.IsError())
            {
                TraceStandIn(TEXT("preflight static raw bulk archive error"), Target);
                return;
            }
            Saved.BuildSettings = Model.BuildSettings;
            Saved.ReductionSettings = Model.ReductionSettings;
            Saved.ScreenSize = Model.ScreenSize;
            Saved.LODDistance = Model.LODDistance_DEPRECATED;
            Saved.SourceImportFilename = Model.SourceImportFilename;
            Saved.ImportWithBaseMesh = Model.bImportWithBaseMesh;
            if (!Model.MeshDescriptionBulkData)
            {
                TraceStandIn(TEXT("preflight missing static bulk source"), Target);
                return;
            }
            FMemoryWriter BulkWriter(Saved.BulkData, true);
            Model.MeshDescriptionBulkData->Serialize(BulkWriter, Mesh);
            if (BulkWriter.IsError())
            {
                TraceStandIn(TEXT("preflight static bulk archive error"), Target);
                return;
            }
            StaticSources.Add(MoveTemp(Saved));
        }
    }
    WasDirty = Target->GetOutermost()->IsDirty();
    if (UTexture* Texture = Cast<UTexture>(Target))
    {
        FTexturePlatformData** Running = Texture->GetRunningPlatformData();
        if (!Running)
        {
            TraceStandIn(TEXT("preflight unsupported texture running data"), Target);
            return;
        }
        HadTexturePlatformData = *Running != nullptr;
        HadTextureResource = Texture->Resource != nullptr;
        if (HadTextureResource && !HadTexturePlatformData)
        {
            TraceStandIn(TEXT("preflight texture resource without reconstructible platform data"), Target);
            return;
        }
        if (HadTexturePlatformData && !Texture->Source.IsValid())
        {
            TraceStandIn(TEXT("preflight texture platform data without reconstructible source"), Target);
            return;
        }
        // Finishing is needed only for a cache that already exists. The engine
        // may create an empty cache when asked to finish a valid uncached source.
        if (HadTexturePlatformData)
            Texture->FinishCachePlatformData();
    }
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Target, Objects, true);
    Objects.Insert(Target, 0);
    for (UObject* Object : Objects)
    {
        FSnapshot Snapshot;
        Snapshot.Object = Object;
        // A delta archive omits original null/default values and would leave
        // those fields with the failed fill's values when read back in place.
        FObjectWriter Writer(Object, Snapshot.Bytes, false, false, false);
        if (Writer.IsError())
        {
            TraceStandIn(TEXT("preflight object archive error"), Object);
            return;
        }
        Snapshots.Add(MoveTemp(Snapshot));
    }
    for (const TCHAR* Key : {TEXT("P4OriginalRecord"), TEXT("P4BlueprintStandIn"), TEXT("P4NativeTextureBaseline"),
             TEXT("P4NativeMeshBaseline"), TEXT("P4NativeObjectBaseline"), TEXT("P4NativeObjectValuesNotHeld")})
        Metadata.Add(FName(Key), Target->GetOutermost()->GetMetaData()->GetValue(Target, Key));
    Ready = true;
    if (UTexture* Texture = Cast<UTexture>(Target))
    {
        // CachePlatformData will not rebuild an emptied archive-restored cache
        // whose key already matches the restored source. Retain the actual old
        // native cache instead, isolated from every operation of the failed fill.
        Texture->ReleaseResource();
        FlushRenderingCommands();
        FTexturePlatformData** Running = Texture->GetRunningPlatformData();
        TexturePlatformBackup = *Running;
        *Running = nullptr;
        TraceStandIn(TEXT("detached original texture cache"), Target);
    }
}

FP4StandInFill::~FP4StandInFill()
{
    if (!Ready || Committed || !IsValid(Target))
    {
        delete TexturePlatformBackup;
        return;
    }
    TraceStandIn(TEXT("release resources"), Target);
    if (UTexture* Texture = Cast<UTexture>(Target))
    {
        Texture->FinishCachePlatformData();
        Texture->ReleaseResource();
    }
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(Target))
        Mesh->ReleaseResources();
    if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Target))
        Mesh->ReleaseResources();
    FlushRenderingCommands();
    TArray<UObject*> Now;
    GetObjectsWithOuter(Target, Now, true);
    for (UObject* Object : Now)
        if (!Snapshots.ContainsByPredicate([&](const FSnapshot& Saved) { return Saved.Object == Object; }))
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->MarkPendingKill();
        }
    for (FSnapshot& Snapshot : Snapshots)
    {
        TraceStandIn(TEXT("read native snapshot"), Snapshot.Object);
        FObjectReader Reader(Snapshot.Object, Snapshot.Bytes);
        TraceStandIn(TEXT("native snapshot read"), Snapshot.Object);
    }
    TraceStandIn(TEXT("restore placeholder renderer"), Target);
    P4RestoreSkinnedPlaceholderRender(Target);
    UMetaData* Meta = Target->GetOutermost()->GetMetaData();
    for (const auto& Entry : Metadata)
        if (Entry.Value.IsEmpty())
            Meta->RemoveValue(Target, Entry.Key);
        else
            Meta->SetValue(Target, Entry.Key, *Entry.Value);
    if (UTexture* Texture = Cast<UTexture>(Target))
    {
        // The replacement's cache task/resource was finished/released above.
        // ObjectReader can empty its mips without changing its derived key; discard
        // that cache and put back the exact original native cache we held aside.
        FTexturePlatformData** Running = Texture->GetRunningPlatformData();
        if (*Running != TexturePlatformBackup)
            delete *Running;
        *Running = TexturePlatformBackup;
        TexturePlatformBackup = nullptr;
        if (HadTextureResource)
            Texture->UpdateResource();
        TraceStandIn(TEXT("restored original texture cache"), Target);
    }
    // Static source bulk data is not retained by the object archive. Restore
    // native mesh descriptions explicitly, then rebuild derived render buffers.
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(Target))
    {
        Mesh->SetNumSourceModels(StaticSources.Num());
        for (int32 LOD = 0; LOD < StaticSources.Num(); ++LOD)
        {
            FStaticMeshSourceModel& Model = Mesh->GetSourceModel(LOD);
            Model.BuildSettings = StaticSources[LOD].BuildSettings;
            Model.ReductionSettings = StaticSources[LOD].ReductionSettings;
            Model.ScreenSize = StaticSources[LOD].ScreenSize;
            Model.LODDistance_DEPRECATED = StaticSources[LOD].LODDistance;
            Model.SourceImportFilename = StaticSources[LOD].SourceImportFilename;
            Model.bImportWithBaseMesh = StaticSources[LOD].ImportWithBaseMesh;
            Mesh->CreateMeshDescription(LOD, MoveTemp(*StaticSources[LOD].Description));
            Mesh->CommitMeshDescription(LOD);
            // Restore the engine's own bulk archive, including its original
            // GUID/custom versions; committing geometry alone creates new identity.
            FMemoryReader BulkReader(StaticSources[LOD].BulkData, true);
            Model.MeshDescriptionBulkData->Serialize(BulkReader, Mesh);
            FMemoryReader RawReader(StaticSources[LOD].RawBulkData, true);
            Model.RawMeshBulkData->Serialize(RawReader, Mesh);
        }
        TraceStandIn(TEXT("rebuild static render data"), Target);
        Mesh->Build(true);
    }
    if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Target))
        Mesh->InitResources();
    Target->GetOutermost()->SetDirtyFlag(WasDirty);
    TraceStandIn(TEXT("rollback complete"), Target);
}

void FP4StandInFill::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(Target);
    for (FSnapshot& Snapshot : Snapshots)
        Collector.AddReferencedObject(Snapshot.Object);
}

#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"

struct FTexturePlatformData;

// A verified placeholder may be filled only while this guard owns its original native editor state.
// Disk rollback belongs to P4SaveAssetPackage; this guard preserves existing UObject identities.
class KADAN_API FP4StandInFill : public FGCObject
{
public:
    explicit FP4StandInFill(UObject* Asset);
    ~FP4StandInFill();
    FP4StandInFill(const FP4StandInFill&) = delete;
    FP4StandInFill& operator=(const FP4StandInFill&) = delete;
    bool IsReady() const { return Ready; }
    bool OwnsTarget(const UObject* Asset) const { return Ready && !Committed && Target == Asset; }
    void Commit() { Committed = true; }
    void AddReferencedObjects(FReferenceCollector& Collector) override;
    FString GetReferencerName() const override { return TEXT("FP4StandInFill"); }

private:
    struct FSnapshot
    {
        UObject* Object = nullptr;
        TArray<uint8> Bytes;
    };
    struct FStaticSourceSnapshot
    {
        TUniquePtr<FMeshDescription> Description;
        TArray<uint8> BulkData, RawBulkData;
        FMeshBuildSettings BuildSettings;
        FMeshReductionSettings ReductionSettings;
        FPerPlatformFloat ScreenSize;
        float LODDistance = 0;
        FString SourceImportFilename;
        bool ImportWithBaseMesh = false;
    };
    UObject* Target = nullptr;
    TArray<FSnapshot> Snapshots;
    TMap<FName, FString> Metadata;
    TArray<FStaticSourceSnapshot> StaticSources;
    bool Ready = false, Committed = false, WasDirty = false;
    bool HadTexturePlatformData = false;
    bool HadTextureResource = false;
    // The failed fill owns a different running cache. Native mips/key in this
    // detached, finished cache remain intact through source archive restoration.
    FTexturePlatformData* TexturePlatformBackup = nullptr;
};

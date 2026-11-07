#include "P4StandInFill.h"
#include "P4StaticMeshContent.h"
#include "P4TextureContent.h"
#include "P4NativeHash.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "MeshDescription.h"
#include "RawMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "RenderingThread.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
bool TextureCacheDigest(FTexturePlatformData* Cache, FString& Out)
{
    if (!Cache)
        return P4HashUtf8(TEXT("no-running-texture-cache"), Out);
    FString Facts = FString::Printf(TEXT("native-running-texture-cache-v1\t%d\t%d\t%u\t%d\t%d\n"), Cache->SizeX,
        Cache->SizeY, Cache->PackedData, int32(Cache->PixelFormat), Cache->Mips.Num());
    Facts += Cache->DerivedDataKey + TEXT("\n");
    for (FTexture2DMipMap& Mip : Cache->Mips)
    {
        const int64 Bytes = Mip.BulkData.GetBulkDataSize();
        const bool Resident = Mip.BulkData.IsBulkDataLoaded();
        Facts += FString::Printf(TEXT("%d\t%d\t%d\t%lld\t%d\n"), Mip.SizeX, Mip.SizeY, Mip.SizeZ,
            static_cast<long long>(Bytes), int32(Resident));
        Facts += Mip.DerivedDataKey + TEXT("\n");
        // Observe resident bytes only. A diagnostic must not cause a previously
        // nonresident mip to load merely to qualify the cache-preservation test.
        if (Resident && Bytes)
        {
            if (Bytes < 0 || Bytes > 64LL * 1024 * 1024)
                return false;
            const uint8* Data = static_cast<const uint8*>(Mip.BulkData.LockReadOnly());
            FString Hash;
            const bool Read = Data && P4HashSourceBuffers(Data, Bytes, nullptr, 0, Hash);
            Mip.BulkData.Unlock();
            if (!Read)
                return false;
            Facts += Hash + TEXT("\n");
        }
    }
    return P4HashUtf8(Facts, Out);
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StandInTextureRollback, "P4MapKit.OriginalRecord.TextureStandInRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StandInTextureRollback::RunTest(const FString&)
{
    UPackage* Package = CreatePackage(TEXT("/Temp/P4StandInTextureRollback"));
    TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>(Package, TEXT("Texture"), RF_Public | RF_Standalone));
    Package->GetMetaData()->SetValue(Texture.Get(), TEXT("P4BlueprintStandIn"), TEXT("1"));
    UTexture2D* Identity = Texture.Get();
    const uint8 Pixel[4] = {10, 20, 30, 255};
    for (int32 Case = 0; Case != 4; ++Case)
    {
        Texture->FinishCachePlatformData();
        FlushRenderingCommands();
        if (Case >= 2)
        {
            Texture->ReleaseResource();
            FlushRenderingCommands();
        }
        if (Case == 3)
        {
            delete Texture->PlatformData;
            Texture->PlatformData = nullptr;
        }
        const bool HadSource = Texture->Source.IsValid();
        const bool HadPlatformData = Texture->PlatformData != nullptr;
        const bool HadResource = Texture->Resource != nullptr;
        FTexturePlatformData* const OriginalPlatformData = Texture->PlatformData;
        const int32 OriginalMips = Texture->PlatformData ? Texture->PlatformData->Mips.Num() : 0;
        FString OriginalCache;
        if (!TestTrue(TEXT("native original running-cache facts readable without changing mip residency"),
                TextureCacheDigest(Texture->PlatformData, OriginalCache)))
            return false;
        AddInfo(FString::Printf(TEXT("Texture rollback case %d source=%d cache=%d resource=%d mips=%d"), Case,
            int32(HadSource), int32(HadPlatformData), int32(HadResource), OriginalMips));
        const FString Before = HadSource ? P4TextureState(Texture.Get()) : FString();
        {
            FP4StandInFill Guard(Texture.Get());
            if (!TestTrue(TEXT("native snapshot readable"), Guard.IsReady()))
                return false;
            Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, Pixel);
            Texture->CompressionSettings = TC_Normalmap;
            Texture->UpdateResource();
            Package->GetMetaData()->RemoveValue(Texture.Get(), FName(TEXT("P4BlueprintStandIn")));
            Package->MarkPackageDirty();
        }
        TestTrue(TEXT("texture pointer survives refused fill"), Texture.Get() == Identity);
        FlushRenderingCommands();
        TestEqual(TEXT("source presence restored, including blank stand-in"), Texture->Source.IsValid(), HadSource);
        TestEqual(TEXT("running platform data presence restored, including blank stand-in"),
            Texture->PlatformData != nullptr, HadPlatformData);
        TestTrue(TEXT("exact original native running cache ownership restored"),
            Texture->PlatformData == OriginalPlatformData);
        TestEqual(TEXT("native cached mip count restored"),
            Texture->PlatformData ? Texture->PlatformData->Mips.Num() : 0, OriginalMips);
        FString RestoredCache;
        TestTrue(TEXT("original running-cache keys/mip dimensions/resident bytes restored"),
            TextureCacheDigest(Texture->PlatformData, RestoredCache) &&
                P4ExactNativeString(OriginalCache, RestoredCache));
        TestEqual(TEXT("renderer resource presence restored, including blank stand-in"), Texture->Resource != nullptr,
            HadResource);
        TestEqual(TEXT("texture settings restored"), int32(Texture->CompressionSettings), int32(TC_Default));
        if (HadSource)
            TestEqual(TEXT("native source and settings restored"), P4TextureState(Texture.Get()), Before);
        TestEqual(TEXT("stand-in provenance restored"),
            FString(Package->GetMetaData()->GetValue(Texture.Get(), TEXT("P4BlueprintStandIn"))), FString(TEXT("1")));
        TestFalse(TEXT("rollback leaves original clean"), Package->IsDirty());
        Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, Pixel);
        Texture->UpdateResource();
    }
    Texture->ClearFlags(RF_Public | RF_Standalone);
    Texture->MarkPendingKill();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StandInMeshRollback, "P4MapKit.OriginalRecord.MeshStandInRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StandInMeshRollback::RunTest(const FString&)
{
    UPackage* Package = CreatePackage(TEXT("/Temp/P4StandInMeshRollback"));
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(Package, TEXT("Mesh"), RF_Public | RF_Standalone));
    Mesh->AddSourceModel();
    FMeshDescription* Description = Mesh->CreateMeshDescription(0);
    FStaticMeshAttributes Attributes(*Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    const FPolygonGroupID Group = Description->CreatePolygonGroup();
    TArray<FVertexInstanceID> Instances;
    for (const FVector Point : {FVector(0, 0, 0), FVector(0, 10, 0), FVector(10, 0, 0)})
    {
        const FVertexID Vertex = Description->CreateVertex();
        Positions[Vertex] = Point;
        Instances.Add(Description->CreateVertexInstance(Vertex));
    }
    Description->CreatePolygon(Group, Instances);
    Mesh->CommitMeshDescription(0);
    Mesh->StaticMaterials.Add(FStaticMaterial());
    Mesh->Build(true);
    Package->GetMetaData()->SetValue(Mesh.Get(), TEXT("P4BlueprintStandIn"), TEXT("1"));
    Package->SetDirtyFlag(false);
    UStaticMesh* Identity = Mesh.Get();
    const FString Before = P4StaticMeshState(Mesh.Get());
    TestFalse(TEXT("placeholder native state readable"), Before.IsEmpty());
    if (!TestNotNull(TEXT("placeholder render data exists"), Mesh->RenderData.Get()))
        return false;
    const FBoxSphereBounds Bounds = Mesh->RenderData->Bounds;
    const FString BulkIdentity = Mesh->GetSourceModel(0).MeshDescriptionBulkData->GetIdString();
    const FString RawIdentity = Mesh->GetSourceModel(0).RawMeshBulkData->GetIdString();
    {
        FP4StandInFill Guard(Mesh.Get());
        if (!TestTrue(TEXT("native mesh snapshot readable"), Guard.IsReady()))
            return false;
        FStaticMeshAttributes Changed(*Mesh->GetMeshDescription(0));
        Changed.GetVertexPositions()[FVertexID(0)].Z = 99;
        Mesh->CommitMeshDescription(0);
        Mesh->Build(true);
        Package->GetMetaData()->RemoveValue(Mesh.Get(), FName(TEXT("P4BlueprintStandIn")));
        Package->MarkPackageDirty();
    }
    TestTrue(TEXT("mesh pointer survives refused fill"), Mesh.Get() == Identity);
    const bool SourceRestored = TestEqual(TEXT("source model count restored"), Mesh->GetNumSourceModels(), 1) &&
        TestNotNull(TEXT("source mesh description restored"), Mesh->GetMeshDescription(0));
    if (SourceRestored)
        TestEqual(TEXT("source geometry and settings restored"), P4StaticMeshState(Mesh.Get()), Before);
    if (SourceRestored)
        TestEqual(TEXT("native source bulk identity restored"),
            Mesh->GetSourceModel(0).MeshDescriptionBulkData->GetIdString(), BulkIdentity);
    if (SourceRestored)
        TestEqual(TEXT("native raw source bulk identity restored"),
            Mesh->GetSourceModel(0).RawMeshBulkData->GetIdString(), RawIdentity);
    if (TestNotNull(TEXT("derived render data restored"), Mesh->RenderData.Get()))
        TestTrue(TEXT("derived render bounds restored"),
            Mesh->RenderData->Bounds.Origin.Equals(Bounds.Origin) &&
                Mesh->RenderData->Bounds.BoxExtent.Equals(Bounds.BoxExtent));
    TestEqual(TEXT("stand-in provenance restored"),
        FString(Package->GetMetaData()->GetValue(Mesh.Get(), TEXT("P4BlueprintStandIn"))), FString(TEXT("1")));
    TestFalse(TEXT("rollback leaves original clean"), Package->IsDirty());
    Mesh->ClearFlags(RF_Public | RF_Standalone);
    Mesh->MarkPendingKill();
    return true;
}
#endif

#include "P4MeshSourceReconstruction.h"
#include "P4Common.h"
#include "P4NativeHash.h"
#include "P4CreatorSource.h"
#include "P4StaticMeshContent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "StaticMeshAttributes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
const TCHAR* const ReconstructionMetadataKey = TEXT("P4MeshSourceReconstruction");
const TCHAR* const Owner = TEXT("kadan-mesh-source-reconstruction-v1");
FDelegateHandle Loaded;
struct FReconstructionBuildHandles
{
    FDelegateHandle Before, After;
};
TMap<TWeakObjectPtr<UStaticMesh>, FReconstructionBuildHandles> Builds;
TSet<UStaticMesh*> Restoring;
TSet<TWeakObjectPtr<UStaticMesh>> ProvenBuilds;
TSet<UStaticMesh*> RestoringProvenBuild;

FString DescriptionsDigest(UStaticMesh* Mesh)
{
    FString Digest;
    const bool Valid = P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Write)
        {
            for (int32 L = 0; L < Mesh->GetNumSourceModels(); ++L)
            {
                FMeshDescription* D = Mesh->GetMeshDescription(L);
                if (!D)
                    return false;
                TArray<uint8> Bytes;
                FMemoryWriter Writer(Bytes, true);
                FNameAsStringProxyArchive Names(Writer);
                D->Serialize(Names);
                if (Writer.IsError() || Names.IsError())
                    return false;
                const int32 Size = Bytes.Num();
                if (!Write(reinterpret_cast<const uint8*>(&Size), sizeof(Size)))
                    return false;
                for (int32 At = 0; At < Size; At += 65536)
                    if (!Write(Bytes.GetData() + At, FMath::Min(65536, Size - At)))
                        return false;
            }
            return true;
        },
        Digest);
    return Valid ? Digest : FString();
}
void Register(UStaticMesh* Mesh);
void RestoreLoaded(UObject* Object)
{
    UStaticMesh* Mesh = Cast<UStaticMesh>(Object);
    if (!Mesh)
        return;
    if (!P4HasMeshSourceReconstruction(Mesh))
    {
        if (P4RefreshOriginalMeshNavigation(Mesh))
            Register(Mesh);
        return;
    }
    FString Error;
    if (!P4RestoreMeshSourceReconstruction(Mesh, Error))
        UE_LOG(LogTemp, Warning, TEXT("Kadan could not reconstruct %s: %s"), *Mesh->GetPathName(), *Error);
}
void Register(UStaticMesh* Mesh)
{
    if (Builds.Contains(Mesh))
        return;
    for (auto It = Builds.CreateIterator(); It; ++It)
        if (!It.Key().IsValid())
            It.RemoveCurrent();
    FReconstructionBuildHandles Handles;
    Handles.Before = Mesh->OnPreMeshBuild().AddLambda(
        [](UStaticMesh* Building)
        {
            ProvenBuilds.Remove(Building);
            if (Restoring.Contains(Building) || P4HasCreatorSource(Building) ||
                !P4HasMeshSourceReconstruction(Building))
                return;
            TSharedPtr<FJsonObject> J;
            FString Digest, Path;
            double Version = -1;
            if (!FJsonSerializer::Deserialize(
                    TJsonReaderFactory<>::Create(FString(
                        Building->GetOutermost()->GetMetaData()->GetValue(Building, ReconstructionMetadataKey))),
                    J) ||
                !J.IsValid() || !J->TryGetStringField(TEXT("geometry_sha256"), Digest) ||
                !J->TryGetStringField(TEXT("asset_path"), Path) || Path != Building->GetPathName() ||
                !J->TryGetNumberField(TEXT("lightmap_uv_version"), Version) || Version != Building->LightmapUVVersion ||
                DescriptionsDigest(Building) != Digest)
                return;
            if (Building->GetOutermost()->GetMetaData()->HasValue(Building, TEXT("P4NativeMeshBaseline")) &&
                !P4OriginalMeshBaselineUnchanged(Building))
                return;
            ProvenBuilds.Add(Building);
        });
    Handles.After = Mesh->OnPostMeshBuild().AddLambda(
        [](UStaticMesh* Built)
        {
            if (Restoring.Contains(Built))
                return;
            const bool Proven = ProvenBuilds.Remove(Built) != 0;
            if (Proven)
                RestoringProvenBuild.Add(Built);
            RestoreLoaded(Built);
            RestoringProvenBuild.Remove(Built);
        });
    Builds.Add(Mesh, Handles);
}
struct FLOD
{
    bool High = false, Full = false, Color = false, Wide = false;
    float Screen = 0;
    TArray<FMeshSectionInfo> Sections;
};
bool Number(const FJsonObject& J, const TCHAR* Name, int32& Out)
{
    double N = 0;
    if (!J.TryGetNumberField(Name, N) || !FMath::IsFinite(N) || N < 0 || N > 65535 || N != int32(N))
        return false;
    Out = int32(N);
    return true;
}
uint32 FloatBits(float Value)
{
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, 4);
    return Bits;
}
bool ReadFloatBits(const FJsonValue& Value, float& Out)
{
    double Number = 0;
    if (!Value.TryGetNumber(Number) || !FMath::IsFinite(Number) || Number < 0 || Number > MAX_uint32 ||
        Number != uint32(Number))
        return false;
    const uint32 Bits = uint32(Number);
    FMemory::Memcpy(&Out, &Bits, 4);
    return FMath::IsFinite(Out);
}
} //namespace

bool P4HasMeshSourceReconstruction(const UStaticMesh* Mesh)
{
    // GetMetaData creates an object when there is none. Ordinary foreign meshes
    // must not acquire package objects just because our load callback saw them.
    if (!Mesh || !FindObjectFast<UMetaData>(Mesh->GetOutermost(), FName(NAME_PackageMetaData)))
        return false;
    return !FString(Mesh->GetOutermost()->GetMetaData()->GetValue(Mesh, ReconstructionMetadataKey)).IsEmpty();
}

bool P4RecordMeshSourceReconstruction(UStaticMesh* Mesh, const FP4OriginalMeshSource& Source, FString& Error)
{
    if (!IsInGameThread() || !Mesh || Mesh->GetNumSourceModels() != int32(Source.Render.LODs.size()))
    {
        Error = TEXT("mesh-reconstruction-needs-complete-source-models");
        return false;
    }
    const FString Digest = DescriptionsDigest(Mesh);
    if (Digest.IsEmpty())
    {
        Error = TEXT("mesh-reconstruction-source-unreadable");
        return false;
    }
    TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("owner"), Owner);
    J->SetStringField(TEXT("geometry_sha256"), Digest);
    J->SetStringField(TEXT("asset_path"), Mesh->GetPathName());
    J->SetNumberField(TEXT("lightmap_uv_version"), Mesh->LightmapUVVersion);
    TArray<TSharedPtr<FJsonValue>> Bounds, LODs;
    // JSON numbers roundtrip these integer bitwords exactly, including -0.
    for (float V : Source.Render.Bounds)
        Bounds.Add(MakeShared<FJsonValueNumber>(double(FloatBits(V))));
    for (size_t L = 0; L < Source.Render.LODs.size(); ++L)
    {
        const auto& From = Source.Render.LODs[L];
        const auto& B = From.Buffers;
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetBoolField(TEXT("high"), B.HighPrecisionTangents);
        Item->SetBoolField(TEXT("full"), B.FullPrecisionUV);
        Item->SetBoolField(TEXT("color"), !B.ColorRGBA.empty());
        Item->SetBoolField(TEXT("wide"), B.Main.Wide);
        Item->SetNumberField(TEXT("screen_bits"), double(FloatBits(Source.Render.ScreenSizes[L])));
        TArray<TSharedPtr<FJsonValue>> Sections;
        for (const auto& S : From.Sections)
        {
            TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();
            Section->SetNumberField(TEXT("material"), S.MaterialIndex);
            Section->SetBoolField(TEXT("collision"), S.Collision);
            Section->SetBoolField(TEXT("shadow"), S.Shadow);
            Section->SetBoolField(TEXT("opaque"), S.ForceOpaque);
            Section->SetBoolField(TEXT("ray"), S.RayTracing);
            Sections.Add(MakeShared<FJsonValueObject>(Section));
        }
        Item->SetArrayField(TEXT("sections"), Sections);
        LODs.Add(MakeShared<FJsonValueObject>(Item));
    }
    J->SetArrayField(TEXT("bounds_bits"), Bounds);
    J->SetArrayField(TEXT("lods"), LODs);
    FString Text;
    FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
    Mesh->GetOutermost()->GetMetaData()->SetValue(Mesh, ReconstructionMetadataKey, *Text);
    return true;
}

bool P4RestoreMeshSourceReconstruction(UStaticMesh* Mesh, FString& Error)
{
    Error.Reset();
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (!IsInGameThread() || !IsValid(Mesh) || Restoring.Contains(Mesh))
        return Fail(TEXT("mesh-reconstruction-wrong-context"));
    if (P4HasCreatorSource(Mesh))
        return Fail(TEXT("mesh-reconstruction-creator-source"));
    if (!P4HasMeshSourceReconstruction(Mesh))
        return Fail(TEXT("mesh-reconstruction-record-absent"));
    const FString Text = Mesh->GetOutermost()->GetMetaData()->GetValue(Mesh, ReconstructionMetadataKey);
    TSharedPtr<FJsonObject> J;
    FString Label, Digest, Path;
    const TArray<TSharedPtr<FJsonValue>>*LODs = nullptr, *Bounds = nullptr;
    if (!P4ParseJson(Text, J) || !J->TryGetStringField(TEXT("owner"), Label) || Label != Owner ||
        !J->TryGetStringField(TEXT("asset_path"), Path) || Path != Mesh->GetPathName() ||
        !J->TryGetStringField(TEXT("geometry_sha256"), Digest) || Digest.Len() != 64 ||
        !J->TryGetArrayField(TEXT("lods"), LODs) || LODs->Num() < 1 || LODs->Num() > 8 ||
        LODs->Num() != Mesh->GetNumSourceModels() || !J->TryGetArrayField(TEXT("bounds_bits"), Bounds) ||
        Bounds->Num() != 7)
        return Fail(TEXT("mesh-reconstruction-record-invalid"));
    // No mutation of creator/edited geometry, or of an unrelated asset.
    if (DescriptionsDigest(Mesh) != Digest)
        return Fail(TEXT("mesh-reconstruction-source-edited"));
    int32 LightmapVersion = Mesh->LightmapUVVersion;
    const bool LegacySettingsRecord = !J->HasField(TEXT("lightmap_uv_version"));
    if (!LegacySettingsRecord && !Number(*J, TEXT("lightmap_uv_version"), LightmapVersion))
        return Fail(TEXT("mesh-reconstruction-lightmap-version-invalid"));
    if (Mesh->LightmapUVVersion != LightmapVersion && !RestoringProvenBuild.Contains(Mesh))
        return Fail(TEXT("mesh-reconstruction-source-settings-edited"));
    float Box[7];
    for (int32 I = 0; I < 7; ++I)
    {
        if (!ReadFloatBits(*(*Bounds)[I], Box[I]))
            return Fail(TEXT("mesh-reconstruction-bounds-invalid"));
    }
    TArray<FLOD> Settings;
    TArray<const FMeshDescription*> Descriptions;
    for (int32 L = 0; L < LODs->Num(); ++L)
    {
        const auto Item = (*LODs)[L]->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
        FLOD S;
        const TSharedPtr<FJsonValue>* Screen = nullptr;
        if (Item.IsValid())
            Screen = Item->Values.Find(TEXT("screen_bits"));
        if (!Item.IsValid() || !Item->TryGetBoolField(TEXT("high"), S.High) ||
            !Item->TryGetBoolField(TEXT("full"), S.Full) || !Item->TryGetBoolField(TEXT("color"), S.Color) ||
            !Item->TryGetBoolField(TEXT("wide"), S.Wide) || !Screen || !Screen->IsValid() ||
            !ReadFloatBits(**Screen, S.Screen) || !Item->TryGetArrayField(TEXT("sections"), Sections) ||
            Sections->Num() < 1 || Sections->Num() > 1024)
            return Fail(TEXT("mesh-reconstruction-lod-invalid"));
        for (const auto& Value : *Sections)
        {
            auto Section = Value->AsObject();
            FMeshSectionInfo Info;
            if (!Section.IsValid() || !Number(*Section, TEXT("material"), Info.MaterialIndex) ||
                Info.MaterialIndex >= Mesh->StaticMaterials.Num() ||
                !Section->TryGetBoolField(TEXT("collision"), Info.bEnableCollision) ||
                !Section->TryGetBoolField(TEXT("shadow"), Info.bCastShadow) ||
                !Section->TryGetBoolField(TEXT("opaque"), Info.bForceOpaque) ||
                !Section->TryGetBoolField(TEXT("ray"), Info.bVisibleInRayTracing))
                return Fail(TEXT("mesh-reconstruction-section-invalid"));
            S.Sections.Add(Info);
        }
        const FMeshDescription* D = Mesh->GetMeshDescription(L);
        if (!D || D->PolygonGroups().Num() != S.Sections.Num())
            return Fail(TEXT("mesh-reconstruction-sections-differ"));
        FStaticMeshConstAttributes A(*D);
        bool AnyColor = false;
        for (const auto I : D->VertexInstances().GetElementIDs())
            AnyColor |= FLinearColor(A.GetVertexInstanceColors()[I]).ToFColor(true) != FColor::White;
        if (AnyColor && !S.Color)
            return Fail(TEXT("mesh-reconstruction-color-layout-unrepresentable"));
        Settings.Add(MoveTemp(S));
        Descriptions.Add(D);
    }
    Restoring.Add(Mesh);
    struct FScope
    {
        UStaticMesh* Mesh;
        ~FScope() { Restoring.Remove(Mesh); }
    } Scope{Mesh};
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Params.bCommitMeshDescription = false;
    Params.bMarkPackageDirty = false;
    if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params))
        return Fail(TEXT("mesh-reconstruction-fast-build-failed"));
    // BuildFromMeshDescriptions initializes RHIs; wait before changing CPU buffers.
    Mesh->ReleaseResources();
    Mesh->ReleaseResourcesFence.Wait();
    for (int32 L = 0; L < Settings.Num(); ++L)
    {
        auto& R = Mesh->RenderData->LODResources[L];
        const auto& S = Settings[L];
        const FMeshDescription& D = *Descriptions[L];
        FStaticMeshConstAttributes A(D);
        // Staging has one instance per triangle corner, but one vertex per
        // original render vertex. A narrow source can have far more than 64K
        // corners. Restore that original vertex sharing instead of truncating
        // corner IDs or widening the source's index layout.
        const bool Remap = !S.Wide && D.VertexInstances().GetArraySize() > 65536;
        const int32 Count = Remap ? D.Vertices().GetArraySize() : D.VertexInstances().GetArraySize();
        TArray<FStaticMeshBuildVertex> Vertices;
        Vertices.SetNum(Count);
        TArray<float> Signs;
        Signs.Init(0.f, Count);
        TArray<bool> Seen;
        Seen.Init(false, Count);
        TArray<int32> VertexForInstance;
        VertexForInstance.Init(INDEX_NONE, D.VertexInstances().GetArraySize());
        for (const auto I : D.VertexInstances().GetElementIDs())
        {
            const int32 At = Remap ? D.GetVertexInstanceVertex(I).GetValue() : I.GetValue();
            if (!Vertices.IsValidIndex(At))
                return Fail(TEXT("mesh-reconstruction-vertex-index-invalid"));
            FStaticMeshBuildVertex V;
            FMemory::Memzero(V);
            V.Position = A.GetVertexPositions()[D.GetVertexInstanceVertex(I)];
            V.TangentX = A.GetVertexInstanceTangents()[I];
            V.TangentZ = A.GetVertexInstanceNormals()[I];
            V.TangentY = FVector::CrossProduct(V.TangentZ, V.TangentX).GetSafeNormal() *
                A.GetVertexInstanceBinormalSigns()[I];
            V.Color = FLinearColor(A.GetVertexInstanceColors()[I]).ToFColor(true);
            for (int32 U = 0; U < A.GetVertexInstanceUVs().GetNumIndices(); ++U)
                V.UVs[U] = A.GetVertexInstanceUVs().Get(I, U);
            const float Sign = A.GetVertexInstanceBinormalSigns()[I];
            if (Seen[At] && (FMemory::Memcmp(&Vertices[At], &V, sizeof(V)) || Signs[At] != Sign))
                return Fail(TEXT("mesh-reconstruction-shared-vertex-attributes-differ"));
            Vertices[At] = V;
            Signs[At] = Sign;
            Seen[At] = true;
            VertexForInstance[I.GetValue()] = At;
        }
        for (bool HasVertex : Seen)
            if (!HasVertex)
                return Fail(TEXT("mesh-reconstruction-vertex-data-absent"));
        if (Remap)
            R.VertexBuffers.PositionVertexBuffer.Init(Vertices);
        auto& B = R.VertexBuffers.StaticMeshVertexBuffer;
        B.SetUseHighPrecisionTangentBasis(S.High);
        B.SetUseFullPrecisionUVs(S.Full);
        B.Init(Vertices, A.GetVertexInstanceUVs().GetNumIndices());
        // The engine derives determinant sign from XYZ. A zero/parallel frame
        // has no determinant, so preserve its separately stored source sign.
        for (int32 I = 0; I < Vertices.Num(); ++I)
        {
            const float Sign = Signs[I];
            if (S.High)
                static_cast<int16*>(B.GetTangentData())[I * 8 + 7] = Sign < 0 ? -MAX_int16 : MAX_int16;
            else
                static_cast<int8*>(B.GetTangentData())[I * 8 + 7] = Sign < 0 ? -MAX_int8 : MAX_int8;
        }
        bool AnyColor = false;
        for (const auto& V : Vertices)
            AnyColor |= V.Color != FColor::White;
        if (S.Color && !AnyColor)
            R.VertexBuffers.ColorVertexBuffer.InitFromSingleColor(FColor::White, Vertices.Num());
        else
            R.VertexBuffers.ColorVertexBuffer.Init(Vertices); // stock Init drops only all-white buffers
        const auto RewriteIndices = [&](FRawStaticIndexBuffer& Buffer)
        {
            TArray<uint32> Indices;
            for (int32 I = 0; I < Buffer.GetNumIndices(); ++I)
            {
                uint32 Index = Buffer.GetIndex(I);
                if (Remap)
                {
                    if (!VertexForInstance.IsValidIndex(Index) || VertexForInstance[Index] < 0)
                        return false;
                    Index = uint32(VertexForInstance[Index]);
                }
                if (!S.Wide && Index > 65535)
                    return false;
                Indices.Add(Index);
            }
            Buffer.SetIndices(Indices, S.Wide ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit);
            return true;
        };
        if (!RewriteIndices(R.IndexBuffer))
            return Fail(TEXT("mesh-reconstruction-index-width-unrepresentable"));
        if (Remap)
        {
            if (!RewriteIndices(R.DepthOnlyIndexBuffer))
                return Fail(TEXT("mesh-reconstruction-depth-indices-invalid"));
            if (R.AdditionalIndexBuffers)
            {
                auto& Extra = *R.AdditionalIndexBuffers;
                if (!RewriteIndices(Extra.ReversedIndexBuffer) || !RewriteIndices(Extra.ReversedDepthOnlyIndexBuffer) ||
                    !RewriteIndices(Extra.WireframeIndexBuffer) || !RewriteIndices(Extra.AdjacencyIndexBuffer))
                    return Fail(TEXT("mesh-reconstruction-additional-indices-invalid"));
            }
            for (auto& Section : R.Sections)
            {
                Section.MinVertexIndex = Section.NumTriangles ? MAX_uint32 : 0;
                Section.MaxVertexIndex = 0;
                for (uint32 At = Section.FirstIndex; At < Section.FirstIndex + Section.NumTriangles * 3; ++At)
                {
                    const uint32 Index = R.IndexBuffer.GetIndex(At);
                    Section.MinVertexIndex = FMath::Min(Section.MinVertexIndex, Index);
                    Section.MaxVertexIndex = FMath::Max(Section.MaxVertexIndex, Index);
                }
            }
        }
        for (int32 I = 0; I < S.Sections.Num(); ++I)
        {
            auto& Section = R.Sections[I];
            const auto& From = S.Sections[I];
            Section.MaterialIndex = From.MaterialIndex;
            Section.bEnableCollision = From.bEnableCollision;
            Section.bCastShadow = From.bCastShadow;
            Section.bForceOpaque = From.bForceOpaque;
            Section.bVisibleInRayTracing = From.bVisibleInRayTracing;
            Mesh->GetSectionInfoMap().Set(L, I, From);
            Mesh->GetOriginalSectionInfoMap().Set(L, I, From);
        }
        Mesh->RenderData->ScreenSize[L].Default = S.Screen;
    }
    Mesh->RenderData->Bounds = FBoxSphereBounds(
        FVector(Box[0], Box[1], Box[2]), FVector(Box[3], Box[4], Box[5]), Box[6]);
    // Stock UStaticMesh::BuildInternal unconditionally overwrites this saved
    // setting with Latest. We preserve the independently proven source version;
    // a caller's setting edit is never authorized by a rebuild notification.
    Mesh->LightmapUVVersion = LightmapVersion;
    Mesh->CalculateExtendedBounds();
    Mesh->InitResources();
    if (LegacySettingsRecord)
    {
        // The already-saved validation fixture predates this one tiny setting.
        // Infer it only after its FULL original semantic baseline still matches;
        // never update or replace that baseline to manufacture equality.
        if (!P4OriginalMeshBaselineUnchanged(Mesh))
            return Fail(TEXT("mesh-reconstruction-settings-record-needs-original-validation"));
        J->SetNumberField(TEXT("lightmap_uv_version"), LightmapVersion);
        FString Updated;
        FJsonSerializer::Serialize(J.ToSharedRef(), TJsonWriterFactory<>::Create(&Updated));
        Mesh->GetOutermost()->GetMetaData()->SetValue(Mesh, ReconstructionMetadataKey, *Updated);
    }
    // The fast builder invalidates BodySetup before the exact source maps are
    // restored. Published original meshes use the same guarded cache refresh
    // as the ordinary build path; first imports refresh after their records exist.
    P4RefreshOriginalMeshNavigation(Mesh);
    Register(Mesh);
    return true;
}

void P4StartMeshSourceReconstruction()
{
    if (!Loaded.IsValid())
        Loaded = FCoreUObjectDelegates::OnAssetLoaded.AddStatic(&RestoreLoaded);
}
void P4StopMeshSourceReconstruction()
{
    FCoreUObjectDelegates::OnAssetLoaded.Remove(Loaded);
    Loaded.Reset();
    for (const auto& Pair : Builds)
        if (Pair.Key.IsValid())
        {
            Pair.Key->OnPreMeshBuild().Remove(Pair.Value.Before);
            Pair.Key->OnPostMeshBuild().Remove(Pair.Value.After);
        }
    Builds.Reset();
    Restoring.Reset();
    ProvenBuilds.Reset();
    RestoringProvenBuild.Reset();
}

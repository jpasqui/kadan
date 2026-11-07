#include "P4SkinnedContent.h"
#include "P4Common.h"
#include "P4NativeHash.h"
#include "P4OriginalAnimationData.h"
#include "P4OriginalRecord.h"
#include "P4MapKitCore/skinned_native.h"
#include "P4MapKitCore/animation_asset_native.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/SmartName.h"
#include "AnimationUtils.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Misc/MemStack.h"
#include "ReferenceSkeleton.h"
#include "BoneContainer.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace P4SkinnedContent
{
using namespace P4MapKit;
const TCHAR* const SkeletonClass = TEXT("/Script/Engine.Skeleton");
const TCHAR* const MeshClass = TEXT("/Script/Engine.SkeletalMesh");
const TCHAR* const SequenceClass = TEXT("/Script/Engine.AnimSequence");
const TCHAR* const MontageClass = TEXT("/Script/Engine.AnimMontage");
// What the game's files are converted with writes for a reference to a package the game does not ship.
const TCHAR* const Unshipped = TEXT("/Engine/UnknownPackage.UnknownExport");
// The largest difference between a pose of the game's keys and the pose of a cook's keys that is
// accepted: the engine compresses the decoded poses again, which does not give the game's keys back.
const float TranslationTolerance = 0.05f, RotationToleranceDegrees = 0.1f, ScaleTolerance = 0.001f,
            CurveTolerance = 0.001f;

FString Text(const std::string& In)
{
    return FString(UTF8_TO_TCHAR(In.c_str()));
}
FName Name(const std::string& In)
{
    return FName(*Text(In));
}
FString Refusal(const TCHAR* What, const FReadError& Error)
{
    return FString::Printf(TEXT("%s: %s at byte %d of the objects"), What, *Text(Error.Code), int32(Error.Offset));
}
FTransform Place(const FSkinnedPose& P)
{
    return FTransform(FQuat(P[0], P[1], P[2], P[3]), FVector(P[4], P[5], P[6]), FVector(P[7], P[8], P[9]));
}
void AddOriginalBone(FReferenceSkeletonModifier& Modifier, const FSkinnedBone& Bone)
{
    const int32 Index = Modifier.GetRefBoneInfo().Num();
    const FTransform Pose = Place(Bone.Pose);
    Modifier.Add(FMeshBoneInfo(Name(Bone.Name), Text(Bone.Name), Bone.Parent), Pose);
    // Add normalizes rotation, even when loading an already-cooked reference pose. This changes
    // valid source quaternion words (for example Mar's RF_j003_eye). Restore the exact native pose
    // through the modifier's supported assignment API; its destructor rebuilds virtual/final bones.
    Modifier.UpdateRefPoseTransform(Index, Pose);
}
FGuid Id(const std::array<std::uint32_t, 4>& Value)
{
    return FGuid(Value[0], Value[1], Value[2], Value[3]);
}

FString Digest(const std::vector<std::uint8_t>& Bytes)
{
    FString Out;
    const bool Good = P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Write)
        {
            const std::size_t Step = 64 * 1024;
            for (std::size_t At = 0; At < Bytes.size(); At += Step)
                if (!Write(Bytes.data() + At, int32(FMath::Min(Step, Bytes.size() - At))))
                    return false;
            return true;
        },
        Out);
    return Good ? Out : FString(TEXT("unreadable"));
}
void Say(const std::vector<FNativeLine>& In, TArray<FString>& Lines, TArray<FString>& Made)
{
    for (const FNativeLine& Line : In)
    {
        FString Said = Text(Line.Text);
        if (Line.Blob)
            Said += FString::Printf(TEXT(" = %d bytes, sha256 %s"), int32(Line.Blob->size()), *Digest(*Line.Blob));
        (Line.Made ? Made : Lines).Add(MoveTemp(Said));
    }
}

// The members of a skeleton and of a sequence that the engine keeps to itself and that are not
// properties. A file's own values of them are what makes the asset the game's.
struct FSkeletonDoor : USkeleton
{
    static FReferenceSkeleton& Reference(USkeleton* S) { return S->*(&FSkeletonDoor::ReferenceSkeleton); }
    static FGuid& GuidOf(USkeleton* S) { return S->*(&FSkeletonDoor::Guid); }
    static FSmartNameContainer& Names(USkeleton* S) { return S->*(&FSkeletonDoor::SmartNames); }
    static FSmartNameMapping*& CurveMapping(USkeleton* S) { return S->*(&FSkeletonDoor::AnimCurveMapping); }
    static int32 BoneTreeCount(USkeleton* S) { return (S->*(&FSkeletonDoor::BoneTree)).Num(); }
};
struct FSequenceDoor : UAnimSequence
{
    static TArray<FTrackToSkeletonMap>& Tracks(UAnimSequence* S)
    {
        return S->*(&FSequenceDoor::TrackToSkeletonMapTable);
    }
    static TArray<FRawAnimSequenceTrack>& Raw(UAnimSequence* S) { return S->*(&FSequenceDoor::RawAnimationData); }
    static TArray<FName>& TrackNames(UAnimSequence* S) { return S->*(&FSequenceDoor::AnimationTrackNames); }
};

bool FillSkeleton(USkeleton* Skeleton, const FSkeletonNative& S, FP4SkinnedFillReport& Report, FString& Error)
{
    // The retargeting choice of each bone is a property, one entry a bone, and is on the object already.
    if (FSkeletonDoor::BoneTreeCount(Skeleton) != int32(S.Bones.size()))
    {
        Error = TEXT("skinned-import-skeleton-bone-list-and-bone-settings-differ");
        return false;
    }
    FReferenceSkeleton& Reference = FSkeletonDoor::Reference(Skeleton);
    Reference.Empty();
    {
        FReferenceSkeletonModifier Modifier(Reference, Skeleton);
        for (const FSkinnedBone& Bone : S.Bones)
            AddOriginalBone(Modifier, Bone);
    }
    if (Reference.GetRawBoneNum() != int32(S.Bones.size()))
    {
        Error = TEXT("skinned-import-skeleton-has-two-bones-of-one-name");
        return false;
    }
    Skeleton->AnimRetargetSources.Reset();
    for (const auto& Source : S.RetargetSources)
    {
        FReferencePose Pose;
        Pose.PoseName = Name(Source.PoseName);
        for (const FSkinnedPose& Bone : Source.Pose)
            Pose.ReferencePose.Add(Place(Bone));
        Skeleton->AnimRetargetSources.Add(Name(Source.Name), MoveTemp(Pose));
    }
    FSkeletonDoor::GuidOf(Skeleton) = Id(S.Guid);
    // The curve names, in the file's order: a cook writes them in the order they were loaded in.
    FSmartNameContainer& Names = FSkeletonDoor::Names(Skeleton);
    int32 Curves = 0;
    for (const auto& Mapping : S.SmartNames)
    {
        FSmartNameMapping* Made = Names.AddContainer(Name(Mapping.Name));
        for (const auto& Curve : Mapping.Curves)
        {
            const FName Called = Name(Curve.Name);
            Made->AddName(Called);
            FCurveMetaData* Data = Made->GetCurveMetaData(Called);
            if (!Data)
            {
                Error = TEXT("skinned-import-skeleton-curve-name-not-kept: ") + Text(Curve.Name);
                return false;
            }
            Data->Type.bMaterial = Curve.Material;
            Data->Type.bMorphtarget = Curve.MorphTarget;
            Data->MaxLOD = Curve.MaxLOD;
            for (const std::string& Bone : Curve.LinkedBones)
                Data->LinkedBones.Add(FBoneReference(Name(Bone)));
            ++Curves;
        }
    }
    Names.PostLoad();
    FSkeletonDoor::CurveMapping(Skeleton) = Names.AddContainer(USkeleton::AnimCurveMappingName);
    Skeleton->ClearCacheData();
    Report.Kind = EP4SkinnedFillKind::Skeleton;
    Report.Bones = int64(S.Bones.size());
    Report.CurveNames = Curves;
    Report.RetargetSources = int64(S.RetargetSources.size());
    return true;
}

template <class T>
T At(const FSkinnedBytes& Buffer, std::size_t Offset)
{
    T Value;
    FMemory::Memcpy(&Value, Buffer.Bytes.data() + Offset, sizeof(T));
    return Value;
}

bool FillMesh(USkeletalMesh* Mesh, const FSkeletalMeshNative& M, const TFunction<FString(int32)>& Reference,
    FP4SkinnedFillReport& Report, FString& Error)
{
    if (!Mesh->Skeleton)
    {
        Error = TEXT("skinned-import-mesh-names-no-skeleton");
        return false;
    }
    if (Mesh->GetLODNum() != int32(M.LODs.size()))
    {
        Error = TEXT("skinned-import-mesh-settings-and-levels-of-detail-differ");
        return false;
    }
    // Every bone of the mesh has to be one of its skeleton's.
    for (const FSkinnedBone& Bone : M.Bones)
        if (Mesh->Skeleton->GetReferenceSkeleton().FindBoneIndex(Name(Bone.Name)) == INDEX_NONE)
        {
            Error = TEXT("skinned-import-mesh-bone-not-in-its-skeleton: ") + Text(Bone.Name);
            return false;
        }
    Mesh->Materials.Reset();
    int32 EmptySlots = 0;
    for (const FSkinnedMaterial& Slot : M.Materials)
    {
        const FString Path = Slot.Object ? Reference(Slot.Object) : FString();
        UMaterialInterface* Material = nullptr;
        if (Path.IsEmpty() || P4SameText(Path, Unshipped))
            ++EmptySlots;
        else if (!(Material = Cast<UMaterialInterface>(StaticLoadObject(
                       UMaterialInterface::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))))
        {
            Error = TEXT("skinned-import-material-not-in-the-project: ") + Path;
            return false;
        }
        FSkeletalMaterial Made(
            Material, true, false, Name(Slot.Slot), Name(Slot.HasImportedSlot ? Slot.ImportedSlot : Slot.Slot));
        Made.UVChannelData.bInitialized = Slot.UVInitialized;
        Made.UVChannelData.bOverrideDensities = Slot.UVOverride;
        for (int32 Channel = 0; Channel < 4; ++Channel)
            Made.UVChannelData.LocalUVDensities[Channel] = Slot.UVDensities[Channel];
        Mesh->Materials.Add(Made);
    }
    Mesh->RefSkeleton.Empty();
    {
        FReferenceSkeletonModifier Modifier(Mesh->RefSkeleton, Mesh->Skeleton);
        for (const FSkinnedBone& Bone : M.Bones)
            AddOriginalBone(Modifier, Bone);
    }
    FSkeletalMeshModel* Model = Mesh->GetImportedModel();
    if (!Model)
    {
        Error = TEXT("skinned-import-mesh-has-no-source-model");
        return false;
    }
    Model->LODModels.Empty();
    int32 Vertices = 0, Triangles = 0;
    for (const FSkinnedLOD& L : M.LODs)
    {
        if (L.HighPrecisionTangents || L.MaxBoneInfluences > MAX_TOTAL_INFLUENCES || L.TexCoords > MAX_TEXCOORDS)
        {
            Error = TEXT("skinned-import-vertex-form-not-brought-over-yet");
            return false;
        }
        FSkeletalMeshLODModel* Made = new FSkeletalMeshLODModel();
        Model->LODModels.Add(Made);
        Made->NumVertices = L.Vertices;
        Made->NumTexCoords = L.TexCoords;
        Made->IndexBuffer.Reserve(L.Indices.Count);
        for (uint32 Index = 0; Index < L.Indices.Count; ++Index)
            Made->IndexBuffer.Add(
                L.IndexSize == 2 ? uint32(At<uint16>(L.Indices, Index * 2)) : At<uint32>(L.Indices, Index * 4));
        for (const std::uint16_t Bone : L.ActiveBones)
            Made->ActiveBoneIndices.Add(Bone);
        for (const std::uint16_t Bone : L.RequiredBones)
            Made->RequiredBones.Add(Bone);
        Made->MaxImportVertex = int32(L.Vertices) - 1;
        for (int32 SectionIndex = 0; SectionIndex < int32(L.Sections.size()); ++SectionIndex)
        {
            const FSkinnedSection& S = L.Sections[SectionIndex];
            FSkelMeshSection& Section = Made->Sections.AddDefaulted_GetRef();
            Section.MaterialIndex = S.Material;
            Section.BaseIndex = S.BaseIndex;
            Section.NumTriangles = S.Triangles;
            Section.bRecomputeTangent = S.RecomputeTangent;
            Section.RecomputeTangentsVertexMaskChannel = ESkinVertexColorChannel(S.RecomputeTangentMask);
            Section.bCastShadow = S.CastShadow;
            Section.BaseVertexIndex = S.BaseVertex;
            Section.NumVertices = S.Vertices;
            Section.MaxBoneInfluences = S.MaxBoneInfluences;
            Section.bUse16BitBoneIndex = false;
            Section.CorrespondClothAssetIndex = S.ClothAsset;
            Section.ClothingData.AssetGuid = Id(S.ClothGuid);
            Section.ClothingData.AssetLodIndex = S.ClothLod;
            Section.bDisabled = S.Disabled;
            Section.OriginalDataSectionIndex = SectionIndex;
            Section.ChunkedParentSectionIndex = INDEX_NONE;
            for (const std::uint16_t Bone : S.BoneMap)
                Section.BoneMap.Add(Bone);
            // The vertices that lie where another does: for each vertex how many and from where in the list.
            for (int32 Vertex = 0; Vertex < S.Vertices; ++Vertex)
            {
                const uint32 Count = At<uint32>(S.DuplicateIndex, Vertex * 8),
                             From = At<uint32>(S.DuplicateIndex, Vertex * 8 + 4);
                if (!Count)
                    continue;
                if (uint64(From) + Count > S.DuplicateVertices.Count)
                {
                    Error = TEXT("skinned-import-table-of-shared-places-is-out-of-range");
                    return false;
                }
                TArray<int32>& Others = Section.OverlappingVertices.Add(Vertex);
                for (uint32 Other = 0; Other < Count; ++Other)
                    Others.Add(int32(At<uint32>(S.DuplicateVertices, (From + Other) * 4)));
            }
            Section.SoftVertices.SetNumZeroed(S.Vertices);
            for (int32 Local = 0; Local < S.Vertices; ++Local)
            {
                const std::size_t V = std::size_t(S.BaseVertex) + Local;
                FSoftSkinVertex& Out = Section.SoftVertices[Local];
                Out.Position = FVector(At<float>(L.Positions, V * 12), At<float>(L.Positions, V * 12 + 4),
                    At<float>(L.Positions, V * 12 + 8));
                // A tangent is three signed bytes. The engine packs a vector by its components times 127,
                // so the byte over 127 packs back to the byte. The fourth byte of the normal is the hand
                // of the basis, which the engine works out from the three vectors.
                int8 T[8];
                FMemory::Memcpy(T, L.Tangents.Bytes.data() + V * 8, 8);
                const FVector X(T[0] / 127.f, T[1] / 127.f, T[2] / 127.f), Z(T[4] / 127.f, T[5] / 127.f, T[6] / 127.f);
                Out.TangentX = X;
                Out.TangentZ = FVector4(Z, T[7] < 0 ? -1.f : 1.f);
                Out.TangentY = FVector::CrossProduct(Z, X) * (T[7] < 0 ? -1.f : 1.f);
                for (uint32 Channel = 0; Channel < L.TexCoords; ++Channel)
                {
                    const std::size_t Place = V * L.TexCoords + Channel;
                    if (L.FullPrecisionUVs)
                        Out.UVs[Channel] = FVector2D(At<float>(L.UVs, Place * 8), At<float>(L.UVs, Place * 8 + 4));
                    else
                    {
                        FFloat16 U, W;
                        U.Encoded = At<uint16>(L.UVs, Place * 4);
                        W.Encoded = At<uint16>(L.UVs, Place * 4 + 2);
                        Out.UVs[Channel] = FVector2D(U.GetFloat(), W.GetFloat());
                    }
                }
                Out.Color = L.ColorVertices ? At<FColor>(L.Colors, V * 4) : FColor::White;
                for (uint32 Influence = 0; Influence < L.MaxBoneInfluences; ++Influence)
                {
                    Out.InfluenceBones[Influence] = L.Weights.Bytes[V * L.MaxBoneInfluences * 2 + Influence];
                    Out.InfluenceWeights[Influence] =
                        L.Weights.Bytes[V * L.MaxBoneInfluences * 2 + L.MaxBoneInfluences + Influence];
                }
            }
            Triangles += int32(S.Triangles);
        }
        Vertices += int32(L.Vertices);
        // UE4 synchronizes FROM its source-section user overrides, not into them. Seed these from
        // the recovered sections before the first sync or it silently restores constructor defaults
        // (including shadow=true on Tomato's deliberately non-shadow-casting first section).
        for (const FSkelMeshSection& Section : Made->Sections)
        {
            FSkelMeshSourceSectionUserData& User = Made->UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
            User.bCastShadow = Section.bCastShadow;
            User.bRecomputeTangent = Section.bRecomputeTangent;
            User.RecomputeTangentsVertexMaskChannel = Section.RecomputeTangentsVertexMaskChannel;
            User.bDisabled = Section.bDisabled;
            User.GenerateUpToLodIndex = Section.GenerateUpToLodIndex;
            User.CorrespondClothAssetIndex = Section.CorrespondClothAssetIndex;
            User.ClothingData = Section.ClothingData;
        }
        Made->SyncronizeUserSectionsDataArray();
    }
    Mesh->SetImportedBounds(FBoxSphereBounds(
        FVector(M.Bounds[0], M.Bounds[1], M.Bounds[2]), FVector(M.Bounds[3], M.Bounds[4], M.Bounds[5]), M.Bounds[6]));
    // The engine keeps what it makes of the source model under the model's id.
    Mesh->InvalidateDeriveDataCacheGUID();
    Mesh->CalculateInvRefMatrices();
    Mesh->PostEditChange();
    // PostEditChange recalculates UV densities from the rebuilt geometry. Preserve the cooked
    // artist values, including their exact float bits, after that rebuild; a normal save retains
    // initialized densities. Sync the render copy as well without overriding the source flag.
    for (int32 SlotIndex = 0; SlotIndex < int32(M.Materials.size()); ++SlotIndex)
    {
        const FSkinnedMaterial& Slot = M.Materials[SlotIndex];
        FMeshUVChannelInfo& Data = Mesh->Materials[SlotIndex].UVChannelData;
        Data.bInitialized = Slot.UVInitialized;
        Data.bOverrideDensities = Slot.UVOverride;
        for (int32 Channel = 0; Channel < 4; ++Channel)
            Data.LocalUVDensities[Channel] = Slot.UVDensities[Channel];
    }
    Mesh->UpdateUVChannelData(false);
    if (!Mesh->GetResourceForRendering() ||
        Mesh->GetResourceForRendering()->LODRenderData.Num() != int32(M.LODs.size()))
    {
        Error = TEXT("skinned-import-engine-made-no-render-data");
        return false;
    }
    Report.Kind = EP4SkinnedFillKind::SkeletalMesh;
    Report.LevelsOfDetail = int64(M.LODs.size());
    Report.Vertices = Vertices;
    Report.Triangles = Triangles;
    Report.MaterialSlots = int64(M.Materials.size());
    Report.Bones = int64(M.Bones.size());
    Report.MissingGameMaterials = EmptySlots;
    return true;
}

// A sequence that holds the game's compressed keys and nothing else, for the engine's own decoder.
UAnimSequence* Oracle(UAnimSequence* Like, const FAnimSequenceNative& S, FString& Error)
{
    UAnimSequence* Made = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transient);
    Made->SetSkeleton(Like->GetSkeleton());
    Made->BoneCompressionSettings = Like->BoneCompressionSettings;
    Made->CurveCompressionSettings = Like->CurveCompressionSettings;
    Made->SequenceLength = Like->SequenceLength;
    Made->SetRawNumberOfFrame(S.Frames);
    Made->Interpolation = Like->Interpolation;
    const std::vector<std::uint8_t> Form = AnimSequenceCacheForm(S);
    TArray<uint8> Bytes;
    Bytes.Append(Form.data(), int32(Form.size()));
    FMemoryReader Reader(Bytes, true);
    // As a cooked file has it: without the editor's record of how far the compression is from the raw keys.
    Reader.SetFilterEditorOnly(true);
    Made->SerializeCompressedData(Reader, true);
    if (Reader.IsError() || Reader.Tell() != Bytes.Num() || !Made->CompressedData.CompressedDataStructure.IsValid() ||
        !Made->CompressedData.BoneCompressionCodec ||
        Made->CompressedData.CompressedTrackToSkeletonMapTable.Num() != int32(S.TrackMap.size()))
    {
        Error = TEXT("skinned-import-engine-did-not-take-the-compressed-keys: ") + Text(S.BoneCodec);
        Made->MarkPendingKill();
        return nullptr;
    }
    if (S.CurveNames.size() && !Made->CompressedData.CurveCompressionCodec)
    {
        Error = TEXT("skinned-import-engine-has-no-such-curve-codec: ") + Text(S.CurveCodec);
        Made->MarkPendingKill();
        return nullptr;
    }
    for (FSmartName& Curve : Made->CompressedData.CompressedCurveNames)
        Like->GetSkeleton()->VerifySmartName(USkeleton::AnimCurveMappingName, Curve);
    return Made;
}
float FrameTime(const UAnimSequence* Sequence, int32 Frame, int32 Frames)
{
    return Frames > 1 ? Sequence->SequenceLength * float(Frame) / float(Frames - 1) : 0.f;
}
bool Bits(const FVector& A, const FVector& B)
{
    return FMemory::Memcmp(&A, &B, sizeof(float) * 3) == 0;
}
bool Bits(const FQuat& A, const FQuat& B)
{
    return A.X == B.X && A.Y == B.Y && A.Z == B.Z && A.W == B.W;
}

bool RestoreExtendedAdditive(UAnimSequence* Sequence, UAnimSequence* Game, const FAnimSequenceNative& Source,
    const TArray<int32>& Tracks, TArray<TArray<FTransform>>& Restored, FString& Error)
{
    USkeleton* Skeleton = Sequence->GetSkeleton();
    if ((Sequence->AdditiveAnimType != AAT_LocalSpaceBase &&
            Sequence->AdditiveAnimType != AAT_RotationOffsetMeshSpace) ||
        (Sequence->RefPoseType != ABPT_RefPose && Sequence->RefPoseType != ABPT_AnimFrame &&
            Sequence->RefPoseType != ABPT_AnimScaled) ||
        !Source.CurveNames.empty() || Skeleton->GetVirtualBones().Num())
    {
        Error = TEXT("skinned-import-extended-additive-shape-not-supported");
        return false;
    }
    if (Sequence->RefPoseType != ABPT_RefPose)
    {
        UAnimSequence* Base = Sequence->RefPoseSeq;
        const UP4OriginalAnimationData* Original = Base ? Cast<UP4OriginalAnimationData>(Base->AssetImportData)
                                                        : nullptr;
        FP4OriginalRecord Record;
        FString Why;
        if (!Base || Base == Sequence || Base->GetSkeleton() != Skeleton || Base->AdditiveAnimType != AAT_None ||
            !Original || Original->CacheSuffix != Base->GetDDCCacheKeySuffix(false) ||
            !P4ReadOriginalRecord(Base, Record, Why) || Record.ReferenceOnly || Record.SourceDigest.Len() != 64 ||
            Base->RawCurveData.FloatCurves.Num() || FSequenceDoor::Raw(Base).Num() == 0)
        {
            Error = TEXT("skinned-import-additive-base-must-be-an-unchanged-original-sequence-without-curves");
            return false;
        }
    }
    if (uint64(Tracks.Num()) * uint64(Source.Frames) > 64ull * 1024 * 1024 / sizeof(FTransform))
    {
        Error = TEXT("skinned-import-additive-reconstruction-size-limit");
        return false;
    }
    const int32 Bones = Skeleton->GetReferenceSkeleton().GetNum();
    if (Bones <= 0 || Bones > 65535)
    {
        Error = TEXT("skinned-import-additive-bone-count-invalid");
        return false;
    }
    TArray<FBoneIndexType> Required;
    TArray<int32> Compressed;
    for (int32 Bone = 0; Bone < Bones; ++Bone)
    {
        Required.Add(FBoneIndexType(Bone));
        const auto Found = std::find(Source.TrackMap.begin(), Source.TrackMap.end(), Bone);
        Compressed.Add(Found == Source.TrackMap.end() ? INDEX_NONE : int32(Found - Source.TrackMap.begin()));
    }
    Restored.SetNum(Tracks.Num());
    for (auto& Track : Restored)
        Track.Reserve(Source.Frames);
    FMemMark Mark(FMemStack::Get());
    FBoneContainer Container;
    Container.SetUseRAWData(true);
    Container.InitializeTo(Required, FCurveEvaluationOption(false), *Skeleton);
    FCompactPose Pose, Delta, Base, RoundTrip;
    for (FCompactPose* Each : {&Pose, &Delta, &Base, &RoundTrip})
    {
        Each->SetBoneContainer(&Container);
        Each->ResetToRefPose();
    }
    FBlendedCurve Curve, DeltaCurve;
    Curve.InitFrom(Container);
    DeltaCurve.InitFrom(Container);
    for (int32 Frame = 0; Frame < Source.Frames; ++Frame)
    {
        const float Time = FrameTime(Sequence, Frame, Source.Frames);
        Sequence->GetAdditiveBasePose(Pose, Curve, FAnimExtractContext(Time, false));
        Delta.ResetToAdditiveIdentity();
        for (int32 Bone = 0; Bone < Bones; ++Bone)
        {
            const FCompactPoseBoneIndex Index(Bone);
            Base[Index] = Pose[Index];
            if (Compressed[Bone] != INDEX_NONE)
                Game->GetBoneTransform(Delta[Index], Compressed[Bone], Time, false);
            if (!Source.ScaleOffsets)
                Delta[Index].SetScale3D(FVector::ZeroVector);
        }
        // Use the same engine operations as additive playback and baking. In mesh-space
        // mode this restores parent-dependent rotations before returning them to local space.
        FAnimationRuntime::AccumulateAdditivePose(Pose, Delta, Curve, DeltaCurve, 1.f, Sequence->AdditiveAnimType);
        Pose.NormalizeRotations();
        for (int32 Track = 0; Track < Tracks.Num(); ++Track)
            Restored[Track].Add(Pose[FCompactPoseBoneIndex(Tracks[Track])]);
        for (int32 Bone = 0; Bone < Bones; ++Bone)
            RoundTrip[FCompactPoseBoneIndex(Bone)] = Pose[FCompactPoseBoneIndex(Bone)];
        if (Sequence->AdditiveAnimType == AAT_RotationOffsetMeshSpace)
        {
            FAnimationRuntime::ConvertPoseToMeshRotation(RoundTrip);
            FAnimationRuntime::ConvertPoseToMeshRotation(Base);
        }
        FAnimationRuntime::ConvertPoseToAdditive(RoundTrip, Base);
        for (int32 Bone = 0; Bone < Bones; ++Bone)
        {
            const FTransform& Expected = Delta[FCompactPoseBoneIndex(Bone)];
            const FTransform& Actual = RoundTrip[FCompactPoseBoneIndex(Bone)];
            if (Actual.ContainsNaN() ||
                !Actual.GetTranslation().Equals(Expected.GetTranslation(), TranslationTolerance) ||
                FMath::RadiansToDegrees(Actual.GetRotation().AngularDistance(Expected.GetRotation())) >
                    RotationToleranceDegrees ||
                !Actual.GetScale3D().Equals(Expected.GetScale3D(), ScaleTolerance))
            {
                Error = FString::Printf(
                    TEXT("skinned-import-additive-inverse-roundtrip-differs: frame %d bone %d"), Frame, Bone);
                return false;
            }
        }
    }
    return true;
}

bool FillSequence(UAnimSequence* Sequence, const FAnimSequenceNative& S, FP4SkinnedFillReport& Report, FString& Error)
{
    USkeleton* Skeleton = Sequence->GetSkeleton();
    if (!Skeleton)
    {
        Error = TEXT("skinned-import-sequence-names-no-skeleton");
        return false;
    }
    if (Skeleton->GetGuid() != Id(S.SkeletonGuid))
    {
        Error = TEXT("skinned-import-sequence-is-of-another-skeleton-than-the-project-has");
        return false;
    }
    // Additive cooked keys are deltas against the reference pose or a source animation. Restore
    // editable absolute keys with the engine's inverse, then prove every frame bakes back to
    // the original deltas. The game's compressed stream is retained below as for any sequence.
    const bool Additive = Sequence->AdditiveAnimType != AAT_None;
    const bool ExtendedAdditive = Additive &&
        (Sequence->AdditiveAnimType != AAT_LocalSpaceBase || Sequence->RefPoseType != ABPT_RefPose);
    if (Sequence->GetRawNumberOfFrames() != S.Frames)
    {
        Error = TEXT("skinned-import-sequence-frame-counts-differ");
        return false;
    }
    // Cooked packages omit editor notify tracks. Reconstruct those containers before the
    // editor refreshes the notify cache; the original event/track indices remain unchanged.
    int32 NotifyTracks = 1;
    for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
    {
        if (Notify.TrackIndex < 0 || Notify.TrackIndex > 20)
        {
            Error = TEXT("skinned-import-notify-track-index-out-of-range");
            return false;
        }
        NotifyTracks = FMath::Max(NotifyTracks, Notify.TrackIndex + 1);
    }
    while (Sequence->AnimNotifyTracks.Num() < NotifyTracks)
        Sequence->AnimNotifyTracks.Add(
            FAnimNotifyTrack(FName(*FString::FromInt(Sequence->AnimNotifyTracks.Num() + 1)), FLinearColor::White));
    Sequence->RefreshCacheData();
    // The property importer has already assigned Skeleton directly, whereas SetSkeleton only
    // updates the serialized identity when the pointer changes. Clear the reflected reference
    // first so the engine records the real skeleton's id without remapping the source tracks.
    FObjectProperty* SkeletonProperty = FindFProperty<FObjectProperty>(
        UAnimationAsset::StaticClass(), TEXT("Skeleton"));
    if (!SkeletonProperty)
    {
        Error = TEXT("skinned-import-skeleton-property-not-found");
        return false;
    }
    SkeletonProperty->SetObjectPropertyValue_InContainer(Sequence, nullptr);
    Sequence->SetSkeleton(Skeleton);
    if (!Sequence->BoneCompressionSettings)
        Sequence->BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
    if (!Sequence->CurveCompressionSettings)
        Sequence->CurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
    // The tracks the file names as a property are the raw tracks'. The compressed keys have to be of the same.
    TArray<int32> Bones;
    for (const FTrackToSkeletonMap& Track : FSequenceDoor::Tracks(Sequence))
        Bones.Add(Track.BoneTreeIndex);
    // Which compressed track holds each raw track's keys. They are the same list, except that an
    // additive sequence's compressed keys cover every bone of the skeleton (the engine bakes the
    // additive pose for all of them); its raw tracks are read from their bones' compressed tracks.
    TArray<int32> Compressed;
    if (!Additive && Bones.Num() != int32(S.TrackMap.size()))
    {
        Error = TEXT("skinned-import-sequence-raw-and-compressed-tracks-differ");
        return false;
    }
    for (int32 Track = 0; Track < Bones.Num(); ++Track)
    {
        const auto Found = std::find(S.TrackMap.begin(), S.TrackMap.end(), Bones[Track]);
        const int32 At = Found == S.TrackMap.end() ? INDEX_NONE : int32(Found - S.TrackMap.begin());
        // The engine drops an additive track that is the identity at every frame (the bone's source
        // keys are its reference pose), except the first (BakeOutAdditiveIntoRawData).
        if ((At == INDEX_NONE && !Additive) || (!Additive && At != Track) ||
            !Skeleton->GetReferenceSkeleton().IsValidRawIndex(Bones[Track]))
        {
            Error = TEXT("skinned-import-sequence-raw-and-compressed-tracks-differ");
            return false;
        }
        Compressed.Add(At);
    }
    UAnimSequence* Game = Oracle(Sequence, S, Error);
    if (!Game)
        return false;
    TArray<TArray<FTransform>> Restored;
    if (ExtendedAdditive && !RestoreExtendedAdditive(Sequence, Game, S, Bones, Restored, Error))
    {
        Game->MarkPendingKill();
        return false;
    }
    FSequenceDoor::Tracks(Sequence).Reset();
    FSequenceDoor::Raw(Sequence).Reset();
    FSequenceDoor::TrackNames(Sequence).Reset();
    const bool Scaled = S.ScaleOffsets != 0;
    int64 RawSize = 0;
    int32 Keys = 0;
    for (int32 Track = 0; Track < Bones.Num(); ++Track)
    {
        FRawAnimSequenceTrack Raw;
        for (int32 Frame = 0; Frame < S.Frames; ++Frame)
        {
            FTransform Pose;
            if (ExtendedAdditive)
                Pose = Restored[Track][Frame];
            else if (Compressed[Track] == INDEX_NONE)
                Pose = FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
            else
                Game->GetBoneTransform(Pose, Compressed[Track], FrameTime(Sequence, Frame, S.Frames), false);
            if (Additive && !ExtendedAdditive)
            {
                const FTransform& Base = Skeleton->GetReferenceSkeleton().GetRefBonePose()[Bones[Track]];
                Pose.SetRotation((Pose.GetRotation() * Base.GetRotation()).GetNormalized());
                Pose.SetTranslation(Pose.GetTranslation() + Base.GetTranslation());
                Pose.SetScale3D(
                    Scaled ? (Pose.GetScale3D() + FVector::OneVector) * Base.GetScale3D() : Base.GetScale3D());
            }
            Raw.PosKeys.Add(Pose.GetTranslation());
            Raw.RotKeys.Add(Pose.GetRotation());
            if (Scaled || (Additive && !Pose.GetScale3D().Equals(FVector::OneVector, 0.f)))
                Raw.ScaleKeys.Add(Pose.GetScale3D());
        }
        // A raw channel holds a key a frame, or one key when every frame has the same.
        const auto Single = [&](auto& Values)
        {
            for (int32 Frame = 1; Frame < Values.Num(); ++Frame)
                if (!Bits(Values[Frame], Values[0]))
                    return;
            Values.SetNum(FMath::Min(Values.Num(), 1));
        };
        Single(Raw.PosKeys);
        Single(Raw.RotKeys);
        Single(Raw.ScaleKeys);
        RawSize += int64(Raw.PosKeys.Num()) * sizeof(FVector) + int64(Raw.RotKeys.Num()) * sizeof(FQuat) +
            int64(Raw.ScaleKeys.Num()) * sizeof(FVector);
        Keys += Raw.PosKeys.Num() + Raw.RotKeys.Num() + Raw.ScaleKeys.Num();
        const FName Bone = Skeleton->GetReferenceSkeleton().GetBoneName(Bones[Track]);
        if (Sequence->AddNewRawTrack(Bone, &Raw) != Track)
        {
            Error = TEXT("skinned-import-sequence-track-not-added: ") + Bone.ToString();
            Game->MarkPendingKill();
            return false;
        }
    }
    // The curves, a key a frame with the value the engine's decoder gives for the game's compressed curve.
    Sequence->RawCurveData.FloatCurves.Reset();
    for (const FSmartName& Curve : Game->CompressedData.CompressedCurveNames)
    {
        FSmartName Known;
        if (!Skeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, Curve.DisplayName, Known) ||
            !Sequence->RawCurveData.AddCurveData(Known))
        {
            Error = TEXT("skinned-import-sequence-curve-not-of-its-skeleton: ") + Curve.DisplayName.ToString();
            Game->MarkPendingKill();
            return false;
        }
        FFloatCurve& Made = Sequence->RawCurveData.FloatCurves.Last();
        for (int32 Frame = 0; Frame < S.Frames; ++Frame)
        {
            const float Time = FrameTime(Sequence, Frame, S.Frames);
            Made.FloatCurve.AddKey(Time,
                Game->CompressedData.CurveCompressionCodec->DecompressCurve(Game->CompressedData, Known.UID, Time));
        }
    }
    Sequence->MarkRawDataAsModified(false);
    // Keep the game's exact stream under the native key of these reconstructed source tracks.
    // Native edits change that key and use normal compression; the retained object is stripped
    // by cooking and only lets an unedited editor asset recover from a missing derived cache.
    UP4OriginalAnimationData* Original = NewObject<UP4OriginalAnimationData>(Sequence);
    Original->CacheSuffix = Sequence->GetDDCCacheKeySuffix(false);
    {
        FMemoryWriter Writer(Original->CacheBytes, true);
        Game->SerializeCompressedData(Writer, true);
        if (Writer.IsError())
        {
            Error = TEXT("skinned-import-original-animation-cache-unwritable");
            Game->MarkPendingKill();
            return false;
        }
    }
    // AssetImportData is an editor-only property. AssetUserData would leave a null runtime array
    // element when its editor-only object is stripped, which changes the game's cooked properties.
    Sequence->AssetImportData = Original;
    if (!Original->Seed())
    {
        Error = TEXT("skinned-import-original-animation-cache-not-seeded");
        Game->MarkPendingKill();
        return false;
    }
    Game->MarkPendingKill();
    Sequence->RequestSyncAnimRecompression(false);
    if (!Sequence->IsCompressedDataValid())
    {
        Error = TEXT("skinned-import-engine-did-not-compress-the-sequence");
        return false;
    }
    Report.Kind = EP4SkinnedFillKind::AnimSequence;
    Report.Tracks = Bones.Num();
    Report.Frames = S.Frames;
    Report.RawKeys = Keys;
    Report.Curves = int64(S.CurveNames.size());
    // The game's file says how large its raw keys were. The same number here says the raw channels
    // made have the game's key counts.
    Report.RawBytes = RawSize;
    Report.GameRawBytes = S.RawSize;
    Report.OriginalCompressedStreamRetained = true;
    Report.OriginalCacheSuffix = Original->CacheSuffix;
    return true;
}

bool CompareSequence(UAnimSequence* Sequence, const FAnimSequenceNative& G, const FAnimSequenceNative& M,
    FP4SkinnedCompareReport& Report, FString& Error)
{
    UAnimSequence* Game = Oracle(Sequence, G, Error);
    if (!Game)
        return false;
    UAnimSequence* Mine = Oracle(Sequence, M, Error);
    if (!Mine)
    {
        Game->MarkPendingKill();
        return false;
    }
    float Translation = 0, Rotation = 0, Scale = 0, Curve = 0;
    int32 Poses = 0;
    bool Tracks = G.TrackMap.size() == M.TrackMap.size();
    for (std::size_t Track = 0; Tracks && Track < G.TrackMap.size(); ++Track)
    {
        const auto Found = std::find(M.TrackMap.begin(), M.TrackMap.end(), G.TrackMap[Track]);
        if (Found == M.TrackMap.end())
        {
            Tracks = false;
            break;
        }
        const int32 Other = int32(Found - M.TrackMap.begin());
        for (int32 Frame = 0; Frame < G.Frames; ++Frame)
        {
            const float Time = FrameTime(Sequence, Frame, G.Frames);
            FTransform A, B;
            Game->GetBoneTransform(A, int32(Track), Time, false);
            Mine->GetBoneTransform(B, Other, Time, false);
            Translation = FMath::Max(Translation, FVector::Dist(A.GetTranslation(), B.GetTranslation()));
            // Identical decoded quaternions can have a dot product slightly below one because
            // the codec's floats are not perfectly unit length. That is not an angular difference.
            if (!Bits(A.GetRotation(), B.GetRotation()))
                Rotation = FMath::Max(
                    Rotation, FMath::RadiansToDegrees(A.GetRotation().AngularDistance(B.GetRotation())));
            Scale = FMath::Max(Scale, (A.GetScale3D() - B.GetScale3D()).GetAbsMax());
            ++Poses;
        }
    }
    bool Curves = G.CurveNames == M.CurveNames;
    for (int32 Index = 0; Curves && Index < Game->CompressedData.CompressedCurveNames.Num(); ++Index)
        for (int32 Frame = 0; Frame < G.Frames; ++Frame)
        {
            const float Time = FrameTime(Sequence, Frame, G.Frames);
            const SmartName::UID_Type Id = Game->CompressedData.CompressedCurveNames[Index].UID;
            Curve = FMath::Max(Curve,
                FMath::Abs(Game->CompressedData.CurveCompressionCodec->DecompressCurve(Game->CompressedData, Id, Time) -
                    Mine->CompressedData.CurveCompressionCodec->DecompressCurve(Mine->CompressedData, Id, Time)));
        }
    Game->MarkPendingKill();
    Mine->MarkPendingKill();
    Report.Measured = true;
    Report.PosesCompared = Poses;
    Report.LargestTranslationDifference = Translation;
    Report.LargestRotationDifferenceDegrees = Rotation;
    Report.LargestScaleDifference = Scale;
    Report.LargestCurveDifference = Curve;
    Report.CompressedKeysEqual = G.Stream == M.Stream && G.KeyEncoding == M.KeyEncoding &&
        G.TranslationFormat == M.TranslationFormat && G.RotationFormat == M.RotationFormat &&
        G.ScaleFormat == M.ScaleFormat && G.TrackOffsets == M.TrackOffsets && G.ScaleOffsets == M.ScaleOffsets &&
        G.ScaleStrip == M.ScaleStrip;
    Report.CompressedCurvesEqual = G.CurveBytes == M.CurveBytes;
    Report.BoneCodecEqual = G.BoneCodec == M.BoneCodec;
    Report.RawSizeEqual = G.RawSize == M.RawSize;
    Report.GameBoneCodec = Text(G.BoneCodec);
    Report.MadeBoneCodec = Text(M.BoneCodec);
    Report.GameCompressedBytes = int64(G.Stream.size());
    Report.MadeCompressedBytes = int64(M.Stream.size());
    const UP4OriginalAnimationData* Original = Cast<UP4OriginalAnimationData>(Sequence->AssetImportData);
    if (Original && Sequence->GetDDCCacheKeySuffix(false) == Original->CacheSuffix)
    {
        Report.ExactSourceComparison = true;
        if (!Report.CompressedKeysEqual || !Report.CompressedCurvesEqual || !Report.BoneCodecEqual ||
            !Report.RawSizeEqual)
        {
            Error = TEXT("skinned-compare-original-compressed-data-differs");
            return false;
        }
    }
    if (!Tracks)
    {
        Error = TEXT("skinned-compare-sequence-tracks-differ");
        return false;
    }
    if (!Curves)
    {
        Error = TEXT("skinned-compare-sequence-curves-differ");
        return false;
    }
    if (Translation > TranslationTolerance || Rotation > RotationToleranceDegrees || Scale > ScaleTolerance ||
        Curve > CurveTolerance)
    {
        Error = FString::Printf(
            TEXT(
                "skinned-compare-sequence-poses-differ-by-more-than-accepted: %g cm, %g degrees, %g of scale, %g of a curve"),
            Translation, Rotation, Scale, Curve);
        return false;
    }
    return true;
}
} //namespace P4SkinnedContent

bool P4RestoreSkinnedSource(UObject* Object)
{
    UAnimSequence* Sequence = Cast<UAnimSequence>(Object);
    if (!Sequence)
        return false;
    UP4OriginalAnimationData* Original = Cast<UP4OriginalAnimationData>(Sequence->AssetImportData);
    if (!Original || Sequence->GetDDCCacheKeySuffix(false) != Original->CacheSuffix)
        return false;
    Sequence->WaitOnExistingCompression(false);
    if (!Original->Seed())
        return false;
    Sequence->RequestSyncAnimRecompression(false);
    return Sequence->IsCompressedDataValid();
}

bool P4CanRestoreSkinnedPlaceholder(UObject* Object)
{
    USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object);
    if (!Mesh)
        return true;
    const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
    const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering();
    return Model && Model->LODModels.Num() == 0 && Render && Render->LODRenderData.Num() == 0;
}

void P4RestoreSkinnedPlaceholderRender(UObject* Object)
{
    USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object);
    if (!Mesh)
        return;
    // The caller released resources and flushed rendering before reading the source snapshot.
    // Only the preflight's empty native source/renderer baseline may reach this reset.
    check(Mesh->GetImportedModel() && Mesh->GetImportedModel()->LODModels.Num() == 0);
    Mesh->AllocateResourceForRendering();
}

bool P4SkinnedClass(const FString& ClassPath)
{
    using namespace P4SkinnedContent;
    return P4SameText(ClassPath, SkeletonClass) || P4SameText(ClassPath, MeshClass) ||
        P4SameText(ClassPath, SequenceClass) || P4SameText(ClassPath, MontageClass);
}

bool P4SkinnedNativeLines(const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body,
    std::size_t Start, std::size_t End, const FString& ClassPath, bool VertexColors,
    const TFunction<FString(int32)>& Reference, TArray<FString>& Lines, TArray<FString>& Made, FString& Error)
{
    using namespace P4SkinnedContent;
    FReadError Read;
    if (P4SameText(ClassPath, MontageClass))
    {
        const auto M = ReadAnimationAssetNative(Body, Start, End);
        if (!M.Error.Code.empty())
        {
            Error = Refusal(TEXT("montage-native-data-not-read"), M.Error);
            return false;
        }
        Lines.Add(FString::Printf(TEXT("animation skeleton id %08x %08x %08x %08x"), M.SkeletonGuid[0],
            M.SkeletonGuid[1], M.SkeletonGuid[2], M.SkeletonGuid[3]));
        return true;
    }
    if (P4SameText(ClassPath, SkeletonClass))
    {
        FSkeletonNative S;
        if (!ReadSkeletonNative(Body, Start, End, Package.Names, S, Read))
        {
            Error = Refusal(TEXT("skeleton-native-data-not-read"), Read);
            return false;
        }
        Say(SkeletonLines(S), Lines, Made);
        return true;
    }
    if (P4SameText(ClassPath, MeshClass))
    {
        FSkeletalMeshNative M;
        if (!ReadSkeletalMeshNative(Body, Start, End, Package.Names, VertexColors, M, Read))
        {
            Error = Refusal(TEXT("skinned-mesh-native-data-not-read"), Read);
            return false;
        }
        Say(SkeletalMeshLines(M, [&](std::int32_t Index) { return std::string(TCHAR_TO_UTF8(*Reference(Index))); }),
            Lines, Made);
        return true;
    }
    if (P4SameText(ClassPath, SequenceClass))
    {
        FAnimSequenceNative S;
        if (!ReadAnimSequenceNative(Body, Start, End, Package.Names, S, Read))
        {
            Error = Refusal(TEXT("sequence-native-data-not-read"), Read);
            return false;
        }
        Say(AnimSequenceLines(S), Lines, Made);
        return true;
    }
    Error = TEXT("skinned-class-not-read-here: ") + ClassPath;
    return false;
}

bool P4FillSkinnedNative(UObject* Object, const P4MapKit::FPackageInventory& Package,
    const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const TFunction<FString(int32)>& Reference, FP4SkinnedFillReport& Report, FString& Error)
{
    Report = FP4SkinnedFillReport();
    using namespace P4SkinnedContent;
    FReadError Read;
    if (UAnimMontage* Montage = Cast<UAnimMontage>(Object))
    {
        const auto M = ReadAnimationAssetNative(Body, Start, End);
        if (!M.Error.Code.empty())
        {
            Error = Refusal(TEXT("montage-native-data-not-read"), M.Error);
            return false;
        }
        USkeleton* Skeleton = Montage->GetSkeleton();
        if (!Skeleton || Skeleton->GetGuid() != Id(M.SkeletonGuid))
        {
            Error = TEXT("skinned-import-montage-skeleton-identity-differs");
            return false;
        }
        FObjectProperty* Property = FindFProperty<FObjectProperty>(UAnimationAsset::StaticClass(), TEXT("Skeleton"));
        if (!Property)
        {
            Error = TEXT("skinned-import-skeleton-property-not-found");
            return false;
        }
        Property->SetObjectPropertyValue_InContainer(Montage, nullptr);
        Montage->SetSkeleton(Skeleton);
        int32 TrackCount = 1;
        for (const FAnimNotifyEvent& Notify : Montage->Notifies)
        {
            if (Notify.TrackIndex < 0 || Notify.TrackIndex > 20)
            {
                Error = TEXT("skinned-import-notify-track-index-out-of-range");
                return false;
            }
            TrackCount = FMath::Max(TrackCount, Notify.TrackIndex + 1);
        }
        while (Montage->AnimNotifyTracks.Num() < TrackCount)
            Montage->AnimNotifyTracks.Add(
                FAnimNotifyTrack(FName(*FString::FromInt(Montage->AnimNotifyTracks.Num() + 1)), FLinearColor::White));
        Montage->RefreshCacheData();
        Report.Kind = EP4SkinnedFillKind::AnimMontage;
        Report.Tracks = Montage->SlotAnimTracks.Num();
        Report.RawKeys = Montage->Notifies.Num();
        return true;
    }
    if (USkeleton* Skeleton = Cast<USkeleton>(Object))
    {
        FSkeletonNative S;
        if (!ReadSkeletonNative(Body, Start, End, Package.Names, S, Read))
        {
            Error = Refusal(TEXT("skeleton-native-data-not-read"), Read);
            return false;
        }
        return FillSkeleton(Skeleton, S, Report, Error);
    }
    if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
    {
        FSkeletalMeshNative M;
        if (!ReadSkeletalMeshNative(Body, Start, End, Package.Names, Mesh->bHasVertexColors, M, Read))
        {
            Error = Refusal(TEXT("skinned-mesh-native-data-not-read"), Read);
            return false;
        }
        return FillMesh(Mesh, M, Reference, Report, Error);
    }
    if (UAnimSequence* Sequence = Cast<UAnimSequence>(Object))
    {
        FAnimSequenceNative S;
        if (!ReadAnimSequenceNative(Body, Start, End, Package.Names, S, Read))
        {
            Error = Refusal(TEXT("sequence-native-data-not-read"), Read);
            return false;
        }
        return FillSequence(Sequence, S, Report, Error);
    }
    Error = TEXT("skinned-class-not-made-here: ") + Object->GetClass()->GetPathName();
    return false;
}

bool P4CompareSkinnedCooked(UObject* Object, const P4MapKit::FPackageInventory& Game,
    const std::vector<std::uint8_t>& GameBody, std::size_t GameStart, std::size_t GameEnd,
    const P4MapKit::FPackageInventory& Mine, const std::vector<std::uint8_t>& MineBody, std::size_t MineStart,
    std::size_t MineEnd, FP4SkinnedCompareReport& Report, FString& Error)
{
    Report = FP4SkinnedCompareReport();
    using namespace P4SkinnedContent;
    UAnimSequence* Sequence = Cast<UAnimSequence>(Object);
    if (!Sequence)
        return true;
    FReadError Read;
    FAnimSequenceNative G, M;
    if (!ReadAnimSequenceNative(GameBody, GameStart, GameEnd, Game.Names, G, Read) ||
        !ReadAnimSequenceNative(MineBody, MineStart, MineEnd, Mine.Names, M, Read))
    {
        Error = Refusal(TEXT("sequence-native-data-not-read"), Read);
        return false;
    }
    return CompareSequence(Sequence, G, M, Report, Error);
}

#include "P4SkinnedContent.h"
#include "P4OriginalAnimationData.h"
#include "P4OriginalAssetImport.h"
#include "P4LevelContent.h"
#include "P4MapKitCore/skinned_native.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Serialization/ObjectWriter.h"
#include "Animation/Skeleton.h"
#include "AnimationUtils.h"
#include "ReferenceSkeleton.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
struct FSkinnedTestSkeleton : USkeleton
{
    static FReferenceSkeleton& Reference(USkeleton* S) { return S->*(&FSkinnedTestSkeleton::ReferenceSkeleton); }
    static void RootSettings(USkeleton* S) { (S->*(&FSkinnedTestSkeleton::BoneTree)).AddDefaulted(); }
    static void BoneSettings(USkeleton* S, int32 Count) { (S->*(&FSkinnedTestSkeleton::BoneTree)).SetNum(Count); }
    static void Identity(USkeleton* S) { S->*(&FSkinnedTestSkeleton::Guid) = FGuid::NewGuid(); }
};
struct FSkinnedTestSequence : UAnimSequence
{
    static TArray<FRawAnimSequenceTrack>& Raw(UAnimSequence* S)
    {
        return S->*(&FSkinnedTestSequence::RawAnimationData);
    }
};
TArray<uint8> CacheForm(UAnimSequence* S)
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, true);
    S->SerializeCompressedData(Writer, true);
    return Bytes;
}
struct FArchivedSkinnedSource
{
    FString Header;
    std::vector<uint8> Bytes;
    P4MapKit::FPackageInventory Package;
    P4MapKit::FPropertyInventory Properties;
    std::size_t End = 0;
    bool Read(FAutomationTestBase& Test, const TCHAR* Relative, const TCHAR* Class)
    {
        const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ARCHIVED_SOURCE"));
        if (!Test.TestFalse(TEXT("Archived original source root is explicitly supplied"), Root.IsEmpty()))
            return false;
        Header = Root / (FString(Relative) + TEXT(".uasset"));
        TArray<uint8> H, B;
        if (!Test.TestTrue(TEXT("Retained source pair is readable"),
                FFileHelper::LoadFileToArray(H, *Header) &&
                    FFileHelper::LoadFileToArray(B, *(Root / (FString(Relative) + TEXT(".uexp"))))))
            return false;
        const std::vector<uint8> Head(H.GetData(), H.GetData() + H.Num());
        Bytes.assign(B.GetData(), B.GetData() + B.Num());
        Package = P4MapKit::ReadPackage(Head, Bytes);
        if (!Test.TestTrue(TEXT("Original native package parses completely"), Package.Error.Code.empty()))
            return false;
        for (const auto& Export : Package.Exports)
        {
            std::string Kind;
            Package.Reference(Export.Class, Kind);
            if (Kind != TCHAR_TO_UTF8(Class))
                continue;
            Properties = P4MapKit::ReadPropertyEnvelopes(
                Bytes, Package.Names, Export.Offset, Export.Offset + Export.Size);
            End = Export.Offset + Export.Size;
            return Test.TestTrue(TEXT("Original tagged/native boundary is known"), Properties.Error.Code.empty());
        }
        Test.AddError(TEXT("Expected native source export is absent"));
        return false;
    }
};
bool ExactPose(const FTransform& Transform, const P4MapKit::FSkinnedPose& Source)
{
    const FQuat Q = Transform.GetRotation();
    const FVector T = Transform.GetTranslation(), S = Transform.GetScale3D();
    const float Values[] = {Q.X, Q.Y, Q.Z, Q.W, T.X, T.Y, T.Z, S.X, S.Y, S.Z};
    return FMemory::Memcmp(Values, Source.data(), sizeof(Values)) == 0;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ArchivedSkeletonReferencePoseTest, "P4MapKit.Skinned.ArchivedReferencePose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ArchivedSkeletonReferencePoseTest::RunTest(const FString&)
{
    FArchivedSkinnedSource Source;
    if (!Source.Read(*this, TEXT("Carrot4/Characters/Teki/Mar/SKEL_chr_Mar_00"), TEXT("/Script/Engine.Skeleton")))
        return false;
    P4MapKit::FSkeletonNative Game;
    P4MapKit::FReadError Error;
    if (!TestTrue(TEXT("Actual Mar reference skeleton decodes completely"),
            P4MapKit::ReadSkeletonNative(
                Source.Bytes, Source.Properties.End, Source.End, Source.Package.Names, Game, Error)))
        return false;
    // The source quaternion that Add normalized was already within the engine's unit-rotation
    // tolerance. Assert exact captured words so a conveniently normalized fixture cannot hide it.
    const uint32 EyeWords[] = {0x3f0cae2b, 0xbda6651d, 0x3f52e615, 0xbde70d06};
    if (!TestTrue(TEXT("The regression uses the exact source eye quaternion"),
            Game.Bones.size() == 31 && FMemory::Memcmp(Game.Bones[30].Pose.data(), EyeWords, sizeof(EyeWords)) == 0))
        return false;
    const FP4OriginalAssetImportResult Result = P4ImportOriginalAssetNative(Source.Header, FString(), true);
    if (!TestTrue(TEXT("Actual skeleton imports through strict source cook comparison"), Result.IsValid()))
    {
        AddError(Result.ErrorDetail);
        return false;
    }
    USkeleton* Skeleton = Cast<USkeleton>(Result.Asset);
    if (!TestNotNull(TEXT("Imported source remains an ordinary skeleton"), Skeleton))
        return false;
    const auto Check = [&](USkeleton* Object)
    {
        const FReferenceSkeleton& Reference = Object->GetReferenceSkeleton();
        if (!TestEqual(TEXT("All source bones survive"), Reference.GetRawBoneNum(), int32(Game.Bones.size())))
            return;
        for (int32 Index = 0; Index < Reference.GetRawBoneNum(); ++Index)
        {
            TestTrue(FString::Printf(TEXT("Raw source pose %d retains every IEEE word"), Index),
                ExactPose(Reference.GetRawRefBonePose()[Index], Game.Bones[Index].Pose));
            TestTrue(FString::Printf(TEXT("Final source pose %d retains every IEEE word"), Index),
                ExactPose(Reference.GetRefBonePose()[Index], Game.Bones[Index].Pose));
        }
    };
    Check(Skeleton);
    FString SavedFile;
    if (!TestTrue(TEXT("Saved skeleton path resolves"),
            FPackageName::TryConvertLongPackageNameToFilename(Result.Package, SavedFile, TEXT(".uasset"))))
        return false;
    SavedFile = FPaths::ConvertRelativePathToFull(SavedFile);
    UPackage* Reload = LoadPackage(
        CreatePackage(*(TEXT("/Temp/P4ArchivedSkeletonReload_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        *SavedFile, LOAD_NoWarn);
    USkeleton* Saved = Reload ? FindObject<USkeleton>(Reload, TEXT("SKEL_chr_Mar_00")) : nullptr;
    if (!TestNotNull(TEXT("A distinct saved skeleton reloads"), Saved) ||
        !TestTrue(TEXT("Readback uses a different object"), Saved != Skeleton))
        return false;
    Check(Saved);
    TestEqual(TEXT("Saved source cook state is unchanged"), P4OriginalObjectAssetState(Saved), Result.State);
    Saved->ClearFlags(RF_Public | RF_Standalone);
    Saved->MarkPendingKill();
    Reload->SetDirtyFlag(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ArchivedSkeletalSectionsTest, "P4MapKit.Skinned.ArchivedSectionOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ArchivedSkeletalSectionsTest::RunTest(const FString&)
{
    FArchivedSkinnedSource Source;
    if (!Source.Read(
            *this, TEXT("Carrot4/Objects/Otakara/TomatoS/SK_stg_TomatoS01"), TEXT("/Script/Engine.SkeletalMesh")))
        return false;
    bool Colors = false;
    for (const auto& Tag : Source.Properties.Properties)
        if (Tag.Name == "bHasVertexColors")
            Colors = Tag.BoolValue;
    P4MapKit::FSkeletalMeshNative Game;
    P4MapKit::FReadError ReadError;
    if (!TestTrue(TEXT("Actual Tomato mesh native data decodes completely"),
            P4MapKit::ReadSkeletalMeshNative(
                Source.Bytes, Source.Properties.End, Source.End, Source.Package.Names, Colors, Game, ReadError)))
        return false;
    if (!TestTrue(TEXT("The regression uses the source non-shadow section"),
            Game.LODs.size() == 3 && !Game.LODs[0].Sections[0].CastShadow))
        return false;
    TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>());
    FSkinnedTestSkeleton::Identity(Skeleton.Get());
    FSkinnedTestSkeleton::BoneSettings(Skeleton.Get(), int32(Game.Bones.size()));
    {
        FReferenceSkeletonModifier Modifier(FSkinnedTestSkeleton::Reference(Skeleton.Get()), Skeleton.Get());
        for (int32 Index = 0; Index < int32(Game.Bones.size()); ++Index)
        {
            const auto& Bone = Game.Bones[Index];
            const auto& P = Bone.Pose;
            const FTransform Pose(FQuat(P[0], P[1], P[2], P[3]), FVector(P[4], P[5], P[6]), FVector(P[7], P[8], P[9]));
            Modifier.Add(
                FMeshBoneInfo(FName(UTF8_TO_TCHAR(Bone.Name.c_str())), UTF8_TO_TCHAR(Bone.Name.c_str()), Bone.Parent),
                Pose);
            Modifier.UpdateRefPoseTransform(Index, Pose);
        }
    }
    TStrongObjectPtr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>());
    Mesh->Skeleton = Skeleton.Get();
    Mesh->bHasVertexColors = Colors;
    Mesh->GetLODInfoArray().SetNum(int32(Game.LODs.size()));
    for (int32 Index = 0; Index < int32(Game.LODs.size()); ++Index)
    {
        Mesh->GetLODInfo(Index)->BuildSettings.bUseFullPrecisionUVs = Game.LODs[Index].FullPrecisionUVs;
        Mesh->GetLODInfo(Index)->BuildSettings.bUseHighPrecisionTangentBasis = Game.LODs[Index].HighPrecisionTangents;
    }
    // This fixture isolates native section/bone reconstruction; material import has its own gate.
    // Use the engine's actual material while retaining every original vertex, section and pose word.
    FP4SkinnedFillReport Report;
    FString Error;
    if (!TestTrue(TEXT("The real native source fills an editable skeletal mesh"),
            P4FillSkinnedNative(
                Mesh.Get(), Source.Package, Source.Bytes, Source.Properties.End, Source.End, [](int32)
                { return FString(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")); }, Report, Error)))
    {
        AddError(Error);
        return false;
    }
    const auto Check = [&]()
    {
        const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
        const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering();
        if (!TestNotNull(TEXT("Native source model survives"), Model) ||
            !TestNotNull(TEXT("Native render model exists"), Render))
            return;
        for (int32 LOD = 0; LOD < Model->LODModels.Num(); ++LOD)
            for (int32 Index = 0; Index < Model->LODModels[LOD].Sections.Num(); ++Index)
            {
                const auto& S = Game.LODs[LOD].Sections[Index];
                const FSkelMeshSection& Section = Model->LODModels[LOD].Sections[Index];
                const FSkelMeshSourceSectionUserData* User = Model->LODModels[LOD].UserSectionsData.Find(
                    Section.OriginalDataSectionIndex);
                const FSkelMeshRenderSection& Drawn = Render->LODRenderData[LOD].RenderSections[Index];
                TestTrue(TEXT("Editor source section flags retain actual source values"),
                    Section.bCastShadow == S.CastShadow && Section.bRecomputeTangent == S.RecomputeTangent &&
                        uint8(Section.RecomputeTangentsVertexMaskChannel) == S.RecomputeTangentMask &&
                        Section.bDisabled == S.Disabled && Section.CorrespondClothAssetIndex == S.ClothAsset);
                TestTrue(TEXT("Native rebuild override storage agrees with recovered sections"),
                    User && User->bCastShadow == S.CastShadow && User->bRecomputeTangent == S.RecomputeTangent &&
                        uint8(User->RecomputeTangentsVertexMaskChannel) == S.RecomputeTangentMask &&
                        User->bDisabled == S.Disabled);
                TestTrue(TEXT("Render section flags and geometry counts retain source values"),
                    Drawn.bCastShadow == S.CastShadow && Drawn.bRecomputeTangent == S.RecomputeTangent &&
                        uint8(Drawn.RecomputeTangentsVertexMaskChannel) == S.RecomputeTangentMask &&
                        Drawn.bDisabled == S.Disabled && Drawn.NumTriangles == S.Triangles &&
                        Drawn.NumVertices == uint32(S.Vertices) &&
                        Drawn.MaxBoneInfluences == uint32(S.MaxBoneInfluences));
            }
        for (int32 Index = 0; Index < Mesh->RefSkeleton.GetRawBoneNum(); ++Index)
            TestTrue(TEXT("Mesh reference poses preserve every source word"),
                ExactPose(Mesh->RefSkeleton.GetRawRefBonePose()[Index], Game.Bones[Index].Pose));
    };
    Check();
    Mesh->PostEditChange();
    Check();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SkinnedSourceRetentionTest, "P4MapKit.Skinned.SourceRetention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SkinnedSourceRetentionTest::RunTest(const FString&)
{
    USkeleton* Skeleton = NewObject<USkeleton>();
    FSkinnedTestSkeleton::Identity(Skeleton);
    FSkinnedTestSkeleton::RootSettings(Skeleton);
    {
        FReferenceSkeletonModifier Modifier(FSkinnedTestSkeleton::Reference(Skeleton), Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
    }
    UAnimSequence* Source = NewObject<UAnimSequence>();
    Source->SetSkeleton(Skeleton);
    Source->SequenceLength = 1.f;
    Source->SetRawNumberOfFrame(3);
    Source->BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
    Source->CurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
    FRawAnimSequenceTrack Raw;
    Raw.PosKeys = {FVector(0, 0, 0), FVector(10, 0, 0), FVector(20, 0, 0)};
    Raw.RotKeys = {FQuat::Identity};
    Raw.ScaleKeys = {FVector::OneVector};
    TestEqual(TEXT("native editable source track added"), Source->AddNewRawTrack(TEXT("root"), &Raw), 0);
    Source->MarkRawDataAsModified(false);
    Source->RequestSyncAnimRecompression(false);
    if (!TestTrue(TEXT("fixture compressed by native engine"), Source->IsCompressedDataValid()))
        return false;
    UP4OriginalAnimationData* Original = NewObject<UP4OriginalAnimationData>(Source);
    Original->CacheSuffix = Source->GetDDCCacheKeySuffix(false);
    Original->CacheBytes = CacheForm(Source);
    Source->AssetImportData = Original;
    TestTrue(TEXT("retained source object is editor only"), Original->IsEditorOnly());
    const FProperty* ImportProperty = FindFProperty<FProperty>(UAnimSequence::StaticClass(), TEXT("AssetImportData"));
    TestTrue(TEXT("reference to retained source is editor only"),
        ImportProperty && ImportProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    TestTrue(TEXT("unedited source restores native derived data"), P4RestoreSkinnedSource(Source));
    TestTrue(TEXT("unedited restored cache bytes are exact"), CacheForm(Source) == Original->CacheBytes);
    UAnimSequence* Edited = DuplicateObject<UAnimSequence>(Source, GetTransientPackage());
    for (FVector& Position : FSkinnedTestSequence::Raw(Edited)[0].PosKeys)
        Position.X += 5.f;
    Edited->MarkRawDataAsModified(false);
    TestTrue(TEXT("raw track edit changes native source cache key"),
        Edited->GetDDCCacheKeySuffix(false) != Original->CacheSuffix);
    TestFalse(TEXT("edited source cannot restore original stream"), P4RestoreSkinnedSource(Edited));
    Edited->RequestSyncAnimRecompression(false);
    TestTrue(TEXT("edited source compresses normally"), Edited->IsCompressedDataValid());
    FTransform Before, After;
    Source->GetBoneTransform(Before, 0, 0.f, false);
    Edited->GetBoneTransform(After, 0, 0.f, false);
    TestTrue(TEXT("native compressed edit retains five cm change"),
        FMath::Abs((After.GetTranslation() - Before.GetTranslation()).X - 5.f) < 0.0001f);
    TestTrue(
        TEXT("edited compressed stream differs from retained original"), CacheForm(Edited) != Original->CacheBytes);
    Edited->MarkPendingKill();
    Source->MarkPendingKill();
    Skeleton->MarkPendingKill();
    TArray<FString> Fixtures;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_SKINNED_SOURCE_FIXTURE")).ParseIntoArray(Fixtures, TEXT(";"), true);
    for (const FString& FixturePath : Fixtures)
    {
        AddInfo(FString::Printf(TEXT("Exact-source fixture: %s"), *FixturePath));
        UAnimSequence* Fixture = LoadObject<UAnimSequence>(nullptr, *FixturePath);
        if (!TestNotNull(TEXT("real source fixture reopens"), Fixture))
            return false;
        UP4OriginalAnimationData* Held = Cast<UP4OriginalAnimationData>(Fixture->AssetImportData);
        if (!TestNotNull(TEXT("real fixture retains editor source binary"), Held))
            return false;
        TestTrue(TEXT("real fixture source key remains original"),
            Fixture->GetDDCCacheKeySuffix(false) == Held->CacheSuffix);
        TestTrue(
            TEXT("real fixture compressed cache bytes exact after reopen"), CacheForm(Fixture) == Held->CacheBytes);
        TestTrue(TEXT("real fixture can restore its exact source cache"), P4RestoreSkinnedSource(Fixture));
        TestTrue(TEXT("real fixture native restored data remains exact"), CacheForm(Fixture) == Held->CacheBytes);
        UAnimSequence* FixtureEdit = DuplicateObject<UAnimSequence>(Fixture, GetTransientPackage());
        for (FVector& Position : FSkinnedTestSequence::Raw(FixtureEdit)[0].PosKeys)
            Position.X += 5.f;
        FixtureEdit->MarkRawDataAsModified(false);
        TestFalse(TEXT("real fixture raw edit cannot replay original compressed source"),
            P4RestoreSkinnedSource(FixtureEdit));
        FixtureEdit->RequestSyncAnimRecompression(false);
        TestTrue(TEXT("real fixture edit takes ordinary native compression"), FixtureEdit->IsCompressedDataValid());
        TestTrue(TEXT("real fixture edit compressed bytes differ"), CacheForm(FixtureEdit) != Held->CacheBytes);
        Fixture->GetBoneTransform(Before, 0, 0.f, false);
        FixtureEdit->GetBoneTransform(After, 0, 0.f, false);
        TestTrue(TEXT("real fixture native edit preserves five cm translation"),
            FMath::Abs((After.GetTranslation() - Before.GetTranslation()).X - 5.f) < 0.05f);
        FixtureEdit->MarkPendingKill();
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MontageIdentityTest, "P4MapKit.Skinned.MontageIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MontageIdentityTest::RunTest(const FString&)
{
    USkeleton* Skeleton = NewObject<USkeleton>();
    FSkinnedTestSkeleton::Identity(Skeleton);
    UAnimMontage* Montage = NewObject<UAnimMontage>();
    // Direct reflected assignment is how the importer assigns tagged properties. It does not
    // update UAnimationAsset's separate native skeleton GUID.
    FObjectProperty* Property = FindFProperty<FObjectProperty>(UAnimationAsset::StaticClass(), TEXT("Skeleton"));
    if (!TestNotNull(TEXT("Animation skeleton property exists"), Property))
        return false;
    Property->SetObjectPropertyValue_InContainer(Montage, Skeleton);
    Montage->Notifies.AddDefaulted();
    Montage->Notifies[0].TrackIndex = 3;
    TArray<uint8> Native;
    {
        FMemoryWriter W(Native, true);
        int32 HasGuid = 0;
        FGuid Guid = Skeleton->GetGuid();
        W << HasGuid;
        W << Guid;
    }
    const std::vector<uint8> Data(Native.GetData(), Native.GetData() + Native.Num());
    P4MapKit::FPackageInventory Package;
    FP4SkinnedFillReport Report;
    FString Error;
    if (!TestTrue(TEXT("Montage native skeleton identity imports"),
            P4FillSkinnedNative(
                Montage, Package, Data, 0, Data.size(), [](int32) { return FString(); }, Report, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Montage gets its original skeleton and notify track index"),
        Montage->GetSkeleton() == Skeleton && Montage->Notifies[0].TrackIndex == 3 &&
            Montage->AnimNotifyTracks.Num() == 4);
    TestTrue(TEXT("Native fill reports a montage"), Report.Kind == EP4SkinnedFillKind::AnimMontage);
    TArray<uint8> Saved;
    {
        FObjectWriter Writer(Montage, Saved, false, false, false);
    }
    TArray<uint8> GuidBytes;
    {
        FMemoryWriter W(GuidBytes, true);
        FGuid Guid = Skeleton->GetGuid();
        W << Guid;
    }
    TestTrue(TEXT("Native montage archive stores the original skeleton GUID"),
        Saved.Num() >= GuidBytes.Num() &&
            FMemory::Memcmp(Saved.GetData() + Saved.Num() - GuidBytes.Num(), GuidBytes.GetData(), GuidBytes.Num()) ==
                0);
    auto Foreign = Data;
    Foreign[4] ^= 1;
    TestFalse(TEXT("Montage from another skeleton is refused"),
        P4FillSkinnedNative(
            Montage, Package, Foreign, 0, Foreign.size(), [](int32) { return FString(); }, Report, Error));
    TestTrue(TEXT("Refused skeleton assignment leaves the original skeleton"), Montage->GetSkeleton() == Skeleton);
    Montage->MarkPendingKill();
    Skeleton->MarkPendingKill();
    return !HasAnyErrors();
}
#endif

#if P4_MAPKIT_TESTS
#include "P4OriginalRecord.h"
#include "P4OriginalAnimationData.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/CustomAttributesRuntime.h"
#include "Misc/AutomationTest.h"
#include "Misc/MemStack.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SourceAdditiveEyePoseTest, "P4MapKit.Animation.SourceAdditiveEyeRawPoses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SourceAdditiveEyePoseTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ADDITIVE_PROOF")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned imported Demejako eye-direction fixture."));
        return true;
    }
    auto* Sequence = LoadObject<UAnimSequence>(nullptr,
        TEXT("/Game/Carrot4/Characters/Teki/Demejako/Animation/AS_Demejako_EyeDirection.AS_Demejako_EyeDirection"));
    if (!TestNotNull(TEXT("The actual source eye-direction sequence imports as an animation"), Sequence))
        return false;
    USkeleton* Skeleton = Sequence->GetSkeleton();
    if (!TestTrue(TEXT("The real fixture retains its source mesh-space/fixed-frame additive identity"),
            Skeleton && Sequence->AdditiveAnimType == AAT_RotationOffsetMeshSpace &&
                Sequence->RefPoseType == ABPT_AnimFrame && Sequence->RefPoseSeq))
        return false;
    const auto Diagnostic = P4DiagnoseOriginalAsset(Sequence);
    if (!TestTrue(TEXT("The actual imported sequence retains its qualified original source record and cook"),
            Diagnostic.Error.IsEmpty() && Diagnostic.Comparison == EP4OriginalComparison::Equal))
        return false;
    auto* Base = Sequence->RefPoseSeq;
    auto* SourceData = Cast<UP4OriginalAnimationData>(Base->AssetImportData);
    if (!TestNotNull(TEXT("The actual reference animation retains original raw-source custody"), SourceData))
        return false;
    const FString BaseKey = Base->GetDDCCacheKeySuffix(false), StoredBaseKey = SourceData->CacheSuffix;
    TestEqual(TEXT("Reference animation starts unedited"), BaseKey, StoredBaseKey);
    FMemMark Mark(FMemStack::Get());
    TArray<FBoneIndexType> Required;
    for (int32 Bone = 0; Bone < Skeleton->GetReferenceSkeleton().GetNum(); ++Bone)
        Required.Add(FBoneIndexType(Bone));
    FBoneContainer Container;
    Container.SetUseRAWData(true);
    Container.InitializeTo(Required, FCurveEvaluationOption(false), *Skeleton);
    FCompactPose Pose;
    Pose.SetBoneContainer(&Container);
    FBlendedCurve Curve;
    Curve.InitFrom(Container);
    FStackCustomAttributes Attributes;
    FAnimationPoseData AnimationPose(Pose, Curve, Attributes);
    int32 Compared = 0;
    float TranslationError = 0, RotationError = 0;
    for (int32 Frame = 0; Frame < Sequence->GetRawNumberOfFrames(); ++Frame)
    {
        const float Time = Sequence->GetRawNumberOfFrames() > 1
            ? Sequence->SequenceLength * float(Frame) / float(Sequence->GetRawNumberOfFrames() - 1)
            : 0.f;
        // This native path explicitly evaluates editable RAW tracks and bakes their additive
        // difference. Retained compressed bytes alone cannot make this assertion pass.
        Sequence->GetAnimationPose(AnimationPose, FAnimExtractContext(Time, false));
        for (int32 Track = 0; Track < Sequence->CompressedData.CompressedTrackToSkeletonMapTable.Num(); ++Track)
        {
            const int32 Bone = Sequence->CompressedData.CompressedTrackToSkeletonMapTable[Track].BoneTreeIndex;
            FTransform Expected;
            Sequence->GetBoneTransform(Expected, Track, Time, false);
            const FTransform& Actual = Pose[FCompactPoseBoneIndex(Bone)];
            TranslationError = FMath::Max(
                TranslationError, FVector::Distance(Expected.GetTranslation(), Actual.GetTranslation()));
            RotationError = FMath::Max(
                RotationError, FMath::RadiansToDegrees(Expected.GetRotation().AngularDistance(Actual.GetRotation())));
            ++Compared;
        }
    }
    TestEqual(TEXT("The actual source fixture retains all three authored frames"), Sequence->GetRawNumberOfFrames(), 3);
    TestEqual(
        TEXT("The actual source fixture has eighty editable bone tracks"), Sequence->GetRawAnimationData().Num(), 80);
    TestTrue(TEXT("Every original frame and compressed bone was actually compared"),
        Compared > 0 &&
            Compared ==
                Sequence->GetRawNumberOfFrames() * Sequence->CompressedData.CompressedTrackToSkeletonMapTable.Num());
    TestTrue(TEXT("Editable absolute raw translations bake back to the original deltas"), TranslationError <= .05f);
    TestTrue(TEXT("Parent-dependent raw mesh-space rotations bake back to the original deltas"), RotationError <= .1f);
    TestEqual(TEXT("Pose validation leaves reference raw data unchanged"), Base->GetDDCCacheKeySuffix(false), BaseKey);
    TestEqual(
        TEXT("Pose validation leaves reference source custody unchanged"), SourceData->CacheSuffix, StoredBaseKey);
    AddInfo(FString::Printf(
        TEXT("Actual additive raw proof: %d frame/bone samples, translation %.9g cm, rotation %.9g degrees"), Compared,
        TranslationError, RotationError));
    return !HasAnyErrors();
}
#endif

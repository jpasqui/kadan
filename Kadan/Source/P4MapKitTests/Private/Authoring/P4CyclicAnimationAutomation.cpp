#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4OriginalRecord.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CyclicAnimationPlanTest, "P4MapKit.Kadan.CyclicAnimationDeclarations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CyclicAnimationPlanTest::RunTest(const FString&)
{
    auto Sequence = MakeShared<FP4KadanSourceAsset>(), Montage = MakeShared<FP4KadanSourceAsset>(),
         Other = MakeShared<FP4KadanSourceAsset>();
    Sequence->Package = TEXT("/Game/KadanCycleControl/A");
    Sequence->Kind = TEXT("AnimSequence");
    Montage->Package = TEXT("/Game/KadanCycleControl/B");
    Montage->Kind = TEXT("AnimMontage");
    Other->Package = TEXT("/Game/KadanCycleControl/C");
    Other->Kind = TEXT("DataTable");
    Sequence->Dependencies.Add(Montage->Package);
    Montage->Dependencies.Add(Sequence->Package);
    const auto Native = P4KadanPlanImport({Sequence, Montage}, TSet<FString>{Montage->Package}, true);
    TestTrue(TEXT("A real native animation cycle has a two-phase declaration plan"),
             Native.Problems.Num() == 0 && Native.CyclicAnimations.Num() == 2);
    Montage->Dependencies.Add(Other->Package);
    Other->Dependencies.Add(Sequence->Package);
    const auto Mixed = P4KadanPlanImport({Sequence, Montage, Other}, TSet<FString>{Montage->Package}, true);
    TestTrue(TEXT("Unqualified cycles through other native asset kinds still refuse"), Mixed.Problems.Num() > 0);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualCyclicAnimationTest, "P4MapKit.Kadan.OriginalNomiAnimationCycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualCyclicAnimationTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIMATION_CYCLE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned original Nomi animation pair."));
        return true;
    }
    const FString Prefix = TEXT("/Game/Carrot4/Characters/Teki/Nomi/Animation/");
    const FString Sequence = Prefix + TEXT("AS_Nomi_Move"), Montage = Prefix + TEXT("AM_Nomi_Move");
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    const FString Private = FPaths::GetPath(Settings.GamePaks);
    Settings.Retoc = Private / TEXT("retoc/retoc.exe");
    Settings.Repak = Private / TEXT("repak/repak.exe");
    Settings.AllowGameBackedLevels = true;
    // Container planning acquires requested roots first; transitive sources are
    // acquired during execution. Request both real halves to exercise the cycle
    // declaration itself rather than assuming the first plan already has them.
    const auto Plan = P4KadanPlanImport(Settings, TSet<FString>{Sequence, Montage}, true);
    for (const FString& Problem : Plan.Problems)
        AddInfo(TEXT("Actual animation plan: ") + Problem);
    for (const auto& Asset : Plan.Assets)
        if (Asset->Package == Sequence || Asset->Package == Montage)
            AddInfo(TEXT("Actual animation source: ") + Asset->Package + TEXT(" ") + Asset->Kind);
    if (!TestTrue(TEXT("The actual original pair no longer blocks the entire map plan"),
                  Plan.Problems.Num() == 0 && Plan.CyclicAnimations.Contains(FName(*Sequence)) &&
                      Plan.CyclicAnimations.Contains(FName(*Montage))))
        return false;
    const auto Imported = P4KadanRunImport(Settings, Plan, true);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(Imported.Report, *(Folder / TEXT("NomiAnimationCycle.json")),
                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestTrue(TEXT("Both source assets complete their guarded native imports"),
             Imported.Imported.Contains(Sequence) && Imported.Imported.Contains(Montage));
    auto* Clip = LoadObject<UAnimSequence>(nullptr, *(Sequence + TEXT(".AS_Nomi_Move")));
    auto* Composite = LoadObject<UAnimMontage>(nullptr, *(Montage + TEXT(".AM_Nomi_Move")));
    TestTrue(TEXT("The real source clip has editable poses and a genuine matching skeleton"),
             Clip && Composite && Clip->GetRawNumberOfFrames() > 0 && Clip->GetSkeleton() &&
                 Clip->GetSkeleton() == Composite->GetSkeleton());
    if (Composite)
        TestEqual(TEXT("Both original montage sections survive"), Composite->CompositeSections.Num(), 2);
    for (UObject* Asset : {static_cast<UObject*>(Clip), static_cast<UObject*>(Composite)})
        if (Asset)
        {
            const auto Compared = P4DiagnoseOriginalAsset(Asset);
            TestTrue(TEXT("Filling the other half preserves this asset's original native state: ") +
                         Asset->GetPathName(),
                     Compared.Error.IsEmpty() && Compared.Comparison == EP4OriginalComparison::Equal);
            TestFalse(TEXT("The completed animation cycle leaves no dirty dependency to block another map"),
                      Asset->GetOutermost()->IsDirty());
        }
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "P4EditorExposure.h"
#include "P4OriginalRecord.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Components/PostProcessComponent.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4EditorExposureScope, "P4MapKit.Editor.ImportedExposureScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4EditorExposureScope::RunTest(const FString&)
{
    TStrongObjectPtr<UWorld> World(
        NewObject<UWorld>(CreatePackage(TEXT("/Game/P4ExposureScope")), TEXT("P4ExposureScope"), RF_Transient));
    World->WorldType = EWorldType::Editor;
    // UObject construction zeroes legacy/deprecated fields that the stock struct
    // constructor intentionally leaves uninitialized on the stack.
    TStrongObjectPtr<UPostProcessComponent> SourceComponent(NewObject<UPostProcessComponent>());
    TStrongObjectPtr<UPostProcessComponent> ViewComponent(NewObject<UPostProcessComponent>());
    FPostProcessSettings& Source = SourceComponent->Settings;
    FPostProcessSettings& View = ViewComponent->Settings;
    Source.AutoExposureMethod = AEM_Manual;
    Source.AutoExposureApplyPhysicalCameraExposure = true;
    Source.AutoExposureBias = 1.f;
    Source.CameraISO = 100.f;
    Source.CameraShutterSpeed = 60.f;
    Source.DepthOfFieldFstop = 4.f;
    FEngineShowFlags EditorFlags(ESFIM_Editor);
    FEngineShowFlags GameViewFlags(EditorFlags);
    GameViewFlags.SetEditor(false);
    GameViewFlags.SetGame(true);
    FEngineShowFlags OtherFlags(EditorFlags);
    OtherFlags.SetEditor(false);
    OtherFlags.SetGame(false);
    for (const FEngineShowFlags* Flags : {&EditorFlags, &GameViewFlags})
    {
        View = Source;
        TestFalse(TEXT("Creator maps retain their exposure in both editor modes"),
            P4ApplyImportedEditorExposure(World.Get(), *Flags, false, View));
        TestTrue(TEXT("Creator physical exposure stays enabled"), View.AutoExposureApplyPhysicalCameraExposure);
    }
    FP4OriginalRecord Record;
    Record.Package = World->GetOutermost()->GetName();
    Record.Class = World->GetClass()->GetPathName();
    Record.Kind = TEXT("level");
    Record.SourceDigest = FString::ChrN(64, TEXT('1'));
    Record.ComparisonDigest = FString::ChrN(64, TEXT('2'));
    Record.SourceDigestKind = TEXT("test-source");
    Record.ComparisonSchema = TEXT("test-level");
    FString Error;
    if (!TestTrue(TEXT("Mark the synthetic imported level"), P4MarkOriginalAsset(World.Get(), Record, Error)))
        return false;
    const bool Dirty = World->GetOutermost()->IsDirty();
    for (const FEngineShowFlags* Flags : {&EditorFlags, &GameViewFlags})
    {
        View = Source;
        TestTrue(TEXT("Imported editor view corrects physical exposure, including Game View"),
            P4ApplyImportedEditorExposure(World.Get(), *Flags, false, View));
        TestFalse(
            TEXT("The resolved view no longer uses the physical camera"), View.AutoExposureApplyPhysicalCameraExposure);
        TestFalse(TEXT("A corrected view is unchanged on a second call"),
            P4ApplyImportedEditorExposure(World.Get(), *Flags, false, View));
        View.AutoExposureApplyPhysicalCameraExposure = true;
        // Compare properties rather than the struct's native padding bytes.
        for (TFieldIterator<FProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
            TestTrue(TEXT("Resolved setting stays exact: ") + It->GetName(),
                It->Identical_InContainer(&View, &Source, PPF_None));
    }
    for (EWorldType::Type Type : {EWorldType::Game, EWorldType::PIE, EWorldType::EditorPreview, EWorldType::Inactive})
    {
        World->WorldType = Type;
        for (const FEngineShowFlags* Flags : {&EditorFlags, &GameViewFlags})
        {
            View = Source;
            TestFalse(TEXT("Game, PIE, asset previews and import/cook worlds are unaffected in either flag mode"),
                P4ApplyImportedEditorExposure(World.Get(), *Flags, false, View));
            TestTrue(
                TEXT("Their physical camera exposure remains enabled"), View.AutoExposureApplyPhysicalCameraExposure);
        }
    }
    World->WorldType = EWorldType::Editor;
    for (const FEngineShowFlags* Flags : {&EditorFlags, &GameViewFlags})
    {
        View = Source;
        TestFalse(TEXT("Scene captures are unaffected in either flag mode"),
            P4ApplyImportedEditorExposure(World.Get(), *Flags, true, View));
        TestTrue(TEXT("Scene capture physical exposure remains enabled"), View.AutoExposureApplyPhysicalCameraExposure);
        View.AutoExposureMethod = AEM_Histogram;
        TestFalse(TEXT("Automatic exposure is unaffected in either flag mode"),
            P4ApplyImportedEditorExposure(World.Get(), *Flags, false, View));
        TestTrue(TEXT("Automatic exposure physical-camera setting stays unchanged"),
            View.AutoExposureApplyPhysicalCameraExposure);
    }
    View = Source;
    TestFalse(TEXT("Views with neither editor nor game flags are unaffected"),
        P4ApplyImportedEditorExposure(World.Get(), OtherFlags, false, View));
    TestFalse(TEXT("A null world is unaffected"), P4ApplyImportedEditorExposure(nullptr, GameViewFlags, false, View));
    FP4OriginalRecord After;
    TestTrue(TEXT("Original level record stays readable"), P4ReadOriginalRecord(World.Get(), After, Error));
    TestTrue(TEXT("Original level evidence stays exact"),
        After.SourceDigest == Record.SourceDigest && After.ComparisonDigest == Record.ComparisonDigest);
    TestEqual(TEXT("No package is dirtied by the preview"), World->GetOutermost()->IsDirty(), Dirty);
    TestTrue(TEXT("Source camera settings stay unchanged"), Source.AutoExposureApplyPhysicalCameraExposure);
    return !HasAnyErrors();
}
#endif

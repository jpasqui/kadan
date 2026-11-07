#include "P4EditorExposure.h"
#include "P4OriginalRecord.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "HAL/PlatformTime.h"

namespace
{
bool P4ExposureEligible(
    UWorld* World, const FEngineShowFlags& ViewFlags, bool SceneCapture, const FPostProcessSettings& Settings)
{
    // Game View clears Editor and sets Game on the same editor-world viewport.
    // WorldType and scene-capture checks still exclude gameplay, PIE and captures.
    return (ViewFlags.Editor || ViewFlags.Game) && !SceneCapture && World && World->WorldType == EWorldType::Editor &&
        Settings.AutoExposureMethod == AEM_Manual && Settings.AutoExposureApplyPhysicalCameraExposure;
}
bool P4ExposureImportedLevel(UWorld* World)
{
    FP4OriginalRecord Record;
    FString Error;
    return P4ReadOriginalRecord(World, Record, Error) && Record.Kind == TEXT("level");
}
void P4CorrectExposure(FPostProcessSettings& Settings)
{
    // Imported manual exposure combines with the stock editor camera's photographic
    // settings (Cave000: ISO 100, f/4, 1/60 s), making its Lit preview almost black.
    // Correct the resolved editor view while retaining the imported source settings.
    Settings.AutoExposureApplyPhysicalCameraExposure = false;
}
} //namespace

bool P4ApplyImportedEditorExposure(
    UWorld* World, const FEngineShowFlags& ViewFlags, bool SceneCapture, FPostProcessSettings& Settings)
{
    if (!P4ExposureEligible(World, ViewFlags, SceneCapture, Settings) || !P4ExposureImportedLevel(World))
        return false;
    P4CorrectExposure(Settings);
    return true;
}

namespace
{
class FImportedExposure final : public FSceneViewExtensionBase
{
public:
    explicit FImportedExposure(const FAutoRegister& Registration) : FSceneViewExtensionBase(Registration) {}
    void SetupViewFamily(FSceneViewFamily&) override {}
    void SetupView(FSceneViewFamily& Family, FSceneView& View) override
    {
        UWorld* World = Family.Scene ? Family.Scene->GetWorld() : nullptr;
        if (!P4ExposureEligible(World, Family.EngineShowFlags, View.bIsSceneCapture, View.FinalPostProcessSettings))
            return;
        // Reading the record parses package metadata. Views render every frame, so the
        // answer is kept per world and read again at most once a second.
        const double Now = FPlatformTime::Seconds();
        if (Checked.World != World || Now - Checked.At > 1.0)
            Checked = {World, P4ExposureImportedLevel(World), Now};
        if (Checked.Imported)
            P4CorrectExposure(View.FinalPostProcessSettings);
    }
    void BeginRenderViewFamily(FSceneViewFamily&) override {}
    void PreRenderViewFamily_RenderThread(FRHICommandListImmediate&, FSceneViewFamily&) override {}
    void PreRenderView_RenderThread(FRHICommandListImmediate&, FSceneView&) override {}

private:
    struct FChecked
    {
        TWeakObjectPtr<UWorld> World;
        bool Imported = false;
        double At = 0;
    } Checked;
};
TSharedPtr<FImportedExposure, ESPMode::ThreadSafe> Preview;
} //namespace

void P4StartEditorExposure()
{
    if (!Preview.IsValid())
        Preview = FSceneViewExtensions::NewExtension<FImportedExposure>();
}
void P4StopEditorExposure()
{
    Preview.Reset();
}

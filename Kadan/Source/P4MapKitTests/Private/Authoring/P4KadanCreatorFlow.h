#pragma once
#if P4_MAPKIT_TESTS
#include "P4KadanEditor.h"
#include "P4KadanCreatorFlowStages.h"
#include "P4KadanCreatorFlowFixtures.h"
#include "P4KadanLastMod.h"
#include "Engine/LevelStreaming.h"
#include "Misc/ConfigCacheIni.h"
#include "P4KadanImport.h"
#include "P4KadanProjectSetup.h"
#include "P4KadanSourceCatalog.h"
#include "P4KadanContainerIndex.h"
#include "P4OriginalRecord.h"
#include "P4OriginalLevelImport.h"
#include "P4KadanAssetPolicy.h"
#include "P4ModDescriptor.h"
#include "P4ExportSelection.h"
#include "P4ExportContainer.h"
#include "P4NativeHash.h"
#include "AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Editor.h"
#include "FeedbackContextEditor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "P4LevelChanges.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "FileHelpers.h"
#include "P4GroundTypes.h"
#include "P4CreatorSurfaceAdvice.h"
#include "P4CreatorSource.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace P4KadanCreatorFlow
{
extern const TCHAR *Owner;
using FPicker = SComboBox<TSharedPtr<FP4ModDescriptor>>;
TSharedPtr<SWidget> Find(const TSharedRef<SWidget> &Root,
                         const TFunctionRef<bool(const TSharedRef<SWidget> &)> &Predicate);
TSharedPtr<SWidget> Text(const TSharedRef<SWidget> &Root, const FString &Value, bool Contains = false);
struct FStep
{
    FString Name;
    TFunction<bool()> Action;
    double Limit = 45;
};
class FFlow;

// One stage of the creator flow. It adds its steps to the shared driver and keeps its own state.
// Inputs: the driver's input helpers and fixture identities. Outputs: its steps and receipt fields.
class FFlowStage
{
  public:
    explicit FFlowStage(FFlow &InFlow);

    virtual ~FFlowStage();

    // Add this stage's steps to the flow, in order.
    virtual void AddSteps() = 0;

    // A deferred panel operation is running between two steps.
    virtual void ObserveBusy();

    // The flow is ending. Put back what the stage borrowed before the receipt is written.
    virtual void Finish();

    // The flow is being destroyed. Stop the stage's observers and restore borrowed files.
    virtual void Stop();

    // What the receipt says this stage qualifies.
    virtual FString QualificationScope() const;

    // Whether the stage uses the labeled picker and tooltip fixtures.
    virtual bool UsesTooltipFixtures() const;

    // Stage fields added to the receipt after its qualification scope.
    virtual void DescribeReceipt(FJsonObject &Receipt) const;

  protected:
    FFlow &Flow;
};

// The stage made for a canonical stage name.
TUniquePtr<FFlowStage> MakeFlowStage(FFlow &Flow, const FString &Stage);

// The shared driver: the latent step loop, its receipt and the real Slate input that every stage uses.
class FFlow final : public IAutomationLatentCommand
{
  public:
    FFlow(FAutomationTestBase &InTest, const FString &InStage, const FString &InOut);

    ~FFlow() override;

    virtual bool Update() override;

    FAutomationTestBase &Test;
    const FString Stage;
    const FString Out;

    // Fixture identities shared by every stage. Stages that reread saved mods update A and B.
    FString Marker, APackage, BPackage, BFolder, ExportFolder, TextureHeader;
    const FString TexturePackage = TEXT("/Game/Carrot4/Objects/Gimmicks/Geyser/T_stg_GeyserStone_GY01_M");
    FP4ModDescriptor A, B;
    FP4ModNames ANames, BNames;

    // Step state that multi-frame actions advance.
    int32 Index = 0, Phase = 0, Frame = 0, ReadyFrame = 0;
    TWeakPtr<SWidget> Prepared;
    double Started = 0;
    bool Failed = false;

    // Receipt state that stages add to.
    TArray<TSharedPtr<FJsonValue>> CapturedScreens;
    TArray<TSharedPtr<FJsonValue>> RecoveredMetadata;
    FString FolderRoute;
    int32 BusyObservations = 0;

    // The native Cancel driver for modal progress windows.
    FDelegateHandle CancelTickHandle;
    bool CancelClicked = false;

    void Add(const FString &Name, TFunction<bool()> Action, double Limit = 45);

    void Fail(const FString &Why);

    bool Expect(bool Value, const FString &Why);

    TSharedPtr<SDockTab> Tab() const;

    TSharedPtr<SWidget> Tagged(const FString &Tag);

    TSharedPtr<SEditableTextBox> Field(const FString &Name);

    bool VisibleText(const FString &Value, bool Contains = false);

    bool OperationBusy();

    TSharedPtr<SWidget> WindowText(const FString &Value, bool MenuOnly = false);

    bool MouseClick(const TSharedPtr<SWidget> &Widget);

    void ScrollParents(const TSharedRef<SWidget> &Root, const TSharedPtr<SWidget> &Target);

    bool Expand(const FString &Tag, const FString &Caption, bool Value);

    bool CheckTag(const FString &Tag, bool Value);

    int32 SourceCount();

    bool StickyImportActions();

    bool StickyModsActions();

    bool Prepare(const TSharedPtr<SWidget> &Widget);

    bool Click(const FString &Caption);

    bool ClickQueuedExport(const FString &Caption);

    bool CommitField(const FString &Name);

    bool SelectTopFolder();

    bool Menu(const FString &Entry, const FString &Heading);

    bool SetField(const FString &Name, const FString &Value);

    bool ButtonEnabled(const FString &Caption);

    FString TaggedText(const FString &Tag);

    bool ResumeCheck(bool Value);

    void StopCancellation();

    bool ArmCancellation();

    bool ChooseKind(const FString &Kind);

    bool CheckSource(bool Value);

    bool Choose(const FP4ModDescriptor &Mod);

    bool CheckRoots(const FP4ModDescriptor &Mod, const TArray<FString> &Expected);

    // Whether the mod picker and the ID, Name and Version fields show this identity.
    bool ModFormMatches(const FString &Id, const FString &Name, const FString &Version);

    // Remove every selected root through its row button, one per step attempt.
    bool RemoveEveryRoot();

    bool NarrowPanel();

    bool Screenshot(const FString &Name);

    // Capture any displayed widget, such as a dialog window, into the evidence folder.
    bool CaptureWidget(const FString &Name, TSharedPtr<SWidget> Content);

    IContentBrowserSingleton &Browser();

    FString Tool(const TCHAR *Variable, const TCHAR *Relative);

  private:
    TUniquePtr<FFlowStage> StageSteps;
    FString Failure;
    int32 QueuedExportClicks = 0;
    double BusyStarted = 0;
    int32 CancelTicks = 0, CancelDialogs = 0, MissedButtonPresses = 0;
    FString LastMissedButtonStep, LastMissedButtonReason;
    FFeedbackContextEditor InteractiveFeedback;
    TUniquePtr<TGuardValue<FFeedbackContext *>> FeedbackScope;
    TArray<FStep> Steps;
    TArray<TSharedPtr<FJsonValue>> Completed;
    double LastStepSeconds = 0;
    bool Finished = false;
    FString LastCompletedStep;

    void WriteProgress(const FString &State);

    void MissedPress(const FString &Reason);

    bool BusyDeadlineExpired() const;

    void Finish();

    void DescribeRun(FJsonObject &Receipt);

    void DescribeInput(FJsonObject &Receipt);
};
} // namespace P4KadanCreatorFlow
#endif

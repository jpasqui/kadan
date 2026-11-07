#include "P4KadanCreatorFlow.h"
#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

FFlowStage::FFlowStage(FFlow& InFlow) : Flow(InFlow)
{}

FFlowStage::~FFlowStage()
{}

void FFlowStage::ObserveBusy()
{}

void FFlowStage::Finish()
{}

void FFlowStage::Stop()
{}

FString FFlowStage::QualificationScope() const
{
    return TEXT("Completed native creator UI steps listed in this receipt");
}

bool FFlowStage::UsesTooltipFixtures() const
{
    return false;
}

void FFlowStage::DescribeReceipt(FJsonObject&) const
{}

TUniquePtr<FFlowStage> MakeFlowStage(FFlow& Flow, const FString& Stage)
{
    struct FMaker
    {
        const TCHAR* Stage;
        TUniquePtr<FFlowStage> (*Make)(FFlow&);
    };
    static const FMaker Makers[] = {{TEXT("HistoryExport"), &MakeHistoryStage}, {TEXT("LastMod"), &MakeLastModStage},
        {TEXT("LastModReopen"), &MakeLastModStage}, {TEXT("CopyExtras"), &MakeCopyExtrasStage},
        {TEXT("Copy"), &MakeCopyStage}, {TEXT("CopyRead"), &MakeCopyStage}, {TEXT("Upgrade"), &MakeUpgradeStage},
        {TEXT("UpgradeReopen"), &MakeUpgradeStage}, {TEXT("Draft"), &MakeDraftStage},
        {TEXT("Receipt"), &MakeReceiptStage}, {TEXT("ReceiptReopen"), &MakeReceiptStage},
        {TEXT("MaterialBefore"), &MakeMaterialStage}, {TEXT("Material"), &MakeMaterialStage},
        {TEXT("MaterialReopen"), &MakeMaterialStage}, {TEXT("GroundBefore"), &MakeGroundStage},
        {TEXT("Ground"), &MakeGroundStage}, {TEXT("EdenBefore"), &MakeEdenStage}, {TEXT("Eden"), &MakeEdenStage},
        {TEXT("Pages"), &MakeLayersStage}, {TEXT("MapLayersBefore"), &MakeLayersStage},
        {TEXT("MapLayers"), &MakeLayersStage}, {TEXT("MapLayersDistinct"), &MakeLayersStage},
        {TEXT("MapProgress"), &MakeMapProgressStage}, {TEXT("IdentityBefore"), &MakeIdentityStage},
        {TEXT("Identity"), &MakeIdentityStage}, {TEXT("IssuesBefore"), &MakeIssuesStage},
        {TEXT("Issues"), &MakeIssuesStage}, {TEXT("Progress"), &MakeIssuesStage},
        {TEXT("ShippingBefore"), &MakeShippingStage}, {TEXT("Shipping"), &MakeShippingStage},
        {TEXT("CreatorReport"), &MakeCreatorReportStage}, {TEXT("ExportBusy"), &MakeExportBusyStage},
        {TEXT("Browser"), &MakeBrowserStage}, {TEXT("Reimport"), &MakeReimportStage},
        {TEXT("SetupReady"), &MakeSetupReadyStage}, {TEXT("Setup"), &MakeSetupStage},
        {TEXT("Create"), &MakeCreateStage}};
    for (const FMaker& Maker : Makers)
    {
        if (Stage == Maker.Stage)
        {
            return Maker.Make(Flow);
        }
    }
    return MakeReopenStage(Flow);
}

FFlow::FFlow(FAutomationTestBase& InTest, const FString& InStage, const FString& InOut)
    : Test(InTest), Stage(InStage), Out(FPaths::ConvertRelativePathToFull(InOut))
{
    A.Id = TEXT("kadan.flow.first");
    A.Name = TEXT("Kadan flow first");
    A.Version = TEXT("1.0.0");
    B.Id = TEXT("kadan.flow.second");
    B.Name = TEXT("Kadan flow second");
    B.Version = TEXT("1.0.0");
    FString Error;
    if (!P4ModDerivedNames(A, ANames, Error) || !P4ModDerivedNames(B, BNames, Error))
        Fail(Error);
    APackage = ANames.PackageRoot / TEXT("Texture");
    BFolder = BNames.PackageRoot / TEXT("Textures");
    BPackage = BFolder / TEXT("Texture");
    ExportFolder = FPaths::GetPath(Out) / TEXT("export");
    Marker = FPaths::ProjectSavedDir() / TEXT("KadanCreatorFlow/owner.txt");
    TextureHeader = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UI_TEXTURE"));
    // Production UI work now starts on a later Slate tick. Keep the real
    // editor feedback context alive for this entire owned latent flow;
    // GLog still feeds automation's registered output device. Restore GWarn
    // before this flow is destroyed, including every failed/timeout path.
    FeedbackScope = MakeUnique<TGuardValue<FFeedbackContext*>>(GWarn, &InteractiveFeedback);
    StageSteps = MakeFlowStage(*this, Stage);
    StageSteps->AddSteps();
}

FFlow::~FFlow()
{
    StageSteps->Stop();
    StopCancellation();
    FeedbackScope.Reset();
    StageSteps.Reset();
}

bool FFlow::Update()
{
    ++Frame;
    if (Failed || Index == Steps.Num())
    {
        Finish();
        return true;
    }
    if (OperationBusy())
    {
        StageSteps->ObserveBusy();
        ++BusyObservations;
        if (!BusyStarted)
            BusyStarted = FPlatformTime::Seconds();
        if (BusyDeadlineExpired())
            Fail(TEXT("Deferred native UI operation exceeded its owned 240 second limit"));
        return false;
    }
    BusyStarted = 0;
    if (!Started)
    {
        Started = FPlatformTime::Seconds();
        Test.AddInfo(TEXT("CreatorFlow ") + Stage + TEXT(": ") + Steps[Index].Name);
        UE_LOG(LogTemp, Display, TEXT("KadanCreatorFlow %s start [%d/%d]: %s"), *Stage, Index + 1, Steps.Num(),
            *Steps[Index].Name);
        WriteProgress(TEXT("running"));
    }
    if (Frame < ReadyFrame)
        return false;
    if (FPlatformTime::Seconds() - Started > Steps[Index].Limit)
    {
        Fail(TEXT("Timed out after ") + FString::SanitizeFloat(Steps[Index].Limit) + TEXT("s: ") + Steps[Index].Name);
        return false;
    }
    if (Steps[Index].Action())
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("step"), Steps[Index].Name);
        Row->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Started);
        Completed.Add(MakeShared<FJsonValueObject>(Row));
        LastCompletedStep = Steps[Index].Name;
        LastStepSeconds = FPlatformTime::Seconds() - Started;
        UE_LOG(LogTemp, Display, TEXT("KadanCreatorFlow %s done [%d/%d] %.3fs: %s"), *Stage, Index + 1, Steps.Num(),
            LastStepSeconds, *LastCompletedStep);
        ++Index;
        Started = 0;
        Phase = 0;
        Prepared.Reset();
        ReadyFrame = Frame + 2;
        WriteProgress(TEXT("between_steps"));
    }
    return false;
}

void FFlow::WriteProgress(const FString& State)
{
    // One tiny overwritten private checkpoint identifies a synchronous UI
    // action even if an outer editor deadline prevents Finish from running.
    auto Progress = MakeShared<FJsonObject>();
    Progress->SetStringField(TEXT("owner"), Owner);
    Progress->SetStringField(TEXT("stage"), Stage);
    Progress->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
    Progress->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());
    Progress->SetStringField(TEXT("state"), State);
    Progress->SetNumberField(TEXT("completed_steps"), Index);
    Progress->SetNumberField(TEXT("total_steps"), Steps.Num());
    Progress->SetStringField(TEXT("active_step"), Index < Steps.Num() ? Steps[Index].Name : FString());
    Progress->SetStringField(TEXT("last_completed_step"), LastCompletedStep);
    Progress->SetNumberField(TEXT("last_step_seconds"), LastStepSeconds);
    Progress->SetNumberField(TEXT("missed_button_presses"), MissedButtonPresses);
    Progress->SetStringField(TEXT("last_missed_button_reason"), LastMissedButtonReason);
    FString Json;
    FJsonSerializer::Serialize(Progress, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*Out, true);
    if (!FFileHelper::SaveStringToFile(Json, *(Out / (Stage.ToLower() + TEXT("-progress.json"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        Fail(TEXT("Cannot save current native UI progress checkpoint"));
}

void FFlow::Add(const FString& Name, TFunction<bool()> Action, double Limit)
{
    FStep Step;
    Step.Name = Name;
    Step.Action = MoveTemp(Action);
    Step.Limit = Limit;
    Steps.Add(MoveTemp(Step));
}

void FFlow::Fail(const FString& Why)
{
    if (!Failed)
    {
        Failed = true;
        Failure = Why;
        Test.AddError(TEXT("CreatorFlow ") + Stage + TEXT(": ") + Why);
    }
}

TSharedPtr<SDockTab> FFlow::Tab() const
{
    return FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("Kadan"))));
}

TSharedPtr<SWidget> FFlow::Tagged(const FString& Tag)
{
    const auto Panel = Tab();
    return Panel
        ? Find(Panel->GetContent(), [&](const TSharedRef<SWidget>& Widget) { return Widget->GetTag() == FName(*Tag); })
        : nullptr;
}

bool FFlow::BusyDeadlineExpired() const
{
    const double Limit = Index > 0 ? Steps[Index - 1].Limit : 240.0;
    return FPlatformTime::Seconds() - BusyStarted > FMath::Max(240.0, Limit);
}

void FFlow::Finish()
{
    if (Finished)
        return;
    Finished = true;
    StopCancellation();
    StageSteps->Finish();
    if (Failed && Tab())
        Screenshot(TEXT("failure"));
    WriteProgress(Failed ? TEXT("failed") : TEXT("completed"));
    auto Receipt = MakeShared<FJsonObject>();
    DescribeRun(*Receipt);
    DescribeInput(*Receipt);
    FString Json;
    FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*Out, true);
    if (!FFileHelper::SaveStringToFile(
            Json, *(Out / (Stage.ToLower() + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        Test.AddError(TEXT("Cannot save CreatorFlow acceptance receipt"));
}

void FFlow::DescribeRun(FJsonObject& Receipt)
{
    Receipt.SetStringField(TEXT("owner"), Owner);
    Receipt.SetStringField(TEXT("stage"), Stage);
    Receipt.SetBoolField(TEXT("passed"), !Failed && Index == Steps.Num());
    Receipt.SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
    Receipt.SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
    Receipt.SetArrayField(TEXT("completed"), Completed);
    Receipt.SetStringField(TEXT("error"), Failure);
    Receipt.SetArrayField(TEXT("screenshots"), CapturedScreens);
    Receipt.SetBoolField(TEXT("cancel_control_activated"), CancelClicked);
    Receipt.SetBoolField(TEXT("path_picker_unavailable_fixture"), StageSteps->UsesTooltipFixtures());
    Receipt.SetBoolField(TEXT("actual_property_tooltip_ui_api"), StageSteps->UsesTooltipFixtures());
    Receipt.SetNumberField(TEXT("cancel_observer_ticks"), CancelTicks);
    Receipt.SetNumberField(TEXT("cancel_dialogs_seen"), CancelDialogs);
    Receipt.SetStringField(TEXT("ui_feedback_route"),
        TEXT("Stock FFeedbackContextEditor scoped to the owned latent flow, including deferred "
             "Slate work; prior automation GWarn restored at flow destruction"));
    Receipt.SetStringField(TEXT("qualification_scope"), StageSteps->QualificationScope());
    StageSteps->DescribeReceipt(Receipt);
    if (Index < Steps.Num())
        Receipt.SetStringField(TEXT("active_step"), Steps[Index].Name);
    auto Panel = Tab();
    FString Visible;
    if (Panel)
        Find(Panel->GetContent(),
            [&](const TSharedRef<SWidget>& Widget)
            {
                if (Widget->GetType() == FName(TEXT("STextBlock")) && Visible.Len() < 8192)
                    Visible += StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() + TEXT("\n");
                return false;
            });
    Receipt.SetStringField(TEXT("visible_text"), Visible.Left(8192));
}

void FFlow::DescribeInput(FJsonObject& Receipt)
{
    TArray<FString> AssetFolders, PathFolders;
    TArray<FAssetData> SelectedAssets;
    Browser().GetSelectedFolders(AssetFolders);
    Browser().GetSelectedPathViewFolders(PathFolders);
    Browser().GetSelectedAssets(SelectedAssets);
    TArray<TSharedPtr<FJsonValue>> AssetFolderRows, PathFolderRows, AssetRows;
    for (const FString& Folder : AssetFolders)
        AssetFolderRows.Add(MakeShared<FJsonValueString>(Folder));
    for (const FString& Folder : PathFolders)
        PathFolderRows.Add(MakeShared<FJsonValueString>(Folder));
    for (const FAssetData& Asset : SelectedAssets)
        AssetRows.Add(MakeShared<FJsonValueString>(Asset.PackageName.ToString()));
    Receipt.SetArrayField(TEXT("browser_asset_folders"), AssetFolderRows);
    Receipt.SetArrayField(TEXT("browser_path_folders"), PathFolderRows);
    Receipt.SetArrayField(TEXT("browser_asset_packages"), AssetRows);
    Receipt.SetStringField(TEXT("folder_selection_route"), FolderRoute);
    Receipt.SetNumberField(TEXT("deferred_busy_observations"), BusyObservations);
    Receipt.SetNumberField(TEXT("actual_queued_export_clicks"), QueuedExportClicks);
    Receipt.SetStringField(TEXT("browser_folder_selection_route"),
        TEXT("Native folder tree row click; breadcrumb and source-list readback"));
    Receipt.SetStringField(
        TEXT("field_input_route"), TEXT("Slate mouse focus, Ctrl+A, Backspace, character events, Enter commit"));
    Receipt.SetStringField(TEXT("button_input_route"),
        TEXT("Current native hit-test path, normal pointer move, actual pressed/hover/capture, routed mouse down/up"));
    Receipt.SetNumberField(TEXT("active_phase"), Phase);
    Receipt.SetNumberField(TEXT("missed_button_presses"), MissedButtonPresses);
    Receipt.SetStringField(TEXT("last_missed_button_step"), LastMissedButtonStep);
    Receipt.SetStringField(TEXT("last_missed_button_reason"), LastMissedButtonReason);
    auto Fields = MakeShared<FJsonObject>();
    for (const FString& Name : {FString(TEXT("ID")), FString(TEXT("Name")), FString(TEXT("Version"))})
        if (auto Input = Field(Name))
            Fields->SetStringField(Name, Input->GetText().ToString());
    Receipt.SetObjectField(TEXT("form_fields"), Fields);
    Receipt.SetArrayField(TEXT("recovered_owned_metadata"), RecoveredMetadata);
}
} // namespace P4KadanCreatorFlow
#endif

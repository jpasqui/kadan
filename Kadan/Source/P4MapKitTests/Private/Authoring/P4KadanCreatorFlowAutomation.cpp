#include "P4KadanCreatorFlow.h"
#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{
// Opt-in acceptance. Both stages use one isolated project and one output folder.
// Fixture construction is native. All mod identity, root selection and export
// actions go through the displayed production panel and its existing delegates.
const TCHAR* Owner = TEXT("K1 Kadan creator menu acceptance v1");

TSharedPtr<SWidget> Find(
    const TSharedRef<SWidget>& Root, const TFunctionRef<bool(const TSharedRef<SWidget>&)>& Predicate)
{
    if (!Root->GetVisibility().IsVisible())
        return nullptr;
    if (Predicate(Root))
        return Root;
    FChildren* Children = Root->GetChildren();
    for (int32 I = 0; Children && I < Children->Num(); ++I)
        if (auto Child = Find(Children->GetChildAt(I), Predicate))
            return Child;
    return nullptr;
}

TSharedPtr<SWidget> Text(const TSharedRef<SWidget>& Root, const FString& Value, bool Contains)
{
    return Find(Root,
        [&](const TSharedRef<SWidget>& Widget)
        {
            if (Widget->GetType() != FName(TEXT("STextBlock")))
                return false;
            const FString Actual = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
            return Contains ? Actual.Contains(Value) : Actual == Value;
        });
}

} // namespace P4KadanCreatorFlow

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanCreatorFlowTest, "P4MapKit.Kadan.CreatorFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanCreatorFlowTest::RunTest(const FString&)
{
    FString Stage = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_FLOW_STAGE"));
    const FString Out = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_FLOW_OUT"));
    if (Stage.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("CreatorFlow was not exercised. Set P4_KADAN_FLOW_STAGE=Browser, Setup, Create, Reopen, "
                     "Reimport, CreatorReport or ExportBusy and P4_KADAN_FLOW_OUT in an isolated creator project."));
        return true;
    }
    const bool KnownStage = P4KadanCanonicalCreatorFlowStage(Stage);
    if (!KnownStage || Out.IsEmpty())
    {
        AddError(TEXT("CreatorFlow requires stage Browser, Setup, Create, Reopen, Reimport, CreatorReport or ExportBusy and an "
                      "evidence output directory"));
        return false;
    }
    if (Stage == TEXT("Browser") && FPaths::FileExists(Out / TEXT("browser.json")))
    {
        AddError(TEXT("Existing successful or failed Browser evidence is never overwritten"));
        return false;
    }
    if (Stage == TEXT("ExportBusy") && FPaths::FileExists(Out / TEXT("exportbusy.json")))
    {
        AddError(TEXT("Existing ExportBusy evidence is never overwritten"));
        return false;
    }
    if (IsRunningCommandlet() || !GEditor || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
    {
        AddError(TEXT("CreatorFlow requires a rendering Slate editor in the desktop session"));
        return false;
    }
    const FString ProjectName = FPaths::GetCleanFilename(FPaths::GetProjectFilePath());
    const bool HistoryProject = Stage == TEXT("HistoryExport") &&
        (ProjectName == TEXT("History.uproject") ||
            (ProjectName == TEXT("NavProof.uproject") &&
                FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NAVIGATION_ROOT_ONLY")) == TEXT("1")));
    const bool OrdinaryProject = Stage != TEXT("HistoryExport") &&
        ProjectName == (Stage == TEXT("Reimport") ? TEXT("KadanUpgrade.uproject") : TEXT("KadanCreatorFlow.uproject"));
    if (Stage != TEXT("CreatorReport") && !HistoryProject && !OrdinaryProject)
    {
        AddError(TEXT("CreatorFlow requires its dedicated KadanCreatorFlow project or the retained KadanUpgrade "
                      "project for Reimport"));
        return false;
    }
    for (const TCHAR* Variable : {TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"), TEXT("P4_NATIVE_ROOT")})
        if (FPlatformMisc::GetEnvironmentVariable(Variable).IsEmpty())
        {
            AddError(FString(TEXT("CreatorFlow requires actual export input: ")) + Variable);
            return false;
        }
    const FString Texture = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UI_TEXTURE"));
    if ((Stage == TEXT("Create") || Stage == TEXT("Reopen")) &&
        (!Texture.EndsWith(TEXT("T_stg_GeyserStone_GY01_M.uasset")) || !FPaths::FileExists(Texture)))
    {
        AddError(TEXT("CreatorFlow requires retained real Geyser texture header in P4_KADAN_UI_TEXTURE"));
        return false;
    }
    P4RegisterKadanEditor();
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<P4KadanCreatorFlow::FFlow>(*this, Stage, Out));
    return true;
}
#endif

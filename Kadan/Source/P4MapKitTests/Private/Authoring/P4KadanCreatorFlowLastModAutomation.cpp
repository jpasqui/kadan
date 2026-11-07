#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// LastMod and LastModReopen: the last chosen mod is restored when the panel and the editor reopen.
class FLastModStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    FDateTime LastModExportStarted = FDateTime(0);
};
} // namespace

void FLastModStage::AddSteps()
{
    Flow.Add(TEXT("Read the owned saved mod identities"),
        [this]()
        {
            TArray<FP4ModDescriptor> Found;
            FString Error;
            if (!P4ListModDescriptors(FPaths::ProjectDir(), Found, Error))
            {
                return Flow.Expect(false, Error);
            }
            const auto* First = Found.FindByPredicate(
                [this](const FP4ModDescriptor& Mod) { return Mod.Id == Flow.A.Id; });
            const auto* Second = Found.FindByPredicate(
                [this](const FP4ModDescriptor& Mod) { return Mod.Id == Flow.B.Id; });
            if (!First || !Second)
            {
                return Flow.Expect(false, TEXT("The two owned mod identities are unavailable"));
            }
            Flow.A = *First;
            Flow.B = *Second;
            return true;
        });
    Flow.Add(TEXT("Open Mods through the actual Kadan menu"),
        [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    if (Flow.Stage == TEXT("LastMod"))
    {
        Flow.Add(TEXT("Choose the second saved mod"), [this]() { return Flow.Choose(Flow.B); });
        Flow.Add(TEXT("Capture the second mod before switching"), [this]() { return Flow.Screenshot(TEXT("second")); });
        Flow.Add(TEXT("Choose the first saved mod"), [this]() { return Flow.Choose(Flow.A); });
        Flow.Add(TEXT("The accepted picker choice is persisted by ID"),
            [this]()
            {
                return Flow.Expect(P4KadanLastModId(GEditorPerProjectIni) == Flow.A.Id,
                           TEXT("The actual accepted mod choice was not persisted")) &&
                    Flow.Screenshot(TEXT("selected"));
            });
        Flow.Add(TEXT("Close only this flow's Kadan tab"),
            [this]()
            {
                if (const auto Panel = Flow.Tab())
                {
                    Panel->RequestCloseTab();
                    return true;
                }
                return false;
            });
        Flow.Add(TEXT("Reopen Kadan through its menu"),
            [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
        Flow.Add(TEXT("The reopened panel restores its actual selected item and fields"),
            [this]()
            {
                return Flow.ModFormMatches(Flow.A.Id, Flow.A.Name, Flow.A.Version) &&
                    Flow.Screenshot(TEXT("panel-reopened"));
            });
        Flow.Add(TEXT("Record this process for the separate cold reopen"),
            [this]()
            {
                const FString Text = FString::Printf(TEXT("%u"), FPlatformProcess::GetCurrentProcessId());
                return Flow.Expect(FFileHelper::SaveStringToFile(Text, *(Flow.Out / TEXT("last-mod-process.txt")),
                                       FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
                    TEXT("The owned process receipt could not be saved"));
            });
        return;
    }
    Flow.Add(TEXT("The editor process is new and its initial picker choice is restored"),
        [this]()
        {
            FString Earlier;
            const bool Read = FFileHelper::LoadFileToString(Earlier, *(Flow.Out / TEXT("last-mod-process.txt")));
            return Flow.Expect(
                       Read && FCString::Strtoui64(*Earlier, nullptr, 10) != FPlatformProcess::GetCurrentProcessId(),
                       TEXT("The cold reopen must use another editor process")) &&
                Flow.ModFormMatches(Flow.A.Id, Flow.A.Name, Flow.A.Version) && Flow.Screenshot(TEXT("cold-reopened"));
        });
    Flow.Add(TEXT("Choose the reusable output directory"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(
        TEXT("Review the automatically restored mod"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Capture the restored mod's export review"),
        [this]()
        {
            return Flow.Expect(
                       Flow.ButtonEnabled(TEXT("Export mod")), TEXT("The restored mod did not become exportable")) &&
                Flow.Screenshot(TEXT("review"));
        });
    Flow.Add(
        TEXT("Export through the restored mod's actual button"),
        [this]()
        {
            if (LastModExportStarted.GetTicks() == 0)
            {
                LastModExportStarted = FDateTime::UtcNow();
            }
            return Flow.ClickQueuedExport(TEXT("Export mod"));
        },
        600);
    Flow.Add(TEXT("The real export publishes the remembered identity"),
        [this]()
        {
            const FString Manifest = Flow.ExportFolder / Flow.A.Id / TEXT("romfs/tsuchi/mods") / Flow.A.Id /
                TEXT("mod.json");
            FString Json;
            FString Error;
            FP4KadanExportReceipt Receipt;
            TSharedPtr<FJsonObject> Document;
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan exported the mod to ")) &&
                           P4KadanReadExportReceipt(FPaths::ProjectDir(), Flow.A.Id, Receipt, Error) &&
                           Receipt.PublishedUtc >= LastModExportStarted &&
                           FFileHelper::LoadFileToString(Json, *Manifest) &&
                           FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Document) && Document &&
                           Document->GetStringField(TEXT("id")) == Flow.A.Id,
                       TEXT("The restored mod did not publish its own manifest")) &&
                Flow.Screenshot(TEXT("published"));
        });
}

TUniquePtr<FFlowStage> MakeLastModStage(FFlow& Flow)
{
    return MakeUnique<FLastModStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

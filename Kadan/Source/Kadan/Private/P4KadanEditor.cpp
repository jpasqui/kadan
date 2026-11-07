#include "P4KadanPanel.h"
#include "P4KadanGroundMenu.h"
namespace P4KadanEditorPrivate
{
const FName TabName(TEXT("Kadan"));
// Linear tint caps even the lightest pixels of stock raised-button brushes.
// White text has at least 5.5:1 contrast against either enabled tint.
const FLinearColor ButtonTint(0.12f, 0.14f, 0.18f);
const FLinearColor PrimaryButtonTint(0.035f, 0.14f, 0.28f);
TSharedPtr<FExtender> MenuExtender;
bool Registered = false;
FText Label(const FString &Value)
{
    return FText::FromString(Value);
}
FString Size(int64 Bytes)
{
    return FString::Printf(TEXT("%.1f MiB"), double(Bytes) / (1024.0 * 1024.0));
}
FString Disposition(EP4ExportDisposition Value)
{
    switch (Value)
    {
    case EP4ExportDisposition::CreatorPackage:
        return TEXT("Packed asset");
    case EP4ExportDisposition::UnchangedGame:
        return TEXT("Game reference");
    case EP4ExportDisposition::GameEdit:
        return TEXT("Game changes");
    case EP4ExportDisposition::NeedsReplacementProof:
        return TEXT("Replacement");
    default:
        return TEXT("Export issue");
    }
}

TWeakPtr<SKadanPanel> CurrentPanel;
TSharedRef<SDockTab> Spawn(const FSpawnTabArgs &)
{
    const TSharedRef<SKadanPanel> Panel = SNew(SKadanPanel);
    CurrentPanel = Panel;
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[Panel];
}
void Open(int32 Page)
{
    FGlobalTabmanager::Get()->TryInvokeTab(TabName);
    if (const auto Panel = CurrentPanel.Pin())
        Panel->ShowPage(Page);
}
} // namespace P4KadanEditorPrivate
void P4KadanShowPanel(int32 Page)
{
    P4KadanEditorPrivate::Open(Page);
}
namespace P4KadanEditorPrivate
{
void BuildMenu(FMenuBuilder &Menu)
{
    Menu.AddMenuEntry(Label(TEXT("Import from game")),
                      Label(TEXT("Choose game content and its required assets.")),
                      FSlateIcon(),
                      FUIAction(FExecuteAction::CreateStatic(&Open, 0)));
    Menu.AddMenuEntry(Label(TEXT("Mods and export")),
                      Label(TEXT("Manage the project's mods and review what each ships.")),
                      FSlateIcon(),
                      FUIAction(FExecuteAction::CreateStatic(&Open, 1)));
    Menu.AddMenuEntry(Label(TEXT("Game connection")),
                      Label(TEXT("Talk to the game running in Eden with the Dev Server mod.")),
                      FSlateIcon(),
                      FUIAction(FExecuteAction::CreateStatic(&P4OpenGameConnection)));
    P4KadanHelpMenu(Menu);
}
// UE4's stock text-only pull-down cannot contain a live status image. A custom menu-bar block
// uses the same menu button style and Kadan's existing menu contents, with the lamp in its header.
class SKadanMenuBar : public SMultiBlockBaseWidget
{
  public:
    SLATE_BEGIN_ARGS(SKadanMenuBar)
    {
    }
    SLATE_END_ARGS()
    void Construct(const FArguments &)
    {
    }
    virtual void BuildMultiBlockWidget(const ISlateStyle *StyleSet, const FName &StyleName) override
    {
        ChildSlot[SNew(SComboButton)
                      .ButtonStyle(StyleSet, ISlateStyle::Join(StyleName, ".Button"))
                      .ContentPadding(FMargin(10, 2))
                      .HasDownArrow(false)
                      .ForegroundColor(FSlateColor::UseForeground())
                      .ToolTipText(Label(TEXT("Content authoring for Pikmin 4")))
                      .OnGetMenuContent_Lambda(
                          []
                          {
                              FMenuBuilder Menu(true, nullptr);
                              BuildMenu(Menu);
                              return Menu.MakeWidget();
                          })
                      .ButtonContent()[P4GameConnectionMenuHeader()]];
    }
};
class FKadanMenuBarBlock : public FMultiBlock
{
  public:
    FKadanMenuBarBlock() : FMultiBlock(FUIAction(), "Kadan", EMultiBlockType::Widget)
    {
    }
    virtual TSharedRef<IMultiBlockBaseWidget> ConstructWidget() const override
    {
        return SNew(SKadanMenuBar);
    }
};
void MenuBar(FMenuBarBuilder &Menu)
{
    Menu.GetMultiBox()->AddMultiBlock(MakeShared<FKadanMenuBarBlock>());
}
} // namespace P4KadanEditorPrivate
using namespace P4KadanEditorPrivate;

FString P4KadanNotePackage(const FString &Asset)
{
    // The import names an asset by its package (/Game/A/B) or its object path (/Game/A/B.B).
    return FPackageName::ObjectPathToPackageName(Asset);
}

FString P4KadanImportCompletionMessage(const FP4KadanImportResult &Result)
{
    // The panel's notice is one line, so a failed import names the problem that stopped it
    // and leaves the rest to the Output Log.
    FString Why;
    if (!Result.Ok)
    {
        Why = Result.Problems.Num() ? TEXT(" ") + Result.Problems[0] : FString();
        if (Result.Problems.Num() > 1)
            Why += FString::Printf(TEXT(" %d more issue%s in the Output Log."), Result.Problems.Num() - 1,
                                   Result.Problems.Num() == 2 ? TEXT(" is") : TEXT("s are"));
        else if (!Result.Problems.Num())
            Why = TEXT(" The reasons are in the Output Log.");
    }
    FString Out;
    if (Result.Cancelled)
        Out = TEXT("The import was canceled. Completed assets are kept. Click Import to continue.");
    else if (!Result.Imported.Num())
        Out = Result.Ok ? TEXT("Import finished. No new packages were imported.")
                        : TEXT("No new packages were imported.") + Why;
    else
        Out = FString::Printf(TEXT("Kadan imported %d package%s%s"),
                              Result.Imported.Num(),
                              Result.Imported.Num() == 1 ? TEXT("") : TEXT("s"),
                              Result.Ok ? TEXT(".") : *(TEXT(" with issues.") + Why));
    int32 ImportedReferences = 0;
    for (const FString &Asset : Result.ReferenceOnly)
        if (Result.Imported.Contains(Asset))
            ++ImportedReferences;
    if (ImportedReferences)
        Out += FString::Printf(TEXT(" %d imported asset%s %s reference-only."),
                               ImportedReferences,
                               ImportedReferences == 1 ? TEXT("") : TEXT("s"),
                               ImportedReferences == 1 ? TEXT("is") : TEXT("are"));
    TSet<FString> Kept;
    for (const FString &Asset : Result.KeptAsTheGames)
        Kept.Add(P4KadanNotePackage(Asset));
    if (Kept.Num())
        Out += FString::Printf(
            TEXT(
                " %d reference%s the game's original assets. Those assets stay in the game and cannot be edited here."),
            Kept.Num(),
            Kept.Num() == 1 ? TEXT(" uses") : TEXT("s use"));
    if (Result.Ok && (Result.Warnings.Num() || Kept.Num()))
        Out += TEXT(" The import finished with notes.");
    return Out;
}

FString P4KadanStayedInGameReason(const FP4KadanImportResult &Result, const FString &Package)
{
    static const FString Because = TEXT("Not imported because "), DidNot = TEXT(" did not import");
    FString Current = Package;
    TSet<FString> Seen;
    while (!Seen.Contains(Current))
    {
        Seen.Add(Current);
        const FString *Reason = Result.GameReferenceReasons.Find(Current);
        if (!Reason || Reason->IsEmpty())
            return FString();
        const int32 End = Reason->StartsWith(Because)
                              ? Reason->Find(DidNot, ESearchCase::CaseSensitive, ESearchDir::FromStart, Because.Len())
                              : INDEX_NONE;
        if (End == INDEX_NONE)
            return (Current == Package ? FString()
                                       : FPackageName::GetShortName(Current) + TEXT(" stayed in the game. ")) +
                   Reason->Left(400);
        Current = Reason->Mid(Because.Len(), End - Because.Len());
    }
    return FString();
}

bool P4KadanActiveCreatorRoot(FString &Root, FString &Error)
{
    Root.Empty();
    Error.Empty();
    if (const auto Panel = CurrentPanel.Pin())
        return Panel->CreatorPackageRoot(Root, Error);
    Error = TEXT("Choose a mod in Kadan before creating content from a game asset.");
    return false;
}

void P4RegisterKadanEditor()
{
    // Slate can be initialized with generic non-rendering windows under NullRHI.
    // Loading LevelEditor here also loads MainFrame, whose shutdown persists
    // native window geometry. Headless import/automation needs no editor UI.
    if (Registered || IsRunningCommandlet() || !GEditor || !FApp::CanEverRender() ||
        !FSlateApplication::IsInitialized())
        return;
    Registered = true;
    P4Kadan::InitializeAssetPolicy();
    P4Kadan::InitializeReadOnlyRouting();
    P4Kadan::InitializePropertyGuards();
    FGlobalTabmanager::Get()
        ->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateStatic(&Spawn))
        .SetDisplayName(Label(TEXT("Kadan")))
        .SetMenuType(ETabSpawnerMenuType::Hidden);
    MenuExtender = MakeShared<FExtender>();
    MenuExtender->AddMenuBarExtension(
        "Help", EExtensionHook::After, nullptr, FMenuBarExtensionDelegate::CreateStatic(&MenuBar));
    FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor")
        .GetMenuExtensibilityManager()
        ->AddExtender(MenuExtender);
    P4RegisterGameConnection();
    P4RegisterGroundMenu();
}
void P4UnregisterKadanEditor()
{
    if (!Registered)
        return;
    Registered = false;
    P4Kadan::ShutdownPropertyGuards();
    P4Kadan::ShutdownReadOnlyRouting();
    P4Kadan::ShutdownAssetPolicy();
    if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
        FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor")
            .GetMenuExtensibilityManager()
            ->RemoveExtender(MenuExtender);
    MenuExtender.Reset();
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
    P4UnregisterGameConnection();
    P4UnregisterGroundMenu();
}

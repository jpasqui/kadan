// A new creator's first hour, driven on the real Kadan panel and recorded step by step.
//
// WalkthroughFirstHour runs in a project that has only the Kadan and P4Stubs plugins: open the panel, give it
// the game folder and the tools, set the project up (Kadan restarts Unreal Editor, and the walkthrough goes on in
// the next editor session), import one map, open it, Save Current Level, add a static mesh actor, make a mod, add
// the map and export, then read the export's containers. WalkthroughEarlierProject does the same from the opened
// map on, in a project whose map an earlier Kadan release imported.
//
// Every step saves a picture to Saved/KadanWalkthrough/<nn>-<step>.png and its result to steps.json there, written
// after each step, so a run that stops says where. tools/kadan_walkthrough.py runs both and makes a page of them.
//
// Inputs, all from the environment: P4_KADAN_WALK_PROJECT (the fresh project's .uproject) or
// P4_KADAN_WALK_EARLIER_PROJECT, P4_KADAN_WALK_GAME (the dump's romfs or Paks folder), P4_KADAN_WALK_RETOC and
// P4_KADAN_WALK_REPAK (needed when Kadan's own Tools folder has neither), P4_KADAN_WALK_MAP (the map, optional).
// Without them, or in another project, the tests report that they did not run.
#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanTestSeams.h"
#include "P4ExportProcess.h"
#include "Async/Async.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "ShaderCompiler.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealEdMisc.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{
namespace
{
// A cave of one floor: its persistent level and its eleven sublevels all have built lighting, and it had the
// fewest placed objects of the caves in the last sweep of every map.
const TCHAR* const DefaultMap = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave013/Cave013_F00/Cave013_F00_P");
const TCHAR* const AddedActor = TEXT("KadanWalkthroughMesh");
// How long a step's picture is tried again before the step goes on without one.
const double PictureSeconds = 30;

FString Variable(const TCHAR* Name)
{
    return FPlatformMisc::GetEnvironmentVariable(Name).TrimStartAndEnd();
}

// What a step's picture shows: the Kadan panel, the editor's main window, or the window a step named.
enum class EView
{
    Panel,
    Editor,
    Dialog
};

struct FWalkStep
{
    FString Id, Title;
    EView View = EView::Panel;
    TArray<TFunction<bool()>> Parts;
    FString Result = TEXT("not reached"), Error, Picture;
    TArray<FString> Notes, LogErrors;
    double Began = 0, Seconds = 0;
    int32 Part = 0;
};

// The Output Log's errors while a step runs. The steps decide the walkthrough's result; these are shown beside them.
class FStepLogErrors final : public FOutputDevice
{
public:
    void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        if (Verbosity != ELogVerbosity::Error && Verbosity != ELogVerbosity::Fatal)
            return;
        FScopeLock Lock(&Guard);
        if (Lines.Num() < 12)
            Lines.Add((Category.ToString() + TEXT(": ") + Text).Left(400));
        ++Count;
    }

    bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }

    TArray<FString> Take()
    {
        FScopeLock Lock(&Guard);
        TArray<FString> Out = MoveTemp(Lines);
        if (Count > Out.Num())
            Out.Add(FString::Printf(TEXT("and %d more"), Count - Out.Num()));
        Lines.Reset();
        Count = 0;
        return Out;
    }

private:
    FCriticalSection Guard;
    TArray<FString> Lines;
    int32 Count = 0;
};

#if PLATFORM_WINDOWS
// Answers the system's file dialog as a person does: the file's path in the name box, then Open. The dialog blocks
// the game thread until it closes, so this runs on a thread of its own, with the system's window messages only.
class FFileDialogAnswer
{
public:
    FFileDialogAnswer(const FString& InTitle, const FString& InFile) : Title(InTitle), File(InFile)
    {
        FPaths::MakePlatformFilename(File);
        Done = Async(EAsyncExecution::Thread, [this]() { Run(); });
    }

    // Wait for the answer. Opened is whether the click that opens the dialog was made at all.
    bool Finish(bool Opened, FString& Why)
    {
        if (!Opened)
            GiveUp = true;
        Done.Wait();
        Why = Reason;
        return Chosen;
    }

private:
    static BOOL CALLBACK NameBox(HWND Child, LPARAM Out)
    {
        WCHAR Class[32] = {};
        GetClassNameW(Child, Class, 32);
        // The file name box of the system's Open dialog.
        if (!wcscmp(Class, L"Edit") && GetDlgCtrlID(Child) == 1148)
        {
            *reinterpret_cast<HWND*>(Out) = Child;
            return 0;
        }
        return 1;
    }

    void Run()
    {
        const double End = FPlatformTime::Seconds() + 40;
        HWND Dialog = nullptr, Box = nullptr;
        while (!Dialog && !GiveUp && FPlatformTime::Seconds() < End)
        {
            FPlatformProcess::Sleep(0.1f);
            Dialog = FindWindowW(L"#32770", *Title);
        }
        if (!Dialog)
        {
            Reason = TEXT("The file dialog titled ") + Title + TEXT(" did not open.");
            return;
        }
        while (!Box && FPlatformTime::Seconds() < End)
        {
            FPlatformProcess::Sleep(0.1f);
            EnumChildWindows(Dialog, &NameBox, reinterpret_cast<LPARAM>(&Box));
        }
        bool Refused = !Box;
        if (Box)
        {
            FPlatformProcess::Sleep(0.5f);
            SendMessageW(Box, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(*File));
            PostMessageW(Dialog, WM_COMMAND, MAKEWPARAM(IDOK, 0), reinterpret_cast<LPARAM>(GetDlgItem(Dialog, IDOK)));
        }
        // The dialog closes when it takes the file. A message it shows instead (no such file) is closed, then the dialog.
        while (IsWindow(Dialog) && FPlatformTime::Seconds() < End)
        {
            FPlatformProcess::Sleep(0.2f);
            const HWND Message = GetWindow(Dialog, GW_ENABLEDPOPUP);
            if (Refused || (Message && Message != Dialog && IsWindow(Message)))
            {
                Refused = true;
                if (Message && Message != Dialog)
                    PostMessageW(Message, WM_CLOSE, 0, 0);
                FPlatformProcess::Sleep(0.3f);
                PostMessageW(Dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
            }
        }
        if (IsWindow(Dialog))
        {
            Refused = true;
            PostMessageW(Dialog, WM_CLOSE, 0, 0);
        }
        Chosen = !Refused;
        if (Refused)
            Reason = !Box ? TEXT("The file dialog has no file name box to type in.")
                          : TEXT("The file dialog did not take ") + File + TEXT(". Is the file there?");
    }

    FString Title, File, Reason;
    TFuture<void> Done;
    TAtomic<bool> GiveUp{false};
    bool Chosen = false;
};
#endif

TSharedPtr<SWindow> WindowTitled(const FString& Title)
{
    TArray<TSharedRef<SWindow>> Windows;
    FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
    for (const TSharedRef<SWindow>& Window : Windows)
        if (Window->GetTitle().ToString() == Title)
            return Window;
    return nullptr;
}

TSharedPtr<SWidget> ButtonIn(const TSharedRef<SWidget>& Root, const FString& Caption)
{
    return Find(Root, [&](const TSharedRef<SWidget>& Widget)
        { return Widget->GetType() == FName(TEXT("SButton")) && Text(Widget, Caption).IsValid(); });
}

// Every /Game package a manifest names, in any of its values.
void ManifestPackages(const TSharedPtr<FJsonValue>& Value, TSet<FString>& Out)
{
    if (!Value.IsValid())
        return;
    if (Value->Type == EJson::String)
    {
        FString Path = Value->AsString();
        if (!Path.StartsWith(TEXT("/Game/")))
            return;
        FString Package;
        if (Path.Split(TEXT("."), &Package, nullptr))
            Path = Package;
        Out.Add(Path);
    }
    else if (Value->Type == EJson::Array)
        for (const auto& Item : Value->AsArray())
            ManifestPackages(Item, Out);
    else if (Value->Type == EJson::Object)
        for (const auto& Pair : Value->AsObject()->Values)
        {
            ManifestPackages(MakeShared<FJsonValueString>(Pair.Key), Out);
            ManifestPackages(Pair.Value, Out);
        }
}

class FWalkthroughStage final : public FFlowStage
{
public:
    explicit FWalkthroughStage(FFlow& InFlow)
        : FFlowStage(InFlow), Earlier(InFlow.Stage == TEXT("WalkthroughEarlierProject")),
          Folder(FPaths::GetPath(InFlow.Out)), Game(Variable(TEXT("P4_KADAN_WALK_GAME"))),
          Retoc(Variable(TEXT("P4_KADAN_WALK_RETOC"))), Repak(Variable(TEXT("P4_KADAN_WALK_REPAK"))),
          Map(Variable(TEXT("P4_KADAN_WALK_MAP")))
    {
        if (Map.IsEmpty())
            Map = DefaultMap;
        ShortName = FPackageName::GetShortName(Map);
        GLog->AddOutputDevice(&LogErrors);
    }

    void AddSteps() override;

    void Finish() override;

    void Stop() override;

    FString QualificationScope() const override
    {
        return TEXT("Editor steps on the real Kadan panel; nothing was run in the game");
    }

private:
    const bool Earlier;
    const FString Folder, Game, Retoc, Repak;
    FString Map, ShortName, ModName, ModId, State = TEXT("running");
    TArray<FWalkStep> Records;
    FStepLogErrors LogErrors;
    TWeakPtr<SWindow> Dialog;
    FDelegateHandle RestartTick, ShutdownHandle;
    double PartBegan = 0, PictureTried = 0;
    // The step that is running. The flow's own index has moved past a step by the time its failure is recorded.
    int32 Current = 0;
    int32 Settle = 0, RestartTicks = 0, ResumeAt = 0;
    bool SetupNotNeeded = false, RestartAnswered = false, Restarting = false, LevelHadLighting = false;
    // The files Unreal reported saving during the last Save Current Level. A file's time cannot tell: Unreal rounds
    // it to the second, and two saves of the walkthrough can fall in one.
    TArray<FString> SavedFiles;
    FString Started = FDateTime::UtcNow().ToIso8601();

    void Step(const TCHAR* Id, const TCHAR* Title, EView View, TArray<TFunction<bool()>> Parts, double Limit = 60);

    bool Run(int32 Number);

    // Saves the step's picture. Empty when it was saved, else why it was not.
    FString Capture(int32 Number);

    void Write();

    void Resume();

    // The step that is running fails. Always true, as a step's part returns when it is over.
    bool Fail(const FString& Why)
    {
        Flow.Fail(Why);
        return true;
    }

    void Note(const FString& Line)
    {
        if (Current < Records.Num() && !Line.IsEmpty())
            Records[Current].Notes.Add(Line);
    }

    // Whether the part that is running has waited this long.
    bool Waited(double Seconds) const
    {
        return FPlatformTime::Seconds() - PartBegan > Seconds;
    }

    FString Status()
    {
        return Flow.TaggedText(TEXT("KadanStatus"));
    }

    bool ClickTagged(const TCHAR* Tag)
    {
        const TSharedPtr<SWidget> Widget = Flow.Tagged(Tag);
        return Flow.Prepare(Widget) && Widget->IsEnabled() && Flow.MouseClick(Widget);
    }

    UWorld* World() const
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    bool OpenPanel();
    bool SizePanel();
    bool ChooseTool(bool IsRepak);
    bool GameRead();
    bool SetUpProject();
    bool SetUpDone();
    void RestartRequested();
    bool ProjectReady();
    bool TickMap();
    bool Reviewed();
    bool Imported();
    bool OpenMap();
    bool SaveLevel();
    bool LevelSaved();
    bool AddActor();
    bool ActorKept();
    bool NameMod();
    bool SaveMod();
    bool WhatShips();
    bool CheckExport();
};

void FWalkthroughStage::Step(const TCHAR* Id, const TCHAR* Title, EView View, TArray<TFunction<bool()>> Parts, double Limit)
{
    const int32 Number = Records.Num();
    FWalkStep Record;
    Record.Id = Id;
    Record.Title = Title;
    Record.View = View;
    Record.Parts = MoveTemp(Parts);
    Records.Add(MoveTemp(Record));
    // Time beside the step's own for its picture's tries.
    Flow.Add(Title, [this, Number]() { return Run(Number); }, Limit + PictureSeconds + 10);
}

// One call per frame while the step is the flow's. A part that returns true is over; the step is over after its
// last part and a few frames for the windows to draw what the picture then shows.
bool FWalkthroughStage::Run(int32 Number)
{
    FWalkStep& Record = Records[Number];
    if (Restarting)
        return true;
    Current = Number;
    if (!Record.Began)
    {
        Record.Began = PartBegan = FPlatformTime::Seconds();
        Record.Result = TEXT("running");
        LogErrors.Take();
        Write();
    }
    if (Record.Part < Record.Parts.Num())
    {
        if (!Record.Parts[Record.Part]() || Flow.Failed || Restarting)
            return Flow.Failed || Restarting;
        ++Record.Part;
        PartBegan = FPlatformTime::Seconds();
        Flow.Phase = 0;
        Flow.Prepared.Reset();
        Flow.ReadyFrame = Flow.Frame + 2;
        if (Record.Part == Record.Parts.Num())
        {
            Settle = Flow.Frame + (Record.View == EView::Editor ? 20 : 6);
            if (Record.View == EView::Editor && GEditor)
                GEditor->RedrawLevelEditingViewports(true);
        }
        return false;
    }
    if (Flow.Frame < Settle)
        return false;
    // A picture is the step's record, not its result: one the editor will not draw is tried again for a while, then
    // the step goes on without it and says why.
    if (Record.Picture.IsEmpty())
    {
        const FString Why = Capture(Number);
        if (!Why.IsEmpty())
        {
            if (!PictureTried)
                PictureTried = FPlatformTime::Seconds();
            if (FPlatformTime::Seconds() - PictureTried < PictureSeconds)
            {
                Flow.ReadyFrame = Flow.Frame + 10;
                return false;
            }
            Record.Notes.Add(TEXT("No picture: ") + Why);
            Flow.Test.AddWarning(TEXT("Walkthrough step ") + Record.Id + TEXT(" has no picture: ") + Why);
        }
    }
    PictureTried = 0;
    Record.Seconds = FPlatformTime::Seconds() - Record.Began;
    Record.LogErrors = LogErrors.Take();
    Record.Result = TEXT("passed");
    Write();
    return true;
}

FString FWalkthroughStage::Capture(int32 Number)
{
    FWalkStep& Record = Records[Number];
    TSharedPtr<SWidget> Content;
    if (Record.View == EView::Dialog)
        Content = Dialog.Pin();
    else if (Record.View == EView::Editor)
        Content = FGlobalTabmanager::Get()->GetRootWindow();
    if (!Content)
        if (const TSharedPtr<SDockTab> Panel = Flow.Tab())
            Content = Panel->GetContent();
    if (!Content)
        Content = FGlobalTabmanager::Get()->GetRootWindow();
    if (!Content)
        return TEXT("the editor has no window to take a picture of.");
    const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Content.ToSharedRef());
    if (!Window)
        return TEXT("what the step shows is in no window of the editor.");
    // Slate draws no minimized window, and on Windows a minimized main window hides the windows that belong to it.
    bool Restored = false;
    for (const TSharedPtr<SWindow>& Shown : {FGlobalTabmanager::Get()->GetRootWindow(), Window})
        if (Shown && Shown->IsWindowMinimized())
        {
            Shown->Restore();
            Restored = true;
        }
    if (Restored)
        return TEXT("its window was minimized.");
    if (!Window->IsVisible())
        return TEXT("its window is hidden.");
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!FSlateApplication::Get().TakeScreenshot(Content.ToSharedRef(), Pixels, Size) || Size.X < 100 || Size.Y < 100)
        return FString::Printf(TEXT("the editor drew no picture of its %.0f by %.0f window titled %s."),
            Window->GetSizeInScreen().X, Window->GetSizeInScreen().Y, *Window->GetTitle().ToString());
    for (FColor& Pixel : Pixels)
        Pixel.A = 255;
    int32 Width = Size.X, Height = Size.Y;
    // A main window wider than two screens of the page at half size: its text still reads, and the page stays small.
    if (Width > 2600)
    {
        TArray<FColor> Small;
        FImageUtils::ImageResize(Width, Height, Pixels, Width / 2, Height / 2, Small, false);
        Pixels = MoveTemp(Small);
        Width /= 2;
        Height /= 2;
    }
    TArray<uint8> Png;
    FImageUtils::CompressImageArray(Width, Height, Pixels, Png);
    const FString Name = FString::Printf(TEXT("%02d-%s.png"), Number + 1, *Record.Id);
    if (!FFileHelper::SaveArrayToFile(Png, *(Folder / Name)))
        return TEXT("Kadan could not write ") + (Folder / Name) + TEXT(".");
    Record.Picture = Name;
    return FString();
}

void FWalkthroughStage::Write()
{
    auto Document = MakeShared<FJsonObject>();
    Document->SetStringField(TEXT("walkthrough"), Earlier ? TEXT("earlier-project") : TEXT("first-hour"));
    Document->SetStringField(TEXT("state"), State);
    Document->SetNumberField(TEXT("resume_at"), ResumeAt);
    Document->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Document->SetStringField(TEXT("game"), Game);
    Document->SetStringField(TEXT("map"), Map);
    Document->SetStringField(TEXT("mod_name"), ModName);
    Document->SetStringField(TEXT("mod_id"), ModId);
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Kadan"));
    Document->SetStringField(TEXT("kadan"), Plugin ? Plugin->GetDescriptor().VersionName : FString());
    Document->SetStringField(TEXT("started"), Started);
    Document->SetStringField(TEXT("written"), FDateTime::UtcNow().ToIso8601());
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 Number = 0; Number < Records.Num(); ++Number)
    {
        const FWalkStep& Record = Records[Number];
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("number"), Number + 1);
        Row->SetStringField(TEXT("id"), Record.Id);
        Row->SetStringField(TEXT("title"), Record.Title);
        Row->SetStringField(TEXT("result"), Record.Result);
        Row->SetNumberField(TEXT("seconds"), Record.Seconds);
        Row->SetStringField(TEXT("picture"), Record.Picture);
        Row->SetStringField(TEXT("error"), Record.Error);
        TArray<TSharedPtr<FJsonValue>> Notes, Errors;
        for (const FString& Line : Record.Notes)
            Notes.Add(MakeShared<FJsonValueString>(Line));
        for (const FString& Line : Record.LogErrors)
            Errors.Add(MakeShared<FJsonValueString>(Line));
        Row->SetArrayField(TEXT("notes"), Notes);
        Row->SetArrayField(TEXT("log_errors"), Errors);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Document->SetArrayField(TEXT("steps"), Rows);
    FString Json;
    FJsonSerializer::Serialize(Document, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*Folder, true);
    // Written beside the file first: the runner reads it while the editor runs.
    const FString File = Folder / TEXT("steps.json");
    if (!FFileHelper::SaveStringToFile(Json, *(File + TEXT(".part")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*File, *(File + TEXT(".part")), true, true))
        Flow.Fail(TEXT("The walkthrough could not write ") + File);
}

// A run that stopped for Kadan's restart of Unreal Editor goes on at the step after it, with the earlier
// session's steps as that session recorded them.
void FWalkthroughStage::Resume()
{
    FString Json;
    TSharedPtr<FJsonObject> Document;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    FString Before;
    if (!FFileHelper::LoadFileToString(Json, *(Folder / TEXT("steps.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Document) || !Document ||
        !Document->TryGetStringField(TEXT("state"), Before) || Before != TEXT("restarting") ||
        Document->GetStringField(TEXT("walkthrough")) != (Earlier ? TEXT("earlier-project") : TEXT("first-hour")) ||
        !Document->TryGetArrayField(TEXT("steps"), Rows) || Rows->Num() != Records.Num())
        return;
    const int32 At = int32(Document->GetNumberField(TEXT("resume_at")));
    if (At <= 0 || At >= Records.Num())
        return;
    for (int32 Number = 0; Number < At; ++Number)
    {
        const TSharedPtr<FJsonObject> Row = (*Rows)[Number]->AsObject();
        FWalkStep& Record = Records[Number];
        if (!Row || Row->GetStringField(TEXT("id")) != Record.Id)
            return;
        Record.Result = Row->GetStringField(TEXT("result"));
        Record.Seconds = Row->GetNumberField(TEXT("seconds"));
        Record.Picture = Row->GetStringField(TEXT("picture"));
        Record.Error = Row->GetStringField(TEXT("error"));
        for (const auto& Line : Row->GetArrayField(TEXT("notes")))
            Record.Notes.Add(Line->AsString());
        for (const auto& Line : Row->GetArrayField(TEXT("log_errors")))
            Record.LogErrors.Add(Line->AsString());
    }
    Started = Document->GetStringField(TEXT("started"));
    Flow.Index = At;
    Flow.Test.AddInfo(FString::Printf(TEXT("The walkthrough goes on at step %d after the editor's restart."), At + 1));
}

void FWalkthroughStage::Finish()
{
    if (Restarting)
        return;
    if (Flow.Failed && Current < Records.Num())
    {
        FWalkStep& Record = Records[Current];
        Record.Result = TEXT("failed");
        Record.Error = Flow.FailureReason();
        Record.Seconds = Record.Began ? FPlatformTime::Seconds() - Record.Began : 0;
        Record.LogErrors = LogErrors.Take();
        Record.Picture.Reset();
        const FString Why = Capture(Current);
        if (!Why.IsEmpty())
            Record.Notes.Add(TEXT("No picture: ") + Why);
    }
    State = Flow.Failed ? TEXT("failed") : TEXT("passed");
    Write();
}

void FWalkthroughStage::Stop()
{
    GLog->RemoveOutputDevice(&LogErrors);
    if (RestartTick.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnPostTick().Remove(RestartTick);
    RestartTick.Reset();
    FEditorDelegates::OnShutdownPostPackagesSaved.Remove(ShutdownHandle);
    ShutdownHandle.Reset();
}

bool FWalkthroughStage::OpenPanel()
{
    return Flow.Menu(TEXT("Import from game"), TEXT("Kadan"));
}

// The panel's own window at one size, so the pictures of two runs can be laid side by side.
bool FWalkthroughStage::SizePanel()
{
    const TSharedPtr<SDockTab> Panel = Flow.Tab();
    const TSharedPtr<SWindow> Window = Panel ? FSlateApplication::Get().FindWidgetWindow(Panel.ToSharedRef()) : nullptr;
    if (!Window)
        return false;
    if (Window != FGlobalTabmanager::Get()->GetRootWindow() && Flow.Phase == 0)
    {
        Window->Resize(FVector2D(760, 940));
        Flow.Phase = 1;
        Flow.ReadyFrame = Flow.Frame + 4;
        return false;
    }
    return true;
}

bool FWalkthroughStage::ChooseTool(bool IsRepak)
{
    const TCHAR* Name = IsRepak ? TEXT("repak") : TEXT("retoc");
    const TSharedPtr<SDockTab> Panel = Flow.Tab();
    const TSharedPtr<SWidget> Button =
        Panel ? ButtonIn(Panel->GetContent(), FString(TEXT("Choose ")) + Name) : TSharedPtr<SWidget>();
    if (!Button)
    {
        // The Import page asks only for a tool it has not found.
        if (!Waited(1))
            return false;
        Note(FString(TEXT("Kadan has ")) + Name + TEXT(".exe and did not ask for it."));
        return true;
    }
    const FString& File = IsRepak ? Repak : Retoc;
    if (File.IsEmpty())
        return Fail(FString(TEXT("Kadan asks for ")) + Name + TEXT(".exe, and the walkthrough was given none."));
#if PLATFORM_WINDOWS
    if (!Flow.Prepare(Button) || !Button->IsEnabled())
        return false;
    FFileDialogAnswer Answer(FString(TEXT("Choose ")) + Name + TEXT(".exe"), File);
    // The click returns when the dialog has closed.
    const bool Clicked = Flow.MouseClick(Button);
    FString Why;
    if (!Answer.Finish(Clicked, Why))
        return Clicked ? Fail(FString(TEXT("Choose ")) + Name + TEXT(": ") + Why) : false;
    Note(TEXT("Chose ") + File + TEXT(" in the system's file dialog."));
    return true;
#else
    return Fail(TEXT("The walkthrough answers the system's file dialog on Windows only."));
#endif
}

bool FWalkthroughStage::GameRead()
{
    const FString Shown = Flow.TaggedText(TEXT("KadanGameState"));
    if (Shown.EndsWith(TEXT("maps and assets in the game")) && FCString::Atoi(*Shown) > 1000)
    {
        Note(Shown);
        return true;
    }
    if (Flow.Tagged(TEXT("KadanMissingTools")).IsValid() && Waited(5))
        return Fail(TEXT("The Import page still asks for a tool. ") + Status());
    // The panel is idle here: a folder it could not read stays unread, and the notice says why.
    return Waited(20) ? Fail(TEXT("Kadan did not read the game folder. ") + Shown + TEXT(" ") + Status()) : false;
}

bool FWalkthroughStage::SetUpProject()
{
    if (!Flow.Tagged(TEXT("KadanProjectSetup")))
    {
        SetupNotNeeded = true;
        Note(TEXT("The project already has the game's settings, so the Import page shows no setup."));
        return true;
    }
    const TSharedPtr<SDockTab> Panel = Flow.Tab();
    const TSharedPtr<SWidget> Button = ButtonIn(Panel->GetContent(), TEXT("Set up project"));
    if (!Button || !Button->IsEnabled())
        return Waited(10) ? Fail(TEXT("The Import page does not offer Set up project. ") +
                                   Flow.TaggedText(TEXT("KadanSetupState")) + TEXT(" ") + Status())
                          : false;
    if (!RestartTick.IsValid())
    {
        // Setup ends in Unreal's question whether to restart, which stops the editor's loop: answer it from
        // Slate's own tick, after the panel behind it has drawn what setup said.
        RestartTick = FSlateApplication::Get().OnPostTick().AddLambda(
            [this](float)
            {
                const TSharedPtr<SWindow> Question = FSlateApplication::Get().GetActiveModalWindow();
                if (RestartAnswered || !Question || Question->GetTitle().ToString() != TEXT("Switch Project"))
                    return;
                if (++RestartTicks < 12)
                    return;
                if (RestartTicks == 12)
                {
                    Note(TEXT("Setup said: ") + Status());
                    Capture(Current);
                    return;
                }
                const TSharedPtr<SWidget> Yes = ButtonIn(Question.ToSharedRef(), TEXT("Yes"));
                if (!Yes)
                    return;
                RestartAnswered = Flow.MouseClick(Yes);
                // A click the button did not take is tried again on later ticks, then the button's own Enter.
                if (!RestartAnswered && RestartTicks > 240)
                {
                    const TSharedPtr<SButton> Button = StaticCastSharedPtr<SButton>(Yes);
                    const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), 0, false, 13, 0);
                    RestartAnswered = Button->OnKeyDown(Button->GetCachedGeometry(), Enter).IsEventHandled() &&
                        Button->OnKeyUp(Button->GetCachedGeometry(), Enter).IsEventHandled();
                }
                if (RestartAnswered)
                    Note(TEXT("Answered Yes to Unreal's question whether to restart the editor."));
            });
        ShutdownHandle = FEditorDelegates::OnShutdownPostPackagesSaved.AddLambda([this]() { RestartRequested(); });
    }
    return Flow.Click(TEXT("Set up project"));
}

// Kadan asked Unreal to restart, which closes this editor and starts another with this one's command line, tests
// and all. The runner starts the next session itself, so the editor's own start of one is taken back.
void FWalkthroughStage::RestartRequested()
{
    if (Restarting || FUnrealEdMisc::Get().GetPendingProjectName().IsEmpty() || Current >= Records.Num())
        return;
    FUnrealEdMisc::Get().ClearPendingProjectName();
    FWalkStep& Record = Records[Current];
    Record.Notes.Add(TEXT("Kadan saved the project's setup and closed Unreal Editor to restart it. The runner starts "
                          "the editor again."));
    if (Record.Picture.IsEmpty())
        Capture(Current);
    Record.Seconds = FPlatformTime::Seconds() - Record.Began;
    Record.LogErrors = LogErrors.Take();
    Record.Result = TEXT("passed");
    Restarting = true;
    State = TEXT("restarting");
    ResumeAt = Current + 1;
    Write();
    Flow.Stopped = true;
}

bool FWalkthroughStage::SetUpDone()
{
    if (SetupNotNeeded)
        return true;
    RestartRequested();
    if (Restarting)
        return true;
    FString Why;
    if (P4KadanProjectReady(Why) && !P4KadanProjectRestartRequired())
    {
        Note(TEXT("Setup needed no restart. ") + Status());
        return true;
    }
    if (!Waited(15))
        return false;
    return Fail((P4KadanProjectRestartRequired() ? TEXT("Setup is saved, but Unreal Editor was not restarted. ")
                                                 : TEXT("Project setup did not finish. ")) +
        Status() + TEXT(" ") + Why);
}

bool FWalkthroughStage::ProjectReady()
{
    FString Why;
    if (!P4KadanProjectReady(Why))
        return Fail(TEXT("The project is not ready for an import. ") + Why);
    if (Flow.Tagged(TEXT("KadanProjectSetup")))
        return Fail(TEXT("The Import page still shows project setup. ") + Flow.TaggedText(TEXT("KadanSetupState")));
    return true;
}

bool FWalkthroughStage::TickMap()
{
    const TSharedPtr<SWidget> List = Flow.Tagged(TEXT("KadanSourceList"));
    const TSharedPtr<SWidget> Name = List ? Text(List.ToSharedRef(), ShortName) : TSharedPtr<SWidget>();
    if (!Name)
        return Waited(15) ? Fail(FString::Printf(TEXT("The search shows no map named %s among its %d rows."),
                                     *ShortName, Flow.SourceCount()))
                          : false;
    // The row's own box: the nearest widget around the name that holds one.
    TSharedPtr<SWidget> Row = Name, Box;
    while (Row && Row != List && !Box)
    {
        Row = Row->GetParentWidget();
        if (Row)
            Box = Find(Row.ToSharedRef(),
                [](const TSharedRef<SWidget>& Widget) { return Widget->GetType() == FName(TEXT("SCheckBox")); });
    }
    if (!Box)
        return Waited(15) ? Fail(TEXT("The map's row has no box to tick.")) : false;
    if (!Flow.Prepare(Box))
        return false;
    const TSharedPtr<SCheckBox> Check = StaticCastSharedPtr<SCheckBox>(Box);
    return Check->IsChecked() || (Flow.MouseClick(Check) && Check->IsChecked());
}

bool FWalkthroughStage::Reviewed()
{
    const FString Line = Flow.TaggedText(TEXT("KadanImportReview"));
    if (Line.Contains(TEXT("not counted")) || Status().StartsWith(TEXT("Some of the selection can't be imported")))
        return Fail(TEXT("The import's count stopped. ") + Line + TEXT(" ") + Status());
    if (!Line.Contains(TEXT("more they need")))
        return Waited(1700) ? Fail(TEXT("Kadan did not count what the map needs. ") + Line) : false;
    const TSharedPtr<SWidget> Import = Flow.Tagged(TEXT("KadanImport"));
    if (!Import || !Import->IsEnabled())
        return Fail(TEXT("Import is off for a counted selection. ") + Line);
    Note(Line);
    return true;
}

bool FWalkthroughStage::Imported()
{
    const FString Line = Status();
    Note(Line);
    if (!FPackageName::DoesPackageExist(Map))
        return Fail(TEXT("The map is not in the project after the import. ") + Line);
    if (!Line.StartsWith(TEXT("Kadan imported ")))
        return Fail(TEXT("The import did not report what it imported. ") + Line);
    return true;
}

bool FWalkthroughStage::OpenMap()
{
    if (!FPackageName::DoesPackageExist(Map))
        return Fail(TEXT("The project has no ") + Map + TEXT("."));
    // As a double click in the Content Browser opens a map.
    GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Map + TEXT(".") + ShortName);
    UWorld* Open = World();
    if (!Open || Open->GetOutermost()->GetName() != Map)
        return Fail(TEXT("Unreal did not open ") + Map + TEXT("."));
    int32 Actors = 0;
    for (TActorIterator<AActor> It(Open); It; ++It)
        ++Actors;
    Note(FString::Printf(TEXT("%s is open: %d sublevels, %d actors, built lighting %s."), *ShortName,
        Open->GetStreamingLevels().Num(), Actors, Open->PersistentLevel->MapBuildData ? TEXT("there") : TEXT("absent")));
    return true;
}

// File > Save Current Level, through the level editor's own command.
bool FWalkthroughStage::SaveLevel()
{
    UWorld* Open = World();
    ULevel* Level = Open ? Open->GetCurrentLevel() : nullptr;
    if (!Level)
        return Fail(TEXT("No level is open to save."));
    LevelHadLighting = Level->MapBuildData != nullptr;
    SavedFiles.Reset();
    const FDelegateHandle Watch = UPackage::PackageSavedEvent.AddLambda(
        [this](const FString& File, UObject*) { SavedFiles.Add(FPaths::ConvertRelativePathToFull(File)); });
    FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    const bool Ran =
        LevelEditor.GetGlobalLevelEditorActions()->ExecuteAction(FLevelEditorCommands::Get().Save.ToSharedRef());
    UPackage::PackageSavedEvent.Remove(Watch);
    if (!Ran)
        return Fail(TEXT("The level editor did not run Save Current Level."));
    return true;
}

// A frame later, as the editor is when the person looks at it again.
bool FWalkthroughStage::LevelSaved()
{
    UWorld* Open = World();
    ULevel* Level = Open ? Open->GetCurrentLevel() : nullptr;
    if (!Level || Open->GetOutermost()->GetName() != Map)
        return Fail(TEXT("The map is no longer open after Save Current Level."));
    const FString Package = Level->GetOutermost()->GetName();
    const FString File = FPaths::ConvertRelativePathToFull(
        FPackageName::LongPackageNameToFilename(Package, FPackageName::GetMapPackageExtension()));
    if (!SavedFiles.ContainsByPredicate([&](const FString& Saved) { return FPaths::IsSamePath(Saved, File); }))
        return Fail(TEXT("Save Current Level did not write ") + File + TEXT(". Unreal saved: ") +
            (SavedFiles.Num() ? FString::Join(SavedFiles, TEXT(", ")) : FString(TEXT("nothing"))) + TEXT("."));
    if (!IFileManager::Get().FileExists(*File))
        return Fail(TEXT("The saved level's file is not there: ") + File);
    if (Level->GetOutermost()->IsDirty())
        return Fail(TEXT("The level still has unsaved changes after Save Current Level."));
    if (LevelHadLighting && !Level->MapBuildData)
        return Fail(TEXT("The level lost its built lighting when it was saved."));
    Note(FString::Printf(TEXT("Saved %s. Built lighting %s."), *FPackageName::GetShortName(Package),
        Level->MapBuildData ? TEXT("is still there") : TEXT("was absent before and after")));
    return true;
}

// One more of a mesh the map already has, a little above the first one found: a first edit that needs no asset
// of the creator's own.
bool FWalkthroughStage::AddActor()
{
    UWorld* Open = World();
    if (!Open || Open->GetOutermost()->GetName() != Map)
        return Fail(TEXT("The map is not open."));
    UStaticMesh* Mesh = nullptr;
    FVector Where = FVector::ZeroVector;
    float Smallest = 0;
    // The game's scenery is actors of its own classes, so the mesh is taken from an actor's mesh component: the
    // smallest one the map shows, a prop rather than the cave's walls or its sky.
    for (TActorIterator<AActor> It(Open); It; ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Components(*It);
        for (const UStaticMeshComponent* Component : Components)
        {
            UStaticMesh* Candidate = Component->GetStaticMesh();
            if (!Candidate || !Candidate->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
                continue;
            const float Radius = Candidate->GetBounds().SphereRadius;
            if (Radius < 10 || (Mesh && (Radius > Smallest || (Radius == Smallest && Candidate->GetName() >= Mesh->GetName()))))
                continue;
            Mesh = Candidate;
            Smallest = Radius;
            Where = Component->GetComponentLocation() + FVector(0, 0, Radius * 2 + 50);
        }
    }
    if (!Mesh)
        return Fail(TEXT("No actor of the map has a static mesh of the game's to place again."));
    AStaticMeshActor* Added = Cast<AStaticMeshActor>(
        GEditor->AddActor(Open->PersistentLevel, AStaticMeshActor::StaticClass(), FTransform(Where)));
    if (!Added)
        return Fail(TEXT("Unreal did not place a static mesh actor."));
    Added->GetStaticMeshComponent()->SetStaticMesh(Mesh);
    Added->SetActorLabel(AddedActor);
    Added->MarkPackageDirty();
    GEditor->SelectNone(false, true);
    GEditor->SelectActor(Added, true, true);
    GEditor->MoveViewportCamerasToActor(*Added, false);
    Note(FString::Printf(TEXT("Placed %s with the game's mesh %s in %s."), AddedActor, *Mesh->GetName(), *ShortName));
    return true;
}

bool FWalkthroughStage::ActorKept()
{
    UWorld* Open = World();
    if (!Open || Open->GetOutermost()->GetName() != Map)
        return Fail(TEXT("The map is not open."));
    for (AActor* Actor : Open->PersistentLevel->Actors)
        if (Actor && Actor->GetActorLabel() == AddedActor)
        {
            Note(FString(AddedActor) + TEXT(" is in the saved level."));
            return true;
        }
    return Fail(TEXT("The added actor is gone from the saved level."));
}

bool FWalkthroughStage::NameMod()
{
    const TSharedPtr<SWindow> Window = WindowTitled(TEXT("New mod"));
    if (!Window)
        return Waited(20) ? Fail(TEXT("New mod opened no window.")) : false;
    Dialog = Window;
    const auto Tagged = [&](const TCHAR* Tag)
    {
        return StaticCastSharedPtr<SEditableTextBox>(Find(Window.ToSharedRef(),
            [&](const TSharedRef<SWidget>& Widget) { return Widget->GetTag() == FName(Tag); }));
    };
    if (!Flow.TypeInto(Tagged(TEXT("KadanField.Name")), TEXT("Name"), ModName))
        return false;
    const TSharedPtr<SEditableTextBox> Id = Tagged(TEXT("KadanField.ID"));
    if (!Id || Id->GetText().IsEmpty())
        return Fail(TEXT("The mod's name did not suggest an ID."));
    ModId = Id->GetText().ToString();
    Note(TEXT("Named the mod ") + ModName + TEXT("; Kadan suggested the ID ") + ModId + TEXT("."));
    return true;
}

bool FWalkthroughStage::SaveMod()
{
    if (const TSharedPtr<SWindow> Window = WindowTitled(TEXT("New mod")))
    {
        if (Flow.Phase == 0 && Flow.MouseClick(ButtonIn(Window.ToSharedRef(), TEXT("Save"))))
            Flow.Phase = 1;
        return Waited(20) ? Fail(TEXT("Save did not close the New mod window. ") + Status()) : false;
    }
    if (!Flow.VisibleText(ModName + TEXT("  1.0.0")))
        return Waited(20) ? Fail(TEXT("The Mods page does not show the new mod. ") + Status()) : false;
    Note(Status());
    return true;
}

bool FWalkthroughStage::WhatShips()
{
    const TSharedPtr<SWidget> Roots = Flow.Tagged(TEXT("KadanSelectionRoots"));
    if (!Roots || !Text(Roots.ToSharedRef(), ShortName))
        return Waited(20) ? Fail(TEXT("Add selected did not put the map in the mod. ") + Status()) : false;
    const FString Line = Flow.TaggedText(TEXT("KadanExportReview"));
    if (Line.IsEmpty() || Line.StartsWith(TEXT("Checking")) || Line.StartsWith(TEXT("Add content")))
        return Waited(1700) ? Fail(TEXT("What ships never filled. ") + Line) : false;
    const TSharedPtr<SWidget> Export = Flow.Tagged(TEXT("KadanExport"));
    if (Line.Contains(TEXT("to fix")) || Line.Contains(TEXT("Output Log")) || !Export || !Export->IsEnabled())
        return Fail(TEXT("The mod can't ship as it is. What ships: ") + Line + TEXT(". ") + Status());
    Note(TEXT("What ships: ") + Line);
    return true;
}

// Reads what was published, apart from the exporter: the manifest, the three container files, and the
// container's own list of packages, which has to hold every package of the mod's own that the manifest names.
bool FWalkthroughStage::CheckExport()
{
    const FString Root = Flow.ExportFolder / ModId / TEXT("romfs/tsuchi/mods") / ModId;
    FString Json, Error;
    TSharedPtr<FJsonObject> Manifest;
    if (!FFileHelper::LoadFileToString(Json, *(Root / TEXT("mod.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) || !Manifest)
        return Fail(TEXT("The export published no readable ") + (Root / TEXT("mod.json")) + TEXT("."));
    if (Manifest->GetStringField(TEXT("id")) != ModId || Manifest->GetStringField(TEXT("name")) != ModName ||
        Manifest->GetStringField(TEXT("version")) != TEXT("1.0.0"))
        return Fail(TEXT("The published manifest is not this mod's: ") + Json.Left(300));
    FP4ModDescriptor Mod;
    Mod.Id = ModId;
    Mod.Name = ModName;
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    if (!P4ModDerivedNames(Mod, Names, Error))
        return Fail(Error);
    const FString Base = Root / (Names.Container + TEXT("_P"));
    for (const TCHAR* Extension : {TEXT(".pak"), TEXT(".utoc"), TEXT(".ucas")})
        if (IFileManager::Get().FileSize(*(Base + Extension)) <= 0)
            return Fail(TEXT("The export has no ") + FPaths::GetCleanFilename(Base + Extension) + TEXT("."));
    FP4ExportContainerTools Tools;
    FString Paks;
    GConfig->GetString(TEXT("Kadan"), TEXT("Retoc"), Tools.Retoc, GEditorPerProjectIni);
    GConfig->GetString(TEXT("Kadan"), TEXT("Repak"), Tools.Repak, GEditorPerProjectIni);
    GConfig->GetString(TEXT("Kadan"), TEXT("GamePaks"), Paks, GEditorPerProjectIni);
    FP4ExportGameInventory Inventory;
    FP4ExportContainerCensus Census;
    if (!P4ReadExportGameInventory(Tools, Paks, Folder / TEXT("inspection"), Inventory, Error) ||
        !P4InspectExportContainer(Tools, Base + TEXT(".utoc"), Folder / TEXT("inspection"), Inventory, Census, Error))
        return Fail(TEXT("The export's container could not be read. ") + Error);
    TSet<FString> Named;
    ManifestPackages(MakeShared<FJsonValueObject>(Manifest), Named);
    TArray<FString> Own, Absent;
    for (const FString& Package : Named)
        if (!Inventory.Contains(FName(*Package)))
        {
            Own.Add(Package);
            if (!Census.PackageIds.Contains(P4ExportPackageId(Package)))
                Absent.Add(Package);
        }
    Own.Sort();
    if (Absent.Num())
        return Fail(TEXT("The manifest names packages the container does not hold: ") + FString::Join(Absent, TEXT(", ")));
    if (!Own.Num())
        return Fail(TEXT("The manifest names no package of the mod's own for the added actor: ") + Json.Left(600));
    // What ships, as the panel lists it after the export: with every row drawn, the rows it calls yours are the
    // packages the container holds.
    const TSharedPtr<SWidget> Shipping = Flow.Tagged(TEXT("KadanShippingList"));
    int32 Yours = 0, Rows = 0;
    if (Shipping)
        Find(Shipping.ToSharedRef(),
            [&](const TSharedRef<SWidget>& Widget)
            {
                if (Widget->GetType() == FName(TEXT("STextBlock")))
                {
                    const FString Tag = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
                    Yours += Tag == TEXT("yours") ? 1 : 0;
                    Rows += Tag == TEXT("yours") || Tag == TEXT("your changes") ? 1 : 0;
                }
                return false;
            });
    const int32 Listed = Shipping
        ? StaticCastSharedPtr<SListView<TSharedPtr<FP4ExportSelectionRow>>>(Shipping)->GetNumItemsBeingObserved()
        : 0;
    if (Rows && Rows == Listed && Yours != Census.PackageIds.Num())
        return Fail(FString::Printf(TEXT("What ships lists %d packages as yours, and the container holds %d."), Yours,
            Census.PackageIds.Num()));
    if (Rows && Rows == Listed)
        Note(FString::Printf(TEXT("What ships lists %d packages as yours, as many as the container holds."), Yours));
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *(Flow.ExportFolder / ModId), TEXT("*"), true, false);
    Note(FString::Printf(TEXT("%d files published; the container holds %d packages."), Files.Num(), Census.PackageIds.Num()));
    Note(TEXT("The mod's own packages the manifest names, all in the container: ") + FString::Join(Own, TEXT(", ")));
    return true;
}

void FWalkthroughStage::AddSteps()
{
    ModName = TEXT("First Hour ") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6).ToUpper();
    Step(TEXT("open-panel"), TEXT("Open the Kadan panel from the Kadan menu"), EView::Panel,
        {[this]() { return OpenPanel(); }, [this]() { return SizePanel(); }});
    Step(TEXT("game-folder"), TEXT("Type the game folder next to Game and press Enter"), EView::Panel,
        {[this]() { return Flow.SetField(TEXT("Game"), Game); }, [this]() { return Flow.CommitField(TEXT("Game")); },
            [this]()
            {
                Note(TEXT("Typed ") + Game + TEXT(". Kadan said: ") + Status());
                return true;
            }},
        900);
    Step(TEXT("choose-tools"), TEXT("Choose retoc and repak where the Import page asks for them"), EView::Panel,
        {[this]() { return ChooseTool(false); }, [this]() { return ChooseTool(true); }}, 900);
    Step(TEXT("read-game"), TEXT("Kadan lists the game's maps and assets"), EView::Panel,
        {[this]() { return GameRead(); }}, 900);
    Step(TEXT("set-up-project"), TEXT("Set up the project if it needs the game's settings"), EView::Panel,
        {[this]() { return SetUpProject(); }, [this]() { return SetUpDone(); }}, 1800);
    Step(TEXT("project-ready"), TEXT("The project is ready for an import"), EView::Panel,
        {[this]() { return OpenPanel(); }, [this]() { return SizePanel(); }, [this]() { return ProjectReady(); },
            [this]() { return GameRead(); }},
        900);
    if (Earlier)
        Step(TEXT("earlier-map"), TEXT("The map an earlier release imported is in the project"), EView::Panel,
            {[this]()
                {
                    if (!FPackageName::DoesPackageExist(Map))
                        return Fail(TEXT("The earlier project has no ") + Map + TEXT("."));
                    Note(Map + TEXT(" is in the project, as the earlier release imported it."));
                    return true;
                }});
    else
    {
        Step(TEXT("search-map"), TEXT("Search for the map"), EView::Panel,
            {[this]() { return Flow.SetField(TEXT("Search"), ShortName); },
                [this]()
                {
                    const TSharedPtr<SWidget> List = Flow.Tagged(TEXT("KadanSourceList"));
                    if (List && Text(List.ToSharedRef(), ShortName))
                    {
                        Note(FString::Printf(TEXT("Searched %s: %d shown."), *ShortName, Flow.SourceCount()));
                        return true;
                    }
                    return Waited(15) ? Fail(FString::Printf(TEXT("The search shows no map named %s among its %d rows."),
                                                 *ShortName, Flow.SourceCount()))
                                      : false;
                }});
        Step(TEXT("tick-map"), TEXT("Tick the map and read what it needs"), EView::Panel,
            {[this]() { return TickMap(); }, [this]() { return Reviewed(); }}, 1800);
        Step(TEXT("import-map"), TEXT("Click Import"), EView::Panel,
            {[this]() { return ClickTagged(TEXT("KadanImport")); }, [this]() { return Imported(); }}, 7200);
    }
    Step(TEXT("open-map"), TEXT("Open the map"), EView::Editor,
        {[this]() { return OpenMap(); },
            [this]() { return !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling(); }},
        3600);
    Step(TEXT("save-level"), TEXT("File > Save Current Level"), EView::Editor,
        {[this]() { return SaveLevel(); }, [this]() { return LevelSaved(); }}, 900);
    Step(TEXT("add-actor"), TEXT("Add a static mesh actor to the map"), EView::Editor,
        {[this]() { return AddActor(); }}, 300);
    Step(TEXT("save-edit"), TEXT("Save Current Level again, with the new actor"), EView::Editor,
        {[this]() { return SaveLevel(); }, [this]() { return LevelSaved(); },
            [this]() { return ActorKept(); }},
        900);
    Step(TEXT("new-mod"), TEXT("On the Mods page, click New mod and name the mod"), EView::Dialog,
        {[this]() { return ClickTagged(TEXT("KadanPage.Mods")); }, [this]() { return Flow.Click(TEXT("New mod")); },
            [this]() { return NameMod(); }},
        120);
    Step(TEXT("save-mod"), TEXT("Save the mod"), EView::Panel, {[this]() { return SaveMod(); }}, 120);
    Step(TEXT("add-to-mod"), TEXT("Select the map in the Content Browser and click Add selected"), EView::Panel,
        {[this]()
            {
                TArray<FAssetData> Found;
                FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                    .Get()
                    .GetAssetsByPackageName(FName(*Map), Found);
                if (!Found.Num())
                    return Fail(TEXT("The Content Browser does not have ") + Map + TEXT("."));
                Flow.Browser().SyncBrowserToAssets(Found);
                return true;
            },
            [this]() { return Flow.Click(TEXT("Add selected")); }, [this]() { return WhatShips(); }},
        1800);
    Step(TEXT("export-mod"), TEXT("Click Export mod"), EView::Panel,
        {[this]()
            {
                // The walkthrough only ever exports into its own folder in the project.
                const TSharedPtr<SWidget> Target = Flow.Tagged(TEXT("KadanExportTarget"));
                if (!Target || !Text(Target.ToSharedRef(), Flow.ExportFolder))
                    return Fail(TEXT("Export to is not the walkthrough's own folder ") + Flow.ExportFolder + TEXT("."));
                return true;
            },
            [this]() { return ClickTagged(TEXT("KadanExport")); },
            [this]()
            {
                const FString Line = Status();
                if (!Line.StartsWith(TEXT("Exported ") + ModName + TEXT(".")))
                    return Fail(TEXT("The export did not finish. ") + Line);
                Note(Line);
                Note(Flow.TaggedText(TEXT("KadanLastExport")));
                return true;
            }},
        7200);
    Step(TEXT("check-export"), TEXT("The export's containers hold the mod's packages"), EView::Panel,
        {[this]() { return CheckExport(); }}, 900);
    Resume();
}
} // namespace

TUniquePtr<FFlowStage> MakeWalkthroughStage(FFlow& Flow)
{
    return MakeUnique<FWalkthroughStage>(Flow);
}

// Starts a walkthrough, or says which of its inputs is missing.
bool StartWalkthrough(FAutomationTestBase& Test, const TCHAR* Stage, const TCHAR* ProjectVariable)
{
    TArray<FString> Missing;
    const FString Project = Variable(ProjectVariable);
    if (Project.IsEmpty())
        Missing.Add(FString(ProjectVariable) + TEXT(" (the project's .uproject)"));
    if (Variable(TEXT("P4_KADAN_WALK_GAME")).IsEmpty())
        Missing.Add(TEXT("P4_KADAN_WALK_GAME (the game dump's romfs or Paks folder)"));
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Kadan"));
    for (const TCHAR* Tool : {TEXT("retoc"), TEXT("repak")})
        if (Variable(*(FString(TEXT("P4_KADAN_WALK_")) + FString(Tool).ToUpper())).IsEmpty() &&
            (!Plugin || !FPaths::FileExists(Plugin->GetBaseDir() / TEXT("Tools") / (FString(Tool) + TEXT(".exe")))))
            Missing.Add(FString(TEXT("P4_KADAN_WALK_")) + FString(Tool).ToUpper() + TEXT(" (this Kadan has no ") + Tool +
                TEXT(".exe in its Tools folder)"));
    if (Missing.Num())
    {
        Test.AddWarning(FString(P4_MAPKIT_NOT_RUN_TEXT) + TEXT("The walkthrough needs ") +
            FString::Join(Missing, TEXT(", ")) + TEXT("."));
        return true;
    }
    if (!FPaths::IsSamePath(Project, FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())))
    {
        Test.AddWarning(FString(P4_MAPKIT_NOT_RUN_TEXT) + TEXT("The walkthrough was given ") + Project +
            TEXT(", and this editor has another project open."));
        return true;
    }
    if (IsRunningCommandlet() || !GEditor || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
    {
        Test.AddError(TEXT("The walkthrough needs Unreal Editor's windows, in a desktop session."));
        return false;
    }
    const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KadanWalkthrough"));
    // The mod is exported into the walkthrough's own folder, never where the project's settings pointed.
    GConfig->SetString(TEXT("Kadan"), TEXT("ExportFolder"), *(Folder / TEXT("export")), GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    P4RegisterKadanEditor();
    // A panel the editor restored from its layout read the settings at startup: a new one reads them now.
    if (const TSharedPtr<SDockTab> Restored = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("Kadan")))))
        Restored->RequestCloseTab();
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFlow>(Test, Stage, Folder / TEXT("flow")));
    return true;
}
} // namespace P4KadanCreatorFlow

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanWalkthroughFirstHourTest, "P4MapKit.Kadan.WalkthroughFirstHour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanWalkthroughFirstHourTest::RunTest(const FString&)
{
    // The steps decide the result. The Output Log's errors during a step are recorded with it in steps.json.
    bSuppressLogs = true;
    return P4KadanCreatorFlow::StartWalkthrough(*this, TEXT("WalkthroughFirstHour"), TEXT("P4_KADAN_WALK_PROJECT"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanWalkthroughEarlierProjectTest, "P4MapKit.Kadan.WalkthroughEarlierProject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanWalkthroughEarlierProjectTest::RunTest(const FString&)
{
    bSuppressLogs = true;
    return P4KadanCreatorFlow::StartWalkthrough(
        *this, TEXT("WalkthroughEarlierProject"), TEXT("P4_KADAN_WALK_EARLIER_PROJECT"));
}
#endif

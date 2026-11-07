#include "P4KadanCreatorFlow.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

TSharedPtr<SEditableTextBox> FFlow::Field(const FString& Name)
{
    const auto Widget = Tagged(TEXT("KadanField.") + Name);
    return Widget ? StaticCastSharedPtr<SEditableTextBox>(Widget) : nullptr;
}

bool FFlow::VisibleText(const FString& Value, bool Contains)
{
    const auto Panel = Tab();
    return Panel && Text(Panel->GetContent(), Value, Contains).IsValid();
}

bool FFlow::OperationBusy()
{
    const auto Panel = Tab();
    return Panel &&
        Find(Panel->GetContent(),
            [](const TSharedRef<SWidget>& Widget) { return Widget->GetType() == FName(TEXT("SThrobber")); })
            .IsValid();
}

TSharedPtr<SWidget> FFlow::WindowText(const FString& Value, bool MenuOnly)
{
    TArray<TSharedRef<SWindow>> Windows;
    FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
    for (const auto& Window : Windows)
    {
        if (MenuOnly)
        {
            auto Block = Find(Window, [&](const TSharedRef<SWidget>& Widget)
                { return Widget->GetType() == FName(TEXT("SMenuEntryBlock")) && Text(Widget, Value).IsValid(); });
            if (Block)
                return Text(Block.ToSharedRef(), Value);
        }
        else if (auto Label = Text(Window, Value))
            return Label;
    }
    return nullptr;
}

bool FFlow::MouseClick(const TSharedPtr<SWidget>& Widget)
{
    if (!Widget || !Widget->IsEnabled())
        return false;
    const FGeometry& Geometry = Widget->GetCachedGeometry();
    if (Geometry.GetLocalSize().X < 1 || Geometry.GetLocalSize().Y < 1)
        return false;
    auto Window = FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef());
    if (!Window || !Window->GetNativeWindow())
        return false;
    const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
    const bool ButtonTarget = Widget->GetType() == FName(TEXT("SButton"));
    if (ButtonTarget)
    {
        const FWidgetPath Hit = FSlateApplication::Get().LocateWindowUnderMouse(
            Point, FSlateApplication::Get().GetInteractiveTopLevelWindows(), false, 0);
        if (!Hit.ContainsWidget(Widget.ToSharedRef()))
        {
            MissedPress(TEXT("Target button is not in the current native hit-test path"));
            return false;
        }
    }
    TSet<FKey> Down;
    Down.Add(EKeys::LeftMouseButton);
    TSet<FKey> Up;
    const FVector2D Previous = FSlateApplication::Get().GetCursorPos();
    FSlateApplication::Get().SetCursorPos(Point);
    // These are injected user-input events, not an idle synthetic cursor
    // poll. The synthetic route can omit hover while the app is inactive.
    FSlateApplication::Get().ProcessMouseMoveEvent(
        FPointerEvent(0, Point, Previous, Up, EKeys::Invalid, 0, FModifierKeysState()), false);
    FSlateApplication::Get().ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
        FPointerEvent(0, Point, Point, Down, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    // A scroll/focus change can leave cached geometry ahead of the hit-test
    // grid. Sending events is not proof that this button accepted the click.
    const auto Button = ButtonTarget ? StaticCastSharedPtr<SButton>(Widget) : nullptr;
    // Stock DownAndUp buttons invoke their delegate only when all three
    // conditions hold. A missed hover/capture is not a completed action.
    const bool Pressed = !Button || (Button->IsPressed() && Button->IsHovered() && Button->HasMouseCapture());
    FSlateApplication::Get().ProcessMouseButtonUpEvent(
        FPointerEvent(0, Point, Point, Up, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    if (!Pressed)
    {
        MissedPress(TEXT("Target button did not accept pressed, hovered and capture state"));
    }
    return Pressed;
}

void FFlow::MissedPress(const FString& Reason)
{
    ++MissedButtonPresses;
    LastMissedButtonStep = Index < Steps.Num() ? Steps[Index].Name : TEXT("native click");
    LastMissedButtonReason = Reason;
    Prepared.Reset();
    ReadyFrame = Frame + 2;
}

void FFlow::ScrollParents(const TSharedRef<SWidget>& Root, const TSharedPtr<SWidget>& Target)
{
    if (!Root->GetVisibility().IsVisible())
        return;
    if (Root->GetType() == FName(TEXT("SScrollBox")) &&
        Find(Root, [&](const TSharedRef<SWidget>& Child) { return &Child.Get() == Target.Get(); }))
        StaticCastSharedRef<SScrollBox>(Root)->ScrollDescendantIntoView(
            Target, false, EDescendantScrollDestination::Center, 8.f);
    FChildren* Children = Root->GetChildren();
    for (int32 I = 0; Children && I < Children->Num(); ++I)
        ScrollParents(Children->GetChildAt(I), Target);
}

bool FFlow::Expand(const FString& Tag, const FString& Caption, bool Value)
{
    auto Widget = Tagged(Tag);
    if (!Widget)
        return false;
    auto Area = StaticCastSharedPtr<SExpandableArea>(Widget);
    auto Header = Text(Area.ToSharedRef(), Caption);
    if (!Prepare(Header))
        return false;
    if (Area->IsExpanded() == Value)
        return true;
    if (!MouseClick(Header))
        return false;
    ReadyFrame = Frame + 8;
    return false;
}

bool FFlow::CheckTag(const FString& Tag, bool Value)
{
    auto Widget = Tagged(Tag);
    if (!Prepare(Widget))
        return false;
    auto Check = StaticCastSharedPtr<SCheckBox>(Widget);
    if (Check->IsChecked() == Value)
        return true;
    if (!MouseClick(Check))
        return false;
    return Expect(Check->IsChecked() == Value, TEXT("Native checkbox did not change: ") + Tag);
}

int32 FFlow::SourceCount()
{
    auto Widget = Tagged(TEXT("KadanSourceList"));
    return Widget ? StaticCastSharedPtr<SListView<TSharedPtr<FP4KadanBrowserRow>>>(Widget)->GetNumItemsBeingObserved()
                  : -1;
}

bool FFlow::StickyImportActions()
{
    auto Panel = Tab();
    if (!Panel)
        return false;
    const FGeometry& Bounds = Panel->GetContent()->GetCachedGeometry();
    const FVector2D Min = Bounds.LocalToAbsolute(FVector2D::ZeroVector);
    const FVector2D Max = Bounds.LocalToAbsolute(Bounds.GetLocalSize());
    for (const FString& Caption : {FString(TEXT("Review required content")), FString(TEXT("Import selection"))})
    {
        auto Button = Find(Panel->GetContent(), [&](const TSharedRef<SWidget>& Widget)
            { return Widget->GetType() == FName(TEXT("SButton")) && Text(Widget, Caption).IsValid(); });
        if (!Button)
            return Expect(false, TEXT("Sticky import action is missing: ") + Caption);
        const FGeometry& Geometry = Button->GetCachedGeometry();
        const FVector2D Start = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
        const FVector2D End = Geometry.LocalToAbsolute(Geometry.GetLocalSize());
        if (Geometry.GetLocalSize().Y < 10 || Start.X < Min.X - 1 || Start.Y < Min.Y - 1 || End.X > Max.X + 1 ||
            End.Y > Max.Y + 1)
            return Expect(false, TEXT("Import action lies outside the visible panel: ") + Caption);
    }
    auto List = Tagged(TEXT("KadanSourceList"));
    return Expect(List && List->GetCachedGeometry().GetLocalSize().Y >= 44 && !VisibleText(TEXT("1. Set up"), true) &&
            !VisibleText(TEXT("1. Create"), true),
        TEXT("The compact browser loses its usable list or retains the numbered banner"));
}

bool FFlow::StickyModsActions()
{
    auto Panel = Tab();
    if (!Panel)
        return false;
    const FGeometry& Bounds = Panel->GetContent()->GetCachedGeometry();
    const FVector2D Min = Bounds.LocalToAbsolute(FVector2D::ZeroVector);
    const FVector2D Max = Bounds.LocalToAbsolute(Bounds.GetLocalSize());
    for (const FString& Caption : {FString(TEXT("Review export")), FString(TEXT("Export mod"))})
    {
        auto Button = Find(Panel->GetContent(), [&](const TSharedRef<SWidget>& Widget)
            { return Widget->GetType() == FName(TEXT("SButton")) && Text(Widget, Caption).IsValid(); });
        if (!Button)
            return Expect(false, TEXT("Missing persistent export control"));
        const FGeometry& Geometry = Button->GetCachedGeometry();
        const FVector2D Start = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
        const FVector2D End = Geometry.LocalToAbsolute(Geometry.GetLocalSize());
        if (Geometry.GetLocalSize().Y < 10 || Start.X < Min.X - 1 || Start.Y < Min.Y - 1 || End.X > Max.X + 1 ||
            End.Y > Max.Y + 1)
            return Expect(false, TEXT("Export control is outside the visible panel"));
    }
    auto List = Tagged(TEXT("KadanShippingList"));
    return Expect(List && List->GetCachedGeometry().GetLocalSize().Y >= 44 &&
            !TaggedText(TEXT("KadanModIdentity")).IsEmpty() && !TaggedText(TEXT("KadanExportDisabledReason")).IsEmpty(),
        TEXT("Persistent identity, shipping space or disabled reason is missing"));
}

bool FFlow::Prepare(const TSharedPtr<SWidget>& Widget)
{
    if (!Widget)
        return false;
    if (Prepared.Pin() != Widget)
    {
        if (auto Panel = Tab())
        {
            Panel->ActivateInParent(ETabActivationCause::SetDirectly);
            if (auto Window = FSlateApplication::Get().FindWidgetWindow(Panel->GetContent()))
                Window->BringToFront(true);
        }
        if (auto Panel = Tab())
            ScrollParents(Panel->GetContent(), Widget);
        Prepared = Widget;
        ReadyFrame = Frame + 2;
        return false;
    }
    return Frame >= ReadyFrame;
}

bool FFlow::Click(const FString& Caption)
{
    auto Panel = Tab();
    if (!Panel)
        return false;
    auto Widget = Find(Panel->GetContent(), [&](const TSharedRef<SWidget>& Candidate)
        { return Candidate->GetType() == FName(TEXT("SButton")) && Text(Candidate, Caption).IsValid(); });
    if (!Widget && Caption == TEXT("Remove"))
    {
        Prepare(Tagged(TEXT("KadanSelectionRoots")));
        return false;
    }
    if (!Prepare(Widget) || !Widget->IsEnabled())
        return false;
    auto Button = StaticCastSharedPtr<SButton>(Widget);
    // Route a real click so focus leaves the preceding editable field before
    // the action changes its bound model. Calling button key handlers directly
    // leaves that field editing stale text, unlike an actual user click.
    return MouseClick(Button);
}

bool FFlow::ClickQueuedExport(const FString& Caption)
{
    const FString PriorStatus = TaggedText(TEXT("KadanStatus"));
    if (!Click(Caption))
        return false;
    const auto Page = Tagged(TEXT("KadanPages"));
    ++QueuedExportClicks;
    // Inspect immediately after the real click, before the deferred work
    // runs. A synchronous handler returns only after its slow work ends.
    return Expect(OperationBusy() && Page && !Page->IsEnabled() && TaggedText(TEXT("KadanStatus")) != PriorStatus,
        TEXT("Export action did not queue visible busy feedback and disable its form before work: ") + Caption);
}

bool FFlow::CommitField(const FString& Name)
{
    auto Input = Field(Name);
    if (!Prepare(Input))
        return false;
    if (!Input->HasKeyboardFocus() && !MouseClick(Input))
        return false;
    const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), 0, false, 13, 0);
    const bool Handled = FSlateApplication::Get().ProcessKeyDownEvent(Enter);
    FSlateApplication::Get().ProcessKeyUpEvent(Enter);
    return Expect(Handled, TEXT("The form field did not handle Enter: ") + Name);
}

bool FFlow::SelectTopFolder()
{
    const auto Tree = Tagged(TEXT("KadanFolderTree"));
    if (!Prepare(Tree))
        return false;
    const auto Entry = Find(Tree.ToSharedRef(),
        [](const TSharedRef<SWidget>& Widget)
        {
            return Widget->GetType() == FName(TEXT("STextBlock")) &&
                StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().StartsWith(TEXT("Carrot4 ("));
        });
    if (!Entry || !MouseClick(Entry))
        return false;
    return true;
}

bool FFlow::Menu(const FString& Entry, const FString& Heading)
{
    if (Phase == 0)
    {
        auto Window = FGlobalTabmanager::Get()->GetRootWindow();
        if (Window)
        {
            if (Window->IsWindowMinimized())
                Window->Restore();
            Window->ShowWindow();
            Window->BringToFront(true);
        }
        // The status lamp moved the top-level entry into a custom menu-bar
        // widget. Its menu items remain ordinary SMenuEntryBlock widgets.
        TSharedPtr<SWidget> Header;
        if (Window)
            if (auto Bar = Find(Window.ToSharedRef(), [](const TSharedRef<SWidget>& Widget)
                    { return Widget->GetType() == FName(TEXT("SKadanMenuBar")); }))
                Header = Text(Bar.ToSharedRef(), TEXT("Kadan"));
        if (!Header)
            Header = WindowText(TEXT("Kadan"), true);
        if (!MouseClick(Header))
            return false;
        Phase = 1;
        ReadyFrame = Frame + 2;
        return false;
    }
    if (Phase == 1)
    {
        if (!MouseClick(WindowText(Entry, true)))
            return false;
        Phase = 2;
        ReadyFrame = Frame + 2;
        return false;
    }
    return VisibleText(Heading, true);
}

bool FFlow::SetField(const FString& Name, const FString& Value)
{
    auto Input = Name == TEXT("Search") ? StaticCastSharedPtr<SEditableTextBox>(Tagged(TEXT("KadanSearch")))
                                        : Field(Name);
    if (!Prepare(Input))
        return false;
    // SetText replaces the live Text_Lambda attribute. Type through Slate so
    // normal OnTextChanged edits preserve the form binding when mods switch.
    if (Phase == 0)
    {
        if (!MouseClick(Input))
            return false;
        Phase = 1;
        ReadyFrame = Frame + 2;
        return false;
    }
    if (Phase == 1)
    {
        if (!Input->HasKeyboardFocus())
        {
            Fail(TEXT("Form field did not receive keyboard focus: ") + Name);
            return true;
        }
        auto& Slate = FSlateApplication::Get();
        {
            // Always clear the live editor. While it has focus, its uncommitted
            // text can differ from the Text_Lambda model after New mod/reset.
            const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
            const FKeyEvent SelectAll(EKeys::A, Control, 0, false, 0, 0);
            const bool Selected = Slate.ProcessKeyDownEvent(SelectAll);
            Slate.ProcessKeyUpEvent(SelectAll);
            if (!Selected)
            {
                Fail(TEXT("Form field did not handle Ctrl+A: ") + Name);
                return true;
            }
            // UE4.26 deletes ordinary Backspace in HandleKeyChar, as the
            // Windows character event that follows the physical key down.
            const FKeyEvent Backspace(EKeys::BackSpace, FModifierKeysState(), 0, false, 8, 0);
            Slate.ProcessKeyDownEvent(Backspace);
            const bool Deleted = Slate.ProcessKeyCharEvent(FCharacterEvent(TCHAR(8), FModifierKeysState(), 0, false));
            Slate.ProcessKeyUpEvent(Backspace);
            if (!Deleted)
            {
                Fail(TEXT("Form field did not handle Backspace: ") + Name);
                return true;
            }
        }
        for (int32 Character = 0; Character < Value.Len(); ++Character)
            if (!Slate.ProcessKeyCharEvent(FCharacterEvent(Value[Character], FModifierKeysState(), 0, false)))
            {
                Fail(TEXT("Form field did not handle character ") + FString::FromInt(Character) + TEXT(": ") + Name);
                return true;
            }
        Phase = 2;
        ReadyFrame = Frame + 2;
        return false;
    }
    return Input->GetText().ToString() == Value;
}

bool FFlow::Expect(bool Value, const FString& Why)
{
    if (!Value)
        Fail(Why);
    return true;
}

bool FFlow::ButtonEnabled(const FString& Caption)
{
    auto Panel = Tab();
    if (!Panel)
        return false;
    auto Button = Find(Panel->GetContent(), [&](const TSharedRef<SWidget>& Widget)
        { return Widget->GetType() == FName(TEXT("SButton")) && Text(Widget, Caption).IsValid(); });
    return Button && Button->IsEnabled();
}

FString FFlow::TaggedText(const FString& Tag)
{
    auto Widget = Tagged(Tag);
    return Widget && Widget->GetType() == FName(TEXT("STextBlock"))
        ? StaticCastSharedPtr<STextBlock>(Widget)->GetText().ToString()
        : FString();
}

bool FFlow::ResumeCheck(bool Value)
{
    auto Widget = Tagged(TEXT("KadanResume"));
    if (!Prepare(Widget))
        return false;
    auto Check = StaticCastSharedPtr<SCheckBox>(Widget);
    if (Check->IsChecked() == Value)
        return true;
    if (!MouseClick(Check))
        return false;
    return Expect(Check->IsChecked() == Value, TEXT("Resume checkbox did not change through native Slate input"));
}

void FFlow::StopCancellation()
{
    if (CancelTickHandle.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnPostTick().Remove(CancelTickHandle);
    CancelTickHandle.Reset();
}

bool FFlow::ArmCancellation()
{
    StopCancellation();
    if (!Tab())
        return false;
    CancelClicked = false;
    CancelTicks = 0;
    CancelDialogs = 0;
    // Modal progress windows can stop ticking the background panel widget.
    // Observe actual application ticks instead, while the native slow task pumps Slate.
    CancelTickHandle = FSlateApplication::Get().OnPostTick().AddLambda(
        [this](float)
        {
            if (CancelClicked)
                return;
            ++CancelTicks;
            TArray<TSharedRef<SWindow>> Windows;
            FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
            if (auto Modal = FSlateApplication::Get().GetActiveModalWindow())
                Windows.AddUnique(Modal.ToSharedRef());
            for (auto Window : Windows)
            {
                const auto Task = Find(Window, [](const TSharedRef<SWidget>& Widget)
                    { return Widget->GetType() == FName(TEXT("SSlowTaskWidget")); });
                if (!Task)
                    continue;
                ++CancelDialogs;
                auto Button = Find(Task.ToSharedRef(), [&](const TSharedRef<SWidget>& Widget)
                    { return Widget->GetType() == FName(TEXT("SButton")) && Text(Widget, TEXT("Cancel")).IsValid(); });
                if (Button)
                {
                    // Activate the actual native Cancel control while its modal
                    // progress window is pumping Slate, including the first layout.
                    auto Control = StaticCastSharedPtr<SButton>(Button);
                    const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
                    if (Control->OnKeyDown(Control->GetCachedGeometry(), Enter).IsEventHandled() &&
                        Control->OnKeyUp(Control->GetCachedGeometry(), Enter).IsEventHandled())
                    {
                        CancelClicked = true;
                        return;
                    }
                }
            }
        });
    return true;
}

bool FFlow::ChooseKind(const FString& Kind)
{
    auto Widget = Tagged(TEXT("KadanKindPicker"));
    if (!Prepare(Widget))
        return false;
    auto Picker = StaticCastSharedPtr<SComboBox<TSharedPtr<FString>>>(Widget);
    if (Phase == 0)
    {
        if (!MouseClick(Picker))
            return false;
        Phase = 1;
        ReadyFrame = Frame + 2;
        return false;
    }
    if (Phase == 1)
    {
        if (!MouseClick(WindowText(Kind == TEXT("Texture2D") ? TEXT("Textures") : Kind)))
            return false;
        Phase = 2;
        ReadyFrame = Frame + 2;
        return false;
    }
    auto Selected = Picker->GetSelectedItem();
    return Expect(Selected && *Selected == Kind, TEXT("Native kind-picker selection differs from chosen kind"));
}

bool FFlow::CheckSource(bool Value)
{
    auto List = Tagged(TEXT("KadanSourceList"));
    if (!List)
        return false;
    auto Widget = Find(List.ToSharedRef(),
        [](const TSharedRef<SWidget>& Child) { return Child->GetType() == FName(TEXT("SCheckBox")); });
    if (!Prepare(Widget))
        return false;
    auto Check = StaticCastSharedPtr<SCheckBox>(Widget);
    if (Check->IsChecked() == Value)
        return true;
    if (!MouseClick(Check))
        return false;
    return Expect(Check->IsChecked() == Value, TEXT("Source-row checkbox does not update selection"));
}

bool FFlow::Choose(const FP4ModDescriptor& Mod)
{
    auto Widget = Tagged(TEXT("KadanModPicker"));
    if (!Prepare(Widget))
        return false;
    auto Picker = StaticCastSharedPtr<FPicker>(Widget);
    if (Phase == 0)
    {
        if (!MouseClick(Picker))
            return false;
        Phase = 1;
        ReadyFrame = Frame + 2;
        return false;
    }
    if (Phase == 1)
    {
        if (!MouseClick(WindowText(Mod.Name + TEXT(" · ") + Mod.Id)))
            return false;
        Phase = 2;
        ReadyFrame = Frame + 2;
        return false;
    }
    auto Input = Field(TEXT("ID"));
    auto Selected = Picker->GetSelectedItem();
    return Input && Input->GetText().ToString() == Mod.Id && Selected && Selected->Id == Mod.Id;
}

bool FFlow::CheckRoots(const FP4ModDescriptor& Mod, const TArray<FString>& Expected)
{
    auto List = Tagged(TEXT("KadanSelectionRoots"));
    if (!Prepare(List))
        return false;
    FString Error;
    TArray<FP4ExportRoot> Roots;
    if (!P4ReadExportRoots(FPaths::ProjectDir(), Mod, Roots, Error))
    {
        Fail(Error);
        return true;
    }
    if (Roots.Num() != Expected.Num())
    {
        Fail(TEXT("Persisted root count differs for ") + Mod.Id);
        return true;
    }
    for (const FString& Root : Expected)
        if (!Roots.ContainsByPredicate(
                [&](const FP4ExportRoot& Row)
                {
                    return Row.Path == Root &&
                        Row.Kind ==
                        ((Root == BFolder || Root == ANames.PackageRoot) ? EP4ExportRootKind::Folder
                                                                         : EP4ExportRootKind::Package);
                }) ||
            !Text(List.ToSharedRef(), Root))
        {
            Fail(TEXT("Persisted and displayed root differ: ") + Root);
            return true;
        }
    if (!Expected.Num())
    {
        if (!List || Text(List.ToSharedRef(), APackage) || Text(List.ToSharedRef(), BFolder))
        {
            Fail(TEXT("The empty mod still displays a previous root"));
            return true;
        }
    }
    return true;
}

bool FFlow::Screenshot(const FString& Name)
{
    auto Panel = Tab();
    if (!Panel)
        return false;
    FSlateApplication::Get().Tick();
    TArray<FColor> Pixels;
    FIntVector Size;
    // SDockTab's own arranged geometry is its tab header. Capture the
    // displayed content widget, whose geometry covers the creator panel.
    if (!FSlateApplication::Get().TakeScreenshot(Panel->GetContent(), Pixels, Size) || Size.X < 400 || Size.Y < 200)
    {
        Fail(TEXT("Slate screenshot unavailable: ") + Name);
        return true;
    }
    TArray<uint8> PNG;
    FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, PNG);
    const FString File = Stage.ToLower() + TEXT("-") + Name + TEXT(".png");
    if (!FFileHelper::SaveArrayToFile(PNG, *(Out / File)))
        Fail(TEXT("Cannot save screenshot: ") + Name);
    else
        CapturedScreens.Add(MakeShared<FJsonValueString>(File));
    return true;
}

bool FFlow::ModFormMatches(const FString& Id, const FString& Name, const FString& Version)
{
    const auto PickerWidget = Tagged(TEXT("KadanModPicker"));
    const auto Picker = PickerWidget ? StaticCastSharedPtr<FPicker>(PickerWidget) : nullptr;
    const auto Selected = Picker ? Picker->GetSelectedItem() : nullptr;
    const auto IdField = Field(TEXT("ID"));
    const auto NameField = Field(TEXT("Name"));
    const auto VersionField = Field(TEXT("Version"));
    return Expect(IdField && NameField && VersionField && IdField->GetText().ToString() == Id &&
            NameField->GetText().ToString() == Name && VersionField->GetText().ToString() == Version &&
            (Id.IsEmpty() ? !Selected : Selected && Selected->Id == Id),
        TEXT("The mod picker or identity draft changed without the chosen action"));
}

bool FFlow::RemoveEveryRoot()
{
    const auto List = Tagged(TEXT("KadanSelectionRoots"));
    if (!List || StaticCastSharedPtr<SListView<TSharedPtr<FP4ExportRoot>>>(List)->GetNumItemsBeingObserved() == 0)
    {
        return true;
    }
    if (!Click(TEXT("Remove")))
    {
        return false;
    }
    Phase = 0;
    Prepared.Reset();
    ReadyFrame = Frame + 2;
    return false;
}

bool FFlow::NarrowPanel()
{
    auto Panel = Tab();
    if (!Panel)
        return false;
    auto Window = FSlateApplication::Get().FindWidgetWindow(Panel->GetContent());
    if (!Window)
        return false;
    if (Phase == 0)
    {
        Window->Resize(FVector2D(650, 780));
        Phase = 1;
        ReadyFrame = Frame + 4;
        return false;
    }
    return Expect(Panel->GetContent()->GetCachedGeometry().GetLocalSize().X <= 660,
        TEXT("The actual editor panel did not resize to the narrow proof width"));
}

bool FFlow::CaptureWidget(const FString& Name, TSharedPtr<SWidget> Content)
{
    if (!Content)
    {
        return false;
    }
    FSlateApplication::Get().Tick();
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!Expect(FSlateApplication::Get().TakeScreenshot(Content.ToSharedRef(), Pixels, Size) && Size.X > 100 &&
                Size.Y > 100,
            TEXT("The actual Material Details Slate panel could not be captured")))
    {
        return true;
    }
    TArray<uint8> Png;
    FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, Png);
    const FString File = Stage.ToLower() + TEXT("-") + Name + TEXT(".png");
    if (Expect(
            FFileHelper::SaveArrayToFile(Png, *(Out / File)), TEXT("The Material Details capture could not be saved")))
    {
        CapturedScreens.Add(MakeShared<FJsonValueString>(File));
    }
    return true;
}

IContentBrowserSingleton& FFlow::Browser()
{
    return FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get();
}

FString FFlow::Tool(const TCHAR* Variable, const TCHAR* Relative)
{
    FString Value = FPlatformMisc::GetEnvironmentVariable(Variable);
    if (Value.IsEmpty())
    {
        const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
        Value = FPaths::GetPath(FPaths::GetPath(Root)) / Relative;
    }
    return Value;
}
} // namespace P4KadanCreatorFlow
#endif

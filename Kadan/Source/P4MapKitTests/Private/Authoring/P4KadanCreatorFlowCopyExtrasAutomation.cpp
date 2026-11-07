#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// CopyExtras: the editable path fallback when the file picker is unavailable, and the actual field tooltips of
// Material Details opened by hover input. Only the picker's unavailability is a labeled fixture.
class FCopyExtrasStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    bool UsesTooltipFixtures() const override;

private:
    TSharedPtr<IToolTip> ActualPropertyTooltip(const TSharedRef<SWidget>& Widget);

    TSharedPtr<SWindow> ActualMaterialTooltip(const FString& Expected);

    bool HoverMaterialName(const FName Field, const FString& Expected);

    bool CaptureMaterialTooltip(const FString& Name, const FString& Expected);

    FFlowAssets Assets{Flow};
    FMaterialDetails Details{Flow};
    TWeakObjectPtr<UMaterial> DetailsMaterial;
    TUniquePtr<FP4KadanUnavailablePickerFixture> UnavailablePickerFixture;
    TUniquePtr<FP4KadanTooltipFixture> TooltipFixture;
    FName OpenedPropertyTooltip;
};
} // namespace

void FCopyExtrasStage::AddSteps()
{
    Flow.Add(
        TEXT("Open Mods through the real menu"), [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the native narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Choose the retained second identity"), [this]() { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Use the labeled unavailable-picker fixture"),
        [this]()
        {
            UnavailablePickerFixture = MakeUnique<FP4KadanUnavailablePickerFixture>();
            return true;
        });
    Flow.Add(TEXT("Click the actual Output folder Browse button"),
        [this]()
        {
            const auto Button = Flow.Tagged(TEXT("KadanPathPicker.Output folder"));
            return Flow.Prepare(Button) && Flow.MouseClick(Button);
        });
    Flow.Add(TEXT("The fallback names the editable field action"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")) ==
                    TEXT("The file picker is unavailable. Type the path in the field."),
                TEXT("The path picker fallback did not show its complete instruction"));
        });
    Flow.Add(TEXT("Capture the unavailable-picker fixture beside the editable field"),
        [this]() { return Flow.Screenshot(TEXT("picker-unavailable")); });
    Flow.Add(TEXT("Restore the desktop picker provider"),
        [this]()
        {
            UnavailablePickerFixture.Reset();
            return true;
        });
    Flow.Add(TEXT("Type the output path through the same field"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(TEXT("The fallback leaves its field usable"),
        [this]()
        {
            const auto Input = Flow.Field(TEXT("Output folder"));
            return Flow.Expect(Input && Input->GetText().ToString() == Flow.ExportFolder,
                TEXT("The editable path did not accept the fallback input"));
        });
    Flow.Add(TEXT("Prepare an owned material for actual Details tooltips"),
        [this]()
        {
            const FString Package = Flow.BNames.PackageRoot / TEXT("UXCopyMaterial");
            auto* Material = LoadObject<UMaterial>(nullptr, *(Package + TEXT(".UXCopyMaterial")), nullptr, LOAD_NoWarn);
            if (!Material)
            {
                Material = NewObject<UMaterial>(
                    CreatePackage(*Package), TEXT("UXCopyMaterial"), RF_Public | RF_Standalone | RF_Transactional);
            }
            DetailsMaterial = Material;
            return Material && UP4MaterialGameSettings::FindOrAdd(Material) && Assets.SaveIssueFixture(Material);
        });
    Flow.Add(TEXT("Open the real Material Details toolkit"),
        [this]() { return Details.OpenMaterialDetails(DetailsMaterial.Get()); });
    Flow.Add(TEXT("Allow actual tooltip hover during scoped automation"),
        [this]()
        {
            TooltipFixture = MakeUnique<FP4KadanTooltipFixture>();
            return true;
        });
    TArray<TPair<FString, FString>> Tooltips;
    Tooltips.Emplace(TEXT("C4ShaderParam3"), TEXT("This setting's effect is not known. Keep its current value."));
    Tooltips.Emplace(TEXT("C4DecalBlendMode"), TEXT("This game decal mode writes the water wetness mask."));
    for (const auto& Tooltip : Tooltips)
    {
        Flow.Add(TEXT("Find the actual serialized field ") + Tooltip.Key,
            [this, Tooltip]() { return Details.MaterialSearch(Tooltip.Key); });
        Flow.Add(
            TEXT("Open the actual field tooltip after hover input ") + Tooltip.Key,
            [this, Tooltip]() { return HoverMaterialName(FName(*Tooltip.Key), Tooltip.Value); }, 20);
        Flow.Add(TEXT("Capture the actual field tooltip ") + Tooltip.Key,
            [this, Tooltip]() { return CaptureMaterialTooltip(Tooltip.Key.ToLower(), Tooltip.Value); });
    }
    Flow.Add(TEXT("The unknown serialized value is unchanged"),
        [this]()
        {
            const auto* Values = UP4MaterialGameSettings::Find(DetailsMaterial.Get());
            return Flow.Expect(
                Values && Values->C4ShaderParam3 == 1.f, TEXT("Tooltip inspection changed an unknown material value"));
        });
    Flow.Add(TEXT("Close only the owned material toolkit"),
        [this]()
        {
            TooltipFixture.Reset();
            Details.CloseMaterialDetails(DetailsMaterial.Get());
            return true;
        });
}

bool FCopyExtrasStage::UsesTooltipFixtures() const
{
    return true;
}

TSharedPtr<IToolTip> FCopyExtrasStage::ActualPropertyTooltip(const TSharedRef<SWidget>& Widget)
{
    const auto Tooltip = Widget->GetToolTip();
    if (Tooltip && !Tooltip->IsEmpty())
    {
        return Tooltip;
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 ChildIndex = 0; Children && ChildIndex < Children->Num(); ++ChildIndex)
    {
        if (auto Found = ActualPropertyTooltip(Children->GetChildAt(ChildIndex)))
        {
            return Found;
        }
    }
    return nullptr;
}

TSharedPtr<SWindow> FCopyExtrasStage::ActualMaterialTooltip(const FString& Expected)
{
    TArray<TSharedRef<SWindow>> Windows;
    FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
    for (const auto& Window : Windows)
    {
        if (Text(Window, Expected, true))
        {
            return Window;
        }
    }
    return nullptr;
}

bool FCopyExtrasStage::HoverMaterialName(const FName Field, const FString& Expected)
{
    const auto Widget = Details.MaterialTagged(FName(*(TEXT("KadanMaterialName.") + Field.ToString())));
    const auto Panel = Details.DetailsTab.Pin();
    if (!Widget || !Panel)
    {
        return false;
    }
    if (Flow.Phase == 0)
    {
        if (const auto Window = FSlateApplication::Get().FindWidgetWindow(Panel->GetContent()))
        {
            Window->BringToFront(true);
        }
        Flow.ScrollParents(Panel->GetContent(), Widget);
        Flow.Phase = 1;
        Flow.ReadyFrame = Flow.Frame + 4;
        return false;
    }
    if (Flow.Frame < Flow.ReadyFrame)
    {
        return false;
    }
    auto& Slate = FSlateApplication::Get();
    const auto& Geometry = Widget->GetCachedGeometry();
    const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
    if (Flow.Phase == 1 || (Slate.GetCursorPos() - Point).SizeSquared() > 4)
    {
        const FVector2D Previous = Slate.GetCursorPos();
        Slate.SetCursorPos(Point);
        TSet<FKey> Keys;
        Slate.ProcessMouseMoveEvent(
            FPointerEvent(0, Point, Previous, Keys, EKeys::Invalid, 0, FModifierKeysState()), false);
        Flow.Phase = 2;
    }
    FSlateApplication::Get().UpdateToolTip(true);
    if (!ActualMaterialTooltip(Expected) && OpenedPropertyTooltip != Field &&
        FPlatformTime::Seconds() - Flow.Started > 4)
    {
        const auto Tooltip = ActualPropertyTooltip(Widget.ToSharedRef());
        if (Tooltip)
        {
            OpenedPropertyTooltip = Field;
            // Open the field's own tooltip through Slate. No substitute content is made.
            Slate.SpawnToolTip(Tooltip.ToSharedRef(), Point + FVector2D(12, 20));
        }
    }
    return ActualMaterialTooltip(Expected).IsValid();
}

bool FCopyExtrasStage::CaptureMaterialTooltip(const FString& Name, const FString& Expected)
{
    auto& Slate = FSlateApplication::Get();
    if (!ActualMaterialTooltip(Expected))
    {
        const auto Widget = Details.MaterialTagged(FName(*(TEXT("KadanMaterialName.") + Name)));
        const auto Tooltip = Widget ? ActualPropertyTooltip(Widget.ToSharedRef()) : nullptr;
        if (!Tooltip)
        {
            return false;
        }
        const auto& Geometry = Widget->GetCachedGeometry();
        const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
        Slate.SpawnToolTip(Tooltip.ToSharedRef(), Point + FVector2D(12, 20));
    }
    // Paint the actual popup without running platform input that closes a manual tooltip.
    Slate.Tick(ESlateTickType::Widgets);
    const auto Window = ActualMaterialTooltip(Expected);
    if (!Window)
    {
        return false;
    }
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!Slate.TakeScreenshot(Window.ToSharedRef(), Pixels, Size) || Size.X < 16 || Size.Y < 16)
    {
        return false;
    }
    TArray<uint8> Png;
    FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, Png);
    const FString File = Flow.Stage.ToLower() + TEXT("-") + Name + TEXT(".png");
    if (!FFileHelper::SaveArrayToFile(Png, *(Flow.Out / File)))
    {
        Flow.Fail(TEXT("The actual property tooltip could not be captured"));
        return true;
    }
    Flow.CapturedScreens.Add(MakeShared<FJsonValueString>(File));
    return true;
}

TUniquePtr<FFlowStage> MakeCopyExtrasStage(FFlow& Flow)
{
    return MakeUnique<FCopyExtrasStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

#if P4_MAPKIT_TESTS
#include "P4KadanCreatorFlowFixtures.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#endif

FP4KadanTooltipFixture::FP4KadanTooltipFixture()
{
    Previous = FSlateApplication::Get().GetAllowTooltips();
    PreviousPosition = FSlateApplication::Get().GetCursorPos();
    FSlateApplication::Get().UsePlatformCursorForCursorUser(false);
    FSlateApplication::Get().SetAllowTooltips(true);
}

FP4KadanTooltipFixture::~FP4KadanTooltipFixture()
{
    FSlateApplication::Get().CloseToolTip();
    FSlateApplication::Get().UsePlatformCursorForCursorUser(true);
    FSlateApplication::Get().SetCursorPos(PreviousPosition);
    FSlateApplication::Get().SetAllowTooltips(Previous);
}

FP4KadanUnavailablePickerFixture::FP4KadanUnavailablePickerFixture()
{
    P4KadanUnavailablePathPickerForTest(true);
}

FP4KadanUnavailablePickerFixture::~FP4KadanUnavailablePickerFixture()
{
    P4KadanUnavailablePathPickerForTest(false);
}

FP4KadanReadOnlyTextureProbe::FP4KadanReadOnlyTextureProbe(UTexture2D* Value) : Texture(Value)
{
    Address = Value->AddressX;
    Dirty = Value->GetOutermost()->IsDirty();
    Value->AddressX = Address == TA_Clamp ? TA_Wrap : TA_Clamp;
    Value->MarkPackageDirty();
}

FP4KadanReadOnlyTextureProbe::~FP4KadanReadOnlyTextureProbe()
{
    if (Texture.IsValid())
    {
        Texture->AddressX = Address;
        Texture->GetOutermost()->SetDirtyFlag(Dirty);
    }
}

bool P4KadanResizeDetailsSplit(
    const TSharedRef<SWidget>& Root, const TSharedRef<SWidget>& Target, EOrientation Orientation)
{
    FChildren* Children = Root->GetChildren();
    if (!Children)
    {
        return false;
    }
    for (int32 Child = 0; Child < Children->Num(); ++Child)
    {
        if (P4KadanResizeDetailsSplit(Children->GetChildAt(Child), Target, Orientation))
        {
            return true;
        }
    }
    if (Root->GetType() != FName(TEXT("SSplitter")))
    {
        return false;
    }
    TSharedRef<SSplitter> Split = StaticCastSharedRef<SSplitter>(Root);
    if (Split->GetOrientation() != Orientation || Children->Num() <= 1)
    {
        return false;
    }
    TFunction<bool(const TSharedRef<SWidget>&)> Contains;
    Contains = [&](const TSharedRef<SWidget>& Widget)
    {
        if (Widget == Target)
        {
            return true;
        }
        FChildren* Descendants = Widget->GetChildren();
        for (int32 Child = 0; Descendants && Child < Descendants->Num(); ++Child)
        {
            if (Contains(Descendants->GetChildAt(Child)))
            {
                return true;
            }
        }
        return false;
    };
    for (int32 Child = 0; Child < Children->Num(); ++Child)
    {
        if (!Contains(Children->GetChildAt(Child)))
        {
            continue;
        }
        for (int32 Slot = 0; Slot < Children->Num(); ++Slot)
        {
            Split->SlotAt(Slot).SizeValue = Slot == Child ? 0.7f : 0.3f / (Children->Num() - 1);
        }
        return true;
    }
    return false;
}

#if PLATFORM_WINDOWS
FP4KadanReceiptPublicationLock::~FP4KadanReceiptPublicationLock()
{
    Close();
}

bool FP4KadanReceiptPublicationLock::Open(const FString& Directory)
{
    HANDLE Opened = CreateFileW(*Directory, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (Opened == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    Handle = Opened;
    return true;
}

void FP4KadanReceiptPublicationLock::Close()
{
    if (Handle)
    {
        CloseHandle(static_cast<HANDLE>(Handle));
        Handle = nullptr;
    }
}
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#endif // P4_MAPKIT_TESTS

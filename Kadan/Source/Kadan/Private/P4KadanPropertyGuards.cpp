#include "P4KadanReadOnly.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Widgets/SWindow.h"

namespace P4Kadan
{
namespace
{
struct FDetailGuard
{
    TWeakPtr<IDetailsView> View;
    FIsPropertyReadOnly Previous;
    FDelegateHandle Installed;
};

FDelegateHandle Tick;
TArray<FDetailGuard> Details;

void Inspect(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetType() == FName(TEXT("SDetailsView")))
    {
        auto View = StaticCastSharedRef<IDetailsView>(Widget);
        FDetailGuard* Guard = Details.FindByPredicate(
            [&View](const FDetailGuard& Entry) { return Entry.View.Pin() == View; });
        const auto Current = View->GetIsPropertyReadOnlyDelegate();
        if (Guard && Guard->Installed == Current.GetHandle())
            return;
        if (!Guard)
            Guard = &Details.AddDefaulted_GetRef();
        Guard->View = View;
        Guard->Previous = Current;
        View->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateLambda(
            [Weak = Guard->View, Current](const FPropertyAndParent& Field)
            {
                if (Current.IsBound() && Current.Execute(Field))
                    return true;
                if (const auto Live = Weak.Pin())
                    for (const auto& Object : Live->GetSelectedObjects())
                        if (Object.IsValid() && (IsLockedObject(Object.Get()) ||
                            !CanEditGameProperty(Object.Get(), Field.Property, Field.ParentProperties)))
                            return true;
                return false;
            }));
        Guard->Installed = View->GetIsPropertyReadOnlyDelegate().GetHandle();
        return;
    }
    if (FChildren* Children = Widget->GetChildren())
        for (int32 Index = 0; Index < Children->Num(); ++Index)
            Inspect(Children->GetChildAt(Index));
}

bool Refresh(float)
{
    if (FSlateApplication::IsInitialized())
        for (const auto& Window : FSlateApplication::Get().GetTopLevelWindows())
            Inspect(Window);
    Details.RemoveAll([](const FDetailGuard& Entry) { return !Entry.View.IsValid(); });
    return true;
}
} //namespace

void InitializePropertyGuards()
{
    if (!Tick.IsValid())
        Tick = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Refresh), 0.25f);
}

void ShutdownPropertyGuards()
{
    FTicker::GetCoreTicker().RemoveTicker(Tick);
    Tick.Reset();
    for (const FDetailGuard& Guard : Details)
        if (const auto View = Guard.View.Pin())
            if (View->GetIsPropertyReadOnlyDelegate().GetHandle() == Guard.Installed)
                View->SetIsPropertyReadOnlyDelegate(Guard.Previous);
    Details.Reset();
}
} //namespace P4Kadan

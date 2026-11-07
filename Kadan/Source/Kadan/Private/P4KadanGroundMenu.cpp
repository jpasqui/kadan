// Ground types on the level's right-click menu: right-click placed scenery, Ground type, and a type the
// imported game scenery uses. It replaces the Asset page's Ground types panel.
#include "P4KadanGroundMenu.h"
#include "P4KadanGroundPanel.h"
#include "P4GroundTypes.h"
#include "Engine/StaticMeshActor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LevelEditor.h"
#include "Landscape.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
FDelegateHandle MenuHandle;

void Tell(const FString& Text, bool Failed)
{
    FNotificationInfo Info(FText::FromString(Text));
    Info.ExpireDuration = Failed ? 8.f : 4.f;
    Info.bUseLargeFont = false;
    if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
        Item->SetCompletionState(Failed ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
}

void Apply(TArray<TWeakObjectPtr<AActor>> Chosen, FP4GroundType Type)
{
    TArray<AActor*> Actors;
    for (const auto& Actor : Chosen)
        if (Actor.IsValid())
            Actors.Add(Actor.Get());
    const FP4KadanGroundResult Result = P4KadanApplyGroundType(Actors, Type);
    for (const FString& Row : Result.Rows)
        UE_LOG(LogTemp, Display, TEXT("Kadan ground type: %s"), *Row);
    Tell(Result.Summary(), Result.Failed > 0 || !Result.Applied);
}

void Fill(FMenuBuilder& Menu, TArray<TWeakObjectPtr<AActor>> Chosen)
{
    const TArray<FP4GroundType> Types = P4ImportedGroundTypes();
    if (!Types.Num())
    {
        Menu.AddMenuEntry(FText::FromString(TEXT("Import game scenery with collision first")), FText(), FSlateIcon(),
            FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
        return;
    }
    for (const FP4GroundType& Type : Types)
        Menu.AddMenuEntry(FText::FromString(Type.Name),
            FText::FromString(TEXT("Use this ground type for the selected scenery. Save the meshes and the level after.")),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Chosen, Type]() { Apply(Chosen, Type); })));
}
} // namespace

void P4RegisterGroundMenu()
{
    auto Extender = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateLambda(
        [](const TSharedRef<FUICommandList>, const TArray<AActor*> Actors)
        {
            const auto Result = MakeShared<FExtender>();
            TArray<TWeakObjectPtr<AActor>> Scenery;
            for (AActor* Actor : Actors)
                if (Actor && (Actor->IsA<AStaticMeshActor>() || Actor->IsA<ALandscapeProxy>()))
                    Scenery.Add(Actor);
            if (!Scenery.Num())
                return Result;
            Result->AddMenuExtension(TEXT("ActorControl"), EExtensionHook::After, nullptr,
                FMenuExtensionDelegate::CreateLambda(
                    [Scenery](FMenuBuilder& Menu)
                    {
                        Menu.AddSubMenu(FText::FromString(TEXT("Ground type")),
                            FText::FromString(TEXT("The ground the game uses for this scenery: footsteps, effects "
                                                   "and what grows or sinks.")),
                            FNewMenuDelegate::CreateLambda([Scenery](FMenuBuilder& Sub) { Fill(Sub, Scenery); }));
                    }));
            return Result;
        });
    MenuHandle = Extender.GetHandle();
    FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor").GetAllLevelViewportContextMenuExtenders().Add(
        Extender);
}

void P4UnregisterGroundMenu()
{
    if (!FModuleManager::Get().IsModuleLoaded("LevelEditor"))
        return;
    FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor")
        .GetAllLevelViewportContextMenuExtenders()
        .RemoveAll([](const auto& Extender) { return Extender.GetHandle() == MenuHandle; });
}

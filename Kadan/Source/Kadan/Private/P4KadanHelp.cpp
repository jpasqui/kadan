#include "P4KadanHelp.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"

void P4KadanHelpMenu(FMenuBuilder& Menu)
{
    Menu.AddMenuSeparator();
    Menu.AddMenuEntry(FText::FromString(TEXT("Documentation")), FText::FromString(TEXT("Open Kadan's guides.")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda(
            []() { FPlatformProcess::LaunchURL(TEXT("https://kadan.jpasqui.dev/guides/"), nullptr, nullptr); })));
}


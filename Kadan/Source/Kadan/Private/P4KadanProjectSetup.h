#pragma once
#include "CoreMinimal.h"

struct FP4KadanProjectSetupResult
{
    bool Ok = false;
    bool RestartRequired = false;
    int32 Channels = 0;
    int32 Profiles = 0;
    FString Message;
};

// The classes the game's engine configuration names for the setup: its world settings class, and the
// sections the setup copies, which are those of the navigation system class its engine section names and
// of its navigation data classes, as header lines in the configuration's order.
struct FP4KadanGameConfigClasses
{
    FString WorldSettings;
    TArray<FString> NavigationSections;
};
KADAN_API bool P4KadanGameConfigClasses(const FString& GameIni, FP4KadanGameConfigClasses& Out, FString& Why);
// The runtime modules of the game's type declarations, as their plugin lists them, and whether a class is
// declared in one of them.
KADAN_API TArray<FString> P4KadanDeclarationModules();
KADAN_API bool P4KadanDeclaredClass(const UClass* Class);

// Reads only the owner's game configuration. Never imports packages or supplies
// game configuration in the plugin distribution.
KADAN_API FP4KadanProjectSetupResult P4KadanSetupProject(const FString& GamePaks, const FString& Repak);
KADAN_API bool P4KadanProjectReady(FString& Why);
KADAN_API bool P4KadanProjectRestartRequired();
// Whether Set up project can get the project ready: false when the P4Stubs plugin is missing or did not load, which
// only the creator can fix (P4KadanProjectReady says how).
KADAN_API bool P4KadanSetupCanFinish();
KADAN_API bool P4KadanProjectConfig(const FString& GameIni, const FString& ProjectIni,
    FString& Updated, int32& Channels, int32& Profiles, FString& Why);

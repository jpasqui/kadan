#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "P4KadanProjectSetup.h"

// A project where the P4Stubs plugin is there but off, as in Unreal's own Plugins folder or after it was turned off:
// Kadan says so, and setup turns it on in the project file instead of stopping at the same message every time. Run it
// in such a project; in a project that has P4Stubs on, there is nothing to check.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SetupTurnsOnDeclarationsTest, "P4MapKit.Kadan.SetupTurnsOnDeclarations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SetupTurnsOnDeclarationsTest::RunTest(const FString&)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("P4Stubs"));
    if (!Plugin.IsValid() || Plugin->IsEnabled())
    {
        AddInfo(TEXT("P4Stubs is missing or on in this project, so there is nothing to turn on."));
        return true;
    }
    FString Why;
    TestFalse(TEXT("The project is not ready while P4Stubs is off"), P4KadanProjectReady(Why));
    TestTrue(TEXT("Kadan says P4Stubs is off: ") + Why, Why.Contains(TEXT("turned off")));
    TestTrue(TEXT("Setup can finish the project"), P4KadanSetupCanFinish());
    // No game folder: turning the plugin on comes before reading the game's settings.
    const FP4KadanProjectSetupResult Result = P4KadanSetupProject(FString(), FString());
    TestTrue(TEXT("Setup succeeds: ") + Result.Message, Result.Ok);
    TestTrue(TEXT("Setup asks for a restart"), Result.RestartRequired && P4KadanProjectRestartRequired());
    TestTrue(TEXT("Setup says it turned P4Stubs on"), Result.Message.Contains(TEXT("Turned on the P4Stubs plugin")));
    FString Project;
    TestTrue(TEXT("The project file reads"), FFileHelper::LoadFileToString(Project, *FPaths::GetProjectFilePath()));
    const int32 At = Project.Find(TEXT("\"P4Stubs\""));
    // Unreal drops the line for a plugin of the project's own, which is on unless the project says otherwise, and
    // writes "Enabled": true for one in the engine's Plugins folder.
    TestTrue(TEXT("The project file turns P4Stubs on"),
        At == INDEX_NONE || Project.Mid(At, 120).Replace(TEXT(" "), TEXT("")).Contains(TEXT("\"Enabled\":true")));
    P4KadanProjectReady(Why);
    TestTrue(TEXT("Until the restart, Kadan asks for it: ") + Why, Why.Contains(TEXT("Restart Unreal Editor")));
    return !HasAnyErrors();
}
#endif

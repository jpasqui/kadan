#if P4_MAPKIT_TESTS
#include "P4KadanProjectSetup.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NavigationDisplayTest, "P4MapKit.Kadan.NavigationDisplayOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NavigationDisplayTest::RunTest(const FString&)
{
    FString Game = TEXT("[/Script/Engine.Engine]\nWorldSettingsClassName=/Script/Carrot4.MyWorldSettings\n")
        TEXT("NavigationSystemClassName=/Script/Carrot4.CarrotNavigationSystem\n[/Script/Engine.CollisionProfile]\n");
    for (int32 I = 1; I <= 18; ++I)
        Game += FString::Printf(
            TEXT("+DefaultChannelResponses=(Channel=ECC_GameTraceChannel%d,Name=\"Game%d\")\n"), I, I);
    Game += TEXT("+Profiles=(Name=\"CP_Terrain\",CollisionEnabled=QueryAndPhysics)\n")
        TEXT("[/Script/NavigationSystem.RecastNavMesh]\nDrawOffset=1.000000\nRegionPartitioning=ChunkyMonotone\n") TEXT(
            "[/Script/Carrot4.RecastNavMeshPikmin]\nCellSize=5.f\nCellHeight=9.f\nAgentRadius=12.f\nTileSizeUU=1000.f\n");
    FString Updated, Again, Error;
    int32 Channels = 0, Profiles = 0;
    if (!TestTrue(TEXT("Actual game-shaped configuration is accepted"),
            P4KadanProjectConfig(Game, FString(), Updated, Channels, Profiles, Error)))
        return false;
    TestTrue(TEXT("The editor overlay clears the game's shallow draw offset"),
        Updated.Contains(TEXT("DrawOffset=20.000000")));
    TestTrue(TEXT("Bake settings retain the game's voxel size, height and agent"),
        Updated.Contains(TEXT("CellSize=5.f")) && Updated.Contains(TEXT("CellHeight=9.f")) &&
            Updated.Contains(TEXT("AgentRadius=12.f")) && Updated.Contains(TEXT("TileSizeUU=1000.f")));
    const FString Old = TEXT("[/Script/NavigationSystem.RecastNavMesh]\nDrawOffset=1.000000\n");
    TestTrue(TEXT("An earlier copied game draw offset is repaired"),
        P4KadanProjectConfig(Game, Old, Updated, Channels, Profiles, Error) &&
            Updated.Contains(TEXT("DrawOffset=20.000000")));
    const FString Own = TEXT("[/Script/NavigationSystem.RecastNavMesh]\nDrawOffset=45.000000\n");
    TestTrue(TEXT("A creator's different display preference is preserved"),
        P4KadanProjectConfig(Game, Own, Updated, Channels, Profiles, Error) &&
            Updated.Contains(TEXT("DrawOffset=45.000000")));
    TestTrue(TEXT("Repeated setup is accepted"), P4KadanProjectConfig(Game, Updated, Again, Channels, Profiles, Error));
    TestEqual(TEXT("Repeated setup does not duplicate display settings"), Again, Updated);
    return !HasAnyErrors();
}
#endif

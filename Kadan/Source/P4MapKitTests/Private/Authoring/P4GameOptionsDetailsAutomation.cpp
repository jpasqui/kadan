#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "P4GameOptionsDetails.h"

// A gate's Game Options are the options its placement row holds: its length and nav box choice and GActor's row
// options, not the class settings its row does not hold (bAddWall, the widths); an actor that is not one of the
// game's gameplay objects has none.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameOptionsDetailsTest, "P4MapKit.Editor.GameOptionsDetails",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameOptionsDetailsTest::RunTest(const FString&)
{
    UClass* Gate = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.VarGateBaseActor"));
    if (!TestNotNull(TEXT("the game's gate class is declared (P4Stubs)"), Gate))
        return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false, TEXT("GameOptionsDetails"), GetTransientPackage());
    if (!TestNotNull(TEXT("a world for the test"), World))
        return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    AActor* Actor = World->SpawnActor<AActor>(Gate);
    if (!TestNotNull(TEXT("a gate"), Actor))
        return false;
    TArray<FString> Names;
    for (const TPair<UObject*, FProperty*>& Option : P4GameOptionsOf(Actor))
        Names.Add((Option.Key == Actor ? FString() : Option.Key->GetClass()->GetName() + TEXT(".")) +
            Option.Value->GetName());
    AddInfo(TEXT("Game Options: ") + FString::Join(Names, TEXT(", ")));
    for (const TCHAR* Held : {TEXT("AddWallNum"), TEXT("bUseHigherNavBox"), TEXT("DemoBindName"), TEXT("AddActorTags")})
        TestTrue(FString(TEXT("the row's option is there: ")) + Held, Names.Contains(Held));
    for (const TCHAR* Class : {TEXT("bAddWall"), TEXT("WallWidth"), TEXT("CID"), TEXT("PikminColor")})
        TestFalse(FString(TEXT("a setting the row does not hold is not: ")) + Class, Names.Contains(Class));
    // What reaches the game is editable: its length, Kadan's row settings and the engine's; its width is not.
    const auto Reaches = [&](const TCHAR* Name)
    {
        return P4GameOptionReachesGame(Actor, Actor, FindFProperty<FProperty>(Gate, Name));
    };
    TestTrue(TEXT("its length reaches the game"), Reaches(TEXT("AddWallNum")));
    TestTrue(TEXT("its generator settings reach the game"), Reaches(TEXT("GenerateInfo")));
    TestFalse(TEXT("its wall width does not"), Reaches(TEXT("WallWidth")));
    TestFalse(TEXT("its class id does not"), Reaches(TEXT("CID")));
    TestTrue(TEXT("an engine property is not the game's to judge"),
        P4GameOptionReachesGame(Actor, Actor, FindFProperty<FProperty>(AActor::StaticClass(), TEXT("Tags"))));

    UClass* Circulator = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CirculatorActor"));
    if (!TestNotNull(TEXT("the circulator class is declared"), Circulator))
        return false;
    AActor* Added = World->SpawnActor<AActor>(Circulator);
    if (!TestNotNull(TEXT("a new circulator placement"), Added))
        return false;
    FProperty* DemoBind = FindFProperty<FProperty>(Circulator, TEXT("DemoBindName"));
    TestTrue(TEXT("the new placement shows the inherited option"),
        P4GameOptionsOf(Added).Contains(TPair<UObject*, FProperty*>(Added, DemoBind)));
    TestFalse(TEXT("the new placement cannot write an option in an unverified blob"),
        P4GameOptionReachesGame(Added, Added, DemoBind));

    AStaticMeshActor* Plain = World->SpawnActor<AStaticMeshActor>();
    TestEqual(TEXT("a mesh actor has no game options"), P4GameOptionsOf(Plain).Num(), 0);
    return true;
}
#endif

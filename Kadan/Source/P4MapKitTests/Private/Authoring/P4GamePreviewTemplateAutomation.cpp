#if P4_MAPKIT_TESTS
#include "P4GamePreviewTemplate.h"
#include "P4GamePreviewTestGame.h"
#include "P4KadanContainerIndex.h"
#include "Misc/AutomationTest.h"

namespace
{
// The rule a new level's Play used before the template record: fixed stock packages, rows and names.
const FString OldWorld(TEXT("/Game/Carrot4/Maps/Main/Area/Area001/Area001_P"));
const FString OldBase(TEXT("/Game/Carrot4/Maps/Main/Area/Area002/ActorPlacementInfo/AP_Area002_P_Objects"));
const FString OldBaseList(TEXT("/Game/Carrot4/Maps/Main/Area/Area002/ActorPlacementInfo/APL_Area002_P"));
const FString OldCourseIn(TEXT("/Game/Carrot4/Demo/PlayParam/Area/Area002/DPP_Area002_CourseIn"));
const FString OldDemoList(TEXT("/Game/Carrot4/Demo/PlayParam/PlayParamList/Area/DemoPlayParamList_Area002"));
const FString OldSequence(
    TEXT("/Game/Carrot4/Demo/Area/Area002/Area002_CourseIn/LS_Demo_Area002_CourseIn.LS_Demo_Area002_CourseIn"));
} // namespace

// The template record and the game's scene data choose the same stock content, rows and names as the
// fixed rule they replace, and the name-token rule holds for every area scene the game has.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GamePreviewTemplateTest, "P4MapKit.Game.PreviewTemplate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GamePreviewTemplateTest::RunTest(const FString&)
{
    using namespace P4GamePreviewTestGame;
    FP4KadanImportSettings Settings;
    FString Error;
    if (!GameSettings(Settings, Error))
    {
        AddError(Error);
        return false;
    }
    FP4GamePreviewTemplate Template;
    if (!TestTrue(TEXT("the template resolves from the record and the game's data"),
            P4ReadGamePreviewTemplate(Settings, Template, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("the world is the old rule's"), Template.World, OldWorld);
    TestEqual(TEXT("the base list is the old rule's"), Template.BaseList, OldBaseList);
    TestEqual(TEXT("the base placement asset is the old rule's"), Template.Base, OldBase);
    TestEqual(TEXT("the demo list is the old rule's"), Template.DemoList, OldDemoList);
    TestEqual(TEXT("the arrival demo is the old rule's"), Template.Demo, OldCourseIn);
    TestEqual(TEXT("the world token is the old mapping's first name"), Template.WorldToken, FString(TEXT("Area001")));
    TestEqual(
        TEXT("the arrival token is the old mapping's second name"), Template.ArrivalToken, FString(TEXT("Area002")));

    // The old rule kept the world's streams whose names end _GameArea and _Player.
    FPackage World;
    if (!TestTrue(TEXT("the template world reads"), Read(Settings, Template.World, World, Error)))
    {
        AddError(Error);
        return false;
    }
    TSet<FString> Geometry;
    TSet<FString> Player;
    for (const std::string& Name : World.Inventory.Names)
    {
        const FString Package = FPackageName::ObjectPathToPackageName(UTF8_TO_TCHAR(Name.c_str()));
        if (!FPackageName::IsValidLongPackageName(Package) || Package == Template.World)
            continue;
        if (Package.EndsWith(TEXT("_GameArea")))
            Geometry.Add(Package);
        if (Package.EndsWith(TEXT("_Player")))
            Player.Add(Package);
    }
    TestTrue(TEXT("the old suffix rule finds exactly the record's geometry stream"),
        Geometry.Num() == 1 && Geometry.Contains(Template.GeometryStream));
    TestTrue(TEXT("the old suffix rule finds exactly the record's player stream"),
        Player.Num() == 1 && Player.Contains(Template.PlayerStream));

    // The old rule copied rows 1 and 46 and required a pod and a camp there.
    TArray<FString> Classes;
    if (!TestTrue(
            TEXT("the base rows read"), P4ReadGamePreviewGeneratorClasses(Settings, Template.Base, Classes, Error)))
    {
        AddError(Error);
        return false;
    }
    TArray<int32> Rows;
    for (const FString& Class : Template.BaseClasses)
        Rows.Add(Classes.IndexOfByKey(Class));
    Rows.Sort();
    AddInfo(FString::Printf(TEXT("%d generator rows; base rows %s"), Classes.Num(),
        *FString::JoinBy(Rows, TEXT(","), [](int32 Row) { return FString::FromInt(Row); })));
    TestTrue(TEXT("the first row of each base class is the old rule's rows 1 and 46"),
        Rows.Num() == 2 && Rows[0] == 1 && Rows[1] == 46);
    TestTrue(TEXT("the old rule's class checks hold for those rows"),
        Classes.Num() > 46 && Classes[1].EndsWith(TEXT(".GPod_C")) && Classes[46].EndsWith(TEXT(".GPodCampSimple_C")));

    // The old rule required the arrival message Area002_00, the Area002 sequence and the GHappy binding.
    FPackage Demo;
    if (!TestTrue(TEXT("the arrival demo reads"), Read(Settings, Template.Demo, Demo, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Message = Value(Demo, "OverrideMsgId");
    TestEqual(TEXT("the template message is the old rule's"), Message, FString(TEXT("Area002_00")));
    TestEqual(TEXT("the sequence is the old rule's"), Value(Demo, "SequenceAsset"), OldSequence);
    TestEqual(TEXT("the binding is the old rule's"), Value(Demo, "BindBaseName"), FString(TEXT("GHappy")));
    for (const uint32 Area : {5u, 7u, 8u, 9u})
    {
        const FString Name = FString::Printf(TEXT("Area%03u"), Area);
        const auto Renamed = [&](const FString& Package, const FString& Token)
        { return P4GamePreviewRename(FPackageName::GetShortName(Package), Token, Name); };
        TestEqual(
            TEXT("renamed message"), P4GamePreviewRename(Message, Template.ArrivalToken, Name), Name + TEXT("_00"));
        TestEqual(TEXT("renamed world"), Renamed(Template.World, Template.WorldToken), Name + TEXT("_P"));
        TestEqual(TEXT("renamed player"), Renamed(Template.PlayerStream, Template.WorldToken), Name + TEXT("_Player"));
        TestEqual(
            TEXT("renamed geometry"), Renamed(Template.GeometryStream, Template.WorldToken), Name + TEXT("_GameArea"));
        TestEqual(TEXT("renamed demo"), Renamed(Template.Demo, Template.ArrivalToken),
            TEXT("DPP_") + Name + TEXT("_CourseIn"));
        TestEqual(TEXT("renamed demo list"), Renamed(Template.DemoList, Template.ArrivalToken),
            TEXT("DemoPlayParamList_") + Name);
    }

    // Over every area scene of the game: the token of the world and scene names is the scene's own
    // name without its SD_ prefix, the name the game's name-based lookups use.
    FP4KadanContainerIndex Index;
    TArray<FP4KadanContainerPackage> Packages;
    if (!TestTrue(TEXT("the container index reads"),
            P4KadanReadContainerIndex(Settings, Index, Error) && Index.Enumerate(Packages, Error)))
    {
        AddError(Error);
        return false;
    }
    int32 Scenes = 0;
    for (const FP4KadanContainerPackage& Entry : Packages)
    {
        const FString Leaf = FPackageName::GetShortName(Entry.Package);
        if (!Entry.Package.Contains(TEXT("/GameScene/")) || !Leaf.StartsWith(TEXT("SD_Area")))
            continue;
        FP4GamePreviewScene Scene;
        if (!P4ReadGamePreviewScene(Settings, Entry.Package, Scene, Error))
            continue;
        ++Scenes;
        TestEqual(TEXT("token of ") + Leaf, P4GamePreviewNameToken(FPackageName::GetShortName(Scene.World), Leaf),
            Leaf.Mid(3));
    }
    AddInfo(FString::Printf(TEXT("%d area scenes with a world"), Scenes));
    TestTrue(TEXT("the game's area scenes were read"), Scenes >= 8);
    return true;
}
#endif

#if P4_MAPKIT_TESTS
#include "P4KadanPathDefaults.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanPathDiscoveryTest, "P4MapKit.Kadan.PathDiscovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanPathDiscoveryTest::RunTest(const FString&)
{
    const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    const FString Root = Saved / TEXT("Kadan/UiPathDiscovery");
    if (!TestTrue(TEXT("Fixture stays inside this project's Saved directory"), FPaths::IsUnderDirectory(Root, Saved)))
        return false;
    if (IFileManager::Get().DirectoryExists(*Root))
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    };
    const FString Plugin = Root / TEXT("plugin"), Project = Root / TEXT("project"), Cache = Root / TEXT("saved"),
                  Search = Root / TEXT("path");
    auto File = [&](const FString& Name, const FString& Text)
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Name), true);
        return TestTrue(TEXT("Make tiny path selection fixture; no executable is run"),
            FFileHelper::SaveStringToFile(Text, *Name, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    };
    FP4KadanPathDefaults Current;
    auto Found = P4KadanDiscoverPaths(Current, Plugin, Project, Cache, Search);
    TestEqual(TEXT("Missing tools are explicit"), Found.MissingTools.Num(), 2);
    TestEqual(TEXT("Output default is owned by the project"), Found.ExportFolder, Project / TEXT("Exports"));
    TestFalse(
        TEXT("Discovery never creates the output directory"), IFileManager::Get().DirectoryExists(*Found.ExportFolder));
    TestFalse(TEXT("An unverified Paks folder is not selected"), !Found.GamePaks.IsEmpty());
    TestEqual(TEXT("Loose source default is project local"), Found.GameFolder, Project / TEXT("GamePackages"));
    File(Plugin / TEXT("Tools/retoc.exe"), TEXT("locator fixture"));
    File(Project / TEXT("Tools/repak.exe"), TEXT("locator fixture"));
    File(Search / TEXT("retoc.exe"), TEXT("locator fixture"));
    Found = P4KadanDiscoverPaths(Current, Plugin, Project, Cache, TEXT("\"") + Search + TEXT("\""));
    TestEqual(TEXT("Portable plugin tool precedes PATH"), Found.Retoc, Plugin / TEXT("Tools/retoc.exe"));
    TestEqual(TEXT("Project local tool is discovered"), Found.Repak, Project / TEXT("Tools/repak.exe"));
    TestEqual(TEXT("Both found tools remove missing guidance"), Found.MissingTools.Num(), 0);
    Current.Retoc = Search / TEXT("retoc.exe");
    Current.ExportFolder = Project / TEXT("my-output");
    Current.GameFolder = Root / TEXT("explicit-source");
    Current.GamePaks = Root / TEXT("explicit-paks");
    Found = P4KadanDiscoverPaths(Current, Plugin, Project, Cache, Search);
    TestEqual(TEXT("Valid explicit tool wins over plugin defaults"), Found.Retoc, Current.Retoc);
    TestEqual(TEXT("Explicit output is preserved before it exists"), Found.ExportFolder, Current.ExportFolder);
    TestEqual(TEXT("Explicit source is not silently replaced"), Found.GameFolder, Current.GameFolder);
    TestEqual(TEXT("Explicit Paks is not silently replaced"), Found.GamePaks, Current.GamePaks);
    Current = FP4KadanPathDefaults();
    Current.Retoc = Root / TEXT("missing.exe");
    File(Plugin / TEXT("Tools/retoc.exe"), TEXT(""));
    Found = P4KadanDiscoverPaths(Current, Plugin, Project, Cache, Search);
    TestEqual(TEXT("Invalid/empty tool candidates fall through to a real PATH file"), Found.Retoc,
        Search / TEXT("retoc.exe"));
    IFileManager::Get().MakeDirectory(*(Cache / TEXT("Kadan/ConvertedOriginals/Carrot4/Content")), true);
    File(Project / TEXT("GamePaks/unrelated.pak"), TEXT("marker"));
    Found = P4KadanDiscoverPaths(FP4KadanPathDefaults(), Plugin, Project, Cache, TEXT("relative-path"));
    TestEqual(
        TEXT("Existing converted game cache is reused"), Found.GameFolder, Cache / TEXT("Kadan/ConvertedOriginals"));
    TestTrue(TEXT("A relative PATH entry is ignored"), Found.MissingTools.Contains(TEXT("retoc.exe")));
    TestTrue(TEXT("An unrelated pak is not accepted"), Found.GamePaks.IsEmpty());
    File(Project / TEXT("GamePaks/Carrot4-Switch.pak"), TEXT("marker"));
    Found = P4KadanDiscoverPaths(FP4KadanPathDefaults(), Plugin, Project, Cache, Search);
    TestEqual(TEXT("Only an identified game Paks folder is auto-selected"), Found.GamePaks, Project / TEXT("GamePaks"));
    return !HasAnyErrors();
}
#endif

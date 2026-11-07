#include "CoreMinimal.h"
#include "P4OriginalLevelImport.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PortableSourceCustodyPaths, "P4MapKit.Kadan.PortableSourceCustodyPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PortableSourceCustodyPaths::RunTest(const FString&)
{
    const auto Full = [](FString Path)
    {
        Path = FPaths::ConvertRelativePathToFull(Path);
        FPaths::NormalizeFilename(Path);
        FPaths::CollapseRelativeDirectories(Path);
        return Path;
    };
    // A source under the project is written relative to it, with nothing of the folder the project is in.
    const FString InProject = Full(
        FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals/Carrot4/Content/Maps/Level.umap"));
    const FString ProjectForm = P4PortableSourceCustodyPath(InProject);
    TestEqual(TEXT("A source inside the project is written from the project's folder"), ProjectForm,
        FString(TEXT("{project}/Saved/Kadan/ConvertedOriginals/Carrot4/Content/Maps/Level.umap")));
    TestFalse(TEXT("The written form names no drive"), ProjectForm.Contains(TEXT(":")));
    TestTrue(TEXT("It resolves to the file in the project where it is now"),
        FPaths::IsSamePath(P4ResolveSourceCustodyPath(ProjectForm), InProject));
    // A source under the game folder set on the Import page is written from that folder.
    FString Before;
    const bool HadBefore = GConfig->GetString(TEXT("Kadan"), TEXT("GameFolder"), Before, GEditorPerProjectIni);
    ON_SCOPE_EXIT
    {
        if (HadBefore)
            GConfig->SetString(TEXT("Kadan"), TEXT("GameFolder"), *Before, GEditorPerProjectIni);
        else
            GConfig->RemoveKey(TEXT("Kadan"), TEXT("GameFolder"), GEditorPerProjectIni);
    };
    const FString Game = Full(FPaths::EngineIntermediateDir() / TEXT("P4PortableCustodyGame"));
    GConfig->SetString(TEXT("Kadan"), TEXT("GameFolder"), *Game, GEditorPerProjectIni);
    const FString InGame = Game + TEXT("/Carrot4/Content/Maps/Level.uexp");
    TestEqual(TEXT("A source inside the game folder is written from the game folder"),
        P4PortableSourceCustodyPath(InGame), FString(TEXT("{game}/Carrot4/Content/Maps/Level.uexp")));
    const FString Moved = Full(FPaths::EngineIntermediateDir() / TEXT("P4PortableCustodyGameMoved"));
    GConfig->SetString(TEXT("Kadan"), TEXT("GameFolder"), *Moved, GEditorPerProjectIni);
    TestTrue(TEXT("It resolves in the game folder set now, on this machine"),
        FPaths::IsSamePath(P4ResolveSourceCustodyPath(TEXT("{game}/Carrot4/Content/Maps/Level.uexp")),
            Moved + TEXT("/Carrot4/Content/Maps/Level.uexp")));
    // Anything else, and custody written before, keeps its full path.
    const FString Elsewhere = Full(FPaths::EngineIntermediateDir() / TEXT("P4PortableCustodyElsewhere/Level.umap"));
    TestEqual(TEXT("A source outside both is written in full"), P4PortableSourceCustodyPath(Elsewhere), Elsewhere);
    TestEqual(
        TEXT("A full path from older custody is read as it is"), P4ResolveSourceCustodyPath(Elsewhere), Elsewhere);
    TestTrue(TEXT("A written form cannot leave its folder"),
        P4ResolveSourceCustodyPath(TEXT("{project}/../Other/Level.umap")).IsEmpty());
    GConfig->RemoveKey(TEXT("Kadan"), TEXT("GameFolder"), GEditorPerProjectIni);
    TestTrue(TEXT("A game-folder form without a game folder set resolves to nothing"),
        P4ResolveSourceCustodyPath(TEXT("{game}/Carrot4/Level.umap")).IsEmpty());
    return !HasAnyErrors();
}
#endif

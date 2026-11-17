#include "P4ExportPublication.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanExportPublication, "P4MapKit.Kadan.ExportPublication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanExportPublication::RunTest(const FString&)
{
    const FString Parent = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/PublicationTests"));
    const FString Root = Parent / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("The owned fixture stays inside its intended directory"), FPaths::IsUnderDirectory(Root, Parent)))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    };
    // The game's mod folder, where every folder is loaded as a mod, and the export's work folder beside it.
    const FString Mods = Root / TEXT("mods");
    const FString Work = Root / TEXT("work");
    const FString Target = Mods / TEXT("published");
    const FString Prepared = Work / TEXT("prepared");
    IFileManager::Get().MakeDirectory(*Target, true);
    IFileManager::Get().MakeDirectory(*Prepared, true);
    const FString Leaf = TEXT("fixture.txt");
    TestTrue(TEXT("The previous fixture is written"), FFileHelper::SaveStringToFile(TEXT("previous"), *(Target / Leaf)));
    TestTrue(TEXT("The prepared fixture is written"), FFileHelper::SaveStringToFile(TEXT("new"), *(Prepared / Leaf)));
    TArray<FString> Told;
    FString Error;
    // The replaced copy waits in the work folder, never in the mod folder, and an export of the mod to another folder
    // shares the work folder but not that copy.
    const FString Replaced = P4ReplacedExportFolder(Prepared, Target);
    const FString Legacy = Target + TEXT(".previous");
    TestEqual(TEXT("The replaced copy waits in the work folder"), FPaths::GetPath(Replaced), Work);
    TestNotEqual(TEXT("Another published folder has its own replaced copy"),
        P4ReplacedExportFolder(Prepared, Root / TEXT("other/published")), Replaced);
    const auto OnlyPublished = [&]()
    {
        TArray<FString> Children;
        IFileManager::Get().FindFiles(Children, *(Mods / TEXT("*")), true, true);
        return Children.Num() == 1 && Children[0] == TEXT("published") &&
            !IFileManager::Get().DirectoryExists(*Replaced);
    };
    const auto Read = [&]()
    {
        FString Text;
        FFileHelper::LoadFileToString(Text, *(Target / Leaf));
        return Text;
    };
    const auto Prepare = [&](const TCHAR* Text)
    {
        IFileManager::Get().MakeDirectory(*Prepared, true);
        FFileHelper::SaveStringToFile(Text, *(Prepared / Leaf));
    };
    const auto Leave = [&](const FString& Old)
    {
        IFileManager::Get().MakeDirectory(*Old, true);
        FFileHelper::SaveStringToFile(TEXT("older"), *(Old / Leaf));
    };

    TestTrue(TEXT("A validated prepared directory publishes"), P4PublishPreparedExport(Prepared, Target, true, Told, Error));
    TestEqual(TEXT("Publication contains the new bytes"), Read(), FString(TEXT("new")));
    TestTrue(TEXT("Successful publication leaves no previous copy anywhere"), OnlyPublished());
    TestFalse(TEXT("A missing prepared directory fails publication"), P4PublishPreparedExport(Prepared, Target, true, Told, Error));
    TestTrue(TEXT("The diagnostic retains the actual operating-system failure"), Error.Contains(TEXT("Windows error")));
    TestEqual(TEXT("Rollback restores the prior publication with its exact bytes"), Read(), FString(TEXT("new")));
    TestTrue(TEXT("A successful rollback leaves no recovery directory"), OnlyPublished());

    // The old copy an earlier publication could not remove (the game had its files open) is that export's own, and
    // the next publication removes it before it goes on: the copy in the work folder, and the <mod>.previous folder an
    // earlier version of Kadan left in the mod folder.
    for (const FString& Old : {Replaced, Legacy})
    {
        Leave(Old);
        Prepare(TEXT("newest"));
        Told.Reset();
        TestTrue(TEXT("A left-behind old copy does not stop publication: ") + Old,
            P4PublishPreparedExport(Prepared, Target, true, Told, Error));
        TestFalse(TEXT("The left-behind old copy is gone: ") + Old, IFileManager::Get().DirectoryExists(*Old));
        TestTrue(TEXT("The publication says it removed it"),
            Told.ContainsByPredicate([&](const FString& Line) { return Line.Contains(Old); }));
        TestTrue(TEXT("Nothing but the publication is left"), OnlyPublished());
        TestEqual(TEXT("The new publication has the newest bytes"), Read(), FString(TEXT("newest")));
    }

    // Without a publication, the old copy is all there is of the earlier export: it stays, publication waits, and the
    // refusal says where the copy is and where it goes back to. Put back that way, the next publication replaces it.
    for (const FString& Old : {Replaced, Legacy})
    {
        IFileManager::Get().DeleteDirectory(*Target, false, true);
        Leave(Old);
        Prepare(TEXT("after"));
        TestFalse(TEXT("An unresolved transaction is preserved: ") + Old,
            P4PublishPreparedExport(Prepared, Target, false, Told, Error));
        TestTrue(TEXT("The unresolved recovery directory remains"), IFileManager::Get().DirectoryExists(*Old));
        TestTrue(TEXT("The refusal names the recovery directory"), Error.Contains(Old));
        TestTrue(TEXT("The refusal names where it goes back to"), Error.Contains(Target));
        TestTrue(TEXT("The recovery directory moves back"), IFileManager::Get().Move(*Target, *Old));
        TestEqual(TEXT("The earlier export is back"), Read(), FString(TEXT("older")));
        TestTrue(TEXT("Publication goes on once it is back"), P4PublishPreparedExport(Prepared, Target, true, Told, Error));
        TestEqual(TEXT("The publication has the new bytes"), Read(), FString(TEXT("after")));
        TestTrue(TEXT("Nothing but the publication is left after it"), OnlyPublished());
    }
    return !HasAnyErrors();
}
#endif

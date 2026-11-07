#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"
#include "P4ExportWork.h"

// Two exports of one mod to different destinations share the mod's work folder. While one of them holds it, the other
// refuses before it resets the work folder's prepared and cooked files.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ExportWorkLockTest, "P4MapKit.Export.WorkFolderLock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ExportWorkLockTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("test.worklock"), TEXT("Work lock"), RequestData, Names))
        return false;
    UTexture2D* Texture = Cast<UTexture2D>(ImportOwn(*this, Names.PackageRoot, TEXT("T_WorkLock"), false));
    if (!TestNotNull(TEXT("The creator texture is imported"), Texture))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Texture);
        Texture->ClearFlags(RF_Public | RF_Standalone);
        Texture->GetOutermost()->SetDirtyFlag(false);
        Texture->GetOutermost()->MarkPendingKill();
        Texture->MarkPendingKill();
    };
    FP4ExportRoot Root;
    Root.Path = Texture->GetOutermost()->GetName();
    RequestData.Roots.Add(Root);
    // The other export: it claimed the work folder and has prepared files in it, and holds the folder's lock.
    const FString Working = P4ExportWorkingDirectory(RequestData.OutputDirectory, RequestData.WorkDirectory,
        RequestData.Mod.Id, {FPaths::ProjectSavedDir(), FPaths::ProjectContentDir()});
    const FString Prepared = Working / TEXT("prepared/other-export.txt");
    if (!TestTrue(TEXT("The other export's work is in place"),
            !Working.IsEmpty() &&
                FFileHelper::SaveStringToFile(RequestData.Mod.Id, *(Working / TEXT("identity.txt")),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
                FFileHelper::SaveStringToFile(TEXT("prepared"), *Prepared)))
        return false;
    {
        TUniquePtr<FArchive> Held(IFileManager::Get().CreateFileWriter(*(Working + TEXT(".lock"))));
        if (!TestTrue(TEXT("The other export holds the work folder"), Held.IsValid()))
            return false;
        FString Error;
        FP4ExportModResult Exported;
        TestFalse(TEXT("An export to another destination refuses the held work folder"),
            P4ExportMod(RequestData, Exported, Error));
        TestTrue(TEXT("The other export's prepared files are left alone"), IFileManager::Get().FileExists(*Prepared));
    }
    IFileManager::Get().Delete(*(Working + TEXT(".lock")));
    FString Error;
    FP4ExportModResult Exported;
    if (!TestTrue(TEXT("Once the work folder is free the export runs"), P4ExportMod(RequestData, Exported, Error)))
        AddError(Error);
    TestFalse(
        TEXT("The export's own lock is gone after it"), IFileManager::Get().FileExists(*(Working + TEXT(".lock"))));
    return !HasAnyErrors();
}
#endif

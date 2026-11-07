#if P4_MAPKIT_TESTS
#include "P4KadanCreatorReview.h"
#include "P4CreatorSource.h"
#include "AssetImportTask.h"
#include "AssetRegistryModule.h"
#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "Curves/CurveFloat.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanCreatorReviewTest, "P4MapKit.Kadan.CreatorReview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanCreatorReviewTest::RunTest(const FString&)
{
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("creator.review");
    Mod.Name = TEXT("Creator review");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    const TArray<FString> FixtureNames = {TEXT("Texture"), TEXT("UnqualifiedTexture"), TEXT("Curve")};
    for (const FString& Name : FixtureNames)
    {
        const FString Destination = Names.PackageRoot + TEXT("/") + Name;
        if (FPackageName::DoesPackageExist(Destination) || FindPackage(nullptr, *Destination))
        {
            AddError(TEXT("CreatorReview fixture destination already exists: ") + Destination);
            return false;
        }
    }
    const FString InputDirectory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/CreatorReviewTest");
    const FString ImageFile = FPaths::ConvertRelativePathToFull(InputDirectory / TEXT("creator.tga"));
    if (IFileManager::Get().FileExists(*ImageFile))
    {
        AddError(TEXT("CreatorReview fixture input already exists: ") + ImageFile);
        return false;
    }
    ON_SCOPE_EXIT
    {
        // Only the exact destinations checked absent above belong to this run.
        TArray<UPackage*> Packages;
        for (const FString& Name : FixtureNames)
            if (UPackage* Existing = FindPackage(nullptr, *(Names.PackageRoot + TEXT("/") + Name)))
            {
                if (UObject* Asset = FindObject<UObject>(Existing, *Name))
                {
                    FAssetRegistryModule::AssetDeleted(Asset);
                    Asset->ClearFlags(RF_Public | RF_Standalone);
                }
                Existing->SetDirtyFlag(false);
                Packages.Add(Existing);
            }
        UPackageTools::UnloadPackages(Packages);
        IFileManager::Get().Delete(*ImageFile, false, true);
    };
    TStrongObjectPtr<UPackage> UnqualifiedPackage(CreatePackage(*(Names.PackageRoot + TEXT("/UnqualifiedTexture"))));
    TStrongObjectPtr<UTexture2D> UnqualifiedTexture(
        NewObject<UTexture2D>(UnqualifiedPackage.Get(), TEXT("UnqualifiedTexture"), RF_Public | RF_Standalone));
    FAssetRegistryModule::AssetCreated(UnqualifiedTexture.Get());
    FP4ExportSelectionPreview Preview;
    FP4ExportSelectionRow Own;
    Own.Package = UnqualifiedPackage->GetName();
    Own.Disposition = EP4ExportDisposition::CreatorPackage;
    Preview.Rows.Add(Own);
    const bool UnqualifiedDirty = UnqualifiedPackage->IsDirty();
    const auto Unqualified = P4ReviewCreatorSelection(Mod, Preview, nullptr);
    TestFalse(
        TEXT("A naked new texture has no qualified creator source"), P4HasCreatorSource(UnqualifiedTexture.Get()));
    TestFalse(TEXT("An unqualified own texture blocks review"), Unqualified.CanExport());
    TestEqual(TEXT("Unqualified own texture ships nothing"), Unqualified.Ships, 0);
    TestTrue(TEXT("Unqualified texture row has a visible refusal"),
        Unqualified.Rows.Num() == 1 && Unqualified.Rows[0].Disposition == EP4ExportDisposition::Refused &&
            !Unqualified.Rows[0].Reason.IsEmpty());
    TestEqual(TEXT("Unqualified review preserves dirty state"), UnqualifiedPackage->IsDirty(), UnqualifiedDirty);

    IFileManager::Get().MakeDirectory(*InputDirectory, true);
    const TArray<uint8> Tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40, 0, 0, 255, 255, 0, 255, 0, 255,
        255, 0, 0, 255, 0, 255, 255, 255};
    if (!TestTrue(TEXT("Write actual creator review image input"), FFileHelper::SaveArrayToFile(Tga, *ImageFile)))
        return false;
    TStrongObjectPtr<UPackage> Package(CreatePackage(*(Names.PackageRoot + TEXT("/Texture"))));
    TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
    Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
    Factory->AssetImportTask->bAutomated = true;
    bool Canceled = false;
    TStrongObjectPtr<UTexture2D> Texture(
        Cast<UTexture2D>(Factory->ImportObject(UTexture2D::StaticClass(), Package.Get(), FName(TEXT("Texture")),
            RF_Public | RF_Standalone | RF_Transactional, ImageFile, nullptr, Canceled)));
    Factory->CleanUp();
    TestFalse(TEXT("Actual creator image import was not canceled"), Canceled);
    if (!TestNotNull(TEXT("Actual texture factory returned decoded image"), Texture.Get()))
        return false;
    FAssetRegistryModule::AssetCreated(Texture.Get());
    if (!TestTrue(TEXT("Actual supported external image import is qualified creator source"),
            P4HasCreatorSource(Texture.Get())))
        return false;
    Preview.Rows.Reset();
    Own.Package = Package->GetName();
    Preview.Rows.Add(Own);
    FP4ExportSelectionRow Game;
    Game.Package = TEXT("/Game/OriginalReference");
    Game.Disposition = EP4ExportDisposition::UnchangedGame;
    Preview.Rows.Add(Game);
    const bool Dirty = Package->IsDirty();
    const auto Allowed = P4ReviewCreatorSelection(Mod, Preview, nullptr);
    TestTrue(TEXT("Supported texture enables export without a map"), Allowed.CanExport());
    TestEqual(TEXT("Own asset counted once"), Allowed.Ships, 1);
    TestEqual(TEXT("Game reference shown separately"), Allowed.UsesGame, 1);
    TestEqual(TEXT("Successful readiness check preserves dirty state"), Package->IsDirty(), Dirty);
    TStrongObjectPtr<UPackage> CurvePackage(CreatePackage(*(Names.PackageRoot + TEXT("/Curve"))));
    TStrongObjectPtr<UCurveFloat> Curve(
        NewObject<UCurveFloat>(CurvePackage.Get(), TEXT("Curve"), RF_Public | RF_Standalone));
    FAssetRegistryModule::AssetCreated(Curve.Get());
    Own.Package = CurvePackage->GetName();
    Preview.Rows.Add(Own);
    const auto Refused = P4ReviewCreatorSelection(Mod, Preview, nullptr);
    TestFalse(TEXT("Unsupported candidate blocks export during review"), Refused.CanExport());
    TestTrue(TEXT("Refusal names the asset"),
        Refused.Blockers.ContainsByPredicate([&](const FString& Why) { return Why.Contains(Own.Package); }));
    TestTrue(TEXT("Refused row has a visible reason"),
        Refused.Rows.Last().Disposition == EP4ExportDisposition::Refused && !Refused.Rows.Last().Reason.IsEmpty());
    TestEqual(TEXT("Readiness check preserves dirty state"), Package->IsDirty(), Dirty);
    Preview.Rows = {Game};
    Preview.Refused.Reset();
    TestFalse(TEXT("Unchanged game-only selection has nothing to export"),
        P4ReviewCreatorSelection(Mod, Preview, nullptr).CanExport());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanMultipleModsTest, "P4MapKit.Kadan.MultipleMods",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanMultipleModsTest::RunTest(const FString&)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MultipleModsTest");
    FP4ModDescriptor A;
    A.Id = TEXT("creator.first");
    A.Name = TEXT("First mod");
    A.Version = TEXT("1.0.0");
    FP4ModDescriptor B = A;
    B.Id = TEXT("creator.second");
    B.Name = TEXT("Second mod");
    FString Error;
    for (const auto& Mod : {A, B})
    {
        FP4ModDescriptor Previous;
        const bool Exists = P4ReadModDescriptor(Directory, Mod.Id, Previous, Error);
        TestTrue(TEXT("Two identities persist in one project"),
            P4WriteModDescriptor(Directory, Mod, Exists ? &Previous : nullptr, Error));
    }
    FP4ExportRoot Root;
    Root.Path = TEXT("/Game/FirstRoot");
    TestTrue(TEXT("First selection persisted"), P4WriteExportRoots(Directory, A, {Root}, Error));
    Root.Path = TEXT("/Game/SecondRoot");
    TestTrue(TEXT("Second selection persisted"), P4WriteExportRoots(Directory, B, {Root}, Error));
    TestTrue(TEXT("First mod can remove its last root"), P4WriteExportRoots(Directory, A, {}, Error));
    TArray<FP4ExportRoot> Read;
    TestTrue(TEXT("First mod reopens empty"), P4ReadExportRoots(Directory, A, Read, Error));
    TestEqual(TEXT("First mod remains empty"), Read.Num(), 0);
    TestTrue(TEXT("Second mod reopens independently"), P4ReadExportRoots(Directory, B, Read, Error));
    TestEqual(TEXT("Second mod's root is preserved"), Read.Num(), 1);
    if (Read.Num())
        TestEqual(TEXT("Second mod's root identity is preserved"), Read[0].Path, Root.Path);
    TArray<FP4ModDescriptor> Listed;
    TestTrue(TEXT("Project lists both mods"), P4ListModDescriptors(Directory, Listed, Error));
    TestEqual(TEXT("Both mods retained"), Listed.Num(), 2);
    return true;
}
#endif

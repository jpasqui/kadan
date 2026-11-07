#if P4_MAPKIT_TESTS
#include "P4GameReplacement.h"
#include "P4CreatorSource.h"
#include "P4CreatorReferencePreflight.h"
#include "P4ExportContainer.h"
#include "P4ExportDriver.h"
#include "P4ExportProcess.h"
#include "P4ExportSelection.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanGameContent.h"
#include "P4KadanReadOnly.h"
#include "P4LevelChanges.h"
#include "P4OriginalRecord.h"
#include "P4Replacement.h"
#include "AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include <algorithm>
#include <string>
#include <vector>

namespace P4ExportDriverTest
{
bool Request(
    FAutomationTestBase& Test, const FString& Id, const FString& Name, FP4ExportModRequest& Out, FP4ModNames& Names);
bool CookedNames(const FString& Header, std::vector<std::string>& Names);
bool Has(const std::vector<std::string>& Names, const char* Name);
} //namespace P4ExportDriverTest

namespace P4GameReplacementTest
{
// One of the game's own assets of the class in the project (the game tag of its own package), the first
// that Accept takes. Looks at a bounded number so that a project without one does not load every asset.
template <class T>
T* GameAsset(TFunctionRef<bool(T*)> Accept)
{
    FARFilter Filter;
    Filter.ClassNames.Add(T::StaticClass()->GetFName());
    Filter.PackagePaths.Add(TEXT("/Game/Carrot4"));
    Filter.bRecursivePaths = true;
    TArray<FAssetData> Found;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Found);
    for (int32 Index = 0; Index < Found.Num() && Index < 400; ++Index)
    {
        T* Candidate = Cast<T>(Found[Index].GetAsset());
        FString Origin;
        if (Candidate && Candidate->GetClass() == T::StaticClass() && P4HasIntrinsicGameSource(Candidate, &Origin) &&
            Origin == Candidate->GetOutermost()->GetName() && Accept(Candidate))
            return Candidate;
    }
    return nullptr;
}

bool WriteImage(const FString& File, int32 Size, const uint8 (&Bgra)[4])
{
    TArray<uint8> Tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8(Size), 0, uint8(Size), 0, 32, 40};
    for (int32 Pixel = 0; Pixel < Size * Size; ++Pixel)
        Tga.Append(Bgra, 4);
    return FFileHelper::SaveArrayToFile(Tga, *File);
}

const TCHAR* const Cave000 = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");

// The package's cooked file of the extension as the game has it (extracted for the fixture's map), or empty.
TArray<uint8> GameFile(const FString& Extracted, const FString& Package, const TCHAR* Extension)
{
    const FString Content = IFileManager::Get().DirectoryExists(*(Extracted / TEXT("Carrot4/Content/Carrot4")))
        ? Extracted / TEXT("Carrot4/Content")
        : Extracted;
    TArray<uint8> Bytes;
    FFileHelper::LoadFileToArray(Bytes, *(Content / Package.Mid(6) + Extension), FILEREAD_Silent);
    return Bytes;
}
} //namespace P4GameReplacementTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameReplacementClaimsTest, "P4MapKit.Kadan.GameReplacementClaims",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4GameReplacementClaimsTest::RunTest(const FString&)
{
    using namespace P4GameReplacementTest;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(
            *this, TEXT("native.replacement.claims"), TEXT("Replacement claims"), Request, Names))
        return false;
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("The real game inventory is available"),
            P4ReadExportGameInventory(
                Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error)))
    {
        AddError(Error);
        return false;
    }
    UTexture2D* Game = GameAsset<UTexture2D>([](UTexture2D* Texture) { return Texture->Source.IsValid(); });
    if (!TestNotNull(TEXT("A real imported game texture is available"), Game))
        return false;
    UPackage* OriginalPackage = Game->GetOutermost();
    const FString Package = OriginalPackage->GetName();
    UTexture2D* Copy = nullptr;
    ON_SCOPE_EXIT
    {
        if (Copy)
        {
            FAssetRegistryModule::AssetDeleted(Copy);
            Copy->ClearFlags(RF_Public | RF_Standalone);
            Copy->GetOutermost()->SetDirtyFlag(false);
            Copy->MarkPendingKill();
        }
        OriginalPackage->SetDirtyFlag(false);
        FText Why;
        if (!UPackageTools::ReloadPackages({OriginalPackage}, Why, EReloadPackagesInteractionMode::AssumePositive))
            AddError(TEXT("The original fixture texture did not reload: ") + Why.ToString());
    };
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/ReplacementClaims");
    const FString File = FPaths::ConvertRelativePathToFull(Folder / TEXT("creator.tga"));
    const uint8 Color[4] = {0x12, 0x67, 0xCB, 0xFF};
    if (!TestTrue(TEXT("The creator image is written"),
            IFileManager::Get().MakeDirectory(*Folder, true) && WriteImage(File, 8, Color)))
        return false;
    UTexture2D* Replacement = Cast<UTexture2D>(P4ReplaceGameAsset(Game, File, Error));
    if (!TestNotNull(TEXT("The real texture is replaced through Kadan"), Replacement))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("The replacement is at a verified game package"),
        Inventory.Contains(FName(*Package)) && P4IsGameReplacement(Replacement));
    TMap<FName, FString> Claims;
    Claims.Add(FName(*Package), TEXT("another.mod"));
    FP4ExportRoot Root;
    Root.Path = Package;
    FP4ExportSelectionPreview Preview;
    TestTrue(TEXT("Review accepts another mod's replacement of the same game path"),
        P4PreviewExportSelection(Request.Mod, {Root}, Inventory, Claims, Preview, Error) &&
            Preview.Refused.Num() == 0 && Preview.CookCandidates.Contains(Package));
    FP4CreatorReferenceInputs Inputs;
    Inputs.Packages.Add(Package);
    TArray<FString> Referenced;
    TestTrue(TEXT("Live reference preflight accepts stacked game replacements"),
        P4PreflightCreatorReferences(Request.Mod, Inputs, &Inventory, Claims, true, Referenced, Error) &&
            Referenced.Contains(Package));
    const FString CopyPackage = TEXT("/Game/ReplacementClaims/Copy_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Copy = DuplicateObject<UTexture2D>(
        Replacement, CreatePackage(*CopyPackage), *FPackageName::GetShortName(CopyPackage));
    if (!TestNotNull(TEXT("An ordinary creator copy is made"), Copy))
        return false;
    FAssetRegistryModule::AssetCreated(Copy);
    TestFalse(TEXT("The copy is not a replacement at a game path"), P4IsGameReplacement(Copy));
    Claims.Add(FName(*CopyPackage), TEXT("another.mod"));
    Root.Path = CopyPackage;
    TestTrue(TEXT("Ordinary creator-package collisions remain refused by Review"),
        P4PreviewExportSelection(Request.Mod, {Root}, Inventory, Claims, Preview, Error) && Preview.Refused.Num() > 0);
    Inputs.Packages = {CopyPackage};
    TestFalse(TEXT("Ordinary creator-package collisions remain refused by live preflight"),
        P4PreflightCreatorReferences(Request.Mod, Inputs, &Inventory, Claims, true, Referenced, Error));
    return !HasAnyErrors();
}

// A creator replaces one of the game's textures and one of its static meshes with their own files at the game
// assets' paths (K9), Review accepts both, and the export packs both at the game's paths with the creator's data.
// Runs in a project with an imported game map, such as a copy of a kept fixture (kadan_fixture.py).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameReplacementTest, "P4MapKit.Kadan.GameReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4GameReplacementTest::RunTest(const FString&)
{
    using namespace P4GameReplacementTest;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(
            *this, TEXT("native.replacement.test"), TEXT("Native Replacement Test"), Request, Names))
        return false;
    const FP4ModDescriptor& Mod = Request.Mod;
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("The game's inventory is read"),
            P4ReadExportGameInventory(
                Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error)))
    {
        AddError(Error);
        return false;
    }
    UTexture2D* GameTexture = GameAsset<UTexture2D>(
        [&](UTexture2D* Texture)
        {
            return Texture->Source.IsValid() && Texture->Source.GetNumMips() > 0 && Texture->Source.GetSizeX() != 32 &&
                Texture->Source.GetSizeX() != 16;
        });
    if (!GameTexture || !FPackageName::DoesPackageExist(Cave000))
    {
        AddWarning(TEXT("P4MapKit test not run. Run it in a project with an imported game map."));
        return true;
    }
    const FString TexturePackage = GameTexture->GetOutermost()->GetName();
    FString MeshPackage;
    // The game's assets come back from their files when the test ends. Nothing of the test is saved.
    ON_SCOPE_EXIT
    {
        TArray<UPackage*> Packages;
        for (const FString& Name : {TexturePackage, MeshPackage})
            if (UPackage* Package = Name.IsEmpty() ? nullptr : FindPackage(nullptr, *Name))
            {
                Package->SetDirtyFlag(false);
                Packages.Add(Package);
            }
        FText Why;
        if (!UPackageTools::ReloadPackages(Packages, Why, EReloadPackagesInteractionMode::AssumePositive))
            AddWarning(TEXT("The game's assets were not loaded again from their files: ") + Why.ToString());
    };
    // A mesh the open game map uses, so the map's own components show the replacement.
    UWorld* Map = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestNotNull(TEXT("The game map opens"), Map))
        return false;
    ON_SCOPE_EXIT
    {
        if (GEditor)
            UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    TArray<UStaticMesh*> InMap;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
        if (!It->IsTemplate() && It->GetWorld() == Map && It->GetStaticMesh())
            InMap.AddUnique(It->GetStaticMesh());
    UStaticMesh* GameMesh = nullptr;
    for (UStaticMesh* Candidate : InMap)
    {
        FString Origin;
        if (Candidate->GetClass() == UStaticMesh::StaticClass() && P4HasIntrinsicGameSource(Candidate, &Origin) &&
            Origin == Candidate->GetOutermost()->GetName() && !Candidate->GetName().StartsWith(TEXT("COL_")) &&
            Candidate->RenderData && Candidate->RenderData->LODResources.Num() > 0 &&
            Candidate->StaticMaterials.Num() > 0 && Candidate->StaticMaterials[0].MaterialInterface)
        {
            GameMesh = Candidate;
            break;
        }
    }
    if (!TestNotNull(TEXT("The game map uses a game mesh that can be replaced"), GameMesh))
        return false;
    // The game texture's files, written back when the test ends: the test saves its replacement over them.
    TMap<FString, TArray<uint8>> TextureFiles;
    const auto FilesOf = [&]()
    {
        TArray<FString> Out;
        for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk")})
            Out.Add(
                FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(TexturePackage, Extension)));
        return Out;
    };
    for (const FString& File : FilesOf())
    {
        TArray<uint8> Bytes;
        if (FFileHelper::LoadFileToArray(Bytes, *File, FILEREAD_Silent))
            TextureFiles.Add(File, MoveTemp(Bytes));
    }
    ON_SCOPE_EXIT
    {
        // A new file in place of each (never written through: a kept fixture shares these files). The loaded
        // package lets go of its files first.
        if (UPackage* Loaded = FindPackage(nullptr, *TexturePackage))
            ResetLoaders(Loaded);
        for (const FString& File : FilesOf())
        {
            const TArray<uint8>* Kept = TextureFiles.Find(File);
            const int64 Size = IFileManager::Get().FileSize(*File);
            TArray<uint8> Now;
            if (Kept && Size == Kept->Num() && FFileHelper::LoadFileToArray(Now, *File, FILEREAD_Silent) &&
                Now == *Kept)
                continue;
            if (Size >= 0)
                IFileManager::Get().Delete(*File, false, true, true);
            if (Kept && !FFileHelper::SaveArrayToFile(*Kept, *File))
                AddError(TEXT("The game texture's file could not be written back: ") + File);
        }
    };
    MeshPackage = GameMesh->GetOutermost()->GetName();
    const FString MeshPath = GameMesh->GetPathName();

    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/GameReplacementTest"));
    const FString Image = Folder / TEXT("replacement.tga"), Other = Folder / TEXT("replacement2.tga"),
                  Shape = Folder / TEXT("replacement.obj");
    const uint8 Green[4] = {0x40, 0xC0, 0x20, 0xFF}, Blue[4] = {0xE0, 0x30, 0x10, 0xFF};
    const FString Cube = TEXT(
        "o CreatorCube\nv -50 -50 -50\nv 50 -50 -50\nv 50 50 -50\nv -50 50 -50\nv -50 -50 50\nv 50 -50 50\nv 50 50 50\nv -50 50 50\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 3/3 2/2\nf 1/1 4/4 3/3\nf 5/1 6/2 7/3\nf 5/1 7/3 8/4\nf 1/1 2/2 6/3\nf 1/1 6/3 5/4\nf 2/1 3/2 7/3\nf 2/1 7/3 6/4\nf 3/1 4/2 8/3\nf 3/1 8/3 7/4\nf 4/1 1/2 5/3\nf 4/1 5/3 8/4\n");
    if (!TestTrue(TEXT("the creator's files are written"),
            IFileManager::Get().MakeDirectory(*Folder, true) && WriteImage(Image, 32, Green) &&
                WriteImage(Other, 16, Blue) &&
                FFileHelper::SaveStringToFile(Cube, *Shape, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;

    // Before: the game's own assets, locked and refused by export.
    FString Why;
    TestTrue(TEXT("The game's texture can be replaced with a file"), P4CanReplaceGameAsset(GameTexture, &Why));
    TestTrue(TEXT("The game's mesh can be replaced with a file"), P4CanReplaceGameAsset(GameMesh, &Why));
    TestFalse(TEXT("The game's texture is not a replacement"), P4IsGameReplacement(GameTexture));
    TestFalse(TEXT("The game's texture cannot be edited"), P4Kadan::GetAssetPolicy(GameTexture).AllowEdit);
    TestFalse(TEXT("Export refuses the game's texture"), P4ExportAssetAvailability(GameTexture, Mod).IsAvailable());
    TestTrue(TEXT("Export says how to replace it"),
        P4ExportAssetAvailability(GameTexture, Mod).Reason.Contains(TEXT("Replace with my own file")));
    const FString TextureDigest = P4GameContentDigest(GameTexture);
    TestFalse(TEXT("The game texture's content is read"), TextureDigest.IsEmpty());
    TArray64<uint8> GamePixels;
    const bool HavePixels = GameTexture->Source.GetMipData(GamePixels, 0);
    const int32 GameX = GameTexture->Source.GetSizeX(), GameY = GameTexture->Source.GetSizeY(),
                GameSlices = GameTexture->Source.GetNumSlices();
    const ETextureSourceFormat GameFormat = GameTexture->Source.GetFormat();
    const TEnumAsByte<TextureCompressionSettings> GameCompression = GameTexture->CompressionSettings;
    const bool GameSrgb = GameTexture->SRGB;
    const ECollisionTraceFlag GameTrace = GameMesh->BodySetup ? GameMesh->BodySetup->CollisionTraceFlag.GetValue()
                                                              : CTF_UseDefault;
    const FName GameProfile = GameMesh->BodySetup ? GameMesh->BodySetup->DefaultInstance.GetCollisionProfileName()
                                                  : NAME_None;
    UMaterialInterface* GameMaterial = GameMesh->StaticMaterials.Num() ? GameMesh->StaticMaterials[0].MaterialInterface
                                                                       : nullptr;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> Users;
    TMap<UWorld*, bool> Levels; // the game levels whose actors use the mesh, and whether each had unsaved changes
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
        if (It->GetStaticMesh() == GameMesh)
        {
            Users.Add(*It);
            if (!It->IsTemplate() && It->GetWorld() == Map)
                if (UWorld* Level = Cast<UWorld>(It->GetOutermost()->FindAssetInPackage()))
                    Levels.Add(Level, Level->GetOutermost()->IsDirty());
        }

    // A file of the other kind is refused and leaves the game's asset as it was.
    TestNull(TEXT("A mesh file is refused for a texture"), P4ReplaceGameAsset(GameTexture, Shape, Error));
    TestTrue(TEXT("The refusal says what to choose"), Error.Contains(TEXT("image")));
    TestTrue(TEXT("The texture is still the game's"),
        !P4IsGameReplacement(GameTexture) && P4HasIntrinsicGameSource(GameTexture));
    TestFalse(TEXT("The texture is still locked"), P4Kadan::GetAssetPolicy(GameTexture).AllowEdit);

    // The texture: the creator's image at the game texture's path, made by the editor's own factory.
    UTexture2D* Texture = Cast<UTexture2D>(P4ReplaceGameAsset(GameTexture, Image, Error));
    if (!TestNotNull(TEXT("The game's texture is replaced with the image"), Texture))
    {
        AddError(Error);
        return false;
    }
    FP4GameReplacement Replaced;
    TestTrue(TEXT("The replacement is the texture at the game's path"),
        Texture == GameTexture && Texture->GetOutermost()->GetName() == TexturePackage);
    TestTrue(TEXT("It records the game's texture and the creator's file"),
        P4IsGameReplacement(Texture, &Replaced) && Replaced.Package == TexturePackage &&
            FPaths::GetCleanFilename(Replaced.File) == TEXT("replacement.tga") && Replaced.GameDigest == TextureDigest);
    TestTrue(TEXT("It is the creator's"), P4HasCreatorSource(Texture) && P4IsCreatorAsset(Texture));
    TestFalse(TEXT("It has no game tag"), P4HasIntrinsicGameSource(Texture));
    TestFalse(TEXT("It does not say it is an original game asset"), P4QueryOriginalAsset(Texture).IsOriginal());
    TestTrue(TEXT("It can be edited"), P4Kadan::GetAssetPolicy(Texture).AllowEdit);
    TestFalse(TEXT("Its package is not locked"), P4Kadan::IsLockedPackageName(FName(*TexturePackage)));
    FP4CreatorTextureSource Source;
    TestTrue(TEXT("It holds exactly the creator's image"), P4VerifyCreatorTextureSource(Texture, Image, Source, Error));
    TestTrue(TEXT("It keeps the game texture's settings"),
        Texture->CompressionSettings == GameCompression && Texture->SRGB == GameSrgb);
    TestNotEqual(TEXT("Its content is not the game's"), P4GameContentDigest(Texture), TextureDigest);
    FString Same;
    TestFalse(TEXT("Review finds no game content in it"), P4FindGameContentMatch(Texture, Same));
    const FP4ExportAvailability TextureRoute = P4ExportAssetAvailability(Texture, Mod);
    TestTrue(
        TEXT("Export ships it as the creator's texture"), TextureRoute.Route == EP4ExportAssetRoute::CreatorTexture);
    if (!TextureRoute.IsAvailable())
        AddInfo(TEXT("Texture: ") + TextureRoute.Reason);

    // Holding the game's pixels again, the replacement holds the game's content, which Review refuses.
    if (HavePixels)
    {
        Texture->Source.Init(GameX, GameY, GameSlices, 1, GameFormat, GamePixels.GetData());
        TestTrue(TEXT("A replacement holding the game's pixels again matches the game's texture"),
            P4FindGameContentMatch(Texture, Same) && Same == TexturePackage);
        const FP4ExportAvailability Back = P4ExportAssetAvailability(Texture, Mod);
        TestFalse(TEXT("Review refuses it"), Back.IsAvailable());
        TestTrue(TEXT("It names the game's texture"), Back.Reason.Contains(TexturePackage));
    }
    else
        AddInfo(TEXT("The game texture's pixels were not read, so a replacement holding them again was not checked."));
    // Replaced again from another file, it keeps what the first replacement kept of the game's texture.
    Texture = Cast<UTexture2D>(P4ReplaceGameAsset(Texture, Other, Error));
    if (!TestNotNull(TEXT("A replacement can be replaced with another file"), Texture))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("It records the new file and the game's texture"),
        P4IsGameReplacement(Texture, &Replaced) &&
            FPaths::GetCleanFilename(Replaced.File) == TEXT("replacement2.tga") &&
            Replaced.GameDigest == TextureDigest);
    TestTrue(TEXT("It holds exactly the new image"), P4VerifyCreatorTextureSource(Texture, Other, Source, Error));
    TestFalse(TEXT("It no longer matches the game"), P4FindGameContentMatch(Texture, Same));

    // The mesh: the creator's cube at the game mesh's path, with collision from the file.
    UStaticMesh* Mesh = Cast<UStaticMesh>(P4ReplaceGameAsset(GameMesh, Shape, Error));
    if (!TestNotNull(TEXT("The game's mesh is replaced with the file"), Mesh))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("The replacement mesh is at the game's path"), Mesh->GetPathName(), MeshPath);
    TestTrue(TEXT("The game's path finds the replacement"), FindObject<UStaticMesh>(nullptr, *MeshPath) == Mesh);
    TestTrue(TEXT("It records the game's mesh and the creator's file"),
        P4IsGameReplacement(Mesh, &Replaced) && Replaced.Package == MeshPackage &&
            FPaths::GetCleanFilename(Replaced.File) == TEXT("replacement.obj"));
    TestTrue(TEXT("The mesh is the creator's"),
        P4HasCreatorSource(Mesh) && P4IsCreatorAsset(Mesh) && !P4HasIntrinsicGameSource(Mesh));
    TestTrue(TEXT("The mesh can be edited"),
        P4Kadan::GetAssetPolicy(Mesh).AllowEdit && !P4Kadan::IsLockedPackageName(FName(*MeshPackage)));
    const FMeshDescription* Description = Mesh->GetMeshDescription(0);
    TestTrue(TEXT("Its shape is the creator's cube"),
        Description && Description->Vertices().Num() == 8 && Description->Triangles().Num() == 12);
    if (TestNotNull(TEXT("It has collision"), Mesh->BodySetup))
    {
        TestTrue(TEXT("It collides the way the game's mesh did"),
            Mesh->BodySetup->CollisionTraceFlag == GameTrace &&
                (GameProfile.IsNone() || Mesh->BodySetup->DefaultInstance.GetCollisionProfileName() == GameProfile));
        if (Mesh->BodySetup->AggGeom.GetElementCount() > 0)
        {
            const FBox Shapes = Mesh->BodySetup->AggGeom.CalcAABB(FTransform::Identity);
            TestTrue(TEXT("Its collision shapes are made from the creator's cube"),
                Shapes.Min.GetMin() >= -51.f && Shapes.Max.GetMax() <= 51.f);
        }
        else
            TestTrue(TEXT("Without shapes it collides with the creator's triangles"),
                Mesh->BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple);
    }
    if (GameMaterial)
        TestTrue(TEXT("It keeps the game mesh's material"),
            Mesh->StaticMaterials.Num() && Mesh->StaticMaterials[0].MaterialInterface == GameMaterial);
    TestTrue(TEXT("Components of the game map used the game's mesh"), Users.Num() > 0);
    for (const TWeakObjectPtr<UStaticMeshComponent>& User : Users)
        if (User.IsValid())
            TestTrue(TEXT("A component that used the game's mesh uses the replacement"), User->GetStaticMesh() == Mesh);
    for (const TPair<UWorld*, bool>& Level : Levels)
    {
        TestEqual(TEXT("The game level that uses the mesh has no new unsaved change"),
            Level.Key->GetOutermost()->IsDirty(), Level.Value);
        TestTrue(
            TEXT("The game level that uses the mesh is unchanged"), P4AnalyzeOriginalLevelChanges(Level.Key).Unchanged);
    }
    AddInfo(FString::Printf(
        TEXT("%s: %d components in %d levels used the game's mesh."), *MeshPackage, Users.Num(), Levels.Num()));
    const FP4ExportAvailability MeshRoute = P4ExportAssetAvailability(Mesh, Mod);
    TestTrue(TEXT("Export ships it as the creator's mesh"), MeshRoute.Route == EP4ExportAssetRoute::CreatorStaticMesh);
    if (!MeshRoute.IsAvailable())
        AddInfo(TEXT("Mesh: ") + MeshRoute.Reason);

    // Review and export: both are cooked at the game's paths and packed in the mod's container.
    for (const FString& Path : {TexturePackage, MeshPackage})
    {
        FP4ExportRoot Root;
        Root.Path = Path;
        Request.Roots.Add(Root);
    }
    FP4ExportSelectionPreview Preview;
    if (TestTrue(TEXT("The selection is reviewed"),
            P4PreviewExportSelection(Mod, Request.Roots, Inventory, {}, Preview, Error)))
    {
        TestEqual(TEXT("Review refuses nothing"), Preview.Refused.Num(), 0);
        for (const FString& Line : Preview.Refused)
            AddInfo(Line);
        TestTrue(TEXT("Review cooks both replacements"),
            Preview.CookCandidates.Contains(TexturePackage) && Preview.CookCandidates.Contains(MeshPackage));
    }
    else
        AddError(Error);
    FP4ExportModResult Result;
    if (!TestTrue(TEXT("Both replacements are cooked, packed and published"), P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        for (const FString& Line : Result.Refused)
            AddError(Line);
        return false;
    }
    TArray<FString> Expected = {TexturePackage, MeshPackage}, Shipped = Result.Packages,
                    Replacements = Result.Replacements;
    Expected.Sort();
    Shipped.Sort();
    Replacements.Sort();
    TestTrue(TEXT("The container holds the two replacements"), Shipped == Expected);
    TestTrue(TEXT("The export reports them as replacements"),
        Replacements == Expected && Result.ReplacementFiles.Num() == 2);
    TestEqual(TEXT("A manifest and one three-file container"), Result.Files.Num(), 4);
    const FString ContainerFolder = Result.Directory / TEXT("romfs/tsuchi/mods") / Mod.Id;
    FP4ExportContainerCensus Census;
    if (TestTrue(TEXT("The container is read"),
            P4InspectExportContainer(Request.Tools, ContainerFolder / (Names.Container + TEXT("_P.utoc")),
                Request.WorkDirectory, Inventory, Census, Error)))
        for (const FString& Package : Expected)
        {
            TestTrue(TEXT("The container holds the package at the game's path"),
                Census.PackageIds.Contains(P4ExportPackageId(Package)));
            if (Inventory.Contains(FName(*Package)))
                TestTrue(TEXT("It takes the place of the game's package"),
                    Census.GameClaims.Contains(P4ExportPackageId(Package)));
        }
    else
        AddError(Error);
    const FString Work = FPaths::ConvertRelativePathToFull(Request.WorkDirectory / (Mod.Id + TEXT(".export-work")));
    for (const FString& Package : Expected)
    {
        std::vector<std::string> Said;
        TestTrue(TEXT("The cooked package holds nothing of Kadan's records"),
            P4ExportDriverTest::CookedNames(
                Work / TEXT("cooked/Carrot4/Content") / Package.Mid(6) + TEXT(".uasset"), Said) &&
                !P4ExportDriverTest::Has(Said, "P4KadanReplacesGame") &&
                !P4ExportDriverTest::Has(Said, "P4KadanCreatorSource") &&
                !P4ExportDriverTest::Has(Said, "/Script/Kadan") &&
                std::none_of(Said.begin(), Said.end(),
                    [](const std::string& Name) { return Name.find("P4CreatorSourceData") == 0; }));
        // What the container holds at the game's path, read back from it, is not the game's package.
        TArray<uint8> Packed;
        const FString Readback = Work / TEXT("readback/Carrot4/Content") / Package.Mid(6) + TEXT(".uexp");
        if (!TestTrue(TEXT("The packed package is read back from the container"),
                FFileHelper::LoadFileToArray(Packed, *Readback, FILEREAD_Silent) && Packed.Num() > 0))
            continue;
        const TArray<uint8> Game = GameFile(Request.ExtractedGameDirectory, Package, TEXT(".uexp"));
        if (Game.Num())
            TestTrue(TEXT("The packed package is not the game's"), Packed != Game);
        else
            AddInfo(TEXT("The game's own file of ") + Package +
                TEXT(" is not extracted here, so the packed bytes were not compared with it."));
    }
    for (const FString& Line : Result.Told)
        AddInfo(Line);

    // Saved and loaded again, the texture is still the creator's replacement of the game's texture.
    UPackage* TexturePackageObject = Texture->GetOutermost();
    TestTrue(TEXT("The replacement saves at the game's path"),
        UEditorLoadingAndSavingUtils::SavePackages({TexturePackageObject}, false) && !TexturePackageObject->IsDirty());
    FText ReloadWhy;
    TestTrue(TEXT("The saved replacement loads again"),
        UPackageTools::ReloadPackages(
            {TexturePackageObject}, ReloadWhy, EReloadPackagesInteractionMode::AssumePositive));
    UTexture2D* Saved = FindObject<UTexture2D>(
        nullptr, *(TexturePackage + TEXT(".") + FPackageName::GetLongPackageAssetName(TexturePackage)));
    TestTrue(TEXT("Loaded again, it records the game's texture and the creator's file"),
        Saved && P4IsGameReplacement(Saved, &Replaced) && Replaced.GameDigest == TextureDigest &&
            FPaths::GetCleanFilename(Replaced.File) == TEXT("replacement2.tga"));
    TestTrue(TEXT("Loaded again, it is the creator's and can be edited"),
        Saved && P4HasCreatorSource(Saved) && P4IsCreatorAsset(Saved) && !P4HasIntrinsicGameSource(Saved) &&
            P4Kadan::GetAssetPolicy(Saved).AllowEdit && !P4Kadan::IsLockedPackageName(FName(*TexturePackage)));
    if (Saved)
        TestTrue(TEXT("Loaded again, export ships it"),
            P4ExportAssetAvailability(Saved, Mod).Route == EP4ExportAssetRoute::CreatorTexture);
    return !HasAnyErrors();
}
#endif

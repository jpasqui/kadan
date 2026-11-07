#include "P4CreatorSource.h"
#include "P4CreatorMaterialFactories.h"
#include "P4MaterialInstance.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "P4KadanReadOnly.h"
#include "P4KadanAssetPolicy.h"
#include "P4TextureContent.h"
#include "P4AuthoringRows.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "AssetImportTask.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "IAssetTools.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSourceTest, "P4MapKit.Kadan.CreatorSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSourceTest::RunTest(const FString&)
{
    if (!GEditor)
    {
        AddError(TEXT("CreatorSource needs the editor and its actual import subsystem."));
        return false;
    }
    // One reusable output scope, never game assets or an arbitrary previous Content directory.
    const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KadanCreatorSourceTest"));
    const FString Root = TEXT("/Game/__KadanCreatorSourceTest");
    const TArray<FString> Names = {TEXT("Texture"), TEXT("Mesh"), TEXT("Instance"), TEXT("GamePublication"),
        TEXT("GameCopy"), TEXT("Proxy"), TEXT("ProxyCopy"), TEXT("TextureProxy"), TEXT("TextureCopy"), TEXT("MeshCopy"),
        TEXT("InstanceCopy"), TEXT("TextureRenamed"), TEXT("MeshRenamed"), TEXT("InstanceRenamed"), TEXT("Material"),
        TEXT("MaterialCopy"), TEXT("MaterialRenamed"), TEXT("FactoryInstance"), TEXT("FactoryInstanceCopy"),
        TEXT("FactoryInstanceRenamed"), TEXT("StockMaterial"), TEXT("StockInstance"), TEXT("MaterialRenamed_Inst"),
        TEXT("FactoryInstanceRenamed_Inst"), TEXT("GamePublication_Mat"), TEXT("TextureRenamed_Mat"),
        TEXT("UnqualifiedMesh"), TEXT("GameMeshCopy"), TEXT("GameMaterialCopy"), TEXT("GameInstanceCopy"),
        TEXT("ReplacementCopy")};
    TArray<FString> Files;
    for (const FString& Name : Names)
    {
        const FString Package = Root + TEXT("/") + Name;
        const FString File = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
        if (FPackageName::DoesPackageExist(Package) || FindPackage(nullptr, *Package))
        {
            AddError(TEXT("CreatorSource fixture destination already exists: ") + Package);
            return false;
        }
        Files.Add(File);
    }
    IFileManager::Get().MakeDirectory(*Folder, true);
    ON_SCOPE_EXIT
    {
        // Only the exact fixture destinations checked absent above can be removed by this run.
        TArray<UPackage*> Packages;
        for (const FString& Name : Names)
            if (UPackage* Package = FindPackage(nullptr, *(Root + TEXT("/") + Name)))
            {
                Package->SetDirtyFlag(false);
                Packages.Add(Package);
            }
        UPackageTools::UnloadPackages(Packages);
        for (const FString& File : Files)
        {
            FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, false);
            if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true))
                AddWarning(TEXT("Could not remove this run's creator-source fixture: ") + File);
        }
    };
    // A real creator input, decoded by the stock texture factory. No NewObject pixels or
    // inherited ImportData can establish a positive result.
    const FString ImageFile = FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder, TEXT("creator.tga")));
    const TArray<uint8> Tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40, 0, 0, 255, 255, 0, 255, 0, 255,
        255, 0, 0, 255, 0, 255, 255, 255};
    if (!TestTrue(TEXT("Write actual creator image input"), FFileHelper::SaveArrayToFile(Tga, *ImageFile)))
        return false;
    // OBJ is a supported input of Unreal's FBX factory; this is source geometry read from
    // a file, not a programmatically built mesh relabelled as creator source.
    const FString MeshFile = FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder, TEXT("creator.obj")));
    const FString Obj = TEXT(
        "o CreatorTriangle\nv 0 0 0\nv 100 0 0\nv 0 100 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n");
    if (!TestTrue(TEXT("Write actual FBX-factory geometry input"),
            FFileHelper::SaveStringToFile(Obj, *MeshFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    const auto ImportFile = [&](const FString& Name, bool Mesh) -> UObject*
    {
        const FString Package = Root + TEXT("/") + Name;
        TStrongObjectPtr<UFactory> Factory(Mesh ? static_cast<UFactory*>(NewObject<UFbxFactory>())
                                                : static_cast<UFactory*>(NewObject<UTextureFactory>()));
        Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
        Factory->AssetImportTask->bAutomated = true;
        Factory->AssetImportTask->bReplaceExisting = true;
        Factory->AssetImportTask->bReplaceExistingSettings = true;
        if (UFbxFactory* Fbx = Cast<UFbxFactory>(Factory.Get()))
        {
            Fbx->SetDetectImportTypeOnImport(false);
            Fbx->ImportUI->MeshTypeToImport = FBXIT_StaticMesh;
            Fbx->ImportUI->bAutomatedImportShouldDetectType = false;
            Fbx->ImportUI->bImportMaterials = false;
            Fbx->ImportUI->bImportTextures = false;
        }
        bool Canceled = false;
        UObject* Asset = Factory->ImportObject(Mesh ? UStaticMesh::StaticClass() : UTexture2D::StaticClass(),
            CreatePackage(*Package), FName(*Name), RF_Public | RF_Standalone | RF_Transactional,
            Mesh ? MeshFile : ImageFile, nullptr, Canceled);
        TestFalse(Name + TEXT(" source factory did not cancel"), Canceled);
        if (Asset)
            FAssetRegistryModule::AssetCreated(Asset);
        Factory->CleanUp();
        return Asset;
    };
    const auto Cold = [&](UObject*& Asset, bool Qualified, const FString& Label) -> bool
    {
        if (!TestNotNull(Label + TEXT(" asset exists"), Asset))
            return false;
        const FString Path = Asset->GetPathName(), PackageName = Asset->GetOutermost()->GetName();
        const FString File = FPackageName::LongPackageNameToFilename(
            PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        UPackage* Package = Asset->GetOutermost();
        UClass* Class = Asset->GetClass();
        FString ExpectedSourcePackage;
        const bool ExpectedGameSource = P4HasIntrinsicGameSource(Asset, &ExpectedSourcePackage);
        const bool ExpectedProxy = P4HasIntrinsicGameProxy(Asset);
        {
            bool SawSavingTags = false;
            const UObject* Target = Asset;
            const FDelegateHandle Tags = UObject::FAssetRegistryTag::OnGetExtraObjectTags.AddLambda(
                [&](const UObject* Tagged, TArray<UObject::FAssetRegistryTag>&)
                {
                    if (Tagged == Target && GIsSavingPackage)
                    {
                        SawSavingTags = true;
                        TestEqual(Label + TEXT(" actual serialization-time source query"), P4HasCreatorSource(Tagged),
                            Qualified);
                        FString SavingSourcePackage;
                        TestEqual(Label + TEXT(" actual serialization-time negative source query"),
                            P4HasIntrinsicGameSource(Tagged, &SavingSourcePackage), ExpectedGameSource);
                        TestEqual(Label + TEXT(" actual serialization-time original source identity"),
                            SavingSourcePackage, ExpectedSourcePackage);
                        TestEqual(Label + TEXT(" actual serialization-time proxy query"),
                            P4HasIntrinsicGameProxy(Tagged), ExpectedProxy);
                    }
                });
            ON_SCOPE_EXIT
            {
                UObject::FAssetRegistryTag::OnGetExtraObjectTags.Remove(Tags);
            };
            P4ImportPublication::FSaveScope Publish(Package);
            if (!TestTrue(Label + TEXT(" ordinary package save"),
                    UPackage::SavePackage(
                        Package, Asset, RF_Public | RF_Standalone, *File, GError, nullptr, false, false, SAVE_NoError)))
                return false;
            TestTrue(Label + TEXT(" actual save gathered tags while serializing"), SawSavingTags);
        }
        Package->SetDirtyFlag(false);
        Asset = nullptr;
        FText Error;
        const bool Unloaded = UPackageTools::UnloadPackages({Package}, Error);
        if (!TestTrue(Label + TEXT(" unloaded for a real cold read: ") + Error.ToString(), Unloaded))
            return false;
        if (!TestNull(Label + TEXT(" previous asset was released"), FindObject<UObject>(nullptr, *Path)))
            return false;
        Asset = StaticLoadObject(Class, nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!TestNotNull(Label + TEXT(" reopened from disk"), Asset))
            return false;
        return TestEqual(Label + TEXT(" qualification survives cold read"), P4HasCreatorSource(Asset), Qualified);
    };
    const auto Copy = [&](UObject* Source, const FString& Name) -> UObject*
    {
        UObject* Out = Source ? Tools.DuplicateAsset(Name, Root, Source) : nullptr;
        TestNotNull(Name + TEXT(" ordinary Unreal duplicate succeeds"), Out);
        return Out;
    };
    const auto Negative = [&](UObject* Asset, const FString& Origin, bool Proxy, const FString& Label)
    {
        if (!TestNotNull(Label + TEXT(" asset exists"), Asset))
            return;
        FString SourcePackage;
        TestTrue(Label + TEXT(" carries intrinsic game source"), P4HasIntrinsicGameSource(Asset, &SourcePackage));
        TestEqual(Label + TEXT(" keeps the exact original package"), SourcePackage, Origin);
        TestEqual(Label + TEXT(" keeps the current proxy role"), P4HasIntrinsicGameProxy(Asset), Proxy);
        TestFalse(Label + TEXT(" does not become a creator source"), P4HasCreatorSource(Asset));
        const P4Kadan::FAssetPolicy Policy = P4Kadan::GetAssetPolicy(Asset);
        TestFalse(Label + TEXT(" ordinary policy keeps game payload locked"), Policy.AllowEdit);
        TestEqual(Label + TEXT(" policy preserves proxy role"), Policy.StandIn, Proxy);
        IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset);
        const UP4CreatorSourceData* Data = Owner
            ? Cast<UP4CreatorSourceData>(Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()))
            : nullptr;
        TestTrue(Label + TEXT(" intrinsic role is an editor-only instanced subobject"),
            Data && Data->GetOuter() == Asset && Data->IsEditorOnly());
    };
    const auto PublishOriginal = [&](UObject* Asset, const TCHAR* Kind, const FString& Label)
    {
        if (!Asset)
            return;
        FP4OriginalRecord Original;
        Original.Package = Asset->GetOutermost()->GetName();
        Original.Class = Asset->GetClass()->GetPathName();
        Original.Kind = Kind;
        FString Reason;
        TestTrue(Label + TEXT(" is published at the synthetic original-source boundary"),
            P4MarkOriginalAsset(Asset, Original, Reason));
    };
    UObject* Texture = ImportFile(TEXT("Texture"), false);
    UObject* Mesh = ImportFile(TEXT("Mesh"), true);
    if (TestNotNull(TEXT("Fresh actual FBX-factory mesh is imported"), Mesh))
    {
        TestTrue(TEXT("Fresh actual FBX source is qualified"), P4HasCreatorSource(Mesh));
        Mesh = ImportFile(TEXT("Mesh"), true);
        P4Kadan::FlushReadOnlyGuards();
        TestNotNull(TEXT("Actual reimport of a known creator mesh succeeds"), Mesh);
        TestTrue(
            TEXT("Actual creator mesh reimport retains positive source through guard flush"), P4HasCreatorSource(Mesh));
    }
    UObject* UnqualifiedMesh = nullptr;
    {
        // Synthetic publication boundary gives a real occupied mesh without creator evidence.
        // This tests source classification, not recovered game mesh appearance or collision.
        P4ImportPublication::FImportScope Import;
        UnqualifiedMesh = ImportFile(TEXT("UnqualifiedMesh"), true);
        PublishOriginal(UnqualifiedMesh, TEXT("static-mesh"), TEXT("UnqualifiedMesh"));
    }
    if (TestNotNull(TEXT("Existing unqualified mesh has actual decoded geometry"), UnqualifiedMesh))
    {
        const FString MeshOrigin = Root + TEXT("/UnqualifiedMesh");
        Negative(UnqualifiedMesh, MeshOrigin, false, TEXT("Published mesh"));
        UObject* GameMeshCopy = Copy(UnqualifiedMesh, TEXT("GameMeshCopy"));
        if (GameMeshCopy)
        {
            Negative(GameMeshCopy, MeshOrigin, false, TEXT("GameMeshCopy"));
            Cold(GameMeshCopy, false, TEXT("GameMeshCopy"));
            Negative(GameMeshCopy, MeshOrigin, false, TEXT("GameMeshCopy cold"));
        }
        TestFalse(TEXT("Publication-scoped mesh begins unqualified"), P4HasCreatorSource(UnqualifiedMesh));
        UnqualifiedMesh = ImportFile(TEXT("UnqualifiedMesh"), true);
        P4Kadan::FlushReadOnlyGuards();
        TestNotNull(TEXT("Actual file reimport of occupied unqualified mesh returns an asset"), UnqualifiedMesh);
        TestFalse(TEXT("File reimport cannot qualify retained unknown collision/render payload"),
            P4HasCreatorSource(UnqualifiedMesh));
        Negative(UnqualifiedMesh, MeshOrigin, false, TEXT("Occupied mesh after real reimport"));
        Cold(UnqualifiedMesh, false, TEXT("UnqualifiedMesh reimport"));
        Negative(UnqualifiedMesh, MeshOrigin, false, TEXT("Occupied mesh reimport cold"));
    }
    FString Why;
    TArray<FString> Missing;
    UMaterialInterface* DefaultParent = Cast<UMaterialInterface>(StaticLoadObject(
        UMaterialInterface::StaticClass(), nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));
    if (!TestNotNull(TEXT("Real default material parent is available"), DefaultParent))
        return false;
    UObject* Instance = P4BuildMaterialInstance(
        TEXT("{\"parent\":\"/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial\"}"), Root + TEXT("/Instance"),
        Missing, Why);
    TestTrue(TEXT("Explicit creator instance construction succeeded: ") + Why, Instance != nullptr);
    const TArray<UFactory*> MenuFactories = Tools.GetNewAssetFactories();
    TestTrue(TEXT("Ordinary Browser factory selection includes owned Material factory"),
        MenuFactories.Contains(GetMutableDefault<UP4CreatorMaterialFactory>()));
    TestTrue(TEXT("Ordinary Browser factory selection includes owned MIC factory"),
        MenuFactories.Contains(GetMutableDefault<UP4CreatorMaterialInstanceFactory>()));
    TestFalse(TEXT("Ordinary Browser factory selection excludes exact stock Material factory"),
        MenuFactories.Contains(GetMutableDefault<UMaterialFactoryNew>()));
    TestFalse(TEXT("Ordinary Browser factory selection excludes exact stock MIC factory"),
        MenuFactories.Contains(GetMutableDefault<UMaterialInstanceConstantFactoryNew>()));
    TStrongObjectPtr<UP4CreatorMaterialFactory> MaterialFactory(NewObject<UP4CreatorMaterialFactory>());
    UObject* Material = Tools.CreateAsset(TEXT("Material"), Root, UMaterial::StaticClass(), MaterialFactory.Get());
    TStrongObjectPtr<UP4CreatorMaterialInstanceFactory> InstanceFactory(NewObject<UP4CreatorMaterialInstanceFactory>());
    InstanceFactory->InitialParent = Cast<UMaterialInterface>(Material);
    UObject* FactoryInstance = Tools.CreateAsset(
        TEXT("FactoryInstance"), Root, UMaterialInstanceConstant::StaticClass(), InstanceFactory.Get());
    TestNotNull(TEXT("Ordinary AssetTools calls actual owned Material constructor"), Material);
    TestNotNull(TEXT("Ordinary AssetTools calls actual owned MIC constructor"), FactoryInstance);
    // Passing an unobservable stock factory directly is still not source evidence.
    TStrongObjectPtr<UMaterialFactoryNew> StockMaterialFactory(NewObject<UMaterialFactoryNew>());
    UObject* StockMaterial = Tools.CreateAsset(
        TEXT("StockMaterial"), Root, UMaterial::StaticClass(), StockMaterialFactory.Get());
    TStrongObjectPtr<UMaterialInstanceConstantFactoryNew> StockInstanceFactory(
        NewObject<UMaterialInstanceConstantFactoryNew>());
    StockInstanceFactory->InitialParent = Cast<UMaterialInterface>(StockMaterial);
    UObject* StockInstance = Tools.CreateAsset(
        TEXT("StockInstance"), Root, UMaterialInstanceConstant::StaticClass(), StockInstanceFactory.Get());
    TestNotNull(TEXT("Direct stock Material factory fixture actually creates"), StockMaterial);
    TestNotNull(TEXT("Direct stock MIC factory fixture actually creates"), StockInstance);
    TestFalse(TEXT("Direct stock Material factory remains unqualified"), P4HasCreatorSource(StockMaterial));
    TestFalse(TEXT("Direct stock MIC factory remains unqualified"), P4HasCreatorSource(StockInstance));
    // These actual stock-created objects are source-boundary fixtures, not creator evidence.
    // Their synthetic publication exercises the same intrinsic role as verified loaded game assets.
    if (StockInstance)
        CastChecked<UMaterialInstanceConstant>(StockInstance)->SetParentEditorOnly(DefaultParent);
    StockInstanceFactory->InitialParent = nullptr;
    {
        P4ImportPublication::FImportScope Import;
        PublishOriginal(StockMaterial, TEXT("material"), TEXT("StockMaterial"));
        PublishOriginal(StockInstance, TEXT("material-instance"), TEXT("StockInstance"));
    }
    for (const TPair<UObject*, FString>& Fixture : {TPair<UObject*, FString>(StockMaterial, TEXT("GameMaterialCopy")),
             TPair<UObject*, FString>(StockInstance, TEXT("GameInstanceCopy"))})
    {
        if (!Fixture.Key)
            continue;
        const FString Origin = Fixture.Key->GetOutermost()->GetName();
        Negative(Fixture.Key, Origin, false, Fixture.Key->GetName());
        UObject* GameDuplicate = Copy(Fixture.Key, Fixture.Value);
        if (GameDuplicate)
        {
            Negative(GameDuplicate, Origin, false, Fixture.Value);
            Cold(GameDuplicate, false, Fixture.Value);
            Negative(GameDuplicate, Origin, false, Fixture.Value + TEXT(" cold"));
        }
    }
    if (Material)
        TestNull(TEXT("Direct fresh Material factory refuses existing target before overwriting"),
            MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), Material->GetOutermost(), Material->GetFName(),
                RF_Public | RF_Standalone, nullptr, GWarn));
    if (FactoryInstance)
        TestNull(TEXT("Direct fresh MIC factory refuses existing target before overwriting"),
            InstanceFactory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), FactoryInstance->GetOutermost(),
                FactoryInstance->GetFName(), RF_Public | RF_Standalone, nullptr, GWarn));
    // Break a legitimate parent pointer before testing independent cold package unloads.
    if (FactoryInstance)
        CastChecked<UMaterialInstanceConstant>(FactoryInstance)
            ->SetParentEditorOnly(Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr,
                TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"))));
    InstanceFactory->InitialParent = nullptr;
    TArray<UObject*> Creator = {Texture, Mesh, Instance, Material, FactoryInstance};
    const TArray<FString> CreatorNames = {
        TEXT("Texture"), TEXT("Mesh"), TEXT("Instance"), TEXT("Material"), TEXT("FactoryInstance")};
    for (int32 Index = 0; Index < Creator.Num(); ++Index)
    {
        UObject*& Asset = Creator[Index];
        const FString& Name = CreatorNames[Index];
        if (!Asset)
            continue;
        TestTrue(Name + TEXT(" actual creator source is positively qualified"), P4HasCreatorSource(Asset));
        Cold(Asset, true, Name);
        UObject* Duplicate = Copy(Asset, Name + TEXT("Copy"));
        if (Duplicate)
        {
            TestTrue(Name + TEXT(" qualified creator duplicate remains qualified"), P4HasCreatorSource(Duplicate));
            Cold(Duplicate, true, Name + TEXT("Copy"));
        }
        const FString Renamed = Name + TEXT("Renamed");
        TArray<FAssetRenameData> Rename;
        Rename.Emplace(Asset, Root, Renamed);
        TestTrue(Name + TEXT(" ordinary creator rename succeeds"), Tools.RenameAssets(Rename));
        TestTrue(Name + TEXT(" creator rename retains qualification"), P4HasCreatorSource(Asset));
        Cold(Asset, true, Renamed);
    }
    if (Creator[3] && Creator[4])
    {
        P4CreateCreatorMaterialInstances({Creator[3], Creator[4]});
        for (const FString& Name :
            {FString(TEXT("MaterialRenamed_Inst")), FString(TEXT("FactoryInstanceRenamed_Inst"))})
        {
            UObject* Made = FindObject<UObject>(nullptr, *(Root + TEXT("/") + Name + TEXT(".") + Name));
            if (TestNotNull(TEXT("Familiar multi-selection Create Material Instance action produces ") + Name, Made))
            {
                TestTrue(Name + TEXT(" native action output has actual constructor source"), P4HasCreatorSource(Made));
                CastChecked<UMaterialInstanceConstant>(Made)->SetParentEditorOnly(DefaultParent);
                Cold(Made, true, Name);
            }
        }
    }
    UObject* Game = nullptr;
    {
        // Synthetic original-publication boundary fixture: even a supported file factory
        // invoked inside game reconstruction must not create creator-origin evidence.
        P4ImportPublication::FImportScope Import;
        Game = ImportFile(TEXT("GamePublication"), false);
        if (Game)
        {
            UTexture2D* PublishedTexture = CastChecked<UTexture2D>(Game);
            TArray64<uint8> ExpectedPixels, RestoredPixels;
            TestTrue(TEXT("Read actual file pixels for source-role comparison"),
                PublishedTexture->Source.GetMipData(ExpectedPixels, 0));
            const FString PixelState = P4TextureState(PublishedTexture);
            if (uint8* Pixels = PublishedTexture->Source.LockMip(0))
            {
                const uint8 Previous = Pixels[0];
                Pixels[0] ^= 1;
                PublishedTexture->Source.UnlockMip(0);
                TestFalse(TEXT("Actual pixel changes still change texture game state"),
                    P4TextureState(PublishedTexture) == PixelState);
                Pixels = PublishedTexture->Source.LockMip(0);
                if (TestNotNull(TEXT("Actual source mip can be restored"), Pixels))
                {
                    Pixels[0] = Previous;
                    PublishedTexture->Source.UnlockMip(0);
                }
            }
            else
                AddError(TEXT("Could not lock actual file mip for texture comparison regression"));
            const FString BeforeRole = P4TextureState(PublishedTexture);
            TestFalse(TEXT("Actual texture game state was captured before policy publication"), BeforeRole.IsEmpty());
            const FGuid BeforeRoleId = PublishedTexture->Source.GetId();
            FP4OriginalRecord Original;
            Original.Package = Game->GetOutermost()->GetName();
            Original.Class = Game->GetClass()->GetPathName();
            Original.Kind = TEXT("texture");
            TestTrue(TEXT("Publish the synthetic original boundary fixture"), P4MarkOriginalAsset(Game, Original, Why));
            TestEqual(TEXT("Editor-only intrinsic origin leaves exact texture game state unchanged"),
                P4TextureState(PublishedTexture), BeforeRole);
            TestTrue(TEXT("Source-role publication leaves actual source id unchanged"),
                PublishedTexture->Source.GetId() == BeforeRoleId);
            TestTrue(TEXT("Source-role publication leaves exact pixel bytes unchanged"),
                PublishedTexture->Source.GetMipData(RestoredPixels, 0) && RestoredPixels == ExpectedPixels);
            const bool PreviousSRGB = PublishedTexture->SRGB;
            PublishedTexture->SRGB = !PreviousSRGB;
            TestFalse(TEXT("A normal texture setting change still changes game state"),
                P4TextureState(PublishedTexture) == BeforeRole);
            PublishedTexture->SRGB = PreviousSRGB;
            IInterface_AssetUserData* Owner = CastChecked<IInterface_AssetUserData>(PublishedTexture);
            UP4CreatorSourceData* RoleBeforeRead = Cast<UP4CreatorSourceData>(
                Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()));
            TestNotNull(TEXT("Published texture has its intrinsic source-role object"), RoleBeforeRead);
            // Another real editor-only class must still participate: the exclusion is exact,
            // and never drops every editor-only or every asset-user-data object.
            UP4MaterialExportData* Unrelated = NewObject<UP4MaterialExportData>(PublishedTexture);
            Owner->AddAssetUserData(Unrelated);
            TestFalse(TEXT("Unrelated editor-only asset user data remains part of texture game state"),
                P4TextureState(PublishedTexture) == BeforeRole);
            TestTrue(TEXT("Texture state reader retains unrelated data while filtering only intrinsic origin"),
                Owner->GetAssetUserDataOfClass(UP4MaterialExportData::StaticClass()) == Unrelated);
            TestTrue(TEXT("Texture state reader never mutates the intrinsic array entry"),
                Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()) == RoleBeforeRead);
            Owner->RemoveUserDataOfClass(UP4MaterialExportData::StaticClass());
            TestEqual(TEXT("Removing only unrelated fixture data restores exact game state"),
                P4TextureState(PublishedTexture), BeforeRole);
            TestTrue(TEXT("Texture game-state reader never mutates the source-role array"),
                Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()) == RoleBeforeRead);
        }
    }
    if (Game)
    {
        Negative(Game, Root + TEXT("/GamePublication"), false, TEXT("Published texture"));
        TestFalse(TEXT("Game publication does not qualify creator source"), P4HasCreatorSource(Game));
        if (Creator[0] && Cold(Game, false, TEXT("GamePublication original")))
        {
            // A verified legacy original receives its intrinsic role in memory without an
            // unsolicited disk rewrite. Native copies will serialize their own role afterward.
            if (IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Game))
            {
                Owner->RemoveUserDataOfClass(UP4CreatorSourceData::StaticClass());
                Game->GetOutermost()->SetDirtyFlag(false);
                TestFalse(TEXT("Legacy loaded fixture has no intrinsic role before verified update"),
                    P4HasIntrinsicGameSource(Game));
                P4Kadan::UpdateReadOnlyAsset(Game);
                TestFalse(TEXT("Verified loaded original upgrade does not dirty the package"),
                    Game->GetOutermost()->IsDirty());
                Negative(Game, Root + TEXT("/GamePublication"), false, TEXT("Verified loaded legacy original"));
            }
            TArray64<uint8> BeforePixels, AfterPixels;
            UTexture2D* Source = CastChecked<UTexture2D>(Game);
            TestTrue(TEXT("Read actual source pixels before Create Material action"),
                Source->Source.GetMipData(BeforePixels, 0));
            P4CreateCreatorMaterials({Game, Creator[0]});
            TArray<UPackage*> ActionMaterialPackages;
            for (const FString& Name : {FString(TEXT("GamePublication_Mat")), FString(TEXT("TextureRenamed_Mat"))})
            {
                UObject* Made = FindObject<UObject>(nullptr, *(Root + TEXT("/") + Name + TEXT(".") + Name));
                if (TestNotNull(TEXT("Familiar texture Create Material action produces ") + Name, Made))
                {
                    UMaterial* NewMaterial = CastChecked<UMaterial>(Made);
                    TestTrue(Name + TEXT(" actual fresh constructor is qualified"), P4HasCreatorSource(Made));
                    TestFalse(Name + TEXT(" referencing a game texture does not inherit its payload origin"),
                        P4HasIntrinsicGameSource(Made));
                    UMaterialExpressionTextureSample* Sample = NewMaterial->Expressions.Num() == 1
                        ? Cast<UMaterialExpressionTextureSample>(NewMaterial->Expressions[0])
                        : nullptr;
                    TestTrue(Name + TEXT(" fresh graph uses an asset reference without copied pixels"),
                        Sample && Sample->Texture == (Name == TEXT("GamePublication_Mat") ? Game : Creator[0]));
                    if (Cold(Made, true, Name))
                        ActionMaterialPackages.Add(Made->GetOutermost());
                }
            }
            TestTrue(TEXT("Create Material leaves source pixel bytes unchanged"),
                Source->Source.GetMipData(AfterPixels, 0) && BeforePixels == AfterPixels);
            TestFalse(TEXT("Create Material does not qualify the original texture itself"), P4HasCreatorSource(Game));
            TestTrue(
                TEXT("Create Material preserves the source's original role"), P4QueryOriginalAsset(Game).IsOriginal());
            // A reopened material's texture sample owns a reference to this source texture.
            // Release only these saved fixture graph packages before the later cold replacement.
            FText UnloadError;
            const bool ActionMaterialsReleased = UPackageTools::UnloadPackages(ActionMaterialPackages, UnloadError);
            TestTrue(TEXT("Release saved action materials before replacing their referenced source: ") +
                    UnloadError.ToString(),
                ActionMaterialsReleased);
        }
        UObject* Duplicate = Copy(Game, TEXT("GameCopy"));
        if (Duplicate)
        {
            TestFalse(
                TEXT("Ordinary game copy with inherited import data is unqualified"), P4HasCreatorSource(Duplicate));
            Negative(Duplicate, Root + TEXT("/GamePublication"), false, TEXT("GameCopy"));
            Cold(Duplicate, false, TEXT("GameCopy"));
            Negative(Duplicate, Root + TEXT("/GamePublication"), false, TEXT("GameCopy cold"));
        }
        // A real complete file replacement is positive even at a previously original path.
        Game = ImportFile(TEXT("GamePublication"), false);
        P4Kadan::FlushReadOnlyGuards();
        if (TestNotNull(TEXT("Complete external replacement of original fixture succeeds"), Game))
        {
            TestTrue(
                TEXT("Complete external source qualifies independently of original history"), P4HasCreatorSource(Game));
            TestTrue(TEXT("Complete external replacement retains original history"),
                P4QueryOriginalAsset(Game).IsOriginal());
            TestFalse(TEXT("Complete texture replacement retires intrinsic current game payload"),
                P4HasIntrinsicGameSource(Game));
            if (Cold(Game, true, TEXT("GamePublication replacement")))
            {
                UObject* ReplacementCopy = Copy(Game, TEXT("ReplacementCopy"));
                if (ReplacementCopy)
                {
                    TestTrue(TEXT("Genuine replaced texture copy carries positive source"),
                        P4HasCreatorSource(ReplacementCopy));
                    TestFalse(TEXT("Genuine replaced texture copy has no intrinsic negative role"),
                        P4HasIntrinsicGameSource(ReplacementCopy));
                    TestTrue(TEXT("Genuine replaced texture copy has ordinary edit policy"),
                        P4Kadan::GetAssetPolicy(ReplacementCopy).AllowEdit);
                    Cold(ReplacementCopy, true, TEXT("ReplacementCopy"));
                    TestTrue(TEXT("Genuine replaced texture copy stays editable after reopen"),
                        P4Kadan::GetAssetPolicy(ReplacementCopy).AllowEdit);
                    TestFalse(TEXT("Historic source metadata does not recreate negative copy origin"),
                        P4HasIntrinsicGameSource(ReplacementCopy));
                }
            }
        }
    }
    // Use the class-correct material-interface proxy representation: an instance referring
    // to the editor's real default parent. An empty Texture2D has no valid mip payload and
    // cannot stand in for a real importer proxy in a cold package test.
    UMaterialInstanceConstant* ProxyInstance = NewObject<UMaterialInstanceConstant>(
        CreatePackage(*(Root + TEXT("/Proxy"))), TEXT("Proxy"), RF_Public | RF_Standalone);
    if (!TestNotNull(TEXT("Real material-interface proxy parent is available"), DefaultParent))
        return false;
    ProxyInstance->SetParentEditorOnly(DefaultParent);
    UObject* Proxy = ProxyInstance;
    Proxy->GetOutermost()->GetMetaData()->SetValue(Proxy, TEXT("P4BlueprintStandIn"), TEXT("1"));
    FAssetRegistryModule::AssetCreated(Proxy);
    P4Kadan::UpdateReadOnlyAsset(Proxy);
    Negative(Proxy, Root + TEXT("/Proxy"), true, TEXT("Known proxy"));
    UObject* ProxyDuplicate = Copy(Proxy, TEXT("ProxyCopy"));
    if (ProxyDuplicate)
    {
        TestFalse(TEXT("Ordinary stand-in copy is unqualified"), P4HasCreatorSource(ProxyDuplicate));
        Negative(ProxyDuplicate, Root + TEXT("/Proxy"), true, TEXT("ProxyCopy"));
        Cold(ProxyDuplicate, false, TEXT("ProxyCopy"));
        Negative(ProxyDuplicate, Root + TEXT("/Proxy"), true, TEXT("ProxyCopy cold"));
    }
    {
        // Separate synthetic texture boundary fixture: real decoded source is required for
        // a texture replacement test; this does not claim recovered game appearance.
        P4ImportPublication::FImportScope Import;
        Proxy = ImportFile(TEXT("TextureProxy"), false);
        if (!TestNotNull(TEXT("Synthetic texture-role fixture has actual decoded file source"), Proxy))
            return false;
        Proxy->GetOutermost()->GetMetaData()->SetValue(Proxy, TEXT("P4BlueprintStandIn"), TEXT("1"));
        P4Kadan::UpdateReadOnlyAsset(Proxy);
        Negative(Proxy, Root + TEXT("/TextureProxy"), true, TEXT("Known texture-role proxy"));
    }
    Proxy = ImportFile(TEXT("TextureProxy"), false);
    P4Kadan::FlushReadOnlyGuards();
    if (TestNotNull(TEXT("Complete external file replaces the synthetic stand-in texture role"), Proxy))
    {
        TestTrue(TEXT("Completed source replacement qualifies the former proxy"), P4HasCreatorSource(Proxy));
        TestFalse(TEXT("Completed texture source retires intrinsic proxy payload"), P4HasIntrinsicGameSource(Proxy));
        TestFalse(TEXT("Completed replacement retires the current proxy role"),
            Proxy->GetOutermost()->GetMetaData()->HasValue(Proxy, TEXT("P4BlueprintStandIn")));
        Cold(Proxy, true, TEXT("TextureProxy replacement"));
        // The common game publication boundary must strip origin inherited from reused objects.
        {
            P4ImportPublication::FImportScope Import;
            FP4OriginalRecord Original;
            Original.Package = Proxy->GetOutermost()->GetName();
            Original.Class = Proxy->GetClass()->GetPathName();
            Original.Kind = TEXT("texture");
            TestTrue(TEXT("Reused object can enter original publication"), P4MarkOriginalAsset(Proxy, Original, Why));
        }
        TestFalse(
            TEXT("Game publication strips prior creator-origin data from reused object"), P4HasCreatorSource(Proxy));
        Negative(Proxy, Root + TEXT("/TextureProxy"), false, TEXT("Republished replacement as game source"));
    }
    return !HasAnyErrors();
}
#endif

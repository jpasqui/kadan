#if P4_MAPKIT_TESTS
// The shared helpers of the export driver tests (P4ExportDriverTestShared.h).
#include "P4ExportDriverTestShared.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/UObjectIterator.h"

namespace P4SweepCapture
{
    void AddSavedDayLevels(UWorld* World,const FString& Map,const FP4KadanImportResult& Imported,
        const TSharedRef<FJsonObject>& Report)
    {
        TSet<FString> Packages;
        const auto DayLayer=[&](const FString& Package)
        {return Package!=Map && !Package.Contains(TEXT("_Night")) && !Package.Contains(TEXT("_Hero"));};
        for (const auto& Asset : Imported.Imported)
            if (Asset.Value==TEXT("World") && DayLayer(Asset.Key)) Packages.Add(Asset.Key);
        // A resumed plan can omit previously completed layers altogether. Inspect
        // saved original worlds in this map's own folder, never arbitrary creator maps.
        FString Prefix=FPackageName::GetShortName(Map); Prefix.RemoveFromEnd(TEXT("_P")); Prefix+=TEXT("_");
        TArray<FString> Files;
        const FString Directory=FPackageName::LongPackageNameToFilename(FPackageName::GetLongPackagePath(Map));
        IFileManager::Get().FindFilesRecursive(Files,*Directory,TEXT("*.umap"),true,false);
        for (const FString& File : Files)
        {
            if (!FPaths::GetBaseFilename(File).StartsWith(Prefix,ESearchCase::CaseSensitive)) continue;
            FString Package;
            if (!FPackageName::TryConvertFilenameToLongPackageName(File,Package) || !DayLayer(Package)) continue;
            UWorld* Saved=LoadObject<UWorld>(nullptr,*(Package+TEXT(".")+FPackageName::GetShortName(Package)));
            FP4OriginalRecord Record; FString Error;
            if (Saved && P4ReadOriginalRecord(Saved,Record,Error) && Record.Kind==TEXT("level") && Record.Package==Package)
                Packages.Add(Package);
        }
        TArray<FString> Sorted=Packages.Array(); Sorted.Sort();
        TArray<TSharedPtr<FJsonValue>> Listed,Failed;
        for (const FString& Package : Sorted)
        {
            Listed.Add(MakeShared<FJsonValueString>(Package));
            if (!World || !UEditorLevelUtils::AddLevelToWorld(World,*Package,ULevelStreamingAlwaysLoaded::StaticClass()))
                Failed.Add(MakeShared<FJsonValueString>(Package));
        }
        Report->SetArrayField(TEXT("capture_saved_layer_packages"),Listed);
        Report->SetArrayField(TEXT("capture_saved_layers_not_loaded"),Failed);
    }

}

namespace P4ExportDriverTest
{
    // Opt-in lifetime evidence for a kept-map sweep. This does not retain worlds,
    // change their types/flags, or bypass the editor's normal cleanup checks.
    void TraceWorldsBeforeMapCleanup()
    {
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_WORLD_LIFETIME_TRACE")) != TEXT("1")) return;
        UWorld* Current = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        int32 Count = 0;
        for (TObjectIterator<UWorld> It; It && Count < 64; ++It)
        {
            UWorld* Candidate = *It;
            if (!IsValid(Candidate) || !Candidate->GetOutermost()->GetName().StartsWith(TEXT("/Game/Carrot4/Maps/"))) continue;
            UWorld* Owning = Candidate->PersistentLevel ? Candidate->PersistentLevel->OwningWorld : nullptr;
            const bool InCurrentLevels = Current && Candidate->PersistentLevel && Current->GetLevels().Contains(Candidate->PersistentLevel);
            int32 CurrentStreamingLoads = 0;
            if (Current && Candidate->PersistentLevel)
                for (const ULevelStreaming* Streaming : Current->GetStreamingLevels())
                    if (IsValid(Streaming) && Streaming->GetLoadedLevel() == Candidate->PersistentLevel) ++CurrentStreamingLoads;
            UE_LOG(LogTemp, Display, TEXT("Map sweep before cleanup: world=%s type=%d flags=%08x rooted=%d context=%d current=%d owning=%s levels=%d packageflags=%08x levelincurrentlevels=%d currentstreamingloads=%d"),
                *Candidate->GetPathName(), int32(Candidate->WorldType), uint32(Candidate->GetFlags()), int32(Candidate->IsRooted()),
                int32(GEngine && GEngine->GetWorldContextFromWorld(Candidate)), int32(Candidate == Current),
                IsValid(Owning) ? *Owning->GetPathName() : TEXT("none"), Candidate->GetLevels().Num(), uint32(Candidate->GetOutermost()->GetPackageFlags()),
                int32(InCurrentLevels), CurrentStreamingLoads);
            ++Count;
        }
        UE_LOG(LogTemp, Display, TEXT("Map sweep before cleanup: traced %d worlds (limit64)"), Count);
    }

    bool QualifiedOriginalFamilyActor(AActor* Actor, const TSet<FName>& Qualified, const TSet<FName>& Omitted)
    {
        TSet<AActor*> Seen;
        for (int32 Depth = 0; Depth < 32; ++Depth)
        {
            if (!IsValid(Actor) || Omitted.Contains(Actor->GetFName()) || Seen.Contains(Actor)) return false;
            if (Qualified.Contains(Actor->GetFName())) return true;
            Seen.Add(Actor);
            UChildActorComponent* Parent = Actor->GetParentComponent();
            if (!IsValid(Parent) || Parent->GetChildActor() != Actor) return false;
            AActor* Owner = Parent->GetOwner();
            if (!IsValid(Owner) || Owner->GetLevel() != Actor->GetLevel()) return false;
            Actor = Owner;
        }
        return false;
    }

    FString ObjectPathOf(const FString& Package)
{ return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package); }

    bool KeepImport(FAutomationTestBase& Test, const FString& Name, const FP4KadanImportResult& Import)
    {
        // Private automation evidence only. The next test reuses Saved/Kadan's source caches. A whole map's report is
        // kept whatever its size (Area001's is about 19 MB).
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/Lane2Imports") / (Name + TEXT(".json"));
        return Test.TestTrue(TEXT("the actual import report is retained for ") + Name, !Import.Report.IsEmpty() &&
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) &&
            FFileHelper::SaveStringToFile(Import.Report, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }

    // Each test reuses its own output so an earlier export cannot leave another mod's files behind.
    bool Request(FAutomationTestBase& Test, const FString& Id, const FString& Name, FP4ExportModRequest& Out, FP4ModNames& Names)
    {
        const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
        const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
        if (NativeRoot.IsEmpty() || GamePaks.IsEmpty())
        { Test.AddError(TEXT("Real container export verification requires the native tests' root and original containers")); return false; }
        const FString Repo = FPaths::GetPath(FPaths::GetPath(NativeRoot));
        const FString Scratch = FPaths::ProjectSavedDir() / TEXT("P4MapKit/ExportDriverTest") / Id;
        if (!IFileManager::Get().DeleteDirectory(*Scratch, false, true))
        { Test.AddError(TEXT("The export test's previous output could not be cleared")); return false; }
        FP4ModDescriptor Mod; Mod.Id = Id; Mod.Name = Name; Mod.Version = TEXT("1.0.0");
        FString Error; FP4ModDescriptor Existing;
        Out.ProjectDirectory = Scratch / TEXT("project");
        if (!P4ReadModDescriptor(Out.ProjectDirectory, Mod.Id, Existing, Error) && !P4WriteModDescriptor(Out.ProjectDirectory, Mod, nullptr, Error))
        { Test.AddError(Error); return false; }
        if (!P4ModDerivedNames(Mod, Names, Error)) { Test.AddError(Error); return false; }
        Out.Mod = Mod; Out.OutputDirectory = Scratch / TEXT("out"); Out.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit/ExportDriverTest") / Id / TEXT("work"); Out.GamePaksDirectory = GamePaks;
        Out.Tools.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
        Out.Tools.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
        if (Out.Tools.Retoc.IsEmpty()) Out.Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
        if (Out.Tools.Repak.IsEmpty()) Out.Tools.Repak = Repo / TEXT("private/repak/repak.exe");
        Out.ExtractedGameDirectory = NativeRoot / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
        return true;
    }

    // Actual external inputs pass through Unreal's factory callbacks, as a creator's import does.
    UObject* ImportOwn(FAutomationTestBase& Test, const FString& Root, const FString& Name, bool Mesh)
    {
        const FString Package = Root / Name;
        if (FindPackage(nullptr, *Package) || FPackageName::DoesPackageExist(Package))
        { Test.AddError(TEXT("Fixture destination already exists: ") + Package); return nullptr; }
        const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/ExportDriverTest/source"));
        if (!IFileManager::Get().MakeDirectory(*Folder, true)) return nullptr;
        const FString File = Folder / (Mesh ? TEXT("creator.obj") : TEXT("creator.tga"));
        bool Written = false;
        if (Mesh)
        {
            const FString Obj = TEXT("o CreatorCube\nv -50 -50 -50\nv 50 -50 -50\nv 50 50 -50\nv -50 50 -50\nv -50 -50 50\nv 50 -50 50\nv 50 50 50\nv -50 50 50\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 3/3 2/2\nf 1/1 4/4 3/3\nf 5/1 6/2 7/3\nf 5/1 7/3 8/4\nf 1/1 2/2 6/3\nf 1/1 6/3 5/4\nf 2/1 3/2 7/3\nf 2/1 7/3 6/4\nf 3/1 4/2 8/3\nf 3/1 8/3 7/4\nf 4/1 1/2 5/3\nf 4/1 5/3 8/4\n");
            Written = FFileHelper::SaveStringToFile(Obj, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        else
        {
            TArray<uint8> Tga = {0,0,2,0,0,0,0,0,0,0,0,0,32,0,32,0,32,40};
            for (int32 I = 0; I < 32 * 32; ++I) Tga.Append({255,0,255,255});
            Written = FFileHelper::SaveArrayToFile(Tga, *File);
        }
        if (!Test.TestTrue(TEXT("the actual creator source file is written"), Written)) return nullptr;
        TStrongObjectPtr<UFactory> Factory(Mesh ? static_cast<UFactory*>(NewObject<UFbxFactory>()) : static_cast<UFactory*>(NewObject<UTextureFactory>()));
        Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
        Factory->AssetImportTask->bAutomated = true;
        if (UFbxFactory* Fbx = Cast<UFbxFactory>(Factory.Get()))
        {
            Fbx->SetDetectImportTypeOnImport(false);
            Fbx->ImportUI->MeshTypeToImport = FBXIT_StaticMesh;
            Fbx->ImportUI->bAutomatedImportShouldDetectType = false;
            Fbx->ImportUI->bImportMaterials = false; Fbx->ImportUI->bImportTextures = false;
            Fbx->ImportUI->StaticMeshImportData->bAutoGenerateCollision = false;
        }
        bool Cancelled = false;
        UObject* Asset = Factory->ImportObject(Mesh ? UStaticMesh::StaticClass() : UTexture2D::StaticClass(),
            CreatePackage(*Package), FName(*Name), RF_Public | RF_Standalone | RF_Transactional, File, nullptr, Cancelled);
        Test.TestFalse(TEXT("the creator import was not cancelled"), Cancelled);
        if (Asset) FAssetRegistryModule::AssetCreated(Asset);
        Factory->CleanUp();
        if (Asset) Test.TestTrue(TEXT("the completed external import has creator source"), P4HasCreatorSource(Asset));
        return Asset;
    }

    // The game material instance a creator instance is made from: Cave000's soil instance when the project has it,
    // otherwise the first by path of the instances the open map's static meshes use whose material takes a Diffuse_01
    // texture. The game only has shaders for how it uses a material, so one made for characters cannot draw a static mesh.
    UMaterialInterface* OwnParent()
    {
        // P4_NATIVE_SWEEP_CUBE_PARENT names one, to test a parent the game made for another kind of mesh.
        const FString Named = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CUBE_PARENT"));
        if (!Named.IsEmpty())
            return LoadObject<UMaterialInterface>(nullptr, *ObjectPathOf(Named), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (FPackageName::DoesPackageExist(GameParent))
            if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ObjectPathOf(GameParent), nullptr, LOAD_NoWarn | LOAD_Quiet))
                return Parent;
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        UMaterialInterface* Found = nullptr;
        for (TObjectIterator<UStaticMeshComponent> It; It && World; ++It)
        {
            UStaticMesh* Mesh = It->GetWorld() == World ? It->GetStaticMesh() : nullptr;
            UMaterialInterface* Parent = Mesh ? Mesh->GetMaterial(0) : nullptr;
            UTexture* Texture = nullptr;
            if (Parent && Parent->IsA<UMaterialInstanceConstant>() && Parent->GetPathName().StartsWith(TEXT("/Game/Carrot4/")) &&
                (!Found || Parent->GetPathName() < Found->GetPathName()) &&
                Parent->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Diffuse_01")), Texture))
                Found = Parent;
        }
        return Found;
    }

    bool MakeOwn(FAutomationTestBase& Test, const FString& Root, FOwn& Own)
    {
        for (const TCHAR* Leaf : {TEXT("T_Own"), TEXT("MI_Own"), TEXT("SM_Own")})
            if (FindObject<UPackage>(nullptr, *(Root / Leaf)) || FPackageName::DoesPackageExist(Root / Leaf))
            { Test.AddError(TEXT("A fixture package is already in the project. Do not overwrite another test.")); return false; }
        Own.Texture.Reset(Cast<UTexture2D>(ImportOwn(Test, Root, TEXT("T_Own"), false)));
        if (!Own.Texture.IsValid()) return false;
        Own.Texture->MipGenSettings = TMGS_NoMipmaps; Own.Texture->PostEditChange();
        Own.Made.Add(Own.Texture.Get());
        if (UMaterialInterface* Parent = OwnParent())
            {
                TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>(), Textures = MakeShared<FJsonObject>();
                Json->SetStringField(TEXT("parent"), Parent->GetPathName());
                Textures->SetStringField(TEXT("Diffuse_01"), Own.Texture->GetPathName()); Json->SetObjectField(TEXT("textures"), Textures);
                FString Input, Error; FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Input));
                TArray<FString> Missing; Own.Instance.Reset(P4BuildMaterialInstance(Input, Root / TEXT("MI_Own"), Missing, Error));
                if (!Test.TestTrue(TEXT("the supported creator instance builder succeeds: ") + Error, Own.Instance.IsValid())) return false;
                Test.TestTrue(TEXT("the creator instance builder qualifies its exact output"), P4HasCreatorSource(Own.Instance.Get()));
                Own.Made.Add(Own.Instance.Get());
            }
        Own.Mesh.Reset(Cast<UStaticMesh>(ImportOwn(Test, Root, TEXT("SM_Own"), true)));
        if (!Test.TestTrue(TEXT("the creator mesh is imported from its external geometry"), Own.Mesh.IsValid())) return false;
        Own.Mesh->CreateBodySetup();
        FKBoxElem Box; Box.X = 100; Box.Y = 100; Box.Z = 100;
        Own.Mesh->BodySetup->AggGeom.BoxElems.Add(Box);
        Own.Mesh->BodySetup->InvalidatePhysicsData(); Own.Mesh->BodySetup->CreatePhysicsMeshes();
        Own.Mesh->PostEditChange(); Own.Made.Add(Own.Mesh.Get());
        if (Own.Instance.IsValid()) Own.Mesh->SetMaterial(0, Own.Instance.Get());
        return true;
    }

    bool CookedNames(const FString& Header, std::vector<std::string>& Names)
    {
        TArray<uint8> Head; const int64 Body = IFileManager::Get().FileSize(*FPaths::ChangeExtension(Header, TEXT("uexp")));
        if (Body < 0 || !FFileHelper::LoadFileToArray(Head, *Header, FILEREAD_Silent)) return false;
        const P4MapKit::FPackageInventory Inventory = P4MapKit::ReadPackageHeader(std::vector<std::uint8_t>(Head.GetData(), Head.GetData() + Head.Num()), std::size_t(Body));
        Names = Inventory.Names;
        return Inventory.Error.Code.empty();
    }

    bool Has(const std::vector<std::string>& Names, const char* Name)
{ return std::find(Names.begin(), Names.end(), Name) != Names.end(); }

    // What an import made, taken out of the editor and off the disk again when the test ends. The project's
    // files before the import are kept in a file while it runs, so that a run cut off is cleaned up by the next.
    TArray<FString> ContentFiles()
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), TEXT("*"), true, false);
        for (FString& File : Files) File = FPaths::ConvertRelativePathToFull(File);
        return Files;
    }

    bool RemoveFiles(const TArray<FString>& Files, const TSet<FName>& Preserve)
    {
        TArray<UPackage*> Packages;
        for (const FString& File : Files)
        {
            FString Package;
            if (FPackageName::IsPackageFilename(File) && FPackageName::TryConvertFilenameToLongPackageName(File, Package))
                if (UPackage* Made = FindPackage(nullptr, *Package)) Packages.AddUnique(Made);
        }
        bool Retired = true;
        TSet<FName> Kept = Preserve;
        for (UPackage* Made : Packages)
        {
            if (Preserve.Contains(Made->GetFName())) continue;
            if (!P4FixtureRetirement::Package(Made)) { Kept.Add(Made->GetFName()); Retired = false; }
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        for (const FString& File : Files)
        {
            FString Name;
            if (FPackageName::TryConvertFilenameToLongPackageName(FPaths::ChangeExtension(File, TEXT("uasset")), Name) && Kept.Contains(FName(*Name))) continue;
            if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true, true)) Retired = false;
            for (FString Folder = FPaths::GetPath(File); FPaths::IsUnderDirectory(Folder, Content) && !FPaths::IsSamePath(Folder, Content); Folder = FPaths::GetPath(Folder))
                if (!IFileManager::Get().DeleteDirectory(*Folder, false, false)) break;
        }
        return Retired;
    }

    TSharedPtr<FJsonObject> ReadJson(const FString& File)
    {
        FString Text; TSharedPtr<FJsonObject> Json;
        if (FFileHelper::LoadFileToString(Text, *File)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    }

}

#endif

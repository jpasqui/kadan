#if P4_MAPKIT_TESTS
#include "P4ExportDriver.h"
#include "P4ExportMaps.h"
#include "P4KadanImport.h"
#include "P4LevelChanges.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/export_contract.h"
#include "AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

// Reuse the real container fixture and its native package retirement, rather than a second exporter.
namespace P4ExportDriverTest
{
bool Request(FAutomationTestBase&, const FString&, const FString&, FP4ExportModRequest&, FP4ModNames&);
TArray<FString> ContentFiles();
bool RemoveFiles(const TArray<FString>&, const TSet<FName>&);
bool CookedNames(const FString&, std::vector<std::string>&);
UObject* ImportOwn(FAutomationTestBase&, const FString&, const FString&, bool);
} //namespace P4ExportDriverTest
namespace P4FixtureRetirement
{
TSet<FName> LivePackages();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MultiMapGroupingParityTest, "P4MapKit.Parity.MultiMapGrouping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MultiMapGroupingParityTest::RunTest(const FString&)
{
    const FString First(TEXT("/Game/First/Map_P")), Second(TEXT("/Game/Second/Map_P"));
    const FString A(TEXT("/Game/First/Sublevels/Area")), B(TEXT("/Game/Second/Sublevels/Area"));
    FP4WorldStreamingGraphResult One;
    One.SourcePackage = First;
    One.Nodes.Add(First, FP4WorldStreamingNode());
    One.Nodes.Add(A, FP4WorldStreamingNode());
    FP4WorldStreamingGraphResult Two;
    Two.SourcePackage = Second;
    Two.Nodes.Add(Second, FP4WorldStreamingNode());
    Two.Nodes.Add(B, FP4WorldStreamingNode());
    TArray<FP4ExportMapGroup> Groups;
    FString Error;
    if (!TestTrue(TEXT("Each edited level belongs to its own persistent map"),
            P4GroupExportMapGraphs({B, A}, {Two, One}, Groups, Error)))
        return false;
    TestTrue(TEXT("Group order follows the map path rather than selection order"),
        Groups.Num() == 2 && Groups[0].Map == First && Groups[0].Levels == TArray<FString>{A} &&
            Groups[1].Map == Second && Groups[1].Levels == TArray<FString>{B});
    const TArray<FP4ExportMapGroup> Before = Groups;
    Two.Nodes.Add(A, FP4WorldStreamingNode());
    bool MissingPersistentMap = true;
    TestFalse(TEXT("An edit shared by two persistent maps is refused"),
        P4GroupExportMapGraphs({A}, {One, Two}, Groups, Error, &MissingPersistentMap));
    TestFalse(TEXT("Ambiguous ownership does not ask for an imported map"), MissingPersistentMap);
    TestTrue(TEXT("Refusal leaves the complete earlier grouping intact"),
        Groups.Num() == Before.Num() && Groups[0].Map == Before[0].Map);
    FP4WorldStreamingGraphResult Child;
    Child.SourcePackage = A;
    Child.Nodes.Add(A, FP4WorldStreamingNode());
    TestTrue(TEXT("A nested streaming world remains part of its outer map"),
        P4GroupExportMapGraphs({A}, {Child, One}, Groups, Error) && Groups.Num() == 1 && Groups[0].Map == First);
    TestFalse(TEXT("An orphan edit cannot be treated as a persistent map"),
        P4GroupExportMapGraphs({B}, {One}, Groups, Error, &MissingPersistentMap));
    TestTrue(TEXT("An orphan edit asks for its persistent map"), MissingPersistentMap);
    TestFalse(TEXT("Duplicate streaming graph identities are refused"),
        P4GroupExportMapGraphs({A}, {One, One}, Groups, Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MultiMapManifestParityTest, "P4MapKit.Parity.MultiMapManifest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MultiMapManifestParityTest::RunTest(const FString&)
{
    using namespace P4MapKit::Export;
    Descriptor Mod{"parity.multi.map", "Parity Multiple Maps", "1.0.0"};
    MapEdit First;
    First.Package = "/Game/First/Shared_P";
    First.Changes["Shared_Area"].Move["Floor_1"].Location = {{10, 20, 30}};
    MapEdit Second;
    Second.Package = "/Game/Second/Shared_P";
    Second.Changes["Shared_Area"].Remove = {"Floor_2"};
    std::map<std::string, std::string> Files;
    std::string Error;
    if (!TestTrue(TEXT("The compiled exporter core accepts two edited maps"),
            FrameworkFiles(Mod, std::vector<MapEdit>{First, Second}, Files, Error)))
        return false;
    TSharedPtr<FJsonObject> Manifest;
    TestTrue(TEXT("The one mod manifest parses"),
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(Files.at("mod.json").c_str())), Manifest));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    if (!TestTrue(TEXT("Both maps are in this mod's shipping manifest"),
            Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 2))
        return false;
    FString FirstChanges, SecondChanges;
    TestTrue(TEXT("Maps with the same asset name use distinct change files"),
        (*Maps)[0]->AsObject()->TryGetStringField(TEXT("changes"), FirstChanges) &&
            (*Maps)[1]->AsObject()->TryGetStringField(TEXT("changes"), SecondChanges) && FirstChanges != SecondChanges);
    TestTrue(TEXT("Both change files are admitted by the normal mod output contract"),
        AllowedFile(Mod, "romfs/tsuchi/mods/" + Mod.Id + '/' + TCHAR_TO_UTF8(*FirstChanges), Error) &&
            AllowedFile(Mod, "romfs/tsuchi/mods/" + Mod.Id + '/' + TCHAR_TO_UTF8(*SecondChanges), Error));
    const auto Before = Files;
    TestFalse(TEXT("The same game map cannot silently overwrite itself"),
        FrameworkFiles(Mod, std::vector<MapEdit>{First, First}, Files, Error));
    TestTrue(TEXT("A refused second map leaves the earlier complete result intact"), Files == Before);
    AddInfo(TEXT(
        "This is the native manifest writer's editor test. Real two-map panel export and game travel remain required."));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4EmulatorOutputParityTest, "P4MapKit.Parity.EmulatorOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4EmulatorOutputParityTest::RunTest(const FString&)
{
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(
            *this, TEXT("parity.emulator.output"), TEXT("Parity Emulator Output"), Request, Names))
        return false;
    const FString Given = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_PARITY_EMULATOR_MODS"));
    Request.OutputDirectory = Given.IsEmpty()
        ? FPaths::ProjectSavedDir() / TEXT("P4Parity/emulator/load/0100B7C00933A000")
        : Given;
    TStrongObjectPtr<UObject> Texture(
        P4ExportDriverTest::ImportOwn(*this, Names.PackageRoot, TEXT("T_ParityOutput"), false));
    if (!TestNotNull(TEXT("A real creator texture is imported"), Texture.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        UObject* Asset = Texture.Get();
        if (Asset)
        {
            FAssetRegistryModule::AssetDeleted(Asset);
            Asset->ClearFlags(RF_Public | RF_Standalone);
            Asset->MarkPendingKill();
            Asset->GetOutermost()->SetDirtyFlag(false);
        }
        Texture.Reset();
    };
    FP4ExportRoot Root;
    Root.Path = Texture->GetOutermost()->GetName();
    Root.Kind = EP4ExportRootKind::Package;
    Request.Roots.Add(Root);
    FP4ExportModResult Export;
    if (!TestTrue(TEXT("The panel exporter writes directly below the selected emulator mods folder"),
            P4ExportMod(Request, Export, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Expected = FPaths::ConvertRelativePathToFull(Request.OutputDirectory / Request.Mod.Id);
    TestTrue(TEXT("The mod is published at the emulator mod's own root"),
        Export.Published && FPaths::IsSamePath(Export.Directory, Expected));
    const FString Metadata = Expected / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    TestTrue(TEXT("The manifest is in the emulator's RomFS layout"),
        IFileManager::Get().FileSize(*(Metadata / TEXT("mod.json"))) > 0);
    for (const TCHAR* Extension : {TEXT(".pak"), TEXT(".ucas"), TEXT(".utoc")})
        TestTrue(TEXT("The active mod's container is beside the manifest"),
            IFileManager::Get().FileSize(*(Metadata / (Names.Container + TEXT("_P") + Extension))) > 0);
    TestFalse(TEXT("The export does not put a container in global game Paks"),
        IFileManager::Get().DirectoryExists(*(Expected / TEXT("romfs/Carrot4/Content/Paks"))));
    FP4ExportModResult Again;
    TestTrue(TEXT("A repeat export safely replaces this mod's earlier output"), P4ExportMod(Request, Again, Error));
    AddInfo(TEXT("Direct emulator output is ") + Expected);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StockMeshParityTest, "P4MapKit.Parity.StockMeshCollisionCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StockMeshParityTest::RunTest(const FString&)
{
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("parity.stock.mesh"), TEXT("Parity Stock Mesh"), Request, Names))
        return false;
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    const TSet<FString> Before(P4ExportDriverTest::ContentFiles());
    const TSet<FName> LiveBefore = P4FixtureRetirement::LivePackages();
    ON_SCOPE_EXIT
    {
        TArray<FString> Made;
        for (const FString& File : P4ExportDriverTest::ContentFiles())
            if (!Before.Contains(File))
                Made.Add(File);
        TestTrue(
            TEXT("Only this test's imported packages are retired"), P4ExportDriverTest::RemoveFiles(Made, LiveBefore));
    };
    if (!TestFalse(TEXT("This project has no earlier Cave000 import"), FPackageName::DoesPackageExist(Map)))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Request.ExtractedGameDirectory;
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    Settings.Repak = Request.Tools.Repak;
    const FP4KadanImportResult Import = P4KadanImportMap(Settings, Map, true);
    for (const FString& Problem : Import.Problems)
        AddInfo(TEXT("Import limitation. ") + Problem);
    AddInfo(FString::Printf(
        TEXT("Complete map import succeeded %s. This test proves only the stock scenery it actually exports."),
        Import.Ok ? TEXT("yes") : TEXT("no")));
    if (!TestTrue(TEXT("The panel importer made the persistent map"), FPackageName::DoesPackageExist(Map)))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The map opens"), World))
        return false;
    ULevel* Area = nullptr;
    for (ULevelStreaming* Streaming : World->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_GameArea")))
            Area = Streaming->GetLoadedLevel();
    if (!TestNotNull(TEXT("The stock scenery level is loaded"), Area))
        return false;
    UWorld* AreaWorld = CastChecked<UWorld>(Area->GetOuter());
    AActor* Source = FindObject<AActor>(Area, TEXT("CMA_T02_CP_1x1_3"));
    if (!TestNotNull(TEXT("The source stock actor exists"), Source))
        return false;
    TInlineComponentArray<UStaticMeshComponent*> Components(Source);
    UStaticMeshComponent* Part = nullptr;
    for (UStaticMeshComponent* Component : Components)
        if (Component && Component->GetStaticMesh() &&
            Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
        {
            Part = Component;
            break;
        }
    if (!TestNotNull(TEXT("The source carries collision on its own mesh component"), Part))
        return false;
    UStaticMesh* Mesh = Part->GetStaticMesh();
    if (!TestNotNull(TEXT("The stock mesh owns its collision body"), Mesh->BodySetup))
        return false;
    const FName Profile = Part->GetCollisionProfileName();
    const FCollisionResponseContainer Responses = Part->GetCollisionResponseToChannels();
    const ECollisionEnabled::Type Enabled = Part->GetCollisionEnabled();
    const FString MeshPath = Mesh->GetPathName();
    const FTransform Original = Source->GetActorTransform();
    const FTransform CopyPlace(FRotator(0, 20, 0), FVector(-180, 160, 180), FVector(0.75f));
    FObjectDuplicationParameters Duplication(Source, Area);
    Duplication.DestName = TEXT("P4ParityCopiedStock");
    AActor* Copy = Cast<AActor>(StaticDuplicateObjectEx(Duplication));
    if (!TestNotNull(TEXT("The stock actor is duplicated with its components"), Copy))
        return false;
    Area->Actors.Add(Copy);
    Copy->SetActorTransform(CopyPlace, false, nullptr, ETeleportType::TeleportPhysics);
    Copy->RegisterAllComponents();
    FActorSpawnParameters Spawn;
    Spawn.OverrideLevel = Area;
    Spawn.Name = TEXT("P4ParityDraggedStock");
    Spawn.ObjectFlags = RF_Transactional;
    const FTransform DragPlace(FRotator(0, -20, 0), FVector(180, 160, 180), FVector(0.75f));
    AStaticMeshActor* Dragged = Cast<AStaticMeshActor>(
        World->SpawnActor(AStaticMeshActor::StaticClass(), &DragPlace, Spawn));
    if (!TestNotNull(TEXT("Dragging a mesh creates a native mesh actor"), Dragged))
        return false;
    Dragged->GetStaticMeshComponent()->SetStaticMesh(Mesh);
    ON_SCOPE_EXIT
    {
        Source->SetActorTransform(Original, false, nullptr, ETeleportType::TeleportPhysics);
        if (Copy)
            World->EditorDestroyActor(Copy, false);
        if (Dragged)
            World->EditorDestroyActor(Dragged, false);
        Area->GetOutermost()->SetDirtyFlag(false);
    };
    const FTransform Moved(Original.GetRotation(), Original.GetLocation() + FVector(90, 0, 0), Original.GetScale3D());
    Source->SetActorTransform(Moved, false, nullptr, ETeleportType::TeleportPhysics);
    TInlineComponentArray<UStaticMeshComponent*> Copies(Copy);
    UStaticMeshComponent* CopyPart = Copies.Num() ? Copies[0] : nullptr;
    if (!TestNotNull(TEXT("The duplicated actor has its own mesh component"), CopyPart))
        return false;
    TestTrue(TEXT("The moved stock actor keeps its mesh and body"),
        Part->GetStaticMesh() == Mesh && Mesh->BodySetup != nullptr);
    TestTrue(TEXT("The copied mesh stays a reference to the stock mesh"), CopyPart->GetStaticMesh() == Mesh);
    TestTrue(TEXT("Collision settings follow the copied component"),
        CopyPart->GetCollisionEnabled() == Enabled && CopyPart->GetCollisionProfileName() == Profile &&
            CopyPart->GetCollisionResponseToChannels() == Responses);
    TestTrue(TEXT("The ordinary dragged mesh actor enables the stock mesh's collision without a sync step"),
        Dragged->GetStaticMeshComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision &&
            Dragged->GetStaticMeshComponent()->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
    const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(AreaWorld);
    if (!TestTrue(TEXT("The two new actors are additions and the source is a move"),
            Changes.IsValid() && Changes.Added.Contains(Copy->GetName()) &&
                Changes.Added.Contains(Dragged->GetName()) && Changes.Moved.Num() == 1))
        return false;
    Request.MapPackage = Map;
    FP4ExportRoot Root;
    Root.Path = Area->GetOutermost()->GetName();
    Root.Kind = EP4ExportRootKind::Package;
    Request.Roots.Add(Root);
    FP4ExportModResult Result;
    if (!TestTrue(TEXT("The panel exporter publishes stock mesh references and the source move"),
            P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        return false;
    }
    // The dragged mesh actor does not move, so the export lights it in the mod's lit level. The copied game actor
    // goes there too when it is a plain static mesh actor, and into the level of unlit added actors otherwise.
    const FString Base = Names.PackageRoot + TEXT("/Maps/") + Area->GetOutermost()->GetName().Mid(6);
    const FString Unlit = Base + TEXT("_Added"), Lit = Base + TEXT("_Lit");
    const bool CopyLit = Result.BakedActors.Contains(Copy->GetName());
    AddInfo(FString::Printf(TEXT("The copied game actor %s baked lighting."), CopyLit ? TEXT("has") : TEXT("has no")));
    TestTrue(TEXT("The creator sublevels are the unlit one when needed and the lit one"),
        Result.Levels == (CopyLit ? TArray<FString>({Lit}) : TArray<FString>({Unlit, Lit})));
    TestTrue(TEXT("The container holds the sublevels and the lit one's lighting data"),
        Result.Packages.Num() == (CopyLit ? 2 : 3) && (CopyLit || Result.Packages.Contains(Unlit)) &&
            Result.Packages.Contains(Lit) && Result.Packages.Contains(Lit + TEXT("_BuiltData")));
    TestTrue(TEXT("The dragged mesh is baked"), Result.BakedActors.Contains(Dragged->GetName()));
    TestFalse(
        TEXT("The stock mesh package is never shipped"), Result.Packages.Contains(Mesh->GetOutermost()->GetName()));
    const FString Cooked = Request.WorkDirectory / (Request.Mod.Id + TEXT(".export-work/cooked/Carrot4/Content"));
    std::vector<std::string> UnlitNames, LitNames;
    if (!TestTrue(TEXT("The cooked native sublevels are readable"),
            (CopyLit || P4ExportDriverTest::CookedNames(Cooked / Unlit.Mid(6) + TEXT(".umap"), UnlitNames)) &&
                P4ExportDriverTest::CookedNames(Cooked / Lit.Mid(6) + TEXT(".umap"), LitNames)))
        return false;
    if (CopyLit)
        UnlitNames = LitNames;
    const auto Named = [](const std::vector<std::string>& Names, const FString& Value)
    { return std::find(Names.begin(), Names.end(), TCHAR_TO_UTF8(*Value)) != Names.end(); };
    TestTrue(TEXT("Both native actors survive cooking"),
        Named(UnlitNames, Copy->GetName()) && Named(LitNames, Dragged->GetName()));
    TestTrue(TEXT("The cooked sublevels name the stock mesh package"),
        Named(UnlitNames, Mesh->GetOutermost()->GetName()) && Named(LitNames, Mesh->GetOutermost()->GetName()));
    TestFalse(TEXT("No Kadan editor class leaks into the levels"),
        Named(UnlitNames, TEXT("/Script/Kadan")) || Named(LitNames, TEXT("/Script/Kadan")));
    TSharedRef<FJsonObject> Proof = MakeShared<FJsonObject>();
    Proof->SetStringField(TEXT("mod"), Request.Mod.Id);
    Proof->SetStringField(TEXT("directory"), Result.Directory);
    Proof->SetStringField(TEXT("level"), Unlit);
    Proof->SetStringField(TEXT("lit_level"), Lit);
    Proof->SetStringField(TEXT("mesh"), MeshPath);
    Proof->SetStringField(TEXT("copied"), Copy->GetName());
    Proof->SetStringField(TEXT("dragged"), Dragged->GetName());
    Proof->SetStringField(TEXT("moved"), Source->GetName());
    Proof->SetStringField(TEXT("profile"), Profile.ToString());
    Proof->SetNumberField(TEXT("collision_enabled"), int32(Enabled));
    Proof->SetStringField(TEXT("source_transform"), Original.ToHumanReadableString());
    Proof->SetStringField(TEXT("copy_transform"), CopyPlace.ToHumanReadableString());
    Proof->SetStringField(TEXT("drag_transform"), DragPlace.ToHumanReadableString());
    FString Text;
    FJsonSerializer::Serialize(Proof, TJsonWriterFactory<>::Create(&Text));
    TestTrue(TEXT("The game proof has the exact actor and mesh paths"),
        FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("P4MapKit/stock-mesh-parity.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}
#endif

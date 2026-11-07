#include "P4GamePreviewNavigation.h"
#include "P4GameNames.h"
#include "P4GamePreviewPackage.h"
#include "P4ExportNavigation.h"
#include "P4KadanContainerIndex.h"
#include "P4KadanImport.h"
#include "P4MapKitCore/navigation_convert.h"
#include "P4MapKitCore/navigation_stock.h"
#include "BSPOps.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Model.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
namespace Nav = P4MapKit::NavigationExport;

bool ReadBytes(const FString& File, std::vector<uint8>& Out, FString& Error)
{
    const int64 Size = IFileManager::Get().FileSize(*File);
    TArray<uint8> Data;
    if (Size <= 0 || Size > 64 * 1024 * 1024 || !FFileHelper::LoadFileToArray(Data, *File, FILEREAD_Silent))
    {
        Error = TEXT("A scratch preview source package is absent or exceeds 64 MiB: ") + FPaths::GetCleanFilename(File);
        return false;
    }
    Out.assign(Data.GetData(), Data.GetData() + Data.Num());
    return true;
}

bool SetAgents(UNavigationSystemV1* System, const TArray<FNavDataConfig>& Configs, FString& Error)
{
    // OverrideSupportedAgents filters the class default's existing agents;
    // it cannot install new profiles. Set only this disposable instance's
    // reflected array before initializing the navigation system.
    FArrayProperty* Property = FindFProperty<FArrayProperty>(System->GetClass(), TEXT("SupportedAgents"));
    FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
    if (!Inner || Inner->Struct != FNavDataConfig::StaticStruct())
    {
        Error = TEXT("The editor's supported navigation agent property is unavailable.");
        return false;
    }
    FScriptArrayHelper Values(Property, Property->ContainerPtrToValuePtr<void>(System));
    Values.Resize(Configs.Num());
    for (int32 Index = 0; Index < Configs.Num(); ++Index)
        Inner->Struct->CopyScriptStruct(Values.GetRawPtr(Index), &Configs[Index]);
    return true;
}

bool AddBounds(UWorld* World, TArray<AActor*>& Created, FString& Error)
{
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
        if (IsValid(*It))
            return true;
    FBox Ground(ForceInit);
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for (UPrimitiveComponent* Part : Parts)
            if (Part && Part->CanEverAffectNavigation() && Part->IsCollisionEnabled())
                Ground += Part->Bounds.GetBox();
    }
    if (!Ground.IsValid || Ground.GetSize().GetMax() > 500000.f)
    {
        Error = TEXT("Add bounded collision geometry or a Nav Mesh Bounds Volume before scratch level Play.");
        return false;
    }
    Ground = Ground.ExpandBy(FVector(100, 100, 200));
    ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>(Ground.GetCenter(), FRotator::ZeroRotator);
    if (!Bounds)
    {
        Error = TEXT("Cannot prepare scratch preview navigation bounds.");
        return false;
    }
    Created.Add(Bounds);
    TStrongObjectPtr<UCubeBuilder> Builder(NewObject<UCubeBuilder>());
    Builder->X = FMath::CeilToInt(Ground.GetSize().X);
    Builder->Y = FMath::CeilToInt(Ground.GetSize().Y);
    Builder->Z = FMath::CeilToInt(Ground.GetSize().Z);
    Bounds->PreEditChange(nullptr);
    Bounds->Brush = NewObject<UModel>(Bounds, NAME_None, RF_Transactional);
    Bounds->Brush->Initialize(nullptr, true);
    Bounds->Brush->Polys = NewObject<UPolys>(Bounds->Brush, NAME_None, RF_Transactional);
    Bounds->GetBrushComponent()->Brush = Bounds->Brush;
    Builder->Build(World, Bounds);
    FBSPOps::csgPrepMovingBrush(Bounds);
    Bounds->PostEditChange();
    return true;
}

struct FBuildWorld
{
    UWorld* World;
    EWorldType::Type PreviousType;
    TStrongObjectPtr<UNavigationSystemV1> System;
    TArray<AActor*> Created;
    bool MadeContext = false, Initialized = false;
    explicit FBuildWorld(UWorld* InWorld) : World(InWorld), PreviousType(InWorld->WorldType) {}
    ~FBuildWorld()
    {
        if (System.IsValid())
        {
            System->CancelBuild();
            System->CleanUp();
        }
        for (AActor* Actor : Created)
            if (IsValid(Actor))
                World->DestroyActor(Actor, false, false);
        World->SetNavigationSystem(nullptr);
        if (Initialized)
            World->CleanupWorld();
        if (MadeContext && GEngine)
            GEngine->DestroyWorldContext(World);
        World->WorldType = PreviousType;
    }
};
} //namespace

bool P4AcquireGamePreviewSource(
    const FP4KadanImportSettings& Settings, const FString& Package, FString& HeaderFile, FString& Error)
{
    HeaderFile.Reset();
    Error.Reset();
    if (!IsInGameThread() || !Package.StartsWith(TEXT(P4_GAME_ASSET_ROOT "/")))
    {
        Error = TEXT("A scratch preview source must name a game package on the editor thread.");
        return false;
    }
    FP4KadanContainerIndex Index;
    FString Relative;
    if (!P4KadanReadContainerIndex(Settings, Index, Error) || !Index.Resolve(Package, Relative, Error))
        return false;
    const FString Root = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Kadan/Preview/BootstrapSource"));
    if (!P4KadanConvertOriginal(Settings, Index, Package, Root, Error))
        return false;
    HeaderFile = Root / Relative;
    return P4KadanExtractedPackageReady(HeaderFile, Package, Error);
}

bool P4BuildGamePreviewNavigation(UWorld* World, const FP4KadanImportSettings& Settings, const FString& ProfileWorld,
    FP4GamePreviewNavigation& Out, FString& Error)
{
    Out = {};
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel ||
        !World->GetOutermost()->GetName().StartsWith(TEXT(P4_PREVIEW_PACKAGE_ROOT)))
    {
        Error = TEXT("Build scratch navigation only on the disposable private preview world.");
        return false;
    }
    FString ProfileHeader;
    std::vector<uint8> Header, Body;
    if (!P4AcquireGamePreviewSource(Settings, ProfileWorld, ProfileHeader, Error) ||
        !ReadBytes(ProfileHeader, Header, Error) ||
        !ReadBytes(FPaths::ChangeExtension(ProfileHeader, TEXT("uexp")), Body, Error))
        return false;
    std::vector<Nav::StockActor> Stock;
    Nav::Problem Problem;
    if (!Nav::ReadStockMap(Header, Body, Stock, Problem) || Stock.size() != 6)
    {
        Error = FString(TEXT("The game area template does not provide its six qualified navigation meshes. ")) +
            UTF8_TO_TCHAR(Problem.Code.c_str());
        return false;
    }
    FBuildWorld Build(World);
    World->WorldType = EWorldType::EditorPreview;
    if (GEngine && !GEngine->GetWorldContextFromWorld(World))
    {
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        Build.MadeContext = true;
    }
    if (!World->bIsWorldInitialized)
    {
        World->InitWorld(UWorld::InitializationValues()
                .AllowAudioPlayback(false)
                .RequiresHitProxies(false)
                .CreatePhysicsScene(true)
                .CreateNavigation(false)
                .CreateAISystem(false)
                .ShouldSimulatePhysics(false));
        Build.Initialized = true;
    }
    // Source nav actors are editor bake data; the persistent bootstrap receives
    // the game's exact actors instead. Do not ship editor Recast native tails.
    TArray<AActor*> OldNavigation;
    for (TActorIterator<ANavigationData> It(World); It; ++It)
        OldNavigation.Add(*It);
    for (AActor* Actor : OldNavigation)
        World->DestroyActor(Actor, false, false);
    // Register the authored component snapshot for collision. Re-running
    // construction scripts here would replace saved materials/components after
    // their source/copy proofs and cross-package references were established.
    World->UpdateWorldComponents(false, false);
    if (!AddBounds(World, Build.Created, Error))
        return false;
    TArray<FNavDataConfig> Configs;
    TArray<const Nav::StockActor*> Profiles;
    for (const Nav::StockActor& Actor : Stock)
    {
        Nav::Vec3 Size{};
        if (!Nav::StockDetail::Agent(Actor.Mesh, Size))
        {
            Error = TEXT("A game navigation profile has inconsistent agent sizes.");
            return false;
        }
        if (Configs.ContainsByPredicate([&](const FNavDataConfig& Config) { return Config.AgentRadius == Size[0]; }))
            continue;
        FNavDataConfig Config(Size[0], Size[1]);
        Config.AgentStepHeight = Size[2];
        Config.Name = FName(*FString::Printf(TEXT("KadanPreviewAgent%d"), Configs.Num()));
        Config.SetNavDataClass(ARecastNavMesh::StaticClass());
        Configs.Add(Config);
        Profiles.Add(&Actor);
    }
    if (Configs.Num() != 3)
    {
        Error = TEXT("The game area template must define three distinct navigation agent sizes.");
        return false;
    }
    Build.System.Reset(NewObject<UNavigationSystemV1>(World, NAME_None, RF_Transient));
    FBoolProperty* AutoCreate = FindFProperty<FBoolProperty>(
        Build.System->GetClass(), TEXT("bAutoCreateNavigationData"));
    if (!AutoCreate)
    {
        Error = TEXT("The editor's automatic navigation data creation setting is unavailable.");
        return false;
    }
    AutoCreate->SetPropertyValue_InContainer(Build.System.Get(), false);
    if (!SetAgents(Build.System.Get(), Configs, Error))
        return false;
    World->SetNavigationSystem(Build.System.Get());
    for (int32 Index = 0; Index < Configs.Num(); ++Index)
    {
        ARecastNavMesh* Actor = Cast<ARecastNavMesh>(
            Build.System->CreateNavigationDataInstanceInLevel(Configs[Index], World->PersistentLevel));
        if (!Actor)
        {
            Error = TEXT("The editor could not create the game's scratch navigation bake profile.");
            return false;
        }
        Build.Created.Add(Actor);
        const Nav::BuiltMesh& Profile = Profiles[Index]->Mesh;
        Actor->bFixedTilePoolSize = true;
        Actor->TilePoolSize = Profile.Settings.MaxTiles;
        Actor->TileNumberHardLimit = Profile.Settings.MaxTiles;
        Actor->TileSizeUU = Profile.Settings.TileWidth;
        Actor->CellSize = 1.f / Profile.Tiles[0].Header.BvQuantFactor;
        Actor->CellHeight = 9.f;
        Actor->AgentRadius = Configs[Index].AgentRadius;
        Actor->AgentHeight = Configs[Index].AgentHeight;
        Actor->AgentMaxStepHeight = Configs[Index].AgentStepHeight;
        Actor->AgentMaxSlope = 44.f;
        Actor->MinRegionArea = 0.f;
        Actor->MergeRegionSize = 500.f;
        Actor->RegionPartitioning = ERecastPartitioning::ChunkyMonotone;
        Actor->bSortNavigationAreasByCost = true;
        Actor->NavMeshOriginOffset = FVector(
            Profile.Settings.Origin[0], Profile.Settings.Origin[1], Profile.Settings.Origin[2]);
        Actor->PostEditChange();
    }
    Build.System->InitializeForWorld(*World, FNavigationSystemRunMode::EditorMode);
    Build.System->ReleaseInitialBuildingLock();
    std::vector<Nav::BakedActor> Baked;
    if (!P4BakeWorldNavigation(World, false, Baked, Error))
        return false;
    FP4GamePreviewNavigation Result;
    for (const Nav::StockActor& Actor : Stock)
    {
        Nav::Vec3 Size{};
        Nav::StockDetail::Agent(Actor.Mesh, Size);
        const Nav::BakedActor* Match = nullptr;
        for (const Nav::BakedActor& Candidate : Baked)
        {
            Nav::Vec3 BuiltSize{};
            if (!Nav::StockDetail::Agent(Candidate.Mesh, BuiltSize) || BuiltSize != Size)
                continue;
            if (Match)
            {
                Error = TEXT("The scratch navigation bake has duplicate agent profiles.");
                return false;
            }
            Match = &Candidate;
        }
        if (!Match || Match->Mesh.Tiles.empty())
        {
            Error = FString(TEXT("The scratch level has no walkable ground for the game navigation agent ")) +
                UTF8_TO_TCHAR(Actor.Name.c_str());
            return false;
        }
        const auto Differences = Nav::MapDetail::SettingsProblems(Actor.Mesh, Size, Match->Mesh, Size);
        if (!Differences.empty())
        {
            Error = FString(TEXT("The scratch navigation grid differs from the game's native profile: ")) +
                UTF8_TO_TCHAR(Differences[0].c_str());
            return false;
        }
        Nav::BuiltMesh Converted;
        Nav::ConversionReport Report;
        std::string Why;
        if (!Nav::Convert(Actor.Mesh, Match->Mesh, Converted, Why, Report, false))
        {
            Error = FString(TEXT("The scratch navigation could not be converted for the game: ")) +
                UTF8_TO_TCHAR(Why.c_str());
            return false;
        }
        std::vector<uint8> Native;
        if (!Nav::EncodePayload(Converted.Settings, Converted.Tiles, Native, Why))
        {
            Error = FString(TEXT("The scratch navigation native payload is invalid: ")) + UTF8_TO_TCHAR(Why.c_str());
            return false;
        }
        TArray<uint8> Bytes;
        Bytes.Append(Native.data(), int32(Native.size()));
        Result.NativePayloads.Add(UTF8_TO_TCHAR(Actor.Name.c_str()), MoveTemp(Bytes));
    }
    Result.Told.Add(TEXT(
        "Scratch navigation was built from the authored collision for all six game meshes; no template terrain or links were retained."));
    Out = MoveTemp(Result);
    return true;
}

bool P4PatchGamePreviewNavigation(
    TArray<uint8>& Header, TArray<uint8>& Body, const FP4GamePreviewNavigation& Navigation, FString& Error)
{
    Error.Reset();
    const std::vector<uint8> RawHeader(Header.GetData(), Header.GetData() + Header.Num());
    const std::vector<uint8> RawBody(Body.GetData(), Body.GetData() + Body.Num());
    const auto Package = P4MapKit::ReadPackageHeader(RawHeader, RawBody.size());
    if (!Package.Error.Code.empty() || Navigation.NativePayloads.Num() != 6)
    {
        Error = TEXT("The scratch bootstrap or its six navigation payloads are invalid.");
        return false;
    }
    TMap<int32, TArray<uint8>> Replacements;
    for (int32 Index = 0; Index < int32(Package.Exports.size()); ++Index)
    {
        const auto& Export = Package.Exports[Index];
        const TArray<uint8>* Native = Navigation.NativePayloads.Find(UTF8_TO_TCHAR(Export.Name.c_str()));
        if (!Native)
            continue;
        std::string Class;
        if (!Package.Reference(Export.Class, Class) || Class.find(P4_GAME_SCRIPT_PACKAGE ".RecastNavMesh") != 0)
        {
            Error = TEXT("A scratch navigation actor has an unexpected native game class.");
            return false;
        }
        const auto Properties = P4MapKit::ReadPropertyEnvelopes(
            RawBody, Package.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Properties.Error.Code.empty() || Properties.End < Export.Offset ||
            Properties.End > Export.Offset + Export.Size)
        {
            Error = TEXT("The scratch navigation actor properties could not be retained exactly.");
            return false;
        }
        TArray<uint8> Replacement;
        Replacement.Append(RawBody.data() + Export.Offset, int32(Properties.End - Export.Offset));
        Replacement.Append(*Native);
        Replacements.Add(Index, MoveTemp(Replacement));
    }
    if (Replacements.Num() != Navigation.NativePayloads.Num())
    {
        Error = TEXT("The scratch bootstrap is missing one of the game's six navigation actors.");
        return false;
    }
    return P4ReplaceGamePreviewExports(Header, Body, Replacements, Error);
}

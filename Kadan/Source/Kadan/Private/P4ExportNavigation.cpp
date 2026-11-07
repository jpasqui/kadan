#include "P4ExportNavigation.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4ExportNavigationWorld.h"
#include "P4GamePreviewNavigation.h"
#include "P4KadanImport.h"
#include "P4NavigationLayer.h"
#include "P4LevelChanges.h"
#include "P4PlacementExport.h"
#include "P4MapKitCore/navigation_map_export.h"
#include "Components/PrimitiveComponent.h"
#include "Detour/DetourNavMesh.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "NavLinkHostInterface.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastHelpers.h"
#include "NavModifierVolume.h"
#include "NavigationSystem.h"
#include "AI/NavigationSystemConfig.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace
{
namespace Nav = P4MapKit::NavigationExport;

FString Text(const std::string& Value)
{
    return UTF8_TO_TCHAR(Value.c_str());
}
Nav::Vec3 Point(const float* P)
{
    return {{P[0], P[1], P[2]}};
}

// Olimar-mode navigation meshes (RecastNavMeshPikminHero...) and the *_Hero_* sublevels they are built with.
bool IsHero(const FString& Name)
{
    return Name.Contains(TEXT("Hero"), ESearchCase::CaseSensitive);
}

// One navigation mesh the editor built, as the plain data the exporter reads (navigation_export.md).
bool Baked(const ARecastNavMesh* Navigation, Nav::BakedActor& Out, FString& Error)
{
    Out = Nav::BakedActor();
    Out.Name = TCHAR_TO_UTF8(*Navigation->GetName());
    const dtNavMesh* Mesh = Navigation->GetRecastMesh();
    if (!Mesh || !Mesh->getParams())
    {
        Error = FString::Printf(TEXT("The navigation build made no navigation mesh for %s."), *Navigation->GetName());
        return false;
    }
    const dtNavMeshParams& Params = *Mesh->getParams();
    Nav::MapSettings& Settings = Out.Mesh.Settings;
    Settings.Origin = Point(Params.orig);
    Settings.TileWidth = Params.tileWidth;
    Settings.TileHeight = Params.tileHeight;
    Settings.MaxTiles = Params.maxTiles;
    Settings.MaxPolys = Params.maxPolys;
    for (int32 Index = 0; Index < Mesh->getMaxTiles(); ++Index)
    {
        const dtMeshTile* Tile = Mesh->getTile(Index);
        if (!Tile || !Tile->header)
            continue;
        const dtMeshHeader& H = *Tile->header;
        Nav::BuiltTile T;
        T.Reference = Mesh->getTileRef(Tile);
        T.MemorySize = Tile->dataSize;
        Nav::TileHeader& Header = T.Header;
        Header.X = H.x;
        Header.Y = H.y;
        Header.Layer = H.layer;
        Header.UserId = H.userId;
        Header.MaxLinkCount = H.maxLinkCount;
        Header.OffMeshBase = H.offMeshBase;
        Header.WalkableHeight = H.walkableHeight;
        Header.WalkableRadius = H.walkableRadius;
        Header.WalkableClimb = H.walkableClimb;
        Header.Min = Point(H.bmin);
        Header.Max = Point(H.bmax);
        Header.BvQuantFactor = H.bvQuantFactor;
#if WITH_NAVMESH_SEGMENT_LINKS
        Header.SegmentPolyBase = H.offMeshSegPolyBase;
        Header.SegmentVertBase = H.offMeshSegVertBase;
#endif
        for (int32 I = 0; I < H.vertCount; ++I)
            T.Verts.push_back(Point(Tile->verts + I * 3));
        for (int32 I = 0; I < H.polyCount; ++I)
        {
            const dtPoly& Source = Tile->polys[I];
            Nav::Poly P;
            P.FirstLink = Source.firstLink;
            for (int32 J = 0; J < 6; ++J)
            {
                P.Verts[J] = Source.verts[J];
                P.Neighbors[J] = Source.neis[J];
            }
            P.Flags = Source.flags;
            P.VertCount = Source.vertCount;
            P.Area = Source.getArea();
            P.Type = Source.getType();
            T.Polys.push_back(P);
        }
        for (int32 I = 0; I < H.detailMeshCount; ++I)
        {
            const dtPolyDetail& Source = Tile->detailMeshes[I];
            Nav::DetailMesh D;
            D.VertBase = Source.vertBase;
            D.TriBase = Source.triBase;
            D.VertCount = Source.vertCount;
            D.TriCount = Source.triCount;
            T.DetailMeshes.push_back(D);
        }
        for (int32 I = 0; I < H.detailVertCount; ++I)
            T.DetailVerts.push_back(Point(Tile->detailVerts + I * 3));
        for (int32 I = 0; I < H.detailTriCount; ++I)
        {
            const unsigned char* Tri = Tile->detailTris + I * 4;
            T.DetailTris.push_back({{Tri[0], Tri[1], Tri[2], Tri[3]}});
        }
        for (int32 I = 0; I < H.bvNodeCount; ++I)
        {
            const dtBVNode& Source = Tile->bvTree[I];
            Nav::BvNode B;
            for (int32 J = 0; J < 3; ++J)
            {
                B.Bounds[J] = Source.bmin[J];
                B.Bounds[3 + J] = Source.bmax[J];
            }
            B.Index = Source.i;
            T.BvTree.push_back(B);
        }
        for (int32 I = 0; I < H.offMeshConCount; ++I)
        {
            const dtOffMeshConnection& Source = Tile->offMeshCons[I];
            Nav::PointLink C;
            for (int32 J = 0; J < 6; ++J)
                C.Pos[J] = Source.pos[J];
            C.Radius = Source.rad;
            C.Height = Source.height;
            C.Polygon = Source.poly;
            C.Flags = Source.flags;
            C.Side = Source.side;
            C.UserId = Source.userId;
            T.PointLinks.push_back(C);
        }
#if WITH_NAVMESH_SEGMENT_LINKS
        for (int32 I = 0; I < H.offMeshSegConCount; ++I)
        {
            const dtOffMeshSegmentConnection& Source = Tile->offMeshSeg[I];
            Nav::SegmentLink S;
            S.StartA = Point(Source.startA);
            S.StartB = Point(Source.startB);
            S.EndA = Point(Source.endA);
            S.EndB = Point(Source.endB);
            S.Radius = Source.rad;
            S.FirstPoly = Source.firstPoly;
            S.PolyCount = Source.npolys;
            S.Flags = Source.flags;
            S.UserId = Source.userId;
            T.SegmentLinks.push_back(S);
        }
#endif
#if WITH_NAVMESH_CLUSTER_LINKS
        for (int32 I = 0; I < H.clusterCount; ++I)
            T.Clusters.push_back(Point(Tile->clusters[I].center));
        if (H.offMeshBase > 0 && !Tile->polyClusters)
        {
            Error = FString::Printf(TEXT("A tile of %s has no cluster table."), *Navigation->GetName());
            return false;
        }
        for (int32 I = 0; I < H.offMeshBase; ++I)
            T.PolyClusters.push_back(Tile->polyClusters[I]);
#else
        Error = TEXT("This editor is built without navigation clusters, which the game's navigation has.");
        return false;
#endif
        Out.Mesh.Tiles.push_back(MoveTemp(T));
    }
    // The bake's own areas by class name, which the exporter turns into the game's.
    TArray<FSupportedAreaData> Areas;
    Navigation->GetSupportedAreas(Areas);
    for (const FSupportedAreaData& Area : Areas)
    {
        const UClass* Class = Navigation->GetAreaClass(Area.AreaID);
        if (!Class || Area.AreaID < 0 || Area.AreaID >= 64)
            continue;
        Out.Mesh.AreaIds[TCHAR_TO_UTF8(*Class->GetName())] = std::uint8_t(Area.AreaID);
    }
    return true;
}

// The levels shown for a build, and what was changed so that it can be put back.
struct FBuildScene
{
    UWorld* World = nullptr;
    const TMap<ULevel*, bool>* LevelVariants = nullptr;
    TArray<TPair<ULevel*, bool>> Shown;
    TArray<TPair<UActorComponent*, bool>> Navigation;
    ~FBuildScene() { Restore(); }
    bool Show(bool HeroLevels, FString& Error)
    {
        if (LevelVariants)
        {
            for (const auto& Pair : *LevelVariants)
            {
                const bool Wanted = !Pair.Value || HeroLevels;
                if (Pair.Key->bIsVisible != Wanted)
                {
                    UEditorLevelUtils::SetLevelVisibility(
                        Pair.Key, Wanted, false, ELevelVisibilityDirtyMode::DontModify);
                }
            }
            return true;
        }
        for (ULevelStreaming* Streaming : World->GetStreamingLevels())
        {
            if (!Streaming)
                continue;
            const FString Package = Streaming->GetWorldAssetPackageName();
            ULevel* Level = Streaming->GetLoadedLevel();
            const bool Wanted = !P4IsOlimarNavigationLevel(Package) || HeroLevels;
            if (!Level)
            {
                if (!Wanted)
                    continue;
                Error = FString::Printf(
                    TEXT(
                        "The sublevel %s is not loaded, so its ground cannot be in the navigation build. Load it in the Levels window and export again."),
                    *FPackageName::GetShortName(Package));
                return false;
            }
            if (Level->bIsVisible == Wanted)
                continue;
            if (!Shown.ContainsByPredicate([&](const TPair<ULevel*, bool>& Pair) { return Pair.Key == Level; }))
                Shown.Emplace(Level, bool(Level->bIsVisible));
            UEditorLevelUtils::SetLevelVisibility(Level, Wanted, false, ELevelVisibilityDirtyMode::DontModify);
        }
        return true;
    }
    // The game's gameplay objects (pots, creatures, gates) are not in the game's navigation build:
    // the game places them when the map is played. Their stand-ins are left out of the build too.
    void LeaveOutGameplay()
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (!P4IsGameplayActor(*It))
                continue;
            TInlineComponentArray<UActorComponent*> Parts(*It);
            for (UActorComponent* Part : Parts)
                if (Part && Part->CanEverAffectNavigation())
                {
                    Navigation.Emplace(Part, true);
                    Part->SetCanEverAffectNavigation(false);
                }
        }
    }
    void Restore()
    {
        for (const auto& Pair : Navigation)
            if (IsValid(Pair.Key))
                Pair.Key->SetCanEverAffectNavigation(Pair.Value);
        Navigation.Reset();
        for (int32 Index = Shown.Num(); Index-- > 0;)
            if (IsValid(Shown[Index].Key))
                UEditorLevelUtils::SetLevelVisibility(
                    Shown[Index].Key, Shown[Index].Value, false, ELevelVisibilityDirtyMode::DontModify);
        Shown.Reset();
    }
};

// Whether this editor can build the map's navigation the way export does. The Kadan review shows
// the same sentence before export. Nothing is added, built or changed here. Stock may be empty when
// the game's map is not extracted yet; the agent check then waits for export.
bool NavigationPreflight(UWorld* World, const std::vector<Nav::StockActor>& Stock, UNavigationSystemV1*& Existing,
    UClass*& GameClass, FString& Error, bool Snapshot = false)
{
    Existing = nullptr;
    GameClass = nullptr;
    if (!World || (!Snapshot && World->WorldType != EWorldType::Editor))
    {
        Error = TEXT("Open the map in the editor before building navigation.");
        return false;
    }
    Existing = Snapshot ? nullptr : FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Snapshot && World->GetNavigationSystem() && !Existing)
    {
        Error = TEXT("This map's active navigation system cannot build Recast data.");
        return false;
    }
    GameClass = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".CarrotNavigationSystem"));
    if (!Existing &&
        (!GameClass || !GameClass->IsChildOf(UNavigationSystemV1::StaticClass()) ||
            GameClass->HasAnyClassFlags(CLASS_Abstract)))
    {
        Error = TEXT(
            "The game's native navigation system is unavailable. Set up Kadan's game declarations and restart the editor.");
        return false;
    }
    const UNavigationSystemV1* Settings = Existing ? Existing
                                                   : Cast<UNavigationSystemV1>(GameClass->GetDefaultObject());
    if (!Settings)
    {
        Error = TEXT("The game's navigation agents are unavailable in this project.");
        return false;
    }
    for (const Nav::StockActor& Actor : Stock)
    {
        Nav::Vec3 Agent{};
        Nav::Problem Problem;
        if (!Nav::MapDetail::Admit(Actor.Name, Actor.Mesh, Agent, Problem))
        {
            Error = Problem.Message.empty() ? TEXT("The game's navigation of this map cannot be read.")
                                            : Text(Problem.Message);
            return false;
        }
        bool Matched = false;
        for (const FNavDataConfig& Candidate : Settings->GetSupportedAgents())
            Matched |= FMath::IsNearlyEqual(Candidate.AgentRadius, Agent[0], 0.01f) &&
                FMath::IsNearlyEqual(Candidate.AgentHeight, Agent[1], 0.01f);
        if (!Matched)
        {
            Error = TEXT(
                "This project lacks one of the game's navigation agents. Run Kadan's project setup and restart the editor.");
            return false;
        }
    }
    if (!Existing && !Snapshot)
        for (TActorIterator<ANavigationData> It(World); It; ++It)
            // Starting a navigation system could retire navigation actors it does not support,
            // so export never starts one beside navigation actors of the creator's.
            if (IsValid(*It))
            {
                Error = TEXT(
                    "This map has navigation actors but its navigation system is off. Turn on Enable Navigation System in World Settings.");
                return false;
            }
    return true;
}

// Game maps can intentionally disable UE's ordinary navigation system. Export
// builds with the real configured game agents without editing that authored flag
// or replacing the world's source config. A temporary system is retired before
// returning to ordinary editing. Navigation actors the build makes are removed
// again, so they never become creator data.
struct FExportNavigationSystem
{
    UWorld* World = nullptr;
    UNavigationSystemV1* Existing = nullptr;
    TStrongObjectPtr<UNavigationSystemV1> Made;
    TStrongObjectPtr<UNavigationSystemConfig> Config;
    TSet<ANavigationData*> PriorActors;
    TMap<TWeakObjectPtr<UPackage>, bool> Dirty;
    bool Ready(UWorld* InWorld, const std::vector<Nav::StockActor>& Stock, FString& Error, bool Snapshot)
    {
        World = InWorld;
        UClass* GameClass = nullptr;
        if (Stock.empty())
        {
            Error = TEXT("The game's navigation of this map is unavailable.");
            return false;
        }
        if (!NavigationPreflight(World, Stock, Existing, GameClass, Error, Snapshot))
            return false;
        for (TActorIterator<ANavigationData> It(World); It; ++It)
            if (IsValid(*It))
                PriorActors.Add(*It);
        for (ULevel* Level : World->GetLevels())
            if (Level)
                Dirty.FindOrAdd(Level->GetOutermost()) = Level->GetOutermost()->IsDirty();
        if (Existing)
            return true;
        Config.Reset(NewObject<UNavigationSystemConfig>(GetTransientPackage(), NAME_None, RF_Transient));
        Config->NavigationSystemClass = FSoftClassPath(GameClass);
        FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode, Config.Get(), true);
        Made.Reset(FNavigationSystem::GetCurrent<UNavigationSystemV1>(World));
        if (!Made.IsValid() || Made->GetClass() != GameClass)
        {
            Error = TEXT("The configured game navigation system could not initialize for export.");
            return false;
        }
        Made->SetFlags(RF_Transient);
        return true;
    }
    ~FExportNavigationSystem()
    {
        if (World)
        {
            // Navigation actors the build spawned are removed while their system still knows them.
            TArray<ANavigationData*> Spawned;
            for (TActorIterator<ANavigationData> It(World); It; ++It)
                if (IsValid(*It) && !PriorActors.Contains(*It))
                    Spawned.Add(*It);
            for (ANavigationData* Actor : Spawned)
                World->DestroyActor(Actor);
            if (Made.IsValid())
            {
                // Unregisters the temporary system's delegates and data. CleanupUnsafe keeps the
                // editor world's navigation link ids, which CleanupWithWorld would reset.
                Made->CleanUp(FNavigationSystem::ECleanupMode::CleanupUnsafe);
                if (World->GetNavigationSystem() == Made.Get())
                    World->SetNavigationSystem(nullptr);
                Made.Reset();
            }
        }
        for (const auto& Pair : Dirty)
            if (Pair.Key.IsValid())
                Pair.Key->SetDirtyFlag(Pair.Value);
    }
};

// Map-specific generation settings live in the original dtNavMesh header,
// rather than the project's shared Recast INI. Apply them only for this bake.
struct FExportNavigationSettings
{
    struct FBefore
    {
        TWeakObjectPtr<ARecastNavMesh> Actor;
        FRecastNavMeshGenerationProperties Properties;
        FVector Origin;
        explicit FBefore(ARecastNavMesh* In) : Actor(In), Properties(*In), Origin(In->NavMeshOriginOffset) {}
    };
    TArray<FBefore> Before;
    TMap<TWeakObjectPtr<UPackage>, bool> Dirty;
    // The creator's navigation system, when its actors are the ones changed for the bake.
    UNavigationSystemV1* RebuildWith = nullptr;
    bool Apply(UWorld* World, const std::vector<Nav::StockActor>& Stock, FString& Error)
    {
        for (ULevel* Level : World->GetLevels())
            if (Level)
                Dirty.FindOrAdd(Level->GetOutermost()) = Level->GetOutermost()->IsDirty();
        for (const Nav::StockActor& Source : Stock)
        {
            Nav::Vec3 Agent{};
            Nav::Problem Problem;
            if (!Nav::MapDetail::Admit(Source.Name, Source.Mesh, Agent, Problem))
            {
                Error = Text(Problem.Message);
                return false;
            }
            const Nav::MapSettings& Grid = Source.Mesh.Settings;
            const float Quantization = Source.Mesh.Tiles.front().Header.BvQuantFactor;
            if (!FMath::IsFinite(Grid.TileWidth) || Grid.TileWidth < 300.f || Grid.TileWidth != Grid.TileHeight ||
                Grid.MaxTiles <= 0 || !FMath::IsFinite(Quantization) || Quantization <= 0.f)
            {
                Error = TEXT("The original navigation grid cannot be represented by the editor's Recast settings.");
                return false;
            }
            const float Cell = 1.f / Quantization;
            if (!FMath::IsFinite(Cell) || Cell < 1.f || Cell > 1024.f ||
                Grid.TileWidth != FMath::TruncToInt(Grid.TileWidth / Cell) * Cell)
            {
                Error = TEXT("The original navigation tile size is not aligned to its source voxel grid.");
                return false;
            }
            for (const auto& Tile : Source.Mesh.Tiles)
                if (Tile.Header.BvQuantFactor != Quantization)
                {
                    Error = TEXT("The original navigation tiles use different voxel grids.");
                    return false;
                }
            ARecastNavMesh* Match = nullptr;
            for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
            {
                if (!IsValid(*It) || IsHero(It->GetName()) != IsHero(Text(Source.Name)) ||
                    It->GetClass()->GetPathName() != Text(Source.Class) ||
                    !FMath::IsNearlyEqual(It->AgentRadius, Agent[0], 0.01f))
                    continue;
                if (Match)
                {
                    Error = TEXT("More than one editor Recast actor matches an original navigation agent.");
                    return false;
                }
                Match = *It;
            }
            if (!Match)
            {
                Error = TEXT(
                    "An original navigation agent has no matching native editor Recast actor. Run Kadan's project setup and restart the editor.");
                return false;
            }
            if (Before.ContainsByPredicate([&](const FBefore& Entry) { return Entry.Actor.Get() == Match; }))
            {
                Error = TEXT("Original navigation actors ambiguously share an editor agent.");
                return false;
            }
            Match->EnsureBuildCompletion();
            Before.Emplace(Match);
            Match->bFixedTilePoolSize = true;
            Match->TilePoolSize = Grid.MaxTiles;
            Match->TileSizeUU = Grid.TileWidth;
            Match->CellSize = Cell;
            Match->AgentHeight = Agent[1];
            Match->AgentMaxStepHeight = Agent[2];
            const float Origin[3] = {Grid.Origin[0], Grid.Origin[1], Grid.Origin[2]};
            Match->NavMeshOriginOffset = Recast2UnrealPoint(Origin);
        }
        return true;
    }
    ~FExportNavigationSettings()
    {
        for (FBefore& Entry : Before)
            if (Entry.Actor.IsValid())
            {
                Entry.Actor->EnsureBuildCompletion();
                Entry.Actor->UpdateGenerationProperties(Entry.Properties);
                Entry.Actor->NavMeshOriginOffset = Entry.Origin;
                Entry.Actor->ConditionalConstructGenerator();
            }
        // The export bake replaced the creator's built navigation. Build it again from the creator's
        // own settings and scene, so nothing of theirs stays as the bake left it.
        if (RebuildWith && Before.Num() && IsValid(RebuildWith))
            RebuildWith->Build();
        for (const auto& Pair : Dirty)
            if (Pair.Key.IsValid())
                Pair.Key->SetDirtyFlag(Pair.Value);
    }
};

// Builds the world's navigation and collects the navigation meshes, the Olimar-mode ones or the others.
bool Build(UWorld* World, bool Hero, std::vector<Nav::BakedActor>& Out, FString& Error)
{
    UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!System)
    {
        Error = TEXT(
            "The map has no navigation system in the editor, so its navigation cannot be built. Turn on Enable Navigation System in its World Settings.");
        return false;
    }
    System->Build();
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
    {
        if (!IsValid(*It) || IsHero(It->GetName()) != Hero)
            continue;
        Nav::BakedActor Actor;
        if (!Baked(*It, Actor, Error))
            return false;
        Out.push_back(MoveTemp(Actor));
    }
    return true;
}
} //namespace

bool P4IsNavigationOnlyActor(const AActor* Actor)
{
    if (!Actor)
        return false;
    return Actor->IsA<ANavModifierVolume>() || Actor->IsA<ANavMeshBoundsVolume>() ||
        (Actor->GetClass()->ImplementsInterface(UNavLinkHostInterface::StaticClass()) && !P4IsGameplayActor(Actor));
}

bool P4EditChangesNavigation(const FP4LevelChangesResult& Changes, FString& Why)
{
    Why.Reset();
    // A removed actor is no longer there to ask, so any removal counts.
    if (Changes.Removed.Num())
    {
        Why = Changes.Removed[0];
        return true;
    }
    UWorld* World = FindObject<UWorld>(
        FindPackage(nullptr, *Changes.Package), *FPackageName::GetShortName(Changes.Package));
    ULevel* Level = World ? World->PersistentLevel : nullptr;
    for (const FString& Name : Changes.NavigationRelevant)
    {
        const AActor* Actor = Level ? FindObject<AActor>(Level, *Name) : nullptr;
        if (Actor && P4IsGameplayActor(Actor))
        {
            continue;
        }
        Why = Name;
        return true;
    }
    for (const FP4LevelActorMove& Move : Changes.Moved)
    {
        const AActor* Actor = Level ? FindObject<AActor>(Level, *Move.Actor) : nullptr;
        if (P4IsNavigationOnlyActor(Actor))
        {
            Why = Move.Actor;
            return true;
        }
    }
    for (const FP4LevelActorChanges& Change : Changes.OtherChanged)
    {
        const AActor* Actor = Level ? FindObject<AActor>(Level, *Change.Actor) : nullptr;
        if (P4IsNavigationOnlyActor(Actor))
        {
            Why = Change.Actor;
            return true;
        }
    }
    return false;
}

bool P4BakeWorldNavigation(
    UWorld* World, bool Hero, std::vector<P4MapKit::NavigationExport::BakedActor>& Out, FString& Error)
{
    if (!IsInGameThread() || !IsValid(World))
    {
        Error = TEXT("The navigation is built on the editor thread, in a world.");
        return false;
    }
    return Build(World, Hero, Out, Error);
}

bool P4BakeMapNavigation(UWorld* World, const FString& MapPackage,
    const std::vector<P4MapKit::NavigationExport::StockActor>& Stock, FP4MapNavigation& Out, FString& Error,
    const TMap<ULevel*, bool>* LevelVariants)
{
    Out = FP4MapNavigation();
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World))
    {
        Error = TEXT("The navigation is built on the editor thread, in a world.");
        return false;
    }
    Nav::MapExportInput In;
    Nav::Problem Problem;
    In.Package = TCHAR_TO_UTF8(*MapPackage);
    In.Stock = Stock;
    FExportNavigationSystem NavigationSystem;
    if (!NavigationSystem.Ready(World, Stock, Error, LevelVariants != nullptr))
        return false;
    FExportNavigationSettings NavigationSettings;
    NavigationSettings.RebuildWith = NavigationSystem.Existing;
    if (!NavigationSettings.Apply(World, Stock, Error))
        return false;
    bool HeroActors = false;
    for (const Nav::StockActor& Actor : In.Stock)
        HeroActors |= IsHero(Text(Actor.Name));
    // A project without the game's navigation agents (Kadan's project setup adds them) builds navigation
    // the game cannot use. The game's navigation is kept then, and the creator is told why.
    if (UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
    {
        bool Agent = false;
        for (const Nav::StockActor& Actor : In.Stock)
        {
            Nav::Vec3 Size{};
            if (!Nav::StockDetail::Agent(Actor.Mesh, Size))
                continue;
            for (const FNavDataConfig& Config : System->GetSupportedAgents())
                Agent |= FMath::IsNearlyEqual(Config.AgentRadius, Size[0], 0.01f);
        }
        if (!Agent)
        {
            Out.Kept = true;
            Out.Told.Add(TEXT(
                "This project has none of the game's navigation agents, so the game's navigation is kept and Pikmin do not know about your changes. Run Kadan's project setup and export again."));
            return true;
        }
    }
    UPackage* Persistent = World->GetOutermost();
    const bool WasDirty = Persistent->IsDirty();
    {
        FBuildScene Scene;
        Scene.World = World;
        Scene.LevelVariants = LevelVariants;
        Scene.LeaveOutGameplay();
        if (!Scene.Show(false, Error) || !Build(World, false, In.Bakes, Error))
            return false;
        if (HeroActors)
        {
            if (!Scene.Show(true, Error) || !Build(World, true, In.HeroBakes, Error))
                return false;
            In.HasHeroBake = true;
        }
    }
    // The build is the editor's own; the game's map is not saved for it.
    if (!WasDirty)
        Persistent->SetDirtyFlag(false);
    Nav::MapExport Made;
    if (!Nav::ExportMap(In, Made, Problem))
    {
        Error = Problem.Message.empty()
            ? TEXT("The navigation could not be converted for the game (") + Text(Problem.Code) + TEXT(").")
            : Text(Problem.Message);
        return false;
    }
    Out.Bytes.Append(Made.Bytes.data(), int32(Made.Bytes.size()));
    for (const Nav::ActorReport& Row : Made.Actors)
    {
        if (Row.Replaced)
        {
            FString Left;
            if (!Row.LinksSkipped.empty())
                Left = FString::Printf(TEXT(" and %d left out"), int32(Row.LinksSkipped.size()));
            Out.Told.Add(FString::Printf(
                TEXT(
                    "The navigation %s was built again from %s: %d tiles, %d polygons, %d of the game's links carried over%s."),
                *Text(Row.Actor), *Text(Row.Source), int32(Row.Tiles), int32(Row.Polygons), int32(Row.LinksAdded),
                *Left));
        }
        else
            Out.Told.Add(FString::Printf(TEXT("The navigation %s stays as the game has it, as there is %s."),
                *Text(Row.Actor), *Text(Row.Reason)));
    }
    return true;
}

bool P4BuildMapNavigation(const FString& MapPackage, const FString& GameFiles, FP4MapNavigation& Out, FString& Error,
    const FP4KadanImportSettings* Containers)
{
    Out = FP4MapNavigation();
    Error.Reset();
    // The game's own navigation of the map, from its cooked map: extracted in the game folder, or converted from
    // the game's containers when the game folder holds only those.
    const FString Base = GameFiles / P4ContentRelativePath(MapPackage);
    std::vector<std::uint8_t> Header, Body;
    bool Read = !GameFiles.IsEmpty() && P4LoadFileBytes(Base + TEXT(".umap"), Header) &&
        P4LoadFileBytes(Base + TEXT(".uexp"), Body);
    FString Converted, Why;
    if (!Read && Containers && P4AcquireGamePreviewSource(*Containers, MapPackage, Converted, Why))
        Read = P4LoadFileBytes(Converted, Header) &&
            P4LoadFileBytes(FPaths::ChangeExtension(Converted, TEXT("uexp")), Body);
    if (!Read)
    {
        Error = TEXT("The game's map ") + FPackageName::GetShortName(MapPackage) +
            TEXT(" is not in the game folder or its containers, so its navigation cannot be built. Check the game folder "
                 "in Kadan's settings.") + (Why.IsEmpty() ? FString() : TEXT(" ") + Why);
        return false;
    }
    std::vector<Nav::StockActor> Stock;
    Nav::Problem Problem;
    if (!Nav::ReadStockMap(Header, Body, Stock, Problem))
    {
        Error = Problem.Message.empty()
            ? TEXT("The navigation of the game's map could not be read (") + Text(Problem.Code) + TEXT(").")
            : Text(Problem.Message);
        return false;
    }
    FP4ExportNavigationWorld Snapshot;
    UWorld* World = Snapshot.Load(MapPackage, Error);
    return World && P4BakeMapNavigation(World, MapPackage, Stock, Out, Error, &Snapshot.Variants());
}

namespace
{
bool MapNavigationReady(UWorld* World, const FString& ExtractedGameDirectory, FString& Why, bool Snapshot)
{
    Why.Reset();
    if (!IsInGameThread() || !IsValid(World))
    {
        Why = TEXT("Open the map in the editor before building navigation.");
        return false;
    }
    std::vector<Nav::StockActor> Stock;
    if (!ExtractedGameDirectory.IsEmpty())
    {
        const FString GameFiles = IFileManager::Get().DirectoryExists(
                                      *(ExtractedGameDirectory / TEXT(P4_GAME_CONTENT_FOLDER "/" P4_GAME_PROJECT)))
            ? ExtractedGameDirectory / TEXT(P4_GAME_CONTENT_FOLDER)
            : ExtractedGameDirectory;
        const FString Base = GameFiles / P4ContentRelativePath(World->GetOutermost()->GetName());
        std::vector<std::uint8_t> Header, Body;
        Nav::Problem Problem;
        // A map not extracted yet is read during export. Only a readable map is checked here.
        if (P4LoadFileBytes(Base + TEXT(".umap"), Header) && P4LoadFileBytes(Base + TEXT(".uexp"), Body) &&
            !Nav::ReadStockMap(Header, Body, Stock, Problem))
        {
            Why = TEXT("The game's navigation of this map cannot be read.");
            return false;
        }
    }
    UNavigationSystemV1* Existing = nullptr;
    UClass* GameClass = nullptr;
    return NavigationPreflight(World, Stock, Existing, GameClass, Why, Snapshot);
}
} //namespace

bool P4MapNavigationReady(UWorld* World, const FString& ExtractedGameDirectory, FString& Why)
{
    return MapNavigationReady(World, ExtractedGameDirectory, Why, false);
}

bool P4MapNavigationPackageReady(const FString& Package, const FString& ExtractedGameDirectory, FString& Why)
{
    FP4ExportNavigationWorld Snapshot;
    UWorld* World = Snapshot.Load(Package, Why);
    if (!World)
    {
        return false;
    }
    return MapNavigationReady(World, ExtractedGameDirectory, Why, true);
}

bool P4MapNavigationPath(const FString& Manifest, const FString& MapPackage, FString& Relative)
{
    Relative.Reset();
    TSharedPtr<FJsonObject> Json;
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    if (!P4ParseJson(Manifest, Json) || !Json->TryGetArrayField(TEXT("maps"), Maps))
        return false;
    for (const TSharedPtr<FJsonValue>& Value : *Maps)
    {
        const TSharedPtr<FJsonObject>* Entry = nullptr;
        FString Map;
        if (Value->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("map"), Map) && Map == MapPackage)
            return (*Entry)->TryGetStringField(TEXT("navigation"), Relative) && !Relative.IsEmpty();
    }
    return false;
}

bool P4WriteMapNavigation(const FString& ModFolder, const FString& Relative, const TArray<uint8>& Bytes, FString& Error)
{
    if (!Relative.StartsWith(TEXT("maps/"), ESearchCase::CaseSensitive) ||
        !Relative.EndsWith(TEXT(".nav"), ESearchCase::CaseSensitive) || Relative.Contains(TEXT("..")) ||
        Relative.Mid(5).Contains(TEXT("/")))
    {
        Error = TEXT("The manifest names the map's navigation file at an unexpected path: ") + Relative;
        return false;
    }
    const FString File = ModFolder / Relative;
    if (!Bytes.Num() || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
        !FFileHelper::SaveArrayToFile(Bytes, *File))
    {
        Error = TEXT("Cannot write the map's navigation file");
        return false;
    }
    return true;
}

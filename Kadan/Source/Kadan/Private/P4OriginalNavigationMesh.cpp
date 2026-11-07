#include "P4OriginalNavigationMesh.h"
#include "P4LevelContentInternal.h"
#include "P4TextureBuild.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "P4MapKitCore/package_reader.h"
#include "AI/NavigationSystemConfig.h"
#include "Algo/Reverse.h"
#include "Detour/DetourNavMesh.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NavigationSystem.h"
#include "NavMesh/PImplRecastNavMesh.h"
#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include <map>

namespace
{
const TCHAR* const NavigationTail = TEXT("the navigation mesh of a navigation build");

// The engine's own load and the mesh it keeps, which ARecastNavMesh leaves to itself and its subclasses.
struct FNavigationMeshAccess : ARecastNavMesh
{
    static void Load(ARecastNavMesh& Mesh, FArchive& Ar, int32 Version)
    {
        FPImplRecastNavMesh* Unused = nullptr;
        (Mesh.*&FNavigationMeshAccess::SerializeRecastNavMesh)(Ar, Unused, Version);
    }
};

uint32 U32(const uint8* At)
{
    return uint32(At[0]) | uint32(At[1]) << 8 | uint32(At[2]) << 16 | uint32(At[3]) << 24;
}

// What follows a navigation mesh actor's properties: UObject's id flag (none), the navigation version, and the size of
// the mesh counted from that size on. Mesh is what follows the size.
bool ReadTail(const uint8* Data, int64 Size, uint32& Version, const uint8*& Mesh, int64& MeshSize)
{
    if (!Data || Size < 12 || U32(Data) || int64(U32(Data + 8)) != Size - 8)
        return false;
    Version = U32(Data + 4);
    Mesh = Data + 12;
    MeshSize = Size - 12;
    return true;
}

// The mesh's tiles, by their place in the mesh.
TArray<int32> AllTiles(const dtNavMesh& Detour)
{
    TArray<int32> Out;
    for (int32 Index = 0; Index < Detour.getMaxTiles(); ++Index)
    {
        const dtMeshTile* Tile = Detour.getTile(Index);
        if (Tile && Tile->header && Tile->dataSize > 0)
            Out.Add(Index);
    }
    return Out;
}

// The mesh as FPImplRecastNavMesh::Serialize saves it, with the tiles in Order: a mesh not generated while the game
// runs, whose tiles have no compressed layers.
TArray<uint8> MeshInOrder(const dtNavMesh& Detour, int32 Version, const TArray<int32>& Order)
{
    TArray<uint8> Out;
    FMemoryWriter Ar(Out, true);
    int32 Count = Order.Num();
    Ar << Count;
    dtNavMeshParams Params = *Detour.getParams();
    Ar << Params.orig[0] << Params.orig[1] << Params.orig[2];
    Ar << Params.tileWidth << Params.tileHeight << Params.maxTiles << Params.maxPolys;
    for (const int32 Index : Order)
    {
        const dtMeshTile* Tile = Detour.getTile(Index);
        dtTileRef Ref = Detour.getTileRef(Tile);
        int32 Size = Tile->dataSize;
        Ar << Ref << Size;
        unsigned char* Data = Tile->data;
        FPImplRecastNavMesh::SerializeRecastMeshTile(Ar, Version, Data, Size);
        uint8* Compressed = nullptr;
        int32 CompressedSize = 0;
        FPImplRecastNavMesh::SerializeCompressedTileCacheData(Ar, Version, Compressed, CompressedSize);
    }
    return Out;
}

// The mesh loaded again with its tiles added in Order. Detour links each tile to the tiles next to it as it is added,
// so the order gives each polygon its links.
bool Relink(ARecastNavMesh& Mesh, int32 Version, const TArray<int32>& Order)
{
    const dtNavMesh* Detour = Mesh.GetRecastMesh();
    if (!Detour || Mesh.SupportsRuntimeGeneration())
        return false;
    const TArray<uint8> Bytes = MeshInOrder(*Detour, Version, Order);
    FMemoryReader Reader(Bytes, true);
    FNavigationMeshAccess::Load(Mesh, Reader, Version);
    return Mesh.GetRecastMesh() && AllTiles(*Mesh.GetRecastMesh()).Num() == Order.Num();
}

// The mesh as a cook saves it (FPImplRecastNavMesh::Serialize run by a commandlet): every tile the actor holds, in the
// order the mesh keeps them. False when the actor holds no mesh, and the engine writes none, or when the mesh is
// generated while the game runs: its save also writes each tile's compressed layers, and the editor's save of such a
// mesh is left as the engine wrote it.
bool CommandletMesh(const ARecastNavMesh& Mesh, int32 Version, TArray<uint8>& Out)
{
    Out.Reset();
    const dtNavMesh* Detour = Mesh.GetRecastMesh();
    if (!Detour || Mesh.SupportsRuntimeGeneration())
        return false;
    Out = MeshInOrder(*Detour, Version, AllTiles(*Detour));
    return true;
}

// The meshes change the size of their actors, so every object after the first of them in the file moves. Each must
// hold nothing that says where in the file it is, or only what is placed again after the move: the flag every object
// has, the world's and the level's lists of objects, a lighting build's id, a navigation build's mesh, the levels of
// detail of a static mesh component (as for the post-process merge of a level cook), or cooked collision, whose
// inline payloads are placed again. Objects before it stay where they are.
bool MovesSafely(const std::vector<uint8>& Header, const std::vector<uint8>& Body, std::size_t First, FString& Error)
{
    using namespace P4LevelContent;
    FFile File;
    File.Header = Header;
    File.Body = Body;
    if (!Read(File, FString(), Error))
        return false;
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
    {
        const FNode& Node = File.Node(Index);
        if (std::size_t(File.Package.Exports[Index - 1].Offset) <= First)
            continue;
        const bool Moves = Node.Undecoded.IsEmpty() &&
            (Node.PlainTail || Index == File.World || Index == File.Level ||
                Node.EngineTail == TEXT("the id of a lighting build") || Node.EngineTail == NavigationTail ||
                Node.EngineTail == TEXT("cooked collision") || (Node.Lods >= 0 && !File.Instanced.Contains(Index)));
        if (!Moves)
        {
            Error = TEXT("navigation-mesh-cook-relocation-unqualified: ") + Node.Path;
            return false;
        }
    }
    return true;
}
} // namespace

namespace
{
// The navigation system of a world whose own is off, with nothing of it running but the bounds its navigation bounds
// volumes give: what a navigation mesh's save asks it for. Null when the world runs one.
struct FNavigationSystemAccess : UNavigationSystemV1
{
    static void GatherBounds(UNavigationSystemV1& System)
    {
        (System.*&FNavigationSystemAccess::GatherNavigationBounds)();
    }
};

UNavigationSystemV1* RunBoundsOnly(UWorld* World)
{
    if (!IsValid(World) || World->GetNavigationSystem())
        return nullptr;
    // The class the world's settings name, whose agents say which bounds are whose.
    UClass* Class = UNavigationSystemV1::StaticClass();
    if (const AWorldSettings* Settings = World->GetWorldSettings(false, false))
        if (const UNavigationSystemConfig* Config = Settings->GetNavigationSystemConfig())
            if (UClass* Named = Config->NavigationSystemClass.TryLoadClass<UNavigationSystemV1>())
                if (!Named->HasAnyClassFlags(CLASS_Abstract))
                    Class = Named;
    UNavigationSystemV1* System = NewObject<UNavigationSystemV1>(World, Class, NAME_None, RF_Transient);
    World->SetNavigationSystem(System);
    FNavigationSystemAccess::GatherBounds(*System);
    return System;
}

void StopBoundsOnly(UWorld* World, UNavigationSystemV1* System)
{
    if (!System)
        return;
    System->CleanUp(FNavigationSystem::ECleanupMode::CleanupUnsafe);
    if (IsValid(World) && World->GetNavigationSystem() == System)
        World->SetNavigationSystem(nullptr);
    System->MarkPendingKill();
}

// The order of the game's editor: an editor save with its navigation system running writes the tiles inside the
// level's navigation bounds, cell by cell in the order of those bounds (FPImplRecastNavMesh::GetNavMeshTilesIn), and
// the tiles of a cell as Detour finds them there, the last added first. Its maps' meshes were loaded from such saves,
// with each cell's tiles last added in the mesh's own order: the tiles of a cell from the last in the mesh to the
// first. Up gives each cell's tiles from the first instead. Only tiles inside the bounds the world's navigation system
// gives the mesh are in it.
TArray<int32> BoundsOrder(const ARecastNavMesh& Mesh, bool Up)
{
    TArray<int32> Order;
    const dtNavMesh* Detour = Mesh.GetRecastMesh();
    if (!Detour)
        return Order;
    const float* Origin = Detour->getParams()->orig;
    const float Size = Detour->getParams()->tileWidth;
    TSet<FIntPoint> Cells;
    for (const FBox& Box : Mesh.GetNavigableBoundsInLevel(Mesh.GetLevel()))
    {
        const FBox Rc = Unreal2RecastBox(Box);
        for (int32 Y = FMath::FloorToInt((Rc.Min.Z - Origin[2]) / Size);
             Y <= FMath::FloorToInt((Rc.Max.Z - Origin[2]) / Size); ++Y)
            for (int32 X = FMath::FloorToInt((Rc.Min.X - Origin[0]) / Size);
                 X <= FMath::FloorToInt((Rc.Max.X - Origin[0]) / Size); ++X)
                Cells.Add(FIntPoint(X, Y));
    }
    for (const FIntPoint& Cell : Cells)
    {
        const int32 Count = Detour->getTileCountAt(Cell.X, Cell.Y);
        if (Count <= 0)
            continue;
        TArray<const dtMeshTile*> Found;
        Found.SetNumZeroed(Count);
        const int32 Got = Detour->getTilesAt(Cell.X, Cell.Y, Found.GetData(), Count);
        TArray<int32> Here;
        for (int32 At = 0; At < Got; ++At)
            if (Found[At] && Found[At]->header && Found[At]->dataSize > 0 && Detour->getTileRef(Found[At]))
                Here.AddUnique(int32(Detour->decodePolyIdTile(Detour->getTileRef(Found[At]))));
        Here.Sort();
        if (!Up)
            Algo::Reverse(Here);
        for (const int32 Index : Here)
            Order.AddUnique(Index);
    }
    return Order;
}

// A mesh given its saved order for a save, with the order to link it back in after.
struct FRelinked
{
    TWeakObjectPtr<ARecastNavMesh> Mesh;
    TArray<int32> Order;
};
// The worlds being saved: the bounds-only system made for the save (null when the world runs its own) and the meshes.
struct FSaving
{
    TWeakObjectPtr<UNavigationSystemV1> Made;
    TArray<FRelinked> Meshes;
};
TMap<TWeakObjectPtr<UWorld>, FSaving> GSaving;
} // namespace

int32 P4HoldOriginalNavigationMeshes(UWorld* Owner, const P4LevelContent::FFile& Game)
{
    using namespace P4LevelContent;
    if (!IsInGameThread() || !IsValid(Owner) || !Owner->PersistentLevel)
        return 0;
    UPackage* Package = Owner->GetOutermost();
    TArray<TPair<ARecastNavMesh*, int32>> Loaded;
    for (const int32 Index : Game.Actors)
    {
        const FNode& Node = Game.Node(Index);
        if (Node.EngineTail != NavigationTail)
            continue;
        ARecastNavMesh* Mesh = FindObject<ARecastNavMesh>(Package, *Node.Path);
        uint32 Version = 0;
        const uint8* Data = nullptr;
        int64 Size = 0;
        if (!IsValid(Mesh) ||
            !ReadTail(Game.Body.data() + Node.TailStart, int64(Node.TailEnd - Node.TailStart), Version, Data, Size))
            continue;
        // As ARecastNavMesh::Serialize loads it: a mesh of another version is left for a build, an empty one is none.
        if (Version < NAVMESHVER_MIN_COMPATIBLE || Version > NAVMESHVER_LATEST || Size <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("%s keeps no navigation tiles: the game's mesh is of version %u"),
                *Mesh->GetPathName(), Version);
            continue;
        }
        TArray<uint8> Bytes(Data, int32(Size));
        FMemoryReader Reader(Bytes, true);
        FNavigationMeshAccess::Load(*Mesh, Reader, int32(Version));
        if (Reader.IsError() || Reader.Tell() != Bytes.Num() || !Mesh->GetRecastMesh())
        {
            UE_LOG(LogTemp, Warning, TEXT("%s keeps no navigation tiles: the game's mesh did not load (%lld of %d bytes)"),
                *Mesh->GetPathName(), Reader.Tell(), Bytes.Num());
            continue;
        }
        Loaded.Emplace(Mesh, int32(Version));
    }
    // The game's file is a cook's, which writes the tiles in the mesh's own order. Its editor held them as it loaded
    // them from the map its own save wrote, in the order of the level's navigation bounds (BoundsOrder).
    UNavigationSystemV1* Bounds = Loaded.Num() ? RunBoundsOnly(Owner) : nullptr;
    int32 Held = 0;
    for (const auto& Pair : Loaded)
    {
        ARecastNavMesh* Mesh = Pair.Key;
        if (Mesh->SupportsStreaming() && !Mesh->SupportsRuntimeGeneration())
        {
            const TArray<int32> Order = BoundsOrder(*Mesh, false);
            const int32 Tiles = AllTiles(*Mesh->GetRecastMesh()).Num();
            // Bounds that hold fewer tiles (bounds no agent of the mesh's has) would lose the game's; the mesh then
            // keeps the cook's order.
            if (Order.Num() != Tiles || !Relink(*Mesh, Pair.Value, Order))
                UE_LOG(LogTemp, Warning, TEXT("%s keeps its navigation tiles in the cook's order: its bounds hold %d of %d"),
                    *Mesh->GetPathName(), Order.Num(), Tiles);
        }
#if !UE_BUILD_SHIPPING
        Mesh->RequestDrawingUpdate();
#endif
        Held += Mesh->GetRecastMesh() != nullptr;
    }
    StopBoundsOnly(Owner, Bounds);
    return Held;
}

void P4GiveNavigationBoundsForSave(UWorld* World, bool Saving)
{
    // A mesh's save asks the world it is in, which for a sublevel is the world it is shown in.
    UWorld* Owner = IsValid(World) && World->PersistentLevel && World->PersistentLevel->OwningWorld
        ? World->PersistentLevel->OwningWorld
        : World;
    if (!Saving)
    {
        if (FSaving* Was = GSaving.Find(Owner))
        {
            for (const FRelinked& One : Was->Meshes)
                if (ARecastNavMesh* Mesh = One.Mesh.Get())
                    Relink(*Mesh, NAVMESHVER_LATEST, One.Order);
            StopBoundsOnly(Owner, Was->Made.Get());
        }
        GSaving.Remove(Owner);
        return;
    }
    if (!IsInGameThread() || !IsValid(Owner) || !World->PersistentLevel || GSaving.Contains(Owner))
        return;
    TArray<ARecastNavMesh*> Holding;
    for (AActor* Actor : World->PersistentLevel->Actors)
        if (ARecastNavMesh* Mesh = Cast<ARecastNavMesh>(Actor))
            if (IsValid(Mesh) && Mesh->GetRecastMesh() && Mesh->SupportsStreaming() &&
                !Mesh->SupportsRuntimeGeneration())
                Holding.Add(Mesh);
    if (!Holding.Num())
        return;
    FSaving& Now = GSaving.Add(Owner);
    Now.Made = RunBoundsOnly(Owner);
    // The save writes the order of BoundsOrder when each cell's tiles were added from the first: they are, for the
    // save, and linked back after it as the save's next load links them.
    for (ARecastNavMesh* Mesh : Holding)
    {
        TArray<int32> Order = BoundsOrder(*Mesh, false);
        if (Order.Num() == AllTiles(*Mesh->GetRecastMesh()).Num() && Relink(*Mesh, NAVMESHVER_LATEST, BoundsOrder(*Mesh, true)))
            Now.Meshes.Add({Mesh, MoveTemp(Order)});
    }
}

bool P4CookNavigationMeshesAsCommandlet(
    UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    Error.Reset();
    TMap<FString, const ARecastNavMesh*> Meshes;
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package, Objects, true);
        for (UObject* Object : Objects)
            if (const ARecastNavMesh* Mesh = Cast<ARecastNavMesh>(Object))
                if (IsValid(Mesh) && Mesh->GetRecastMesh())
                {
                    FString Path = Mesh->GetPathName(Package);
                    Path.ReplaceInline(TEXT(":"), TEXT("."));
                    Meshes.Add(Path, Mesh);
                }
    }
    if (!Meshes.Num())
        return true;
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Native.Error.Code.c_str());
        return false;
    }
    std::map<std::size_t, std::vector<uint8>> Replacements;
    TMap<std::size_t, TArray<uint8>> Written;
    std::size_t First = SIZE_MAX;
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
    {
        std::string Path;
        if (!Native.Reference(int32(At + 1), Path))
        {
            Error = TEXT("navigation-mesh-cook-export-unnamed");
            return false;
        }
        const ARecastNavMesh* const* Mesh = Meshes.Find(UTF8_TO_TCHAR(Path.c_str()));
        if (!Mesh)
            continue;
        const auto& Export = Native.Exports[At];
        const auto Tags =
            P4MapKit::ReadPropertyEnvelopes(Body, Native.Names, Export.Offset, Export.Offset + Export.Size);
        uint32 Version = 0;
        const uint8* Saved = nullptr;
        int64 SavedSize = 0;
        if (!Tags.Error.Code.empty() ||
            !ReadTail(Body.data() + Tags.End, int64(Export.Offset + Export.Size - Tags.End), Version, Saved, SavedSize))
        {
            Error = TEXT("navigation-mesh-cook-actor-not-read: ") + FString(UTF8_TO_TCHAR(Path.c_str()));
            return false;
        }
        TArray<uint8> Mine;
        if (!CommandletMesh(**Mesh, int32(Version), Mine) ||
            (SavedSize == Mine.Num() && !FMemory::Memcmp(Saved, Mine.GetData(), Mine.Num())))
            continue;
        std::vector<uint8> Rebuilt(Body.begin() + Export.Offset, Body.begin() + Tags.End + 8);
        const uint32 Counted = uint32(Mine.Num() + 4);
        for (int32 Shift = 0; Shift < 32; Shift += 8)
            Rebuilt.push_back(uint8(Counted >> Shift));
        Rebuilt.insert(Rebuilt.end(), Mine.GetData(), Mine.GetData() + Mine.Num());
        Replacements.emplace(At, std::move(Rebuilt));
        Written.Add(At, MoveTemp(Mine));
        First = FMath::Min(First, std::size_t(Export.Offset));
    }
    if (Replacements.empty())
        return true;
    UE_LOG(LogTemp, Display, TEXT("%s: %d navigation meshes written with every tile they hold"), *Package->GetName(),
        int32(Replacements.size()));
    if (!MovesSafely(Header, Body, First, Error))
        return false;
    P4MapKit::Export::RewrittenPackage Candidate;
    std::string Why;
    if (!P4MapKit::Export::RebuildCookedExports(Header, Body, Native.Names, Replacements, Candidate, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    if (!P4RelocatePackageTextureImages(Header, Body, Candidate.Header, Candidate.Body, Error) ||
        !P4PlacePackageInlinePhysicsBulk(Candidate.Header, Candidate.Body, Error))
        return false;
    // Read the actual output again: each mesh counts only when it reads back as written.
    const auto Checked = P4MapKit::ReadPackage(Candidate.Header, Candidate.Body);
    if (!Checked.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Checked.Error.Code.c_str());
        return false;
    }
    for (const auto& Pair : Written)
    {
        const auto& Export = Checked.Exports[Pair.Key];
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Candidate.Body, Checked.Names, Export.Offset, Export.Offset + Export.Size);
        uint32 Version = 0;
        const uint8* Read = nullptr;
        int64 ReadSize = 0;
        if (!Tags.Error.Code.empty() ||
            !ReadTail(Candidate.Body.data() + Tags.End, int64(Export.Offset + Export.Size - Tags.End), Version, Read,
                ReadSize) ||
            ReadSize != Pair.Value.Num() || FMemory::Memcmp(Read, Pair.Value.GetData(), Pair.Value.Num()))
        {
            std::string Path;
            Checked.Reference(int32(Pair.Key + 1), Path);
            Error = TEXT("navigation-mesh-cook-readback-differs: ") + FString(UTF8_TO_TCHAR(Path.c_str()));
            return false;
        }
    }
    Header = std::move(Candidate.Header);
    Body = std::move(Candidate.Body);
    return true;
}

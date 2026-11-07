# Navigation export

The plugin writes a mod's `.nav` file for one game map with one call:

```cpp
#include "navigation_map_export.h"
bool P4MapKit::NavigationExport::ExportMap(const MapExportInput& In, MapExport& Out, Problem& Error);
```

On success `Out.Bytes` is the whole `.nav` file and `Out.Actors` says which game actors were
replaced, from which bake, and why the others were kept. On failure `Out` is unchanged,
`Error.Code` is a stable code, and `Error.Message` is the sentence the Python tools print for the
same case (empty when they have none). It does what `mapkit.convert` does through
`swap_navmesh.swap(keep=True, paint=False)` and `mapkit.nav_bin`, and the host test checks it
byte for byte against them.

## What goes in

`In.Package` is the game map's package, such as `/Game/Carrot4/Maps/Main/Area/Area001/Area001_P`.

`In.Stock` is the game map's own Recast actors, read from its cooked package. Pass the `.umap`
and `.uexp` bytes to `ReadStockMap(Header, Body, In.Stock, Error)`. When the package is already
open, as in `P4LevelFileRead.cpp` where a `RecastNavMesh` actor's tail is found,
`ReadStockActor(Body, Names, ExportStart, ExportEnd, Name, Class, Actor, Error)` reads one export:
its `SupportedAreas` property and the native tail after its tagged properties.
`DecodePayload` and `ReadSupportedAreas` are the two halves on their own. Pass the `_P` map: a
sublevel holds no Recast actors, so a sublevel edit is exported through its parent map.

`In.Bakes` is the editor's bake, one `BakedActor` for each `ARecastNavMesh` in the level. All of it
comes from UE 4.26 without engine changes. `ARecastNavMesh::GetRecastMesh()` gives the `dtNavMesh`:

- `Mesh.Settings` from `getParams()`: `orig`, `tileWidth`, `tileHeight`, `maxTiles`, `maxPolys`.
- One `BuiltTile` for each `i < getMaxTiles()` whose `getTile(i)->header` is set:
  `Reference = getTileRef(tile)` and `MemorySize = tile->dataSize` (what the cook writes, so the
  bytes match a cooked bake; 0 derives it from the counts).
- `Header` from `dtMeshHeader`: x, y, layer, userId, maxLinkCount, offMeshBase, walkable height,
  radius and climb, bmin, bmax, bvQuantFactor, offMeshSegPolyBase, offMeshSegVertBase.
- The arrays, with the counts the header gives: `verts`, `polys` (area and type from
  `getArea()`/`getType()`), `detailMeshes`, `detailVerts`, `detailTris`, `bvTree` (bmin then bmax
  as `Bounds`, `i` as `Index`), `offMeshCons` with their `height`, `offMeshSeg`, the cluster
  centers and `polyClusters`. Leave `Cache` empty. Links (`dtLink`) are not passed: the game
  makes them again when it adds the tile.
- `Mesh.AreaIds` from the bake's own areas: `GetAreaClass(Id)->GetName()` for each ID in use, which
  gives `NavArea_Water_C` for a Blueprint area. NavArea_Default must be 63 and NavArea_Null 0, as
  UE 4.26 sets them.
- `Name` is the actor's name. It only shows in messages.

`In.HasHeroBake` and `In.HeroBakes`: a map with Olimar-mode actors (their names contain `Hero`)
was baked a second time with its `*_Hero_*` sublevels loaded. Pass that bake here. Without it the
Hero actors take `In.Bakes`, as `swap_navmesh.swap` does; `mapkit.convert` refuses instead, so
the plugin decides which to do.

`In.CarryStockLinks` copies the game's jump and drop links onto the bake. `mapkit.convert`
always does.

## What it does

Each game actor takes the baked actor with the same agent radius, from `HeroBakes` for a Hero
actor. A game actor with no baked radius keeps the game's navigation and is left out of the file.
Two bakes with one radius, settings that differ from the game's (tile size, origin, tile pool,
polygons per tile, agent height and step height) and no match at all are refused with Python's
sentences. The areas and flags become the game's by area name (`Convert`), the game's links are
carried over, and `EncodeBuilt` writes the file.

Not ported, as `mapkit.convert` does not use them: painting the game's areas onto the bake
(`paint=True`), the helper volumes, link edits (`swap_navmesh.edit_links`) and writing the
navigation into a copy of the map. `ReadPackage` refuses a `.uexp` over 64 MiB.

## Test

    py -3.11 modding/mapkit/tests/test_navigation_export.py

It reads private extracts of Cave000_F00_P, Area001_P and Area001_GameArea and skips those cases
with a message when they are not on the machine.

#pragma once
// Plain editor/host boundary. Coordinates are Recast (x, y up, z), not Unreal XYZ.
// No Detour pointers, engine types, package bytes or Python objects cross this API.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace P4MapKit { namespace NavigationExport {
using Vec3 = std::array<float, 3>;
struct TileHeader {
    std::int32_t X=0, Y=0, Layer=0, MaxLinkCount=0, OffMeshBase=0;
    std::uint32_t UserId=0;
    float WalkableHeight=0, WalkableRadius=0, WalkableClimb=0, BvQuantFactor=0;
    Vec3 Min{}, Max{};
    std::int32_t SegmentPolyBase=0, SegmentVertBase=0;
};
struct Poly {
    std::uint32_t FirstLink=UINT32_MAX;
    std::array<std::uint16_t, 6> Verts{}, Neighbors{};
    std::uint16_t Flags=0;
    std::uint8_t VertCount=0, Area=0, Type=0;
};
struct DetailMesh {
    std::uint32_t VertBase=0, TriBase=0;
    std::uint8_t VertCount=0, TriCount=0;
};
struct BvNode { std::array<std::uint16_t, 6> Bounds{}; std::int32_t Index=0; };
struct PointLink {
    std::array<float, 6> Pos{};
    float Radius=0, Height=0;
    std::uint16_t Polygon=0;
    std::uint8_t Flags=0, Side=0;
    std::uint32_t UserId=0;
};
struct SegmentLink {
    Vec3 StartA{}, StartB{}, EndA{}, EndB{};
    float Radius=0;
    std::uint16_t FirstPoly=0;
    std::uint8_t PolyCount=0, Flags=0;
    std::uint32_t UserId=0;
};
struct BuiltTile {
    std::uint64_t Reference=0;
    // Zero derives the declared size as nav_encode.layout_size does. A positive
    // editor-supplied value is retained, just as nav_encode.encode retains it.
    std::int32_t MemorySize=0;
    TileHeader Header;
    std::vector<Vec3> Verts, DetailVerts, Clusters;
    std::vector<Poly> Polys;
    std::vector<DetailMesh> DetailMeshes;
    std::vector<std::array<std::uint8_t, 4>> DetailTris;
    std::vector<BvNode> BvTree;
    std::vector<PointLink> PointLinks;
    std::vector<SegmentLink> SegmentLinks;
    std::vector<std::uint16_t> PolyClusters;
    // A stock tile's compressed tile cache, kept so a decoded payload encodes to the same bytes.
    // The editor leaves it empty, and a .nav file refuses a tile that has one.
    std::vector<std::uint8_t> Cache;
    // Runtime dtLink records are deliberately absent: addTile recreates them.
};
struct MapSettings {
    Vec3 Origin{};
    float TileWidth=0, TileHeight=0;
    std::int32_t MaxTiles=0, MaxPolys=0;
};
using Areas = std::map<std::string, std::uint8_t>;
struct BuiltMesh { MapSettings Settings; std::vector<BuiltTile> Tiles; Areas AreaIds; };
}}

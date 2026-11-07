// Read the game's UE4.26 Recast native tail without interpreting it as editable tagged data.
// Serialized links are absent; max_link_count describes the allocation the engine recreates.
#pragma once
#include "package_reader.h"
#include <cmath>
#include <set>
#include <tuple>

namespace P4MapKit
{
namespace Navigation
{
struct Span { std::size_t Offset = 0, Size = 0; };
struct Tile
{
    std::uint64_t Reference = 0;
    std::int32_t MemorySize = 0, X = 0, Y = 0, Layer = 0, GroundPolys = 0;
    // vertices, polygons, links, detail meshes/vertices/triangles, BV nodes, point links,
    // segment links, clusters; the wire repeats these counts in its Detour header.
    std::array<std::size_t, 10> Counts{};
    Span Wire, Header, Vertices, Polygons, Detail, Bounds, PointLinks, SegmentLinks, Clusters, Cache;
};
struct Payload
{
    std::int32_t Version = 0, MaxTiles = 0, MaxPolys = 0;
    std::array<float, 3> Origin{};
    float TileWidth = 0, TileHeight = 0;
    std::vector<Tile> Tiles;
};
inline bool Real(FByteReader& Reader, float& Value)
{
    std::uint32_t Bits = 0;
    if (!Reader.U32(Bits)) return false;
    std::memcpy(&Value, &Bits, 4);
    return std::isfinite(Value) || Reader.Reject("navigation-nonfinite");
}
inline bool Reals(FByteReader& Reader, std::size_t Count)
{
    if (Count > (Reader.Limit() - Reader.Tell()) / 4) return Reader.Reject("truncated");
    float Value = 0;
    for (std::size_t Index = 0; Index < Count; ++Index) if (!Real(Reader, Value)) return false;
    return true;
}
inline bool Records(FByteReader& Reader, std::size_t Count, std::size_t Width)
{
    if (Count > (Reader.Limit() - Reader.Tell()) / Width) return Reader.Reject("truncated");
    return Reader.Skip(Count * Width);
}
inline Span Since(const FByteReader& Reader, std::size_t At) { return {At, Reader.Tell() - At}; }
inline bool ReadTile(FByteReader& Reader, const std::vector<std::uint8_t>& Bytes, Tile& Out)
{
    const auto Start = Reader.Tell();
    if (!Reader.Unsigned(8, Out.Reference) || !Reader.I32(Out.MemorySize)) return false;
    if (Out.Reference == UINT64_MAX || Out.MemorySize <= 0) return Reader.Reject("navigation-empty-tile");
    for (auto& Count : Out.Counts) if (!Reader.Count(Count, 1000000)) return false;
    const auto HeaderAt = Reader.Tell();
    std::array<std::int32_t, 15> Header{};
    for (auto& Value : Header) if (!Reader.I32(Value)) return false;
    if (Header[0] != 0x444e4156 || Header[1] != 7) return Reader.Reject("navigation-detour-version");
    Out.X = Header[2]; Out.Y = Header[3]; Out.Layer = Header[4]; Out.GroundPolys = Header[14];
    const std::array<std::size_t, 8> Order{{1, 0, 2, 3, 4, 5, 6, 7}};
    for (std::size_t Index = 0; Index < Order.size(); ++Index)
        if (Header[6 + Index] < 0 || std::size_t(Header[6 + Index]) != Out.Counts[Order[Index]])
            return Reader.Reject("navigation-count-mismatch");
    if (Out.GroundPolys < 0 || std::size_t(Out.GroundPolys) > Out.Counts[1])
        return Reader.Reject("navigation-ground-count");
    if (!Reals(Reader, 10)) return false;
    std::array<std::int32_t, 4> ClusterHeader{};
    for (auto& Value : ClusterHeader) if (!Reader.I32(Value)) return false;
    if (ClusterHeader[0] < 0 || ClusterHeader[1] < 0 || std::size_t(ClusterHeader[0]) != Out.Counts[9] ||
        std::size_t(ClusterHeader[1]) != Out.Counts[8]) return Reader.Reject("navigation-count-mismatch");
    Out.Header = Since(Reader, HeaderAt);
    auto At = Reader.Tell();
    if (!Reals(Reader, Out.Counts[0] * 3)) return false;
    Out.Vertices = Since(Reader, At); At = Reader.Tell();
    for (std::size_t Poly = 0; Poly < Out.Counts[1]; ++Poly)
    {
        const auto PolyAt = Reader.Tell();
        if (!Reader.Skip(32)) return false;
        const auto Count = Bytes[PolyAt + 30];
        if (Count > 6) return Reader.Reject("navigation-polygon-size");
        for (std::size_t Vertex = 0; Vertex < Count; ++Vertex)
        {
            const auto P = PolyAt + 4 + Vertex * 2;
            const auto Index = std::uint32_t(Bytes[P]) | std::uint32_t(Bytes[P + 1]) << 8;
            if (Index >= Out.Counts[0]) return Reader.Reject("navigation-polygon-vertex");
        }
    }
    Out.Polygons = Since(Reader, At); At = Reader.Tell();
    if (!Records(Reader, Out.Counts[3], 10) || !Reals(Reader, Out.Counts[4] * 3) ||
        !Records(Reader, Out.Counts[5], 4)) return false;
    Out.Detail = Since(Reader, At); At = Reader.Tell();
    if (!Records(Reader, Out.Counts[6], 16)) return false;
    Out.Bounds = Since(Reader, At); At = Reader.Tell();
    for (std::size_t Index = 0; Index < Out.Counts[7]; ++Index)
        if (!Reals(Reader, 7) || !Reader.Skip(8)) return false;
    if (!Reals(Reader, Out.Counts[7])) return false;
    Out.PointLinks = Since(Reader, At); At = Reader.Tell();
    for (std::size_t Index = 0; Index < Out.Counts[8]; ++Index)
        if (!Reals(Reader, 13) || !Reader.Skip(8)) return false;
    Out.SegmentLinks = Since(Reader, At); At = Reader.Tell();
    if (!Reals(Reader, Out.Counts[9] * 3) || !Records(Reader, Out.Counts[3], 2)) return false;
    Out.Clusters = Since(Reader, At);
    std::size_t CacheSize = 0;
    if (!Reader.Count(CacheSize, 64 * 1024 * 1024)) return false;
    At = Reader.Tell();
    if (!Reader.Skip(CacheSize)) return false;
    Out.Cache = Since(Reader, At); Out.Wire = Since(Reader, Start);
    return true;
}
inline bool Read(const std::vector<std::uint8_t>& Bytes, Payload& Out, FReadError& Error)
{
    FByteReader Reader(Bytes); Payload Result;
    const auto Parse = [&]() {
        std::int32_t Guid = 0; std::uint32_t Size = 0; std::size_t Count = 0;
        if (!Reader.I32(Guid) || !Reader.I32(Result.Version) || !Reader.U32(Size)) return false;
        if (Guid != 0) return Reader.Reject("navigation-object-guid");
        if (Result.Version != 13) return Reader.Reject("navigation-version");
        if (Bytes.size() < 8 || Size != Bytes.size() - 8) return Reader.Reject("navigation-size");
        if (!Reader.Count(Count, 100000)) return false;
        for (auto& Value : Result.Origin) if (!Real(Reader, Value)) return false;
        if (!Real(Reader, Result.TileWidth) || !Real(Reader, Result.TileHeight) ||
            !Reader.I32(Result.MaxTiles) || !Reader.I32(Result.MaxPolys)) return false;
        if (Result.TileWidth <= 0 || Result.TileHeight <= 0 || Result.MaxTiles <= 0 ||
            Result.MaxTiles > 65536 || Count > std::size_t(Result.MaxTiles) || Result.MaxPolys <= 0)
            return Reader.Reject("navigation-parameters");
        std::set<std::uint64_t> References;
        std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> Coordinates;
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            Tile Next;
            if (!ReadTile(Reader, Bytes, Next)) return false;
            if (!References.insert(Next.Reference).second ||
                !Coordinates.insert(std::make_tuple(Next.X, Next.Y, Next.Layer)).second)
                return Reader.Reject("navigation-duplicate-tile");
            Result.Tiles.push_back(std::move(Next));
        }
        return Reader.Tell() == Reader.Limit() || Reader.Reject("navigation-trailing-bytes");
    };
    if (!Parse()) { Error = Reader.Error; return false; }
    Out = std::move(Result); Error = {}; return true;
}
inline bool EqualSpan(const std::vector<std::uint8_t>& A, Span AS,
    const std::vector<std::uint8_t>& B, Span BS)
{
    return AS.Size == BS.Size && AS.Offset <= A.size() && AS.Size <= A.size() - AS.Offset &&
        BS.Offset <= B.size() && BS.Size <= B.size() - BS.Offset &&
        std::equal(A.begin() + AS.Offset, A.begin() + AS.Offset + AS.Size, B.begin() + BS.Offset);
}
}
}

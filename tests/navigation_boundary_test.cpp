#include "core/navigation_boundary.h"
#include <fstream>
#include <iostream>
using namespace P4MapKit;
using namespace P4MapKit::Navigation;
#define CHECK(X) do { if (!(X)) { std::cerr << "Failed at " << __LINE__ << ": " << #X << '\n'; return 1; } } while (false)
static void Integer(std::vector<std::uint8_t>& B, std::uint64_t Value, std::size_t Width = 4)
{ for (std::size_t I = 0; I < Width; ++I) B.push_back(std::uint8_t(Value >> (I * 8))); }
static void Put(std::vector<std::uint8_t>& B, std::size_t At, std::uint32_t Value)
{ for (std::size_t I = 0; I < 4; ++I) B.at(At + I) = std::uint8_t(Value >> (I * 8)); }
static void Float(std::vector<std::uint8_t>& B, float Value)
{ std::uint32_t Bits = 0; std::memcpy(&Bits, &Value, 4); Integer(B, Bits); }
static std::vector<std::uint8_t> Fixture()
{
    std::vector<std::uint8_t> B;
    for (auto V : {0, 13, 0, 1}) Integer(B, V);
    for (float V : {0.f, 0.f, 0.f, 100.f, 100.f}) Float(B, V);
    Integer(B, 16); Integer(B, 4096);
    Integer(B, 123, 8); Integer(B, 256);
    for (auto V : {3, 1, 4, 0, 0, 0, 0, 0, 0, 0}) Integer(B, V);
    for (auto V : {0x444e4156, 7, 2, 3, 0, 0, 1, 3, 4, 0, 0, 0, 0, 0, 1}) Integer(B, V);
    for (float V : {50.f, 10.f, 20.f, 0.f, 0.f, 0.f, 100.f, 100.f, 50.f, 1.f}) Float(B, V);
    for (auto V : {0, 0, 0, 0}) Integer(B, V);
    for (float V : {0.f, 0.f, 0.f, 100.f, 0.f, 0.f, 0.f, 0.f, 100.f}) Float(B, V);
    Integer(B, UINT32_MAX);
    for (auto V : {0, 1, 2, 0, 0, 0}) Integer(B, V, 2);
    for (auto V : {0, 0x8000, 0, 0, 0, 0}) Integer(B, V, 2);
    Integer(B, 1, 2); Integer(B, 3, 1); Integer(B, 1, 1);
    Integer(B, 3); B.insert(B.end(), {1, 2, 3});
    Put(B, 8, std::uint32_t(B.size() - 8)); return B;
}
static std::vector<std::uint8_t> File(const char* Path)
{ std::ifstream In(Path, std::ios::binary); return {std::istreambuf_iterator<char>(In), {}}; }
static void Summary(const Payload& P)
{
    std::cout << "{\"version\":" << P.Version << ",\"tile_count\":" << P.Tiles.size()
        << ",\"max_tiles\":" << P.MaxTiles << ",\"max_polys\":" << P.MaxPolys
        << ",\"tile_width\":" << P.TileWidth << ",\"tile_height\":" << P.TileHeight << ",\"tiles\":[";
    bool First = true;
    for (const auto& T : P.Tiles)
    {
        if (!First) std::cout << ','; First = false;
        std::cout << "{\"x\":" << T.X << ",\"y\":" << T.Y << ",\"layer\":" << T.Layer
            << ",\"reference\":" << T.Reference << ",\"ground_polys\":" << T.GroundPolys << ",\"counts\":[";
        for (std::size_t I = 0; I < T.Counts.size(); ++I) { if (I) std::cout << ','; std::cout << T.Counts[I]; }
        std::cout << "],\"bytes\":" << T.Wire.Size << ",\"cache_bytes\":" << T.Cache.Size << '}';
    }
    std::cout << "]}\n";
}
static int Compare(const char* Left, const char* Right)
{
    const auto A = File(Left), B = File(Right); Payload AP, BP; FReadError Error;
    if (!Read(A, AP, Error) || !Read(B, BP, Error))
    { std::cerr << Error.Code << " at " << Error.Offset << '\n'; return 1; }
    const bool Exact = A == B;
    std::cout << "{\"payload_equal\":" << (Exact ? "true" : "false")
        << ",\"parameters_equal\":" << (EqualSpan(A, {16, 28}, B, {16, 28}) ? "true" : "false")
        << ",\"source_tiles\":" << AP.Tiles.size() << ",\"built_tiles\":" << BP.Tiles.size() << ",\"tiles\":[";
    bool First = true;
    for (const auto& T : AP.Tiles)
    {
        if (!First) std::cout << ','; First = false;
        const Tile* Found = nullptr;
        for (const auto& Candidate : BP.Tiles)
            if (std::tie(Candidate.X, Candidate.Y, Candidate.Layer) == std::tie(T.X, T.Y, T.Layer)) { Found = &Candidate; break; }
        std::cout << "{\"x\":" << T.X << ",\"y\":" << T.Y << ",\"layer\":" << T.Layer
            << ",\"present\":" << (Found ? "true" : "false");
        if (Found)
        {
            const char* Names[] = {"wire", "header", "vertices", "polygons", "detail", "bounds", "point_links", "segment_links", "clusters", "cache"};
            const Span Tile::* Parts[] = {&Tile::Wire, &Tile::Header, &Tile::Vertices, &Tile::Polygons, &Tile::Detail,
                &Tile::Bounds, &Tile::PointLinks, &Tile::SegmentLinks, &Tile::Clusters, &Tile::Cache};
            std::cout << ",\"different_sections\":["; bool FirstPart = true;
            for (std::size_t I = 0; I < 10; ++I)
                if (!EqualSpan(A, T.*Parts[I], B, Found->*Parts[I]))
                { if (!FirstPart) std::cout << ','; FirstPart = false; std::cout << '"' << Names[I] << '"'; }
            std::cout << ']';
        }
        std::cout << '}';
    }
    std::cout << "]}\n"; return 0;
}
int main(int Count, char** Args)
{
    if (Count == 4 && std::string(Args[1]) == "--compare") return Compare(Args[2], Args[3]);
    if (Count == 3 && std::string(Args[1]) == "--inspect")
    {
        Payload P; FReadError Error; const auto B = File(Args[2]);
        if (!Read(B, P, Error)) { std::cerr << Error.Code << " at " << Error.Offset << '\n'; return 1; }
        Summary(P); return 0;
    }
    const auto Good = Fixture(); Payload P; FReadError Error;
    CHECK(Read(Good, P, Error)); CHECK(P.Tiles.size() == 1);
    CHECK(P.Tiles[0].Counts[0] == 3 && P.Tiles[0].Counts[1] == 1 && P.Tiles[0].GroundPolys == 1);
    CHECK(P.Tiles[0].Wire.Offset == 44 && P.Tiles[0].Cache.Size == 3);
    CHECK(EqualSpan(Good, P.Tiles[0].Wire, Good, P.Tiles[0].Wire));
    auto Changed = Good; Changed.back() = 4;
    CHECK(!EqualSpan(Good, P.Tiles[0].Wire, Changed, P.Tiles[0].Wire));
    // Every shortened payload is refused without replacing the previous decoded result.
    for (std::size_t I = 0; I < Good.size(); ++I)
    {
        auto B = Good; B.resize(I); if (I >= 12) Put(B, 8, std::uint32_t(I - 8));
        CHECK(!Read(B, P, Error)); CHECK(P.Tiles.size() == 1 && P.Tiles[0].Reference == 123);
    }
    auto Bad = Good; Put(Bad, 4, 12); CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-version");
    Bad = Good; Put(Bad, 0, 1); CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-object-guid");
    Bad = Good; Put(Bad, 16, 0x7f800000); CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-nonfinite");
    Bad = Good; Put(Bad, 44 + 12 + 40 + 24, 2); CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-count-mismatch");
    Bad = Good; Bad[P.Tiles[0].Polygons.Offset + 4] = 3;
    CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-polygon-vertex");
    Bad = Good; Bad[P.Tiles[0].Polygons.Offset + 30] = 7;
    CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-polygon-size");
    Bad = Good; Bad.insert(Bad.end(), Good.begin() + 44, Good.end()); Put(Bad, 12, 2); Put(Bad, 8, std::uint32_t(Bad.size() - 8));
    CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-duplicate-tile");
    Bad = Good; Bad.push_back(0); Put(Bad, 8, std::uint32_t(Bad.size() - 8));
    CHECK(!Read(Bad, P, Error)); CHECK(Error.Code == "navigation-trailing-bytes");
    std::cout << "Native navigation boundary checks passed\n";
}

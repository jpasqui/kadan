#pragma once
#include "navigation_data.h"
#include "navigation_boundary.h"
#include "export_contract.h"
#include <limits>

namespace P4MapKit { namespace NavigationExport {
using Meshes = std::map<std::string, std::vector<std::uint8_t>>;
inline void Integer(std::vector<std::uint8_t>& Bytes, std::uint64_t Value, unsigned Width=4) {
    for (unsigned I=0; I<Width; ++I) Bytes.push_back(std::uint8_t(Value>>(I*8)));
}
inline void Real(std::vector<std::uint8_t>& Bytes, float Value) {
    std::uint32_t Bits=0; std::memcpy(&Bits, &Value, 4); Integer(Bytes, Bits);
}
template<class T, std::size_t N> inline void Reals(std::vector<std::uint8_t>& B, const std::array<T,N>& V) {
    for (auto X:V) Real(B, X);
}
inline std::uint64_t Align(std::uint64_t N) { return (N+3)&~UINT64_C(3); }
inline std::uint64_t TileMemory(const Navigation::Tile& T) {
    const auto& C=T.Counts;
    return 116+Align(12*C[0])+Align(32*C[1])+Align(16*C[2])+Align(12*C[3])+
        Align(12*C[4])+Align(4*C[5])+Align(16*C[6])+Align(40*C[7])+Align(64*C[8])+
        Align(20*C[9])+Align(2*C[3]);
}
inline std::uint64_t PythonMemory(const BuiltTile& T) {
    // Python's declared segment allocation is 60, the loader's actual allocation
    // is 64. Preserve Python bytes; the loader derives its allocation from counts.
    return 116+Align(12*T.Verts.size())+Align(32*T.Polys.size())+Align(16ull*T.Header.MaxLinkCount)+
        Align(12*T.DetailMeshes.size())+Align(12*T.DetailVerts.size())+Align(4*T.DetailTris.size())+
        Align(16*T.BvTree.size())+Align(40*T.PointLinks.size())+Align(60*T.SegmentLinks.size())+
        Align(20*T.Clusters.size())+Align(2*T.PolyClusters.size());
}
inline bool Encode(const std::string& Package, const Meshes& Payloads,
    std::vector<std::uint8_t>& Out, std::string& Error) {
    const std::uint64_t Limit=64*1024*1024;
    if (!Export::PackageName(Package) || Package.size()>255 || Payloads.empty() || Payloads.size()>64) {
        Error="navigation-export-map-or-roster-invalid"; return false;
    }
    std::set<std::string> Seen; std::uint64_t Size=12+2+Package.size()+4;
    for (const auto& Mesh:Payloads) {
        std::string Folded=Mesh.first;
        for (char& C:Folded) if (C>='A' && C<='Z') C+=char('a'-'A');
        if (!Export::Plain(Mesh.first) || !Seen.insert(Folded).second || Mesh.second.size()>Limit) {
            Error="navigation-export-actor-identity-invalid"; return false;
        }
        Size+=2+Mesh.first.size()+4+Mesh.second.size();
        if (Size>Limit) { Error="navigation-export-byte-budget"; return false; }
        Navigation::Payload P; FReadError Why;
        if (!Navigation::Read(Mesh.second, P, Why)) { Error=Why.Code; return false; }
        if (P.Tiles.empty() || P.Tiles.size()>4096) { Error="navigation-export-tile-budget"; return false; }
        for (const auto& T:P.Tiles) {
            if (T.Cache.Size) { Error="navigation-export-cache-unsupported"; return false; }
            if (TileMemory(T)>Limit) { Error="navigation-export-tile-memory-budget"; return false; }
        }
    }
    const char Magic[]="P4MNAVM1";
    std::vector<std::uint8_t> B(Magic, Magic+8); B.reserve(std::size_t(Size));
    Integer(B, 1); Integer(B, Package.size(), 2); B.insert(B.end(), Package.begin(), Package.end());
    Integer(B, Payloads.size());
    for (const auto& M:Payloads) {
        Integer(B, M.first.size(), 2); B.insert(B.end(), M.first.begin(), M.first.end());
        Integer(B, M.second.size()); B.insert(B.end(), M.second.begin(), M.second.end());
    }
    Out=std::move(B); Error.clear(); return true;
}
template<std::size_t N> inline bool Finite(const std::array<float,N>& V) {
    for (auto X:V) if (!std::isfinite(X)) return false;
    return true;
}
// Every check EncodePayload and the boundary reader make, without writing any bytes.
// Bytes, when given, receives the payload size EncodePayload writes.
inline bool ValidatePayload(const MapSettings& Settings, const std::vector<BuiltTile>& Tiles,
    std::string& Error, std::uint64_t* Bytes=nullptr) {
    const auto Fail=[&](const char* Why) { Error=Why; return false; };
    if (Tiles.empty() || Tiles.size()>4096 || Settings.MaxTiles<1 || Settings.MaxTiles>65536 ||
        Tiles.size()>std::size_t(Settings.MaxTiles) || Settings.MaxPolys<1 || !Finite(Settings.Origin) ||
        !std::isfinite(Settings.TileWidth) || !std::isfinite(Settings.TileHeight) ||
        !(Settings.TileWidth>0) || !(Settings.TileHeight>0)) return Fail("navigation-export-parameters");
    std::uint64_t Size=44;
    std::set<std::uint64_t> References;
    std::set<std::tuple<std::int32_t,std::int32_t,std::int32_t>> Places;
    for (const auto& T:Tiles) {
        const auto& H=T.Header;
        const std::size_t Counts[]={T.Verts.size(), T.Polys.size(), T.DetailMeshes.size(), T.DetailVerts.size(),
            T.DetailTris.size(), T.BvTree.size(), T.PointLinks.size(), T.SegmentLinks.size(), T.Clusters.size()};
        for (auto C:Counts) if (C>1000000) return Fail("navigation-export-count");
        if (H.MaxLinkCount<0 || H.MaxLinkCount>1000000 || T.MemorySize<0 || T.Reference==UINT64_MAX ||
            T.Polys.size()>std::size_t(Settings.MaxPolys) || T.PolyClusters.size()!=T.DetailMeshes.size() ||
            H.OffMeshBase<0 || std::size_t(H.OffMeshBase)>T.Polys.size() || T.Cache.size()>64*1024*1024)
            return Fail("navigation-export-tile");
        if (!References.insert(T.Reference).second || !Places.insert(std::make_tuple(H.X,H.Y,H.Layer)).second)
            return Fail("navigation-export-duplicate-tile");
        bool Numbers=std::isfinite(H.WalkableHeight) && std::isfinite(H.WalkableRadius) &&
            std::isfinite(H.WalkableClimb) && std::isfinite(H.BvQuantFactor) && Finite(H.Min) && Finite(H.Max);
        for (const auto* List:{&T.Verts,&T.DetailVerts,&T.Clusters}) for (const auto& V:*List) Numbers=Numbers && Finite(V);
        for (const auto& C:T.PointLinks) Numbers=Numbers && Finite(C.Pos) && std::isfinite(C.Radius) && std::isfinite(C.Height);
        for (const auto& S:T.SegmentLinks) Numbers=Numbers && Finite(S.StartA) && Finite(S.StartB) && Finite(S.EndA) &&
            Finite(S.EndB) && std::isfinite(S.Radius);
        if (!Numbers) return Fail("navigation-export-nonfinite");
        for (const auto& P:T.Polys) {
            if (P.Area>=64 || P.Type>=4 || P.VertCount>6) return Fail("navigation-export-polygon");
            for (unsigned I=0; I<P.VertCount; ++I)
                if (P.Verts[I]>=T.Verts.size()) return Fail("navigation-export-polygon-vertex");
        }
        for (const auto& D:T.DetailMeshes)
            if (D.VertBase>T.DetailVerts.size() || D.VertCount>T.DetailVerts.size()-D.VertBase ||
                D.TriBase>T.DetailTris.size() || D.TriCount>T.DetailTris.size()-D.TriBase)
                return Fail("navigation-export-detail-range");
        // A leaf names a polygon, an inner node skips -Index nodes and stays inside the tree.
        for (std::size_t N=0; N<T.BvTree.size(); ++N) {
            const std::int64_t Index=T.BvTree[N].Index;
            if (Index>=0 ? std::uint64_t(Index)>=T.Polys.size() : std::uint64_t(-Index)>T.BvTree.size()-N)
                return Fail("navigation-export-bv-node");
        }
        for (const auto& C:T.PointLinks) if (C.Polygon>=T.Polys.size()) return Fail("navigation-export-link-polygon");
        // A segment's polygons start at the header's segment polygon base plus FirstPoly.
        for (const auto& S:T.SegmentLinks)
            if (H.SegmentPolyBase<0 || std::uint64_t(H.SegmentPolyBase)+S.FirstPoly+S.PolyCount>T.Polys.size())
                return Fail("navigation-export-segment-polygon");
        // As the stock reader (inspect_map_navigation.tile) checks: a cluster index or 0xffff.
        if (!T.Clusters.empty())
            for (auto C:T.PolyClusters) if (C!=0xffff && C>=T.Clusters.size()) return Fail("navigation-export-poly-cluster");
        if (PythonMemory(T)>64*1024*1024) return Fail("navigation-export-tile-memory-budget");
        Size+=12+40+116+12*T.Verts.size()+32*T.Polys.size()+10*T.DetailMeshes.size()+
            12*T.DetailVerts.size()+4*T.DetailTris.size()+16*T.BvTree.size()+40*T.PointLinks.size()+
            60*T.SegmentLinks.size()+12*T.Clusters.size()+2*T.PolyClusters.size()+4+T.Cache.size();
        if (Size>64*1024*1024) return Fail("navigation-export-byte-budget");
    }
    if (Bytes) *Bytes=Size;
    Error.clear(); return true;
}
inline bool EncodePayload(const MapSettings& Settings, const std::vector<BuiltTile>& Tiles,
    std::vector<std::uint8_t>& Out, std::string& Error) {
    std::uint64_t Size=0;
    if (!ValidatePayload(Settings,Tiles,Error,&Size)) return false;
    std::vector<std::uint8_t> B; B.reserve(std::size_t(Size));
    Integer(B, 0); Integer(B, 13); Integer(B, Size-8); Integer(B, Tiles.size());
    Reals(B, Settings.Origin); Real(B, Settings.TileWidth); Real(B, Settings.TileHeight);
    Integer(B, Settings.MaxTiles); Integer(B, Settings.MaxPolys);
    for (const auto& T:Tiles) {
        const auto& H=T.Header;
        Integer(B, T.Reference, 8); Integer(B, T.MemorySize ? T.MemorySize : PythonMemory(T));
        const std::uint64_t Counts[]={T.Verts.size(), T.Polys.size(), std::uint64_t(H.MaxLinkCount),
            T.DetailMeshes.size(), T.DetailVerts.size(), T.DetailTris.size(), T.BvTree.size(),
            T.PointLinks.size(), T.SegmentLinks.size(), T.Clusters.size()};
        for (auto C:Counts) Integer(B, C);
        Integer(B, 0x444e4156); Integer(B, 7);
        Integer(B, H.X); Integer(B, H.Y); Integer(B, H.Layer); Integer(B, H.UserId);
        for (auto I:{1,0,2,3,4,5,6,7}) Integer(B, Counts[I]);
        Integer(B, H.OffMeshBase);
        Real(B, H.WalkableHeight); Real(B, H.WalkableRadius); Real(B, H.WalkableClimb);
        Reals(B, H.Min); Reals(B, H.Max); Real(B, H.BvQuantFactor);
        Integer(B, T.Clusters.size()); Integer(B, T.SegmentLinks.size());
        Integer(B, H.SegmentPolyBase); Integer(B, H.SegmentVertBase);
        for (const auto& V:T.Verts) Reals(B, V);
        for (const auto& P:T.Polys) {
            Integer(B, P.FirstLink); for (auto V:P.Verts) Integer(B, V, 2);
            for (auto N:P.Neighbors) Integer(B, N, 2);
            Integer(B, P.Flags, 2); Integer(B, P.VertCount, 1); Integer(B, P.Area | P.Type<<6, 1);
        }
        for (const auto& D:T.DetailMeshes) {
            Integer(B, D.VertBase); Integer(B, D.TriBase); Integer(B, D.VertCount, 1); Integer(B, D.TriCount, 1);
        }
        for (const auto& V:T.DetailVerts) Reals(B, V);
        for (const auto& Tri:T.DetailTris) for (auto V:Tri) Integer(B, V, 1);
        for (const auto& N:T.BvTree) { for (auto V:N.Bounds) Integer(B, V, 2); Integer(B, N.Index); }
        for (const auto& C:T.PointLinks) {
            Reals(B, C.Pos); Real(B, C.Radius); Integer(B, C.Polygon, 2);
            Integer(B, C.Flags, 1); Integer(B, C.Side, 1); Integer(B, C.UserId);
        }
        for (const auto& C:T.PointLinks) Real(B, C.Height);
        for (const auto& S:T.SegmentLinks) {
            Reals(B, S.StartA); Reals(B, S.StartB); Reals(B, S.EndA); Reals(B, S.EndB); Real(B, S.Radius);
            Integer(B, S.FirstPoly, 2); Integer(B, S.PolyCount, 1); Integer(B, S.Flags, 1); Integer(B, S.UserId);
        }
        for (const auto& C:T.Clusters) Reals(B, C);
        for (auto C:T.PolyClusters) Integer(B, C, 2);
        // The compressed tile cache: empty for editor tiles, as the mod loader requires.
        Integer(B, T.Cache.size()); B.insert(B.end(), T.Cache.begin(), T.Cache.end());
    }
    Navigation::Payload P; FReadError Why;
    if (!Navigation::Read(B, P, Why)) { Error=Why.Code; return false; }
    Out=std::move(B); Error.clear(); return true;
}
// Final editor-built tiles with the target actor's areas/flags and links already applied.
// Returns the complete file bytes; filesystem publication belongs to the plugin.
inline bool EncodeBuilt(const std::string& Package, const std::map<std::string,BuiltMesh>& Actors,
    std::vector<std::uint8_t>& Out, std::string& Error) {
    Meshes Payloads;
    for (const auto& A:Actors)
        if (!EncodePayload(A.second.Settings, A.second.Tiles, Payloads[A.first], Error)) return false;
    return Encode(Package, Payloads, Out, Error);
}
}}

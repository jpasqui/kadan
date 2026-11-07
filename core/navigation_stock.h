#pragma once
// The stock map's own navigation as plain data: each RecastNavMesh actor's SupportedAreas table
// and native tail. Reads what swap_navmesh.navmeshes reads (inspect_map_navigation.inspect,
// nav_encode.decode, nav_rules.area_table) with the same checks, so the plugin needs no Python.
#include "navigation_export.h"
#include "package_reader.h"

namespace P4MapKit { namespace NavigationExport {
// Code is stable. Message is the sentence the Python tools print, when they have one.
struct Problem { std::string Code, Message; };
struct StockActor { std::string Name, Class; BuiltMesh Mesh; };

namespace StockDetail {
inline bool U16(FByteReader& R, std::uint16_t& V) { std::uint64_t B=0; if (!R.Unsigned(2,B)) return false; V=std::uint16_t(B); return true; }
inline bool U8(FByteReader& R, std::uint8_t& V) { std::uint64_t B=0; if (!R.Unsigned(1,B)) return false; V=std::uint8_t(B); return true; }
template<std::size_t N> inline bool Floats(FByteReader& R, std::array<float,N>& V) {
    for (auto& X:V) if (!Navigation::Real(R,X)) return false;
    return true;
}
// Refuse a count the rest of the payload cannot hold before allocating for it.
inline bool Room(FByteReader& R, std::size_t Count, std::size_t Width) {
    return Count<=(R.Limit()-R.Tell())/Width || R.Reject("truncated");
}
inline bool Vectors(FByteReader& R, std::size_t Count, std::vector<Vec3>& Out) {
    if (!Room(R,Count,12)) return false;
    Out.resize(Count);
    for (auto& V:Out) if (!Floats(R,V)) return false;
    return true;
}
inline bool ReadTile(FByteReader& R, const std::vector<std::uint8_t>& Body, BuiltTile& T) {
    auto& H=T.Header;
    if (!R.Unsigned(8,T.Reference) || !R.I32(T.MemorySize)) return false;
    if (T.Reference==UINT64_MAX || T.MemorySize<=0) return R.Reject("navigation-empty-tile");
    // vertices, polygons, links, detail meshes/vertices/triangles, BV nodes, point links, segments, clusters
    std::array<std::size_t,10> N{};
    for (auto& C:N) if (!R.Count(C,1000000)) return false;
    std::array<std::int32_t,15> W{};
    for (auto& V:W) if (!R.I32(V)) return false;
    if (W[0]!=0x444e4156 || W[1]!=7) return R.Reject("navigation-detour-version");
    const std::size_t Order[8]={1,0,2,3,4,5,6,7};
    for (std::size_t I=0; I<8; ++I)
        if (W[6+I]<0 || std::size_t(W[6+I])!=N[Order[I]]) return R.Reject("navigation-count-mismatch");
    // The ground polygons are the detail meshes, no more than the polygons.
    if (W[14]<0 || std::size_t(W[14])!=N[3] || N[3]>N[1]) return R.Reject("navigation-ground-count");
    H.X=W[2]; H.Y=W[3]; H.Layer=W[4]; H.UserId=std::uint32_t(W[5]);
    H.MaxLinkCount=std::int32_t(N[2]); H.OffMeshBase=W[14];
    if (!Navigation::Real(R,H.WalkableHeight) || !Navigation::Real(R,H.WalkableRadius) ||
        !Navigation::Real(R,H.WalkableClimb) || !Floats(R,H.Min) || !Floats(R,H.Max) ||
        !Navigation::Real(R,H.BvQuantFactor)) return false;
    std::int32_t Clusters=0, Segments=0;
    if (!R.I32(Clusters) || !R.I32(Segments) || !R.I32(H.SegmentPolyBase) || !R.I32(H.SegmentVertBase)) return false;
    if (Clusters<0 || Segments<0 || std::size_t(Clusters)!=N[9] || std::size_t(Segments)!=N[8])
        return R.Reject("navigation-count-mismatch");
    if (!Vectors(R,N[0],T.Verts) || !Room(R,N[1],32)) return false;
    T.Polys.resize(N[1]);
    for (auto& P:T.Polys) {
        std::uint8_t AreaType=0;
        if (!R.U32(P.FirstLink)) return false;
        for (auto& V:P.Verts) if (!U16(R,V)) return false;
        for (auto& V:P.Neighbors) if (!U16(R,V)) return false;
        if (!U16(R,P.Flags) || !U8(R,P.VertCount) || !U8(R,AreaType)) return false;
        P.Area=AreaType&63; P.Type=AreaType>>6;
        if (P.VertCount<2 || P.VertCount>6) return R.Reject("navigation-polygon-size");
        for (unsigned I=0; I<P.VertCount; ++I) {
            if (P.Verts[I]>=N[0]) return R.Reject("navigation-polygon-vertex");
            const auto Next=P.Neighbors[I];
            if (Next!=0 && !(Next&0x8000) && Next>N[1]) return R.Reject("navigation-polygon-neighbor");
        }
    }
    if (!Room(R,N[3],10)) return false;
    T.DetailMeshes.resize(N[3]);
    for (auto& D:T.DetailMeshes)
        if (!R.U32(D.VertBase) || !R.U32(D.TriBase) || !U8(R,D.VertCount) || !U8(R,D.TriCount)) return false;
    if (!Vectors(R,N[4],T.DetailVerts) || !Room(R,N[5],4)) return false;
    T.DetailTris.resize(N[5]);
    for (auto& Tri:T.DetailTris) for (auto& V:Tri) if (!U8(R,V)) return false;
    for (std::size_t I=0; I<N[3]; ++I) {
        const auto& D=T.DetailMeshes[I];
        if (std::uint64_t(D.VertBase)+D.VertCount>N[4] || std::uint64_t(D.TriBase)+D.TriCount>N[5])
            return R.Reject("navigation-detail-range");
        for (std::size_t J=D.TriBase; J<std::size_t(D.TriBase)+D.TriCount; ++J)
            for (unsigned K=0; K<3; ++K)
                if (T.DetailTris[J][K]>=unsigned(T.Polys[I].VertCount)+D.VertCount) return R.Reject("navigation-detail-vertex");
    }
    if (!Room(R,N[6],16)) return false;
    T.BvTree.resize(N[6]);
    for (auto& B:T.BvTree) {
        for (auto& V:B.Bounds) if (!U16(R,V)) return false;
        if (!R.I32(B.Index)) return false;
    }
    if (!Room(R,N[7],44)) return false;
    T.PointLinks.resize(N[7]);
    for (auto& C:T.PointLinks) {
        if (!Floats(R,C.Pos) || !Navigation::Real(R,C.Radius) || !U16(R,C.Polygon) ||
            !U8(R,C.Flags) || !U8(R,C.Side) || !R.U32(C.UserId)) return false;
        if (C.Polygon>=N[1]) return R.Reject("navigation-link-polygon");
    }
    for (auto& C:T.PointLinks) if (!Navigation::Real(R,C.Height)) return false;
    if (!Room(R,N[8],60)) return false;
    T.SegmentLinks.resize(N[8]);
    for (auto& S:T.SegmentLinks)
        if (!Floats(R,S.StartA) || !Floats(R,S.StartB) || !Floats(R,S.EndA) || !Floats(R,S.EndB) ||
            !Navigation::Real(R,S.Radius) || !U16(R,S.FirstPoly) || !U8(R,S.PolyCount) || !U8(R,S.Flags) ||
            !R.U32(S.UserId)) return false;
    if (!Vectors(R,N[9],T.Clusters) || !Room(R,N[3],2)) return false;
    T.PolyClusters.resize(N[3]);
    for (auto& C:T.PolyClusters) if (!U16(R,C)) return false;
    if (N[9])
        for (auto C:T.PolyClusters) if (C!=0xffff && C>=N[9]) return R.Reject("navigation-poly-cluster");
    std::size_t Cache=0;
    if (!R.Count(Cache,64*1024*1024)) return false;
    const auto At=R.Tell();
    if (!R.Skip(Cache)) return false;
    T.Cache.assign(Body.begin()+std::ptrdiff_t(At),Body.begin()+std::ptrdiff_t(At+Cache));
    return true;
}
inline bool StartsWith(const std::string& Text, const char* Prefix) {
    return Text.compare(0,std::strlen(Prefix),Prefix)==0;
}
inline bool Agent(const BuiltMesh& M, Vec3& Out) {
    if (M.Tiles.empty()) return false;
    const auto& First=M.Tiles[0].Header;
    Out={{First.WalkableRadius,First.WalkableHeight,First.WalkableClimb}};
    for (const auto& T:M.Tiles)
        if (T.Header.WalkableRadius!=Out[0] || T.Header.WalkableHeight!=Out[1] || T.Header.WalkableClimb!=Out[2]) return false;
    return true;
}
}

// nav_encode.decode of one native payload, Body[Start, End), with inspect_map_navigation.tile's
// checks. Every serialized field is kept, so EncodePayload writes the same bytes again.
// AreaIds stay empty: the areas are the actor's SupportedAreas property (ReadSupportedAreas).
inline bool DecodePayload(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    BuiltMesh& Out, std::string& Error) {
    FByteReader R(Body,Start,End); BuiltMesh Result;
    const auto Parse=[&]() {
        std::int32_t Guid=0, Version=0, Count=0; std::uint32_t Size=0;
        if (!R.I32(Guid) || !R.I32(Version) || !R.U32(Size)) return false;
        if (Guid!=0) return R.Reject("navigation-object-guid");
        if (Version!=13) return R.Reject("navigation-version");
        if (std::uint64_t(Size)+8!=End-Start) return R.Reject("navigation-size");
        if (!R.I32(Count)) return false;
        if (Count<=0 || Count>100000) return R.Reject("navigation-tile-count");
        auto& S=Result.Settings;
        if (!StockDetail::Floats(R,S.Origin) || !Navigation::Real(R,S.TileWidth) ||
            !Navigation::Real(R,S.TileHeight) || !R.I32(S.MaxTiles) || !R.I32(S.MaxPolys)) return false;
        for (std::int32_t I=0; I<Count; ++I) {
            BuiltTile T;
            if (!StockDetail::ReadTile(R,Body,T)) return false;
            Result.Tiles.push_back(std::move(T));
        }
        return R.Tell()==R.Limit() || R.Reject("navigation-trailing-bytes");
    };
    if (!R.Error.Code.empty() || !Parse()) { Error=R.Error.Code; return false; }
    Out=std::move(Result); Error.clear(); return true;
}
inline bool DecodePayload(const std::vector<std::uint8_t>& Bytes, BuiltMesh& Out, std::string& Error) {
    return DecodePayload(Bytes,0,Bytes.size(),Out,Error);
}

// nav_rules.area_table's checks: unique IDs, NavArea_Default 63 and NavArea_Null 0.
inline bool CheckAreas(const Areas& Table, std::string& Error) {
    std::set<unsigned> Ids;
    for (const auto& A:Table) if (!Ids.insert(A.second).second) { Error="navigation-areas-duplicate-id"; return false; }
    const auto Default=Table.find("NavArea_Default"), Null=Table.find("NavArea_Null");
    if (Default==Table.end() || Default->second!=63 || Null==Table.end() || Null->second!=0) {
        Error="navigation-areas-default-null"; return false;
    }
    Error.clear(); return true;
}
// The SupportedAreas array of the actor's tagged properties (envelopes from ReadPropertyEnvelopes),
// read as inspect_map_navigation.inspect and nav_rules.area_table read it.
inline bool ReadSupportedAreas(const std::vector<std::uint8_t>& Body, const std::vector<std::string>& Names,
    const std::vector<FPropertyEnvelope>& Properties, Areas& Out, std::string& Error) {
    const auto Fail=[&](const std::string& Why) { Error=Why; return false; };
    const FPropertyEnvelope* Array=nullptr;
    for (const auto& P:Properties) if (P.Name=="SupportedAreas") { Array=&P; break; }
    if (!Array) return Fail("navigation-areas-missing");
    if (Array->Type!="ArrayProperty" || Array->Detail.size()!=1 || Array->Detail[0]!="StructProperty")
        return Fail("navigation-areas-array");
    FByteReader R(Body,Array->PayloadOffset,Array->PayloadOffset+Array->Size);
    std::size_t Count=0; FPropertyEnvelope Inner;
    if (!R.Count(Count,64) || !ReadPropertyEnvelope(R,Names,Array->Size,Inner)) return Fail(R.Error.Code);
    if (Inner.Name=="None" || Inner.Type!="StructProperty" || Inner.Detail.size()!=1 ||
        Inner.Detail[0]!="SupportedAreaData" || R.Tell()!=R.Limit()) return Fail("navigation-areas-array");
    // Python builds a dict: a repeated class name keeps its last ID.
    std::map<std::string,std::int32_t> Rows;
    std::size_t At=Inner.PayloadOffset; const std::size_t End=Inner.PayloadOffset+Inner.Size;
    for (std::size_t Row=0; Row<Count; ++Row) {
        const auto Fields=ReadPropertyEnvelopes(Body,Names,At,End);
        if (!Fields.Error.Code.empty()) return Fail(Fields.Error.Code);
        At=Fields.End;
        const FPropertyEnvelope *Name=nullptr, *Id=nullptr;
        for (const auto& F:Fields.Properties) {
            if (F.Name=="AreaClassName" && !Name) Name=&F;
            if (F.Name=="AreaID" && !Id) Id=&F;
        }
        if (!Name || !Id || Id->Type!="IntProperty" || Id->Size!=4) return Fail("navigation-areas-row");
        std::string Class; std::int32_t Value=0;
        FByteReader N(Body,Name->PayloadOffset,Name->PayloadOffset+Name->Size), V(Body,Id->PayloadOffset,Id->PayloadOffset+4);
        const bool Good=Name->Type=="StrProperty" ? N.String(Class) : Name->Type=="NameProperty" && N.Name(Names,Class);
        if (!Good || N.Tell()!=N.Limit() || !V.I32(Value)) return Fail("navigation-areas-row");
        Rows[Class]=Value;
    }
    if (At!=End) return Fail("navigation-areas-array");
    Areas Table;
    for (const auto& Row:Rows) {
        if (Row.second<0 || Row.second>=64) return Fail("navigation-areas-id");
        Table.emplace(Row.first,std::uint8_t(Row.second));
    }
    if (!CheckAreas(Table,Error)) return false;
    Out=std::move(Table); Error.clear(); return true;
}
inline std::string EmptyNavmesh(const std::string& Name) {
    return "The navmesh "+Name+" is empty because no walkable ground is inside a NavMeshBoundsVolume. "
        "Keep the ground inside the map's NavMeshBoundsVolume, or place a new one around it, and export again.";
}
// One RecastNavMesh export: its tagged properties and native tail, Body[Start, End).
inline bool ReadStockActor(const std::vector<std::uint8_t>& Body, const std::vector<std::string>& Names,
    std::size_t Start, std::size_t End, const std::string& Name, const std::string& Class,
    StockActor& Out, Problem& Error) {
    const auto Fail=[&](const std::string& Code, const std::string& Message) { Error={Code,Message}; return false; };
    const auto Properties=ReadPropertyEnvelopes(Body,Names,Start,End);
    if (!Properties.Error.Code.empty()) return Fail(Properties.Error.Code,"");
    StockActor Result; Result.Name=Name; Result.Class=Class; std::string Code; Areas Table;
    if (!ReadSupportedAreas(Body,Names,Properties.Properties,Table,Code)) return Fail(Code,"");
    const auto Tail=Properties.End;
    if (End-Tail>=16 && (Body[Tail+12]|Body[Tail+13]|Body[Tail+14]|Body[Tail+15])==0)
        return Fail("navigation-empty-navmesh",EmptyNavmesh(Name));
    if (!DecodePayload(Body,Tail,End,Result.Mesh,Code)) return Fail(Code,"");
    Result.Mesh.AreaIds=std::move(Table);
    std::vector<std::uint8_t> Again;
    if (!EncodePayload(Result.Mesh.Settings,Result.Mesh.Tiles,Again,Code)) return Fail(Code,"");
    if (Again.size()!=End-Tail || !std::equal(Again.begin(),Again.end(),Body.begin()+std::ptrdiff_t(Tail)))
        return Fail("navigation-stock-unreadable","The data of navmesh "+Name+" could not be read. Nothing was changed.");
    Vec3 Agent{};
    if (!StockDetail::Agent(Result.Mesh,Agent))
        return Fail("navigation-agent-sizes","The tiles of navmesh "+Name+" use different agent sizes");
    Out=std::move(Result); Error={}; return true;
}
// Every RecastNavMesh actor of a cooked map (.umap header, .uexp body), in export order:
// the game's Carrot4 classes and the engine's own class.
inline bool ReadStockMap(const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body,
    std::vector<StockActor>& Out, Problem& Error) {
    const auto Package=ReadPackage(Header,Body);
    if (!Package.Error.Code.empty()) { Error={Package.Error.Code,""}; return false; }
    std::vector<StockActor> Result;
    for (const auto& Export:Package.Exports) {
        std::string Class;
        if (!Package.Reference(Export.Class,Class)) { Error={"navigation-stock-class",""}; return false; }
        if (!StockDetail::StartsWith(Class,"/Script/Carrot4.RecastNavMesh") &&
            !StockDetail::StartsWith(Class,"/Script/NavigationSystem.RecastNavMesh")) continue;
        StockActor Actor;
        if (!ReadStockActor(Body,Package.Names,Export.Offset,Export.Offset+Export.Size,Export.Name,Class,Actor,Error))
            return false;
        Result.push_back(std::move(Actor));
    }
    Out=std::move(Result); Error={}; return true;
}
}}

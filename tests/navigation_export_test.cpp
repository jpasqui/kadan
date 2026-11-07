// Host-only adapter: JSON is test transport, never part of the editor API.
//   navigation_export_test convert input.json output.nav       Convert + EncodeBuilt on plain tiles
//   navigation_export_test stock map.umap map.uexp output.json  ReadStockMap, written as Python decodes
//   navigation_export_test route input.json output.json output.nav   ReadStockMap + ExportMap
#include "core/navigation_map_export.h"
#include "core/blueprint_json.h"
#include "navigation/nav_tiles.h"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace P4MapKit::NavigationExport;
using J=P4MapKit::Blueprint::J;
using MapDetail::Repr;

template<class T, std::size_t N> static std::array<T,N> Array(const J& V) {
    if (V.Items.size()!=N) throw std::runtime_error("fixture-array-size");
    std::array<T,N> R{};
    for (std::size_t I=0; I<N; ++I) R[I]=T(V[I].D());
    return R;
}
static BuiltMesh Mesh(const J& V) {
    BuiltMesh R;
    R.Settings.Origin=Array<float,3>(V["origin"]);
    R.Settings.TileWidth=float(V["tile_width"].D()); R.Settings.TileHeight=float(V["tile_height"].D());
    R.Settings.MaxTiles=std::int32_t(V["max_tiles"].I()); R.Settings.MaxPolys=std::int32_t(V["max_polys"].I());
    for (const auto& T:V["tiles"].Items) {
        BuiltTile B; auto& H=B.Header; const auto& S=T["header"];
        B.Reference=T["reference"].U(); B.MemorySize=std::int32_t(T["memory_size"].I());
        H.X=std::int32_t(S["x"].I()); H.Y=std::int32_t(S["y"].I()); H.Layer=std::int32_t(S["layer"].I());
        H.UserId=std::uint32_t(S["user_id"].U()); H.MaxLinkCount=std::int32_t(S["max_link_count"].I());
        H.OffMeshBase=std::int32_t(S["offmesh_base"].I());
        H.WalkableHeight=float(S["walkable_height"].D()); H.WalkableRadius=float(S["walkable_radius"].D());
        H.WalkableClimb=float(S["walkable_climb"].D()); H.BvQuantFactor=float(S["bv_quant_factor"].D());
        H.Min=Array<float,3>(S["bmin"]); H.Max=Array<float,3>(S["bmax"]);
        H.SegmentPolyBase=std::int32_t(S["seg_poly_base"].I()); H.SegmentVertBase=std::int32_t(S["seg_vert_base"].I());
        for (const auto& P:T["verts"].Items) B.Verts.push_back(Array<float,3>(P));
        for (const auto& P:T["detail_verts"].Items) B.DetailVerts.push_back(Array<float,3>(P));
        for (const auto& P:T["clusters"].Items) B.Clusters.push_back(Array<float,3>(P));
        for (const auto& P:T["polys"].Items) {
            Poly Q; Q.FirstLink=std::uint32_t(P["first_link"].U());
            Q.Verts=Array<std::uint16_t,6>(P["verts"]); Q.Neighbors=Array<std::uint16_t,6>(P["neis"]);
            Q.Flags=std::uint16_t(P["flags"].U()); Q.VertCount=std::uint8_t(P["vert_count"].U());
            Q.Area=std::uint8_t(P["area"].U()); Q.Type=std::uint8_t(P["type"].U()); B.Polys.push_back(Q);
        }
        for (const auto& P:T["detail_meshes"].Items) {
            DetailMesh Q; Q.VertBase=std::uint32_t(P[0].U()); Q.TriBase=std::uint32_t(P[1].U());
            Q.VertCount=std::uint8_t(P[2].U()); Q.TriCount=std::uint8_t(P[3].U()); B.DetailMeshes.push_back(Q);
        }
        for (const auto& P:T["detail_tris"].Items) B.DetailTris.push_back(Array<std::uint8_t,4>(P));
        for (const auto& P:T["bv_tree"].Items) {
            BvNode Q; for (unsigned I=0; I<6; ++I) Q.Bounds[I]=std::uint16_t(P[I].U());
            Q.Index=std::int32_t(P[6].I()); B.BvTree.push_back(Q);
        }
        for (const auto& P:T["off_mesh"].Items) {
            PointLink Q; Q.Pos=Array<float,6>(P["pos"]); Q.Radius=float(P["rad"].D()); Q.Height=float(P["height"].D());
            Q.Polygon=std::uint16_t(P["poly"].U()); Q.Flags=std::uint8_t(P["flags"].U());
            Q.Side=std::uint8_t(P["side"].U()); Q.UserId=std::uint32_t(P["user_id"].U()); B.PointLinks.push_back(Q);
        }
        for (const auto& P:T["segments"].Items) {
            SegmentLink Q; Q.StartA=Array<float,3>(P["start_a"]); Q.StartB=Array<float,3>(P["start_b"]);
            Q.EndA=Array<float,3>(P["end_a"]); Q.EndB=Array<float,3>(P["end_b"]); Q.Radius=float(P["rad"].D());
            Q.FirstPoly=std::uint16_t(P["first_poly"].U()); Q.PolyCount=std::uint8_t(P["npolys"].U());
            Q.Flags=std::uint8_t(P["flags"].U()); Q.UserId=std::uint32_t(P["user_id"].U()); B.SegmentLinks.push_back(Q);
        }
        for (const auto& P:T["poly_clusters"].Items) B.PolyClusters.push_back(std::uint16_t(P.U()));
        if (!T["cache"].S().empty()) throw std::runtime_error("fixture-cache-unsupported");
        R.Tiles.push_back(std::move(B));
    }
    return R;
}
static Areas ReadAreas(const J& V) {
    Areas R; for (const auto& A:V.Fields) R.emplace(A.first,std::uint8_t(A.second.U())); return R;
}
#define CHECK(X) do { if (!(X)) throw std::runtime_error(#X); } while (false)

// ---- output in nav_encode.decode's layout, floats as Python's repr ----
static std::string Quote(const std::string& Text) {
    std::string Out="\"";
    for (char C:Text) {
        if (C=='"' || C=='\\') { Out+='\\'; Out+=C; }
        else if (C=='\n') Out+="\\n";
        else if (std::uint8_t(C)<0x20) { char B[8]; std::snprintf(B,sizeof B,"\\u%04x",unsigned(C)); Out+=B; }
        else Out+=C;
    }
    return Out+'"';
}
template<std::size_t N> static std::string Floats(const std::array<float,N>& V) {
    std::string Out="[";
    for (std::size_t I=0; I<N; ++I) { if (I) Out+=','; Out+=Repr(V[I]); }
    return Out+']';
}
template<class T, std::size_t N> static std::string Integers(const std::array<T,N>& V) {
    std::string Out="[";
    for (std::size_t I=0; I<N; ++I) { if (I) Out+=','; Out+=std::to_string(std::int64_t(V[I])); }
    return Out+']';
}
template<class T, class F> static void List(std::ostream& O, const std::vector<T>& Items, F Write) {
    O<<'['; for (std::size_t I=0; I<Items.size(); ++I) { if (I) O<<','; Write(Items[I]); } O<<']';
}
static void WriteMesh(std::ostream& O, const BuiltMesh& M) {
    const auto& S=M.Settings;
    O<<"{\"version\":13,\"origin\":"<<Floats(S.Origin)<<",\"tile_width\":"<<Repr(S.TileWidth)<<",\"tile_height\":"
     <<Repr(S.TileHeight)<<",\"max_tiles\":"<<S.MaxTiles<<",\"max_polys\":"<<S.MaxPolys<<",\"tiles\":";
    List(O,M.Tiles,[&](const BuiltTile& T) {
        const auto& H=T.Header;
        O<<"{\"reference\":"<<T.Reference<<",\"memory_size\":"<<T.MemorySize<<",\"header\":{\"x\":"<<H.X<<",\"y\":"<<H.Y
         <<",\"layer\":"<<H.Layer<<",\"user_id\":"<<H.UserId<<",\"max_link_count\":"<<H.MaxLinkCount
         <<",\"offmesh_base\":"<<H.OffMeshBase<<",\"walkable_height\":"<<Repr(H.WalkableHeight)
         <<",\"walkable_radius\":"<<Repr(H.WalkableRadius)<<",\"walkable_climb\":"<<Repr(H.WalkableClimb)
         <<",\"bmin\":"<<Floats(H.Min)<<",\"bmax\":"<<Floats(H.Max)<<",\"bv_quant_factor\":"<<Repr(H.BvQuantFactor)
         <<",\"seg_poly_base\":"<<H.SegmentPolyBase<<",\"seg_vert_base\":"<<H.SegmentVertBase<<"},\"verts\":";
        List(O,T.Verts,[&](const Vec3& V) { O<<Floats(V); });
        O<<",\"polys\":";
        List(O,T.Polys,[&](const Poly& P) {
            O<<"{\"first_link\":"<<P.FirstLink<<",\"verts\":"<<Integers(P.Verts)<<",\"neis\":"<<Integers(P.Neighbors)
             <<",\"flags\":"<<P.Flags<<",\"vert_count\":"<<unsigned(P.VertCount)<<",\"area\":"<<unsigned(P.Area)
             <<",\"type\":"<<unsigned(P.Type)<<'}';
        });
        O<<",\"detail_meshes\":";
        List(O,T.DetailMeshes,[&](const DetailMesh& D) {
            O<<'['<<D.VertBase<<','<<D.TriBase<<','<<unsigned(D.VertCount)<<','<<unsigned(D.TriCount)<<']';
        });
        O<<",\"detail_verts\":"; List(O,T.DetailVerts,[&](const Vec3& V) { O<<Floats(V); });
        O<<",\"detail_tris\":"; List(O,T.DetailTris,[&](const std::array<std::uint8_t,4>& V) { O<<Integers(V); });
        O<<",\"bv_tree\":";
        List(O,T.BvTree,[&](const BvNode& N) { const auto B=Integers(N.Bounds); O<<B.substr(0,B.size()-1)<<','<<N.Index<<']'; });
        O<<",\"off_mesh\":";
        List(O,T.PointLinks,[&](const PointLink& C) {
            O<<"{\"pos\":"<<Floats(C.Pos)<<",\"rad\":"<<Repr(C.Radius)<<",\"poly\":"<<C.Polygon<<",\"flags\":"<<unsigned(C.Flags)
             <<",\"side\":"<<unsigned(C.Side)<<",\"user_id\":"<<C.UserId<<",\"height\":"<<Repr(C.Height)<<'}';
        });
        O<<",\"segments\":";
        List(O,T.SegmentLinks,[&](const SegmentLink& L) {
            O<<"{\"start_a\":"<<Floats(L.StartA)<<",\"start_b\":"<<Floats(L.StartB)<<",\"end_a\":"<<Floats(L.EndA)
             <<",\"end_b\":"<<Floats(L.EndB)<<",\"rad\":"<<Repr(L.Radius)<<",\"first_poly\":"<<L.FirstPoly
             <<",\"npolys\":"<<unsigned(L.PolyCount)<<",\"flags\":"<<unsigned(L.Flags)<<",\"user_id\":"<<L.UserId<<'}';
        });
        O<<",\"clusters\":"; List(O,T.Clusters,[&](const Vec3& V) { O<<Floats(V); });
        O<<",\"poly_clusters\":"; List(O,T.PolyClusters,[&](std::uint16_t V) { O<<V; });
        static const char Hex[]="0123456789abcdef";
        O<<",\"cache\":\"";
        for (auto B:T.Cache) O<<Hex[B>>4]<<Hex[B&15];
        O<<"\"}";
    });
    O<<'}';
}
static std::vector<std::uint8_t> ReadBytes(const std::string& Path) {
    std::ifstream In(Path,std::ios::binary);
    if (!In) throw std::runtime_error("fixture-open: "+Path);
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(In),{}};
}
static void WriteBytes(const std::string& Path, const std::string& Text) {
    std::ofstream File(Path,std::ios::binary); File.write(Text.data(),std::streamsize(Text.size()));
    CHECK(bool(File));
}
static void WriteProblem(std::ostream& O, const Problem& P) {
    O<<"{\"error\":"<<Quote(P.Code)<<",\"message\":"<<Quote(P.Message)<<'}';
}

static void Rejections(const BuiltMesh& Good, const std::string& Package) {
    std::string Error; std::vector<std::uint8_t> Out{1,2,3}; const auto Sentinel=Out;
    auto Bad=Good; Bad.Tiles[0].Polys[0].Area=64;
    CHECK(!EncodeBuilt(Package,{{"RecastNavMeshPikmin-Pikmin",Bad}},Out,Error)); CHECK(Out==Sentinel);
    Bad=Good; Bad.Tiles[0].DetailMeshes[0].TriBase=UINT32_MAX;
    CHECK(!EncodePayload(Bad.Settings,Bad.Tiles,Out,Error)); CHECK(Out==Sentinel);
    Bad=Good; Bad.Tiles[0].Verts[0][0]=std::numeric_limits<float>::infinity();
    CHECK(!EncodePayload(Bad.Settings,Bad.Tiles,Out,Error)); CHECK(Out==Sentinel);
    Bad=Good; Bad.Tiles.push_back(Bad.Tiles[0]); Bad.Settings.MaxTiles=65536;
    CHECK(!EncodePayload(Bad.Settings,Bad.Tiles,Out,Error)); CHECK(Out==Sentinel);
    Bad=Good; Bad.Tiles[0].Header.MaxLinkCount=-1;
    CHECK(!EncodePayload(Bad.Settings,Bad.Tiles,Out,Error)); CHECK(Out==Sentinel);
    CHECK(!EncodeBuilt("/Game/../Bad",{{"Mesh",Good}},Out,Error)); CHECK(Out==Sentinel);
    CHECK(!EncodeBuilt(Package,{{"Mesh",Good},{"mesh",Good}},Out,Error)); CHECK(Out==Sentinel);
    // Indices the loader follows without checking.
    if (!Good.Tiles[0].Clusters.empty()) {
        Bad=Good; Bad.Tiles[0].PolyClusters[0]=std::uint16_t(Bad.Tiles[0].Clusters.size());
        CHECK(!EncodePayload(Bad.Settings,Bad.Tiles,Out,Error) && Error=="navigation-export-poly-cluster");
        Bad.Tiles[0].PolyClusters[0]=0xffff; CHECK(ValidatePayload(Bad.Settings,Bad.Tiles,Error));
    }
    Bad=Good; Bad.Tiles[0].BvTree.push_back(BvNode{}); Bad.Tiles[0].BvTree.back().Index=std::int32_t(Bad.Tiles[0].Polys.size());
    CHECK(!EncodePayload(Bad.Settings,Bad.Tiles,Out,Error) && Error=="navigation-export-bv-node");
    Bad.Tiles[0].BvTree.back().Index=-2; CHECK(!ValidatePayload(Bad.Settings,Bad.Tiles,Error) && Error=="navigation-export-bv-node");
    Bad.Tiles[0].BvTree.back().Index=-1; CHECK(ValidatePayload(Bad.Settings,Bad.Tiles,Error));
    Bad=Good; Bad.Tiles[0].SegmentLinks.push_back(SegmentLink{}); Bad.Tiles[0].Header.SegmentPolyBase=0;
    Bad.Tiles[0].SegmentLinks.back().FirstPoly=std::uint16_t(Bad.Tiles[0].Polys.size()); Bad.Tiles[0].SegmentLinks.back().PolyCount=1;
    CHECK(!EncodePayload(Bad.Settings,Bad.Tiles,Out,Error) && Error=="navigation-export-segment-polygon");
    Bad.Tiles[0].SegmentLinks.back().FirstPoly=0; CHECK(ValidatePayload(Bad.Settings,Bad.Tiles,Error));
    Bad.Tiles[0].Header.SegmentPolyBase=std::int32_t(Bad.Tiles[0].Polys.size());
    CHECK(!ValidatePayload(Bad.Settings,Bad.Tiles,Error) && Error=="navigation-export-segment-polygon");
    CHECK(Out==Sentinel);
    BuiltMesh Result=Good; ConversionReport Report; Report.LinksAdded=99;
    Bad=Good; Bad.Settings.TileWidth*=2;
    CHECK(!Convert(Good,Bad,Result,Error,Report)); CHECK(Report.LinksAdded==99);
    CHECK(Result.Tiles[0].Reference==Good.Tiles[0].Reference);
    // A decoded payload encodes to its own bytes.
    std::vector<std::uint8_t> Bytes, Again; BuiltMesh Decoded;
    CHECK(EncodePayload(Good.Settings,Good.Tiles,Bytes,Error));
    CHECK(DecodePayload(Bytes,Decoded,Error)); CHECK(EncodePayload(Decoded.Settings,Decoded.Tiles,Again,Error)); CHECK(Again==Bytes);
    auto Cut=Bytes; Cut.pop_back(); CHECK(!DecodePayload(Cut,Decoded,Error));
}
static int ConvertCase(const std::string& Input, const std::string& Output) {
    const auto Text=ReadBytes(Input);
    // Full Area001 has six actors. This larger budget is only JSON test transport.
    const J Doc=J::Parse(std::string(Text.begin(),Text.end()),64u*1024u*1024u,10000000);
    const auto Package=Doc["package"].S(); std::map<std::string,BuiltMesh> Actors;
    std::size_t Added=0, Skipped=0; std::string Error;
    for (const auto& A:Doc["actors"].Items) {
        BuiltMesh Bake=Mesh(A["bake"]), Result=Bake;
        if (A.Has("target")) {
            BuiltMesh Target=Mesh(A["target"]); Target.AreaIds=ReadAreas(A["target_areas"]);
            Bake.AreaIds=ReadAreas(A["bake_areas"]); ConversionReport Report;
            if (!Convert(Target,Bake,Result,Error,Report,A["carry"].B())) throw std::runtime_error(Error);
            Added+=Report.LinksAdded; Skipped+=Report.LinksSkipped.size();
        }
        Actors.emplace(A["name"].S(),std::move(Result));
    }
    CHECK(!Actors.empty()); Rejections(Actors.begin()->second,Package);
    std::vector<std::uint8_t> Bytes;
    if (!EncodeBuilt(Package,Actors,Bytes,Error)) throw std::runtime_error(Error);
    p4m::nav::Map Map[1]{}; p4m::nav::Navmesh Meshes[64]{};
    p4m::nav::Set Set{Bytes.data(),std::uint32_t(Bytes.size()),Map,1,0,Meshes,64,0};
    CHECK(p4m::nav::parse(&Set)==p4m::nav::kOk); CHECK(Set.navmesh_count==Actors.size());
    // Reconstruct every Detour block with the actual consumer, including record reordering.
    for (unsigned I=0; I<Set.navmesh_count; ++I) p4m::nav::for_each_tile(Bytes.data(),Meshes[I],[](const p4m::nav::Tile& T) {
        std::vector<std::uint8_t> Block(T.memory); p4m::nav::build_tile(T,Block.data()); return true;
    });
    WriteBytes(Output,std::string(Bytes.begin(),Bytes.end()));
    std::cout<<"bytes="<<Bytes.size()<<" links_added="<<Added<<" links_skipped="<<Skipped<<'\n';
    return 0;
}
static int StockCase(const std::string& Header, const std::string& Body, const std::string& Output) {
    std::vector<StockActor> Actors; Problem Error; std::ostringstream O;
    if (!ReadStockMap(ReadBytes(Header),ReadBytes(Body),Actors,Error)) WriteProblem(O,Error);
    else List(O,Actors,[&](const StockActor& A) {
        Vec3 Agent{}; CHECK(StockDetail::Agent(A.Mesh,Agent) || A.Mesh.Tiles.empty());
        O<<"{\"name\":"<<Quote(A.Name)<<",\"cls\":"<<Quote(A.Class)<<",\"agent\":"<<Floats(Agent)<<",\"areas\":{";
        bool First=true;
        for (const auto& Area:A.Mesh.AreaIds) { O<<(First?"":",")<<Quote(Area.first)<<':'<<unsigned(Area.second); First=false; }
        O<<"},\"nav\":"; WriteMesh(O,A.Mesh); O<<'}';
    });
    WriteBytes(Output,O.str());
    std::cout<<"actors="<<Actors.size()<<'\n';
    return 0;
}
static std::vector<BakedActor> Bakes(const J& List) {
    std::vector<BakedActor> Out;
    for (const auto& B:List.Items) { BakedActor A; A.Name=B["name"].S(); A.Mesh=Mesh(B["nav"]); A.Mesh.AreaIds=ReadAreas(B["areas"]); Out.push_back(std::move(A)); }
    return Out;
}
static int RouteCase(const std::string& Input, const std::string& Output, const std::string& Nav) {
    const auto Text=ReadBytes(Input);
    const J Doc=J::Parse(std::string(Text.begin(),Text.end()),64u*1024u*1024u,10000000);
    MapExportInput In; Problem Error; std::ostringstream O;
    In.Package=Doc["package"].S(); In.CarryStockLinks=Doc["carry"].B();
    In.Bakes=Bakes(Doc["bakes"]);
    if (Doc.Has("hero_bakes")) { In.HasHeroBake=true; In.HeroBakes=Bakes(Doc["hero_bakes"]); }
    MapExport Out; Out.Bytes={9}; const auto Sentinel=Out.Bytes;
    if (!ReadStockMap(ReadBytes(Doc["stock"]["umap"].S()),ReadBytes(Doc["stock"]["uexp"].S()),In.Stock,Error)) {
        WriteProblem(O,Error); WriteBytes(Output,O.str()); return 0;
    }
    if (!ExportMap(In,Out,Error)) {
        CHECK(Out.Bytes==Sentinel && Out.Actors.empty());
        WriteProblem(O,Error); WriteBytes(Output,O.str()); std::cout<<"error="<<Error.Code<<'\n'; return 0;
    }
    O<<"{\"error\":\"\",\"actors\":";
    List(O,Out.Actors,[&](const ActorReport& R) {
        O<<"{\"actor\":"<<Quote(R.Actor)<<",\"replaced\":"<<(R.Replaced?"true":"false")<<",\"source\":"<<Quote(R.Source)
         <<",\"reason\":"<<Quote(R.Reason)<<",\"tiles\":"<<R.Tiles<<",\"polygons\":"<<R.Polygons
         <<",\"links_added\":"<<R.LinksAdded<<",\"links_skipped\":"<<R.LinksSkipped.size()<<'}';
    });
    O<<'}';
    WriteBytes(Output,O.str()); WriteBytes(Nav,std::string(Out.Bytes.begin(),Out.Bytes.end()));
    std::cout<<"bytes="<<Out.Bytes.size()<<'\n';
    return 0;
}
int main(int Count, char** Args) {
    try {
        const std::string Mode=Count>1 ? Args[1] : "";
        if (Mode=="convert" && Count==4) return ConvertCase(Args[2],Args[3]);
        if (Mode=="stock" && Count==5) return StockCase(Args[2],Args[3],Args[4]);
        if (Mode=="route" && Count==5) return RouteCase(Args[2],Args[3],Args[4]);
        throw std::runtime_error("usage: navigation_export_test convert|stock|route ...");
    } catch (const std::exception& Error) { std::cerr<<Error.what()<<'\n'; return 1; }
}

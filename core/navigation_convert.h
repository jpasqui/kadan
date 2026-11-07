#pragma once
#include "navigation_export.h"
#include <tuple>

// mapkit.convert's paint=False path: swap_navmesh.remap and stock point-link carry-over.
// Modifier areas are baked by the editor. This boundary does not bake collision,
// recover helper volumes, repaint polygons, or open/cook/transplant map packages.
namespace P4MapKit { namespace NavigationExport {
struct ConversionReport { std::size_t LinksAdded=0; std::vector<std::string> LinksSkipped; };
namespace ConvertDetail {
using DVec = std::array<double,3>;
inline DVec D(const Vec3& P) { return {{P[0],P[1],P[2]}}; }
inline DVec Closest(const BuiltTile& T, std::size_t Index, const DVec& P) {
    const auto& Poly=T.Polys.at(Index); std::vector<DVec> Vs;
    for (unsigned I=0; I<Poly.VertCount; ++I) Vs.push_back(D(T.Verts.at(Poly.Verts[I])));
    bool Inside=false;
    for (std::size_t I=0,J=Vs.size()-1; I<Vs.size(); J=I++) {
        const auto& A=Vs[I]; const auto& B=Vs[J];
        if ((A[2]>P[2])!=(B[2]>P[2]) && P[0]<(B[0]-A[0])*(P[2]-A[2])/(B[2]-A[2])+A[0]) Inside=!Inside;
    }
    if (Inside) {
        const auto& M=T.DetailMeshes.at(Index);
        const auto Vertex=[&](unsigned I) { return I<Poly.VertCount ? Vs.at(I) : D(T.DetailVerts.at(M.VertBase+I-Poly.VertCount)); };
        for (std::size_t I=M.TriBase; I<std::size_t(M.TriBase)+M.TriCount; ++I) {
            const auto& Tri=T.DetailTris.at(I); const auto A=Vertex(Tri[0]), B=Vertex(Tri[1]), C=Vertex(Tri[2]);
            const double X0=C[0]-A[0], Z0=C[2]-A[2], X1=B[0]-A[0], Z1=B[2]-A[2], X2=P[0]-A[0], Z2=P[2]-A[2];
            const double Den=X0*Z1-Z0*X1; if (std::abs(Den)<1e-9) continue;
            const double U=(Z1*X2-X1*Z2)/Den, V=(X0*Z2-Z0*X2)/Den;
            if (U>=-1e-4 && V>=-1e-4 && U+V<=1+1e-4)
                return {{P[0], A[1]+(C[1]-A[1])*U+(B[1]-A[1])*V, P[2]}};
        }
    }
    DVec Best{}; double Distance=std::numeric_limits<double>::infinity();
    for (std::size_t I=0; I<Vs.size(); ++I) {
        const auto& A=Vs[I]; const auto& B=Vs[(I+1)%Vs.size()];
        const double Dx=B[0]-A[0], Dz=B[2]-A[2], Den=Dx*Dx+Dz*Dz;
        const double U=Den==0 ? 0 : std::max(0.0,std::min(1.0,((P[0]-A[0])*Dx+(P[2]-A[2])*Dz)/Den));
        DVec Q{{A[0]+U*Dx, A[1]+U*(B[1]-A[1]), A[2]+U*Dz}};
        const double Dist=(Q[0]-P[0])*(Q[0]-P[0])+(Q[2]-P[2])*(Q[2]-P[2]);
        if (Dist<Distance) { Distance=Dist; Best=Q; }
    }
    return Best;
}
inline std::pair<double,double> Column(const MapSettings& S, const DVec& P) {
    return {std::floor((P[0]-S.Origin[0])/S.TileWidth),std::floor((P[2]-S.Origin[2])/S.TileHeight)};
}
inline std::vector<std::size_t> TilesAt(const BuiltMesh& M, const DVec& P) {
    const auto C=Column(M.Settings,P); std::vector<std::size_t> Result;
    for (std::size_t I=0; I<M.Tiles.size(); ++I)
        if (M.Tiles[I].Header.X==C.first && M.Tiles[I].Header.Y==C.second) Result.push_back(I);
    return Result;
}
inline std::size_t Nearest(const BuiltMesh& M, const std::vector<std::size_t>& Indices,
    const DVec& P, double Radius, double Height) {
    std::size_t Best=SIZE_MAX; double Distance=std::numeric_limits<double>::infinity();
    for (auto I:Indices) for (std::size_t J=0; J<std::size_t(M.Tiles[I].Header.OffMeshBase); ++J) {
        const auto Q=Closest(M.Tiles[I],J,P);
        const double Xz=std::hypot(Q[0]-P[0],Q[2]-P[2]), Y=std::abs(Q[1]-P[1]);
        if (Xz>Radius || Y>Height) continue;
        const double Dist=Xz*Xz+Y*Y;
        if (Dist<Distance) { Distance=Dist; Best=I; }
    }
    return Best;
}
inline std::uint8_t Side(const DVec& P, const TileHeader& H) {
    const unsigned Code=(P[0]>=H.Max[0]?1:0)|(P[2]>=H.Max[2]?2:0)|(P[0]<H.Min[0]?4:0)|(P[2]<H.Min[2]?8:0);
    switch (Code) { case 1:return 0; case 3:return 1; case 2:return 2; case 6:return 3;
        case 4:return 4; case 12:return 5; case 8:return 6; case 9:return 7; default:return 0x80; }
}
// The layout the link insertion needs. The caller has validated the tiles already.
inline bool Editable(const BuiltMesh& M, std::string& Error) {
    for (const auto& T:M.Tiles) {
        const auto Base=T.Header.OffMeshBase;
        if (Base<0 || std::size_t(Base)!=T.DetailMeshes.size() || T.DetailMeshes.size()!=T.PolyClusters.size() ||
            T.Polys.size()!=std::size_t(Base)+T.PointLinks.size() || !T.SegmentLinks.empty() ||
            T.Verts.size()<2*T.PointLinks.size() || (T.MemorySize && std::uint64_t(T.MemorySize)!=PythonMemory(T))) {
            Error="navigation-convert-point-link-layout"; return false;
        }
        const auto GroundVerts=T.Verts.size()-2*T.PointLinks.size();
        for (std::size_t I=0; I<T.Polys.size(); ++I) {
            const auto& P=T.Polys[I];
            if (I<std::size_t(Base)) {
                if (P.Type!=0 || P.VertCount<3) { Error="navigation-convert-ground-layout"; return false; }
                for (unsigned J=0; J<P.VertCount; ++J) if (P.Verts[J]>=GroundVerts) { Error="navigation-convert-ground-vertex"; return false; }
                const auto& Dm=T.DetailMeshes[I];
                for (std::size_t J=Dm.TriBase; J<std::size_t(Dm.TriBase)+Dm.TriCount; ++J)
                    for (unsigned K=0; K<3; ++K) if (T.DetailTris[J][K]>=P.VertCount+Dm.VertCount) {
                        Error="navigation-convert-detail-vertex"; return false;
                    }
            } else {
                const auto J=I-Base;
                if (T.PointLinks[J].Polygon!=I || P.Type!=1 || P.VertCount!=2 ||
                    P.Verts[0]!=GroundVerts+2*J || P.Verts[1]!=GroundVerts+2*J+1) {
                    Error="navigation-convert-point-link-layout"; return false;
                }
            }
        }
    }
    return true;
}
inline std::string GameName(std::string Name, const Areas& Target) {
    if (Name.size()>2 && Name.compare(Name.size()-2,2,"_C")==0) {
        std::string Base=Name.substr(0,Name.size()-2);
        const auto At=Base.rfind("__L");
        if (At!=std::string::npos && At+3<Base.size() &&
            Base.find_first_not_of("0123456789",At+3)==std::string::npos) Base.resize(At);
        if (Target.count(Base)) return Base;
    }
    return Name;
}
inline int Fallback(const std::string& Name) {
    static const std::map<std::string,int> Flags={
        {"NavArea_AvatarAndWorkPikmin",8193},{"NavArea_Closed",32769},{"NavArea_Default",1},
        {"NavArea_FlyOff",17},{"NavArea_HiddenBox",513},{"NavArea_Obstacle",1},{"NavArea_ObstacleLight",1},
        {"NavArea_ObstacleOff",3},{"NavArea_Off",3},{"NavArea_ReservePaint",1},{"NavArea_Splash",4097},
        {"NavArea_Water",5},{"NavArea_WaterSurface",16385}};
    const auto I=Flags.find(Name); return I==Flags.end() ? -1 : I->second;
}
inline std::string Insert(BuiltMesh& M, PointLink C, const Poly& Source) {
    DVec A{{C.Pos[0],C.Pos[1],C.Pos[2]}}, B{{C.Pos[3],C.Pos[4],C.Pos[5]}};
    if (!(C.Radius>0 && C.Radius<=10000 && C.Height>=0 && C.Height<=10000)) return "link-radius-or-height";
    for (auto V:C.Pos) if (!std::isfinite(V) || std::abs(V)>=1e6) return "link-coordinate";
    const auto ColumnTiles=TilesAt(M,A); if (ColumnTiles.empty()) return "link-start-outside-tiles";
    auto I=Nearest(M,ColumnTiles,A,C.Radius,C.Height);
    if (I==SIZE_MAX) {
        double Best=std::numeric_limits<double>::infinity();
        for (auto J:ColumnTiles) {
            const auto& H=M.Tiles[J].Header; const double Gap=std::max({H.Min[1]-A[1],A[1]-H.Max[1],0.0});
            if (Gap<Best) { Best=Gap; I=J; }
        }
    }
    auto& T=M.Tiles[I]; C.Side=Side(B,T.Header);
    const int Steps[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    const int Dx=C.Side==0x80 ? 0 : Steps[C.Side][0], Dy=C.Side==0x80 ? 0 : Steps[C.Side][1];
    if (Column(M.Settings,B)!=std::make_pair(double(T.Header.X)+Dx,double(T.Header.Y)+Dy)) return "link-end-outside-neighbors";
    if (T.Polys.size()+1>std::size_t(M.Settings.MaxPolys) || T.Polys.size()>UINT16_MAX || T.Verts.size()+1>UINT16_MAX)
        return "link-tile-capacity";
    Poly P; P.Verts[0]=std::uint16_t(T.Verts.size()); P.Verts[1]=std::uint16_t(T.Verts.size()+1);
    P.Flags=Source.Flags; P.Area=Source.Area; P.Type=1; P.VertCount=2;
    C.Polygon=std::uint16_t(T.Header.OffMeshBase+T.PointLinks.size()); C.Flags&=0xc0;
    T.Verts.push_back({{C.Pos[0],C.Pos[1],C.Pos[2]}}); T.Verts.push_back({{C.Pos[3],C.Pos[4],C.Pos[5]}});
    T.Polys.push_back(P); T.PointLinks.push_back(C); T.MemorySize=std::int32_t(PythonMemory(T));
    return {};
}
}
// Target supplies original settings, SupportedAreas and stock polygons/links.
// Bake supplies editor-built tiles and its SupportedAreas. Output is transactional.
// The editor has already applied modifier volumes (mapkit.convert uses paint=False).
inline bool Convert(const BuiltMesh& Target, const BuiltMesh& Bake, BuiltMesh& Out,
    std::string& Error, ConversionReport& Report, bool CarryStockLinks=true) {
    const auto& G=Target.Settings; const auto& B=Bake.Settings;
    if (G.Origin!=B.Origin || G.TileWidth!=B.TileWidth || G.TileHeight!=B.TileHeight ||
        G.MaxTiles!=B.MaxTiles || G.MaxPolys!=B.MaxPolys || Target.Tiles.empty() || Bake.Tiles.empty()) {
        Error="navigation-convert-grid-mismatch"; return false;
    }
    const auto& Agent=Target.Tiles[0].Header;
    for (const auto* M:{&Target,&Bake}) for (const auto& T:M->Tiles)
        if (T.Header.WalkableRadius!=Agent.WalkableRadius || T.Header.WalkableHeight!=Agent.WalkableHeight ||
            T.Header.WalkableClimb!=Agent.WalkableClimb) { Error="navigation-convert-agent-mismatch"; return false; }
    // Validated once here and once at the end. EncodeBuilt writes the bytes once.
    if (!ValidatePayload(Target.Settings,Target.Tiles,Error) || !ValidatePayload(Bake.Settings,Bake.Tiles,Error)) return false;
    std::map<unsigned,std::string> GameNames, BakeNames;
    for (const auto& A:Target.AreaIds) if (!GameNames.emplace(A.second,A.first).second || A.second>=64) {
        Error="navigation-convert-area-table"; return false;
    }
    for (const auto& A:Bake.AreaIds) if (!BakeNames.emplace(A.second,ConvertDetail::GameName(A.first,Target.AreaIds)).second || A.second>=64) {
        Error="navigation-convert-area-table"; return false;
    }
    std::map<std::string,std::uint16_t> Ground, Links;
    for (const auto& T:Target.Tiles) for (const auto& P:T.Polys) {
        auto N=GameNames.find(P.Area);
        if (N==GameNames.end()) { Error="navigation-convert-unknown-stock-area"; return false; }
        (P.Type ? Links : Ground).emplace(N->second,P.Flags);
    }
    BuiltMesh Result=Bake; Result.AreaIds=Target.AreaIds;
    for (auto& T:Result.Tiles) for (auto& P:T.Polys) {
        auto N=BakeNames.find(P.Area);
        if (N==BakeNames.end() || !Target.AreaIds.count(N->second)) { Error="navigation-convert-unknown-bake-area"; return false; }
        const auto& Name=N->second; int Flags=0;
        if (P.Type) Flags=Links.count(Name) ? Links.at(Name) : ((Ground.count(Name)?Ground.at(Name):1)|0x8000);
        else Flags=Ground.count(Name) ? Ground.at(Name) : ConvertDetail::Fallback(Name);
        if (Flags<0) { Error="navigation-convert-unknown-ground-flags"; return false; }
        P.Area=Target.AreaIds.at(Name); P.Flags=std::uint16_t(Flags);
    }
    ConversionReport Prepared;
    if (CarryStockLinks) {
        if (!ConvertDetail::Editable(Result,Error)) return false;
        // carry_over keys links by their JSON text, where -0.0 and 0.0 differ: compare bit patterns.
        const auto Bits=[](float V) { std::uint32_t B=0; std::memcpy(&B,&V,4); return B; };
        using Key=std::tuple<std::array<std::uint32_t,6>,std::uint32_t,std::uint32_t,unsigned,unsigned,unsigned,unsigned>;
        std::set<Key> Seen;
        for (const auto& T:Target.Tiles) for (const auto& C:T.PointLinks) {
            const auto& P=T.Polys.at(C.Polygon);
            std::array<std::uint32_t,6> Pos{};
            for (std::size_t I=0; I<6; ++I) Pos[I]=Bits(C.Pos[I]);
            if (!Seen.emplace(Pos,Bits(C.Radius),Bits(C.Height),C.Flags&0xc0,C.UserId,P.Flags,P.Area).second) continue;
            const auto Why=ConvertDetail::Insert(Result,C,P);
            if (Why.empty()) ++Prepared.LinksAdded; else Prepared.LinksSkipped.push_back(Why);
        }
    }
    if (!ValidatePayload(Result.Settings,Result.Tiles,Error)) return false;
    Out=std::move(Result); Report=std::move(Prepared); Error.clear(); return true;
}
}}

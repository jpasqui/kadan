#pragma once
// A mod's .nav file for one game map: swap_navmesh.swap's actor routing (keep=True, paint=False,
// as mapkit.convert runs it) followed by mapkit.nav_bin. Python's error sentences are kept.
#include "navigation_convert.h"
#include "navigation_stock.h"
#include <cstdio>
#include <cstdlib>

namespace P4MapKit { namespace NavigationExport {
// A navmesh the editor built, as plain data: tiles, grid and the bake's own SupportedAreas.
struct BakedActor { std::string Name; BuiltMesh Mesh; };
struct MapExportInput {
    std::string Package;              // the game map's package, such as /Game/Carrot4/Maps/Main/Area/Area001/Area001_P
    std::vector<StockActor> Stock;    // the game map's Recast actors in export order (ReadStockMap)
    std::vector<BakedActor> Bakes;    // the bake without the map's *_Hero_* sublevels
    // The bake with the *_Hero_* sublevels, for the Hero actors. Without one, the Hero actors
    // take Bakes, as swap_navmesh.swap does when it gets no hero bake.
    bool HasHeroBake=false;
    std::vector<BakedActor> HeroBakes;
    bool CarryStockLinks=true;        // carry the game's jump/drop links over (mapkit.convert does)
};
struct ActorReport {
    std::string Actor, Source, Reason;  // Source: the bake used. Reason: why the actor was kept.
    bool Replaced=false;
    std::size_t Tiles=0, Polygons=0, LinksAdded=0;
    std::vector<std::string> LinksSkipped;
};
struct MapExport { std::vector<std::uint8_t> Bytes; std::vector<ActorReport> Actors; };

namespace MapDetail {
// Python's repr of a float: the shortest digits that read back, laid out as float.__repr__ does.
inline std::string Repr(double V) {
    if (std::isnan(V)) return "nan";
    if (std::isinf(V)) return V<0 ? "-inf" : "inf";
    char Buffer[64]={};
    for (int Digits=1; Digits<=17; ++Digits) {
        std::snprintf(Buffer,sizeof Buffer,"%.*e",Digits-1,V);
        if (std::strtod(Buffer,nullptr)==V) break;
    }
    std::string Text(Buffer);
    const bool Negative=Text[0]=='-';
    if (Negative) Text.erase(0,1);
    const auto E=Text.find('e');
    const int Exponent=std::atoi(Text.c_str()+E+1);
    std::string Digits;
    for (std::size_t I=0; I<E; ++I) if (Text[I]!='.') Digits+=Text[I];
    while (Digits.size()>1 && Digits.back()=='0') Digits.pop_back();
    const int Point=Exponent+1;
    std::string Out;
    if (Point<=-4 || Point>16) {
        Out=Digits.substr(0,1);
        if (Digits.size()>1) Out+="."+Digits.substr(1);
        char Tail[16]; std::snprintf(Tail,sizeof Tail,"e%c%02d",Exponent<0?'-':'+',std::abs(Exponent));
        Out+=Tail;
    } else if (Point<=0) Out="0."+std::string(std::size_t(-Point),'0')+Digits;
    else if (std::size_t(Point)>=Digits.size()) Out=Digits+std::string(std::size_t(Point)-Digits.size(),'0')+".0";
    else Out=Digits.substr(0,std::size_t(Point))+"."+Digits.substr(std::size_t(Point));
    return (Negative?"-":"")+Out;
}
// Python's format(V, 'g').
inline std::string G(double V) { char Buffer[64]; std::snprintf(Buffer,sizeof Buffer,"%g",V); return Buffer; }
inline std::string Tuple(std::initializer_list<float> Values) {
    std::string Out="(";
    for (auto V:Values) { if (Out.size()>1) Out+=", "; Out+=Repr(V); }
    return Out+")";
}
// Olimar-mode navmeshes (RecastNavMeshPikminHero...) were baked with the *_Hero_* sublevels.
inline bool IsHero(const std::string& Name) { return Name.find("Hero")!=std::string::npos; }
// swap_navmesh.navmeshes's checks of one actor, for the stock map and for the bakes alike.
inline bool Admit(const std::string& Name, const BuiltMesh& Mesh, Vec3& Agent, Problem& Error) {
    if (Mesh.Tiles.empty()) { Error={"navigation-empty-navmesh",EmptyNavmesh(Name)}; return false; }
    if (!StockDetail::Agent(Mesh,Agent)) {
        Error={"navigation-agent-sizes","The tiles of navmesh "+Name+" use different agent sizes"}; return false;
    }
    std::string Code;
    if (!CheckAreas(Mesh.AreaIds,Code)) { Error={Code,""}; return false; }
    return true;
}
// swap_navmesh.settings_problems.
inline std::vector<std::string> SettingsProblems(const BuiltMesh& Game, const Vec3& GameAgent,
    const BuiltMesh& Bake, const Vec3& BakeAgent) {
    const auto& G=Game.Settings; const auto& B=Bake.Settings;
    std::vector<std::string> Rows;
    const auto Row=[&](const char* What, bool Differs, const std::string& Want, const std::string& Have) {
        if (Differs) Rows.push_back(std::string("  ")+What+" is "+Want+" in the game and "+Have+" in your bake");
    };
    Row("Tile Size UU",G.TileWidth!=B.TileWidth || G.TileHeight!=B.TileHeight,
        Tuple({G.TileWidth,G.TileHeight}),Tuple({B.TileWidth,B.TileHeight}));
    Row("Nav Mesh Origin Offset",G.Origin!=B.Origin,Tuple({G.Origin[0],G.Origin[1],G.Origin[2]}),
        Tuple({B.Origin[0],B.Origin[1],B.Origin[2]}));
    Row("Fixed Tile Pool Size = on, Tile Pool Size",G.MaxTiles!=B.MaxTiles,std::to_string(G.MaxTiles),std::to_string(B.MaxTiles));
    Row("max polygons per tile (engine-derived)",G.MaxPolys!=B.MaxPolys,std::to_string(G.MaxPolys),std::to_string(B.MaxPolys));
    Row("Agent Height",GameAgent[1]!=BakeAgent[1],Repr(GameAgent[1]),Repr(BakeAgent[1]));
    Row("Agent Max Step Height",GameAgent[2]!=BakeAgent[2],Repr(GameAgent[2]),Repr(BakeAgent[2]));
    return Rows;
}
inline std::string Join(const std::vector<std::string>& Parts, const char* Between) {
    std::string Out;
    for (std::size_t I=0; I<Parts.size(); ++I) { if (I) Out+=Between; Out+=Parts[I]; }
    return Out;
}
}

// The .nav file for one game map. Each stock actor takes the baked actor with its agent radius
// (from HeroBakes for a Hero actor when HasHeroBake); its areas and flags become the game's and
// the game's links are carried over (Convert). An actor no bake has the radius of keeps the
// game's navigation and is left out of the file. Out is written only when the file is complete.
inline bool ExportMap(const MapExportInput& In, MapExport& Out, Problem& Error) {
    using namespace MapDetail;
    const auto Admitted=[&](const std::vector<BakedActor>& Bakes, std::vector<Vec3>& Agents) {
        for (const auto& B:Bakes) { Agents.emplace_back(); if (!Admit(B.Name,B.Mesh,Agents.back(),Error)) return false; }
        return true;
    };
    std::vector<Vec3> StockAgents, BakeAgents, HeroAgents;
    for (const auto& G:In.Stock) { StockAgents.emplace_back(); if (!Admit(G.Name,G.Mesh,StockAgents.back(),Error)) return false; }
    if (!Admitted(In.Bakes,BakeAgents) || (In.HasHeroBake && !Admitted(In.HeroBakes,HeroAgents))) return false;
    if (In.Stock.empty()) { Error={"navigation-map-no-navmesh","The game map has no navmesh"}; return false; }
    const auto& Heroes=In.HasHeroBake ? In.HeroBakes : In.Bakes;
    const auto& HeroSizes=In.HasHeroBake ? HeroAgents : BakeAgents;
    if (In.Bakes.empty() || Heroes.empty()) {
        Error={"navigation-map-no-bake","No RecastNavMesh found in your map. Build navigation (Build > Build Paths) and save before cooking."};
        return false;
    }
    std::map<std::string,BuiltMesh> Built; std::vector<ActorReport> Rows; std::vector<std::string> Problems;
    for (std::size_t I=0; I<In.Stock.size(); ++I) {
        const auto& Game=In.Stock[I]; const auto& Agent=StockAgents[I];
        const bool Hero=IsHero(Game.Name);
        const auto& Pool=Hero ? Heroes : In.Bakes; const auto& Sizes=Hero ? HeroSizes : BakeAgents;
        std::vector<std::size_t> Matches;
        for (std::size_t J=0; J<Pool.size(); ++J) if (Sizes[J][0]==Agent[0]) Matches.push_back(J);
        ActorReport Row; Row.Actor=Game.Name;
        if (Matches.empty()) {
            Row.Reason="no navmesh with agent radius "+G(Agent[0])+" in your bake";
            Rows.push_back(std::move(Row)); continue;
        }
        if (Matches.size()>1) {
            Error={"navigation-map-several-bakes","Your bake has several navmeshes with radius "+G(Agent[0])};
            return false;
        }
        const auto& Bake=Pool[Matches[0]];
        const auto Issues=SettingsProblems(Game.Mesh,Agent,Bake.Mesh,Sizes[Matches[0]]);
        if (!Issues.empty()) {
            Problems.push_back(Game.Name+" (radius "+G(Agent[0])+") differs from your "+Bake.Name+"\n"+Join(Issues,"\n"));
            continue;
        }
        BuiltMesh Result; ConversionReport Report; std::string Code;
        if (!Convert(Game.Mesh,Bake.Mesh,Result,Code,Report,In.CarryStockLinks)) { Error={Code,""}; return false; }
        Row.Replaced=true; Row.Source=Bake.Name; Row.Tiles=Result.Tiles.size();
        for (const auto& T:Result.Tiles) Row.Polygons+=T.Polys.size();
        Row.LinksAdded=Report.LinksAdded; Row.LinksSkipped=std::move(Report.LinksSkipped);
        Rows.push_back(std::move(Row));
        Built.emplace(Game.Name,std::move(Result));
    }
    if (!Problems.empty()) {
        Error={"navigation-map-settings","The navmesh settings do not match the game map. Change these on RecastNavMesh, "
            "rebuild paths and cook again.\n"+Join(Problems,"\n")};
        return false;
    }
    if (Built.empty()) {
        std::set<std::string> Radii;
        for (const auto& A:StockAgents) Radii.insert(G(A[0]));
        Error={"navigation-map-nothing-matched","Nothing matched. Bake an agent with one of the game radii, which are "+
            Join(std::vector<std::string>(Radii.begin(),Radii.end()),", ")};
        return false;
    }
    std::vector<std::uint8_t> Bytes; std::string Code;
    if (!EncodeBuilt(In.Package,Built,Bytes,Code)) { Error={Code,""}; return false; }
    Out.Bytes=std::move(Bytes); Out.Actors=std::move(Rows); Error={}; return true;
}
}}

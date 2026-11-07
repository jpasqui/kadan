// Bounded independently supplied triangle OBJ geometry for a native constructor.
// Source authorship and asset/dependency/cooked admission are separate boundaries.
#pragma once
#include "package_reader.h"
#include <cmath>
#include <locale>
#include <sstream>
#include <map>
namespace P4MapKit { namespace CreatorMeshGeometry {
enum class Coordinates { UnrealCentimeters, WavefrontYUpMeters };
using Vec3=std::array<float,3>;using Vec2=std::array<float,2>;
struct Corner {std::uint32_t Vertex=0,UV=0,Normal=0;};
struct Face {std::array<Corner,3> Corners{};std::uint32_t Material=0;};
struct Source {std::vector<Vec3> Vertices,Normals;std::vector<Vec2> UVs;std::vector<Face> Faces;std::vector<std::string> Materials;};
namespace Detail {
inline bool Slot(const std::string& S) {
    if(S.empty()||S.size()>128) return false;for(const auto C:S) if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-'||C=='.')) return false;return true;
}
inline bool Index(const std::string& Token,std::size_t Count,std::uint32_t& Out) {
    if(Token.empty()||Token.size()>11) return false;std::size_t At=Token[0]=='-'?1:0;if(At==Token.size()) return false;
    std::uint64_t Value=0;for(;At<Token.size();++At) {const auto C=Token[At];if(C<'0'||C>'9') return false;Value=Value*10+std::uint64_t(C-'0');if(Value>Count) return false;}
    if(!Value) return false;Out=std::uint32_t(Token[0]=='-'?Count-Value:Value-1);return true;
}
inline bool CornerIn(const std::string& Token,const Source& S,Corner& Out) {
    const auto A=Token.find('/'),B=A==std::string::npos?A:Token.find('/',A+1);
    if(A==std::string::npos||B==std::string::npos||Token.find('/',B+1)!=std::string::npos) return false;
    return Index(Token.substr(0,A),S.Vertices.size(),Out.Vertex)&&Index(Token.substr(A+1,B-A-1),S.UVs.size(),Out.UV)&&Index(Token.substr(B+1),S.Normals.size(),Out.Normal);
}
inline bool Real(std::istringstream& I,float& Out) {
    double Value=0;if(!(I>>Value)||!std::isfinite(Value)||std::abs(Value)>1000000) return false;
    Out=float(Value);return std::isfinite(Out);
}
inline bool End(std::istringstream& I) {std::string Extra;return !(I>>Extra);}
}
inline bool ReadObj(const std::string& Input,Coordinates Space,Source& Out,std::string& Error) {
    const auto Fail=[&](const char* Why) {Error=Why;return false;};
    if(Input.empty()||Input.size()>16*1024*1024||Input.find('\0')!=std::string::npos ||
       (Space!=Coordinates::UnrealCentimeters&&Space!=Coordinates::WavefrontYUpMeters)) return Fail("creator-mesh-geometry-input-bound-or-space");
    Source Pending;std::map<std::string,std::uint32_t> Slots;std::uint32_t Current=0;
    std::istringstream Lines(Input);Lines.imbue(std::locale::classic());std::string Line;std::size_t Number=0;
    while(std::getline(Lines,Line)) {
        if(++Number>1000000||Line.size()>4096) return Fail("creator-mesh-geometry-line-bound");
        const auto Comment=Line.find('#');if(Comment!=std::string::npos) Line.resize(Comment);
        std::istringstream I(Line);I.imbue(std::locale::classic());std::string Tag;if(!(I>>Tag)) continue;
        if(Tag=="v"||Tag=="vn") {
            Vec3 V{};for(auto& C:V) if(!Detail::Real(I,C)) return Fail("creator-mesh-geometry-nonfinite-coordinate");
            if(!Detail::End(I)) return Fail("creator-mesh-geometry-coordinate-arity");
            if(Space==Coordinates::WavefrontYUpMeters) {std::swap(V[1],V[2]);if(Tag=="v") for(auto& C:V) C*=100;}
            if(Tag=="vn") {double Length=0;for(const auto C:V) Length+=double(C)*C;if(std::abs(Length-1)>0.001) return Fail("creator-mesh-geometry-normal-not-unit");}
            auto& Values=Tag=="v"?Pending.Vertices:Pending.Normals;if(Values.size()>=200000) return Fail("creator-mesh-geometry-vertex-bound");Values.push_back(V);
        } else if(Tag=="vt") {
            Vec2 V{};for(auto& C:V) if(!Detail::Real(I,C)) return Fail("creator-mesh-geometry-invalid-uv");
            if(!Detail::End(I)||Pending.UVs.size()>=600000) return Fail("creator-mesh-geometry-uv-arity-or-bound");Pending.UVs.push_back(V);
        } else if(Tag=="usemtl") {
            std::string Name;if(!(I>>Name)||!Detail::End(I)||!Detail::Slot(Name)) return Fail("creator-mesh-geometry-invalid-material-slot");
            const auto Found=Slots.find(Name);if(Found!=Slots.end()) Current=Found->second;
            else {if(Slots.size()>=256) return Fail("creator-mesh-geometry-slot-bound");Current=std::uint32_t(Slots.size());Slots.emplace(Name,Current);Pending.Materials.push_back(Name);}
        } else if(Tag=="f") {
            if(Pending.Materials.empty()||Pending.Faces.size()>=200000) return Fail("creator-mesh-geometry-face-or-slot-bound");
            Face F;F.Material=Current;for(auto& C:F.Corners) {std::string Token;if(!(I>>Token)||!Detail::CornerIn(Token,Pending,C)) return Fail("creator-mesh-geometry-corner-index");}
            if(!Detail::End(I)) return Fail("creator-mesh-geometry-requires-triangulated-faces");
            if(Space==Coordinates::WavefrontYUpMeters) std::swap(F.Corners[1],F.Corners[2]);
            const auto& A=Pending.Vertices[F.Corners[0].Vertex];const auto& B=Pending.Vertices[F.Corners[1].Vertex];const auto& C=Pending.Vertices[F.Corners[2].Vertex];
            std::array<double,3> Cross{};for(std::size_t Axis=0;Axis<3;++Axis) {const auto J=(Axis+1)%3,K=(Axis+2)%3;Cross[Axis]=(double(B[J])-A[J])*(double(C[K])-A[K])-(double(B[K])-A[K])*(double(C[J])-A[J]);}
            double Area=0;for(const auto V:Cross) Area+=V*V;if(!std::isfinite(Area)||Area<=1e-12) return Fail("creator-mesh-geometry-degenerate-triangle");
            for(const auto& Corner:F.Corners) {double Dot=0;for(std::size_t Axis=0;Axis<3;++Axis) Dot+=Cross[Axis]*Pending.Normals[Corner.Normal][Axis];if(Dot<=0) return Fail("creator-mesh-geometry-normal-winding-disagrees");}
            Pending.Faces.push_back(F);
        } else if(Tag=="o"||Tag=="g"||Tag=="s") {
            // Organizational/smoothing labels do not override explicit per-corner
            // normals; retain their original bytes in the independent source hash.
            std::string Value;while(I>>Value) if(Value.size()>128) return Fail("creator-mesh-geometry-label-bound");
        } else return Fail("creator-mesh-geometry-directive-unqualified");
    }
    if(Pending.Faces.empty()) return Fail("creator-mesh-geometry-empty");
    std::vector<bool> Used(Pending.Materials.size(),false);for(const auto& F:Pending.Faces) Used[F.Material]=true;
    if(std::find(Used.begin(),Used.end(),false)!=Used.end()) return Fail("creator-mesh-geometry-unused-material-slot");
    Out=std::move(Pending);Error.clear();return true;
}
}}

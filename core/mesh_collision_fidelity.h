// Exact triangle/material comparison between native source collision geometry
// and the independently decoded CPU PhysX cook. This helper issues no custody.
#pragma once
#include "physics_body.h"
namespace P4MapKit { namespace MeshCollisionFidelity {
struct Face {std::array<PhysicsBody::Vec3,3> Points{};std::uint16_t Material=0;};
using Key=std::array<std::uint32_t,10>;
inline Key MakeKey(const Face& F) {
    Key Best{};bool Set=false;
    // Cooking may rotate a triangle's first index, but may not change winding.
    for(unsigned Rotate=0;Rotate<3;++Rotate) {
        Key K{};for(unsigned P=0;P<3;++P) for(unsigned C=0;C<3;++C) std::memcpy(&K[P*3+C],&F.Points[(P+Rotate)%3][C],4);
        K[9]=F.Material;if(!Set||K<Best) {Best=K;Set=true;}
    }return Best;
}
inline bool Match(const std::vector<Face>& Expected,const PhysicsBody::Cook& Actual,std::string& Error) {
    const auto Fail=[&](const char* Why) {Error=Why;return false;};
    if(Expected.empty()||Expected.size()>200000||Actual.TriMeshes.size()!=1||Actual.TriMeshes[0].Triangles.size()!=Expected.size()) return Fail("mesh-collision-source-census-differs");
    const auto& Mesh=Actual.TriMeshes[0];if(!Mesh.Materials.empty()&&Mesh.Materials.size()!=Mesh.Triangles.size()) return Fail("mesh-collision-material-census-differs");
    std::vector<Key> Wanted,Read;Wanted.reserve(Expected.size());Read.reserve(Expected.size());
    for(const auto& F:Expected) {for(const auto& P:F.Points) for(float V:P) if(!std::isfinite(V)) return Fail("mesh-collision-source-nonfinite");Wanted.push_back(MakeKey(F));}
    for(std::size_t T=0;T<Mesh.Triangles.size();++T) {
        Face F;F.Material=Mesh.Materials.empty()?0:Mesh.Materials[T];
        for(unsigned P=0;P<3;++P) {const auto Index=Mesh.Triangles[T][P];if(Index>=Mesh.Vertices.size()) return Fail("mesh-collision-cooked-index-invalid");F.Points[P]=Mesh.Vertices[Index];}
        Read.push_back(MakeKey(F));
    }
    std::sort(Wanted.begin(),Wanted.end());std::sort(Read.begin(),Read.end());
    if(Wanted!=Read) return Fail("mesh-collision-source-geometry-material-or-winding-differs");Error.clear();return true;
}
}}

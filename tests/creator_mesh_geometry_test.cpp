#include "../core/creator_mesh_geometry.h"
#include <iostream>
#include <cstdlib>
using namespace P4MapKit::CreatorMeshGeometry;
static std::size_t Checks=0;static void Check(bool V) {++Checks;if(!V) {std::cerr<<"creator geometry failed "<<Checks<<'\n';std::exit(1);}}
int main() {
    const std::string Input="v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nusemtl Own\nf 1/1/1 2/2/1 3/3/1\n";
    Source S;std::string Error;Check(ReadObj(Input,Coordinates::UnrealCentimeters,S,Error));Check(S.Vertices.size()==3 && S.Faces.size()==1 && S.Materials[0]=="Own");
    Check(ReadObj(Input,Coordinates::WavefrontYUpMeters,S,Error));Check(S.Vertices[2]==Vec3({0,0,100}));Check(S.Normals[0]==Vec3({0,1,0}));Check(S.Faces[0].Corners[1].Vertex==2);
    const auto Refuse=[&](const std::string& Text) {Source Kept=S;Check(!ReadObj(Text,Coordinates::UnrealCentimeters,Kept,Error));Check(!Error.empty());Check(Kept.Faces.size()==S.Faces.size() && Kept.Vertices==S.Vertices);};
    Refuse("");Refuse(Input+"mtllib external.mtl\n");Refuse(Input+"v nan 0 0\n");Refuse(Input+"vn 0 0 0\n");Refuse(Input+"v 0 0 0 1\n");Refuse(Input+"vt 0 0 0\n");Refuse(Input+"usemtl unused\n");Refuse(Input+"usemtl /Game/Stock.Material\n");Refuse(Input+"f 1/1/1 2/2/1 3/3/1 1/1/1\n");
    for(const auto Tail:{"f 0/1/1 2/2/1 3/3/1\n","f 4/1/1 2/2/1 3/3/1\n","f 1//1 2/2/1 3/3/1\n","f 1/1/2 2/2/1 3/3/1\n","f 1/1/1 3/3/1 2/2/1\n","f 1/1/1 1/2/1 3/3/1\n"}) Refuse(Input+Tail);
    Refuse(Input+std::string("v 1\0 0 0\n",10));Refuse(Input+std::string(4097,'#'));
    Check(ReadObj(Input+"f -3/-3/-1 -2/-2/-1 -1/-1/-1\n",Coordinates::UnrealCentimeters,S,Error));Check(S.Faces.size()==2);
    for(unsigned I=0;I<1000;++I) {const auto Changed=Input+"f "+std::to_string(4+I)+"/1/1 2/2/1 3/3/1\n";Refuse(Changed);}
    std::cout<<"creator mesh geometry: "<<Checks<<" checks passed\n";return 0;
}

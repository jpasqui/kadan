#include "../core/mesh_collision_fidelity.h"
#include <cstdlib>
#include <iostream>
using namespace P4MapKit;using namespace MeshCollisionFidelity;
static unsigned Checks=0;static void Check(bool V) {++Checks;if(!V) {std::cerr<<"collision fidelity failed "<<Checks<<'\n';std::exit(1);}}
int main() {
    Face A;A.Points={{{{0,0,0}},{{1,0,0}},{{0,1,0}}}};A.Material=7;Face B=A;for(auto& P:B.Points) P[2]=2;B.Material=3;
    PhysicsBody::Cook C;C.TriMeshes.resize(1);auto& M=C.TriMeshes[0];for(const auto& P:A.Points) M.Vertices.push_back(P);for(const auto& P:B.Points) M.Vertices.push_back(P);
    M.Triangles={{{5,3,4}},{{1,2,0}}};M.Materials={3,7};std::string Error;
    Check(Match({A,B},C,Error));Check(Error.empty());Check(Match({B,A},C,Error));
    auto Bad=C;std::swap(Bad.TriMeshes[0].Triangles[0][0],Bad.TriMeshes[0].Triangles[0][1]);Check(!Match({A,B},Bad,Error));
    Bad=C;Bad.TriMeshes[0].Materials[0]=4;Check(!Match({A,B},Bad,Error));Bad=C;Bad.TriMeshes[0].Vertices[0][0]=-0.0f;Check(!Match({A,B},Bad,Error));
    Bad=C;Bad.TriMeshes[0].Vertices[0][0]=0.01f;Check(!Match({A,B},Bad,Error));Bad=C;Bad.TriMeshes[0].Triangles[0][0]=123;Check(!Match({A,B},Bad,Error));
    Bad=C;Bad.TriMeshes[0].Materials.clear();Check(!Match({A,B},Bad,Error));A.Material=B.Material=0;Check(Match({A,B},Bad,Error));
    Bad=C;Bad.TriMeshes.push_back(M);Check(!Match({A,B},Bad,Error));Check(!Match({},C,Error));
    A.Points[0][0]=std::numeric_limits<float>::infinity();Check(!Match({A,B},C,Error));
    A.Points[0][0]=0;A.Material=7;B.Material=3;Check(Match({A,B},C,Error));
    for(unsigned I=0;I<1000;++I) {Bad=C;Bad.TriMeshes[0].Vertices[1][0]+=float(I+1);Check(!Match({A,B},Bad,Error));}
    std::cout<<"mesh collision fidelity: "<<Checks<<" checks passed\n";return 0;
}

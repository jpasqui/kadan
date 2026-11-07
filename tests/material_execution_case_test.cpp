// A game material instance whose object differs from its package's leaf in case only is read with the object's
// own spelling: MI_chr_Nightkochappy_00_M00 holds MI_chr_NightKochappy_00_M00. The package path itself must
// still be the exact one, and a different name still refuses.
#include "../core/material_execution_resources.h"
#include <fstream>
#include <iostream>
namespace M=P4MapKit::Material;namespace C=P4MapKit::CustomMaterial;
static int Checks=0;
static void Check(bool Value,const char* Why){++Checks;if(!Value)throw std::runtime_error(Why);}
template<class F>static void Refuses(F Action,const char* Why){bool Held=false;try{Action();}catch(const M::Refused&){Held=true;}Check(Held,Why);}
static C::Bytes File(const std::string& Path)
{
    std::ifstream In(Path,std::ios::binary);if(!In)throw std::runtime_error("missing explicit source "+Path);In.seekg(0,std::ios::end);
    const auto Size=In.tellg();if(Size<=0||std::uint64_t(Size)>64u*1024u*1024u)throw std::runtime_error("source size bound");
    In.seekg(0);C::Bytes Result(std::size_t(Size),0);if(!In.read(reinterpret_cast<char*>(Result.data()),Size))throw std::runtime_error("source read");return Result;
}
int main(int Argc,char** Argv){try{
    if(Argc!=2)throw std::runtime_error("one explicit source root required");
    const std::string Package="/Game/Carrot4/Characters/Teki/Kochappy/MI_chr_Nightkochappy_00_M00";
    const auto Stem=std::string(Argv[1])+"/Carrot4/Content/"+Package.substr(6);
    const M::MaterialOriginalImportRawAsset Raw{std::make_shared<const C::Bytes>(File(Stem+".uasset")),std::make_shared<const C::Bytes>(File(Stem+".uexp"))};
    const auto Read=M::ReadOriginalMaterialExecutionResources(Raw,Package);
    Check(Read.Package==Package,"the exact package");
    Check(Read.Object==Package+".MI_chr_NightKochappy_00_M00","the object keeps its own spelling");
    Check(Read.NativeClass=="/Script/Engine.MaterialInstanceConstant"&&!Read.Resources.empty()&&
        Read.Resources.front().Kind==M::EMaterialExecutionResourceKind::Parent,"an instance with its parent");
    auto Alias=Package;Alias[1]='g';Refuses([&]{M::ReadOriginalMaterialExecutionResources(Raw,Alias);},"a package path in another case still refuses");
    Check(M::SameObjectName("MI_a_Stem","mi_A_stem")&&!M::SameObjectName("MI_a_Stem","MI_a_Stem2")&&
        !M::SameObjectName("MI_a_Stem","MI_b_Stem"),"names match in case only");
    std::cout<<"material execution case: "<<Checks<<" checks passed\n";return 0;
}catch(const std::exception& Error){std::cerr<<Error.what()<<"\n";return 1;}}

#include "../core/material_execution_resources.h"
#include <fstream>
#include <iostream>
namespace M=P4MapKit::Material;namespace C=P4MapKit::CustomMaterial;
static int Checks=0;
static void Check(bool Value,const char* Why){++Checks;if(!Value)throw std::runtime_error(Why);}
template<class F>static void Refuses(F Action,const char* Why){bool Held=false;try{Action();}catch(const M::Refused&){Held=true;}Check(Held,Why);}
template<class F>static void FailureKind(F Action,bool Unsupported,const char* Why)
{bool Held=false,Kind=false;try{Action();}catch(const M::MaterialExecutionUnsupported&){Held=true;Kind=true;}catch(const M::Refused&){Held=true;}Check(Held&&Kind==Unsupported,Why);}
static C::Bytes File(const std::string& Path)
{
    std::ifstream In(Path,std::ios::binary);if(!In)throw std::runtime_error("missing explicit source");In.seekg(0,std::ios::end);
    const auto Size=In.tellg();if(Size<=0||std::uint64_t(Size)>64u*1024u*1024u)throw std::runtime_error("source size bound");
    In.seekg(0);C::Bytes Result(std::size_t(Size),0);if(!In.read(reinterpret_cast<char*>(Result.data()),Size))throw std::runtime_error("source read");return Result;
}
int main(int Argc,char** Argv){try{
    if(Argc!=3)throw std::runtime_error("explicit source root and one JSON receipt required");
    const std::vector<std::string> Packages={
        "/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master","/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow",
        "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve","/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow",
        "/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master","/Game/Carrot4/Characters/Teki/Dodoro/MI_chr_Dodoro_00_Silhouette",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Object_00","/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Player_00",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Teki_00","/Game/Carrot4/MasterMaterial/Texture/T_Base_M"};
    std::map<std::string,M::MaterialOriginalImportRawAsset> Sources;
    for(const auto& Package:Packages){const auto Stem=std::string(Argv[1])+"/Carrot4/Content/"+Package.substr(6);
        Sources.emplace(Package,M::MaterialOriginalImportRawAsset{std::make_shared<const C::Bytes>(File(Stem+".uasset")),std::make_shared<const C::Bytes>(File(Stem+".uexp"))});}
    auto Report=M::Object({{"sources",M::Json::MakeArray()}});std::size_t Masters=0,Instances=0,Overrides=0;
    for(std::size_t Index=0;Index+1<Packages.size();++Index)
    {
        const auto& Package=Packages[Index];const auto Read=M::ReadOriginalMaterialExecutionResources(Sources.at(Package),Package);
        Check(Read.Package==Package&&Read.Object==Package+"."+Package.substr(Package.find_last_of('/')+1)&&Read.SourceSha256.size()==64,"exact actual canonical root/digest");
        auto Row=M::Object({{"package",Package},{"native_class",Read.NativeClass},{"source_sha256",Read.SourceSha256},{"resources",M::Json::MakeArray()}});
        if(Read.NativeClass=="/Script/Engine.Material"){
            ++Masters;for(const auto& Edge:Read.Resources)Check(Edge.Kind==M::EMaterialExecutionResourceKind::ParameterCollection,
                "master prerequisites are typed collections, never cached defaults/functions");}
        else
        {
            ++Instances;Check(!Read.Resources.empty()&&Read.Resources.front().Kind==M::EMaterialExecutionResourceKind::Parent,"MIC retains its actual structural Parent");
            for(const auto& Edge:Read.Resources)
            {
                Check(Edge.ImportIndex<0&&Edge.Object.find(Edge.Package+".")==0&&!Edge.Property.empty(),"actual hard source import/property evidence");
                if(Edge.Kind==M::EMaterialExecutionResourceKind::TextureOverride){++Overrides;Check(!Edge.Parameter.empty()&&Edge.Property.find("TextureParameterValues[")==0,"only explicit local texture override arrays");}
            }
        }
        for(const auto& Edge:Read.Resources)Row["resources"].array.push_back(M::Object({{"kind",Edge.Kind==M::EMaterialExecutionResourceKind::Parent?"Parent":
            Edge.Kind==M::EMaterialExecutionResourceKind::ParameterCollection?"ParameterCollection":"TextureOverride"},
            {"package",Edge.Package},{"object",Edge.Object},{"native_class",Edge.NativeClass},{"parameter",Edge.Parameter},{"property",Edge.Property},{"import_index",Edge.ImportIndex}}));
        Report["sources"].array.push_back(Row);
    }
    Check(Masters==2&&Instances==8,"all ten actual family material sources verified without shaders");
    const auto Master=Packages.front(),Instance=Packages.at(1);const auto& Raw=Sources.at(Instance);
    Refuses([&]{M::ReadOriginalMaterialExecutionResources(Raw,Master);},"wrong package input refuses");
    auto Alias=Instance;Alias[1]='g';Refuses([&]{M::ReadOriginalMaterialExecutionResources(Raw,Alias);},"noncanonical case alias refuses");
    Refuses([&]{M::ReadOriginalMaterialExecutionResources(Sources.at(Packages.back()),Packages.back());},"Texture source cannot authorize master dependency deferral");
    auto Broken=std::make_shared<C::Bytes>(*Raw.Header);Broken->at(0)^=255;
    Refuses([&]{M::ReadOriginalMaterialExecutionResources({Broken,Raw.Body},Instance);},"corrupt source never authorizes deferral");
    const auto Custody=M::ReadOriginalMaterialImportCustody(Raw,Instance);P4MapKit::FMaterialCookedRecord Record;P4MapKit::FReadError Error;
    Check(P4MapKit::ReadMaterialCookedRecord(Custody.Asset.Package,*Raw.Body,Record,Error),"controls use actual instance properties");
    auto Soft=Record;for(auto& Field:Soft.Properties.Fields)if(Field.first=="Parent")Field.second.Kind=P4MapKit::FDecodedValue::EKind::SoftPath;
    FailureKind([&]{M::MaterialExecutionResourceDetail::Instance(Custody,Soft);},true,"soft Parent is typed unsupported, not corrupt-source classification");
    auto Wrong=Record;for(auto& Field:Wrong.Properties.Fields)if(Field.first=="Parent")Field.second.Text="/Game/Other.Other";
    FailureKind([&]{M::MaterialExecutionResourceDetail::Instance(Custody,Wrong);},false,"changed text/import disagreement is invalid source, not an unsupported asset waiver");
    auto Duplicate=Record;Duplicate.Properties.Fields.push_back({"Parent",*Record.Properties.Field("Parent")});
    FailureKind([&]{M::MaterialExecutionResourceDetail::Instance(Custody,Duplicate);},false,"ambiguous source Parent fields refuse invalid");
    auto NoParent=Record;auto& Fields=NoParent.Properties.Fields;
    Fields.erase(std::remove_if(Fields.begin(),Fields.end(),[](const std::pair<std::string,P4MapKit::FDecodedValue>& Field){return Field.first=="Parent";}),Fields.end());
    FailureKind([&]{M::MaterialExecutionResourceDetail::Instance(Custody,NoParent);},true,"a valid MIC with no authored Parent is unsupported, not raw corruption");
    auto NullRecord=Record;
    P4MapKit::FDecodedValue Info,Row,Rows;Info.Kind=Row.Kind=P4MapKit::FDecodedValue::EKind::Object;
    Info.Fields.push_back({"Name",P4MapKit::FDecodedValue::Atom(P4MapKit::FDecodedValue::EKind::Name,"ExplicitNull")});
    Row.Fields.push_back({"ParameterInfo",Info});Row.Fields.push_back({"ParameterValue",P4MapKit::FDecodedValue::Atom(P4MapKit::FDecodedValue::EKind::HardReference,"")});
    Rows.Kind=P4MapKit::FDecodedValue::EKind::Array;Rows.Items.push_back(Row);
    auto& NullFields=NullRecord.Properties.Fields;NullFields.erase(std::remove_if(NullFields.begin(),NullFields.end(),[](const auto& f){return f.first=="TextureParameterValues";}),NullFields.end());NullFields.push_back({"TextureParameterValues",Rows});
    const auto NullProjection=M::ProjectOriginalMaterialInstanceForImport(NullRecord);
    Check(NullProjection.At("textures").At("ExplicitNull").kind==M::Json::Null,"explicit null projects as null, not omitted or an asset");
    Check(M::MaterialExecutionResourceDetail::Instance(Custody,NullRecord).size()==1,"explicit null requests no asset but preserves Parent");
    NullFields.back().second.Items[0].Fields.back().second.Text="/Game/Invented.Invented";
    Refuses([&]{M::MaterialExecutionResourceDetail::Instance(Custody,NullRecord);},"zero source index with nonempty invented reference refuses");
    const auto Before=M::OriginalMaterialImportTypedWitness(Record.Properties).Dump();
    const auto Edges=M::MaterialExecutionResourceDetail::Instance(Custody,Record);
    Check(Before==M::OriginalMaterialImportTypedWitness(Record.Properties).Dump()&&!Edges.empty(),"edge projection preserves every raw typed field");
    Report["passed"]=true;Report["checks"]=Checks;Report["masters"]=double(Masters);Report["instances"]=double(Instances);Report["explicit_texture_overrides"]=double(Overrides);
    Report["scope"]="Initial source execution edges only; no archive, graph traces, native admission or metadata deletion";
    const auto Text=Report.Dump();std::ofstream Out(Argv[2],std::ios::binary);if(!Out.write(Text.data(),std::streamsize(Text.size())))throw std::runtime_error("receipt write");
    std::cout<<"PASS "<<Checks<<" checks / 10 actual materials / "<<Overrides<<" explicitly authored texture edges / zero shader traces\n";return 0;
}catch(const std::exception& Error){std::cerr<<Error.what()<<"\n";return 1;}}

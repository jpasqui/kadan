#include "../core/material_collection_source.h"
#include <fstream>
#include <iostream>
using namespace P4MapKit;using namespace P4MapKit::Material;
static int Checks=0;
static void Need(bool V,const char* S){++Checks;if(!V)throw std::runtime_error(S);}
static std::vector<std::uint8_t> Bytes(const std::string& P){std::ifstream F(P,std::ios::binary);if(!F)throw std::runtime_error("missing input "+P);return {std::istreambuf_iterator<char>(F),std::istreambuf_iterator<char>()};}
template<class F>static void Refuses(F Run){bool Refused=false;try{Run();}catch(const Material::Refused&){Refused=true;}Need(Refused,"negative source control accepted");}
int main(int Argc,char** Argv){try{
    if(Argc!=2)throw std::runtime_error("explicit retained source root required");
    for(const char* Name:{"MPC_Env","MPC_Camera"}){
        const std::string P=std::string("/Game/Carrot4/MasterMaterial/ParameterCollections/")+Name;
        const auto Stem=std::string(Argv[1])+"/Carrot4/Content/"+P.substr(6);const auto H=Bytes(Stem+".uasset"),B=Bytes(Stem+".uexp");
        const auto S=ReadMaterialCollectionSource(H,B,P);Need(S.Package==P,"source package");Need(S.Object==P+'.'+Name,"source object");
        Need(MaterialCollectionStateHex(S.State).size()==32,"state GUID encoding");
        Refuses([&]{ReadMaterialCollectionSource(H,B,P+"_wrong");});auto Bad=B;Bad.back()^=1;Refuses([&]{ReadMaterialCollectionSource(H,Bad,P);});
        // The package footer or actual serialized suffix must not be silently discarded.
        Refuses([&]{auto Cut=B;Cut.resize(1);ReadMaterialCollectionSource(H,Cut,P);});
        const auto Inventory=ReadPackage(H,B);FPropertyDecoder D(Inventory,B);FDecodedValue Props;auto At=Inventory.Exports[0].Offset;
        Need(D.TypedProperties(At,Inventory.Exports[0].Offset+Inventory.Exports[0].Size,Props),"typed source properties");
        const auto Good=CollectionSourceDetail::Project(Props,P);Need(Good.State==S.State,"independent projection state");
        auto Extra=Props;Extra.Fields.push_back({"UnknownNativeField",FDecodedValue()});Refuses([&]{CollectionSourceDetail::Project(Extra,P);});
        for(const std::string Field:{"StateId","ScalarParameters","VectorParameters"}){auto Missing=Props;Missing.Fields.erase(std::remove_if(Missing.Fields.begin(),Missing.Fields.end(),[&](const std::pair<std::string,FDecodedValue>& F){return F.first==Field;}),Missing.Fields.end());Refuses([&]{CollectionSourceDetail::Project(Missing,P);});}
        auto Invalid=Props;for(auto& F:Invalid.Fields)if(F.first=="StateId")for(auto& W:F.second.Fields)W.second.Text="0";Refuses([&]{CollectionSourceDetail::Project(Invalid,P);});
        const auto Tags=ReadPropertyEnvelopes(B,Inventory.Names,Inventory.Exports[0].Offset,Inventory.Exports[0].Offset+Inventory.Exports[0].Size);
        auto WrongTag=CollectionSourceDetail::Field(Tags,"StateId");WrongTag.Detail[0]="Vector4";Refuses([&]{CollectionSourceDetail::Tag(WrongTag,"StateId","StructProperty","Guid");});
        for(const std::string Field:{"ScalarParameters","VectorParameters"}){
            auto It=std::find_if(Props.Fields.begin(),Props.Fields.end(),[&](const std::pair<std::string,FDecodedValue>& F){return F.first==Field;});
            if(It!=Props.Fields.end()&&!It->second.Items.empty()){
                auto Duplicate=Props;for(auto& F:Duplicate.Fields)if(F.first==Field){const auto Row=F.second.Items[0];F.second.Items.push_back(Row);}Refuses([&]{CollectionSourceDetail::Project(Duplicate,P);});
                auto Missing=Props;for(auto& F:Missing.Fields)if(F.first==Field)F.second.Items[0].Fields.pop_back();Refuses([&]{CollectionSourceDetail::Project(Missing,P);});
                auto Name=Props;for(auto& F:Name.Fields)if(F.first==Field)for(auto& R:F.second.Items[0].Fields)if(R.first=="ParameterName")R.second.NameBase.clear();Refuses([&]{CollectionSourceDetail::Project(Name,P);});
                auto Type=Props;for(auto& F:Type.Fields)if(F.first==Field)for(auto& R:F.second.Items[0].Fields)if(R.first=="DefaultValue"){
                    if(Field=="ScalarParameters")R.second.Type="DoubleProperty";else R.second.Fields[0].second.Type="FloatProperty";}
                Refuses([&]{CollectionSourceDetail::Project(Type,P);});
                const auto& Array=CollectionSourceDetail::Field(Tags,Field.c_str());FByteReader Reader(B,Array.PayloadOffset,Array.PayloadOffset+Array.Size);std::size_t Count=0;FPropertyEnvelope Inner;
                Need(Reader.Count(Count)&&ReadPropertyEnvelope(Reader,Inventory.Names,B.size(),Inner)&&Count>0,"actual array native descriptor");
                auto Shape=ReadPropertyEnvelopes(B,Inventory.Names,Inner.PayloadOffset,Inner.PayloadOffset+Inner.Size);CollectionSourceDetail::ParameterShape(Shape,Field=="VectorParameters");
                for(auto& F:Shape.Properties)if(F.Name=="DefaultValue")F.Type="DoubleProperty";Refuses([&]{CollectionSourceDetail::ParameterShape(Shape,Field=="VectorParameters");});
                auto Nonfinite=Props;for(auto& F:Nonfinite.Fields)if(F.first==Field)for(auto& R:F.second.Items[0].Fields)if(R.first=="DefaultValue"){
                    if(Field=="ScalarParameters")R.second.Text="nan";else R.second.Fields[0].second.Text="nan";}
                Refuses([&]{CollectionSourceDetail::Project(Nonfinite,P);});
            }}
        std::cout<<Name<<" scalars="<<S.Scalars.size()<<" vectors="<<S.Vectors.size()<<" state="<<MaterialCollectionStateHex(S.State)<<"\n";
    }std::cout<<"PASS "<<Checks<<" source controls\n";return 0;
}catch(const std::exception& E){std::cerr<<E.what()<<"\n";return 1;}}

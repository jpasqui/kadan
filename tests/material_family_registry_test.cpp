#include "../core/material_family_registry.h"
#include <array>
#include <iostream>
using namespace P4MapKit::Material;
#define CHECK(X) do { if(!(X)){std::cerr<<"failed line "<<__LINE__<<": "<<#X<<'\n';return 1;} } while(false)

static std::string Row(const std::string& Package,const std::string& Class,const std::string& Parent="",const std::string& Extra="")
{
    const auto Name=Package.substr(Package.find_last_of('/')+1);
    return "AssetData {\n    object_path: \""+Package+"."+Name+"\",\n    asset_class: \""+Class+"\",\n    package_name: \""+Package+"\",\n    tags: [],\n    legacy_tags: [\n"+
        (Parent.empty()?"":"        (\n            \"Parent\",\n            \""+Parent+"\",\n        ),\n")+Extra+"    ],\n}\n";
}
static std::string Parent(const std::string& Package,const std::string& Class="Material")
{return Class+"'"+Package+"."+Package.substr(Package.find_last_of('/')+1)+"'";}
static std::string Input(const std::string& Rows,std::size_t Count)
{return "Asset Registry\n  Registry Version: AddedDependencyFlags\n  Assets: "+std::to_string(Count)+"\n\n"+Rows;}
static bool Parse(const std::string& Text,FMaterialParentRegistry& Registry,std::size_t Chunk=37)
{
    for(std::size_t At=0;At<Text.size();At+=Chunk)
        if(!Registry.Feed(reinterpret_cast<const std::uint8_t*>(Text.data()+At),std::min(Chunk,Text.size()-At)))return false;
    return Registry.Finish();
}
static std::string Replace(std::string Text,const std::string& From,const std::string& To)
{const auto At=Text.find(From);if(At!=std::string::npos)Text.replace(At,From.size(),To);return Text;}
static bool Refuses(const std::string& Text,const std::string& Error)
{FMaterialParentRegistry R;return !Parse(Text,R)&&R.Error()==Error&&!R.Valid();}
static int Controls()
{
    const std::string Master="/Game/M/Master",A="/Game/I/A",B="/Game/Else/B",C="/Game/C/Child";
    const auto Root=Row(Master,"Material"),First=Row(A,"MaterialInstanceConstant",Parent(Master));
    const auto Second=Row(B,"MaterialInstanceConstant",Parent(Master));
    const auto Child=Row(C,"MaterialInstanceConstant",Parent(A,"MaterialInstanceConstant"));
    const auto Good=Input(Child+Root+Second+First,4);
    for(std::size_t Chunk=1;Chunk<=64;++Chunk)
    {
        FMaterialParentRegistry R;CHECK(Parse(Good,R,Chunk));CHECK(R.Valid());CHECK(R.AssetCount()==4);CHECK(R.MaterialInstanceCount()==3);
        FMaterialRegistryFamily F;std::string Why;CHECK(R.Family(C,F,Why));CHECK(Why.empty());CHECK(F.Master.Package==Master);CHECK(F.Members.size()==4);
        CHECK(F.Members[0].Package==C);CHECK(F.Members[1].Package==B);CHECK(F.Members[2].Package==A);CHECK(F.Members[3].Package==Master);
        CHECK(R.Family("/game/m/master",F,Why));CHECK(R.Family(Master+".Master",F,Why));
        CHECK(!R.Family("/Game/Missing",F,Why));CHECK(F.Members.empty()&&F.Master.Package.empty());
        CHECK(R.Finish());CHECK(!R.Feed(nullptr,0));CHECK(!R.Valid());
    }
    // Input order/chunking/CRLF are not ancestry or family order.
    FMaterialParentRegistry Ordered,CrLf;CHECK(Parse(Input(Root+First+Child+Second,4),Ordered));
    auto Windows=Good;std::size_t At=0;while((At=Windows.find('\n',At))!=std::string::npos){Windows.insert(At,"\r");At+=2;}
    CHECK(Parse(Windows,CrLf));FMaterialRegistryFamily OF,WF;std::string Why;CHECK(Ordered.Family(A,OF,Why)&&CrLf.Family(B,WF,Why));
    for(std::size_t I=0;I<OF.Members.size();++I)CHECK(OF.Members[I].Object==WF.Members[I].Object);
    CHECK(Refuses(Replace(Good,"AddedDependencyFlags","UnknownVersion"),"material-registry-version-unsupported"));
    CHECK(Refuses(Replace(Good,"Assets: 4","Assets: 3"),"material-registry-count-exceeded"));
    CHECK(Refuses(Replace(Good,"Assets: 4","Assets: 5"),"material-registry-incomplete-census"));
    CHECK(Refuses(Replace(Good,"Assets: 4","Assets: 1000001"),"material-registry-count-bound"));
    CHECK(Refuses(Good.substr(0,Good.size()-2),"material-registry-incomplete-census"));
    CHECK(Refuses(Input(Row(A,"MaterialInstanceConstant"),1),"material-registry-parent-invalid"));
    CHECK(Refuses(Replace(Input(First,1),"\""+Parent(Master)+"\",","None,"),"material-registry-parent-duplicate-or-invalid"));
    CHECK(Refuses(Input(Row(A,"MaterialInstanceConstant","None"),1),"material-registry-parent-invalid"));
    CHECK(Refuses(Input(Row(A,"MaterialInstanceConstant","Material'/Game/M/Master'"),1),"material-registry-parent-invalid"));
    CHECK(Refuses(Input(Row(A,"MaterialInstanceConstant","Blueprint'/Game/M/Master.Master'"),1),"material-registry-parent-invalid"));
    CHECK(Refuses(Input(Row(A,"MaterialInstanceConstant","Material'/Game/../Master.Master'"),1),"material-registry-parent-invalid"));
    const auto Tag="        (\n            \"Parent\",\n            \""+Parent(Master)+"\",\n        ),\n";
    CHECK(Refuses(Input(Row(A,"MaterialInstanceConstant",Parent(Master),Tag),1),"material-registry-parent-duplicate-or-invalid"));
    CHECK(Refuses(Replace(Input(First,1),"        (\n",""),"material-registry-tag-entry-invalid"));
    CHECK(Refuses(Replace(Input(First,1),"    legacy_tags: [","    legacy_tags: None,"),"material-registry-tags-format-invalid"));
    CHECK(Refuses(Replace(Input(First,1),"            \"Parent\",","            None,"),"material-registry-tag-key-invalid"));
    CHECK(Refuses(Input(Row(A,"UnknownClass","Material'/Game/Broken'"),1),"material-registry-parent-invalid"));
    for(const char* Wrapper:{"material","MATERIAL","materialinstanceconstant","Materialinterface"," Material"})
        CHECK(Refuses(Input(Row(A,"UnknownClass",Parent(Master,Wrapper)),1),"material-registry-parent-invalid"));
    CHECK(Refuses(Input(Row(Master,"Material",Parent(Master)),1),"material-registry-root-has-parent"));
    CHECK(Refuses(Input(Root+Root,2),"material-registry-object-duplicate-or-case-alias"));
    CHECK(Refuses(Input(Root+Row("/game/m/master","Material"),2),"material-registry-package-case-alias"));
    CHECK(Refuses(Replace(Input(Root,1),"package_name:","asset_class:"),"material-registry-field-duplicate"));
    CHECK(Refuses(Replace(Input(Root,1),"package_name: \"/Game/M/Master\"","package_name: \"/Game/Other\""),"material-registry-object-identity-invalid"));
    CHECK(Refuses(Replace(Input(Root,1),"asset_class: \"Material\",","asset_class: null,"),"material-registry-class-missing-or-invalid"));
    CHECK(Refuses(Replace(Input(Root,1),"/Game/M/Master.Master","/Game/M/Master.Ma\\ster"),"material-registry-object-identity-invalid"));
    CHECK(Refuses(Replace(Input(Root,1),"/Game/M/Master.Master",std::string("/Game/M/Master.")+char(0xc0)+char(0x80)),"material-registry-object-identity-invalid"));
    CHECK(Refuses(Replace(Input(Root,1),"Material\",",std::string(66000,'x')+"\","),"material-registry-identity-line-bound"));
    // Irrelevant large values are discarded; they cannot consume the row/string budget.
    const auto Large="        (\n            \"Unrelated\",\n            \""+std::string(200000,'x')+"\",\n        ),\n";
    FMaterialParentRegistry Long;CHECK(Parse(Input(Row("/Game/T/T","Texture2D","",Large)+Root+First,3),Long));CHECK(Long.Rows().size()==2);
    CHECK(Refuses(Input(Row(A,"MaterialInstanceConstant",Parent(Master),Replace(Large,"Unrelated","Parent")),1),"material-registry-parent-line-bound"));
    FMaterialParentRegistry Null,Bound;const std::uint8_t Byte=0;CHECK(!Null.Feed(nullptr,1));
    CHECK(!Bound.Feed(&Byte,std::size_t(1u<<30)+1));CHECK(Bound.Error()=="material-registry-stream-bound");
    // Unselected external and absent ancestry do not invalidate unrelated valid families.
    FMaterialParentRegistry External;CHECK(Parse(Input(Root+First+Row("/Engine/E/M","Material")+
        Row("/Game/E/I","MaterialInstanceConstant",Parent("/Engine/E/M"))+
        Row("/Game/U/I","MaterialInstanceConstant",Parent("/Game/U/Missing")),5),External));
    CHECK(External.Family(Master,OF,Why));CHECK(!External.Family("/Game/E/I",OF,Why));CHECK(Why=="material-family-external-root-unsupported");
    CHECK(!External.Family("/Game/U/I",OF,Why));CHECK(Why=="material-family-parent-source-missing");
    FMaterialParentRegistry Unknown;CHECK(Parse(Input(Root+Row(A,"NewMaterialClass",Parent(Master)),2),Unknown));
    CHECK(!Unknown.Family(Master,OF,Why));CHECK(Why=="material-family-class-unsupported"&&OF.Members.empty());
    FMaterialParentRegistry Unrelated;CHECK(Parse(Input(Root+Row("/Game/B/B","Blueprint","Blueprint'/Game/Parent.Parent'"),2),Unrelated));
    CHECK(Unrelated.Family(Master,OF,Why));CHECK(OF.Members.size()==1);
    FMaterialParentRegistry Conflict;CHECK(Parse(Input(Root+Row(A,"MaterialInstanceConstant",Parent(Master,"MaterialInstanceConstant")),2),Conflict));
    CHECK(!Conflict.Family(A,OF,Why));CHECK(Why=="material-family-parent-class-conflict");
    FMaterialParentRegistry Cycle;CHECK(Parse(Input(Row(A,"MaterialInstanceConstant",Parent(B,"MaterialInstanceConstant"))+
        Row(B,"MaterialInstanceConstant",Parent(A,"MaterialInstanceConstant")),2),Cycle));
    CHECK(!Cycle.Family(A,OF,Why));CHECK(Why=="material-family-parent-cycle");
    std::string Deep=Root;std::string Prior=Master;for(unsigned I=0;I<256;++I){const auto P="/Game/Deep/I"+std::to_string(I);Deep+=Row(P,"MaterialInstanceConstant",Parent(Prior,I?"MaterialInstanceConstant":"Material"));Prior=P;}
    FMaterialParentRegistry Boundary;CHECK(Parse(Input(Deep,257),Boundary));CHECK(Boundary.Family(Prior,OF,Why));CHECK(OF.Members.size()==257);
    CHECK(Boundary.Family(Master,OF,Why));CHECK(OF.Members.size()==257);
    Deep+=Row("/Game/Deep/TooDeep","MaterialInstanceConstant",Parent(Prior,"MaterialInstanceConstant"));
    FMaterialParentRegistry Depth;CHECK(Parse(Input(Deep,258),Depth));CHECK(!Depth.Family("/Game/Deep/TooDeep",OF,Why));CHECK(Why=="material-family-parent-depth");
    CHECK(!Depth.Family(Master,OF,Why));CHECK(Why=="material-family-parent-depth"&&OF.Members.empty()&&OF.Master.Package.empty());
    std::string Wide=Root;for(unsigned I=0;I<4096;++I)Wide+=Row("/Game/Wide/I"+std::to_string(I),"MaterialInstanceConstant",Parent(Master));
    FMaterialParentRegistry Width;CHECK(Parse(Input(Wide,4097),Width));CHECK(!Width.Family(Master,OF,Why));CHECK(Why=="material-family-member-bound"&&OF.Members.empty());
    const std::string Unicode="/Game/M/\xc3\xa9";FMaterialParentRegistry Uni;CHECK(Parse(Input(Root+Row(Unicode,"Material"),2),Uni));
    CHECK(Uni.Family(Master,OF,Why));CHECK(!Uni.Family(Unicode,OF,Why));CHECK(Why=="material-family-unicode-identity-unqualified");
    // Multiple distinct objects in a package remain representable, but package selection is ambiguous.
    const auto Other=Replace(Root,"Master.Master","Master.Other");FMaterialParentRegistry Ambiguous;CHECK(Parse(Input(Root+Other,2),Ambiguous));
    CHECK(!Ambiguous.Family(Master,OF,Why));CHECK(Why=="material-family-selection-ambiguous");CHECK(Ambiguous.Family(Master+".Master",OF,Why));
    std::cout<<"PASS: streaming/chunks, census, identity, Parent, external ancestry, cycles, bounds, deterministic complete families\n";
    return 0;
}
int main(int Argc,char** Argv)
{
    if(Argc==1)return Controls();
    FMaterialParentRegistry Registry;std::array<char,65536> Buffer;
    while(std::cin){std::cin.read(Buffer.data(),Buffer.size());const auto Count=std::cin.gcount();if(Count&&!Registry.Feed(reinterpret_cast<const std::uint8_t*>(Buffer.data()),std::size_t(Count)))break;}
    if(!Registry.Finish()){std::cerr<<Registry.Error()<<'\n';return 2;}
    std::cout<<"assets\t"<<Registry.AssetCount()<<"\nmaterial_instances\t"<<Registry.MaterialInstanceCount()<<"\nretained_rows\t"<<Registry.Rows().size()<<'\n';
    for(int I=1;I<Argc;++I)
    {
        FMaterialRegistryFamily Family;std::string Why;if(!Registry.Family(Argv[I],Family,Why)){std::cerr<<Argv[I]<<'\t'<<Why<<'\n';return 3;}
        std::cout<<"family\t"<<Family.Master.Package<<'\t'<<Family.Members.size()<<'\n';for(const auto& R:Family.Members)std::cout<<"member\t"<<R.Class<<'\t'<<R.Package<<'\t'<<R.ParentObject<<'\n';
    }
    return 0;
}

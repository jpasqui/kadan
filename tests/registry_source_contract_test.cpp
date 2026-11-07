#include "../core/material_family_registry.h"
#include "../core/export_registry.h"
#include "../Kadan/Source/Kadan/Private/P4RegistryCanonicalRows.h"
#include <iostream>
using P4MapKit::Material::FMaterialParentRegistry;
#define CHECK(X) do{if(!(X)){std::cerr<<"failed line "<<__LINE__<<": "<<#X<<'\n';return 1;}}while(false)
static std::string Row(const std::string& Name,const std::string& Class,const std::string& Parent="")
{
    return "AssetData {\n    object_path: \"/Game/M/"+Name+"."+Name+"\",\n    asset_class: \""+Class+"\",\n    package_name: \"/Game/M/"+Name+"\",\n    legacy_tags: [\n"+
        (Parent.empty()?"":"        (\n            \"Parent\",\n            \""+Parent+"\",\n        ),\n")+"    ],\n}\n";
}
static std::string Input(const std::string& Rows){return "  Registry Version: AddedDependencyFlags\n  Assets: 2\n"+Rows;}
static bool Read(const std::string& Text,FMaterialParentRegistry& R)
{return R.Feed(reinterpret_cast<const std::uint8_t*>(Text.data()),Text.size())&&R.Finish();}
static bool Encode(const FMaterialParentRegistry& R,std::vector<std::uint8_t>& Out)
{Out.clear();return P4RegistryLease::CanonicalRows(R,[&](const std::uint8_t* P,int N){Out.insert(Out.end(),P,P+N);return true;});}
int main()
{
    const auto Master=Row("Master","Material"),Instance=Row("I","MaterialInstanceConstant","Material'/Game/M/Master.Master'");
    FMaterialParentRegistry A,B,Changed;CHECK(Read(Input(Master+Instance),A));CHECK(Read(Input(Instance+Master),B));
    std::vector<std::uint8_t> AB,BB,CB;CHECK(Encode(A,AB)&&Encode(B,BB));CHECK(AB==BB);
    CHECK(Read(Input(Master+Row("I","MaterialInstanceConstant","Material'/Game/Else/Master.Master'")),Changed));
    CHECK(Encode(Changed,CB));CHECK(AB!=CB);
    bool Touched=false;CHECK(!P4RegistryLease::CanonicalRows(A,[&](const std::uint8_t*,int){Touched=true;return false;}));CHECK(Touched);
    FMaterialParentRegistry Invalid;Touched=false;CHECK(!P4RegistryLease::CanonicalRows(Invalid,[&](const std::uint8_t*,int){Touched=true;return true;}));CHECK(!Touched);
    // Simultaneous name/family parser contract: strict material refusal cannot turn
    // a complete name-only census into a failure or truncate later package names.
    const auto Bad=Input(Row("I","MaterialInstanceConstant","None")+Master);
    P4MapKit::Export::RegistryPackageLines Names;FMaterialParentRegistry Material;bool MaterialOkay=true;
    for(unsigned char C:Bad){CHECK(Names.Feed(&C,1));if(MaterialOkay)MaterialOkay=Material.Feed(&C,1);}
    CHECK(Names.Finish());CHECK(Names.Names.size()==2);CHECK(Names.Names[1]=="/Game/M/Master");
    CHECK(!MaterialOkay||!Material.Finish());CHECK(!Material.Valid());
    // Malformed package encoding remains a name-census failure, independently.
    const std::string BadName="package_name: \"/Game/Bad\\Name\",\n";
    P4MapKit::Export::RegistryPackageLines Refused;CHECK(!Refused.Feed(reinterpret_cast<const std::uint8_t*>(BadName.data()),BadName.size()));
    std::cout<<"PASS: canonical source-row framing/order/edge sensitivity and independent name/family failure contracts\n";
    return 0;
}

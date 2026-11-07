#include "core/table_edit_export.h"
#include <iostream>
#include <limits>
using namespace P4MapKit::TableEdit;
#define CHECK(X) do { if (!(X)) { std::cerr << __LINE__ << ": " << #X << '\n'; return 1; } } while (false)
Value Int(std::int64_t X) { Value V; V.Type=Kind::Integer; V.Integer=X; return V; }
Value Num(double X) { Value V; V.Type=Kind::Number; V.Number=X; return V; }
Value Text(std::string X) { Value V; V.Type=Kind::Text; V.Text=std::move(X); return V; }
Table Fixture() {
    Table T; T.Package="/Game/Folder/DT_Creatures"; T.ObjectPath=T.Package+".DT_Creatures"; T.RowStruct="/Script/Carrot4.TekiParam";
    T.Origin.SourceDigest=T.Origin.RecordSourceDigest=std::string(64,'a');
    T.Origin.ComparisonDigest=T.Origin.RecordComparisonDigest=std::string(64,'b');
    Value Position; Position.Type=Kind::Structure; Position.Fields={{"X",Num(1)},{"Y",Num(2)}};
    Value Distances; Distances.Type=Kind::Array; Distances.Elements={Num(10),Num(20)};
    T.Before["CHAPPY"]={{"Kira",Int(3)},{"BombHit",Num(1)},{"ActorName",Text("CHAPPY")},{"PositionOffset",Position},{"Distances",Distances}};
    T.After=T.Before; T.After["CHAPPY"]["Kira"]=Int(4294967295LL); T.After["CHAPPY"]["BombHit"]=Num(1.25);
    T.After["CHAPPY"]["ActorName"]=Text("KOCHAPPY"); T.After["CHAPPY"]["PositionOffset"].Fields["X"]=Num(4);
    T.After["CHAPPY"]["Distances"].Elements[1]=Num(30); return T;
}
int main(int Count,char** Args) {
    Table T=Fixture(); Result R; Refusal E; CHECK(Build({T},R,E)); CHECK(R.ChangedFields==5); CHECK(R.Sources.size()==1);
    CHECK(R.TablesJson.find("PositionOffset.X")!=std::string::npos); CHECK(R.TablesJson.find("Distances[1]")!=std::string::npos);
    const auto Held=R.TablesJson;
    auto Scalars=T; Value Flag; Flag.Type=Kind::Boolean;
    Scalars.Before["CHAPPY"]["Enabled"]=Flag; Flag.Boolean=true; Scalars.After["CHAPPY"]["Enabled"]=Flag;
    Scalars.Before["CHAPPY"]["Signed"]=Int(std::numeric_limits<std::int64_t>::min()); Scalars.After["CHAPPY"]["Signed"]=Int(std::numeric_limits<std::int64_t>::max());
    Result ScalarResult; CHECK(Build({Scalars},ScalarResult,E)); CHECK(ScalarResult.ChangedFields==7);
    CHECK(ScalarResult.TablesJson.find("\"Enabled\":true")!=std::string::npos); CHECK(ScalarResult.TablesJson.find("9223372036854775807")!=std::string::npos);
    auto Refuses=[&](Table X,const char* Code) { bool V=Build({X},R,E); return !V && E.Code==Code && R.TablesJson==Held; };
    auto X=T; X.Origin.RecordSourceDigest=std::string(64,'c'); CHECK(Refuses(X,"table-source-provenance-mismatch"));
    X=T; X.Origin.RecordComparisonDigest=std::string(64,'c'); CHECK(Refuses(X,"table-source-provenance-mismatch"));
    X=T; X.RowStruct="/Script/"; CHECK(Refuses(X,"table-identity-invalid"));
    X=T; X.After["NEW"]=X.After["CHAPPY"]; CHECK(Refuses(X,"table-row-add-remove-unsupported"));
    X=T; X.After.clear(); CHECK(Refuses(X,"table-row-add-remove-unsupported"));
    X=T; X.After["CHAPPY"]["Distances"].Elements.clear(); CHECK(Refuses(X,"table-array-resize-unsupported"));
    X=T; X.After["CHAPPY"]["BombHit"]=Num(std::numeric_limits<double>::infinity()); CHECK(Refuses(X,"table-number-not-finite"));
    X=T; X.After["CHAPPY"]["ActorName"]=Text(std::string("a\0b",3)); CHECK(Refuses(X,"table-text-embedded-nul"));
    X=T; X.After["CHAPPY"]["ActorName"]=Text(std::string(1024*1024+1,'x')); CHECK(Refuses(X,"table-text-budget"));
    X=T; X.After["CHAPPY"].erase("BombHit"); CHECK(Refuses(X,"table-schema-changed"));
    X=T; X.After["CHAPPY"]["Kira"]=Num(3); CHECK(Refuses(X,"table-property-type-changed"));
    X=T; Value Unsupported; Unsupported.Text="before"; X.Before["CHAPPY"]["HardObject"]=Unsupported;
    Unsupported.Text="after"; X.After["CHAPPY"]["HardObject"]=Unsupported; CHECK(Refuses(X,"table-property-unsupported"));
    X.After["CHAPPY"]["HardObject"].Text="before"; CHECK(Build({X},R,E)); CHECK(R.ChangedFields==5);
    X=T; X.Before["Row.With/Name"]=X.Before["CHAPPY"]; X.Before.erase("CHAPPY");
    X.After["Row.With/Name"]=X.After["CHAPPY"]; X.After.erase("CHAPPY"); CHECK(Build({X},R,E)); CHECK(R.ChangedFields==5);
    X=T; X.After=X.Before; CHECK(Build({X},R,E)); CHECK(R.TablesJson=="{}");
    CHECK(!Build({T,T},R,E)); CHECK(E.Code=="table-identity-invalid");
    std::map<std::string,std::string> Files; P4MapKit::Export::Descriptor Mod{"example.tables","Tables","1.0.0"};
    CHECK(FrameworkFiles(Mod,nullptr,{T},Files,E)); CHECK(Files.size()==1); CHECK(Files.at("mod.json").find("\"maps\"")==std::string::npos);
    CHECK(Files.at("mod.json").find(T.Origin.SourceDigest)==std::string::npos); const auto Saved=Files;
    CHECK(!FrameworkFiles(Mod,nullptr,{X},Files,E)); CHECK(Files==Saved);
    P4MapKit::Export::MapEdit Map; Map.Package="/Game/Folder/Cave_P"; Map.Navigation=true;
    CHECK(FrameworkFiles(Mod,&Map,{T},Files,E)); CHECK(Files.at("mod.json").find("\"maps\"")!=std::string::npos);
    if (Count>1 && std::string(Args[1])=="--json") std::cout<<Saved.at("mod.json");
    else std::cout<<"Native table export contract checks passed\n";
}

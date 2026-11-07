#include "core/authoring_definitions.h"
#include <iostream>
#include <limits>
using namespace P4MapKit;
using namespace P4MapKit::Definitions;
#define CHECK(X) do { if(!(X)) { std::cerr << "Failed at " << __LINE__ << ": " << #X << '\n'; return 1; } } while(false)
template<class T> Optional<T> Some(T V) { Optional<T> R; R.Present=true; R.Value=std::move(V); return R; }
AssetRef Own(const Export::Descriptor& Mod,const std::string& LeafName,const std::string& Class,bool Object=true) {
    Export::Names N; std::string E; Export::DerivedNames(Mod,N,E);
    AssetRef A; A.Path=N.Root+'/'+LeafName+(Object?'.'+LeafName:""); A.Class=Class; A.Owner=Mod.Id;
    A.SourceDigest=std::string(64,'a'); A.CustodyKey="synthetic-test-custody"; return A;
}
Donor Source(const std::string& Id,const std::string& Table) {
    Donor D; D.Id=D.NativeRow=Id; D.ClassPath="/Game/Test/GDonor.GDonor_C";
    RowSource R; R.Table=Table; R.Row=Id; R.RowStruct="/Script/Carrot4.TestParameter";
    R.SourceDigest=std::string(64,'b'); R.CustodyKey="synthetic-donor-custody";
    R.SourceDigestKind="header-body-sha256-v1"; R.ComparisonSchema="object-lines-v1"; R.ComparisonDigest=std::string(64,'c');
    R.FieldTypes["TestValue"]=TableEdit::Kind::Integer; D.Rows={R};
    R.FieldTypes.clear(); R.Table="/Game/Carrot4/Placeables/Zukan/DT_ZukanBP.DT_ZukanBP"; D.Rows.push_back(R);
    R.Table="/Game/Carrot4/Core/GActor/DT_ActorTexture.DT_ActorTexture"; D.Rows.push_back(R); return D;
}
int main(int Count,char** Args) {
    Export::Descriptor Mod{"example.authors","Author examples","1.0.0"};
    Context C; C.TargetBuild="1.1.1"; C.GameBuildId="060F827BD2B38703E9EA11E3210D89BC"; C.FreeAreas={5,7,8,9};
    Set S; S.SeriesList.push_back({"gems","Gem Collection"});
    Treasure T; T.Id="ruby"; T.Class=Own(Mod,"GOtaRuby","BlueprintGeneratedClass"); T.Class.Path+="_C";
    T.From=Source("OTAECLAIR","/Game/Carrot4/Core/GActor/DT_OtakaraParameter.DT_OtakaraParameter"); T.Text.Name="Ruby";
    T.Sparklium=Some<std::int32_t>(120); T.Carry=Some(Carriers{1,3}); T.Text.Description=Some(std::string("A \"gem\"\n"));
    T.Icon=Some(Own(Mod,"T_Ruby","Texture2D"));
    TableEdit::Value V; V.Type=TableEdit::Kind::Integer; V.Integer=42; T.Fields["TestValue"]=V;
    TreasureCatalog K; K.Series="gems"; K.Source=T.From.Rows[0]; K.Source.Table="/Game/Carrot4/Placeables/Zukan/DT_ZukanOtakaraParameter.DT_ZukanOtakaraParameter"; T.Catalog=Some(K); S.Treasures={T};
    Creature Z; Z.Id="stonehog"; Z.Class=Own(Mod,"GStonehog","BlueprintGeneratedClass"); Z.Class.Path+="_C";
    Z.From=Source("KOCHAPPY","/Game/Carrot4/Core/GActor/DT_TekiParameter.DT_TekiParameter"); Z.Text.Name="Stonehog";
    Z.Day.Life=Some(450.5); Z.Day.Pikmin=Some<std::int32_t>(5);
    auto Night=Own(Mod,"GStonehogNight","BlueprintGeneratedClass"); Night.Path+="_C"; Z.NightClass=Some(Night);
    Numbers NightValues; NightValues.Life=Some(900.0); Z.Night=Some(NightValues);
    CreatureCatalog CZ; CZ.Source=Z.From.Rows[0]; CZ.Source.Table="/Game/Carrot4/Placeables/Zukan/DT_ZukanTekiParameter.DT_ZukanTekiParameter"; Z.Catalog=Some(CZ); S.Creatures={Z};
    NewMap M; M.Id="floor"; M.Name="Own Floor Course"; MapBuild B; B.Area=7;
    B.World=Own(Mod,"Area007_P","World",false); B.Scene=Own(Mod,"SD_Area007","/Script/Carrot4.GameSceneData");
    B.RootScene=Own(Mod,"SD_Root007","/Script/Carrot4.GameSceneData"); B.Counts=Own(Mod,"ANC_Area007","/Script/Carrot4.ActorPlacedNumAsset"); M.Builds={B};
    M.Preview=Some(Own(Mod,"T_Preview","Texture2D")); Radar R; R.Texture=Own(Mod,"T_Radar","Texture2D");
    R.Offset=Some(std::array<double,2>{{1,2}}); R.Size=Some(3000.0); R.Angle=Some(-45.0); R.Scale=Some(1.0); R.Limits=Some(std::array<double,4>{{-1,2,3,-4}}); M.MapRadar=Some(R);
    M.Pikmin=Some(std::vector<std::string>{"red","ice"}); Totals N; N.Treasures=Some<std::int32_t>(3); N.Flarlic=Some<std::int32_t>(1); M.Counts=Some(N); S.Maps={M};
    Result Good; Refusal E; CHECK(Build(Mod,S,C,Good,E)); CHECK(Good.Assets.size()==10); CHECK(Good.Donors.size()==2); CHECK(Good.Rows.size()==8); CHECK(Good.Reservations.size()==4); CHECK(Good.Slots[0].Id=="example.authors:floor");
    std::map<std::string,std::string> Files; Result Out;
    CHECK(FrameworkFiles(Mod,nullptr,{},S,C,Files,Out,E)); const auto Saved=Files; const auto Original=S; const auto OriginalContext=C;
    auto Refused=[&](const char* Code)->bool {
        Out.ContentJson="sentinel"; Files=Saved;
        if(FrameworkFiles(Mod,nullptr,{},S,C,Files,Out,E)) return false;
        return E.Code==Code && Files==Saved && Out.ContentJson=="sentinel";
    };
    S.Treasures[0].Class.Path="/Game/Other/Bad.Bad_C"; CHECK(Refused("definition-creator-root")); S=Original;
    S.Treasures[0].Class.SourceDigest.clear(); CHECK(Refused("definition-source-custody")); S=Original;
    S.Treasures[0].Class.Class="StaticMesh"; CHECK(Refused("definition-asset-type-or-path")); S=Original;
    S.Treasures[0].From.Rows[0].Table="/Game/Wrong.Wrong"; CHECK(Refused("definition-donor-parameter-row")); S=Original;
    S.Treasures[0].From.Rows.pop_back(); CHECK(Refused("definition-donor-auxiliary-row")); S=Original;
    S.Treasures[0].From.Rows[0].ComparisonDigest.clear(); CHECK(Refused("definition-donor-row")); S=Original;
    S.Treasures[0].From.NativeRow="NOT_THE_DONOR"; CHECK(Refused("definition-donor-row")); S=Original;
    S.Treasures[0].Fields["Unknown"]=V; CHECK(Refused("definition-field-schema")); S=Original;
    S.Treasures[0].Fields["TestValue"].Type=TableEdit::Kind::Structure; CHECK(Refused("definition-field-unsupported")); S=Original;
    S.Treasures[0].Carry.Value={4,3}; CHECK(Refused("definition-carriers")); S=Original;
    S.Treasures[0].Catalog.Value.Series="absent"; CHECK(Refused("definition-catalog-series")); S=Original;
    S.Treasures[0].Catalog.Value.Source.Row="wrong"; CHECK(Refused("definition-catalog-source")); S=Original;
    S.Creatures[0].NightClass=Some(S.Creatures[0].Class); CHECK(Refused("definition-night-class")); S=Original;
    S.Creatures[0].Class=S.Treasures[0].Class; CHECK(Refused("definition-class-shared")); S=Original;
    S.Creatures[0].Day.Life.Value=std::numeric_limits<double>::infinity(); CHECK(Refused("definition-life")); S=Original;
    S.Treasures[0].Text.Name=std::string("A\0B",3); CHECK(Refused("definition-text")); S=Original;
    S.Maps[0].Builds[0].Area=6; CHECK(Refused("definition-map-slot")); S=Original;
    C.Occupied={{7,"other.mod:floor"}}; CHECK(Refused("definition-map-slot-occupied")); C=OriginalContext;
    C.Occupied={{7,"example.authors:floor"}}; CHECK(Build(Mod,S,C,Out,E)); C=OriginalContext;
    S.Maps[0].Builds[0].Scene.Path=S.Maps[0].Builds[0].World.Path; CHECK(Refused("definition-map-build-name")); S=Original;
    S.Maps[0].Builds.push_back(S.Maps[0].Builds[0]); CHECK(Refused("definition-map-slot")); S=Original;
    S.Maps[0].MapRadar.Value.Limits.Value={{2,1,1,0}}; CHECK(Refused("definition-radar-limits")); S=Original;
    S.Maps[0].MapRadar.Value.Scale.Value=0; CHECK(Refused("definition-radar-number")); S=Original;
    S.Maps[0].Counts.Value.Treasures.Value=10001; CHECK(Refused("definition-integer")); S=Original;
    S.Maps[0].Pikmin.Value={"red","RED"}; CHECK(Refused("definition-map-pikmin")); S=Original;
    S.Maps[0].Name=std::string(161,'x'); CHECK(Refused("definition-map-builds")); S=Original;
    S.Maps[0].Name=std::string("\xc0\xaf",2); CHECK(Refused("definition-map-name")); S=Original;
    C.GameBuildId=std::string(32,'0'); CHECK(Refused("definition-target-build")); C=OriginalContext;
    CHECK(LogicalId("OTAE-CLAIR")); CHECK(LogicalId("mod.id:thing")); CHECK(!LogicalId("#123"));
    // Composition uses the first-party table/map writer, including its independent refusals.
    Export::MapEdit Map; Map.Package="/Game/Stock/Area001_P"; Map.Navigation=true;
    CHECK(FrameworkFiles(Mod,&Map,{},S,C,Files,Out,E)); CHECK(Files["mod.json"].find("\"maps\"")!=std::string::npos);
    TableEdit::Table Table; Table.Package="/Game/Tables/Test"; Table.ObjectPath=Table.Package+".Test"; Table.RowStruct="/Script/Carrot4.TestParameter";
    Table.Origin.SourceDigest=Table.Origin.RecordSourceDigest=std::string(64,'a'); Table.Origin.ComparisonDigest=Table.Origin.RecordComparisonDigest=std::string(64,'b');
    Table.Before["Original"]["Number"]=V; Table.After=Table.Before;
    CHECK(FrameworkFiles(Mod,nullptr,{Table},S,C,Files,Out,E)); CHECK(Files==Saved);
    Table.After["Original"]["Number"].Integer=99;
    CHECK(FrameworkFiles(Mod,&Map,{Table},S,C,Files,Out,E)); CHECK(Files["mod.json"].find("\"tables\"")!=std::string::npos);
    const auto Combined=Files; Out.ContentJson="sentinel"; Table.After["Added"]={};
    CHECK(!FrameworkFiles(Mod,&Map,{Table},S,C,Files,Out,E)); CHECK(E.Code=="table-row-add-remove-unsupported"&&Files==Combined&&Out.ContentJson=="sentinel");
    if(Count>1&&std::string(Args[1])=="--json") std::cout<<Saved.at("mod.json");
    else std::cout<<"Authoring definition checks passed\n";
}

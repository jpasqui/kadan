#include "core/new_map_census.h"
#include <iostream>
using namespace P4MapKit::MapCensus;
#define CHECK(X) do { if(!(X)) { std::cerr<<"Failed at "<<__LINE__<<": "<<#X<<'\n'; return 1; } } while(false)
File Bytes(const std::string& S) { File F; F.State=Presence::Present; F.Bytes=S; F.Digest=std::string(64,'a'); return F; }
int main(int Count,char** Args) {
    if(Count==4) {
        Input Actual; File* Files[]={&Actual.First,&Actual.Second,&Actual.Legacy};
        for(int N=0;N<3;++N) {
            if(std::string(Args[N+1])=="-") continue;
            std::ifstream In(Args[N+1],std::ios::binary|std::ios::ate);
            if(!In) { Files[N]->State=Presence::Unavailable; continue; }
            const auto Size=In.tellg();
            if(Size<0||std::uint64_t(Size)>kMaxRecordBytes) { Files[N]->State=Presence::Unavailable; continue; }
            In.seekg(0); Files[N]->Bytes.resize(std::size_t(Size));
            if(Size&&!In.read(&Files[N]->Bytes[0],Size)) { Files[N]->State=Presence::Unavailable; Files[N]->Bytes.clear(); continue; }
            Files[N]->State=Presence::Present;
        }
        const auto ActualResult=Read(Actual); CHECK(IsUsable(ActualResult));
        std::cout<<"{\"status\":"<<int(ActualResult.State)<<",\"source\":"<<int(ActualResult.RuntimeSource)<<",\"counter\":"<<ActualResult.Counter<<",\"occupied\":[";
        bool First=true; for(const auto& V:ActualResult.Entries) { if(!First) std::cout<<','; First=false; std::cout<<"{\"area\":"<<V.Area<<",\"map\":"<<P4MapKit::Export::Quote(V.Id)<<",\"used\":"<<(V.Used?"true":"false")<<'}'; }
        std::cout<<"]}\n"; return 0;
    }
    Input I; auto R=Read(I); CHECK(R.State==Status::Empty&&CanUse(R)&&R.Occupied.empty());
    I.First=Bytes("{\"counter\":1,\"areas\":{\"5\":{\"map\":\"proof.course:course\",\"used\":true}}}");
    I.Second=Bytes("{\"counter\":2,\"areas\":{\"5\":{\"map\":\"proof.course:course\",\"used\":true,\"future\":[1,2]},\"7\":{\"map\":\"proof.floor:floor\",\"used\":false},\"100\":null},\"future\":{\"whatever\":3}}");
    const auto Good=I; R=Read(I); CHECK(R.State==Status::Valid&&R.RuntimeSource==Source::Second&&R.Counter==2&&R.Occupied.size()==2&&!R.Entries[1].Used); CHECK(R.Snapshot.Second.Bytes==I.Second.Bytes);
    I.Second=Bytes("{\"counter\":3,\"future\":{}}"); R=Read(I); CHECK(R.State==Status::Recovered&&R.RuntimeSource==Source::First&&R.Occupied.size()==1); CHECK(R.Copies[1].Error=="census-required-areas");
    I.First=Bytes("not json"); R=Read(I); CHECK(R.State==Status::Damaged&&!CanUse(R)&&R.Occupied.empty());
    I.Legacy=Good.First; CHECK(Read(I).State==Status::Damaged); // Canonical presence suppresses legacy.
    I={}; I.Legacy=Bytes("{\"areas\":{\"05\":{\"map\":\"proof.course:course\",\"used\":true},\"7\":{\"map\":\"proof.floor:floor\",\"used\":true}},\"future\":42}");
    R=Read(I); CHECK(R.State==Status::Legacy&&R.Counter==0&&R.Occupied.size()==2&&R.RuntimeSource==Source::Legacy); CHECK(R.Snapshot.Legacy.Bytes==I.Legacy.Bytes);
    I.First=Bytes("{\"counter\":0,\"areas\":{}}"); R=Read(I); CHECK(R.State==Status::Valid&&R.Occupied.empty()&&R.Copies.size()==2);
    I=Good; I.Second=Bytes("{\"counter\":1,\"areas\":{\"7\":{\"map\":\"proof.floor:floor\",\"used\":true}}}");
    R=Read(I); CHECK(R.State==Status::Ambiguous&&!CanUse(R)&&R.RuntimeSource==Source::First&&R.Occupied.empty());
    I.Second=I.First; R=Read(I); CHECK(R.State==Status::Valid&&R.RuntimeSource==Source::First);
    for(const char* Bad:{"{\"counter\":1.0,\"areas\":{}}","{\"counter\":-1,\"areas\":{}}","{\"counter\":9223372036854775808,\"areas\":{}}","{\"areas\":{\"5\":{\"map\":\"proof:map\",\"used\":0}}}","{\"areas\":{\"5\":{\"map\":\"proof:map\",\"used\":true},\"05\":{\"map\":\"proof:other\",\"used\":true}}}","{\"areas\":{\"5\":{\"map\":\"proof:map\",\"used\":true},\"7\":{\"map\":\"proof:map\",\"used\":false}}}","{\"areas\":{\"5\":{\"map\":\"plain\",\"used\":true}}}","{\"areas\":{},\"areas\":{}}"}) {
        I={}; I.First=Bytes(Bad); R=Read(I); CHECK(R.State==Status::Damaged&&!CanUse(R));
    }
    I={}; I.First=Bytes("{\"counter\":9223372036854775807,\"areas\":{}}"); CHECK(Read(I).State==Status::Valid);
    I.First.State=Presence::Unavailable; R=Read(I); CHECK(R.State==Status::Unavailable&&!CanUse(R));
    I=Good; I.Second.State=Presence::Unavailable; R=Read(I); CHECK(R.State==Status::Unavailable&&!CanUse(R));
    I={}; I.Legacy.State=Presence::Unavailable; CHECK(Read(I).State==Status::Unavailable);
    I=Good; I.Legacy.State=Presence::Unavailable; CHECK(Read(I).State==Status::Valid);
    I={}; I.First=Bytes("{\"areas\":{}}"); I.First.Digest.clear(); CHECK(Read(I).State==Status::Valid); // Digests are native custody facts, never parser proof.
    I.First=Bytes("{\"areas\":{},\"future\":1e400}"); CHECK(Read(I).State==Status::Damaged);
    I.First=Bytes("{\"areas\":{},\"future\":1e-400}"); CHECK(Read(I).State==Status::Valid);
    I.First=Bytes(std::string(kMaxRecordBytes+1,' ')); CHECK(Read(I).State==Status::Unavailable);
    const auto Nested=[](unsigned N) { return "{\"areas\":{},\"future\":"+std::string(N,'[')+"0"+std::string(N,']')+'}'; };
    I.First=Bytes(Nested(63)); CHECK(Read(I).State==Status::Valid); I.First=Bytes(Nested(64)); CHECK(Read(I).State==Status::Damaged);
    CHECK(std::string(Path(Source::First))=="save:/tsuchi/tsuchi.content/new_maps.json");
    std::cout<<"Read-only new-map census checks passed\n";
}

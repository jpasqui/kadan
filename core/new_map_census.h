#pragma once
// Pure read-only projection of Tsuchi's paired map-allocation record. Never allocates a slot.
#include "authoring_definitions.h"
#include "blueprint_json.h"

namespace P4MapKit { namespace MapCensus {
constexpr std::size_t kMaxRecordBytes=2*1024*1024; // Authoring bound; oversize blocks acquisition.
enum class Presence { Missing, Present, Unavailable };
enum class Source { None, First, Second, Legacy };
enum class Status { Empty, Valid, Recovered, Legacy, Damaged, Unavailable, Ambiguous };
struct File { Presence State=Presence::Missing; std::string Bytes, Digest; };
struct Input { File First, Second, Legacy; };
struct Retained { unsigned Area=0; std::string Id; bool Used=false; };
struct Copy {
    Source Which=Source::None; Presence State=Presence::Missing;
    bool Valid=false; std::uint64_t Counter=0; std::string Error;
    std::vector<Retained> Entries;
};
struct Result {
    Status State=Status::Unavailable; Source RuntimeSource=Source::None;
    std::uint64_t Counter=0;
    std::vector<Retained> Entries;
    std::vector<Definitions::SlotClaim> Occupied;
    std::vector<Copy> Copies;
    Input Snapshot; // Exact bytes/digests, including unknown members and unused copies.
};
inline const char* Path(Source S) {
    switch(S) {
    case Source::First: return "save:/tsuchi/tsuchi.content/new_maps.json";
    case Source::Second: return "save:/tsuchi/tsuchi.content/new_maps.2.json";
    case Source::Legacy: return "save:/tsuchi/new_maps.json";
    default: return "";
    }
}
inline bool FreeArea(unsigned A) { return A==5||A==7||A==8||A==9; }
inline int Area(const std::string& S) {
    int N=0; if(S.empty()) return -1;
    // Matches the service's numeric-key admission, including leading zeros.
    for(char C:S) { if(C<'0'||C>'9'||N>=100000) return -1; N=N*10+(C-'0'); }
    return N;
}
inline bool FiniteNumber(const std::string& S) {
    // Same bounded-digit scaling as the runtime JSON reader, including finite underflow to zero.
    std::size_t I=!S.empty()&&S[0]=='-'?1:0; std::uint64_t Digits=0; int Tens=0;
    auto Take=[&](bool Fraction) { const unsigned D=unsigned(S[I++]-'0'); if(Digits<=(UINT64_MAX-D)/10) { Digits=Digits*10+D; Tens-=Fraction; } else Tens+=!Fraction; };
    while(I<S.size()&&S[I]>='0'&&S[I]<='9') Take(false);
    if(I<S.size()&&S[I]=='.') { ++I; while(I<S.size()&&S[I]>='0'&&S[I]<='9') Take(true); }
    if(I<S.size()&&(S[I]=='e'||S[I]=='E')) { ++I; const bool Down=I<S.size()&&S[I]=='-'; if(I<S.size()&&(S[I]=='-'||S[I]=='+')) ++I; int Exp=0; while(I<S.size()) { if(Exp<100000) Exp=Exp*10+(S[I]-'0'); ++I; } Tens+=Down?-Exp:Exp; }
    double Amount=double(Digits); const double Powers[]={1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,1e20,1e21,1e22};
    for(int Left=Tens;Left>0&&Amount!=0;Left-=22) { Amount*=Powers[Left<22?Left:22]; if(!std::isfinite(Amount)) return false; }
    for(int Left=-Tens;Left>0&&Amount!=0;Left-=22) Amount/=Powers[Left<22?Left:22];
    return true;
}
inline bool RuntimeNumbers(const Blueprint::J& J,unsigned Depth=0) {
    if((J.Kind==Blueprint::J::K::Object||J.Kind==Blueprint::J::K::Array)&&Depth==64) return false;
    if(J.Kind==Blueprint::J::K::Number) return FiniteNumber(J.Text);
    for(const auto& V:J.Fields) if(!RuntimeNumbers(V.second,Depth+1)) return false;
    for(const auto& V:J.Items) if(!RuntimeNumbers(V,Depth+1)) return false;
    return true;
}
inline Copy Parse(const File& F,Source Which) {
    Copy C; C.Which=Which; C.State=F.State;
    if(F.State!=Presence::Present) return C;
    try {
        const auto J=Blueprint::J::Parse(F.Bytes,kMaxRecordBytes,100000,64);
        if(!RuntimeNumbers(J)) throw Blueprint::Error("census-nonfinite-number");
        if(J.Kind!=Blueprint::J::K::Object) throw Blueprint::Error("census-root-object");
        if(J.Has("counter")) { const auto N=J.Get("counter").I(); if(N<0) throw Blueprint::Error("census-counter"); C.Counter=static_cast<std::uint64_t>(N); }
        if(!J.Has("areas")||J.Get("areas").Kind!=Blueprint::J::K::Object) throw Blueprint::Error("census-required-areas");
        const auto& Areas=J.Get("areas"); std::set<unsigned> Slots; std::set<std::string> Ids;
        for(const auto& P:Areas.Fields) {
            const int N=Area(P.first); if(N<0||!FreeArea(static_cast<unsigned>(N))) continue;
            const auto& V=P.second;
            if(V.Kind!=Blueprint::J::K::Object||!V.Has("map")||!V.Has("used")) throw Blueprint::Error("census-slot-object");
            const auto Id=V.Get("map").S();
            if(!Definitions::LogicalId(Id)||Id.find(':')==std::string::npos||!Slots.insert(static_cast<unsigned>(N)).second||!Ids.insert(Id).second) throw Blueprint::Error("census-slot-identity");
            C.Entries.push_back({static_cast<unsigned>(N),Id,V.Get("used").B()});
        }
        std::sort(C.Entries.begin(),C.Entries.end(),[](const Retained& A,const Retained& B){return A.Area<B.Area;});
        C.Valid=true;
    } catch(const Blueprint::Error& E) { C.Error=E.what(); C.Entries.clear(); }
    return C;
}
inline bool Same(const std::vector<Retained>& A,const std::vector<Retained>& B) {
    if(A.size()!=B.size()) return false;
    for(std::size_t I=0;I<A.size();++I) if(A[I].Area!=B[I].Area||A[I].Id!=B[I].Id||A[I].Used!=B[I].Used) return false;
    return true;
}
inline bool Consistent(const File& F) {
    return F.State==Presence::Present || (F.Bytes.empty()&&F.Digest.empty());
}
inline Result Read(const Input& In) {
    Result R; R.Snapshot=In;
    // Runtime fetch errors do not authorize falling back to another copy.
    if(!Consistent(In.First)||!Consistent(In.Second)||In.First.State==Presence::Unavailable||In.Second.State==Presence::Unavailable||In.First.Bytes.size()>kMaxRecordBytes||In.Second.Bytes.size()>kMaxRecordBytes) return R;
    R.Copies={Parse(In.First,Source::First),Parse(In.Second,Source::Second)};
    const bool UseLegacy=In.First.State==Presence::Missing&&In.Second.State==Presence::Missing;
    if(UseLegacy) {
        if(!Consistent(In.Legacy)||In.Legacy.State==Presence::Unavailable||In.Legacy.Bytes.size()>kMaxRecordBytes) return R;
        R.Copies.push_back(Parse(In.Legacy,Source::Legacy));
    }
    const Copy* Chosen=nullptr; bool Any=false,Damaged=false;
    for(const auto& C:R.Copies) {
        Any=Any||C.State==Presence::Present; Damaged=Damaged||(C.State==Presence::Present&&!C.Valid);
        // Strict > retains the runtime's first-copy preference on equal generations.
        if(C.Valid&&(!Chosen||C.Counter>Chosen->Counter)) Chosen=&C;
    }
    if(!Chosen) { R.State=Any?Status::Damaged:Status::Empty; return R; }
    R.RuntimeSource=Chosen->Which; R.Counter=Chosen->Counter; R.Entries=Chosen->Entries;
    R.State=UseLegacy?Status::Legacy:Damaged?Status::Recovered:Status::Valid;
    for(const auto& C:R.Copies) if(C.Valid&&C.Which!=Chosen->Which&&C.Counter==Chosen->Counter&&!Same(C.Entries,Chosen->Entries)) R.State=Status::Ambiguous;
    // On ambiguity/damage the driver must not construct a context from an empty list.
    if(R.State!=Status::Ambiguous) for(const auto& V:R.Entries) R.Occupied.push_back({V.Area,V.Id});
    return R;
}
inline bool CanUse(const Result& R) {
    return R.State==Status::Empty||R.State==Status::Valid||R.State==Status::Recovered||R.State==Status::Legacy;
}
inline bool IsUsable(const Result& R) { return CanUse(R); }
}}

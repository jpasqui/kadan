// First-party bounded NSO0 / ELF64 AArch64 identity reader. Format references:
// switchbrew/switch-tools src/elf2nso.c, ELF gABI, GNU ld build-id documentation,
// and lz4/doc/lz4_Block_format.md. No source or game bytes are embedded here.
// This mutable diagnostic result is NOT a custody token. Native callers must
// freshly acquire bytes, Read + RequireSupportedCodeBuild, and bind their pak census.
// Expanded Eden research dumps use the separate ReadExpandedResearch entry point.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace P4MapKit { namespace OriginalExecutable {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint64_t MaxFileBytes = 512ull << 20;
constexpr std::uint64_t MaxMemoryBytes = 512ull << 20;
struct Section {
    std::string Name, Sha256, HeaderSha256;
    std::uint64_t FileOffset=0, FileSize=0, MemoryOffset=0, MemorySize=0;
    bool HashRequired=false, Compressed=false;
};
struct Identity {
    std::string Format, Representation, BuildId, Sha256, HeaderSha256;
    std::vector<Section> Sections;
    std::uint64_t BssSize=0;
    bool SupportedCodeBuild=false;
};
inline const char* SupportedNsoSha256()
{ return "fe616565ecfd35868fdd9a43525931a3bd46c383151e2a244cdf8e199a974190"; }
inline const char* SupportedNsoBuildId()
{ return "060f827bd2b38703e9ea11e3210d89bc00000000000000000000000000000000"; }
// Authority was independently read from the exact expanded research artifact
// and checked against all three retained original header hashes. A normal NSO
// may have different compression/container bytes: retain its own actual digest.
// Qualification identifies code/build, not capture producer authenticity.
inline bool RequireSupportedCodeBuild(const Identity& Value, std::string& Why) {
    static const char* Hashes[]={"dc51485f6cae16cead3790ac1ae393fff987b5662a3cf997b5d5516342917346",
        "c938e3510e2273eaa5602283a970b4dfe47afed8fefc9dbcd0b7c1c3f193569c",
        "29fa6e0d1e58bf45e7de6c532d8c11a3854e883e18977a56ba3083a20317dc7e"};
    static const std::uint64_t Offsets[]={0,103747584,142204928},Sizes[]={103745680,38457336,11789536};
    bool Normal=Value.Format=="NSO"&&Value.Representation=="normal-executable";
    bool Expanded=Value.Format=="NSO-expanded"&&Value.Representation=="derived-research"&&Value.Sha256==SupportedNsoSha256();
    if ((!Normal&&!Expanded) || Value.BuildId!=SupportedNsoBuildId() || Value.Sha256.size()!=64 ||
        !std::all_of(Value.Sha256.begin(),Value.Sha256.end(),[](char C){return (C>='0'&&C<='9')||(C>='a'&&C<='f');}) ||
        Value.BssSize!=22842144 || Value.Sections.size()!=3) {
        Why="executable does not establish supported Pikmin 4 1.1.1 code-build identity";return false;
    }
    for(unsigned I=0;I<3;++I)if(Value.Sections[I].MemoryOffset!=Offsets[I]||Value.Sections[I].MemorySize!=Sizes[I]||
        Value.Sections[I].Sha256!=Hashes[I]||Value.Sections[I].HeaderSha256!=Hashes[I]) {
        Why="supported code-build section layout or digest mismatch";return false;
    }
    Why.clear(); return true;
}
namespace Detail {
inline std::string Hex(const std::uint8_t* P, std::size_t N) {
    const char* Digits="0123456789abcdef"; std::string R; R.reserve(N*2);
    for (std::size_t I=0;I<N;++I) { R+=Digits[P[I]>>4]; R+=Digits[P[I]&15]; }
    return R;
}
// Incremental SHA-256 avoids copying the executable or decompressed sections.
class Hash {
    std::uint32_t State[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<std::uint8_t,64> Block{}; std::size_t Used=0; std::uint64_t Count=0;
    static std::uint32_t R(std::uint32_t X,unsigned N) { return (X>>N)|(X<<(32-N)); }
    void Transform() {
        static const std::uint32_t K[64]={
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t W[64];
        for (unsigned I=0;I<16;++I) W[I]=std::uint32_t(Block[I*4])<<24|
            std::uint32_t(Block[I*4+1])<<16|std::uint32_t(Block[I*4+2])<<8|Block[I*4+3];
        for (unsigned I=16;I<64;++I) W[I]=W[I-16]+(R(W[I-15],7)^R(W[I-15],18)^(W[I-15]>>3))+
            W[I-7]+(R(W[I-2],17)^R(W[I-2],19)^(W[I-2]>>10));
        auto A=State[0],B=State[1],C=State[2],D=State[3],E=State[4],F=State[5],G=State[6],H=State[7];
        for (unsigned I=0;I<64;++I) {
            auto T1=H+(R(E,6)^R(E,11)^R(E,25))+((E&F)^(~E&G))+K[I]+W[I];
            auto T2=(R(A,2)^R(A,13)^R(A,22))+((A&B)^(A&C)^(B&C));
            H=G;G=F;F=E;E=D+T1;D=C;C=B;B=A;A=T1+T2;
        }
        State[0]+=A;State[1]+=B;State[2]+=C;State[3]+=D;
        State[4]+=E;State[5]+=F;State[6]+=G;State[7]+=H;
    }
public:
    void Add(const std::uint8_t* P,std::size_t N) {
        Count+=N;
        while (N) {
            auto Take=std::min(N,64-Used); std::memcpy(Block.data()+Used,P,Take);
            Used+=Take;P+=Take;N-=Take;
            if (Used==64) { Transform();Used=0; }
        }
    }
    void Byte(std::uint8_t V) {
        ++Count;Block[Used++]=V;if (Used==64) { Transform();Used=0; }
    }
    std::string Finish() {
        auto Bits=Count*8; Byte(128);while (Used!=56) Byte(0);
        for (int I=7;I>=0;--I) Byte(std::uint8_t(Bits>>(I*8)));
        std::array<std::uint8_t,32> Out{};
        for(unsigned I=0;I<8;++I)for(unsigned J=0;J<4;++J)Out[I*4+J]=std::uint8_t(State[I]>>(24-J*8));
        return Hex(Out.data(),Out.size());
    }
};
inline std::string Digest(const std::uint8_t* P,std::size_t N) { Hash H;H.Add(P,N);return H.Finish(); }
inline bool Range(std::uint64_t At,std::uint64_t N,std::uint64_t End)
{ return At<=End && N<=End-At; }
inline bool Overlap(std::uint64_t A,std::uint64_t AN,std::uint64_t B,std::uint64_t BN)
{ return AN && BN && (A<=B ? AN>B-A : BN>A-B); }
inline std::uint32_t U32(const Bytes& B,std::size_t O)
{ return std::uint32_t(B[O])|std::uint32_t(B[O+1])<<8|std::uint32_t(B[O+2])<<16|std::uint32_t(B[O+3])<<24; }
inline std::uint16_t U16(const Bytes& B,std::size_t O) { return std::uint16_t(B[O])|std::uint16_t(B[O+1])<<8; }
inline std::uint64_t U64(const Bytes& B,std::size_t O) { return U32(B,O)|(std::uint64_t(U32(B,O+4))<<32); }
inline bool Bad(std::string& Why,const char* Text) { Why=Text;return false; }

// Independent raw LZ4 block decoder, hashing through a 64 KiB history ring.
// Every input, extension, output, offset and terminal sequence is bounded.
inline bool Lz4(const std::uint8_t* In,std::size_t N,std::uint64_t Expected,
    std::string& DigestOut,std::string& Why) {
    std::array<std::uint8_t,65536> History{}; Hash H;std::size_t At=0;
    std::uint64_t Produced=0,LastMatch=0;bool HadMatch=false;
    auto Length=[&](std::uint64_t Base,std::uint64_t& Out)->bool {
        Out=Base;
        if (Base==15) { unsigned V;do {
            if (At==N) return false;V=In[At++];
            if (V>Expected || Out>Expected-V) return false;Out+=V;
        }while(V==255); }
        return Out<=Expected;
    };
    auto Emit=[&](std::uint8_t V) { History[std::size_t(Produced&65535)]=V;++Produced;H.Byte(V); };
    while(At<N) {
        unsigned Token=In[At++];std::uint64_t Literals;
        if(!Length(Token>>4,Literals)||Literals>N-At||Literals>Expected-Produced)
            return Bad(Why,"NSO LZ4 literal length refused");
        for(std::uint64_t I=0;I<Literals;++I) Emit(In[At++]);
        if(At==N) {
            if(Produced!=Expected || (HadMatch && (Literals<5 || Expected-LastMatch<12)))
                return Bad(Why,"NSO LZ4 terminal sequence refused");
            DigestOut=H.Finish();return true;
        }
        if(N-At<2) return Bad(Why,"NSO LZ4 truncated offset");
        unsigned Offset=unsigned(In[At])|(unsigned(In[At+1])<<8);At+=2;
        if(!Offset||Offset>Produced) return Bad(Why,"NSO LZ4 history offset refused");
        std::uint64_t Match;
        if(!Length(Token&15,Match)||Match>Expected-Produced||Expected-Produced-Match<4)
            return Bad(Why,"NSO LZ4 match length refused");
        Match+=4;HadMatch=true;LastMatch=Produced;
        for(std::uint64_t I=0;I<Match;++I) Emit(History[std::size_t((Produced-Offset)&65535)]);
    }
    return Bad(Why,"NSO LZ4 final literal sequence missing");
}
inline bool Nso(const Bytes& B,Identity& Out,std::string& Why) {
    if(B.size()<256) return Bad(Why,"NSO header truncated");
    if(U32(B,4)!=0 || U32(B,8)!=0 || (U32(B,12)&~63u)) return Bad(Why,"NSO header version or flags refused");
    auto Flags=U32(B,12);Out.Format="NSO";Out.Representation="normal-executable";Out.BuildId=Hex(B.data()+64,32);
    if(Out.BuildId==std::string(64,'0'))return Bad(Why,"NSO module build ID absent");
    Out.HeaderSha256=Digest(B.data(),256);
    std::uint64_t Total=0;const char* Names[]={"text","rodata","data"};
    for(unsigned I=0;I<3;++I) {
        Section S;S.Name=Names[I];auto Base=16+I*16;
        S.FileOffset=U32(B,Base);S.MemoryOffset=U32(B,Base+4);S.MemorySize=U32(B,Base+8);
        S.Compressed=(Flags&(1u<<I))!=0;S.HashRequired=(Flags&(8u<<I))!=0;
        S.FileSize=S.Compressed?U32(B,96+I*4):S.MemorySize;
        S.HeaderSha256=Hex(B.data()+160+I*32,32);
        if(!S.FileSize||!S.MemorySize||S.FileOffset<256 || !Range(S.FileOffset,S.FileSize,B.size()) ||
            !Range(S.MemoryOffset,S.MemorySize,MaxMemoryBytes) || S.MemorySize>MaxMemoryBytes-Total)
            return Bad(Why,"NSO section extent or budget refused");
        Total+=S.MemorySize;
        for(const auto& Prev:Out.Sections)
            if(Overlap(S.FileOffset,S.FileSize,Prev.FileOffset,Prev.FileSize) ||
                Overlap(S.MemoryOffset,S.MemorySize,Prev.MemoryOffset,Prev.MemorySize))
                return Bad(Why,"NSO overlapping section extents");
        if(S.Compressed) {
            if(!Lz4(B.data()+std::size_t(S.FileOffset),std::size_t(S.FileSize),S.MemorySize,S.Sha256,Why))return false;
        }else S.Sha256=Digest(B.data()+std::size_t(S.FileOffset),std::size_t(S.FileSize));
        if(S.HashRequired && S.Sha256!=S.HeaderSha256)return Bad(Why,"NSO decompressed section SHA256 mismatch");
        Out.Sections.push_back(S);
    }
    const auto& Data=Out.Sections[2];
    Out.BssSize=U32(B,60);
    if(!Range(Data.MemoryOffset+Data.MemorySize,Out.BssSize,MaxMemoryBytes)) return Bad(Why,"NSO BSS extent refused");
    auto ModuleAt=U32(B,28),ModuleSize=U32(B,44);
    if(ModuleSize && (ModuleAt<256 || !Range(ModuleAt,ModuleSize,B.size())))return Bad(Why,"NSO module name extent refused");
    for(unsigned At=136;At<160;At+=8)
        if(!Range(U32(B,At),U32(B,At+4),Out.Sections[1].MemorySize))return Bad(Why,"NSO rodata metadata extent refused");
    return true;
}
inline bool Notes(const Bytes& B,std::uint64_t Begin,std::uint64_t Size,std::string& BuildId,
    std::uint64_t& TotalNotes,unsigned& NoteCount,std::string& Why) {
    if(Size>(16ull<<20)||Size>(64ull<<20)-TotalNotes||!Range(Begin,Size,B.size()))return Bad(Why,"ELF note extent or aggregate budget refused");
    TotalNotes+=Size;
    auto At=Begin,End=Begin+Size;
    while(At<End) {
        if(++NoteCount>4096)return Bad(Why,"ELF note count budget refused");
        if(End-At<12)return Bad(Why,"ELF note header truncated");
        auto Name=std::uint64_t(U32(B,std::size_t(At))),Desc=std::uint64_t(U32(B,std::size_t(At+4)));
        auto Type=U32(B,std::size_t(At+8));At+=12;
        auto NamePad=(Name+3)&~std::uint64_t(3),DescPad=(Desc+3)&~std::uint64_t(3);
        if(!Range(At,NamePad,End)||!Range(At+NamePad,DescPad,End))return Bad(Why,"ELF note payload truncated");
        if(Type==3 && Name==4 && std::memcmp(B.data()+std::size_t(At),"GNU\0",4)==0) {
            if(!Desc||Desc>64)return Bad(Why,"ELF GNU build ID length refused");
            auto Id=Hex(B.data()+std::size_t(At+NamePad),std::size_t(Desc));
            if(Id==std::string(std::size_t(Desc)*2,'0')||(!BuildId.empty()&&Id!=BuildId))
                return Bad(Why,"ELF absent or conflicting GNU build IDs");
            BuildId=Id;
        }
        At+=NamePad+DescPad;
    }
    return true;
}
inline bool Elf(const Bytes& B,Identity& Out,std::string& Why) {
    if(B.size()<64)return Bad(Why,"ELF header truncated");
    if(B[4]!=2||B[5]!=1||B[6]!=1||U16(B,18)!=183||U32(B,20)!=1||
        (U16(B,16)!=2&&U16(B,16)!=3)||U16(B,52)!=64)
        return Bad(Why,"ELF must be ELF64 little-endian AArch64 executable");
    auto Ph=U64(B,32),Sh=U64(B,40);auto PhN=U16(B,56),ShN=U16(B,60);
    if(!PhN||PhN>512||U16(B,54)!=56||Ph<64||!Range(Ph,std::uint64_t(PhN)*56,B.size()) ||
        (ShN && (ShN>8192||U16(B,58)!=64||Sh<64||!Range(Sh,std::uint64_t(ShN)*64,B.size()))) ||
        (!ShN&&Sh!=0))return Bad(Why,"ELF header table extent refused");
    if(ShN && U16(B,62)>=ShN)return Bad(Why,"ELF section string table index refused");
    Out.Format="ELF";Out.Representation="normal-executable";Out.HeaderSha256=Digest(B.data(),64);std::uint64_t Total=0,TotalNotes=0;unsigned NoteCount=0;
    bool EntryOk=U64(B,24)==0;unsigned Loads=0;
    for(unsigned I=0;I<PhN;++I) {
        auto At=std::size_t(Ph)+I*56;auto Type=U32(B,At),Flags=U32(B,At+4);
        auto Offset=U64(B,At+8),Address=U64(B,At+16),FileSize=U64(B,At+32),MemSize=U64(B,At+40),Align=U64(B,At+48);
        if(!Range(Offset,FileSize,B.size()) || (Align>1 && ((Align&(Align-1)) || Address%Align!=Offset%Align)))
            return Bad(Why,"ELF program extent or alignment refused");
        if(Type==4 && !Notes(B,Offset,FileSize,Out.BuildId,TotalNotes,NoteCount,Why))return false;
        if(Type!=1)continue;
        if(++Loads>64 || FileSize>MemSize || !MemSize || MemSize>MaxMemoryBytes-Total ||
            !Range(Address,MemSize,std::numeric_limits<std::uint64_t>::max()) || (Flags&~7u))
            return Bad(Why,"ELF load memory extent or budget refused");
        Total+=MemSize;
        for(const auto& Prev:Out.Sections)
            if(Overlap(Address,MemSize,Prev.MemoryOffset,Prev.MemorySize))return Bad(Why,"ELF overlapping load memory");
        Section S;S.Name="load"+std::to_string(I);S.FileOffset=Offset;S.FileSize=FileSize;
        S.MemoryOffset=Address;S.MemorySize=MemSize;S.HeaderSha256=Digest(B.data()+At,56);
        S.Sha256=Digest(B.data()+std::size_t(Offset),std::size_t(FileSize));Out.Sections.push_back(S);
        auto Entry=U64(B,24);if((Flags&1)&&Entry>=Address&&Entry-Address<FileSize)EntryOk=true;
    }
    for(unsigned I=0;I<ShN;++I) {
        auto At=std::size_t(Sh)+I*64;auto Type=U32(B,At+4);
        auto Offset=U64(B,At+24),Size=U64(B,At+32),Align=U64(B,At+48),Ent=U64(B,At+56);
        if((Type!=8 && !Range(Offset,Size,B.size())) || (Align>1&&(Align&(Align-1))) ||
            (Ent && Size%Ent))return Bad(Why,"ELF section extent or alignment refused");
        if(Type==7 && !Notes(B,Offset,Size,Out.BuildId,TotalNotes,NoteCount,Why))return false;
    }
    if(!Loads||!EntryOk||Out.BuildId.empty())return Bad(Why,"ELF load entry or GNU build ID absent");
    return true;
}
} // Detail
inline bool Read(const Bytes& B,Identity& Out,std::string& Why) {
    if(B.size()<4||B.size()>MaxFileBytes)return Detail::Bad(Why,"executable size budget refused");
    Identity Candidate;
    if(std::memcmp(B.data(),"NSO0",4)==0) { if(!Detail::Nso(B,Candidate,Why))return false; }
    else if(B[0]==127&&B[1]=='E'&&B[2]=='L'&&B[3]=='F') { if(!Detail::Elf(B,Candidate,Why))return false; }
    else return Detail::Bad(Why,"executable format refused");
    Candidate.Sha256=Detail::Digest(B.data(),B.size());std::string Hold;
    Candidate.SupportedCodeBuild=RequireSupportedCodeBuild(Candidate,Hold);
    Out=std::move(Candidate);Why.clear();return true;
}
// Deliberately separate: a stale-header emulator dump is never accepted by Read.
// The only admitted expanded representation is the independently pinned file;
// every actual body section must also match the supported section authority.
inline bool ReadExpandedResearch(const Bytes& B,Identity& Out,std::string& Why) {
    if(B.size()<256||B.size()>MaxFileBytes||std::memcmp(B.data(),"NSO0",4)!=0)
        return Detail::Bad(Why,"expanded research executable extent or format refused");
    Identity Candidate;Candidate.Format="NSO-expanded";Candidate.Representation="derived-research";
    Candidate.Sha256=Detail::Digest(B.data(),B.size());
    if(Candidate.Sha256!=SupportedNsoSha256())return Detail::Bad(Why,"expanded research whole-file identity mismatch");
    if(Detail::U32(B,4)||Detail::U32(B,8)||Detail::U32(B,12)!=63)
        return Detail::Bad(Why,"expanded research retained header refused");
    Candidate.BuildId=Detail::Hex(B.data()+64,32);Candidate.HeaderSha256=Detail::Digest(B.data(),256);
    Candidate.BssSize=Detail::U32(B,60);const char* Names[]={"text","rodata","data"};
    for(unsigned I=0;I<3;++I) {
        Section S;S.Name=Names[I];S.MemoryOffset=Detail::U32(B,20+I*16);S.MemorySize=Detail::U32(B,24+I*16);
        S.FileOffset=256+S.MemoryOffset;S.FileSize=S.MemorySize;S.HashRequired=true;
        if(!Detail::Range(S.FileOffset,S.FileSize,B.size())||!Detail::Range(S.MemoryOffset,S.MemorySize,MaxMemoryBytes))
            return Detail::Bad(Why,"expanded research section extent refused");
        S.Sha256=Detail::Digest(B.data()+std::size_t(S.FileOffset),std::size_t(S.FileSize));
        S.HeaderSha256=Detail::Hex(B.data()+160+I*32,32);Candidate.Sections.push_back(S);
    }
    if(!RequireSupportedCodeBuild(Candidate,Why))return false;
    Candidate.SupportedCodeBuild=true;Out=std::move(Candidate);Why.clear();return true;
}
}} // P4MapKit::OriginalExecutable

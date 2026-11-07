#pragma once
// Bounded first-party archive index/LZ4 reader. Borrowed game bytes are input
// only; callers retain the archive, and no original program enters a set.
#include "custom_material_shader_set.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
inline Bytes DecodeBlock(const Bytes& input,std::size_t expected) {
    if(!expected||expected>(1u<<20))throw std::runtime_error("shader archive payload limit");
    Bytes out;out.reserve(expected);std::size_t at=0;
    auto length=[&](unsigned n){std::size_t value=n;if(n==15){unsigned x;do{if(at==input.size())throw std::runtime_error("shader archive truncated LZ4 length");x=input[at++];if(value>expected||x>expected-value)throw std::runtime_error("shader archive LZ4 length limit");value+=x;}while(x==255);}return value;};
    while(at<input.size()){
        auto token=input[at++];auto literals=length(token>>4);
        if(literals>input.size()-at||literals>expected-out.size())throw std::runtime_error("shader archive LZ4 literal bounds");
        out.insert(out.end(),input.begin()+at,input.begin()+at+literals);at+=literals;
        if(at==input.size())break;
        if(input.size()-at<2)throw std::runtime_error("shader archive truncated LZ4 match");
        unsigned offset=unsigned(input[at])|unsigned(input[at+1])<<8;at+=2;
        auto count=length(token&15)+4;
        if(!offset||offset>out.size()||count>expected-out.size())throw std::runtime_error("shader archive LZ4 match bounds");
        for(std::size_t i=0;i<count;++i)out.push_back(out[out.size()-offset]);
    }
    if(out.size()!=expected)throw std::runtime_error("shader archive decompressed size mismatch");return out;
}
class ShaderArchive {
    const Bytes& data;
    struct Array {std::size_t at=0,count=0,stride=0;};
    std::array<Array,6> arrays;std::size_t payload_at=0;
    std::map<Bytes,std::size_t> maps;
    std::size_t entry(unsigned array,std::size_t index)const{auto a=arrays[array];if(index>=a.count)throw std::runtime_error("shader archive index bounds");return a.at+index*a.stride;}
    std::uint64_t word64(std::size_t at)const{return std::uint64_t(Read32(data,at))|std::uint64_t(Read32(data,at+4))<<32;}
public:
    explicit ShaderArchive(const Bytes& input):data(input){
        if(data.size()>(1ull<<31)||Read32(data,0)!=2)throw std::runtime_error("shader archive version/size unsupported");
        std::size_t at=4;unsigned i=0;for(unsigned stride:{20u,20u,16u,17u,16u,4u}){
            auto n=Read32(data,at);at+=4;if(n>2000000||at>data.size()||n>(data.size()-at)/stride)throw std::runtime_error("shader archive array bounds");
            arrays[i++]={at,n,stride};at+=std::size_t(n)*stride;
        }payload_at=at;
        if(arrays[0].count!=arrays[2].count||arrays[1].count!=arrays[3].count)throw std::runtime_error("shader archive array count mismatch");
        for(std::size_t j=0;j<arrays[0].count;++j){at=entry(0,j);Bytes hash(data.begin()+at,data.begin()+at+20);if(!maps.emplace(hash,j).second)throw std::runtime_error("shader archive duplicate map hash");}
    }
    std::vector<unsigned> Map(const Bytes& hash)const{
        auto found=maps.find(hash);if(found==maps.end())throw std::runtime_error("material resource hash absent from shader archive");
        auto at=entry(2,found->second);auto first=Read32(data,at),count=Read32(data,at+4),preload=Read32(data,at+8),preloads=Read32(data,at+12);
        if(!count||count>4096||first>arrays[5].count||count>arrays[5].count-first||preload>arrays[4].count||preloads>arrays[4].count-preload)throw std::runtime_error("shader archive map bounds");
        std::vector<unsigned> out;for(unsigned i=0;i<count;++i){auto index=Read32(data,entry(5,first+i));(void)entry(3,index);out.push_back(index);}return out;
    }
    unsigned Frequency(unsigned index)const{return data[entry(3,index)+16];}
    std::string SourceSha256()const{return Hex(Sha256(data));}
    Bytes OriginalIdentity(unsigned index)const{auto at=entry(1,index);return Bytes(data.begin()+at,data.begin()+at+20);}
    Bytes Payload(unsigned index)const{
        auto at=entry(3,index);auto offset=word64(at);auto size=Read32(data,at+8),full=Read32(data,at+12);
        if(!size||!full||size>(1u<<20)||full>(1u<<20)||offset>data.size()-payload_at||size>data.size()-payload_at-offset)throw std::runtime_error("shader archive payload bounds");
        at=payload_at+std::size_t(offset);Bytes raw(data.begin()+at,data.begin()+at+size);
        Bytes out=size==full?std::move(raw):DecodeBlock(raw,full);
        // Archive ShaderHashes are UE compiler identities, not SHA1(payload).
        // Source provenance binds the user's entire archive separately. The
        // emitted format3 payload receives its own byte checksum in the writer.
        return out;
    }
};
} } }

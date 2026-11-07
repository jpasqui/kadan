#include "core/export_container.h"
#include <cassert>
#include <fstream>
#include <iostream>

static void U32(std::vector<std::uint8_t>& B, std::uint32_t N) {
    for (unsigned I = 0; I < 4; ++I) B.push_back(std::uint8_t(N >> (8*I)));
}
static void U64(std::vector<std::uint8_t>& B, std::uint64_t N) {
    for (unsigned I = 0; I < 8; ++I) B.push_back(std::uint8_t(N >> (8*I)));
}
static std::vector<std::uint8_t> Header(bool Redirect = true) {
    std::vector<std::uint8_t> B; U64(B,7); U32(B,2); U32(B,0); U32(B,0); U32(B,2);
    U64(B,11); U64(B,22); U32(B,64); B.resize(B.size()+64); U32(B,0);
    U32(B,Redirect ? 1 : 0); if (Redirect) { U64(B,33); U64(B,22); } return B;
}
int main(int Argc, char** Argv) {
    using namespace P4MapKit::Export;
    std::string Error; ContainerHeader Result;
    auto B = Header(); assert(ReadContainerHeader(B,Result,Error));
    assert(Result.Id == 7 && Result.Packages.size() == 2 && Result.Redirects[0].first == 33);
    // Every truncation boundary refuses without changing the last verified result.
    for (std::size_t N=0; N<B.size(); ++N) {
        std::vector<std::uint8_t> Short(B.begin(),B.begin()+N);
        assert(!ReadContainerHeader(Short,Result,Error)); assert(Result.Id == 7);
    }
    auto Bad=B; Bad[12]=255; Bad[13]=255; Bad[14]=255; Bad[15]=255;
    assert(!ReadContainerHeader(Bad,Result,Error)); // bounds on name blob
    Bad=B; Bad[8]=3; assert(!ReadContainerHeader(Bad,Result,Error)); // declared count
    Bad=B; Bad[32]=11; assert(!ReadContainerHeader(Bad,Result,Error)); // duplicate package
    Bad=B; Bad[Bad.size()-8]=44; assert(!ReadContainerHeader(Bad,Result,Error)); // redirect into absent package
    Bad=B; Bad.push_back(1); assert(!ReadContainerHeader(Bad,Result,Error)); // opaque trailing payload
    Bad=B; Bad.resize(Bad.size()+15,0); assert(ReadContainerHeader(Bad,Result,Error));
    Bad.push_back(0); assert(!ReadContainerHeader(Bad,Result,Error)); // excessive padding
    Bad=B; Bad.resize((Bad.size()+4095)/4096*4096,0);
    assert(ReadContainerHeader(Bad,Result,Error)); // actual IoStore page alignment
    Bad.back()=1; assert(!ReadContainerHeader(Bad,Result,Error));
    Bad.back()=0; Bad.push_back(0); assert(!ReadContainerHeader(Bad,Result,Error));
    Bad=B; Bad.resize(Bad.size()+4096,0); assert(!ReadContainerHeader(Bad,Result,Error));
    B=Header(false); assert(ReadContainerHeader(B,Result,Error)); assert(Result.Redirects.empty());
    assert(CheckSelectedContainer(Result,{11,22},Error));
    Result.Redirects.emplace_back(11,11); Result.Redirects.emplace_back(22,22);
    assert(CheckSelectedContainer(Result,{11,22},Error)); // actual retoc identity records
    assert(!CheckSelectedContainer(Result,{11},Error)); // self identity outside selection
    assert(!CheckSelectedContainer(Result,{11,22,33},Error)); // missing selected package
    assert(!CheckSelectedContainer(Result,{11,22,22},Error)); // duplicate selection
    assert(!CheckSelectedContainer(Result,{},Error));
    Result.Redirects.emplace_back(33,33);
    assert(!CheckSelectedContainer(Result,{11,22},Error)); // unselected alias even if identity
    Result.Redirects.pop_back(); Result.Redirects[0].second=22;
    assert(!CheckSelectedContainer(Result,{11,22},Error)); // nonidentity selected target
    Result.Redirects[0].second=11; Result.CultureRedirects.emplace_back(11,11);
    assert(!CheckSelectedContainer(Result,{11,22},Error)); // culture identity remains refused
    // Culture redirects are game claims too, even when the source ID is not stored locally.
    B=Header(false); B.resize(B.size()-8); U32(B,1); U32(B,3);
    B.push_back('e'); B.push_back('n'); B.push_back(0);
    U32(B,1); U64(B,44); U64(B,22); U32(B,0);
    assert(ReadContainerHeader(B,Result,Error));
    assert(Result.CultureRedirects.size()==1 && Result.CultureRedirects[0].first==44 && Result.CultureRedirects[0].second==22);
    Bad=B; Bad[Bad.size()-12]=55; assert(!ReadContainerHeader(Bad,Result,Error));
    assert(Result.CultureRedirects[0].first==44); // invalid target preserves prior census
    // Signed FString overflow and unterminated culture map cannot consume arbitrary memory.
    B=Header(false); B.resize(B.size()-8); U32(B,1); U32(B,0x80000000u);
    assert(!ReadContainerHeader(B,Result,Error));
    if(Argc==2) {
        std::ifstream Input(Argv[1],std::ios::binary|std::ios::ate);
        const auto Size=Input.tellg();
        if(!Input || Size<=0 || Size>64*1024*1024) return 2;
        std::vector<std::uint8_t> Actual(static_cast<std::size_t>(Size));
        Input.seekg(0); Input.read(reinterpret_cast<char*>(Actual.data()),Size);
        if(!Input || !ReadContainerHeader(Actual,Result,Error)) { std::cerr<<Error<<'\n'; return 3; }
        std::cout<<"Actual header packages="<<Result.Packages.size()<<", cultures="<<Result.CultureRedirects.size()
                 <<", redirects="<<Result.Redirects.size()<<'\n';
    } else if(Argc!=1) return 2;
    return 0;
}

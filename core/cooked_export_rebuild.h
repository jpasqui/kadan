// Rebuild a fully admitted, relocatable native cook. The caller must prove its
// native export payloads contain no absolute inline-bulk offsets before use.
#pragma once
#include "export_package.h"
namespace P4MapKit { namespace Export {
inline bool RebuildCookedExports(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,
    const std::vector<std::string>& Names,const std::map<std::size_t,std::vector<std::uint8_t>>& Replacements,
    RewrittenPackage& Out,std::string& Error)
{
    using namespace PackageRewriteDetail;
    const auto Fail=[&](const char* Why){Error=Why;return false;};
    RewrittenPackage Checked;if(!RewriteCookedNames(Header,Body,{},Checked,Error))return false;
    const auto Before=ReadPackageHeader(Header,Body.size());
    if(Names.size()<Before.Names.size()||Names.size()>1000000||!std::equal(Before.Names.begin(),Before.Names.end(),Names.begin()))return Fail("rebuild-name-prefix-changed");
    for(const auto& Change:Replacements)if(Change.first>=Before.Exports.size())return Fail("rebuild-export-index");
    FByteReader Summary(Header,20);std::size_t Count=0,Declared=0;std::string Text;
    if(!Summary.Count(Count,1000)||!Summary.Skip(Count*20))return Fail("rebuild-custom-versions");
    const auto HeaderSizeAt=Summary.Tell();
    if(!Summary.Count(Declared,Header.size())||!Summary.String(Text))return Fail("rebuild-prefix");
    const auto FlagsAt=Summary.Tell();
    if(!Summary.Skip(72)||!Summary.Count(Count,100)||!Summary.Skip(Count*8))return Fail("rebuild-generations");
    for(int N=0;N<2;++N)if(!Summary.Skip(10)||!Summary.String(Text))return Fail("rebuild-engine-version");
    if(!Summary.Skip(8)||!Summary.Skip(4)||!Summary.Count(Count,100))return Fail("rebuild-compression");
    for(std::size_t N=0;N<Count;++N)if(!Summary.String(Text))return Fail("rebuild-package-string");
    const auto BulkAt=Summary.Tell()+4;
    std::uint64_t NamesAt=0,ExportsAt=0;
    if(!Scalar(Header,FlagsAt+8,NamesAt)||!Scalar(Header,FlagsAt+24,ExportsAt))return Fail("rebuild-tables");
    FByteReader Reader(Header,std::size_t(NamesAt));
    for(std::size_t N=0;N<Before.Names.size();++N)if(!Reader.String(Text)||!Reader.Skip(4))return Fail("rebuild-names");
    RewrittenPackage Result;Result.Header=Header;Result.Changed=!Replacements.empty()||Names.size()!=Before.Names.size();
    if(Names.size()!=Before.Names.size())
    {
        std::size_t PoolSize=Reader.Tell()-std::size_t(NamesAt);
        for(std::size_t N=Before.Names.size();N<Names.size();++N)
        {
            if(!AsciiName(Names[N])||Names[N].size()+9>64*1024*1024-PoolSize)return Fail("rebuild-new-name-size");
            PoolSize+=Names[N].size()+9;
        }
        if(PoolSize>64*1024*1024-Header.size())return Fail("rebuild-header-size");
        const auto NewNamesAt=Result.Header.size();
        Result.Header.insert(Result.Header.end(),Header.begin()+std::size_t(NamesAt),Header.begin()+Reader.Tell());
        for(std::size_t N=Before.Names.size();N<Names.size();++N)
        {
            if(!AsciiName(Names[N])||std::count(Names.begin(),Names.end(),Names[N])!=1)return Fail("rebuild-new-name-identity");
            String(Result.Header,Names[N]);const auto At=Result.Header.size();Result.Header.resize(At+4);Put(Result.Header,At,0);
        }
        Put(Result.Header,FlagsAt+4,Names.size());Put(Result.Header,FlagsAt+8,NewNamesAt);
    }
    std::vector<std::pair<std::size_t,std::size_t>> Order;
    for(std::size_t N=0;N<Before.Exports.size();++N)Order.emplace_back(Before.Exports[N].Offset,N);
    std::sort(Order.begin(),Order.end());
    for(const auto& Item:Order)
    {
        const auto Index=Item.second;const auto& Old=Before.Exports[Index];const auto Change=Replacements.find(Index);
        const auto Size=Change==Replacements.end()?Old.Size:Change->second.size();
        if(Size>64*1024*1024-Result.Body.size())return Fail("rebuild-body-size");
        Put(Result.Header,std::size_t(ExportsAt)+Index*104+28,Size,8);
        Put(Result.Header,std::size_t(ExportsAt)+Index*104+36,Result.Header.size()+Result.Body.size(),8);
        if(Change==Replacements.end())Result.Body.insert(Result.Body.end(),Body.begin()+Old.Offset,Body.begin()+Old.Offset+Old.Size);
        else Result.Body.insert(Result.Body.end(),Change->second.begin(),Change->second.end());
    }
    Put(Result.Header,HeaderSizeAt,Result.Header.size());Put(Result.Header,BulkAt,Result.Header.size()+Result.Body.size(),8);
    Result.Body.insert(Result.Body.end(),{0xc1,0x83,0x2a,0x9e});
    RewrittenPackage Verified;if(!RewriteCookedNames(Result.Header,Result.Body,{},Verified,Error))return false;
    const auto After=ReadPackageHeader(Result.Header,Result.Body.size());
    if(After.Names!=Names||After.Exports.size()!=Before.Exports.size())return Fail("rebuild-readback");
    for(std::size_t N=0;N<Before.Exports.size();++N)if(!Replacements.count(N))
    {
        const auto& A=Before.Exports[N];const auto& B=After.Exports[N];
        if(A.Size!=B.Size||!std::equal(Body.begin()+A.Offset,Body.begin()+A.Offset+A.Size,Result.Body.begin()+B.Offset))return Fail("rebuild-unrelated-export-changed");
    }
    Out=std::move(Result);Error.clear();return true;
}
} }
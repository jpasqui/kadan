#pragma once
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace P4MapKit {
// retoc list --package --path describes original directory-index casing. A
// logical package name cannot be used as a case-sensitive extraction filter.
inline bool ReadOriginalSourcePaths(const std::string& Text, std::map<std::uint64_t,std::string>& Paths, std::string& Error) {
    Paths.clear(); Error.clear();
    if(Text.empty()||Text.size()>64u*1024u*1024u) { Error="source-path-list-limit"; return false; }
    std::istringstream Input(Text); std::string Line; std::size_t Count=0;
    while(std::getline(Input,Line)) {
        if(++Count>200000||Line.size()>8192) { Error="source-path-row-limit"; return false; }
        if(Line.empty()||Line=="\r") continue;
        std::istringstream Row(Line); std::vector<std::string> Words; std::string Word;
        while(Row>>Word) Words.push_back(Word);
        if(Words.size()!=5) { Error="source-path-row"; return false; }
        if(Words[3]!="ExportBundleData"||Words[4]=="-") continue;
        if(Words[1].size()!=24) { Error="source-path-chunk"; return false; }
        for(const char C:Words[1]) if(!((C>='0'&&C<='9')||(C>='a'&&C<='f')||(C>='A'&&C<='F'))) { Error="source-path-chunk"; return false; }
        std::uint64_t Id=0;
        if(Words[2].empty()) { Error="source-path-id"; return false; }
        for(const char C:Words[2]) {
            if(C<'0'||C>'9'||Id>(UINT64_MAX-std::uint64_t(C-'0'))/10) { Error="source-path-id"; return false; }
            Id=Id*10+std::uint64_t(C-'0');
        }
        const auto Nibble=[](char C) { return C<='9'?C-'0':(C<='F'?C-'A'+10:C-'a'+10); };
        std::uint64_t ChunkId=0;
        for(unsigned I=0;I<8;++I) ChunkId|=std::uint64_t((Nibble(Words[1][2*I])<<4)|Nibble(Words[1][2*I+1]))<<(8*I);
        if(ChunkId!=Id) { Error="source-path-id-mismatch"; return false; }
        if(!Paths.emplace(Id,Words[4]).second) { Error="source-path-ambiguous"; return false; }
    }
    if(Paths.empty()) { Error="source-path-list-empty"; return false; } return true;
}
inline bool OriginalSourcePath(const std::map<std::uint64_t,std::string>& Paths,std::uint64_t Id,
    const std::string& ExpectedRelative,std::string& Relative,std::string& Error) {
    Relative.clear(); Error.clear(); const auto Found=Paths.find(Id);
    if(Found==Paths.end()) { Error="source-path-missing"; return false; }
    const auto& Path=Found->second;
    if(Path.compare(0,9,"../../../")!=0) { Error="source-path-prefix"; return false; }
    const auto Actual=Path.substr(9);
    const auto Lower=[](std::string Name) { for(char& C:Name) if(C>='A'&&C<='Z') C=char(C+'a'-'A'); return Name; };
    if(ExpectedRelative.compare(0,16,"Carrot4/Content/")!=0||ExpectedRelative.find("..")!=std::string::npos||
        Actual.find("..")!=std::string::npos||Lower(Actual)!=Lower(ExpectedRelative)) { Error="source-path-identity"; return false; }
    Relative=Actual; return true;
}
}

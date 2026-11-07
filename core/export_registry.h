// Bounded incremental package-name census of retoc's asset-registry debug output.
// Non-package metadata is discarded while streaming, including arbitrarily long lines.
#pragma once
#include "package_reader.h"
namespace P4MapKit { namespace Export {
class RegistryPackageLines
{
    enum class State { Prefix, Name, Tail, Ignore } Mode = State::Prefix;
    std::size_t PrefixAt = 0, Total = 0;
    std::string Name;
    bool Comma = false, Any = false;
    bool LineEnd()
    {
        if (Mode == State::Name) return Fail("unterminated-registry-package");
        if (Mode == State::Tail)
        {
            if (Names.size() >= 1000000 || Name.size() > 32*1024*1024-Total) return Fail("registry-census-bound");
            std::vector<std::uint8_t> String(4);
            const auto Length=Name.size()+1;
            for(std::size_t I=0;I<4;++I) String[I]=std::uint8_t(Length>>(I*8));
            String.insert(String.end(),Name.begin(),Name.end()); String.push_back(0);
            FByteReader Read(String); std::string Valid;
            if(!Read.String(Valid)) return Fail("invalid-registry-package-encoding");
            Total+=Name.size(); Names.push_back(std::move(Valid));
        }
        Mode=State::Prefix; PrefixAt=0; Name.clear(); Comma=false; Any=false; return true;
    }
    bool Fail(const char* Why) { Error=Why; return false; }
public:
    std::vector<std::string> Names;
    std::string Error;
    bool Feed(const std::uint8_t* Bytes,std::size_t Size)
    {
        if(!Error.empty()) return false;
        static const std::string Prefix="package_name: \"";
        for(std::size_t I=0;I<Size;++I)
        {
            const auto C=Bytes[I];
            if(C=='\n') { if(!LineEnd()) return false; continue; }
            Any=true;
            if(Mode==State::Ignore) continue;
            if(Mode==State::Prefix)
            {
                if(!PrefixAt && (C==' ' || C=='\t' || C=='\r')) continue;
                if(C!=std::uint8_t(Prefix[PrefixAt])) { Mode=State::Ignore; continue; }
                if(++PrefixAt==Prefix.size()) Mode=State::Name;
            }
            else if(Mode==State::Name)
            {
                if(C=='"') Mode=State::Tail;
                else
                {
                    if(C<0x20 || C=='\\' || Name.size()>=65535) return Fail("invalid-registry-package-line");
                    Name.push_back(char(C));
                }
            }
            else
            {
                if(C==',' && !Comma) Comma=true;
                else if(C!=' ' && C!='\t' && C!='\r') return Fail("registry-package-trailing-data");
            }
        }
        return true;
    }
    bool Finish() { return Error.empty() && (!Any || LineEnd()); }
};
} }

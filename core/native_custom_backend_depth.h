#pragma once
#include "custom_material_bindings.h"
#include <regex>
#include <sstream>
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
inline bool KnownMaskPass(std::uint64_t h){return h==0x37a48ab631bfc57aull||h==0x4bbfae84d2dbe82aull||h==0x7557e6252b46eb81ull||h==0x925355faf735b81cull;}
inline std::string OriginalAssemblyMetadata(const Bytes& payload,const ShaderBindings& bindings){
    auto at=ShaderHeader(bindings).size();auto code=Read32(payload,at);at+=4;
    if(at>payload.size()||code>payload.size()-at)throw std::runtime_error("depth template code bounds");at+=code;
    auto control=Read32(payload,at);at+=4;if(control<0x878||at>payload.size()||control>payload.size()-at)throw std::runtime_error("depth template control bounds");
    auto textAt=Read32(payload,at+0x14),textSize=Read32(payload,at+0x18);
    if(textAt!=0x878||textSize>(1u<<18)||textAt>control||textSize>control-textAt)throw std::runtime_error("depth template assembly bounds");
    std::string text(payload.begin()+at+textAt,payload.begin()+at+textAt+textSize);
    for(unsigned char c:text)if((c<32&&c!='\n'&&c!='\r'&&c!='\t')||c>126)throw std::runtime_error("depth template assembly metadata invalid");return text;
}
inline std::string KeepMaskDepthMetadata(std::string own,const std::string& original){
    std::istringstream lines(original);std::string line;unsigned writes=0,loads=0;std::string offset;
    std::regex load("^LDC\\.F32X4 \\w+\\.w, buf1\\[([0-9]+)\\];$");
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.find("result.depth")!=std::string::npos){++writes;if(line.find("ADD.F32.SAT result.depth.z, ")!=0)throw std::runtime_error("depth output shape unqualified");}
        std::smatch match;if(std::regex_match(line,match,load)){++loads;offset=match[1];}
    }
    if(!writes)return own;
    if(writes!=1||loads!=1||offset.size()>6||std::stoul(offset)>65535)throw std::runtime_error("depth pass binding metadata unqualified");
    const std::string buffer="CBUFFER buf1[] = { program.buffer[1] };\n";
    if(own.find(buffer)==std::string::npos){const std::string first="CBUFFER buf0[] = { program.buffer[0] };\n";auto at=own.find(first);if(at==std::string::npos)throw std::runtime_error("mask graph buffer shape");own.insert(at+first.size(),buffer);}
    auto output=own.find("OUTPUT ");if(output==std::string::npos)throw std::runtime_error("mask graph output shape");own.insert(output,"TEMP P4D;\n");
    auto end=own.rfind("END\n");if(end==std::string::npos||end+4!=own.size())throw std::runtime_error("mask graph program end");
    own.insert(end,"LDC.F32X4 P4D.w, buf1["+offset+"];\nMUL.F32 P4D.x, fragment.attrib[{VAR:in_texcoord6}], P4D.w;\nADD.F32.SAT result.depth.z, P4D.x, fragment.attrib[{VAR:in_texcoord8}].x;\n");return own;
}
} } }

// Logical Settings cook data comes from actual native fields. Original metadata
// supplies only tag identity/order; no source value bytes enter this interface.
#pragma once
#include "package_reader.h"
#include <set>

namespace P4MapKit { namespace PostProcessCook {
using Bytes=std::vector<std::uint8_t>;
struct FLogicalSettings { Bytes Payload; std::vector<std::string> Names; };
// Project away every source value/span, including BoolValue stored in a tag header.
struct FSourceTag
{
    std::string Name,Type;
    std::vector<std::string> Detail;
    std::size_t ArrayIndex=0;
    bool HasGuid=false;
    std::array<std::uint32_t,4> StructGuid{},PropertyGuid{};
    FSourceTag()=default;
    FSourceTag(const FPropertyEnvelope& Source):Name(Source.Name),Type(Source.Type),Detail(Source.Detail),
        ArrayIndex(Source.ArrayIndex),HasGuid(Source.HasGuid),StructGuid(Source.StructGuid),PropertyGuid(Source.PropertyGuid){}
};
namespace Detail {
inline void U(Bytes& Out,std::uint64_t Value,std::size_t Width=4)
{ for(std::size_t N=0;N<Width;++N)Out.push_back(std::uint8_t(Value>>(N*8))); }
// A name as the engine keeps it: a trailing "_" and number is the name's number (one more than written), as the engine
// splits it when it makes the name (not for a number with a leading zero, "Rocket_04"). The reader joins them again.
inline void SplitNumber(const std::string& Value,std::string& Base,std::uint32_t& Number)
{
    Base=Value;Number=0;
    std::size_t Digits=0;
    while(Digits<Value.size()&&Value[Value.size()-1-Digits]>='0'&&Value[Value.size()-1-Digits]<='9')++Digits;
    if(!Digits||Digits>10||Digits+1>=Value.size()||Value[Value.size()-1-Digits]!='_')return;
    if(Digits>1&&Value[Value.size()-Digits]=='0')return;
    const unsigned long long Written=std::stoull(Value.substr(Value.size()-Digits));
    if(Written>=0x7fffffffULL)return;
    Base=Value.substr(0,Value.size()-1-Digits);Number=std::uint32_t(Written+1);
}
inline bool Name(Bytes& Out,std::vector<std::string>& Names,const std::string& Value)
{
    if(Value.empty()||Value.size()>65535||Names.size()>1000000)return false;
    for(const unsigned char C:Value)if(C<0x20||C>0x7e)return false;
    std::string Base;std::uint32_t Number=0;SplitNumber(Value,Base,Number);
    auto It=std::find(Names.begin(),Names.end(),Base);
    if(It==Names.end()){Names.push_back(Base);It=Names.end()-1;}
    if(std::count(Names.begin(),Names.end(),Base)!=1)return false;
    U(Out,std::size_t(It-Names.begin()));U(Out,Number);return true;
}
template<class T> inline std::string Key(const T& P){return P.Name+"["+std::to_string(P.ArrayIndex)+"]";}
struct FType { const char* Type; const char* Struct; std::size_t Size; };
inline bool ForkType(const std::string& Name,FType& Out)
{
    static const std::set<std::string> Bits={
        "bOverride_C4IndirectLightingVolumetricLightmapIntensity","bOverride_C4IndirectLightingSpecularColor",
        "bOverride_C4IndirectLightingSpecularIntensity","bOverride_DepthOfFieldTiltAngles","bOverride_C4DepthOfFieldMask",
        "bOverride_C4DepthOfFieldParam0","bOverride_C4DepthOfFieldParam1","bOverride_C4DepthOfFieldParam2",
        "bOverride_C4DepthOfFieldParam3","bOverride_C4PostProcessParam0","bOverride_DepthOfFieldMethod"};
    if(Bits.count(Name)){Out={"BoolProperty","",0};return true;}
    if(Name=="C4IndirectLightingVolumetricLightmapIntensity"||Name=="C4IndirectLightingSpecularIntensity")Out={"FloatProperty","",4};
    else if(Name=="C4IndirectLightingSpecularColor")Out={"StructProperty","LinearColor",16};
    else if(Name=="DepthOfFieldTiltAngles")Out={"StructProperty","Vector2D",8};
    else if(Name=="C4DepthOfFieldMask")Out={"ObjectProperty","",4};
    else if(Name=="C4DepthOfFieldParam0"||Name=="C4DepthOfFieldParam1"||Name=="C4DepthOfFieldParam2"||Name=="C4DepthOfFieldParam3"||Name=="C4PostProcessParam0")Out={"StructProperty","Vector4",16};
    else if(Name=="DepthOfFieldMethod")Out={"ByteProperty","EP4RecoveredDepthOfFieldMethod",8};
    else return false;
    return true;
}
inline bool Matches(const FPropertyEnvelope& P,const FType& T)
{
    return P.Type==T.Type&&P.Size==T.Size&&P.ArrayIndex==0&&
        (std::string(T.Struct).empty()?P.Detail.empty():P.Detail==std::vector<std::string>{T.Struct});
}
inline bool Tag(Bytes& Out,std::vector<std::string>& Names,const FSourceTag& Metadata,
    const FPropertyEnvelope& Actual,const Bytes& Payload)
{
    if(!Name(Out,Names,Metadata.Name)||!Name(Out,Names,Metadata.Type))return false;
    U(Out,Payload.size());U(Out,Metadata.ArrayIndex);
    for(const auto& Type:Metadata.Detail)if(!Name(Out,Names,Type))return false;
    if(Metadata.Type=="StructProperty")for(const auto Word:Metadata.StructGuid)U(Out,Word);
    if(Metadata.Type=="BoolProperty")U(Out,Actual.BoolValue?1:0,1);
    U(Out,Metadata.HasGuid?1:0,1);if(Metadata.HasGuid)for(const auto Word:Metadata.PropertyGuid)U(Out,Word);
    Out.insert(Out.end(),Payload.begin(),Payload.end());return true;
}
}
// Strict sparse source order is deliberate. SourceTags come from the game's file, or
// from the record of that file's tags an imported level keeps, bound to the file's
// source digest; an order is never made up. Missing values are never filled from
// source or defaults; unknown, extra or duplicate native storage must be diagnosed.
inline bool Merge(const std::vector<std::string>& NativeNames,const Bytes& Stock,const Bytes& Fork,
    const std::vector<FSourceTag>& SourceTags,FLogicalSettings& Out,std::string& Error)
{
    const auto Fail=[&](const std::string& Why){Error="postprocess-cook-"+Why;return false;};
    if(Stock.size()>64*1024*1024||Fork.size()>64*1024*1024||SourceTags.size()>100000||NativeNames.size()>1000000)return Fail("size-bound");
    const auto S=ReadPropertyEnvelopes(Stock,NativeNames,0,Stock.size());
    const auto F=ReadPropertyEnvelopes(Fork,NativeNames,0,Fork.size());
    if(!S.Error.Code.empty()||S.End!=Stock.size()||!F.Error.Code.empty()||F.End!=Fork.size())return Fail("native-tag-stream-invalid");
    struct FValue {const FPropertyEnvelope* Tag;const Bytes* Body;bool Fork;};
    std::map<std::string,FValue> Values;
    for(const auto& P:S.Properties)
    {
        Detail::FType Type;
        if(Detail::ForkType(P.Name,Type))return Fail("fork-field-in-stock-Settings:"+P.Name);
        Values.emplace(Detail::Key(P),FValue{&P,&Stock,false});
    }
    for(const auto& P:F.Properties)
    {
        Detail::FType Type;
        if(!Detail::ForkType(P.Name,Type)||!Detail::Matches(P,Type))return Fail("native-fork-field-invalid:"+P.Name);
        if(!Values.emplace(Detail::Key(P),FValue{&P,&Fork,true}).second)return Fail("native-field-duplicate:"+P.Name);
    }
    if(Values.size()!=SourceTags.size())return Fail("source-native-member-set-differs");
    FLogicalSettings Result;Result.Names=NativeNames;std::set<std::string> Seen;
    for(const auto& Source:SourceTags)
    {
        if(!Seen.insert(Detail::Key(Source)).second)return Fail("source-order-duplicate:"+Source.Name);
        const auto Found=Values.find(Detail::Key(Source));if(Found==Values.end())return Fail("source-native-member-missing:"+Source.Name);
        const auto& Value=Found->second;const auto& Actual=*Value.Tag;
        if(Source.Type!=Actual.Type||Source.ArrayIndex!=Actual.ArrayIndex||
            (Source.Name!="DepthOfFieldMethod"&&Source.Detail!=Actual.Detail))return Fail("source-native-member-type-differs:"+Source.Name);
        Bytes Payload(Value.Body->begin()+Actual.PayloadOffset,Value.Body->begin()+Actual.PayloadOffset+Actual.Size);
        if(Value.Fork&&Source.Name=="DepthOfFieldMethod")
        {
            if(Source.Detail!=std::vector<std::string>{"EDepthOfFieldMethod"})return Fail("source-enum-type-invalid");
            FByteReader Reader(Payload);std::string Native;
            if(!Reader.Name(NativeNames,Native)||Reader.Tell()!=Payload.size())return Fail("native-enum-payload-invalid");
            const std::map<std::string,std::string> Enum={{"P4DOFM_DefaultDOF","DOFM_DefaultDOF"},{"P4DOFM_GatherDOF","DOFM_GatherDOF"},{"P4DOFM_MAX","DOFM_MAX"}};
            const auto Choice=Enum.find(Native);if(Choice==Enum.end())return Fail("native-enum-choice-invalid");
            Payload.clear();if(!Detail::Name(Payload,Result.Names,Choice->second))return Fail("enum-name-identity");
        }
        if(!Detail::Tag(Result.Payload,Result.Names,Source,Actual,Payload))return Fail("tag-name-identity");
    }
    if(!Detail::Name(Result.Payload,Result.Names,"None"))return Fail("terminator-name-identity");
    const auto Check=ReadPropertyEnvelopes(Result.Payload,Result.Names,0,Result.Payload.size());
    if(!Check.Error.Code.empty()||Check.End!=Result.Payload.size()||Check.Properties.size()!=SourceTags.size())return Fail("readback-invalid");
    Out=std::move(Result);Error.clear();return true;
}
} }
// UE4.26 UPhysicsAsset::Serialize writes its collision-disable map after UObject.
#pragma once
#include "level_native.h"
#include "bulk_data.h"
#include <set>

namespace P4MapKit
{
struct FPhysicsCollisionPair
{
    std::int32_t First = 0, Second = 0;
    bool Value = false;
};
struct FPhysicsAssetNative
{
    std::vector<FPhysicsCollisionPair> Disabled;
    FReadError Error;
};
struct FPhysicsBodyNative
{
    std::array<std::uint32_t, 4> Guid{};
    FReadError Error;
};
// Relocation authority only. PhysX cooking produces a package-independent byte
// buffer; parsing its UE bulk container does not qualify the collision geometry.
struct FInlinePhysicsBodyBulk
{
    std::array<std::uint32_t,4> Guid{};
    std::vector<std::string> Formats;
    std::vector<FByteBulkRange> Payloads;
    FReadError Error;
};
// StoredOffset false takes a payload whose header says another place in the file than where it is.
inline FInlinePhysicsBodyBulk ReadInlinePhysicsBodyBulk(const std::vector<std::uint8_t>& Body,
    const std::vector<std::string>& Names,std::size_t Start,std::size_t End,const FByteBulkContext& Context,
    bool StoredOffset=true)
{
    FInlinePhysicsBodyBulk Out;FByteReader R(Body,Start,End);
    if(Context.HeaderSize>std::uint64_t(INT64_MAX)-Body.size())
    {Out.Error={Start,"physics-body-bulk-offset-overflow"};return Out;}
    bool Cooked=false,HasData=false;std::size_t Formats=0;
    bool Good=LevelNative::ObjectId(R);
    for(auto& Part:Out.Guid)Good=Good&&R.U32(Part);
    Good=Good&&LevelNative::Flag(R,Cooked)&&LevelNative::Flag(R,HasData)&&R.Count(Formats,1);
    if(Good&&(!Cooked||Out.Guid==std::array<std::uint32_t,4>{}||HasData!=(Formats!=0)))
        Good=R.Reject("physics-body-bulk-boundary-unqualified");
    for(std::size_t Index=0;Good&&Index<Formats;++Index)
    {
        std::string Format;FByteBulkRange Bulk;
        Good=R.Name(Names,Format);
        if(Good&&Format!="PhysXPC"&&Format!="PhysXGeneric")Good=R.Reject("physics-body-bulk-format-unqualified");
        Good=Good&&ReadByteBulk(R,Context,Bulk);
        if(Good&&(Bulk.Source!=EBulkSource::Body||(Bulk.Header.Flags&BulkAtEnd)||!Bulk.Size||
            Bulk.Offset!=Bulk.Header.End||Bulk.Size>64*1024*1024||
            (StoredOffset&&Bulk.Header.Offset!=std::int64_t(Context.HeaderSize+Bulk.Offset))))
            Good=R.Reject("physics-body-bulk-not-inline");
        if(Good){Out.Payloads.push_back(Bulk);Out.Formats.push_back(Format);}
    }
    std::size_t Used=0;if(Good)LevelNative::Done(R,Used);
    Out.Error=R.Error;if(!Out.Error.Code.empty()){Out.Payloads.clear();Out.Formats.clear();}return Out;
}
inline bool RelocateInlinePhysicsBodyBulk(const std::vector<std::uint8_t>& Before,const std::vector<std::string>& BeforeNames,
    std::size_t OldStart,std::size_t OldEnd,std::uint64_t OldHeaderSize,std::vector<std::uint8_t>& After,
    const std::vector<std::string>& AfterNames,std::size_t NewStart,std::size_t NewEnd,std::uint64_t NewHeaderSize,FReadError& Error)
{
    const auto Fail=[&](const char* Why){Error={OldStart,Why};return false;};
    if(OldHeaderSize>std::uint64_t(INT64_MAX)-Before.size()||NewHeaderSize>std::uint64_t(INT64_MAX)-After.size())
        return Fail("physics-body-relocation-offset-overflow");
    if(OldStart>OldEnd||OldEnd>Before.size()||NewStart>NewEnd||NewEnd>After.size()||OldEnd-OldStart!=NewEnd-NewStart||
        !std::equal(Before.begin()+OldStart,Before.begin()+OldEnd,After.begin()+NewStart))return Fail("physics-body-relocation-native-bytes-changed");
    FByteBulkContext Context;Context.HeaderSize=OldHeaderSize;Context.FileSizes[0]=std::int64_t(Before.size());
    const auto Layout=ReadInlinePhysicsBodyBulk(Before,BeforeNames,OldStart,OldEnd,Context);
    if(!Layout.Error.Code.empty()){Error=Layout.Error;return false;}
    std::vector<std::pair<std::size_t,std::array<std::uint8_t,8>>> Undo;
    for(const auto& Bulk:Layout.Payloads)
    {
        const std::size_t Width=Bulk.Header.Flags&BulkSize64?8:4;
        const std::size_t At=NewStart+(Bulk.Header.Start-OldStart)+4+2*Width;
        std::array<std::uint8_t,8> Saved{};std::copy_n(After.begin()+At,8,Saved.begin());Undo.push_back({At,Saved});
        const std::uint64_t Offset=NewHeaderSize+NewStart+(Bulk.Offset-OldStart);
        for(std::size_t Byte=0;Byte<8;++Byte)After[At+Byte]=std::uint8_t(Offset>>(8*Byte));
    }
    const auto Restore=[&](){for(const auto& Entry:Undo)std::copy(Entry.second.begin(),Entry.second.end(),After.begin()+Entry.first);};
    Context.HeaderSize=NewHeaderSize;Context.FileSizes[0]=std::int64_t(After.size());
    const auto ReadBack=ReadInlinePhysicsBodyBulk(After,AfterNames,NewStart,NewEnd,Context);
    if(!ReadBack.Error.Code.empty()||ReadBack.Guid!=Layout.Guid||ReadBack.Formats!=Layout.Formats||ReadBack.Payloads.size()!=Layout.Payloads.size())
    {Restore();return Fail("physics-body-relocation-readback-failed");}
    for(std::size_t Index=0;Index<Layout.Payloads.size();++Index)
    {
        const auto& Old=Layout.Payloads[Index];const auto& New=ReadBack.Payloads[Index];
        if(Old.Size!=New.Size||!std::equal(Before.begin()+Old.Offset,Before.begin()+Old.Offset+Old.Size,After.begin()+New.Offset))
        {Restore();return Fail("physics-body-relocation-payload-changed");}
    }
    Error={};return true;
}
// A body setup's inline payloads say where they are in the file: the header's size and their place in the body. A
// translation that changes the header's size and not the body leaves them saying the old place. Each is given its own
// place again. Changed says whether one said another.
inline bool PlaceInlinePhysicsBodyBulk(std::vector<std::uint8_t>& Body,const std::vector<std::string>& Names,
    std::size_t Start,std::size_t End,std::uint64_t HeaderSize,bool& Changed,FReadError& Error)
{
    Changed=false;
    FByteBulkContext Context;Context.HeaderSize=HeaderSize;Context.FileSizes[0]=std::int64_t(Body.size());
    const auto Layout=ReadInlinePhysicsBodyBulk(Body,Names,Start,End,Context,false);
    if(!Layout.Error.Code.empty()){Error=Layout.Error;return false;}
    for(const auto& Bulk:Layout.Payloads)
    {
        const std::uint64_t Offset=HeaderSize+Bulk.Offset;
        if(Bulk.Header.Offset==std::int64_t(Offset))continue;
        const std::size_t At=Bulk.Header.Start+4+2*(Bulk.Header.Flags&BulkSize64?8:4);
        for(std::size_t Byte=0;Byte<8;++Byte)Body[At+Byte]=std::uint8_t(Offset>>(8*Byte));
        Changed=true;
    }
    const auto ReadBack=ReadInlinePhysicsBodyBulk(Body,Names,Start,End,Context);
    if(!ReadBack.Error.Code.empty()){Error=ReadBack.Error;return false;}
    Error={};return true;
}
inline FPhysicsBodyNative ReadPrimitivePhysicsBodyNative(const std::vector<std::uint8_t>& Body,
    std::size_t Start, std::size_t End)
{
    FPhysicsBodyNative Out; FByteReader R(Body, Start, End);
    bool Good = LevelNative::ObjectId(R), Cooked = false, HasData = false;
    for (auto& Part : Out.Guid) Good = Good && R.U32(Part);
    Good = Good && LevelNative::Flag(R, Cooked);
    if (Good && !Cooked) Good = R.Reject("physics-body-not-cooked");
    Good = Good && LevelNative::Flag(R, HasData);
    std::size_t Formats = 0;
    Good = Good && R.Count(Formats, 64);
    if (Good && (HasData || Formats)) Good = R.Reject("physics-body-cooked-shapes-not-decoded");
    if (Good && Out.Guid == std::array<std::uint32_t, 4>{}) Good = R.Reject("physics-body-id-invalid");
    std::size_t Used = 0; if (Good) LevelNative::Done(R, Used);
    Out.Error = R.Error; return Out;
}
inline FPhysicsAssetNative ReadPhysicsAssetNative(const std::vector<std::uint8_t>& Body,
    std::size_t Start, std::size_t End)
{
    FPhysicsAssetNative Out;
    FByteReader R(Body, Start, End);
    std::size_t Count = 0;
    if (!LevelNative::ObjectId(R) || !R.Count(Count, 1000000)) { Out.Error = R.Error; return Out; }
    if (Count > (R.Limit() - R.Tell()) / 12) R.Reject("physics-collision-table-truncated");
    std::set<std::pair<std::int32_t, std::int32_t>> Seen;
    for (std::size_t I = 0; I < Count && R.Error.Code.empty(); ++I)
    {
        FPhysicsCollisionPair Pair;
        if (!R.I32(Pair.First) || !R.I32(Pair.Second) || !LevelNative::Flag(R, Pair.Value)) break;
        if (Pair.First < 0 || Pair.First >= Pair.Second) { R.Reject("physics-collision-pair-invalid"); break; }
        if (!Seen.emplace(Pair.First, Pair.Second).second) { R.Reject("physics-collision-pair-duplicate"); break; }
        Out.Disabled.push_back(Pair);
    }
    std::size_t Used = 0;
    LevelNative::Done(R, Used);
    Out.Error = R.Error;
    if (!Out.Error.Code.empty()) Out.Disabled.clear();
    return Out;
}
}

// Renames entries of a fresh UE4.26 cook's name table without changing export data. The editor
// cooks collision for Windows (PhysXPC) and the game reads the same bytes as PhysXGeneric.
#pragma once
#include "package_reader.h"
#include <map>

namespace P4MapKit { namespace Export {
struct RewrittenPackage
{
    std::vector<std::uint8_t> Header, Body;
    bool Changed = false;
};
namespace PackageRewriteDetail
{
inline void Put(std::vector<std::uint8_t>& Bytes, std::size_t At, std::uint64_t Value, std::size_t Width = 4)
{ for (std::size_t Index = 0; Index < Width; ++Index) Bytes[At + Index] = std::uint8_t(Value >> (8 * Index)); }
inline bool Scalar(const std::vector<std::uint8_t>& Bytes, std::size_t At, std::uint64_t& Value, std::size_t Width = 4)
{ FByteReader Reader(Bytes, At); return Reader.Unsigned(Width, Value); }
inline bool AsciiName(const std::string& Name)
{
    if (Name.empty() || Name.size() > 65535) return false;
    for (const unsigned char Byte : Name) if (Byte < 0x20 || Byte > 0x7e) return false;
    return true;
}
inline void String(std::vector<std::uint8_t>& Bytes, const std::string& Value)
{
    const auto At = Bytes.size(); Bytes.resize(At + 4); Put(Bytes, At, Value.size() + 1);
    Bytes.insert(Bytes.end(), Value.begin(), Value.end()); Bytes.push_back(0);
}
}

// The name table is written again at the end of the header and the offsets that follow it are
// moved. Unknown summary bytes and every export and bulk byte stay identical. External bulk
// files (.ubulk, .uptnl) are not touched.
inline bool RewriteCookedNames(const std::vector<std::uint8_t>& Header,
    const std::vector<std::uint8_t>& Body, const std::map<std::string,std::string>& Rewrites,
    RewrittenPackage& Out, std::string& Error)
{
    using namespace PackageRewriteDetail;
    const auto Fail = [&](const char* Why) { Error = Why; return false; };
    const auto Inventory = ReadPackageHeader(Header, Body.size());
    if (!Inventory.Error.Code.empty()) { Error = Inventory.Error.Code; return false; }
    if (!(Inventory.Flags & 0x80000000u) || (Inventory.Flags & 0x2000u)) return Fail("rewrite-requires-versioned-filtered-cook");
    if (Body.size() > 64 * 1024 * 1024 || Rewrites.size() > 4096) return Fail("rewrite-size-bound");
    for (const auto& Pair : Rewrites)
        if (!AsciiName(Pair.first) || !AsciiName(Pair.second)) return Fail("rewrite-name-not-bounded-ascii");
    FByteReader Summary(Header, 20);
    std::size_t CustomCount = 0, DeclaredSize = 0;
    std::string Folder;
    if (!Summary.Count(CustomCount,1000) || !Summary.Skip(CustomCount * 20)) return Fail("rewrite-custom-versions");
    const auto HeaderSizeAt = Summary.Tell();
    if (!Summary.Count(DeclaredSize,Header.size()) || !Summary.String(Folder)) return Fail("rewrite-summary-prefix");
    const auto FlagsAt = Summary.Tell();
    if (!Summary.Skip(72)) return Fail("rewrite-fixed-summary");
    std::size_t Count = 0;
    if (!Summary.Count(Count,100) || !Summary.Skip(Count * 8)) return Fail("rewrite-generations");
    for (int Index = 0; Index < 2; ++Index)
        if (!Summary.Skip(10) || !Summary.String(Folder)) return Fail("rewrite-engine-version");
    std::uint32_t Compression = 0;
    if (!Summary.U32(Compression) || Compression != 0 || !Summary.Count(Count,0)) return Fail("rewrite-compression-unsupported");
    if (!Summary.Skip(4) || !Summary.Count(Count,100)) return Fail("rewrite-additional-packages");
    for (std::size_t Index = 0; Index < Count; ++Index)
        if (!Summary.String(Folder)) return Fail("rewrite-additional-package-string");
    const auto RegistryAt = Summary.Tell();
    std::uint64_t Bulk = 0;
    if (!Summary.Skip(4) || !Summary.Unsigned(8,Bulk)) return Fail("rewrite-bulk-summary");
    const auto BulkAt = RegistryAt + 4, WorldTileAt = Summary.Tell();
    if (!Summary.Skip(4) || !Summary.Count(Count,100) || !Summary.Skip(Count * 4)) return Fail("rewrite-chunk-ids");
    const auto PreloadAt = Summary.Tell();
    std::size_t PreloadCount = 0;
    std::uint32_t PreloadOffset = 0;
    if (!Summary.Count(PreloadCount,10000000) || !Summary.U32(PreloadOffset)) return Fail("rewrite-preload-summary");
    const auto SummaryEnd = Summary.Tell();
    for (const auto At : {FlagsAt+8,FlagsAt+16,FlagsAt+24,FlagsAt+32,FlagsAt+36,
        FlagsAt+44,FlagsAt+48,FlagsAt+52,RegistryAt,WorldTileAt,PreloadAt+4})
    {
        std::uint64_t Offset = 0;
        if (!Scalar(Header,At,Offset) || (Offset && (Offset < SummaryEnd || Offset > Header.size())))
            return Fail("rewrite-summary-offset");
    }
    if ((PreloadCount && !PreloadOffset) || std::uint64_t(PreloadCount) * 4 > Header.size() - PreloadOffset)
        return Fail("rewrite-preload-bounds");
    std::vector<FInventorySpan> Spans;
    for (const auto& Span : Inventory.HeaderSpans) if (Span.Decoded) Spans.push_back(Span);
    Spans.push_back({0,SummaryEnd,"rewrite-summary",true});
    if (PreloadCount) Spans.push_back({PreloadOffset,PreloadCount*4,"rewrite-preload",true});
    if (!PartitionSpans(Header.size(),Spans)) return Fail("rewrite-overlapping-summary-tables");
    auto Ordered = Inventory.Exports;
    std::sort(Ordered.begin(),Ordered.end(),[](const FPackageExport& A,const FPackageExport& B) { return A.Offset < B.Offset; });
    std::size_t End = 0;
    for (const auto& Item : Ordered)
    {
        if (Item.Offset != End || Item.Size > Body.size()-End) return Fail("rewrite-noncontiguous-exports");
        End += Item.Size;
    }
    if (Body.size() - End != 4 || Body[End] != 0xc1 || Body[End+1] != 0x83 || Body[End+2] != 0x2a || Body[End+3] != 0x9e)
        return Fail("rewrite-opaque-body-trailer");
    if (Bulk != Header.size() + End) return Fail("rewrite-bulk-start-mismatch");
    std::uint64_t NamesAt = 0, ExportsAt = 0;
    if (!Scalar(Header,FlagsAt+8,NamesAt) || !Scalar(Header,FlagsAt+24,ExportsAt)) return Fail("rewrite-table-offset");
    FByteReader Names(Header,std::size_t(NamesAt));
    std::vector<std::uint8_t> NewNames;
    bool Changed = false;
    std::map<std::string,std::size_t> ResultCounts;
    for (const auto& OldName : Inventory.Names)
    {
        const auto Start = Names.Tell();
        std::string Name;
        if (!Names.String(Name) || !Names.Skip(4)) return Fail("rewrite-name-table");
        const auto Replacement = Rewrites.find(OldName);
        if (Replacement == Rewrites.end() || Replacement->second == OldName)
            NewNames.insert(NewNames.end(),Header.begin()+Start,Header.begin()+Names.Tell());
        else
        {
            Name = Replacement->second;
            String(NewNames,Name); const auto At = NewNames.size(); NewNames.resize(At+4); Put(NewNames,At,0);
            Changed = true;
        }
        ++ResultCounts[Name];
    }
    // Merging name identities can change numbered FNames and is never a relabel.
    for (const auto& Pair : Rewrites)
        if (Pair.first != Pair.second && std::find(Inventory.Names.begin(),Inventory.Names.end(),Pair.first) != Inventory.Names.end() &&
            ResultCounts[Pair.second] != 1) return Fail("rewrite-name-collision");
    RewrittenPackage Result; Result.Header = Header; Result.Body = Body; Result.Changed = Changed;
    if (Changed)
    {
        if (NewNames.size() > 64*1024*1024-Header.size()) return Fail("rewrite-header-growth-bound");
        // A table that ends the header (one a translation wrote again there) is replaced where it is, so the header
        // keeps a single table besides the save's own.
        bool Last = Names.Tell() == Header.size();
        for (const auto At : {FlagsAt+16,FlagsAt+24,FlagsAt+32,FlagsAt+36,FlagsAt+44,FlagsAt+48,FlagsAt+52,RegistryAt,
            WorldTileAt,PreloadAt+4})
        {
            std::uint64_t Offset = 0;
            Scalar(Header,At,Offset);
            Last = Last && Offset < NamesAt;
        }
        if (Last) Result.Header.resize(std::size_t(NamesAt));
        const auto NameOffset = Result.Header.size();
        Result.Header.insert(Result.Header.end(),NewNames.begin(),NewNames.end());
        const auto NewSize = Result.Header.size();
        Put(Result.Header,HeaderSizeAt,NewSize); Put(Result.Header,FlagsAt+8,NameOffset);
        for (std::size_t Index = 0; Index < Inventory.Exports.size(); ++Index)
            Put(Result.Header,std::size_t(ExportsAt)+Index*104+36,NewSize+Inventory.Exports[Index].Offset,8);
        Put(Result.Header,BulkAt,NewSize+End,8);
        const auto Check = ReadPackageHeader(Result.Header,Result.Body.size());
        if (!Check.Error.Code.empty() || Check.Exports.size() != Inventory.Exports.size()) return Fail("rewrite-readback-rejected");
    }
    Out = std::move(Result); Error.clear(); return true;
}

// The collision of a cooked mesh named as the game names it. A versioned summary is read as an
// unversioned one (the reader knows that layout) and its version words are kept as they were.
inline bool RelabelPhysXCook(const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body,
    RewrittenPackage& Out, std::string& Error)
{
    if (Header.size() < 20) { Error = "rewrite-header-truncated"; return false; }
    std::vector<std::uint8_t> Plain = Header;
    std::fill(Plain.begin() + 8, Plain.begin() + 16, std::uint8_t(0));
    RewrittenPackage Result;
    if (!RewriteCookedNames(Plain,Body,{{"PhysXPC","PhysXGeneric"}},Result,Error)) return false;
    std::copy(Header.begin() + 8, Header.begin() + 16, Result.Header.begin() + 8);
    Out = std::move(Result); return true;
}
} }

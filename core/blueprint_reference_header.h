// Exact supported legacy header-table census shared by role reads and
// original-Zen reservation verification. Gaps are reported, NEVER admitted here.
#pragma once
#include "blueprint_class_source.h"
namespace P4MapKit { namespace Blueprint { namespace ReferenceRoleDetail {
struct FHeaderCoverage
{
    std::vector<FInventorySpan> Spans;
    std::size_t PreloadEnd=0,SoftCount=0;
    std::uint64_t SoftOffset=0;
    std::uint64_t Bulk=0;
};
class HeaderReader
{
    using Bytes=std::vector<std::uint8_t>;
    const FBlueprintClassSource& Source;
    const Bytes& Header; const Bytes& Body;
    const FPackageInventory& Package;
    const std::function<void(const std::string&,const std::string&)>& Required;
    FHeaderCoverage Result;
    static void Require(bool Good,const std::string& Why) { if(!Good) throw Error("reference-role-"+Why); }
    void Index(std::int32_t I,const std::string& Site)
    { if(I) { std::string Path; Require(Package.Reference(I,Path),"invalid-reference"); Required(Path,Site); } }
    std::uint64_t UInt(FByteReader& R,std::size_t Width)
    { std::uint64_t V=0; Require(R.Unsigned(Width,V),"truncated-summary"); return V; }
    std::int32_t Int(FByteReader& R)
    { std::int32_t V=0; Require(R.I32(V),"truncated-summary"); return V; }
    std::size_t Count(FByteReader& R,std::size_t Limit=1000000)
    { std::size_t V=0; Require(R.Count(V,Limit),"invalid-summary-count"); return V; }
    std::string String(FByteReader& R)
    { std::string V; Require(R.String(V),"invalid-summary-string"); return V; }
public:
    HeaderReader(const FBlueprintClassSource& S,const Bytes& H,const Bytes& B,
        const std::function<void(const std::string&,const std::string&)>& Sink)
        :Source(S),Header(H),Body(B),Package(S.Inventory()),Required(Sink) {}
    FHeaderCoverage Read()
    {
        // Exact unversioned UE4.26 summary, not an "opaque summary is harmless"
        // assumption. Unknown auxiliary tables remain explicitly unsupported.
        FByteReader R(Header,20); const auto Versions=Count(R,1000);
        Require(R.Skip(Versions*20),"custom-versions"); Count(R,Header.size()); String(R); UInt(R,4);
        Count(R); UInt(R,4); const auto TextCount=Count(R),TextAt=UInt(R,4);
        Count(R); UInt(R,4); Count(R); UInt(R,4); const auto Depends=UInt(R,4);
        Result.SoftCount=Count(R); Result.SoftOffset=UInt(R,4); const auto Searchable=UInt(R,4),Thumbnails=UInt(R,4);
        Require(R.Skip(16),"guid"); const auto Generations=Count(R,100);
        Require(R.Skip(Generations*8),"generations");
        for(int N=0;N<2;++N) { Require(R.Skip(10),"engine-version"); String(R); }
        const auto Compression=UInt(R,4),Chunks=Count(R,100);
        Require(Compression==0&&Chunks==0,"compressed-header"); UInt(R,4);
        const auto Additional=Count(R,100);
        for(std::size_t N=0;N<Additional;++N) Required(String(R),"summary.additional-package");
        const auto Registry=UInt(R,4),Bulk=UInt(R,8),WorldTile=UInt(R,4),ChunkIds=Count(R,100);
        Require(R.Skip(ChunkIds*4),"chunk-ids");
        const auto PreloadCount=Int(R); const auto PreloadAt=UInt(R,4);
        Require(PreloadCount>=0&&PreloadCount<=1000000,"preload-count");
        Result.Bulk=Bulk; Result.PreloadEnd=PreloadCount>0?std::size_t(PreloadAt)+4*std::size_t(PreloadCount):0;
        Require(TextCount==0&&TextAt==0&&Searchable==0&&Thumbnails==0&&WorldTile==0,
            "unqualified-header-table");
        // SaveBulkData records Tell() before SavePackage appends PACKAGE_FILE_TAG.
        // Only that exact final sentinel can account for this four-byte delta;
        // the body census below still refuses inline bulk/uncovered body bytes.
        const bool Footer=Body.size()>=4&&Body[Body.size()-4]==0xc1&&Body[Body.size()-3]==0x83&&
            Body[Body.size()-2]==0x2a&&Body[Body.size()-1]==0x9e;
        Require(Bulk==Header.size()+Body.size() || Bulk==0 ||
            (Footer&&Bulk==Header.size()+Body.size()-4),"unqualified-bulk-boundary");
        std::vector<FInventorySpan> Spans;
        Spans.push_back({0,R.Tell(),"summary",true});
        for(const auto& Span:Package.HeaderSpans)
            if(Span.Decoded) Spans.push_back(Span);
        if(Registry)
        {
            // WritePackageData freezes the cooked pre-dependency format and
            // writes zero asset records when PKG_FilterEditorOnly is present.
            // Nonempty/editor dependency formats are not inferred from zeros.
            const auto& Class=Source.ClassStructure()["class"];
            Require((Package.Flags&0x80000000u)!=0&&Class.Kind==J::K::Object&&Class["cooked"].Truth(),
                "registry-format-not-qualified");
            Require(Registry<=Header.size()&&Header.size()-std::size_t(Registry)>=4,"registry-bounds");
            FByteReader RegistryReader(Header,std::size_t(Registry),std::size_t(Registry)+4);
            Require(Int(RegistryReader)==0,"nonempty-registry-not-qualified");
            Spans.push_back({std::size_t(Registry),4,"empty-cooked-registry",true});
        }
        if(Depends)
        {
            FByteReader D(Header,std::size_t(Depends));
            for(std::size_t E=0;E<Package.Exports.size();++E)
            {
                const auto Number=Count(D);
                for(std::size_t N=0;N<Number;++N) Index(Int(D),"header.depends");
            }
            Spans.push_back({std::size_t(Depends),D.Tell()-std::size_t(Depends),"depends",true});
        }
        if(PreloadCount)
        {
            Require(PreloadAt!=0,"missing-preload-table"); FByteReader D(Header,std::size_t(PreloadAt));
            for(std::int32_t N=0;N<PreloadCount;++N) Index(Int(D),"header.preload");
            Spans.push_back({std::size_t(PreloadAt),D.Tell()-std::size_t(PreloadAt),"preload",true});
        }
        else Require(PreloadAt==0,"unexpected-preload-offset");
        for(const auto& E:Package.Exports)
        {
            Require(E.FirstDependency>=-1,"invalid-export-dependency");
            std::size_t Total=0; for(const auto C:E.DependencyCounts) Total+=C;
            Require(Total==0 || (E.FirstDependency>=0&&std::size_t(E.FirstDependency)<=std::size_t(PreloadCount)&&
                Total<=std::size_t(PreloadCount)-std::size_t(E.FirstDependency)),"export-dependency-range");
        }
        Require(PartitionSpans(Header.size(),Spans),"overlapping-header-tables");
        Result.Spans=std::move(Spans); return std::move(Result);
    }
};
inline FHeaderCoverage ReadReferenceHeader(const FBlueprintClassSource& Source,const std::vector<std::uint8_t>& Header,
    const std::vector<std::uint8_t>& Body,const std::function<void(const std::string&,const std::string&)>& Required)
{
    if(!Source.MatchesSource(Header,Body)) throw Error("reference-role-class-source-changed");
    return HeaderReader(Source,Header,Body,Required).Read();
}
}}} // namespace P4MapKit::Blueprint::ReferenceRoleDetail

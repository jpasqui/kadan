// Actual-byte, original-Zen-bound converted legacy header reservation.
// This witness proves source correspondence and a precise zero reservation;
// callers still own the selected-container and current-file read lifecycle.
#pragma once
#include "blueprint_reference_header.h"
namespace P4MapKit { namespace Blueprint {
class FVerifiedConvertedHeaderReservation;
FVerifiedConvertedHeaderReservation VerifyConvertedHeaderReservation(const FBlueprintClassSource&,
    const FImportIdentityBytes&,const FImportIdentityBytes&,const FImportIdentityBytes&,const FImportIdentityBytes&);
class FVerifiedConvertedHeaderReservation
{
    std::string PackageValue,HeaderDigest,BodyDigest,OriginalDigest,MetadataDigest;
    std::size_t BeginValue=0,EndValue=0,BodyBytes=0,OriginalBytes=0,MetadataBytes=0;
    static std::string Hash(const FImportIdentityBytes& Bytes)
    { return CustomMaterial::Hex(CustomMaterial::Sha256(Bytes)); }
    FVerifiedConvertedHeaderReservation()=default;
    friend FVerifiedConvertedHeaderReservation VerifyConvertedHeaderReservation(const FBlueprintClassSource&,
        const FImportIdentityBytes&,const FImportIdentityBytes&,const FImportIdentityBytes&,const FImportIdentityBytes&);
public:
    const std::string& Package()const{return PackageValue;}
    const std::string& HeaderSha256()const{return HeaderDigest;}
    const std::string& BodySha256()const{return BodyDigest;}
    const std::string& OriginalZenSha256()const{return OriginalDigest;}
    const std::string& OriginalMetadataSha256()const{return MetadataDigest;}
    std::size_t Begin()const{return BeginValue;}
    std::size_t End()const{return EndValue;}
    bool MatchesSource(const FImportIdentityBytes& Header,const FImportIdentityBytes& Body,
        const FImportIdentityBytes& Original,const FImportIdentityBytes& Metadata)const
    {return Header.size()==EndValue&&Body.size()==BodyBytes&&Original.size()==OriginalBytes&&Metadata.size()==MetadataBytes&&
        HeaderDigest==Hash(Header)&&BodyDigest==Hash(Body)&&OriginalDigest==Hash(Original)&&MetadataDigest==Hash(Metadata);}
};
inline FVerifiedConvertedHeaderReservation VerifyConvertedHeaderReservation(const FBlueprintClassSource& Source,
    const FImportIdentityBytes& Header,const FImportIdentityBytes& Body,const FImportIdentityBytes& Original,const FImportIdentityBytes& Metadata)
{
    const auto Need=[](bool Good,const char* Code){if(!Good)throw Error(std::string("converted-header-")+Code);};
    Need(Source.MatchesSource(Header,Body),"class-source-changed");
    Need(Source.RawPackage()==Source.Inventory().Package&&Source.RawPackage().compare(0,6,"/Game/")==0,"package-identity");
    Need(Source.ImportIdentityOriginalZenSha256().empty()||
        (Source.ImportIdentityOriginalZenSha256()==FVerifiedConvertedHeaderReservation::Hash(Original)&&
         Source.ImportIdentityOriginalMetadataSha256()==FVerifiedConvertedHeaderReservation::Hash(Metadata)),"recovered-origin-differs");
    // Reuse the same complete native original export/payload validation as
    // import identity recovery, independently of unknown import rows.
    const auto Bound=ImportIdentityDetail::ReadLegacyZenCorrespondence(Header,Body,Original,Metadata,Source.RawPackage());
    Need(Bound.CookedHeaderSize==Header.size()&&Bound.PackageFlags==Source.Inventory().Flags,"original-header-identity");
    const auto& Class=Source.ClassStructure()["class"];
    Need((Bound.PackageFlags&0x80000000u)!=0&&Class.Kind==J::K::Object&&Class["cooked"].Truth(),"cooked-format");
    const auto Parsed=ReferenceRoleDetail::ReadReferenceHeader(Source,Header,Body,[](const std::string&,const std::string&){});
    Need(Parsed.SoftCount==0&&Parsed.SoftOffset==0,"unsupported-soft-table");
    Need(Parsed.PreloadEnd>0&&Parsed.PreloadEnd<Header.size(),"missing-final-reservation");
    std::size_t Gaps=0;
    for(const auto& Span:Parsed.Spans)if(!Span.Decoded)
    {
        ++Gaps;
        Need(Span.Offset==Parsed.PreloadEnd&&Span.Size==Header.size()-Parsed.PreloadEnd,"unqualified-gap");
        Need(std::all_of(Header.begin()+Span.Offset,Header.end(),[](std::uint8_t Byte){return Byte==0;}),"nonzero-reservation");
    }
    Need(Gaps==1,"missing-final-reservation");
    // There can be no undisclosed header/native/bulk bytes: every export is
    // matched to the original, and the only body suffix is the exact footer.
    const auto& Package=Source.Inventory();std::size_t ExportEnd=0;bool Footer=false;
    for(const auto& Export:Package.Exports)ExportEnd=std::max(ExportEnd,Export.Offset+Export.Size);
    for(const auto& Span:Package.BodySpans)if(Span.Kind!="export-payload")
    {
        Need(!Footer&&Span.Size==4&&Span.Offset==ExportEnd&&Span.Offset+4==Body.size()&&
            Body[Span.Offset]==0xc1&&Body[Span.Offset+1]==0x83&&Body[Span.Offset+2]==0x2a&&Body[Span.Offset+3]==0x9e,"body-footer-or-gap");
        Footer=true;
    }
    Need(Footer&&Parsed.Bulk==Header.size()+ExportEnd,"bulk-boundary");
    FVerifiedConvertedHeaderReservation Out;
    Out.PackageValue=Source.RawPackage();Out.BeginValue=Parsed.PreloadEnd;Out.EndValue=Header.size();
    Out.BodyBytes=Body.size();Out.OriginalBytes=Original.size();Out.MetadataBytes=Metadata.size();
    Out.HeaderDigest=FVerifiedConvertedHeaderReservation::Hash(Header);Out.BodyDigest=FVerifiedConvertedHeaderReservation::Hash(Body);
    Out.OriginalDigest=FVerifiedConvertedHeaderReservation::Hash(Original);Out.MetadataDigest=FVerifiedConvertedHeaderReservation::Hash(Metadata);
    return Out;
}
}} // namespace P4MapKit::Blueprint

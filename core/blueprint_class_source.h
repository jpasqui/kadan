// Immutable native class/script read from actual source bytes. No constructor
// accepts JSON, an inventory supplied by callers, or a coverage assertion.
#pragma once
#include "blueprint_class.h"
#include "blueprint_import_identities.h"

namespace P4MapKit { namespace Blueprint {
class FBlueprintClassSource
{
    FPackageInventory InventoryValue;
    J StructureValue;
    std::string RawPackageValue,HeaderDigest,BodyDigest,IdentityOriginalDigest,IdentityMetadataDigest;
    std::size_t HeaderSize=0,BodySize=0;
    static std::string Hash(const std::vector<std::uint8_t>& B)
    { return CustomMaterial::Hex(CustomMaterial::Sha256(B)); }
    FBlueprintClassSource(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,
        const std::string& LogicalPackage,const FVerifiedImportIdentities* Identities)
    {
        InventoryValue=ReadPackage(Header,Body);
        if(!InventoryValue.Error.Code.empty()) throw Error(InventoryValue.Error.Code);
        RawPackageValue=InventoryValue.Package;
        if(!LogicalPackage.empty()) InventoryValue.Package=LogicalPackage;
        if(Identities)
        {
            if(Identities->Package()!=InventoryValue.Package) throw Error("import-identities-source-package");
            if(!Identities->MatchesSource(Header,Body)) throw Error("import-identities-source-bytes");
            IdentityOriginalDigest=Identities->OriginalZenSha256();IdentityMetadataDigest=Identities->OriginalMetadataSha256();
            InventoryValue.RecoveredImportReferences=Identities->Imports();
        }
        HeaderSize=Header.size(); BodySize=Body.size(); HeaderDigest=Hash(Header); BodyDigest=Hash(Body);
        StructureValue=ReadClassStructure(InventoryValue,Body);
    }
public:
    static FBlueprintClassSource Read(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,
        const std::string& LogicalPackage="")
    { return FBlueprintClassSource(Header,Body,LogicalPackage,nullptr); }
    static FBlueprintClassSource ReadWithIdentities(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body,
        const std::string& LogicalPackage,const FVerifiedImportIdentities& Identities)
    { return FBlueprintClassSource(Header,Body,LogicalPackage,&Identities); }
    const FPackageInventory& Inventory() const { return InventoryValue; }
    const J& ClassStructure() const { return StructureValue; }
    const std::string& RawPackage() const { return RawPackageValue; }
    const std::string& HeaderSha256() const { return HeaderDigest; }
    const std::string& BodySha256() const { return BodyDigest; }
    const std::string& ImportIdentityOriginalZenSha256()const{return IdentityOriginalDigest;}
    const std::string& ImportIdentityOriginalMetadataSha256()const{return IdentityMetadataDigest;}
    bool MatchesBody(const std::vector<std::uint8_t>& Body) const
    { return Body.size()==BodySize&&Hash(Body)==BodyDigest; }
    bool MatchesSource(const std::vector<std::uint8_t>& Header,const std::vector<std::uint8_t>& Body) const
    { return Header.size()==HeaderSize&&Hash(Header)==HeaderDigest&&MatchesBody(Body); }
};
}} // namespace P4MapKit::Blueprint

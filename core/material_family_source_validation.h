// Source-only family witness. Completeness comes from the independently leased
// current registry, not from these supplied bytes or a historical recipe.
#pragma once
#include "material_family_registry.h"
#include "material_cooked.h"
#include "material_resource_boundary.h"
#include "custom_material_hash.h"
#include <functional>
#include <memory>
#include "material_source_common.h"

namespace P4MapKit
{
namespace Material
{
struct FMaterialFamilyRawSource
{
    std::shared_ptr<const std::vector<std::uint8_t>> Header, Body;
};
using FMaterialFamilyRawResolver = std::function<bool(const std::string&, FMaterialFamilyRawSource&)>;
struct FMaterialFamilySourceMember
{
    FMaterialParentRow Row;
    std::string HeaderSha256, BodySha256, SourceSha256, Owner, ResourceHash;
    bool OwnsShaders = false;
    std::size_t ExportIndex = 0, ShaderReferences = 0;
};
struct FMaterialFamilySourceWitness
{
    std::string Master, SourceSetSha256;
    std::map<std::string, FMaterialFamilySourceMember> Members;
    std::vector<std::string> Owners;
    std::size_t SourceBytes = 0;
};
namespace MaterialFamilySourceDetail
{
inline bool SameRow(const FMaterialParentRow& A, const FMaterialParentRow& B)
{
    return A.Package == B.Package && A.Object == B.Object && A.Class == B.Class && A.ParentPackage == B.ParentPackage &&
           A.ParentObject == B.ParentObject && A.ParentClass == B.ParentClass;
}
inline bool RowIdentity(const FMaterialParentRow& Row)
{
    std::string Package;
    if (Row.Package.compare(0, 6, "/Game/") || !MaterialRegistryDetail::Object(Row.Object, Package) ||
        Package != Row.Package)
        return false;
    if (Row.Class == "Material")
        return Row.ParentPackage.empty() && Row.ParentObject.empty() && Row.ParentClass.empty();
    return Row.Class == "MaterialInstanceConstant" && MaterialRegistryDetail::Object(Row.ParentObject, Package) &&
           Package == Row.ParentPackage &&
           (Row.ParentClass == "Material" || Row.ParentClass == "MaterialInstanceConstant");
}
inline bool SourceMember(const FMaterialParentRow& Row, const FPackageInventory& Package,
                         const std::vector<std::uint8_t>& Body, FMaterialFamilySourceMember& Out, FReadError& Error)
{
    const auto Fail = [&](const char* Why)
    {
        Error = {0, std::string(Why) + ":" + Row.Package};
        return false;
    };
    if (!RowIdentity(Row) || !Package.Error.Code.empty() || Package.Package != Row.Package)
        return Fail("material-family-source-identity");
    std::size_t Matches = 0, Selected = 0;
    for (std::size_t I = 0; I < Package.Exports.size(); ++I)
    {
        const auto& Export = Package.Exports[I];
        std::string Class;
        if (!Package.Reference(Export.Class, Class))
            return Fail("material-family-source-class-reference");
        const auto Short = Class.substr(Class.find_last_of('.') == std::string::npos ? 0 : Class.find_last_of('.') + 1);
        if (Short != "Material" && Short != "MaterialInstanceConstant")
            continue;
        std::string Object;
        if (++Matches != 1 || Export.Outer != 0 || Class != "/Script/Engine." + Row.Class ||
            !Package.Reference(std::int32_t(I + 1), Object) || Package.Package + "." + Object != Row.Object)
            return Fail("material-family-source-export");
        Selected = I;
    }
    if (Matches != 1)
        return Fail("material-family-source-export-missing");
    const auto& Export = Package.Exports[Selected];
    if (Export.Offset > Body.size() || Export.Size > Body.size() - Export.Offset)
        return Fail("material-family-source-range");
    FPropertyDecoder Decoder(Package, Body);
    FDecodedValue Properties;
    auto At = Export.Offset;
    if (!Decoder.TypedProperties(At, Export.Offset + Export.Size, Properties, 0, Row.Class))
    {
        Error = Decoder.Error;
        return false;
    }
    if (Properties.Kind != FDecodedValue::EKind::Object)
        return Fail("material-family-source-properties");
    std::set<std::string> Fields;
    for (const auto& Field : Properties.Fields)
        if (!Fields.insert(PropertyIdentity(Field.first)).second)
            return Fail("material-family-source-duplicate-property");
    const auto* Parent = Properties.Field("Parent");
    if (Row.Class == "Material")
    {
        if (Parent)
            return Fail("material-family-source-master-parent");
    }
    else
    {
        std::string Reference;
        if (!Parent || Parent->Kind != FDecodedValue::EKind::HardReference || Parent->ReferenceIndex >= 0 ||
            Parent->Text != Row.ParentObject || !Package.Reference(Parent->ReferenceIndex, Reference) ||
            Reference != Row.ParentObject)
            return Fail("material-family-source-parent");
        const auto Index = std::size_t(-std::int64_t(Parent->ReferenceIndex) - 1);
        if (Index >= Package.Imports.size() || Package.Imports[Index].ClassPackage != "/Script/Engine" ||
            Package.Imports[Index].Class != Row.ParentClass)
            return Fail("material-family-source-parent-class");
    }
    FMaterialFamilySourceMember Result;
    Result.Row = Row;
    Result.ExportIndex = Selected;
    Result.OwnsShaders = Row.Class == "Material";
    if (const auto* Flag = Properties.Field("bHasStaticPermutationResource"))
    {
        bool Owns = false;
        if (!MaterialBool(*Flag, Owns))
            return Fail("material-family-source-owner-flag");
        if (Row.Class != "Material")
            Result.OwnsShaders = Owns;
    }
    if (Result.OwnsShaders)
    {
        FMaterialResourceBoundary Boundary;
        if (!DiscoverMaterialResourceBoundary(Package, Body, Selected, Boundary, Error))
            return false;
        FMaterialShaderMapEnvelope Map;
        std::vector<FMaterialShaderReference> References;
        if (!ReadMaterialShaderMapEnvelope(Body, Boundary.ResourceStart, Boundary.ResourceEnd, Map, Error) ||
            !ReadMaterialShaderReferences(Map, References, Error))
            return false;
        if (References.empty())
            return Fail("material-family-source-owner-no-shaders");
        Result.ResourceHash =
            CustomMaterial::Hex(CustomMaterial::Bytes(Map.ResourceHash.begin(), Map.ResourceHash.end()));
        Result.ShaderReferences = References.size();
    }
    Error = {};
    Out = std::move(Result);
    return true;
}
inline void Fact(CustomMaterial::Bytes& Bytes, const std::string& Text)
{
    const auto Size = std::uint64_t(Text.size());
    for (unsigned I = 0; I < 8; ++I)
        Bytes.push_back(std::uint8_t(Size >> (I * 8)));
    Bytes.insert(Bytes.end(), Text.begin(), Text.end());
}
}

// Validate ONE newly converted member against its exact current registry row.
// This does not establish ancestry/complete family coverage. The byte total is
// charged only on success and cannot wrap; a failed member publishes nothing.
inline bool ValidateMaterialFamilySourceMember(const FMaterialParentRow& Row, const FMaterialFamilyRawSource& Raw,
                                               std::size_t& SourceBytes, FMaterialFamilySourceMember& Out,
                                               FReadError& Error)
{
    Out = {};
    Error = {};
    const auto Fail = [&](const char* Why)
    {
        Error = {0, Why};
        return false;
    };
    if (!Raw.Header || !Raw.Body || Raw.Header->empty() || Raw.Body->empty())
        return Fail("material-family-source-missing");
    constexpr std::size_t OneLimit = 64u * 1024u * 1024u, TotalLimit = 128u * 1024u * 1024u;
    if (SourceBytes > TotalLimit || Raw.Header->size() > OneLimit || Raw.Body->size() > OneLimit ||
        Raw.Header->size() > TotalLimit - SourceBytes ||
        Raw.Body->size() > TotalLimit - SourceBytes - Raw.Header->size())
        return Fail("material-family-source-byte-bound");
    FMaterialFamilySourceMember Member;
    if (!MaterialFamilySourceDetail::SourceMember(Row, ReadPackage(*Raw.Header, *Raw.Body), *Raw.Body, Member, Error))
        return false;
    Member.HeaderSha256 = PayloadDigest(*Raw.Header);
    Member.BodySha256 = PayloadDigest(*Raw.Body);
    CustomMaterial::Bytes Joined;
    Joined.reserve(Raw.Header->size() + Raw.Body->size());
    Joined.insert(Joined.end(), Raw.Header->begin(), Raw.Header->end());
    Joined.insert(Joined.end(), Raw.Body->begin(), Raw.Body->end());
    Member.SourceSha256 = PayloadDigest(Joined);
    SourceBytes += Raw.Header->size() + Raw.Body->size();
    Out = std::move(Member);
    return true;
}

// No shader archive/recovery, UObject construction, destination import or write.
// Result is published only after every registry member and owner is verified.
// This proves structural owner coverage, not payload-vs-game or graph fidelity.
inline bool ValidateMaterialFamilySources(const FMaterialRegistryFamily& Family,
                                          const FMaterialFamilyRawResolver& Resolve, FMaterialFamilySourceWitness& Out,
                                          FReadError& Error)
{
    Out = {};
    Error = {};
    const auto Fail = [&](const char* Why)
    {
        Error = {0, Why};
        return false;
    };
    if (!Resolve || Family.Members.empty() || Family.Members.size() > 4096 || Family.Master.Class != "Material" ||
        !MaterialFamilySourceDetail::RowIdentity(Family.Master))
        return Fail("material-family-source-family");
    std::set<std::string> Keys, Objects;
    std::size_t Strings = 0;
    FMaterialFamilySourceWitness Result;
    Result.Master = Family.Master.Package;
    for (const auto& Row : Family.Members)
    {
        if (!MaterialFamilySourceDetail::RowIdentity(Row) || !Keys.insert(PropertyIdentity(Row.Package)).second ||
            !Objects.insert(PropertyIdentity(Row.Object)).second)
            return Fail("material-family-source-duplicate-or-invalid-member");
        const auto StringSize = Row.Package.size() + Row.Object.size() + Row.Class.size() + Row.ParentPackage.size() +
                                Row.ParentObject.size() + Row.ParentClass.size();
        if (StringSize > 32u * 1024u * 1024u - Strings)
            return Fail("material-family-source-string-bound");
        Strings += StringSize;
        if (Row.Class == "Material" && !MaterialFamilySourceDetail::SameRow(Row, Family.Master))
            return Fail("material-family-source-multiple-roots");
        FMaterialFamilyRawSource Raw;
        if (!Resolve(Row.Package, Raw))
            return Fail("material-family-source-missing");
        FMaterialFamilySourceMember Member;
        if (!ValidateMaterialFamilySourceMember(Row, Raw, Result.SourceBytes, Member, Error))
            return false;
        Result.Members.emplace(Row.Package, std::move(Member));
    }
    const auto Root = Result.Members.find(Family.Master.Package);
    if (Root == Result.Members.end() || !MaterialFamilySourceDetail::SameRow(Root->second.Row, Family.Master))
        return Fail("material-family-source-root-missing");
    for (auto& Pair : Result.Members)
    {
        auto* Member = &Pair.second;
        std::set<std::string> Seen;
        for (;;)
        {
            if (Seen.size() >= 256 || !Seen.insert(Member->Row.Package).second)
                return Fail("material-family-source-parent-cycle-or-depth");
            // Validate the ENTIRE chain even if this member owns a shader map.
            if (Pair.second.Owner.empty() && Member->OwnsShaders)
                Pair.second.Owner = Member->Row.Package;
            if (Member->Row.Class == "Material")
                break;
            const auto Parent = Result.Members.find(Member->Row.ParentPackage);
            if (Parent == Result.Members.end() || Parent->second.Row.Object != Member->Row.ParentObject ||
                Parent->second.Row.Class != Member->Row.ParentClass)
                return Fail("material-family-source-parent-outside-closure");
            Member = &Parent->second;
        }
        if (Member->Row.Package != Result.Master || Pair.second.Owner.empty())
            return Fail("material-family-source-owner-outside-root");
        if (Pair.second.OwnsShaders)
            Result.Owners.push_back(Pair.first);
    }
    CustomMaterial::Bytes Facts;
    MaterialFamilySourceDetail::Fact(Facts, "p4-material-family-source-witness/1");
    for (const auto& Pair : Result.Members)
    {
        const auto& M = Pair.second;
        for (const auto* Text :
             {&M.Row.Package, &M.Row.Object, &M.Row.Class, &M.Row.ParentPackage, &M.Row.ParentObject,
              &M.Row.ParentClass, &M.HeaderSha256, &M.BodySha256, &M.SourceSha256, &M.Owner, &M.ResourceHash})
            MaterialFamilySourceDetail::Fact(Facts, *Text);
    }
    Result.SourceSetSha256 = PayloadDigest(Facts);
    Out = std::move(Result);
    return true;
}
}
}

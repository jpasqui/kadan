// Bounded structural discovery inside a typed material export. This does not
// assert that the unknown class-native tail preceding the envelope is decoded.
#pragma once
#include "material_shader_map.h"
#include "property_decoder.h"
namespace P4MapKit
{
struct FMaterialResourceBoundary
{
    std::string Package, Class, Provenance = "unique-structural-envelope-scan";
    std::size_t ExportIndex = 0, ExportStart = 0, ExportEnd = 0, PropertyEnd = 0;
    std::size_t ResourceStart = 0, ResourceEnd = 0, CandidatesExamined = 0, ValidatedCandidates = 0;
};
struct FMaterialBoundaryLimits
{
    std::size_t MaximumScanBytes = 64u * 1024u * 1024u, MaximumCandidates = 4096;
};
inline bool DiscoverMaterialResourceBoundary(const FPackageInventory& Package, const std::vector<std::uint8_t>& Body,
                                             std::size_t ExportIndex, FMaterialResourceBoundary& Out, FReadError& Error,
                                             const FMaterialBoundaryLimits& Limits = FMaterialBoundaryLimits())
{
    if (!Package.Error.Code.empty())
    {
        Error = Package.Error;
        return false;
    }
    if (ExportIndex >= Package.Exports.size())
    {
        Error = {0, "material-export-index"};
        return false;
    }
    const auto& Export = Package.Exports[ExportIndex];
    FMaterialResourceBoundary Result;
    Result.Package = Package.Package;
    Result.ExportIndex = ExportIndex;
    if (!Package.Reference(Export.Class, Result.Class))
    {
        Error = {Export.Offset, "material-class-reference"};
        return false;
    }
    Result.Class = Result.Class.substr(
        Result.Class.find_last_of('.') == std::string::npos ? 0 : Result.Class.find_last_of('.') + 1);
    if (Result.Class != "Material" && Result.Class != "MaterialInstanceConstant")
    {
        Error = {Export.Offset, "material-resource-owner-class"};
        return false;
    }
    if (Export.Offset > Body.size() || Export.Size > Body.size() - Export.Offset)
    {
        Error = {Export.Offset, "material-export-range"};
        return false;
    }
    Result.ExportStart = Export.Offset;
    Result.ExportEnd = Export.Offset + Export.Size;
    Result.PropertyEnd = Export.Offset;
    FPropertyDecoder Decoder(Package, Body);
    FDecodedValue Properties;
    if (!Decoder.TypedProperties(Result.PropertyEnd, Result.ExportEnd, Properties, 0, Result.Class))
    {
        Error = Decoder.Error;
        return false;
    }
    if (Result.ExportEnd - Result.PropertyEnd > Limits.MaximumScanBytes)
    {
        Error = {Result.PropertyEnd, "material-boundary-scan-budget"};
        return false;
    }
    for (std::size_t At = Result.PropertyEnd; At < Result.ExportEnd && Result.ExportEnd - At >= 20; ++At)
    {
        // NumResources is exactly one in this qualified archive schema. This is
        // a structural prefilter, never a hash or package-name search.
        if (Body[At] != 1 || Body[At + 1] || Body[At + 2] || Body[At + 3])
            continue;
        if (++Result.CandidatesExamined > Limits.MaximumCandidates)
        {
            Error = {At, "material-boundary-candidate-budget"};
            return false;
        }
        FMaterialShaderMapEnvelope Map;
        FReadError CandidateError;
        if (!ReadMaterialShaderMapEnvelope(Body, At, Result.ExportEnd, Map, CandidateError))
            continue;
        std::vector<FMaterialShaderReference> Refs;
        if (!ReadMaterialShaderReferences(Map, Refs, CandidateError))
            continue;
        FByteReader Prefix(Body, At, Result.ExportEnd);
        std::size_t Resources = 0, Names = 0, Locations = 0, Size = 0;
        Prefix.Count(Resources);
        Prefix.Count(Names);
        for (std::size_t I = 0; I < Names; ++I)
        {
            std::string Name;
            std::uint32_t Hash = 0;
            Prefix.String(Name);
            Prefix.U32(Hash);
        }
        Prefix.Count(Locations);
        Prefix.Skip(Locations * 6);
        Prefix.Count(Size, std::numeric_limits<std::int32_t>::max());
        if (!Prefix.Error.Code.empty())
        {
            Error = Prefix.Error;
            return false;
        }
        Result.ResourceStart = At;
        Result.ResourceEnd = Prefix.Tell() + Size;
        if (++Result.ValidatedCandidates > 1)
        {
            Error = {At, "material-boundary-ambiguous"};
            return false;
        }
    }
    if (Result.ValidatedCandidates != 1)
    {
        Error = {Result.PropertyEnd, "material-boundary-not-found"};
        return false;
    }
    Error = {};
    Out = std::move(Result);
    return true;
}
}

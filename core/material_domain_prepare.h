// Domain source preparation retains hashed UB identities. The generic +240
// FMeshMaterialShader Pass tail is not a proven global-subclass field.
#pragma once
#include "material_domain_fields.h"
namespace P4MapKit
{
namespace Material
{
enum class MaterialDomainSourceScope
{
    QualifiedGlobalTail,
    ReadOnlyExpressions
};
struct MaterialDomainPreparedProgram
{
    MaterialPreparedProgram Program;
    std::map<unsigned, std::string> UnqualifiedMeshTailAliases;
    MaterialDomainFieldContract LegacyFields;
    MaterialPostBindings PostBindings;
    bool GlobalSubclassTailProved = false, ProductionAdmitted = false;
};
inline MaterialDomainPreparedProgram PrepareOriginalMaterialDomainPixelProgram(
    const FMaterialShaderMapEnvelope& map, const FMaterialShaderReference& original,
    const CustomMaterial::Bytes& payload, std::uint8_t archiveFrequency, const Json& settings,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail)
{
    RequireKind(settings, Json::Object, "material domain source settings");
    const auto domain = settings.At("domain").Text();
    if (!IsMember(domain, {"MD_Surface", "MD_UI", "MD_PostProcess", "MD_LightFunction", "MD_DeferredDecal"}))
        throw Refused("unrecovered original material domain: " + domain, "material domain source contract");
    MaterialDomainPreparedProgram result;
    auto checked = original;
    if (original.VertexFactoryHash == 0)
    {
        if (!IsMember(domain, {"MD_PostProcess", "MD_LightFunction", "MD_DeferredDecal"}))
            throw Refused("mesh material domain has no authoritative vertex factory", "material domain source factory");
        CustomMaterial::ShaderBindings header;
        std::string failure;
        try
        {
            header = CustomMaterial::ReadPixelBindings(payload);
        }
        catch (const std::exception& error)
        {
            failure = error.what();
        }
        if (!failure.empty())
            throw Refused(failure, "material program payload");
        MaterialUniqueGlobalShaderRecord(map, original);
        if (domain == "MD_PostProcess" && original.TypeHash == 0x89588aefe9328bc4ull)
        {
            FReadError error;
            if (!ReadMaterialPostBindings(map, original, MaterialNativePostHeader(header), result.PostBindings, error))
                throw Refused(error.Code, "material postprocess binding");
            result.GlobalSubclassTailProved = true;
        }
        else if (original.TypeHash == 0x58ed11e84076a289ull || original.TypeHash == 0xcbf72596a6e713c2ull)
        {
            result.LegacyFields = ReadMaterialLegacyGlobalFields(map, original, header);
            result.GlobalSubclassTailProved = result.LegacyFields.GlobalSubclassTailProved;
        }
        else if (scope != MaterialDomainSourceScope::ReadOnlyExpressions)
            throw Refused("global shader subclass lacks an exact qualified tail contract",
                          "material global subclass tail");
        // Exclude only the unproved generic mesh Pass extension. Material and
        // every other name still undergo the unchanged typed/hash conflict guard.
        for (auto it = checked.NamedBuffers.begin(); it != checked.NamedBuffers.end();)
        {
            if (it->second == "Pass")
            {
                result.UnqualifiedMeshTailAliases[it->first] = it->second;
                it = checked.NamedBuffers.erase(it);
            }
            else
                ++it;
        }
    }
    result.Program = PrepareOriginalMaterialPixelProgram(map, checked, payload, archiveFrequency);
    result.Program.Reference = original;
    return result;
}
}
} // P4MapKit::Material

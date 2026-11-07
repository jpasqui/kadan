// Exact legacy global FMaterialShader subclass fields, from native frozen
// records and original payload packed-buffer metadata. No engine-policy proof.
#pragma once
#include "material_prepare.h"
#include "material_post_bindings.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialDomainField
{
    int Buffer = -1;
    unsigned Offset = 0, Size = 0, Kind = 0;
};
struct MaterialDomainFieldContract
{
    std::string ClassName;
    std::map<std::string, MaterialDomainField> Fields;
    bool GlobalSubclassTailProved = false;
};
inline std::size_t MaterialUniqueGlobalShaderRecord(const FMaterialShaderMapEnvelope& map,
                                                    const FMaterialShaderReference& ref)
{
    if (ref.VertexFactoryHash || ref.Frequency != 3)
        throw Refused("domain shader is not a global pixel reference", "material global shader reference");
    FMaterialFrozenReader reader(map.Frozen);
    std::size_t types = 0, perms = 0, shaders = 0, n = 0, np = 0, ns = 0;
    if (!reader.Array(24, 8, types, n) || !reader.Array(40, 4, perms, np) || !reader.Array(56, 8, shaders, ns))
        throw Refused(reader.Error.Code, "material global shader record");
    if (n != np || n != ns || n > 1000000)
        throw Refused("global shader dimensions differ or exceed their bound", "material global shader record");
    std::size_t matches = 0, base = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        std::uint64_t type = 0;
        std::int32_t permutation = 0, resource = 0;
        std::size_t at = 0;
        bool present = false;
        if (!reader.U(types + 8 * i, 8, type) || !reader.I(perms + 4 * i, permutation) ||
            !reader.Pointer(shaders + 8 * i, at, present) || !present || !reader.I(at + 196, resource))
            throw Refused(reader.Error.Code.empty() ? "null global shader" : reader.Error.Code,
                          "material global shader record");
        if (type == ref.TypeHash && permutation == ref.Permutation && resource == ref.ResourceIndex)
        {
            ++matches;
            base = at;
        }
    }
    if (matches != 1 || base != ref.FrozenOffset)
        throw Refused("domain reference has no unique matching global record", "material global shader record");
    return base;
}
inline MaterialDomainFieldContract ReadMaterialLegacyGlobalFields(const FMaterialShaderMapEnvelope& map,
                                                                  const FMaterialShaderReference& ref,
                                                                  const CustomMaterial::ShaderBindings& header)
{
    MaterialDomainFieldContract result;
    std::vector<std::string> names;
    std::map<std::string, unsigned> required, optional;
    std::set<std::string> unsupported;
    if (ref.TypeHash == 0x58ed11e84076a289ull)
    {
        result.ClassName = "FVolumetricFogLightFunctionPS";
        names = {"Parameters", "Parameters2", "WorldToLight", "TexelSize", "ShadowToWorld"};
        required = {{"Parameters", 16}, {"Parameters2", 12}, {"TexelSize", 8}, {"ShadowToWorld", 64}};
        unsupported = {"WorldToLight"};
    }
    else if (ref.TypeHash == 0xcbf72596a6e713c2ull)
    {
        result.ClassName = "FDeferredDecalPS";
        names = {"SvPositionToDecal", "DecalToWorld", "WorldToDecal",
                 "DecalOrientation",  "DecalParams",  "C4DistanceFadeParams"};
        required = {{"SvPositionToDecal", 64}};
        optional = {{"DecalToWorld", 64}, {"DecalParams", 8}, {"C4DistanceFadeParams", 16}};
        unsupported = {"WorldToDecal", "DecalOrientation"};
    }
    else
        throw Refused("global subclass has no source-qualified legacy field layout", "material global subclass tail");
    const auto base = MaterialUniqueGlobalShaderRecord(map, ref);
    FMaterialShaderParameters checked;
    FReadError error;
    if (!ReadMaterialShaderParameters(map, ref, checked, error))
        throw Refused(error.Code, "material global shader record");
    std::map<unsigned, int> native;
    for (const auto& mapping : header.packed_global_slots)
    {
        const auto known = native.find(mapping.first);
        if (mapping.second < 0 || mapping.second > 31 || (known != native.end() && known->second != mapping.second))
            throw Refused("legacy packed buffer mapping is invalid or ambiguous", "material global packed mapping");
        native[mapping.first] = mapping.second;
    }
    const auto packed = native.find(0);
    if (packed == native.end())
        throw Refused("legacy float field has no native packed buffer", "material global packed mapping");
    FMaterialFrozenReader reader(map.Frozen);
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        std::uint64_t kind = 0, offset = 0, size = 0;
        const auto at = base + 230 + 6 * i;
        if (!reader.U(at, 2, kind) || !reader.U(at + 2, 2, offset) || !reader.U(at + 4, 2, size))
            throw Refused(reader.Error.Code, "material global subclass tail");
        if (!size)
            continue;
        if (kind != 104)
            throw Refused("legacy global field has an unknown packed numeric type", "material global subclass field");
        if (unsupported.count(names[i]))
            throw Refused("global field requires an unrecovered orientation/vector contract: " + names[i],
                          "material global subclass field");
        result.Fields[names[i]] = {packed->second, unsigned(offset), unsigned(size), unsigned(kind)};
    }
    for (const auto& field : required)
    {
        const auto found = result.Fields.find(field.first);
        if (found == result.Fields.end() || found->second.Size != field.second)
            throw Refused("legacy global field size differs: " + field.first, "material global subclass field");
    }
    for (const auto& field : optional)
    {
        const auto found = result.Fields.find(field.first);
        if (found != result.Fields.end() && found->second.Size != field.second)
            throw Refused("legacy global field size differs: " + field.first, "material global subclass field");
    }
    result.GlobalSubclassTailProved = true;
    return result;
}
inline MaterialPostHeaderMetadata MaterialNativePostHeader(const CustomMaterial::ShaderBindings& header)
{
    MaterialPostHeaderMetadata result;
    for (const auto& buffer : header.packed_global_slots)
    {
        const auto known = result.NativeBuffers.find(buffer.first);
        if (buffer.second < 0 || buffer.second > 31 ||
            (known != result.NativeBuffers.end() && known->second != buffer.second))
            throw Refused("post packed buffer mapping is invalid or ambiguous", "material post packed mapping");
        result.NativeBuffers[buffer.first] = buffer.second;
    }
    for (const auto& array : header.global_arrays)
        result.PackedGlobalArrays.push_back({array.size, array.type_name, array.type_index});
    result.InputVaryingCount = header.inputs.size();
    return result;
}
}
} // P4MapKit::Material

// Original named texture preshader operands remain runtime graph inputs.
// External affine coordinates belong to the stock external sampler; admit only
// the exact source registry and remove that transform once, never substitute it.
#pragma once
#include "material_fixed_source.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalExternalTexture
{
    std::string Name;
    std::array<std::uint8_t, 16> Guid{{}};
    std::int32_t TextureIndex = -1;
};
struct MaterialOriginalRuntimeTextureContract
{
    FMaterialUniformSet Uniforms;
    std::vector<MaterialOriginalExternalTexture> External;
    std::map<int, std::string> ExternalTextures, ExternalSamplers;
    bool ProductionAdmitted = false;
};
inline std::vector<MaterialOriginalExternalTexture>
ReadOriginalMaterialExternalTextures(const FMaterialShaderMapEnvelope& map)
{
    FMaterialFrozenReader reader(map.Frozen);
    std::size_t start = 0, count = 0;
    if (!reader.Array(256, 32, start, count))
        throw Refused(reader.Error.Code, "external texture source table");
    if (count > 1024 || count != map.Uniforms.ExternalTextureCount)
        throw Refused("external texture source count differs", "external texture source table");
    std::vector<MaterialOriginalExternalTexture> result;
    std::set<std::string> names;
    for (std::size_t n = 0; n < count; ++n)
    {
        const auto at = start + 32 * n;
        const auto name = map.ScriptNames.find(at);
        if (name == map.ScriptNames.end() || IsMember(name->second, {"", "?", "None"}) ||
            !JsonDetail::ValidUtf8(name->second) || !names.insert(name->second).second)
            throw Refused("external texture source name is absent or ambiguous", "external texture source identity");
        MaterialOriginalExternalTexture record;
        record.Name = name->second;
        std::copy_n(map.Frozen.begin() + at + 12, 16, record.Guid.begin());
        if (!reader.I(at + 28, record.TextureIndex) || record.TextureIndex < 0)
            throw Refused("external texture source index is invalid", "external texture source identity");
        result.push_back(record);
    }
    return result;
}
inline std::string OriginalRuntimeTextureName(const FMaterialUniformSet& uniforms,
                                              const FMaterialPreshaderInstruction& instruction)
{
    if (instruction.Parameter >= uniforms.PreshaderNames.size())
        throw BindingRefused("runtime texture parameter is outside its source name table", "runtime texture parameter");
    const auto& name = uniforms.PreshaderNames[instruction.Parameter];
    if (IsMember(name, {"", "?", "None"}) || !JsonDetail::ValidUtf8(name))
        throw BindingRefused("runtime texture parameter has no named identity", "runtime texture parameter");
    return name;
}
inline bool SameOriginalRuntimeTextureInstruction(const FMaterialPreshaderInstruction& a,
                                                  const FMaterialPreshaderInstruction& b)
{
    return a.Opcode == b.Opcode && a.Parameter == b.Parameter && a.LayerIndex == b.LayerIndex &&
           a.Association == b.Association && a.TextureIndex == b.TextureIndex && a.VectorIndex == b.VectorIndex &&
           a.ExternalGuid == b.ExternalGuid;
}
inline void ValidateOriginalRuntimeTextureInstruction(const MaterialOriginalRuntimeTextureContract& contract,
                                                      const FMaterialPreshaderInstruction& instruction)
{
    using O = EMaterialPreshaderOpcode;
    const auto name = OriginalRuntimeTextureName(contract.Uniforms, instruction);
    bool original = false;
    for (const auto* programs : {&contract.Uniforms.VectorPrograms, &contract.Uniforms.ScalarPrograms})
        for (const auto& program : *programs)
            for (const auto& i : program.Instructions)
                if (int(i.Opcode) >= 37 && SameOriginalRuntimeTextureInstruction(i, instruction))
                    original = true;
    if (!original)
        throw BindingRefused("runtime texture operand differs from the original source program",
                             "runtime texture source instruction");
    if (instruction.Opcode == O::TexelSize)
    {
        if (instruction.LayerIndex != -1 || instruction.Association != 2 || instruction.TextureIndex < 0)
            throw BindingRefused("layered or anonymous TexelSize is not bound: " + name,
                                 "runtime texture parameter identity");
        std::size_t matches = 0;
        for (const auto& texture : contract.Uniforms.Textures)
            if (texture.Parameter.Name == name && !texture.Parameter.NameIsNone)
            {
                if (texture.Parameter.LayerIndex != -1 || texture.Parameter.Association != 2 ||
                    texture.TextureIndex != instruction.TextureIndex || texture.TextureType != 0)
                    throw BindingRefused("TexelSize differs from its original Standard2D texture identity",
                                         "runtime texture uniform identity");
                ++matches;
            }
        if (matches != 1)
            throw BindingRefused("TexelSize has no unique original texture uniform",
                                 "runtime texture uniform identity");
    }
    else if (instruction.Opcode == O::ExternalTextureCoordinateScaleRotation ||
             instruction.Opcode == O::ExternalTextureCoordinateOffset)
    {
        if (contract.External.size() != 1)
            throw BindingRefused("several independent external registries are not bound", "external texture registry");
        const auto& record = contract.External.front();
        if (record.Name != name || record.TextureIndex != instruction.TextureIndex ||
            record.Guid != instruction.ExternalGuid)
            throw BindingRefused("external preshader operand differs from its frozen registry",
                                 "external texture registry identity");
    }
    else
        throw BindingRefused("runtime texture preshader opcode is not bound: " +
                                 std::to_string(int(instruction.Opcode)),
                             "runtime texture opcode");
}
inline MaterialOriginalRuntimeTextureContract
PrepareOriginalMaterialRuntimeTextures(const MaterialOriginalUserData& user, const FMaterialShaderMapEnvelope& map,
                                       const MaterialPreparedProgram& program)
{
    MaterialOriginalRuntimeTextureContract result;
    FReadError error;
    if (!ReadMaterialUniformSet(map.Frozen, map.ScriptNames, result.Uniforms, error))
        throw Refused(error.Code, "runtime texture source uniforms");
    result.External = ReadOriginalMaterialExternalTextures(map);
    // Actual source default references and sampler kinds have already been
    // decoded from the master/instance chain, never supplied as runtime values.
    for (const auto* programs : {&result.Uniforms.VectorPrograms, &result.Uniforms.ScalarPrograms})
        for (const auto& expression : *programs)
            for (const auto& instruction : expression.Instructions)
                if (int(instruction.Opcode) >= 37)
                {
                    ValidateOriginalRuntimeTextureInstruction(result, instruction);
                    const auto name = OriginalRuntimeTextureName(result.Uniforms, instruction);
                    const auto& textures = user.Defaults.At("textures");
                    const auto& samplers = user.Defaults.At("sampler_types");
                    if (!textures.Has(name) || textures.At(name).kind != Json::String || !samplers.Has(name) ||
                        samplers.At(name).kind != Json::String)
                        throw Refused("runtime texture has no actual source default and sampler: " + name,
                                      "runtime texture source default");
                    MaterialSourcePackage(textures.At(name).string);
                    if (int(instruction.Opcode) >= 39)
                    {
                        if (expression.Instructions.size() != 1)
                            throw Refused("external transform has additional preshader arithmetic",
                                          "external texture expression shape");
                        if (samplers.At(name).string != "SAMPLERTYPE_External")
                            throw Refused("external registry default is not an external texture",
                                          "external texture source sampler");
                    }
                    else if (samplers.At(name).string == "SAMPLERTYPE_External" ||
                             starts(samplers.At(name).string, "SAMPLERTYPE_Virtual"))
                        throw Refused("TexelSize default is not a supported Standard2D resource",
                                      "runtime texture source sampler");
                }
    if (!result.External.empty())
    {
        int material = -1;
        for (const auto& buffer : program.BaseBinding.Buffers)
            if (buffer.second == "Material")
            {
                if (material != -1)
                    throw Refused("external texture has ambiguous Material buffer", "external texture source binding");
                material = buffer.first;
            }
        if (material < 0)
            throw Refused("external texture has no Material source buffer", "external texture source binding");
        for (const auto pair : {std::make_pair(&program.Header.textures, &result.ExternalTextures),
                                std::make_pair(&program.Header.samplers, &result.ExternalSamplers)})
        {
            const auto table = CustomMaterial::ReadResourceTable(*pair.first);
            const auto rows = table.find(unsigned(material));
            if (rows == table.end())
                continue;
            for (const auto& row : rows->second)
            {
                const auto entry = std::size_t(row.first / 2);
                if (entry < result.Uniforms.Textures.size())
                    continue;
                const auto external = entry - result.Uniforms.Textures.size();
                if (external >= result.External.size())
                    continue;
                const auto slot = int(row.second);
                const auto& name = result.External[external].Name;
                const auto old = pair.second->find(slot);
                if (old != pair.second->end() && old->second != name)
                    throw Refused("external texture binding is ambiguous", "external texture source binding");
                (*pair.second)[slot] = name;
            }
        }
        if (result.ExternalTextures.empty() || result.ExternalSamplers.empty())
            throw Refused("external texture source has no paired texture/sampler resources",
                          "external texture source binding");
    }
    return result;
}
inline Value OriginalExternalTextureSource(SymbolicArena& arena, const std::string& name, bool rotation, int component)
{
    return source(arena, rotation ? "ExternalTextureScaleRotation" : "ExternalTextureOffset", name, component);
}
inline bool OriginalExternalCoordinateTerms(const Value& value, std::vector<Value>& terms)
{
    std::vector<Value> pending{value};
    std::size_t nodes = 0;
    while (!pending.empty())
    {
        if (++nodes > 8)
            return false;
        const auto next = pending.back();
        pending.pop_back();
        if (is(next, "ADD"))
        {
            if (next.expression->args.size() != 2)
                return false;
            pending.push_back(next.expression->args[1]);
            pending.push_back(next.expression->args[0]);
        }
        else
        {
            terms.push_back(next);
            if (terms.size() > 3)
                return false;
        }
    }
    return true;
}
inline bool OriginalExternalCoordinateRow(SymbolicArena& arena, const Value& value, const std::string& name, int row,
                                          std::array<Value, 2>& uv)
{
    std::vector<Value> terms;
    if (!OriginalExternalCoordinateTerms(value, terms) || terms.size() != 3)
        return false;
    bool offset = false;
    std::array<bool, 2> found{{false, false}};
    for (const auto& term : terms)
    {
        if (same(term, OriginalExternalTextureSource(arena, name, false, row)))
        {
            if (offset)
                return false;
            offset = true;
            continue;
        }
        if (!is(term, "MUL") || term.expression->args.size() != 2)
            return false;
        bool matched = false;
        for (int k = 0; k < 2; ++k)
            for (int side = 0; side < 2; ++side)
                if (same(term.expression->args[side], OriginalExternalTextureSource(arena, name, true, row * 2 + k)))
                {
                    if (found[k] || matched)
                        return false;
                    found[k] = true;
                    matched = true;
                    uv[k] = term.expression->args[1 - side];
                }
        if (!matched)
            return false;
    }
    return offset && found[0] && found[1];
}
inline void InstallOriginalMaterialRuntimeTextures(SymbolicArena& arena, BindingContext& context,
                                                   const MaterialOriginalRuntimeTextureContract& contract)
{
    if (context.RuntimePreshader)
        throw Refused("runtime texture callback is already installed", "runtime texture callback conflict");
    const auto prior = context.TextureOverride;
    const auto bindings = context.TextureParameters;
    for (const auto& texture : contract.ExternalTextures)
    {
        const auto old = bindings.find(texture.first);
        if (old != bindings.end() && old->second != texture.second)
            throw Refused("external resource conflicts with another texture", "external texture binding conflict");
    }
    context.RuntimePreshader = [&arena, contract](const FMaterialPreshaderInstruction& instruction)
    {
        ValidateOriginalRuntimeTextureInstruction(contract, instruction);
        const auto name = OriginalRuntimeTextureName(contract.Uniforms, instruction);
        Vec result;
        if (instruction.Opcode == EMaterialPreshaderOpcode::TexelSize)
        {
            result = {
                {source(arena, "TextureTexelSize", name, 0), source(arena, "TextureTexelSize", name, 1), 1.0, 0.0}};
        }
        else
            for (int k = 0; k < 4; ++k)
                result[k] = OriginalExternalTextureSource(
                    arena, name, instruction.Opcode == EMaterialPreshaderOpcode::ExternalTextureCoordinateScaleRotation,
                    k);
        return result;
    };
    context.TextureOverride = [&arena, contract, prior](const Value& original, const Vec& coordinates, Value& output)
    {
        const auto& args = original.expression->args;
        if (args.size() != 8 || args[2].kind == Value::Null)
            return prior && prior(original, coordinates, output);
        if (args[2].kind != Value::Int && args[2].kind != Value::Float)
            return prior && prior(original, coordinates, output);
        const auto word = args[2].number();
        if (!std::isfinite(word) || std::floor(word) != word || word < 328 || word > INT32_MAX ||
            (std::uint64_t(word) - 328) % 8)
            return prior && prior(original, coordinates, output);
        const auto slot = int((word - 328) / 8);
        const auto texture = contract.ExternalTextures.find(slot);
        if (texture == contract.ExternalTextures.end())
            return prior && prior(original, coordinates, output);
        if (args[0].text != "TEX" || args[1].text != "2D" ||
            (args[3].kind != Value::Int && args[3].kind != Value::Float))
            throw BindingRefused("external texture read is not the original stock 2D sampler",
                                 "external texture sample shape");
        const auto samplerWord = args[3].number();
        if (!std::isfinite(samplerWord) || std::floor(samplerWord) != samplerWord || samplerWord < 1352 ||
            samplerWord > INT32_MAX || (std::uint64_t(samplerWord) - 1352) % 8)
            throw BindingRefused("external texture sampler handle is invalid", "external texture sampler identity");
        const auto sampler = contract.ExternalSamplers.find(int((samplerWord - 1352) / 8));
        if (sampler == contract.ExternalSamplers.end() || sampler->second != texture->second)
            throw BindingRefused("external texture and sampler name different registries",
                                 "external texture sampler identity");
        std::array<Value, 2> first, second;
        if (!OriginalExternalCoordinateRow(arena, coordinates[0], texture->second, 0, first) ||
            !OriginalExternalCoordinateRow(arena, coordinates[1], texture->second, 1, second) ||
            !same(first[0], second[0]) || !same(first[1], second[1]))
            throw BindingRefused("external texture coordinates are not the exact source registry affine pair",
                                 "external texture affine coordinates");
        output = arena.mk("SAMPLE", {texture->second, "plain/SSM_FromTextureAsset", first[0], first[1]});
        return true;
    };
    for (const auto& texture : contract.ExternalTextures)
        context.TextureParameters[texture.first] = texture.second;
}
}
} // P4MapKit::Material

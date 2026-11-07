// Fixed material resources retain their cooked reference index and actual asset
// identity. This source contract does not infer resources from shader slots.
#pragma once
#include "material_source_data.h"

namespace P4MapKit
{
namespace Material
{
struct MaterialFixedTextureSource
{
    std::size_t UniformIndex = 0;
    std::int32_t TextureIndex = -1;
    std::string Package, SamplerType;
};
struct MaterialOriginalFixedTextures
{
    std::map<std::size_t, MaterialFixedTextureSource> UniformEntries;
    std::map<std::size_t, std::uint8_t> UnsupportedUniformEntries;
    std::vector<FMaterialUniformTexture> Uniforms;
    Json Defaults = Object({{"fixed_textures", Json::MakeObject()},
                            {"textures", Json::MakeObject()},
                            {"sampler_types", Json::MakeObject()}});
};
inline bool MaterialFixedAnonymous(const FMaterialUniformTexture& entry)
{
    return entry.Parameter.NameIsNone || IsMember(entry.Parameter.Name, {"?", "None", ""});
}
inline const std::vector<FDecodedValue>& OriginalMaterialFixedReferences(const MaterialOriginalUserData& user)
{
    if (user.Chain.empty() || user.Chain.size() > 1024 || user.Chain.front().Class != "Material")
        throw Refused("fixed texture source has no bounded material parent chain", "fixed texture reference table");
    const auto& selected = user.Chain.back();
    if (selected.Class == "MaterialInstanceConstant")
    {
        if (const auto cached = selected.Properties.Field("CachedReferencedTextures"))
        {
            if (cached->Kind != FDecodedValue::EKind::Array || cached->Items.size() > 1000000)
                throw Refused("instance cached texture table is not a bounded array", "fixed texture reference table");
            if (!cached->Items.empty())
                return cached->Items;
        }
    }
    else if (selected.Class != "Material")
        throw Refused("fixed texture source has an unsupported selected class", "fixed texture reference table");
    const auto cached = user.Chain.front().Properties.Field("CachedExpressionData");
    const auto values =
        cached && cached->Kind == FDecodedValue::EKind::Object ? cached->Field("ReferencedTextures") : nullptr;
    if (!values || values->Kind != FDecodedValue::EKind::Array || values->Items.size() > 1000000)
        throw Refused("the cooked material has no bounded indexed texture table", "fixed texture reference table");
    return values->Items;
}
inline std::string OriginalMaterialFixedPackage(const FDecodedValue& value)
{
    if ((value.Kind != FDecodedValue::EKind::HardReference && value.Kind != FDecodedValue::EKind::SoftPath) ||
        (value.Kind == FDecodedValue::EKind::HardReference && value.ReferenceIndex == 0) || !value.SubPath.empty() ||
        value.Text.compare(0, 6, "/Game/") != 0 || value.Text.find('.') == std::string::npos)
        throw Refused("fixed texture has no supported typed cooked asset reference", "fixed texture asset reference");
    const auto package = MaterialSourcePackage(value.Text);
    if (package.size() <= 6 || value.Text.find_first_of(" \t\r\n\"'<>|?*") != std::string::npos)
        throw Refused("fixed texture asset reference is not canonical", "fixed texture asset reference");
    return package;
}
inline bool SameMaterialFixedUniform(const FMaterialUniformTexture& a, const FMaterialUniformTexture& b)
{
    return a.TextureIndex == b.TextureIndex && a.TextureType == b.TextureType && a.SamplerSource == b.SamplerSource &&
           a.VirtualLayer == b.VirtualLayer && a.Parameter.Name == b.Parameter.Name &&
           a.Parameter.NameIsNone == b.Parameter.NameIsNone && a.Parameter.LayerIndex == b.Parameter.LayerIndex &&
           a.Parameter.Association == b.Parameter.Association;
}
inline MaterialOriginalFixedTextures PrepareOriginalMaterialFixedTextures(const MaterialOriginalUserData& user,
                                                                          const FMaterialShaderMapEnvelope& map,
                                                                          const MaterialSourceAssetResolver& resolve)
{
    if (map.Uniforms.Textures.size() > 1000000)
        throw Refused("fixed texture uniform table exceeds its bound", "fixed texture uniform entries");
    MaterialOriginalFixedTextures result;
    result.Uniforms = map.Uniforms.Textures;
    const std::vector<FDecodedValue>* references = nullptr;
    std::map<std::string, std::string> resolved;
    for (std::size_t i = 0; i < result.Uniforms.size(); ++i)
    {
        const auto& entry = result.Uniforms[i];
        if (!MaterialFixedAnonymous(entry))
            continue;
        // Other kinds are refused when a compiled resource actually consumes
        // them. An unused optimized-out uniform does not invent a binding.
        if (entry.TextureType != 0)
        {
            result.UnsupportedUniformEntries.emplace(i, entry.TextureType);
            continue;
        }
        if (!references)
            references = &OriginalMaterialFixedReferences(user);
        if (entry.TextureIndex < 0 || std::size_t(entry.TextureIndex) >= references->size())
            throw Refused("fixed texture index is outside the cooked table: " + std::to_string(entry.TextureIndex),
                          "fixed texture index");
        const auto package = OriginalMaterialFixedPackage(references->at(std::size_t(entry.TextureIndex)));
        auto known = resolved.find(package);
        if (known == resolved.end())
        {
            MaterialSourceAsset asset;
            if (!resolve || !resolve(package, asset))
                throw Refused("fixed texture asset is unavailable: " + package, "fixed texture source asset");
            if (PropertyIdentity(MaterialSourcePackage(asset.Package.Package)) != PropertyIdentity(package))
                throw Refused("fixed texture resolver returned a different package", "fixed texture asset identity");
            std::string type;
            FDecodedValue properties;
            if (!MaterialSourceReadObject(asset, {"Texture2D", "MediaTexture", "TextureRenderTarget2D"}, type,
                                          properties))
                throw Refused("fixed Standard2D asset has no supported texture object: " + package,
                              "fixed texture source class");
            std::string sampler;
            FReadError error;
            if (!ReadMaterialSamplerType(type, properties, sampler, error) || sampler.empty())
                throw Refused(error.Code.empty() ? "fixed texture sampler type is unavailable" : error.Code,
                              "fixed texture sampler type");
            known = resolved.emplace(package, sampler).first;
        }
        result.UniformEntries.emplace(i, MaterialFixedTextureSource{i, entry.TextureIndex, package, known->second});
        MaterialSourceInsert(result.Defaults["fixed_textures"], package, known->second, "fixed texture sampler type");
        MaterialSourceInsert(result.Defaults["textures"], package, package, "fixed texture asset identity");
        MaterialSourceInsert(result.Defaults["sampler_types"], package, known->second, "fixed texture sampler type");
    }
    return result;
}
inline void ApplyOriginalMaterialFixedTextures(BindingContext& context, const MaterialOriginalFixedTextures& fixed)
{
    // Validate the complete compiled slot mapping before mutating any bindings.
    auto parameters = context.TextureParameters;
    for (const auto& binding : context.TextureEntries)
    {
        const auto index = binding.second;
        if (binding.first < 0 || index >= fixed.Uniforms.size() || index >= context.Uniforms.Textures.size())
            throw Refused("compiled texture resource index is outside the uniform entries",
                          "fixed texture compiled index");
        if (!SameMaterialFixedUniform(fixed.Uniforms[index], context.Uniforms.Textures[index]))
            throw Refused("fixed texture source differs from the compiled uniform identity",
                          "fixed texture uniform identity");
        if (fixed.UnsupportedUniformEntries.count(index))
            throw Refused("fixed texture kind is not supported: " +
                              std::to_string(fixed.UnsupportedUniformEntries.at(index)),
                          "fixed texture kind");
        const auto entry = fixed.UniformEntries.find(index);
        if (entry == fixed.UniformEntries.end())
            continue;
        const auto prior = parameters.find(binding.first);
        if (prior != parameters.end() && !IsMember(prior->second, {"?", "None", ""}) &&
            prior->second != entry->second.Package)
            throw Refused("compiled fixed texture slot has a conflicting identity", "fixed texture compiled identity");
        parameters[binding.first] = entry->second.Package;
    }
    context.TextureParameters = std::move(parameters);
}
}
} // P4MapKit::Material

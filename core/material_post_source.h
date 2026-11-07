// Automatic original postprocess binder from bounded payload and typed owner.
#pragma once
#include "material_source_data.h"
#include "material_domain_prepare.h"
#include "material_scene.h"
#include "material_post_bind.h"
#include <tuple>
namespace P4MapKit
{
namespace Material
{
inline MaterialPostBindingOptions PrepareOriginalPostProcessBindingOptions(const MaterialOriginalUserData& user,
                                                                           const FMaterialShaderMapEnvelope& map,
                                                                           const MaterialPreparedProgram& program)
{
    MaterialPostBindingOptions options;
    MaterialPostSceneHeaderMetadata metadata;
    metadata.LayoutHashes = program.Header.layout_hashes;
    metadata.Buffers = program.BaseBinding.Buffers;
    const auto tables =
        [&](const std::vector<std::uint32_t>& words, std::map<int, std::vector<std::pair<int, int>>>& into)
    {
        const auto parsed = CustomMaterial::ReadResourceTable(words);
        for (const auto& buffer : parsed)
            for (const auto& row : buffer.second)
                into[int(buffer.first)].push_back({int(row.first), int(row.second)});
    };
    std::string failure;
    try
    {
        tables(program.Header.textures, metadata.TextureEntries);
        tables(program.Header.srv, metadata.SRVEntries);
        tables(program.Header.samplers, metadata.SamplerEntries);
    }
    catch (const std::exception& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material post source resource table");
    MaterialPostSupplementalBindings supplemental;
    FReadError error;
    if (!ReadMaterialPostSupplemental(&map, metadata, supplemental, error))
        throw Refused(error.Code, "material post source supplemental");
    options.SupplementalInputs = supplemental.Enabled;
    options.ViewLayoutProved = supplemental.ViewLayoutProved;
    if (user.Chain.empty())
        throw Refused("original postprocess has no master properties", "material post source owner");
    const auto cached = user.Chain.front().Properties.Field("CachedExpressionData");
    const auto refs = cached ? cached->Field("ReferencedTextures") : nullptr;
    for (const auto& binding : program.BaseBinding.TextureEntries)
    {
        if (binding.second >= map.Uniforms.Textures.size())
            throw Refused("compiled texture resource index exceeds original uniform entries",
                          "material post source texture");
        const auto& entry = map.Uniforms.Textures[binding.second];
        if (!entry.Parameter.NameIsNone && !entry.Parameter.Name.empty() && entry.Parameter.Name != "None" &&
            entry.Parameter.Name != "?")
            continue;
        if (!refs || refs->Kind != FDecodedValue::EKind::Array || entry.TextureIndex < 0 ||
            std::size_t(entry.TextureIndex) >= refs->Items.size())
            throw Refused("fixed postprocess texture has no cooked index", "material post source texture");
        const auto& asset = refs->Items[std::size_t(entry.TextureIndex)];
        if (asset.Kind != FDecodedValue::EKind::HardReference && asset.Kind != FDecodedValue::EKind::SoftPath)
            throw Refused("fixed postprocess texture has no typed object reference", "material post source texture");
        MaterialSourcePackage(asset.Text);
        options.FixedTextures[binding.first] = asset.Text;
    }
    return options;
}
inline void InstallOriginalPostProcessSourceBinding(SymbolicArena& arena, BindingContext& context,
                                                    const MaterialOriginalUserData& user,
                                                    const FMaterialShaderMapEnvelope& map,
                                                    const MaterialPreparedProgram& program,
                                                    const MaterialPostBindings& bindings)
{
    auto options = PrepareOriginalPostProcessBindingOptions(user, map, program);
    if (options.SupplementalInputs)
    {
        MaterialPostSceneHeaderMetadata metadata;
        metadata.LayoutHashes = program.Header.layout_hashes;
        metadata.Buffers = program.BaseBinding.Buffers;
        const auto parsed = CustomMaterial::ReadResourceTable(program.Header.textures);
        for (const auto& buffer : parsed)
            for (const auto& row : buffer.second)
                metadata.TextureEntries[int(buffer.first)].push_back({int(row.first), int(row.second)});
        const auto srvs = CustomMaterial::ReadResourceTable(program.Header.srv);
        for (const auto& buffer : srvs)
            for (const auto& row : buffer.second)
                metadata.SRVEntries[int(buffer.first)].push_back({int(row.first), int(row.second)});
        const auto samplers = CustomMaterial::ReadResourceTable(program.Header.samplers);
        for (const auto& buffer : samplers)
            for (const auto& row : buffer.second)
                metadata.SamplerEntries[int(buffer.first)].push_back({int(row.first), int(row.second)});
        MaterialPostSupplementalBindings supplemental;
        FReadError error;
        if (!ReadMaterialPostSupplemental(&map, metadata, supplemental, error))
            throw Refused(error.Code, "material post source supplemental");
        SceneReadFactory scene;
        scene.RolesProved = true;
        for (const auto& role : supplemental.EngineTextureRoles)
            scene.TextureRoles[role.first] = role.second;
        options.SupplementalBoundRewrite = [&arena, scene](const Value& value)
        {
            return DeviceDepthValues(arena, {value}, scene)[0];
        };
    }
    if (options.SupplementalInputs)
    {
        const auto previous = context.LeafOverride;
        const auto buffers = context.Buffers;
        const auto proved = options.ViewLayoutProved;
        context.LeafOverride = [&arena, previous, buffers, proved](const Value& value, Value& out)
        {
            if (IsExpr(value, "IN") && Args(value).size() == 3 && Args(value)[0].kind == Value::Text &&
                Args(value)[0].text == "buffer")
            {
                const auto named = buffers.find(int(Args(value)[1].number()));
                if (named != buffers.end() && named->second == "View")
                {
                    const auto offset = int(Args(value)[2].number());
                    for (const auto& field :
                         std::vector<std::tuple<const char*, int, int>>{{"TranslatedWorldToView", 192, 16},
                                                                        {"TranslatedWorldToCameraView", 320, 16},
                                                                        {"ViewToClip", 448, 16},
                                                                        {"ClipToTranslatedWorld", 640, 16},
                                                                        {"BufferSizeAndInvSize", 2128, 4}})
                    {
                        const auto start = std::get<1>(field), width = std::get<2>(field);
                        if (offset >= start && offset < start + 4 * width)
                        {
                            if (!proved)
                                throw BindingRefused("postprocess water View member lacks exact layout",
                                                     "postprocess View layout");
                            if (offset % 4)
                                throw BindingRefused("postprocess water View member is not word aligned",
                                                     "buffer word alignment");
                            const auto c = (offset - start) / 4;
                            out = source(arena, "WaterViewRow",
                                         Value::tuple_value({Value(std::get<0>(field)), Value(c / 4)}), c % 4);
                            return true;
                        }
                    }
                }
            }
            return previous && previous(value, out);
        };
    }
    InstallPostProcessBinding(arena, context, bindings, options);
}
}
} // P4MapKit::Material

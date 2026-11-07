// Exact constructor resource ABI facts. Resource identity only; pass execution
// availability, renderer and production admission require separate evidence.
#pragma once
#include "material_scene_original_layout.h"
#include "material_program.h"
namespace P4MapKit
{
namespace Material
{
inline const MaterialSceneNativeRegistry& MaterialBuiltinSceneNativeRegistry()
{
    static const MaterialSceneNativeRegistry registry = []
    {
        MaterialSceneNativeRegistry out;
        {
            MaterialSceneNativeLayout layout;
            layout.Name = "FOpaqueBasePassUniformParameters";
            layout.Size = 2368;
            layout.StaticSlot = 0;
            layout.Hash = 0x0d480502;
            layout.ConstructorProvenanceProved = true;
            layout.Resources = {
                {"Shared.Forward.DirectionalLightShadowmapAtlas", 576, 5},
                {"Shared.Forward.ShadowmapSampler", 584, 8},
                {"Shared.Forward.DirectionalLightStaticShadowmap", 592, 5},
                {"Shared.Forward.StaticShadowmapSampler", 600, 8},
                {"Shared.Forward.ForwardLocalLightBuffer", 608, 6},
                {"Shared.Forward.NumCulledLightsGrid", 616, 6},
                {"Shared.Forward.CulledLightDataGrid", 624, 6},
                {"Shared.Forward.DummyRectLightSourceTexture", 632, 5},
                {"Shared.ForwardISR.DirectionalLightShadowmapAtlas", 1216, 5},
                {"Shared.ForwardISR.ShadowmapSampler", 1224, 8},
                {"Shared.ForwardISR.DirectionalLightStaticShadowmap", 1232, 5},
                {"Shared.ForwardISR.StaticShadowmapSampler", 1240, 8},
                {"Shared.ForwardISR.ForwardLocalLightBuffer", 1248, 6},
                {"Shared.ForwardISR.NumCulledLightsGrid", 1256, 6},
                {"Shared.ForwardISR.CulledLightDataGrid", 1264, 6},
                {"Shared.ForwardISR.DummyRectLightSourceTexture", 1272, 5},
                {"Shared.Reflection.SkyLightCubemap", 1304, 5},
                {"Shared.Reflection.SkyLightCubemapSampler", 1312, 8},
                {"Shared.Reflection.SkyLightBlendDestinationCubemap", 1320, 5},
                {"Shared.Reflection.SkyLightBlendDestinationCubemapSampler", 1328, 8},
                {"Shared.Reflection.ReflectionCubemap", 1336, 5},
                {"Shared.Reflection.ReflectionCubemapSampler", 1344, 8},
                {"Shared.Reflection.PreIntegratedGF", 1352, 5},
                {"Shared.Reflection.PreIntegratedGFSampler", 1360, 8},
                {"Shared.PlanarReflection.PlanarReflectionTexture", 1696, 5},
                {"Shared.PlanarReflection.PlanarReflectionSampler", 1704, 8},
                {"Shared.Fog.FogInscatteringColorCubemap", 1840, 5},
                {"Shared.Fog.FogInscatteringColorSampler", 1848, 8},
                {"Shared.Fog.IntegratedLightScattering", 1856, 9},
                {"Shared.Fog.IntegratedLightScatteringSampler", 1864, 8},
                {"Shared.FogISR.FogInscatteringColorCubemap", 2000, 5},
                {"Shared.FogISR.FogInscatteringColorSampler", 2008, 8},
                {"Shared.FogISR.IntegratedLightScattering", 2016, 9},
                {"Shared.FogISR.IntegratedLightScatteringSampler", 2024, 8},
                {"Shared.SSProfilesTexture", 2032, 5},
                {"SceneTextures.SceneColorTexture", 2048, 9},
                {"SceneTextures.SceneDepthTexture", 2056, 9},
                {"SceneTextures.GBufferATexture", 2064, 9},
                {"SceneTextures.GBufferBTexture", 2072, 9},
                {"SceneTextures.GBufferCTexture", 2080, 9},
                {"SceneTextures.GBufferDTexture", 2088, 9},
                {"SceneTextures.GBufferETexture", 2096, 9},
                {"SceneTextures.GBufferFTexture", 2104, 9},
                {"SceneTextures.GBufferVelocityTexture", 2112, 9},
                {"SceneTextures.ScreenSpaceAOTexture", 2120, 9},
                {"SceneTextures.CustomDepthTexture", 2128, 9},
                {"SceneTextures.CustomStencilTexture", 2136, 6},
                {"SceneTextures.SceneStencilTexture", 2144, 6},
                {"SceneTextures.PointClampSampler", 2152, 8},
                {"ForwardScreenSpaceShadowMaskTexture", 2168, 9},
                {"IndirectOcclusionTexture", 2176, 9},
                {"ResolvedSceneDepthTexture", 2184, 9},
                {"DBufferATexture", 2192, 9},
                {"DBufferATextureSampler", 2200, 8},
                {"DBufferBTexture", 2208, 9},
                {"DBufferBTextureSampler", 2216, 8},
                {"DBufferCTexture", 2224, 9},
                {"DBufferCTextureSampler", 2232, 8},
                {"DBufferRenderMask", 2240, 9},
                {"SceneColorWithoutSingleLayerWaterTexture", 2248, 9},
                {"SceneColorWithoutSingleLayerWaterSampler", 2256, 8},
                {"SceneDepthWithoutSingleLayerWaterTexture", 2264, 9},
                {"SceneDepthWithoutSingleLayerWaterSampler", 2272, 8},
                {"PreIntegratedGFTexture", 2280, 5},
                {"PreIntegratedGFSampler", 2288, 8},
                {"EyeAdaptationTexture", 2336, 9},
                {"SceneColorCopyTexture", 2344, 9},
                {"SceneColorCopySampler", 2352, 8},
            };
            ValidateMaterialSceneNativeLayout(layout);
            out.emplace(layout.Hash, std::move(layout));
        }
        {
            MaterialSceneNativeLayout layout;
            layout.Name = "FTranslucentBasePassUniformParameters";
            layout.Size = 2528;
            layout.StaticSlot = 0;
            layout.Hash = 0x05ec0dee;
            layout.ConstructorProvenanceProved = true;
            layout.Resources = {
                {"Shared.Forward.DirectionalLightShadowmapAtlas", 576, 5},
                {"Shared.Forward.ShadowmapSampler", 584, 8},
                {"Shared.Forward.DirectionalLightStaticShadowmap", 592, 5},
                {"Shared.Forward.StaticShadowmapSampler", 600, 8},
                {"Shared.Forward.ForwardLocalLightBuffer", 608, 6},
                {"Shared.Forward.NumCulledLightsGrid", 616, 6},
                {"Shared.Forward.CulledLightDataGrid", 624, 6},
                {"Shared.Forward.DummyRectLightSourceTexture", 632, 5},
                {"Shared.ForwardISR.DirectionalLightShadowmapAtlas", 1216, 5},
                {"Shared.ForwardISR.ShadowmapSampler", 1224, 8},
                {"Shared.ForwardISR.DirectionalLightStaticShadowmap", 1232, 5},
                {"Shared.ForwardISR.StaticShadowmapSampler", 1240, 8},
                {"Shared.ForwardISR.ForwardLocalLightBuffer", 1248, 6},
                {"Shared.ForwardISR.NumCulledLightsGrid", 1256, 6},
                {"Shared.ForwardISR.CulledLightDataGrid", 1264, 6},
                {"Shared.ForwardISR.DummyRectLightSourceTexture", 1272, 5},
                {"Shared.Reflection.SkyLightCubemap", 1304, 5},
                {"Shared.Reflection.SkyLightCubemapSampler", 1312, 8},
                {"Shared.Reflection.SkyLightBlendDestinationCubemap", 1320, 5},
                {"Shared.Reflection.SkyLightBlendDestinationCubemapSampler", 1328, 8},
                {"Shared.Reflection.ReflectionCubemap", 1336, 5},
                {"Shared.Reflection.ReflectionCubemapSampler", 1344, 8},
                {"Shared.Reflection.PreIntegratedGF", 1352, 5},
                {"Shared.Reflection.PreIntegratedGFSampler", 1360, 8},
                {"Shared.PlanarReflection.PlanarReflectionTexture", 1696, 5},
                {"Shared.PlanarReflection.PlanarReflectionSampler", 1704, 8},
                {"Shared.Fog.FogInscatteringColorCubemap", 1840, 5},
                {"Shared.Fog.FogInscatteringColorSampler", 1848, 8},
                {"Shared.Fog.IntegratedLightScattering", 1856, 9},
                {"Shared.Fog.IntegratedLightScatteringSampler", 1864, 8},
                {"Shared.FogISR.FogInscatteringColorCubemap", 2000, 5},
                {"Shared.FogISR.FogInscatteringColorSampler", 2008, 8},
                {"Shared.FogISR.IntegratedLightScattering", 2016, 9},
                {"Shared.FogISR.IntegratedLightScatteringSampler", 2024, 8},
                {"Shared.SSProfilesTexture", 2032, 5},
                {"SceneTextures.SceneColorTexture", 2048, 9},
                {"SceneTextures.SceneDepthTexture", 2056, 9},
                {"SceneTextures.GBufferATexture", 2064, 9},
                {"SceneTextures.GBufferBTexture", 2072, 9},
                {"SceneTextures.GBufferCTexture", 2080, 9},
                {"SceneTextures.GBufferDTexture", 2088, 9},
                {"SceneTextures.GBufferETexture", 2096, 9},
                {"SceneTextures.GBufferFTexture", 2104, 9},
                {"SceneTextures.GBufferVelocityTexture", 2112, 9},
                {"SceneTextures.ScreenSpaceAOTexture", 2120, 9},
                {"SceneTextures.CustomDepthTexture", 2128, 9},
                {"SceneTextures.CustomStencilTexture", 2136, 6},
                {"SceneTextures.SceneStencilTexture", 2144, 6},
                {"SceneTextures.PointClampSampler", 2152, 8},
                {"ForwardScreenSpaceShadowMaskTexture", 2168, 9},
                {"DBufferATexture", 2176, 9},
                {"DBufferATextureSampler", 2184, 8},
                {"DBufferBTexture", 2192, 9},
                {"DBufferBTextureSampler", 2200, 8},
                {"DBufferCTexture", 2208, 9},
                {"DBufferCTextureSampler", 2216, 8},
                {"DBufferRenderMask", 2224, 9},
                {"HZBTexture", 2296, 9},
                {"HZBSampler", 2304, 8},
                {"PrevSceneColor", 2312, 9},
                {"PrevSceneColorSampler", 2320, 8},
                {"MaterialSSRTexture", 2368, 9},
                {"MaterialSSRSampler", 2376, 8},
                {"VolumetricCloudColor", 2384, 5},
                {"VolumetricCloudColorSampler", 2392, 8},
                {"VolumetricCloudDepth", 2400, 5},
                {"VolumetricCloudDepthSampler", 2408, 8},
                {"TranslucencyLightingVolumeAmbientInner", 2424, 9},
                {"TranslucencyLightingVolumeAmbientInnerSampler", 2432, 8},
                {"TranslucencyLightingVolumeAmbientOuter", 2440, 9},
                {"TranslucencyLightingVolumeAmbientOuterSampler", 2448, 8},
                {"TranslucencyLightingVolumeDirectionalInner", 2456, 9},
                {"TranslucencyLightingVolumeDirectionalInnerSampler", 2464, 8},
                {"TranslucencyLightingVolumeDirectionalOuter", 2472, 9},
                {"TranslucencyLightingVolumeDirectionalOuterSampler", 2480, 8},
                {"PreIntegratedGFTexture", 2488, 5},
                {"PreIntegratedGFSampler", 2496, 8},
                {"EyeAdaptationTexture", 2504, 9},
                {"SceneColorCopyTexture", 2512, 9},
                {"SceneColorCopySampler", 2520, 8},
            };
            ValidateMaterialSceneNativeLayout(layout);
            out.emplace(layout.Hash, std::move(layout));
        }
        {
            MaterialSceneNativeLayout layout;
            layout.Name = "FSceneTextureUniformParameters";
            layout.Size = 112;
            layout.StaticSlot = 0;
            layout.Hash = 0x097f0609;
            layout.ConstructorProvenanceProved = true;
            layout.Resources = {
                {"SceneColorTexture", 0, 9},     {"SceneDepthTexture", 8, 9},   {"GBufferATexture", 16, 9},
                {"GBufferBTexture", 24, 9},      {"GBufferCTexture", 32, 9},    {"GBufferDTexture", 40, 9},
                {"GBufferETexture", 48, 9},      {"GBufferFTexture", 56, 9},    {"GBufferVelocityTexture", 64, 9},
                {"ScreenSpaceAOTexture", 72, 9}, {"CustomDepthTexture", 80, 9}, {"CustomStencilTexture", 88, 6},
                {"SceneStencilTexture", 96, 6},  {"PointClampSampler", 104, 8},
            };
            ValidateMaterialSceneNativeLayout(layout);
            out.emplace(layout.Hash, std::move(layout));
        }
        return out;
    }();
    return registry;
}
struct MaterialNativeSceneSamplerFacts
{
    int CopySamplerSlot = -1;
    std::string CopySamplerRole, CopySamplerIdentity;
    std::vector<std::size_t> CopyConsumerLines;
    bool UsesHeaderDefault = false;
};
struct MaterialNativeSceneResources
{
    MaterialOriginalSceneResourcePolicy Policy;
    SceneReadFactory Factory;
    MaterialNativeSceneSamplerFacts SamplerFacts;
};
inline MaterialOriginalSceneResourcePolicy
ReadMaterialNativeSceneResourcePolicy(const FMaterialShaderReference& reference,
                                      const CustomMaterial::ShaderBindings& header, const std::string& domain,
                                      const std::string& blend, bool originalAssociationProved)
{
    if (!originalAssociationProved || reference.Frequency != 3)
        throw Refused("original pixel association is unqualified", "material scene association");
    bool known = false;
    for (const auto hash : header.layout_hashes)
        known |= MaterialBuiltinSceneNativeRegistry().count(hash) != 0;
    if (!known)
        throw Refused("no constructor-qualified native scene layout is present", "material native scene layout");
    return ReadMaterialOriginalSceneResourcePolicy(reference, header, MaterialBuiltinSceneNativeRegistry(), domain,
                                                   blend, true);
}
namespace NativeSceneDetail
{
struct Handle
{
    bool Known = false;
    std::vector<std::pair<int, int>> Words;
};
using Handles = std::array<Handle, 4>;
using Registers = std::map<std::string, Handles>;
inline bool equal(const Handle& a, const Handle& b)
{
    return a.Known == b.Known && a.Words == b.Words;
}
inline Handles read(const Registers& registers, const std::string& text)
{
    static const std::regex pattern("^([A-Za-z_]\\w*)(?:\\.([xyzw]+))?$");
    std::smatch match;
    Handles out;
    if (!std::regex_match(text, match, pattern))
        return out;
    const auto found = registers.find(match[1]);
    if (found == registers.end())
        return out;
    const auto swizzle = match[2].matched ? match[2].str() : "xyzw";
    if (swizzle.size() != 1 && swizzle.size() != 4)
        return out;
    for (int c = 0; c < 4; ++c)
        out[c] = found->second[std::size_t(NumericMachine::component(swizzle[swizzle.size() == 1 ? 0 : c]))];
    return out;
}
inline void write(Registers& registers, const std::string& text, const Handles& values)
{
    static const std::regex pattern("^([A-Za-z_]\\w*)(?:\\.([xyzw]+))?(?:\\s*\\(([^)]*)\\))?$");
    std::smatch match;
    if (!std::regex_match(text, match, pattern))
        return;
    const auto mask = match[2].matched ? match[2].str() : "xyzw";
    auto& out = registers[match[1]];
    for (const auto component : mask)
    {
        const auto c = std::size_t(NumericMachine::component(component));
        out[c] = match[3].matched ? Handle() : values[c];
    }
}
inline Registers merge(const Registers& a, const Registers& b)
{
    Registers out;
    std::set<std::string> names;
    for (const auto& item : a)
        names.insert(item.first);
    for (const auto& item : b)
        names.insert(item.first);
    for (const auto& name : names)
    {
        const auto left = a.find(name), right = b.find(name);
        const auto av = left == a.end() ? Handles() : left->second, bv = right == b.end() ? Handles() : right->second;
        for (int c = 0; c < 4; ++c)
            if (equal(av[c], bv[c]))
                out[name][c] = av[c];
    }
    return out;
}
inline std::pair<int, int> cache(const Handle& handle)
{
    if (!handle.Known || handle.Words.empty())
        throw Refused("texture consumer has no immutable original handle producer", "material native scene handle");
    int texture = -1, sampler = -1;
    for (const auto& word : handle.Words)
    {
        if (word.first != 0)
            throw Refused("texture handle is outside the original immutable cache", "material native scene handle");
        if (word.second >= 328 && word.second < 328 + 64 * 8 && (word.second - 328) % 8 == 0)
        {
            if (texture != -1)
                throw Refused("texture handle combines different native resources", "material native scene handle");
            texture = (word.second - 328) / 8;
        }
        else if (word.second >= 1352 && word.second < 1352 + 32 * 8 && (word.second - 1352) % 8 == 0)
        {
            if (sampler != -1)
                throw Refused("texture handle combines different native samplers", "material native scene handle");
            sampler = (word.second - 1352) / 8;
        }
        else
            throw Refused("texture handle word has no original cache identity", "material native scene handle");
    }
    if (texture < 0)
        throw Refused("texture handle has no immutable texture identity", "material native scene handle");
    return {texture, sampler};
}
}
// Interpret only immutable handle dataflow. Arithmetic values are never guessed.
// IF branches merge only identical producers; unresolved handles/jumps refuse.
inline MaterialNativeSceneSamplerFacts
ObserveMaterialNativeSceneCopySampler(const std::vector<std::string>& originalLines,
                                      const MaterialSceneResourcePolicy& policy, int originalHeaderDefaultSampler = -1)
{
    MaterialNativeSceneSamplerFacts facts;
    const auto copy = policy.Textures.find("scene_color_copy");
    if (copy == policy.Textures.end())
        return facts;
    const auto program = NormalizeMaterialTraceDeclarations(originalLines);
    NativeSceneDetail::Registers registers;
    struct Branch
    {
        NativeSceneDetail::Registers Before, Then;
        bool HasElse = false;
    };
    std::vector<Branch> branches;
    for (std::size_t line = 0; line < program.Lines.size(); ++line)
    {
        const auto& text = program.Lines[line];
        const auto space = text.find(' ');
        const auto head = text.substr(0, space);
        const auto dot = head.find('.');
        const auto op = head.substr(0, dot);
        const auto args = split_operands(space == std::string::npos ? "" : text.substr(space + 1));
        if (op == "IF")
        {
            branches.push_back({registers, {}, false});
            continue;
        }
        if (op == "ELSE")
        {
            if (branches.empty() || branches.back().HasElse)
                throw Refused("unpaired scene handle branch", "material native scene handle");
            branches.back().Then = registers;
            branches.back().HasElse = true;
            registers = branches.back().Before;
            continue;
        }
        if (op == "ENDIF")
        {
            if (branches.empty())
                throw Refused("unpaired scene handle branch", "material native scene handle");
            const auto branch = branches.back();
            branches.pop_back();
            registers = NativeSceneDetail::merge(branch.HasElse ? branch.Then : branch.Before, registers);
            continue;
        }
        if (IsMember(op, {"REP", "ENDREP", "BRK", "CAL", "RET", "BRA"}))
            throw Refused("scene handle control flow is not proved", "material native scene handle");
        NativeSceneDetail::Handles values;
        if (op == "LDC" && head.find(".U64") != std::string::npos && args.size() == 2)
        {
            static const std::regex word("^buf([0-9]+)\\[([0-9]+)\\]$");
            std::smatch match;
            if (std::regex_match(args[1], match, word))
            {
                const auto buffer = MaterialDeclarationIndex(match[1]), offset = MaterialDeclarationIndex(match[2]);
                if (buffer <= 31 && offset <= 4096)
                    for (auto& value : values)
                    {
                        value.Known = true;
                        value.Words = {{int(buffer), int(offset)}};
                    }
            }
        }
        else if (op == "MOV" && args.size() == 2)
            values = NativeSceneDetail::read(registers, args[1]);
        else if (op == "OR" && head.find(".U64") != std::string::npos && args.size() == 3)
        {
            const auto a = NativeSceneDetail::read(registers, args[1]), b = NativeSceneDetail::read(registers, args[2]);
            for (int c = 0; c < 4; ++c)
                if (a[c].Known && b[c].Known)
                {
                    values[c].Known = true;
                    values[c].Words = a[c].Words;
                    values[c].Words.insert(values[c].Words.end(), b[c].Words.begin(), b[c].Words.end());
                    std::sort(values[c].Words.begin(), values[c].Words.end());
                    values[c].Words.erase(std::unique(values[c].Words.begin(), values[c].Words.end()),
                                          values[c].Words.end());
                }
        }
        else if (IsMember(op, {"TEX", "TXB", "TXL", "TXF", "TXD", "TXP", "TXFMS", "TXQ", "TXG"}))
        {
            if (args.size() < 3)
                throw Refused("texture instruction is incomplete", "material native scene handle");
            static const std::regex handle("^handle\\(([A-Za-z_]\\w*)(?:\\.([xyzw]))?\\)$");
            std::smatch match;
            if (!std::regex_match(args[2], match, handle))
                throw Refused("texture instruction has no immutable handle operand", "material native scene handle");
            const auto name = match[1].str() + "." + (match[2].matched ? match[2].str() : "x");
            const auto slots = NativeSceneDetail::cache(NativeSceneDetail::read(registers, name)[0]);
            if (slots.first == copy->second)
            {
                if (op != "TEX" || (args.size() > 3 && args[3] != "2D"))
                    throw Refused("scene copy uses an unproved sampling operation",
                                  "material native scene copy sampler");
                int sampler = slots.second;
                if (sampler < 0)
                {
                    sampler = originalHeaderDefaultSampler;
                    facts.UsesHeaderDefault = true;
                }
                std::vector<std::string> roles;
                for (const auto& candidate : policy.Samplers)
                    if (candidate.second == sampler && IsMember(candidate.first, {"view_clamp", "copy", "point"}))
                        roles.push_back(candidate.first);
                if (roles.size() != 1 || sampler < 0 || sampler > 31)
                    throw Refused("observed scene copy sampler lacks one original role",
                                  "material native scene copy sampler");
                if (facts.CopySamplerSlot >= 0 &&
                    (facts.CopySamplerSlot != sampler || facts.CopySamplerRole != roles[0]))
                    throw Refused("scene copy consumers use different native samplers",
                                  "material native scene copy sampler");
                facts.CopySamplerSlot = sampler;
                facts.CopySamplerRole = roles[0];
                facts.CopyConsumerLines.push_back(program.OriginalLines[line]);
            }
        }
        if (!args.empty())
            NativeSceneDetail::write(registers, args[0], values);
    }
    if (!branches.empty())
        throw Refused("scene handle branch has no terminator", "material native scene handle");
    return facts;
}
inline MaterialNativeSceneResources
BindMaterialNativeSceneResources(const FMaterialShaderReference& reference,
                                 const CustomMaterial::ShaderBindings& header, const std::string& domain,
                                 const std::string& blend, const std::vector<std::string>& originalLines,
                                 const std::function<Value(int)>& textureIdentity,
                                 const std::function<std::string(int)>& samplerIdentity, bool originalAssociationProved)
{
    MaterialNativeSceneResources out;
    out.Policy = ReadMaterialNativeSceneResourcePolicy(reference, header, domain, blend, originalAssociationProved);
    const auto& policy = out.Policy.ResourcePolicy;
    if (!policy.ResourceRolesProved || !textureIdentity)
        throw Refused("native scene has no checked texture identity resolver", "material scene factory identities");
    out.SamplerFacts = ObserveMaterialNativeSceneCopySampler(originalLines, policy, header.default_sampler);
    out.Factory.RolesProved = true;
    for (const auto& resource : policy.Textures)
    {
        if (resource.first == "scene_color_copy" && out.SamplerFacts.CopySamplerSlot < 0)
            continue;
        const auto identity = textureIdentity(resource.second);
        if (identity.kind == Value::Null)
            throw Refused("native scene texture identity is null", "material scene factory identities");
        out.Factory.TextureRoles[resource.first] = identity;
    }
    if (out.SamplerFacts.CopySamplerSlot >= 0)
    {
        if (!samplerIdentity)
            throw Refused("observed native copy sampler has no identity resolver", "material scene factory sampler");
        out.SamplerFacts.CopySamplerIdentity = samplerIdentity(out.SamplerFacts.CopySamplerSlot);
        if (out.SamplerFacts.CopySamplerIdentity.empty())
            throw Refused("observed native copy sampler identity is empty", "material scene factory sampler");
        out.Factory.CopySampler = out.SamplerFacts.CopySamplerRole;
    }
    return out;
}
}
}

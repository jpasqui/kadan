// Actual package -> automatic resource boundary -> original programs -> owned
// native family producer. This API accepts assets and archive access, not plans.
#pragma once
#include "material_source_unlit_scene.h"
#include "material_source_unlit_remaining.h"
#include "material_source_unlit_remaining_capture.h"
#include "material_source_unlit_producers.h"
#include "material_source_data.h"
#include "material_fixed_source.h"
#include "material_source_runtime_textures.h"
#include "material_source_vector_anchors.h"
#include "material_source_scene.h"
#include "material_domain_prepare.h"
#include "material_light_function.h"
#include "material_decals.h"
#include "material_post.h"
#include "material_post_source.h"
#include "material_unlit.h"
#include "material_resource_boundary.h"
#include "material_source_view_tail.h"
#include "material_source_precomputed_ao.h"
#include "material_source_particle_lit_reflection.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
// The engine bindings Python's Binder gives every lit surface program (material_lighting.load prepares
// the reflection resources of each; EyeAdaptation is the View's 1x1 exposure texel): the authored
// reflection reads and the exposure texel bind as their engine sources instead of unknown textures.
inline void InstallOriginalLitSurfaceEngineReads(SymbolicArena& arena, BindingContext& context,
                                                 const MaterialPreparedProgram& program, const Json& settings)
{
    if (settings.At("domain").Text() != "MD_Surface" || settings.At("shading_model").Text() == "MSM_Unlit")
        return;
    InstallOriginalParticleLitEyeAdaptation(arena, context, program.Reference, program.Header, settings);
    InstallOriginalParticleLitReflection(arena, context, program.Reference, program.Header, settings);
}
struct MaterialOriginalOwnerSource
{
    MaterialSourceAsset Asset;
    FMaterialResourceBoundary Boundary;
    FMaterialShaderMapEnvelope Map;
    std::vector<FMaterialShaderReference> References;
    MaterialOriginalUserData User;
    MaterialOriginalFixedTextures Fixed;
};
inline MaterialOriginalOwnerSource
ReadOriginalMaterialOwnerSource(const MaterialSourceAsset& selected, const MaterialSourceAssetResolver& resolve,
                                const std::vector<MaterialSourceAsset>& collections = {})
{
    if (!selected.Body || !selected.Package.Error.Code.empty())
        throw Refused("original material package is unavailable or invalid", "material source package");
    MaterialOriginalOwnerSource result;
    result.Asset = selected;
    std::size_t index = selected.Package.Exports.size();
    for (std::size_t i = 0; i < selected.Package.Exports.size(); ++i)
    {
        std::string type;
        if (!selected.Package.Reference(selected.Package.Exports[i].Class, type))
            throw Refused("original material class reference is invalid", "material source package");
        type = type.substr(type.find_last_of('.') == std::string::npos ? 0 : type.find_last_of('.') + 1);
        if (type == "Material" || type == "MaterialInstanceConstant")
        {
            if (index != selected.Package.Exports.size())
                throw Refused("original package has several material roots", "material source owner");
            index = i;
        }
    }
    if (index == selected.Package.Exports.size())
        throw Refused("original package contains no material", "material source owner");
    FReadError error;
    if (!DiscoverMaterialResourceBoundary(selected.Package, *selected.Body, index, result.Boundary, error))
        throw Refused(error.Code, "material source resource boundary");
    if (!ReadMaterialShaderMapEnvelope(*selected.Body, result.Boundary.ResourceStart, result.Boundary.ExportEnd,
                                       result.Map, error) ||
        !ReadMaterialShaderReferences(result.Map, result.References, error))
        throw Refused(error.Code, "material source shader map");
    result.User = ReadOriginalMaterialUserData(selected, result.Map, resolve, collections);
    if (IsMember(result.User.Cooked.Domain, {"MD_Surface", "MD_UI", "MD_DeferredDecal", "MD_PostProcess"}))
        result.Fixed = PrepareOriginalMaterialFixedTextures(result.User, result.Map, resolve);
    return result;
}
inline Json OriginalMaterialGraphDefaults(const MaterialOriginalOwnerSource& owner)
{
    auto defaults = owner.User.Defaults;
    if (IsMember(owner.User.Cooked.Domain, {"MD_Surface", "MD_UI", "MD_DeferredDecal"}))
    {
        defaults["fixed_textures"] = owner.Fixed.Defaults.At("fixed_textures");
        for (const auto* table : {"textures", "sampler_types"})
            for (const auto& field : owner.Fixed.Defaults.At(table).object)
                MaterialSourceInsert(defaults[table], field.first, field.second,
                                     "material source fixed default identity");
    }
    else if (owner.User.Cooked.Domain == "MD_PostProcess")
        defaults["fixed_textures"] = owner.Fixed.Defaults.At("fixed_textures");
    return defaults;
}
inline MaterialPostProcessSettings OriginalMaterialPostSettings(const MaterialOriginalUserData& user)
{
    MaterialPostProcessSettings result;
    const auto& props = user.Chain.front().Properties;
    if (const auto field = props.Field("BlendableLocation"))
        if (!MaterialEnum(*field, result.Location))
            throw Refused("postprocess location has an invalid source type", "material source post settings");
    if (const auto field = props.Field("BlendableOutputAlpha"))
        if (!MaterialBool(*field, result.OutputAlpha))
            throw Refused("postprocess output alpha has an invalid source type", "material source post settings");
    if (const auto field = props.Field("bIsBlendable"))
        if (!MaterialBool(*field, result.IsBlendable))
            throw Refused("postprocess blendable flag has an invalid source type", "material source post settings");
    if (const auto field = props.Field("BlendablePriority"))
    {
        std::int64_t value = 0;
        std::istringstream input(field->Text);
        input >> value;
        if (field->Kind != FDecodedValue::EKind::Number || input.fail() || !input.eof() ||
            value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            throw Refused("postprocess priority has an invalid source integer", "material source post settings");
        result.Priority = int(value);
    }
    return result;
}
struct MaterialOriginalProgramRecovery
{
    MaterialRecoveryResult Material;
    MaterialDomainPreparedProgram Source;
    std::vector<std::size_t> OriginalLines;
    std::vector<MaterialStorageDeclaration> Storage;
    bool ProductionAdmitted = false;
};
// Re-read the bounded map before consuming caller-supplied reference metadata.
// The identity selects a native record; aliases/resource tables come from disk.
inline FMaterialShaderReference CanonicalOriginalMaterialReference(const MaterialOriginalOwnerSource& owner,
                                                                   const FMaterialShaderReference& selected)
{
    std::vector<FMaterialShaderReference> records;
    FReadError error;
    if (!ReadMaterialShaderReferences(owner.Map, records, error))
        throw Refused(error.Code, "material source shader map");
    const FMaterialShaderReference* found = nullptr;
    for (const auto& ref : records)
        if (ref.TypeHash == selected.TypeHash && ref.VertexFactoryHash == selected.VertexFactoryHash &&
            ref.Frequency == selected.Frequency && ref.ResourceIndex == selected.ResourceIndex &&
            ref.Permutation == selected.Permutation && ref.FrozenOffset == selected.FrozenOffset)
        {
            if (found)
                throw Refused("original shader identity is ambiguous", "material source shader association");
            found = &ref;
        }
    if (!found)
        throw Refused("selected original shader is absent from its bounded map", "material source shader association");
    return *found;
}
// Install the same source-qualified scene inputs for every generic surface
// producer; particle-only dispatch must not decide whether EyeAdaptation exists.
inline void InstallOriginalMaterialSourceSceneInputs(SymbolicArena& arena, BindingContext& context,
                                                     const FMaterialShaderReference& reference,
                                                     const CustomMaterial::ShaderBindings& header, const Json& settings)
{
    InstallOriginalMaterialViewRows(arena, context, reference, header, true);
    InstallOriginalMaterialPrecomputedAO(context, reference, header, true);
    if (settings.At("domain").Text() == "MD_Surface")
        InstallOriginalMaterialEyeAdaptation(arena, context, reference, header, settings);
}
inline MaterialOriginalProgramRecovery
RecoverOriginalMaterialProgram(const MaterialOriginalOwnerSource& owner, const FMaterialShaderReference& reference,
                               const CustomMaterial::Bytes& payload, std::uint8_t archiveFrequency,
                               MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
                               const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding = {},
                               const MaterialSurfaceRewrites& surfaceRewrites = {})
{
    // Reference is associated to this map again by preparation; array position,
    // caller-created shader type names and manual program offsets confer no trust.
    MaterialOriginalProgramRecovery result;
    result.Source =
        PrepareOriginalMaterialDomainPixelProgram(owner.Map, CanonicalOriginalMaterialReference(owner, reference),
                                                  payload, archiveFrequency, owner.User.Settings, scope);
    result.Source.Program.BaseBinding.Collections = owner.User.Collections;
    const auto domain = owner.User.Cooked.Domain;
    const auto post = result.Source.PostBindings;
    const auto runtime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, result.Source.Program);
    const auto sourcePlan = PlanOriginalMaterialPixelRecovery(
        result.Source.Program, owner.User.Settings, OriginalMaterialGraphDefaults(owner),
        [domain, post, engineBinding, owner, runtime, program = result.Source.Program](SymbolicArena& arena,
                                                                                       BindingContext& context)
        {
            if (IsMember(domain, {"MD_Surface", "MD_UI", "MD_DeferredDecal"}))
                ApplyOriginalMaterialFixedTextures(context, owner.Fixed);
            InstallOriginalMaterialRuntimeTextures(arena, context, runtime);
            if (domain == "MD_PostProcess")
                InstallOriginalPostProcessSourceBinding(arena, context, owner.User, owner.Map, program, post);
            InstallOriginalLitSurfaceEngineReads(arena, context, program, owner.User.Settings);
            if (engineBinding)
                engineBinding(arena, context);
            InstallOriginalMaterialSourceSceneInputs(arena, context, program.Reference, program.Header,
                                                     owner.User.Settings);
        });
    result.OriginalLines = sourcePlan.OriginalLines;
    result.Storage = sourcePlan.StorageDeclarations;
    const auto plan = WithOriginalMaterialViewTail(sourcePlan.Recovery, result.Source.Program.Reference,
                                                   result.Source.Program.Header, true);
    if (domain == "MD_LightFunction")
        result.Material = RecoverLightFunctionMaterialExpressionGraph(plan);
    else if (domain == "MD_PostProcess")
        result.Material = RecoverPostProcessExpressionGraph(plan, OriginalMaterialPostSettings(owner.User));
    else if (owner.User.Cooked.ShadingModel == "MSM_Unlit")
    {
        if (domain == "MD_Surface" &&
            IsMember(owner.User.Cooked.BlendMode, {"BLEND_Opaque", "BLEND_Translucent", "BLEND_Additive"}))
        {
            auto unlit = plan;
            const auto scene =
                PrepareOriginalMaterialSceneRewrites(result.Source.Program, owner.User.Settings, surfaceRewrites);
            unlit.Rewrite = [scene](SymbolicArena& arena, std::vector<std::vector<Value>>& values)
            {
                std::vector<Value> flat;
                for (const auto& value : values)
                    flat.insert(flat.end(), value.begin(), value.end());
                const auto count = flat.size();
                if (scene.SceneAndSun)
                    flat = scene.SceneAndSun(arena, flat);
                if (flat.size() != count)
                    throw Refused("original unlit scene rewrite changed output width",
                                  "material surface rewrite width");
                std::size_t at = 0;
                for (auto& value : values)
                    for (auto& component : value)
                        component = flat.at(at++);
            };
            MaterialUnlitSceneDispatch dispatch{owner.User, unlit, result.Source.Program, {}};
            RunMaterialSourceProducers(MaterialUnlitSceneProducers(), dispatch);
            result.Material = std::move(dispatch.Material);
        }
        else
            result.Material = RecoverUnlitMaterialExpressionGraph(plan);
    }
    else
    {
        std::vector<AnchorInstruction> code;
        MaterialAnchors anchors;
        MaterialOriginalSourceAnchors original;
        std::string failure;
        try
        {
            code = ParseMaterialAnchorInstructions(plan.Lines);
            if (domain == "MD_Surface")
            {
                original = PrepareOriginalMaterialLateVectorAnchors(result.Source.Program, owner.User.Settings,
                                                                    sourcePlan.Factory);
                anchors = original.Generic;
            }
            else
            {
                std::string tangent;
                const auto varying = plan.Binding.Varyings.find("in_texcoord11");
                if (varying != plan.Binding.Varyings.end())
                    tangent = "fragment.attrib[" + std::to_string(varying->second) + "]";
                anchors = FindMaterialAnchors(code, tangent);
            }
        }
        catch (const EvalError& error)
        {
            failure = error.what();
        }
        if (!failure.empty())
            throw Refused(failure, "material source anchor scan");
        if (domain == "MD_DeferredDecal")
        {
            std::string mode = "DBM_Translucent";
            if (const auto field = owner.User.Chain.front().Properties.Field("DecalBlendMode"))
                if (!MaterialEnum(*field, mode))
                    throw Refused("original decal blend mode has an invalid source type",
                                  "material source decal settings");
            result.Material = RecoverDecalMaterialExpressionGraph(plan, anchors, mode, surfaceRewrites);
        }
        else
            result.Material = RecoverAnchoredSurfaceExpressionGraph(
                plan, anchors,
                PrepareOriginalMaterialSceneRewrites(result.Source.Program, owner.User.Settings, surfaceRewrites),
                nullptr, &original);
    }
    return result;
}
struct MaterialOriginalArchiveAccess
{
    std::function<std::vector<std::uint32_t>(const std::array<std::uint8_t, 20>&)> MapIndices;
    std::function<CustomMaterial::Bytes(std::uint32_t)> Payload;
    std::function<std::uint8_t(std::uint32_t)> Frequency;
};
inline void RequireOriginalArchiveAccess(const MaterialOriginalArchiveAccess& archive)
{
    if (!archive.MapIndices || !archive.Payload || !archive.Frequency)
        throw Refused("original shader archive access is incomplete", "material source archive");
}
// One shader of an owner's map as the archive holds it.
struct MaterialArchiveProgram
{
    std::uint32_t Index = 0;
    CustomMaterial::Bytes Payload;
    std::uint8_t Frequency = 0;
};
// The archive entry of one of the owner's canonical shader references (CanonicalOriginalMaterialReference).
inline MaterialArchiveProgram ArchiveProgram(const MaterialOriginalOwnerSource& owner,
                                             const FMaterialShaderReference& canonical,
                                             const MaterialOriginalArchiveAccess& archive)
{
    MaterialArchiveProgram result;
    result.Index = ArchiveIndexOf(archive.MapIndices(owner.Map.ResourceHash), canonical,
                                  "original shader resource index is outside its archive map",
                                  "material source archive association");
    result.Payload = archive.Payload(result.Index);
    result.Frequency = archive.Frequency(result.Index);
    return result;
}
inline MaterialOriginalProgramRecovery
RecoverOriginalMaterialSelectedProgram(const MaterialOriginalOwnerSource& owner,
                                       const FMaterialShaderReference& reference,
                                       const MaterialOriginalArchiveAccess& archive,
                                       MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
                                       const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding = {},
                                       const MaterialSurfaceRewrites& rewrites = {})
{
    RequireOriginalArchiveAccess(archive);
    const auto canonical = CanonicalOriginalMaterialReference(owner, reference);
    const auto shader = ArchiveProgram(owner, canonical, archive);
    return RecoverOriginalMaterialProgram(owner, canonical, shader.Payload, shader.Frequency, scope, engineBinding,
                                          rewrites);
}
}
} // P4MapKit::Material

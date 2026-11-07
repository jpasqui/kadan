// The actual factory's independent depth program contributes OpacityMask to
// joint-default proof. A masked graph additionally checks the cooked F32 clip.
#pragma once
#include "material_source_particle_owner.h"
#include "material_source_particle_lit_bind.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalParticleLitDepth
{
    bool Found = false;
    MaterialBoundOutputs Bound;
    Value Clip;
};
inline MaterialOriginalParticleLitDepth
CaptureOriginalParticleLitDepth(const MaterialOriginalOwnerSource& owner, const FMaterialShaderReference& base,
                                const MaterialOriginalArchiveAccess& archive, MaterialDomainSourceScope scope,
                                const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding = {})
{
    const FMaterialShaderReference* reference = nullptr;
    for (const auto& ref : owner.References)
        if (ref.Frequency == 3 && ref.VertexFactoryHash == base.VertexFactoryHash &&
            ref.TypeHash == 0x925355faf735b81cull)
        {
            reference = &ref;
            break;
        }
    if (!reference)
        return {};
    const auto original = PrepareOriginalMaterialArchiveProgram(owner, *reference, archive, scope);
    MaterialParticleArchive access;
    access.MapIndices = archive.MapIndices;
    access.Payload = archive.Payload;
    access.Frequency = archive.Frequency;
    const auto fixed = owner.Fixed;
    const auto runtime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, original.Program);
    const auto binding = [fixed, runtime, engineBinding](SymbolicArena& arena, BindingContext& context)
    {
        ApplyOriginalMaterialFixedTextures(context, fixed);
        InstallOriginalMaterialRuntimeTextures(arena, context, runtime);
        if (engineBinding)
            engineBinding(arena, context);
    };
    const auto prepared = PlanOriginalParticlePixelRecovery(owner.Map, original.Program, access, owner.User.Settings,
                                                            OriginalMaterialGraphDefaults(owner), binding);
    auto plan = prepared.Source.Recovery;
    MaterialMaskAnchor mask;
    const auto code = ParseMaterialAnchorInstructions(plan.Lines);
    if (!FindMaterialMaskAnchor(code, mask))
        return {};
    const auto component = [](const std::string& comps)
    {
        if (comps.size() != 1 || std::string("xyzw").find(comps.front()) == std::string::npos)
            throw Refused("depth mask does not select one component", "material particle depth component");
        return int(std::string("xyzw").find(comps.front()));
    };
    plan.Stop = mask.line;
    plan.Outputs = {{"OpacityMask", mask.mask.first, {component(mask.mask.second)}}};
    if (!mask.constant_clip)
        plan.Outputs.push_back({"__Clip", mask.clip.first, {component(mask.clip.second)}});
    MaterialOriginalParticleLitDepth result;
    result.Found = true;
    result.Bound = BindMaterialOutputs(plan);
    if (mask.constant_clip)
        result.Clip = mask.clip_value;
    else
    {
        result.Clip = result.Bound.Values.back().at(0);
        result.Bound.Values.pop_back();
        result.Bound.Pins.pop_back();
    }
    return result;
}
inline MaterialBoundOutputs AppendOriginalParticleLitDepth(MaterialBoundOutputs base,
                                                           const MaterialOriginalParticleLitDepth& depth)
{
    if (!depth.Found)
        return base;
    if (!base.Arena || !depth.Bound.Arena || depth.Bound.Values.size() != 1 || depth.Bound.Values.front().size() != 1 ||
        std::find(base.Pins.begin(), base.Pins.end(), "OpacityMask") != base.Pins.end())
        throw Refused("invalid independently captured particle depth pin", "material particle depth inputs");
    MaterialImportMemo imports;
    base.Pins.push_back("OpacityMask");
    base.Values.push_back({ImportMaterialValue(*base.Arena, depth.Bound.Values.front().front(), imports)});
    for (const auto& fact : depth.Bound.AssumedEngineFields)
    {
        const auto old = base.AssumedEngineFields.find(fact.first);
        if (old != base.AssumedEngineFields.end() && old->second != fact.second)
            throw Refused("base and depth engine facts conflict", "material particle depth inputs");
        base.AssumedEngineFields.insert(fact);
    }
    base.CanonicalUnitFields.insert(depth.Bound.CanonicalUnitFields.begin(), depth.Bound.CanonicalUnitFields.end());
    return base;
}
inline double ValidateOriginalParticleLitDepthClip(const MaterialOriginalParticleLitDepth& depth, double cooked)
{
    if (!depth.Found || !IsNumber(depth.Clip))
        throw Refused("masked particle requires an independently numeric depth clip", "material particle depth clip");
    const auto actual = depth.Clip.number();
    if (!std::isfinite(actual) || !std::isfinite(cooked))
        throw Refused("particle opacity clip is not finite", "material particle depth clip");
    const float a = float(actual), b = float(cooked);
    std::uint32_t abits, bbits;
    std::memcpy(&abits, &a, 4);
    std::memcpy(&bbits, &b, 4);
    if (!std::isfinite(a) || !std::isfinite(b) || abits != bbits)
        throw Refused("cooked opacity mask clip disagrees with the depth shader",
                      "settings disagree: opacity mask clip value");
    return actual;
}
}
} // P4MapKit::Material

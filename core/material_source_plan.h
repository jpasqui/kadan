// Structural original-program preparation -> owned native recovery plan.
// Policy dispatch, every complementary program and runtime proof remain separate.
#pragma once
#include "material_prepare.h"
#include "material_engine_hashes.h"
#include "material_factory.h"
#include "material_program.h"
#include "material_decompile.h"
#include "material_sampler_prepare.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalSourcePlan
{
    MaterialRecoveryPlan Recovery;
    std::string Factory;
    std::vector<std::size_t> OriginalLines;
    std::vector<MaterialStorageDeclaration> StorageDeclarations;
    bool FactoryContractInstalled = false;
};
inline MaterialOriginalSourcePlan
PlanOriginalMaterialPixelRecovery(const MaterialPreparedProgram& prepared, const Json& settings,
                                  const Json& defaults = Json::MakeObject(),
                                  const std::function<void(SymbolicArena&, BindingContext&)>& additionalBinding = {})
{
    if (prepared.Reference.Frequency != 3)
        throw Refused("original recovery source is not a pixel program", "material source pixel frequency");
    RequireKind(settings, Json::Object, "material source settings");
    RequireKind(defaults, Json::Object, "material source defaults");
    FiniteTree(settings, "material source settings");
    FiniteTree(defaults, "material source defaults");
    const auto domain = settings.At("domain").Text();
    MaterialOriginalSourcePlan result;
    if (domain == "MD_Surface" || domain == "MD_UI" ||
        (domain == "MD_DeferredDecal" && prepared.Reference.VertexFactoryHash))
    {
        result.Factory = MaterialFactoryName(prepared.Reference.VertexFactoryHash);
        result.FactoryContractInstalled = true;
    }
    else if (domain == "MD_PostProcess" || domain == "MD_LightFunction" || domain == "MD_DeferredDecal")
    {
        if (prepared.Reference.VertexFactoryHash || !prepared.BaseBinding.Varyings.empty())
            throw Refused("global domain program has a mesh factory or custom interpolators",
                          domain == "MD_PostProcess" ? "material source postprocess inputs"
                                                     : "material source global inputs");
    }
    else
        throw Refused("original pixel recovery requires a separately proved domain binding contract: " + domain,
                      "material source domain binding");
    const auto normalized = NormalizeMaterialTraceDeclarations(prepared.Lines);
    result.OriginalLines = normalized.OriginalLines;
    result.StorageDeclarations = normalized.Storage;
    auto& plan = result.Recovery;
    plan.Lines = normalized.Lines;
    plan.Stop = plan.Lines.size();
    plan.Binding = prepared.BaseBinding;
    plan.Settings = settings;
    plan.Defaults = defaults;
    int view = -1;
    bool ambiguous = false;
    for (const auto& buffer : prepared.BaseBinding.Buffers)
        if (buffer.second == "View")
        {
            if (view != -1)
                ambiguous = true;
            view = buffer.first;
        }
    if (!ambiguous && view >= 0 && std::size_t(view) < prepared.Header.layout_hashes.size() &&
        prepared.Header.layout_hashes[std::size_t(view)] == EngineHash::ViewLayout)
        plan.OriginalViewportBuffer = view;
    const auto factory = result.Factory;
    const auto shared = PrepareOriginalMaterialSharedSamplers(prepared);
    plan.ConfigureBinding = [factory, shared, additionalBinding](SymbolicArena& arena, BindingContext& binding)
    {
        if (!factory.empty())
            ConfigureMaterialFactoryBinding(arena, binding, factory);
        ApplyMaterialSharedSamplerContract(binding, shared);
        if (additionalBinding)
            additionalBinding(arena, binding);
    };
    return result;
}
}
} // P4MapKit::Material

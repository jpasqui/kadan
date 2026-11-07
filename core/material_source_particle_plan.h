// Particle preparation consumes canonical native shader refs and original
// archive metadata. The owner selects programs; this stage supplies no packets.
#pragma once
#include "material_source_particle_defaults.h"
#include "material_engine_hashes.h"
#include "material_source_plan.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalParticleSourcePlan
{
    MaterialOriginalSourcePlan Source;
    MaterialParticleSubUVSource Inputs;
    std::vector<std::size_t> FlatDeclarations;
    std::size_t ExpandedLoops = 0;
};
inline MaterialOriginalParticleSourcePlan
PlanOriginalParticlePixelRecovery(const FMaterialShaderMapEnvelope& map, const MaterialPreparedProgram& prepared,
                                  const MaterialParticleArchive& archive, const Json& settings,
                                  const Json& defaults = Json::MakeObject(),
                                  const std::function<void(SymbolicArena&, BindingContext&)>& additionalBinding = {})
{
    MaterialOriginalParticleSourcePlan result;
    result.Inputs = PrepareOriginalParticleSubUVSource(map, prepared, archive);
    result.Source = PlanOriginalMaterialPixelRecovery(prepared, settings, defaults, additionalBinding);
    const auto instructions = PrepareOriginalParticleInstructions(result.Source.Recovery.Lines);
    const auto original = result.Source.OriginalLines;
    result.Source.OriginalLines.clear();
    for (const auto i : instructions.OriginalLines)
        result.Source.OriginalLines.push_back(original.at(i));
    for (const auto i : instructions.FlatDeclarations)
        result.FlatDeclarations.push_back(original.at(i));
    result.ExpandedLoops = instructions.ExpandedLoops;
    result.Source.Recovery.Lines = instructions.Lines;
    result.Source.Recovery.Stop = instructions.Lines.size();
    if (MaterialParticleFactory(result.Inputs.Factory))
    {
        const auto configure = result.Source.Recovery.ConfigureBinding;
        const auto contract = result.Inputs;
        result.Source.Recovery.ConfigureBinding = [configure, contract](SymbolicArena& arena, BindingContext& binding)
        {
            if (configure)
                configure(arena, binding);
            InstallOriginalParticleSourceInterpolants(arena, binding, contract);
        };
    }
    return result;
}
inline std::vector<Value> BindOriginalParticleColor(SymbolicArena& arena, const MaterialRecoveryPlan& plan)
{
    if (plan.Lines.empty() || plan.Lines.size() > 100000 || plan.Stop != plan.Lines.size())
        throw Refused("particle native output requires the bounded complete source program",
                      "material particle native output");
    // Both independently selected native programs share an interned arena for
    // later Lifter recognition. Original arrays still have their own bounds.
    std::shared_ptr<SymbolicArena> shared(&arena, [](SymbolicArena*) {});
    SymbolicMachine machine(plan.Lines, shared, MaterialPlanArrayBounds(plan, arena));
    try
    {
        machine.run(plan.Stop);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material particle symbolic trace");
    }
    const auto output = machine.reg.find("result_color0");
    if (output == machine.reg.end())
        throw Refused("joint particle graph has no native output", "particle joint graph");
    auto context = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(arena, context);
    Binder binder(arena, context);
    std::vector<Value> result;
    for (const auto& component : output->second)
        result.push_back(BindMaterialComponent(binder, component));
    return result;
}
inline MaterialParticleJointFormulas
RecoverOriginalParticleJointSource(SymbolicArena& arena, const FMaterialShaderMapEnvelope& map,
                                   const MaterialPreparedProgram& particle, const MaterialPreparedProgram& local,
                                   const MaterialParticleArchive& archive, const Json& settings, const Json& defaults,
                                   const std::function<void(SymbolicArena&, BindingContext&)>& additionalBinding = {})
{
    if (local.Reference.VertexFactoryHash != EngineHash::LocalVertexFactory || local.Reference.Frequency != 3)
        throw Refused("joint particle graph has no actual native local preview", "particle joint graph");
    const auto particlePlan =
                   PlanOriginalParticlePixelRecovery(map, particle, archive, settings, defaults, additionalBinding),
               localPlan =
                   PlanOriginalParticlePixelRecovery(map, local, archive, settings, defaults, additionalBinding);
    return ResolveOriginalParticleFormulas(arena, BindOriginalParticleColor(arena, particlePlan.Source.Recovery),
                                           BindOriginalParticleColor(arena, localPlan.Source.Recovery),
                                           particlePlan.Inputs.Factory);
}
}
} // P4MapKit::Material

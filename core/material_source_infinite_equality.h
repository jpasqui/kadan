// Bounded original UI source producer using finite integer equality nodes.
// Original associations are reconstructed; compiler/renderer gates stay false.
#pragma once
#include "material_source_complements.h"
#include "material_infinite_equality_plan.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialOriginalComplementRecovery
RecoverOriginalMaterialInfiniteEquality(const MaterialOriginalOwnerSource& owner,
                                        const MaterialOriginalArchiveAccess& archive)
{
    if (owner.User.Cooked.Domain != "MD_UI" || owner.User.Cooked.ShadingModel != "MSM_Unlit" ||
        owner.User.Cooked.BlendMode != "BLEND_Translucent")
        throw Refused("original infinite equality source route is limited to translucent unlit UI",
                      "material infinite equality original mode");
    const auto primary = SelectOriginalMaterialPrimaryProgram(owner);
    auto prepared =
        PrepareOriginalMaterialArchiveProgram(owner, primary, archive, MaterialDomainSourceScope::ReadOnlyExpressions);
    const auto runtime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, prepared.Program);
    const auto fixed = owner.Fixed;
    auto source =
        PlanOriginalMaterialPixelRecovery(prepared.Program, owner.User.Settings, OriginalMaterialGraphDefaults(owner),
                                          [fixed, runtime](SymbolicArena& arena, BindingContext& context)
                                          {
                                              ApplyOriginalMaterialFixedTextures(context, fixed);
                                              InstallOriginalMaterialRuntimeTextures(arena, context, runtime);
                                          });
    const auto facts = InstallOriginalInfiniteEqualityPlan(source.Recovery);
    auto recovered = RecoverUnlitMaterialExpressionGraph(source.Recovery);
    if (!facts->Choices)
        throw Refused("original UI has no exact infinite equality choices in its bound native values",
                      "material infinite equality original inventory");
    MaterialOriginalComplementRecovery result;
    result.Graph = std::move(recovered.Graph);
    result.Programs.push_back(std::move(prepared.Program));
    result.Facts["infinite_equality_choices"] = double(facts->Choices);
    result.Facts["nonfinite_property_transport"] = false;
    return result;
}
}
}

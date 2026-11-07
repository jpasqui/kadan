// Native actual-source expression orchestration, including original complements.
// Renderer/family/production qualification remains explicit and separate.
#pragma once
#include "material_source_producers.h"
namespace P4MapKit
{
namespace Material
{
// One owner's recovery: the steps of MaterialSourceProducers() in order (material_source_producer.h says how a
// table runs).
inline MaterialOriginalOwnerRecovery RecoverOriginalMaterialSourceExpressionGraph(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
    const std::function<void(SymbolicArena&, BindingContext&)>& binding = {},
    const MaterialSurfaceRewrites& rewrites = {})
{
    MaterialSourceDispatch dispatch{
        owner, archive, scope, binding, SelectOriginalMaterialPrimaryProgram(owner), rewrites, {}, Json::MakeObject()};
    RunMaterialSourceProducers(MaterialSourceProducers(), dispatch);
    return std::move(dispatch.Result);
}
}
} // P4MapKit::Material

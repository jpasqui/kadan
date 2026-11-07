// The ordered producer table of an unlit surface program (opaque, translucent or additive) inside
// RecoverOriginalMaterialProgram (material_source_recovery.h). It is the second dispatch point of original-source
// recovery: the owner table (material_source_producers.h) reaches it through every producer that recovers a program,
// so it sits below that table in the include order. Each rule reads the program and its settings.
#pragma once
#include "material_source_producer.h"
#include "material_source_unlit_scene.h"
#include "material_source_unlit_remaining.h"
#include "material_source_unlit_remaining_capture.h"
#include "material_source_data.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
// What an unlit surface program's recovery reads, and the result its producer writes.
struct MaterialUnlitSceneDispatch
{
    const MaterialOriginalUserData& User;
    const MaterialRecoveryPlan& Plan;
    const MaterialPreparedProgram& Program;
    MaterialRecoveryResult Material;
};
inline const std::vector<MaterialSourceProducer<MaterialUnlitSceneDispatch>>& MaterialUnlitSceneProducers()
{
    using D = MaterialUnlitSceneDispatch;
    static const std::vector<MaterialSourceProducer<D>> table = {
        {"capture off", MaterialSourceStage::Produce,
         [](const D& d) { return OriginalCaptureReadingProgram(d.Program, d.User.Settings); },
         [](D& d) { d.Material = RecoverOriginalCaveCaptureOffExpressionGraph(d.Plan, d.Program, true); }},
        {"fog wrapper cut", MaterialSourceStage::Produce,
         [](const D& d) { return OriginalUnlitFogWrapperProgram(d.Program, d.User.Settings); },
         [](D& d) { d.Material = RecoverOriginalSoftBlackExpressionGraph(d.Plan, d.Program, true); }},
        {"unlit scene", MaterialSourceStage::Produce, [](const D&) { return true; },
         [](D& d)
         {
             d.Material = RecoverOriginalUnlitSceneExpressionGraph(d.Plan, d.Program.Reference, d.Program.Header, true);
         }},
    };
    return table;
}
}
} // P4MapKit::Material

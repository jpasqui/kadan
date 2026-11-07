#include "../core/native_custom_backend_opaque.h"
#include <iostream>
using namespace P4MapKit::CustomMaterial;
using namespace P4MapKit::CustomMaterial::NativeBackend;
int main() {
    const auto graph=Json::Parse(R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Opaque","shading_model":"MSM_Unlit","nodes":{"Tint":{"type":"VectorParameter","props":{"ParameterName":"Tint","DefaultValue":[1,0,1,1]}}},"outputs":{"EmissiveColor":["Tint",""]}})");
    if (!OpaqueAuthoringIssue(graph).empty()) return 1;
    const auto uniforms=GraphUniforms(graph,{});
    Emitter e; e.owned_uniforms=true; e.material_parameters=uniforms.bindings; e.material_buffer=5;
    const auto result=GraphCompiler(graph,{},std::move(e)).compile();
    if (result.program.find("LDC.F32X4") == std::string::npos || result.program.find("buf5[0]") == std::string::npos) return 2;
    auto bad=graph; bad["blend_mode"]=Json("BLEND_Translucent"); if (OpaqueAuthoringIssue(bad).empty()) return 3;
    bad=graph; bad["outputs"]["Roughness"]=bad["outputs"]["EmissiveColor"]; if (OpaqueAuthoringIssue(bad).empty()) return 4;
    bad=graph; bad["nodes"]["Tint"]["props"]["ParameterName"]=Json("None"); if (OpaqueAuthoringIssue(bad).empty()) return 5;
    std::cout << "5 opaque graph checks passed\n";
}

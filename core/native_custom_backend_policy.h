#pragma once
// Bounded source-pattern facts from the selected original PS metadata only.
#include "native_custom_backend.h"
#include "material_shader_map.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
enum class OriginalOpaqueDirectionalResponse { SurfaceShadow, SeparateSpecularShadow };
class OpaqueDirectionalSourceProof {
public:
    OriginalOpaqueDirectionalResponse Response() const { return response; }
    const std::string& AssemblySha256() const { return assembly_sha256; }
    unsigned ControlLoads() const { return control_loads; }
private:
    OriginalOpaqueDirectionalResponse response;
    std::string assembly_sha256;
    unsigned control_loads;
    OpaqueDirectionalSourceProof(OriginalOpaqueDirectionalResponse InResponse,std::string InHash,unsigned InLoads)
        :response(InResponse),assembly_sha256(std::move(InHash)),control_loads(InLoads){}
    friend OpaqueDirectionalSourceProof ReadOpaqueDirectionalSourceProof(const Bytes&,const FMaterialShaderReference&);
};
// Ref must be obtained from the selected validated original resource/map join.
// This proves a static uniform-load/operand pattern, not material settings,
// shading-model identity, complete policy equivalence or render admission.
OpaqueDirectionalSourceProof ReadOpaqueDirectionalSourceProof(const Bytes& OriginalPixelPayload,
    const FMaterialShaderReference& OriginalReference);
} } }

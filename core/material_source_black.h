// Fog-only unlit owners: an unlit opaque surface whose program reads no material data, so that what it draws is the
// engine's fog over a colour the compiler folded into constants. They are recovered as every other owner is.
#pragma once
#include "material_source_owner.h"
#include "material_source_particle_subuv.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
// The fog-only settings: an unlit opaque surface with no material uniforms.
inline bool OriginalBlackFogSettings(const MaterialOriginalOwnerSource& owner)
{
    const auto& cooked = owner.User.Cooked;
    return cooked.Domain == "MD_Surface" && cooked.BlendMode == "BLEND_Opaque" && cooked.ShadingModel == "MSM_Unlit" &&
           owner.Map.Uniforms.Scalars.empty() && owner.Map.Uniforms.Vectors.empty();
}
// True when a program reads no material data: no Material buffer and no material texture. What it draws is the
// engine's work (fog, exposure) over a colour the compiler folded into constants.
inline bool MaterialProgramReadsNoMaterialData(const MaterialPreparedProgram& program)
{
    for (const auto& buffer : program.BaseBinding.Buffers)
        if (buffer.second == "Material")
            return false;
    return program.BaseBinding.TextureParameters.empty() && program.BaseBinding.TextureEntries.empty();
}
// An owner of the fog-only settings whose program of record reads no material data. Its graph is the program's
// own, recovered as every other owner's is; the particle producer, which recovers particle inputs, has none to find.
inline bool OriginalBlackFogOwner(const MaterialOriginalOwnerSource& owner, const FMaterialShaderReference& program,
                                  const MaterialOriginalArchiveAccess& archive, MaterialDomainSourceScope scope)
{
    return OriginalBlackFogSettings(owner) &&
           MaterialProgramReadsNoMaterialData(
               PrepareOriginalMaterialArchiveProgram(owner, program, archive, scope).Program);
}
}
} // P4MapKit::Material

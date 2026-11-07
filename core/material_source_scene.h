// Original source scene rewrites use actual constructor layouts and compiled
// resource joins. Metadata availability does not imply renderer access policy.
#pragma once
#include "material_surface.h"
#include "material_scene_native_registry.h"
#include "material_source_scene_cache.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialSurfaceRewrites PrepareOriginalMaterialSceneRewrites(const MaterialPreparedProgram& program,
                                                                    const Json& settings,
                                                                    const MaterialSurfaceRewrites& additional = {})
{
    MaterialSurfaceRewrites result;
    result.SceneAndSun = [program, settings, additional](SymbolicArena& arena, const std::vector<Value>& values)
    {
        bool consumesScene = false;
        for (const auto& value : walk_formulas(values))
            consumesScene |= is(value, "SCENETEX");
        SceneReadFactory factory;
        if (consumesScene)
        {
            const auto resources = BindOriginalMaterialSceneResources(program, settings, true);
            factory = resources.Factory;
        }
        SceneRoles roles;
        auto out = SceneReads(arena, values, factory, roles);
        if (additional.SceneAndSun)
            out = additional.SceneAndSun(arena, out);
        return out;
    };
    return result;
}
}
} // P4MapKit::Material

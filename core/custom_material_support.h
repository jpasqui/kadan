// Versioned authoring admissions. A compiler node being available is separate
// from a render route being proved, and from production export being available.
#pragma once
#include "custom_material_graph.h"
namespace P4MapKit
{
namespace CustomMaterial
{
constexpr unsigned kGraphContractVersion = 1;
constexpr unsigned kRenderBindingVersion = 1;
struct Availability
{
    bool available;
    std::string code, reason;
};
inline Availability NodeAvailability(const std::string& type)
{
    static const std::set<std::string> supported = {"Constant",
                                                    "Constant2Vector",
                                                    "Constant3Vector",
                                                    "Constant4Vector",
                                                    "ScalarParameter",
                                                    "VectorParameter",
                                                    "Time",
                                                    "TextureCoordinate",
                                                    "WorldPosition",
                                                    "VertexNormalWS",
                                                    "PixelNormalWS",
                                                    "CameraVectorWS",
                                                    "TextureSample",
                                                    "TextureSampleParameter2D",
                                                    "TextureObjectParameter",
                                                    "Add",
                                                    "Subtract",
                                                    "Multiply",
                                                    "Divide",
                                                    "Min",
                                                    "Max",
                                                    "Sine",
                                                    "Cosine",
                                                    "Frac",
                                                    "Floor",
                                                    "Ceil",
                                                    "Abs",
                                                    "OneMinus",
                                                    "Saturate",
                                                    "Clamp",
                                                    "LinearInterpolate",
                                                    "Power",
                                                    "DotProduct",
                                                    "Normalize",
                                                    "ComponentMask",
                                                    "AppendVector",
                                                    "Fresnel",
                                                    "SquareRoot",
                                                    "Sign",
                                                    "Distance",
                                                    "CrossProduct",
                                                    "If",
                                                    "Panner",
                                                    "Reroute",
                                                    "Round",
                                                    "Desaturation",
                                                    "Arcsine",
                                                    "Arccosine",
                                                    "Arctangent",
                                                    "Arctangent2",
                                                    "ArcsineFast",
                                                    "ArccosineFast",
                                                    "ArctangentFast",
                                                    "Arctangent2Fast",
                                                    "Tangent",
                                                    "Fmod",
                                                    "Truncate",
                                                    "Logarithm2",
                                                    "Logarithm10",
                                                    "Step",
                                                    "SmoothStep",
                                                    "InverseLinearInterpolate",
                                                    "ConstantBiasScale",
                                                    "SphereMask",
                                                    "Rotator",
                                                    "RotateAboutAxis",
                                                    "BumpOffset",
                                                    "DDX",
                                                    "DDY",
                                                    "DeriveNormalZ",
                                                    "TwoSidedSign",
                                                    "CameraPositionWS",
                                                    "ReflectionVectorWS",
                                                    "ScreenPosition",
                                                    "PixelDepth",
                                                    "ViewSize",
                                                    "VertexTangentWS",
                                                    "DeltaTime",
                                                    "ObjectPositionWS",
                                                    "ObjectRadius",
                                                    "ObjectBounds",
                                                    "ObjectOrientation",
                                                    "ActorPositionWS",
                                                    "StaticSwitch",
                                                    "StaticSwitchParameter",
                                                    "StaticBool",
                                                    "StaticBoolParameter",
                                                    "StaticComponentMaskParameter",
                                                    "FeatureLevelSwitch",
                                                    "QualitySwitch",
                                                    "ShadingPathSwitch",
                                                    "ShaderStageSwitch",
                                                    "GIReplace",
                                                    "LightmassReplace",
                                                    "ShadowReplace",
                                                    "MaterialProxyReplace",
                                                    "RayTracingQualitySwitch",
                                                    "PreviousFrameSwitch",
                                                    "ReflectionCapturePassSwitch",
                                                    "VirtualTextureFeatureSwitch",
                                                    "RuntimeVirtualTextureReplace",
                                                    "MaterialFunctionCall",
                                                    "FunctionInput",
                                                    "FunctionOutput",
                                                    "CollectionParameter",
                                                    "Transform"};
    if (supported.count(type))
        return {true, "available", "Uses the graph compiler; pass and binding checks still apply."};
    if (type == "VertexColor")
        return {false, "missing-varying", "The template vertex shaders do not pass vertex color."};
    if (type == "Custom")
        return {false, "hlsl-compiler-unavailable", "The native custom compiler does not translate arbitrary HLSL."};
    if (type == "TextureObject")
        return {false, "texture-object-pin-unavailable",
                "Set a 2D texture on the sample node; connected texture-object pins are unavailable."};
    if (type == "SceneDepth" || type == "SceneColor" || type == "SceneTexture" || type == "DepthFade")
        return {false, "missing-scene-binding", "The admitted base-pass templates bind no scene textures."};
    if (type.find("MaterialAttributes") != std::string::npos || type == "MaterialAttributeLayers")
        return {false, "material-attributes-unavailable", "Material attribute records are not compiled."};
    if (type == "PerInstanceRandom" || type == "PerInstanceFadeAmount")
        return {false, "missing-instance-varying",
                "The admitted templates pass no per-instance values to custom pixels."};
    return {false, "node-unavailable", "Node " + type + " is unavailable in the native custom material compiler."};
}
struct RenderRequest
{
    unsigned graph_version = kGraphContractVersion, binding_version = kRenderBindingVersion;
    std::string domain = "Surface", blend = "Opaque", shading = "DefaultLit", mesh = "static", lighting = "game",
                origin = "from-scratch";
    bool production = true, two_sided = false, tangent_space_normal = true;
    unsigned texture_count = 0;
    bool custom_texture_list = false;
};
inline Availability RenderAvailability(const RenderRequest& r)
{
    if (r.graph_version != kGraphContractVersion || r.binding_version != kRenderBindingVersion)
        return {false, "contract-version-mismatch",
                "Recapture the material and render bindings with this kit version."};
    if (r.origin != "from-scratch")
        return {false, "game-master-graph-read-only",
                "Recovered game master graphs are read-only; author a new material or edit an instance."};
    if (r.domain != "Surface")
        return {false, "domain-unavailable", "Only Surface custom graphs have an admitted shader route."};
    if (r.blend != "Opaque" && r.blend != "Masked" && !(r.blend == "Translucent" && r.lighting == "game"))
        return {false, "blend-unavailable",
                "The " + r.blend +
                    " blend mode is not available for materials of your own yet. Use Opaque or Translucent."};
    if (r.shading != "DefaultLit" && r.shading != "Unlit")
        return {false, "shading-model-unavailable",
                "Shading model " + r.shading + " has no admitted custom shader tail."};
    if (r.mesh != "static" && r.mesh != "instanced-static")
        return {false, "vertex-factory-unavailable",
                "Mesh route " + r.mesh + " needs its own vertex factory interpolants and pass proof."};
    if (r.blend == "Masked" && r.mesh == "instanced-static")
        return {false, "masked-instance-dither-unavailable",
                "Masked instanced meshes need the original per-instance LOD dither in depth and shadow passes."};
    if (r.texture_count > (r.custom_texture_list ? 25u : r.blend == "Masked" ? 1u : 3u))
        return {false, "texture-slot-limit", "The template sampler slots do not cover this texture list."};
    if (r.lighting == "game")
    {
        // Seen in the game (Cave000, 2026-10-13) and equal to the game's own shaders for every base-pass lighting policy.
        if (r.blend == "Opaque" && r.shading == "DefaultLit" && r.mesh == "static")
            return {
                true, "game-lighting",
                "Uses the game's own lighting, with baked and volumetric light, sky, shadows, local lights, reflections and fog."};
        // Lit and unlit translucent glass on a static mesh, seen in the game (Cave000, 2026-10-13) with the cave behind it.
        if (r.blend == "Translucent" && r.mesh == "static")
            return {
                true, "game-lighting",
                "Uses the game's own translucent lighting and fog. Set the translucency options the material check names."};
        if (r.blend == "Translucent")
            return {
                false, "game-lighting-unproved",
                "Translucent materials of your own on instanced meshes are not shown in the game yet. Put them on a plain static mesh for now."};
        if (r.blend == "Masked")
            return {false, "game-lighting-unproved",
                    "Masked materials of your own are not shown in the game yet. Use Opaque for now."};
        if (r.shading == "Unlit")
            return {false, "game-lighting-unproved",
                    "Unlit opaque materials of your own are not shown in the game yet. Use Default Lit for now."};
        if (r.mesh != "static")
            return {
                false, "game-lighting-unproved",
                "Materials of your own on instanced meshes are not shown in the game yet. Put them on a plain static mesh for now."};
        return {false, "game-lighting-unproved", "This kind of material of your own is not shown in the game yet."};
    }
    if (r.lighting != "diagnostic-sun")
        return {false, "lighting-route-unavailable", "Unknown custom lighting route."};
    if (r.production)
        return {
            false, "diagnostic-route-not-release",
            "The sun and fixed ambient route is for diagnostics; release export requires game lighting qualification."};
    return {
        true, "diagnostic-route",
        "Native graph compilation is available for offline and desktop render diagnostics; this is not release acceptance."};
}
inline Availability GraphAvailability(const Json& graph, const RenderRequest& r,
                                      const std::map<std::string, std::string>& slots = {},
                                      const Json& functions = Json())
{
    auto a = RenderAvailability(r);
    if (!a.available)
        return a;
    // The game lighting route checks its graph in the opaque compiler. The checks below are for the diagnostic tail.
    if (r.lighting != "diagnostic-sun")
        return a;
    auto& outputs = Get(graph, "outputs");
    for (auto& p : outputs.object)
        if (p.second.kind != Json::Null && p.first != "BaseColor" && p.first != "EmissiveColor" &&
            p.first != "Normal" && p.first != "OpacityMask")
            return {false, "output-unavailable",
                    "Output " + p.first + " is not consumed by the diagnostic shader tail."};
    if (r.blend == "Masked" && Get(graph, "clip").kind == Json::Null)
        return {false, "masked-clip-required",
                "Masked materials need a finite clip threshold and depth/shadow mask programs."};
    if (r.blend == "Opaque" && Get(graph, "clip").kind != Json::Null)
        return {false, "blend-graph-mismatch", "An opaque route cannot carry a mask clip."};
    if (r.shading == "Unlit" && Get(outputs, "BaseColor").kind != Json::Null)
        return {false, "unlit-output-mismatch", "Use EmissiveColor for the unlit diagnostic route."};
    if (Flag(graph, "two_sided") != r.two_sided || Flag(graph, "tangent_space_normal", true) != r.tangent_space_normal)
        return {false, "render-setting-mismatch", "The graph and material render settings differ."};
    try
    {
        auto result = CompileGraph(graph, slots, false, functions);
        if (r.blend == "Masked")
            CompileGraph(graph, slots, true, functions);
        (void)result;
    }
    catch (const std::exception& error)
    {
        return {false, "graph-refused", error.what()};
    }
    return a;
}
}
}

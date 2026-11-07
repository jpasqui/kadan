#version 450
// Kadan's depth and shadow depth vertex shaders for static meshes (FLocalVertexFactory), written for Pikmin 4's
// renderer (stage 7 of docs/OWN_SHADER_MAPS.md): what a masked material needs so its depth pass and its
// shadows are cut by its mask. They do what UE 4.26's DepthOnlyVertexShader.usf and ShadowDepthVertexShader.usf do
// there, with the game's buffer offsets: the vertex fetched by index, its tangent frame and texture coordinates (the
// mask's pixel shader may read them), and per mode:
//
//   DEPTH_ONLY        the clip position; the translated world position as a fourth interpolant.
//   SHADOW            the shadow projection (ShadowDepthPass.ProjectionMatrix), clamped to the near plane when the
//                     pass asks, the depth biased by a constant and a slope term (from the light direction and the
//                     vertex normal) and scaled: OutputDepth (non perspective correct). SHADOW_PERSPECTIVE instead
//                     passes the clip depth and the bias to the pixel shader. SHADOW_LOOSE_BIAS adds a per-draw bias
//                     from the loose parameter buffer (slot 4) to the pass's ShadowParams.
//   SHADOW_FOR_GS     for the one-pass point light geometry shader: the translated world position and the normal
//                     passed on, the position unprojected. SHADOW_FOR_GS_NO_POSITION leaves the position unwritten.
// Uniform buffers in the game's slots, declared in that order (the driver numbers them by declaration): View 0,
// Primitive 1, the shadow depth pass 2 (shadow modes), the vertex factory's buffer after them, the loose buffer 4.

layout(location = 0) in vec3 in_position;

layout(std140, binding = 0) uniform View {
    layout(offset = 0) vec4 TranslatedWorldToClip[4];
    layout(offset = 1120) vec4 PreViewTranslation;
} View_;

layout(std140, binding = 1) uniform Primitive {
    layout(offset = 0) vec4 LocalToWorld[4];
    layout(offset = 64) vec4 InvNonUniformScaleAndDeterminantSign;
} Primitive_;

#if defined(SHADOW) || defined(SHADOW_PERSPECTIVE)
#define SHADOW_PASS 1
layout(std140, binding = 2) uniform ShadowDepthPass {
    layout(offset = 304) vec4 ProjectionMatrix[4];
    layout(offset = 368) vec4 ViewMatrix[3];
    layout(offset = 432) vec4 ShadowParams;        // constant bias, slope bias, max slope, inverse max subject depth
    layout(offset = 448) float bClampToNearPlane;
} Pass;
#endif

layout(std140, binding = 3) uniform LocalVF {
    layout(offset = 0) ivec4 VertexFetch_Parameters;
} LocalVF_;

#ifdef SHADOW_LOOSE_BIAS
layout(std140, binding = 4) uniform LooseShadowBias {
    layout(offset = 0) vec4 ShadowParamsOffset;   // added to ShadowParams.xyz
} Loose;
#endif

layout(binding = 0) uniform samplerBuffer VertexFetch_PackedTangentsBuffer;
layout(binding = 1) uniform samplerBuffer VertexFetch_TexCoordBuffer;

layout(location = 0) out vec4 out_texcoord10;   // TangentToWorld0
layout(location = 1) out vec4 out_texcoord11;   // TangentToWorld2, bitangent sign in w
layout(location = 2) out vec4 out_texcoord0;    // texture coordinates
#if defined(DEPTH_ONLY)
layout(location = 3) out vec4 out_texcoord6;    // translated world position, w 1
#elif defined(SHADOW_PERSPECTIVE)
layout(location = 3) out float out_texcoord6;   // shadow depth
layout(location = 4) out float out_texcoord8;   // depth bias
layout(location = 5) out vec3 out_texcoord7;    // translated world position
#elif defined(SHADOW)
layout(location = 3) out float out_texcoord6;   // shadow depth (0: the position holds it)
layout(location = 4) out vec3 out_texcoord7;    // translated world position
#else
layout(location = 3) out vec3 out_texcoord7;    // translated world position
layout(location = 4) out vec4 out_texcoord6;    // translated world position, w 1
layout(location = 5) out vec3 out_texcoord8;    // world normal
#endif

void main() {
    ivec4 p = LocalVF_.VertexFetch_Parameters;
    int VertexId = p.w + gl_VertexID;

    vec4 L0 = Primitive_.LocalToWorld[0], L1 = Primitive_.LocalToWorld[1], L2 = Primitive_.LocalToWorld[2], L3 = Primitive_.LocalToWorld[3];
    vec3 WorldPosition = in_position.x * L0.xyz + in_position.y * L1.xyz + in_position.z * L2.xyz + (L3.xyz + View_.PreViewTranslation.xyz);

    vec4 TangentX = texelFetch(VertexFetch_PackedTangentsBuffer, 2 * VertexId);
    vec4 TangentZ = texelFetch(VertexFetch_PackedTangentsBuffer, 2 * VertexId + 1);
    vec3 TangentY = cross(TangentZ.xyz, TangentX.xyz) * TangentZ.w;
    vec3 TX = cross(TangentY, TangentZ.xyz) * TangentZ.w;
    vec4 s = Primitive_.InvNonUniformScaleAndDeterminantSign;
    vec3 W0 = L0.xyz * s.x, W1 = L1.xyz * s.y, W2 = L2.xyz * s.z;
    vec3 WorldNormal = TangentZ.x * W0 + TangentZ.y * W1 + TangentZ.z * W2;
    out_texcoord10 = vec4(TX.x * W0 + TX.y * W1 + TX.z * W2, 0.0);
    out_texcoord11 = vec4(WorldNormal, TangentZ.w * s.w);

    int NumTexCoords = p.y;
    vec2 uv0 = texelFetch(VertexFetch_TexCoordBuffer, NumTexCoords * VertexId + min(0, NumTexCoords - 1)).xy;
    out_texcoord0 = vec4(uv0, 0.0, 0.0);

#if defined(DEPTH_ONLY)
    gl_Position = WorldPosition.x * View_.TranslatedWorldToClip[0] + WorldPosition.y * View_.TranslatedWorldToClip[1]
                + WorldPosition.z * View_.TranslatedWorldToClip[2] + View_.TranslatedWorldToClip[3];
    out_texcoord6 = vec4(WorldPosition, 1.0);
#elif defined(SHADOW_PASS)
    vec4 Clip = WorldPosition.x * Pass.ProjectionMatrix[0] + WorldPosition.y * Pass.ProjectionMatrix[1]
              + WorldPosition.z * Pass.ProjectionMatrix[2] + Pass.ProjectionMatrix[3];
    if (Pass.bClampToNearPlane > 0.0 && Clip.z < 0.0) {
        Clip.z = 0.000001;
        Clip.w = 1.0;
    }
    vec4 Params = Pass.ShadowParams;
#ifdef SHADOW_LOOSE_BIAS
    Params.xyz += Loose.ShadowParamsOffset.xyz;
#endif
    vec3 LightDirection = vec3(Pass.ViewMatrix[0].z, Pass.ViewMatrix[1].z, Pass.ViewMatrix[2].z);
    float NoL = abs(dot(LightDirection, WorldNormal));
    float Slope = NoL > 0.0 ? sqrt(clamp(1.0 - NoL * NoL, 0.0, 1.0)) / NoL : Params.z;
    Slope = clamp(Slope, 0.0, Params.z);
    float DepthBias = Params.y * Slope + Params.x;
#ifdef SHADOW_PERSPECTIVE
    gl_Position = Clip;
    out_texcoord6 = Clip.z;
    out_texcoord8 = DepthBias;
    out_texcoord7 = WorldPosition;
#else
    Clip.z = (Clip.z * Params.w + DepthBias) * Clip.w;
    gl_Position = Clip;
    out_texcoord6 = 0.0;
    out_texcoord7 = WorldPosition;
#endif
#else
#ifndef SHADOW_FOR_GS_NO_POSITION
    gl_Position = vec4(WorldPosition, 1.0);
#endif
    out_texcoord7 = WorldPosition;
    out_texcoord6 = vec4(WorldPosition, 1.0);
    out_texcoord8 = WorldNormal;
#endif
}

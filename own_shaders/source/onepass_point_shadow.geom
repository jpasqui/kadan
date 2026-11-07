#version 450
// Kadan's one-pass point light shadow geometry shader (stage 7 of docs/OWN_SHADER_MAPS.md): UE 4.26's
// MainOnePassPointLightGS for Pikmin 4's renderer. Each triangle is projected with each of the six cube faces'
// view-projection matrices (the shadow depth pass buffer, slot 0, from offset 464) and sent to that face's layer,
// unless all three vertices are outside the same side of that face's frustum (left, right, bottom, top, and as the
// game's shader does, near and far too). The vertex shader is depth_passes.vert with SHADOW_FOR_GS.

#pragma optionNV(unroll all)

layout(triangles) in;
layout(triangle_strip, max_vertices = 18) out;

layout(std140, binding = 0) uniform ShadowDepthPass {
    layout(offset = 464) vec4 ShadowViewProjectionMatrices[24];   // six matrices, rows
} Pass;

layout(location = 0) in vec4 in_texcoord10[];   // TangentToWorld0
layout(location = 1) in vec4 in_texcoord11[];   // TangentToWorld2
layout(location = 2) in vec4 in_texcoord0[];    // texture coordinates
layout(location = 4) in vec4 in_texcoord6[];    // translated world position, w 1

layout(location = 0) out vec4 out_texcoord10;
layout(location = 1) out vec4 out_texcoord11;
layout(location = 2) out vec4 out_texcoord0;
layout(location = 3) out vec3 out_texcoord7;    // translated world position
layout(location = 4) flat out uint out_hlslcc_layer_index;

void main() {
    for (int face = 0; face < 6; face++) {
        vec4 clip[3];
        for (int i = 0; i < 3; i++) {
            vec4 p = in_texcoord6[i];
            clip[i] = p.x * Pass.ShadowViewProjectionMatrices[4 * face] + p.y * Pass.ShadowViewProjectionMatrices[4 * face + 1]
                    + p.z * Pass.ShadowViewProjectionMatrices[4 * face + 2] + p.w * Pass.ShadowViewProjectionMatrices[4 * face + 3];
        }
        vec4 sides = vec4(1.0);
        vec2 depth = vec2(1.0);
        for (int i = 0; i < 3; i++) {
            sides *= clamp(clip[i].xyxy * vec4(-1.0, -1.0, 1.0, 1.0) - clip[i].w, 0.0, 1.0);
            depth *= clamp(clip[i].zz * vec2(-1.0, 1.0) - clip[i].w, 0.0, 1.0);
        }
        if (!any(notEqual(sides, vec4(0.0))) && !any(notEqual(depth, vec2(0.0)))) {
            for (int i = 0; i < 3; i++) {
                gl_Position = clip[i];
                out_texcoord10 = in_texcoord10[i];
                out_texcoord11 = in_texcoord11[i];
                out_texcoord0 = in_texcoord0[i];
                out_texcoord7 = in_texcoord6[i].xyz;
                gl_Layer = face;
                out_hlslcc_layer_index = uint(face);
                EmitVertex();
            }
            EndPrimitive();
        }
    }
}

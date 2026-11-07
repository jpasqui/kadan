#version 450
// Kadan's base pass vertex shader for static meshes (FLocalVertexFactory), written for Pikmin 4's renderer
// (stage 3 of docs/OWN_SHADER_MAPS.md). It does what UE 4.26's BasePassVertexShader.usf does there:
// the vertex fetched by index, its tangent frame, its clip position, and the vertex fog (height fog with the sky
// atmosphere's contribution, then the camera aerial perspective). Buffer offsets are the game's (its executable's
// struct registration and its own shaders' reads), not stock UE's.
//
// Defines: NUM_TEXCOORDS (1 or 2: the texture coordinates the material reads), LIGHTMAP_HQ (the lightmap
// policies: the lightmap coordinates as one more interpolant), DF_SHADOWS (with LIGHTMAP_HQ, the distance field
// shadow policy: the shadow map coordinates in that interpolant's zw), TRANSLUCENT (a translucent material's base pass: no
// vertex fog, since its pixel shader fogs per pixel, and no pass buffer, so the vertex factory's buffer is in slot 2
// and the lightmap's in 3, as the game's translucent base pass vertex shaders bind them), TRANSLUCENT_VERTEX_FOG (a
// translucent material fogged per vertex, UE's default: the vertex fog as in the opaque pass, from the translucent pass
// buffer in slot 0, with the View in 1, the Primitive in 2 and the vertex factory's buffer in 3). The NVIDIA driver
// numbers uniform buffers in declaration order whatever their binding, so build_vertex.py moves them to those slots.
// INSTANCED (stage 7): the instanced static mesh vertex factory (UE 4.26's LocalVertexFactory with USE_INSTANCING):
// each instance's transform rows and origin fetched by instance index (the draw's instance plus the loose instance
// offset), the tangent frame through the instance's normalized rows, the per-instance parameters (random, distance
// fade, show or hide, dithered LOD fade) as one more interpolant (color1, after the tangent frame), the position
// zeroed for a hidden instance, and with a lightmap the instance's lightmap coordinate bias. The loose instancing
// parameters (fade and dithered LOD distances) and the instance offset come after the other uniform buffers.

#ifndef NUM_TEXCOORDS
#define NUM_TEXCOORDS 1
#endif

#ifdef INSTANCED
layout(location = 0) in vec4 in_position;
#else
layout(location = 0) in vec3 in_position;
#endif

layout(std140, binding = 0) uniform View {
    layout(offset = 0) vec4 TranslatedWorldToClip[4];
    layout(offset = 960) vec4 ViewForward;
    layout(offset = 1072) vec4 WorldCameraOrigin;
    layout(offset = 1088) vec4 TranslatedWorldCameraOrigin;
    layout(offset = 1120) vec4 PreViewTranslation;
    layout(offset = 2184) float OneOverPreExposure;
    layout(offset = 3072) vec4 AtmosphereLightDirection[2];
    layout(offset = 3136) vec4 AtmosphereLightColorGlobalPostTransmittance[2];
    layout(offset = 3364) float SkyAtmosphereHeightFogContribution;
    layout(offset = 3376) vec4 SkyAtmosphereCameraAerialPerspectiveVolumeSizeAndInvSize;
    layout(offset = 3392) float SkyAtmosphereAerialPerspectiveStartDepthKm;
    layout(offset = 3396) float SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolution;
    layout(offset = 3400) float SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolutionInv;
    layout(offset = 3408) float SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKmInv;
    layout(offset = 3412) float SkyAtmosphereApplyCameraAerialPerspectiveVolume;
    layout(offset = 3440) float RealTimeReflectionCapture;
    layout(offset = 3864) float VolumetricFogMaxDistance;
} View_;

layout(std140, binding = 1) uniform Primitive {
    layout(offset = 0) vec4 LocalToWorld[4];
    layout(offset = 64) vec4 InvNonUniformScaleAndDeterminantSign;
} Primitive_;

#ifdef TRANSLUCENT
#define LOCAL_VF_SLOT 2
#define LIGHTMAP_SLOT 3
#else
#define LOCAL_VF_SLOT 3
#define LIGHTMAP_SLOT 4
// The base pass buffer's fog parameters (FFogUniformParameters inside the pass's shared parameters).
layout(std140, binding = 2) uniform BasePass {
    layout(offset = 1712) vec4 ExponentialFogParameters;
    layout(offset = 1728) vec4 ExponentialFogParameters2;
    layout(offset = 1744) vec4 ExponentialFogColorParameter;
    layout(offset = 1760) vec4 ExponentialFogParameters3;
    layout(offset = 1776) vec4 InscatteringLightDirection;
    layout(offset = 1792) vec4 DirectionalInscatteringColor;
    layout(offset = 1808) vec2 SinCosInscatteringColorCubemapRotation;
    layout(offset = 1824) vec4 FogInscatteringTextureParameters;
} Fog;
#endif

// VertexFetch_Parameters: color index mask, number of texture coordinates, lightmap coordinate index, first vertex.
layout(std140, binding = LOCAL_VF_SLOT) uniform LocalVF {
    layout(offset = 0) ivec4 VertexFetch_Parameters;
} LocalVF_;

#ifdef LIGHTMAP_HQ
layout(std140, binding = LIGHTMAP_SLOT) uniform PrecomputedLightingBuffer {
    layout(offset = 32) vec4 LightMapCoordinateScaleBias;
    layout(offset = 48) vec4 ShadowMapCoordinateScaleBias;
} Lightmap;
#endif

#ifdef INSTANCED
// The loose parameters: the instance offset (a uint), then the instancing parameters.
layout(std140, binding = 5) uniform InstanceOffsetParameters {
    layout(offset = 0) uint InstanceOffset;
} InstanceOffset_;
layout(std140, binding = 6) uniform InstancingParameters {
    layout(offset = 0) vec4 InstancingViewZCompareZero;     // w: the random LOD transition scale
    layout(offset = 16) vec4 InstancingViewZCompareOne;
    layout(offset = 32) vec4 InstancingViewZConstant;
    layout(offset = 48) vec4 InstancingWorldViewOriginZero;  // w: weight of the first transition
    layout(offset = 64) vec4 InstancingWorldViewOriginOne;   // w: weight of the second
    layout(offset = 80) vec4 InstancingOffset;
    layout(offset = 96) vec4 InstancingFadeOutParams;        // start, inverse length, shown when selected, when not
} Instancing;
#endif

layout(binding = 0) uniform samplerBuffer VertexFetch_PackedTangentsBuffer;
layout(binding = 1) uniform samplerBuffer VertexFetch_TexCoordBuffer;
#ifndef TRANSLUCENT
layout(binding = 2) uniform samplerCube FogInscatteringColorCubemap;
layout(binding = 3) uniform sampler3D CameraAerialPerspectiveVolume;
layout(binding = 4) uniform sampler2D DistantSkyLightLutTexture;
#endif
#ifdef INSTANCED
layout(binding = 5) uniform samplerBuffer InstanceTransformBuffer;      // three rows per instance
layout(binding = 6) uniform samplerBuffer InstanceOriginBuffer;         // w: the instance's random value
#ifdef LIGHTMAP_HQ
layout(binding = 7) uniform samplerBuffer InstanceLightmapBuffer;       // lightmap and shadow map coordinate bias
#endif
#define OUT_UV 3
#else
#define OUT_UV 2
#endif

layout(location = 0) out vec4 out_texcoord10;   // TangentToWorld0
layout(location = 1) out vec4 out_texcoord11;   // TangentToWorld2, bitangent sign in w
#ifdef INSTANCED
layout(location = 2) out vec4 out_color1;       // per-instance parameters
#endif
layout(location = OUT_UV) out vec4 out_texcoord0;    // texture coordinates
#ifdef LIGHTMAP_HQ
layout(location = OUT_UV + 1) out vec4 out_texcoord4;    // lightmap coordinates
#ifndef TRANSLUCENT
layout(location = OUT_UV + 2) out vec4 out_texcoord7;    // vertex fog
#endif
#else
#ifndef TRANSLUCENT
layout(location = OUT_UV + 1) out vec4 out_texcoord7;    // vertex fog
#endif
#endif

#ifndef TRANSLUCENT
const float PI = 3.1415926535897932;

vec3 ComputeInscatteringColor(vec3 CameraToReceiver, float CameraToReceiverLength) {
    vec3 Inscattering = Fog.ExponentialFogColorParameter.xyz;
    if (Fog.ExponentialFogParameters3.z > 0.0) {
        float FadeAlpha = clamp(CameraToReceiverLength * Fog.FogInscatteringTextureParameters.x + Fog.FogInscatteringTextureParameters.y, 0.0, 1.0);
        vec3 Lookup = CameraToReceiver;
        vec2 sc = Fog.SinCosInscatteringColorCubemapRotation;
        Lookup.xy = vec2(dot(Lookup.xy, vec2(sc.y, -sc.x)), dot(Lookup.xy, sc));
        vec3 Directional = textureLod(FogInscatteringColorCubemap, Lookup, 0.0).xyz;
        vec3 NonDirectional = textureLod(FogInscatteringColorCubemap, Lookup, Fog.FogInscatteringTextureParameters.z).xyz;
        Inscattering *= mix(NonDirectional, Directional, FadeAlpha);
    }
    Inscattering += View_.SkyAtmosphereHeightFogContribution * textureLod(DistantSkyLightLutTexture, vec2(0.5), 0.0).rgb;
    return Inscattering;
}

float CalculateLineIntegralShared(float FogHeightFalloff, float RayDirectionZ, float RayOriginTerms) {
    float Falloff = max(-127.0, FogHeightFalloff * RayDirectionZ);
    float LineIntegral = (1.0 - exp2(-Falloff)) / Falloff;
    float LineIntegralTaylor = log(2.0) - (0.5 * log(2.0) * log(2.0)) * Falloff;
    return RayOriginTerms * (abs(Falloff) > 0.01 ? LineIntegral : LineIntegralTaylor);
}

vec4 GetExponentialHeightFog(vec3 WorldPositionRelativeToCamera, float ExcludeDistance) {
    float MinFogOpacity = Fog.ExponentialFogColorParameter.w;
    float MaxWorldObserverHeight = Fog.ExponentialFogParameters.z;
    vec3 WorldObserverOrigin = vec3(View_.WorldCameraOrigin.xy, min(View_.WorldCameraOrigin.z, MaxWorldObserverHeight));
    vec3 CameraToReceiver = WorldPositionRelativeToCamera;
    CameraToReceiver.z += View_.WorldCameraOrigin.z - WorldObserverOrigin.z;
    float LengthSqr = dot(CameraToReceiver, CameraToReceiver);
    float LengthInv = inversesqrt(LengthSqr);
    float CameraToReceiverLength = LengthSqr * LengthInv;
    vec3 CameraToReceiverNormalized = CameraToReceiver * LengthInv;
    float RayOriginTerms = Fog.ExponentialFogParameters.x;
    float RayOriginTermsSecond = Fog.ExponentialFogParameters2.x;
    float RayLength = CameraToReceiverLength;
    float RayDirectionZ = CameraToReceiver.z;
    ExcludeDistance = max(ExcludeDistance, Fog.ExponentialFogParameters.w);
    if (ExcludeDistance > 0.0) {
        float ExcludeIntersectionTime = ExcludeDistance * LengthInv;
        float CameraToExclusionIntersectionZ = ExcludeIntersectionTime * CameraToReceiver.z;
        float ExclusionIntersectionZ = WorldObserverOrigin.z + CameraToExclusionIntersectionZ;
        float ExclusionIntersectionToReceiverZ = CameraToReceiver.z - CameraToExclusionIntersectionZ;
        RayLength = (1.0 - ExcludeIntersectionTime) * CameraToReceiverLength;
        RayDirectionZ = ExclusionIntersectionToReceiverZ;
        float Exponent = max(-127.0, Fog.ExponentialFogParameters.y * (ExclusionIntersectionZ - Fog.ExponentialFogParameters3.y));
        RayOriginTerms = Fog.ExponentialFogParameters3.x * exp2(-Exponent);
        float ExponentSecond = max(-127.0, Fog.ExponentialFogParameters2.y * (ExclusionIntersectionZ - Fog.ExponentialFogParameters2.w));
        RayOriginTermsSecond = Fog.ExponentialFogParameters2.z * exp2(-ExponentSecond);
    }
    float LineIntegralShared = CalculateLineIntegralShared(Fog.ExponentialFogParameters.y, RayDirectionZ, RayOriginTerms)
                             + CalculateLineIntegralShared(Fog.ExponentialFogParameters2.y, RayDirectionZ, RayOriginTermsSecond);
    float LineIntegral = LineIntegralShared * RayLength;
    vec3 InscatteringColor = ComputeInscatteringColor(CameraToReceiver, CameraToReceiverLength);
    vec3 DirectionalInscattering = vec3(0.0);
    if (Fog.InscatteringLightDirection.w >= 0.0 && Fog.ExponentialFogParameters3.z == 0.0) {
        const float UniformPhaseFunction = 1.0 / (4.0 * PI);
        vec3 Color = Fog.DirectionalInscatteringColor.xyz + View_.AtmosphereLightColorGlobalPostTransmittance[0].rgb * UniformPhaseFunction;
        vec3 Light = Color * pow(clamp(dot(CameraToReceiverNormalized, View_.AtmosphereLightDirection[0].xyz), 0.0, 1.0), Fog.DirectionalInscatteringColor.w);
        if (View_.AtmosphereLightColorGlobalPostTransmittance[1].a > 0.0) {
            Color = Fog.DirectionalInscatteringColor.xyz + View_.AtmosphereLightColorGlobalPostTransmittance[1].rgb * UniformPhaseFunction;
            Light += Color * pow(clamp(dot(CameraToReceiverNormalized, View_.AtmosphereLightDirection[1].xyz), 0.0, 1.0), Fog.DirectionalInscatteringColor.w);
        }
        Light *= View_.SkyAtmosphereHeightFogContribution;
        float DirLineIntegral = LineIntegralShared * max(RayLength - Fog.InscatteringLightDirection.w, 0.0);
        float DirFogFactor = clamp(exp2(-DirLineIntegral), 0.0, 1.0);
        DirectionalInscattering = Light * (1.0 - DirFogFactor);
    }
    float ExpFogFactor = max(clamp(exp2(-LineIntegral), 0.0, 1.0), MinFogOpacity);
    if (Fog.ExponentialFogParameters3.w > 0.0 && CameraToReceiverLength > Fog.ExponentialFogParameters3.w) {
        ExpFogFactor = 1.0;
        DirectionalInscattering = vec3(0.0);
    }
    return vec4(InscatteringColor * (1.0 - ExpFogFactor) + DirectionalInscattering, ExpFogFactor);
}

vec4 CalculateHeightFog(vec3 WorldPositionRelativeToCamera) {
    float InvCosAngle = length(WorldPositionRelativeToCamera) / dot(WorldPositionRelativeToCamera, View_.ViewForward.xyz);
    float ExcludeDistance = max(View_.VolumetricFogMaxDistance * InvCosAngle, 0.0);
    return GetExponentialHeightFog(WorldPositionRelativeToCamera, ExcludeDistance);
}

// GetAerialPerspectiveLuminanceTransmittanceWithFogOver (SkyAtmosphereCommon.ush), positions in sky units (km).
vec4 AerialPerspectiveOverFog(vec4 NDC, vec3 SampledWorldPos, vec3 CameraWorldPos, vec4 FogToApplyOver) {
    vec2 ScreenUv = (NDC.xy / NDC.ww) * vec2(0.5, -0.5) + 0.5;
    float tDepth = max(0.0, length(SampledWorldPos - CameraWorldPos) - View_.SkyAtmosphereAerialPerspectiveStartDepthKm);
    float LinearSlice = tDepth * View_.SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKmInv;
    float LinearW = LinearSlice * View_.SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolutionInv;
    float NonLinW = sqrt(LinearW);
    float NonLinSlice = NonLinW * View_.SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolution;
    float Weight = 1.0;
    if (NonLinSlice < 0.70710678118654752) {
        Weight = clamp(NonLinSlice * NonLinSlice * 2.0, 0.0, 1.0);
    }
    if (View_.RealTimeReflectionCapture != 0.0) {
        vec3 WorldDir = normalize(SampledWorldPos - CameraWorldPos);
        float SinPhi = WorldDir.z;
        float CosPhi = sqrt(1.0 - SinPhi * SinPhi);
        ScreenUv.y = WorldDir.z * 0.5 + 0.5;
        float CosTheta = WorldDir.x / CosPhi;
        float SinTheta = WorldDir.y / CosPhi;
        float Theta = acos(CosTheta);
        Theta = SinTheta < 0.0 ? (PI - Theta) + PI : Theta;
        ScreenUv.x = Theta / (2.0 * PI);
        vec4 Size = View_.SkyAtmosphereCameraAerialPerspectiveVolumeSizeAndInvSize;
        ScreenUv = (ScreenUv + 0.5 * Size.zw) * (Size.xy / (Size.xy + 1.0));   // FromUnitToSubUvs
    }
    vec4 AP = textureLod(CameraAerialPerspectiveVolume, vec3(ScreenUv, NonLinW), 0.0);
    AP.rgb *= Weight;
    AP.a = 1.0 - (Weight * (1.0 - AP.a));
    AP.rgb *= View_.OneOverPreExposure;
    return vec4(FogToApplyOver.rgb + AP.rgb * FogToApplyOver.a, FogToApplyOver.a * AP.a);   // the other fog over the aerial perspective
}

#endif

void main() {
    ivec4 p = LocalVF_.VertexFetch_Parameters;
    int VertexId = p.w + gl_VertexID;

    vec4 L0 = Primitive_.LocalToWorld[0], L1 = Primitive_.LocalToWorld[1], L2 = Primitive_.LocalToWorld[2], L3 = Primitive_.LocalToWorld[3];
    vec4 TangentX = texelFetch(VertexFetch_PackedTangentsBuffer, 2 * VertexId);
    vec4 TangentZ = texelFetch(VertexFetch_PackedTangentsBuffer, 2 * VertexId + 1);
    vec3 TangentY = cross(TangentZ.xyz, TangentX.xyz) * TangentZ.w;
    vec3 TX = cross(TangentY, TangentZ.xyz) * TangentZ.w;
    vec4 s = Primitive_.InvNonUniformScaleAndDeterminantSign;
#ifdef INSTANCED
    // The instance: its transform rows (w of the first: the selection flag, times 256) and origin (w: random).
    int InstanceId = int(InstanceOffset_.InstanceOffset + uint(gl_InstanceID));
    vec4 I0 = texelFetch(InstanceTransformBuffer, 3 * InstanceId);
    vec4 I1 = texelFetch(InstanceTransformBuffer, 3 * InstanceId + 1);
    vec4 I2 = texelFetch(InstanceTransformBuffer, 3 * InstanceId + 2);
    vec4 Origin = texelFetch(InstanceOriginBuffer, InstanceId);
    // Tangent frame through the instance's rows taken to world and normalized.
    vec3 W0 = normalize(I0.x * L0.xyz + I0.y * L1.xyz + I0.z * L2.xyz);
    vec3 W1 = normalize(I1.x * L0.xyz + I1.y * L1.xyz + I1.z * L2.xyz);
    vec3 W2 = normalize(I2.x * L0.xyz + I2.y * L1.xyz + I2.z * L2.xyz);
    out_texcoord10 = vec4(TX.x * W0 + TX.y * W1 + TX.z * W2, 0.0);
    out_texcoord11 = vec4(TangentZ.x * W0 + TangentZ.y * W1 + TangentZ.z * W2, TangentZ.w * s.w);
    // Per-instance parameters: random, distance fade, shown (0 hides), dithered LOD fade.
    vec3 Local = Origin.xyz + Instancing.InstancingOffset.xyz;
    vec3 InstanceLocation = Local.x * L0.xyz + Local.y * L1.xyz + Local.z * L2.xyz + L3.xyz;
    float RandomLOD = Instancing.InstancingViewZCompareZero.w * Origin.w;
    float ViewZZero = length(InstanceLocation - Instancing.InstancingWorldViewOriginZero.xyz) + RandomLOD;
    float ViewZOne = length(InstanceLocation - Instancing.InstancingWorldViewOriginOne.xyz) + RandomLOD;
    float Dither = dot(vec3(greaterThan(vec3(ViewZZero), Instancing.InstancingViewZCompareZero.xyz)), Instancing.InstancingViewZConstant.xyz)
                     * Instancing.InstancingWorldViewOriginZero.w
                 + dot(vec3(greaterThan(vec3(ViewZOne), Instancing.InstancingViewZCompareOne.xyz)), Instancing.InstancingViewZConstant.xyz)
                     * Instancing.InstancingWorldViewOriginOne.w;
    float Selected = trunc(I0.w * 0.00390625);
    vec4 Fade = Instancing.InstancingFadeOutParams;
    float Shown = (Fade.z * Selected + (Fade.w - Fade.w * Selected)) * float(abs(Dither) < 0.999);
    float Distance = length(InstanceLocation + View_.PreViewTranslation.xyz);
    out_color1 = vec4(Origin.w, 1.0 - clamp((Distance - Fade.x) * Fade.y, 0.0, 1.0), Shown, Dither);
    // Translated world position, through the instance; a hidden instance's position is zero.
    vec3 InstancePosition = in_position.x * I0.xyz + in_position.y * I1.xyz + in_position.z * I2.xyz + in_position.w * Origin.xyz;
    vec4 Translated = Shown * vec4(InstancePosition.x * L0.xyz + InstancePosition.y * L1.xyz + InstancePosition.z * L2.xyz
                                   + (L3.xyz + View_.PreViewTranslation.xyz), 1.0);
    vec3 WorldPosition = Translated.xyz;
    vec4 Clip = Translated.x * View_.TranslatedWorldToClip[0] + Translated.y * View_.TranslatedWorldToClip[1]
              + Translated.z * View_.TranslatedWorldToClip[2] + Translated.w * View_.TranslatedWorldToClip[3];
#else
    // Translated world position.
    vec3 WorldPosition = in_position.x * L0.xyz + in_position.y * L1.xyz + in_position.z * L2.xyz + (L3.xyz + View_.PreViewTranslation.xyz);
    vec4 Clip = WorldPosition.x * View_.TranslatedWorldToClip[0] + WorldPosition.y * View_.TranslatedWorldToClip[1]
              + WorldPosition.z * View_.TranslatedWorldToClip[2] + View_.TranslatedWorldToClip[3];

    // Tangent frame: TangentX and TangentZ (bitangent sign in w), made orthogonal, to world without scale.
    vec3 W0 = L0.xyz * s.x, W1 = L1.xyz * s.y, W2 = L2.xyz * s.z;
    out_texcoord10 = vec4(TX.x * W0 + TX.y * W1 + TX.z * W2, 0.0);
    out_texcoord11 = vec4(TangentZ.x * W0 + TangentZ.y * W1 + TangentZ.z * W2, TangentZ.w * s.w);
#endif
    gl_Position = Clip;

    // Texture coordinates: coordinate set i of this vertex, the last one for a set the mesh does not have.
    int NumTexCoords = p.y;
    vec2 uv0 = texelFetch(VertexFetch_TexCoordBuffer, NumTexCoords * VertexId + min(0, NumTexCoords - 1)).xy;
#if NUM_TEXCOORDS > 1
    vec2 uv1 = texelFetch(VertexFetch_TexCoordBuffer, NumTexCoords * VertexId + min(1, NumTexCoords - 1)).xy;
    out_texcoord0 = vec4(uv0, uv1);
#else
    out_texcoord0 = vec4(uv0, 0.0, 0.0);
#endif
#ifdef LIGHTMAP_HQ
    vec2 lm = texelFetch(VertexFetch_TexCoordBuffer, NumTexCoords * VertexId + p.z).xy;
    vec4 sb = Lightmap.LightMapCoordinateScaleBias;
    vec4 ss = Lightmap.ShadowMapCoordinateScaleBias;
#ifdef INSTANCED
    vec4 bias = texelFetch(InstanceLightmapBuffer, InstanceId);    // the instance's lightmap and shadow map bias
    sb.zw = bias.xy;
    ss.zw = bias.zw;
#endif
    out_texcoord4 = vec4(lm * sb.xy + sb.zw, 0.0, 0.0);
#ifdef DF_SHADOWS
    out_texcoord4.zw = lm * ss.xy + ss.zw;
#endif
#endif

#ifndef TRANSLUCENT
    // Vertex fog.
    vec4 VertexFog = CalculateHeightFog(WorldPosition - View_.TranslatedWorldCameraOrigin.xyz);
    if (View_.SkyAtmosphereApplyCameraAerialPerspectiveVolume > 0.0) {
        VertexFog = AerialPerspectiveOverFog(Clip, WorldPosition * 0.00001, View_.TranslatedWorldCameraOrigin.xyz * 0.00001, VertexFog);
    }
    out_texcoord7 = VertexFog;
#endif
}

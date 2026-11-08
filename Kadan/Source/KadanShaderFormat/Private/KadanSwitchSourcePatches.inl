// The game's changes to UE 4.26's shader source, as patches of a job's preprocessed source (KadanShaderFormat.cpp,
// ApplySourcePatches; the format is described there). Each one is read from the game's own programs and checked by
// recomp/graphics/nvasm/glsl_probe/switch_source_loop.py and switch_terms.py against them (stage 8.5 of
// tools/navigation/OWN_SHADER_MAPS.md). The game's own View members are declared here by their registered names; the
// format's front end reads them at the game's offsets (own_game_layouts.h). The text is in pieces, each under MSVC's
// string literal limit, split before a patch's "@@" line, and given as an array (the format joins them): one
// concatenated literal would pass MSVC's limit for a whole string.
TEXT(R"KSP(
@@ the game's own View members, and the C4 shadow controls the base pass computes once
--- find
cbuffer View
{
+++ replace
#ifndef KADAN_C4ShaderParam7
#define KADAN_C4ShaderParam7 1
#endif
#ifndef KADAN_C4ShaderParam0
#define KADAN_C4ShaderParam0 1
#endif
#ifndef KADAN_bC4MaterialParam0
#define KADAN_bC4MaterialParam0 0
#endif
#ifndef KADAN_bC4MaterialParam10
#define KADAN_bC4MaterialParam10 0
#endif
#ifndef KADAN_TLM
#define KADAN_TLM 4
#endif
#ifndef KADAN_C4ShaderParam1
#define KADAN_C4ShaderParam1 1
#endif
#ifndef KADAN_C4ShaderParam5
#define KADAN_C4ShaderParam5 1
#endif
#ifndef KADAN_C4TranslucencyLightingMode
#define KADAN_C4TranslucencyLightingMode 0
#endif
#ifndef KADAN_C4MaterialDrawOrder
#define KADAN_C4MaterialDrawOrder 0
#endif
#ifndef KADAN_GROUNDSOFT_ALPHA
#define KADAN_GROUNDSOFT_ALPHA 0
#endif
static float C4ReflectionOcclusion = 1;
static float C4DirectionalScale = 1;
static float C4LocalLightScale = 1;
static float C4MaterialAO = 1;
static float3 C4Darkening = 1;
// The shading models the game lights through its foliage code (transmission, the occlusion that darkens the diffuse
// colour, no C4 ambient): subsurface, preintegrated skin, clear coat and two-sided foliage.
bool C4Transmissive(uint ShadingModelID) { return ShadingModelID == 2 || ShadingModelID == 3 || ShadingModelID == 4 || ShadingModelID == 6; }
cbuffer View
{
	float3 View_C4AmbientLightColor;
	float4 View_C4LightParam;
	float4 View_C4LightParam1;
	float4 View_C4LightParam2;
	uint4 View_C4LightParam6;
	float3 View_C4IndirectLightingSpecularColorScale;
	float3 View_C4IndirectLightingVolumetricLightmapColorScale;
	float3 View_C4SubsurfaceVolumetricLightmapColorScale;
	// The game's XluCut: the global clipping plane and its sign for translucent materials (read by the material's code).
	float4 View_C4GlobalClippingPlane;
	float View_C4XluGlobalClippingPlaneSign;
@@ forward screen UV: clamped to the buffer, no MSAA nearest-depth search requires NearestResolvedDepthScreenUV = CalculateNearestResolvedDepthScreenUV(
--- find
			NearestResolvedDepthScreenUV = CalculateNearestResolvedDepthScreenUV(ScreenUV, MaterialParameters.ScreenPosition.w);
+++ replace
			NearestResolvedDepthScreenUV = max(View_BufferBilinearUVMinMax.xy, min(View_BufferBilinearUVMinMax.zw, ScreenUV));
@@ reflection captures: the game's own specular scale requires ImageBasedReflections.rgb *= ComputeMixingWeight(
--- find
	ImageBasedReflections.rgb *= View_IndirectLightingColorScale;
	CompositedAverageBrightness.x *= Luminance( View_IndirectLightingColorScale );
+++ replace
	ImageBasedReflections.rgb *= View_C4IndirectLightingSpecularColorScale;
	CompositedAverageBrightness.x *= Luminance( View_C4IndirectLightingSpecularColorScale );
@@ C4 ambient: the game's ambient light, tinted by the base color, after the indirect diffuse requires DiffuseColor += (DiffuseIndirectLighting * DiffuseColorForIndirect unless float3 DualBlendColorMul = 1.0f;
--- find
	Color += DiffuseColor;
+++ replace
	Color += DiffuseColor;
	if (!C4Transmissive(GBuffer.ShadingModelID))   // none on the transmissive shading models
		Color += BaseColor * (View_C4AmbientLightColor - Metallic * View_C4AmbientLightColor) * AOMultiBounce(BaseColor.g, MaterialAO);
@@ C4 ambient, thin translucent: the same ambient light in the diffuse colour the thin translucent model adds (UE's thin translucent path keeps the diffuse colour apart and never adds it to Color) requires float3 DualBlendColorMul = 1.0f;
--- find
	float3 DualBlendColorMul = 1.0f;
+++ replace
	float3 DualBlendColorMul = 1.0f;
	DiffuseColor += BaseColor * (View_C4AmbientLightColor - Metallic * View_C4AmbientLightColor) * AOMultiBounce(BaseColor.g, MaterialAO);
@@ decals: the mask is a float texture
--- find
Texture2D<uint> OpaqueBasePass_DBufferRenderMask;
+++ replace
Texture2D OpaqueBasePass_DBufferRenderMask;
)KSP"),
TEXT(R"KSP(@@ decals: the game's mask, modes and buffers requires DBufferMask = GetDBufferMask(
--- find
			DBufferMask = GetDBufferMask(In.SvPosition.xy, OpaqueBasePass_DBufferRenderMask);

			if (DBufferMask)
			{
				float2 NDC = MaterialParameters.ScreenPosition.xy / MaterialParameters.ScreenPosition.w;
				float2 ScreenUV = NDC * View_ScreenPositionScaleBias.xy + View_ScreenPositionScaleBias.wz;
				FDBufferData DBufferData = GetDBufferData(ScreenUV, DBufferMask);

				ApplyDBufferData(DBufferData, MaterialParameters.WorldNormal, SubsurfaceColor, Roughness, BaseColor, Metallic, Specular);
				DBufferOpacity = (DBufferData.ColorOpacity + DBufferData.NormalOpacity + DBufferData.RoughnessOpacity) * (1.0f / 3.0f);
			}
+++ replace
			// The game's decals: a float mask (flag bits in red and alpha, the blend mode in alpha), colour and
			// roughness buffers by flag, a wet-look mode 2, multiply mode 1, no decal normal.
			float4 C4DecalMask = OpaqueBasePass_DBufferRenderMask.Load(int3(int2(In.SvPosition.xy), 0));
			uint C4DecalBits = (uint)(C4DecalMask.r * 255 + C4DecalMask.a * 4080);
			if (C4DecalBits != 0)
			{
				float2 NDC = MaterialParameters.ScreenPosition.xy / MaterialParameters.ScreenPosition.w;
				float2 ScreenUV = NDC * View_ScreenPositionScaleBias.xy + View_ScreenPositionScaleBias.wz;
				ScreenUV = max(View_BufferBilinearUVMinMax.xy, min(View_BufferBilinearUVMinMax.zw, ScreenUV));
				float4 C4DecalColor = float4(0, 0, 0, 1);
				float4 C4DecalRough = float4(0, 0, 0, 1);
				// Each buffer only for a material that takes it (its decal response, the material's own define).
				if ((C4DecalBits & 1) && (MATERIALDECALRESPONSEMASK & 1)) C4DecalColor = Texture2DSampleLevel(OpaqueBasePass_DBufferATexture, OpaqueBasePass_DBufferATextureSampler, ScreenUV, 0);
				if ((C4DecalBits & 4) && (MATERIALDECALRESPONSEMASK & 4)) C4DecalRough = Texture2DSampleLevel(OpaqueBasePass_DBufferCTexture, OpaqueBasePass_DBufferATextureSampler, ScreenUV, 0);
				uint C4DecalMode = (uint)(C4DecalMask.a * 255);
				float3 C4DecalBase = BaseColor;
				if (C4DecalMode == 2)
				{
					float C4Blend = saturate(Roughness * 10);
					C4Blend = C4Blend - Metallic * C4Blend;
					float3 C4Altered = (BaseColor * BaseColor - BaseColor) * C4Blend * C4DecalMask.g * 0.75 + BaseColor;
					float3 C4Lit = C4DecalMask.b * C4Altered * 10 - C4Altered;
					C4DecalBase = saturate(MaterialParameters.WorldNormal.z) * C4Lit + C4Altered;
					Roughness = C4DecalMask.g * (max(Roughness, 0.2) - Roughness) + Roughness;
					Specular = C4DecalMask.g * (Specular * 0.05 - Specular) + Specular;
				}
				if (C4DecalMode == 1) C4DecalBase *= C4DecalColor.a + C4DecalColor.rgb;
				else C4DecalBase = C4DecalBase * C4DecalColor.a + C4DecalColor.rgb;
				Roughness = C4DecalRough.a * Roughness + C4DecalRough.b;
				Metallic = C4DecalRough.a * Metallic + C4DecalRough.r;
				Specular = C4DecalRough.a * Specular + C4DecalRough.g;
				MaterialParameters.WorldNormal += -0.003921628;
				SubsurfaceColor *= C4DecalColor.a;
				BaseColor = C4DecalBase;
			}
@@ indirect occlusion: the game's capsule shadow controls from the forward shadow mask requires IndirectOcclusion = GetIndirectOcclusion(
--- find
			IndirectOcclusion = GetIndirectOcclusion(NearestResolvedDepthScreenUV, GBuffer);
+++ replace
			{
				// The game's capsule shadows: the forward shadow mask's blue channel, by the capsule self-shadowing
				// intensity, gives the reflection (and indirect) occlusion and the directional and local light scales.
				float4 C4ShadowMask = OpaqueBasePass_ForwardScreenSpaceShadowMaskTexture.Load(int3(int2(trunc(NearestResolvedDepthScreenUV * View_BufferSizeAndInvSize.xy)) * OpaqueBasePass_UseForwardScreenSpaceShadowMask, 0));
				float C4Capsule = HasDynamicIndirectShadowCasterRepresentation(GBuffer) ? View_IndirectCapsuleSelfShadowingIntensity : 1;
				// By the material's C4ShaderParam0 (0 on characters and the objects that take no capsule shadow).
				float C4Delta = KADAN_C4ShaderParam0 * (C4ShadowMask.b * C4Capsule - C4Capsule);
				C4ReflectionOcclusion = C4Delta * View_C4LightParam1.z + 1;
				C4DirectionalScale = View_C4LightParam.w * C4Delta + 1;
				C4LocalLightScale = pow(C4DirectionalScale, max(View_C4LightParam1.y, 0));
				IndirectOcclusion = C4ReflectionOcclusion;
			}
@@ reflection: blended toward the directional shadow by the game's weight requires ReflectionColor = GetImageBasedReflectionLighting( unless ColorSeparateSpecular += ReflectionColor;
--- find
						Color += ReflectionColor;
+++ replace
						Color += ReflectionColor * (View_C4LightParam.x + (DirectionalLightShadow - View_C4LightParam.x * DirectionalLightShadow));
@@ reflection, thin translucent: the same blend on the separate specular the thin translucent model keeps its reflection in requires ColorSeparateSpecular += ReflectionColor;
--- find
						ColorSeparateSpecular += ReflectionColor;
+++ replace
						ColorSeparateSpecular += ReflectionColor * (View_C4LightParam.x + (DirectionalLightShadow - View_C4LightParam.x * DirectionalLightShadow));
)KSP"),
TEXT(R"KSP(@@ indirect diffuse: no AO multi-bounce requires DiffuseColor += (DiffuseIndirectLighting
--- find
		DiffuseColor += (DiffuseIndirectLighting * DiffuseColorForIndirect + SubsurfaceIndirectLighting * SubsurfaceColor) * AOMultiBounce( GBuffer.BaseColor, DiffOcclusion );
+++ replace
		// The game's water mode C4Custom0 has no indirect diffuse.
		if (!(KADAN_TRANSLUCENT && KADAN_C4TranslucencyLightingMode == 1))
			DiffuseColor += DiffuseIndirectLighting * DiffuseColorForIndirect + SubsurfaceIndirectLighting * SubsurfaceColor;
@@ reflection mixing: by the indirect brightness before the capsule occlusion requires IndirectIrradiance *= IndirectOcclusion
--- find
			IndirectIrradiance *= IndirectOcclusion;
+++ replace
@@ volumetric lightmap: the game's own indirect colour scale requires FThreeBandSHVectorRGB IrradianceSH = GetVolumetricLightmapSH3(VolumetricLightmapBrickTextureUVs);
--- find
			OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) / PI;
+++ replace
			OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) * View_C4IndirectLightingVolumetricLightmapColorScale / PI;
@@ volumetric lightmap, ambient only (non-directional translucency): the game's own indirect colour scale requires FOneBandSHVectorRGB IrradianceSH = GetVolumetricLightmapSH1(VolumetricLightmapBrickTextureUVs);
--- find
			OutDiffuseLighting = max(float3(0,0,0), DotSH1(IrradianceSH, DiffuseTransferSH)) / PI;
+++ replace
			OutDiffuseLighting = max(float3(0,0,0), DotSH1(IrradianceSH, DiffuseTransferSH)) * View_C4IndirectLightingVolumetricLightmapColorScale / PI;
@@ volumetric lightmap per vertex (translucency): the game's own indirect colour scale requires IrradianceSH.R.V0 = BasePassInterpolants.VertexIndirectSH[0];
--- find
			OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) / PI;
+++ replace
			OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) * View_C4IndirectLightingVolumetricLightmapColorScale / PI;
@@ volumetric lightmap: in place of the stock indirect colour scale requires * View_C4IndirectLightingVolumetricLightmapColorScale / PI;
--- find
#line 552 "/Engine/Private/BasePassPixelShader.usf"
	OutDiffuseLighting *= View_IndirectLightingColorScale;
+++ replace
#line 552 "/Engine/Private/BasePassPixelShader.usf"
@@ sky bent normal (volumetric lightmap): the visibility kept above C4LightParam1.x, except on two-sided foliage as in the game's lightmap path requires float3 SkyBentNormal = GetVolumetricLightmapSkyBentNormal(SkyOcclusionUV3D);
--- find
				SkyVisibility = length(SkyBentNormal);
				NormalizedBentNormal = SkyBentNormal / max(SkyVisibility, .0001f);
+++ replace
				NormalizedBentNormal = SkyBentNormal / max(length(SkyBentNormal), .0001f);
				SkyVisibility = GBuffer.ShadingModelID == 6 ? length(SkyBentNormal) : max(length(SkyBentNormal), View_C4LightParam1.x);
@@ sky bent normal (lightmap): the visibility kept above C4LightParam1.x, except on two-sided foliage requires float4 WorldSkyBentNormalAndOcclusion = GetSkyBentNormalAndOcclusion(
--- find
				SkyVisibility = WorldSkyBentNormalAndOcclusion.w;
+++ replace
				SkyVisibility = GBuffer.ShadingModelID == 6 ? WorldSkyBentNormalAndOcclusion.w : max(WorldSkyBentNormalAndOcclusion.w, View_C4LightParam1.x);
@@ sky reflection: blended toward the destination cubemap requires SkyLighting = GetSkyLightReflection(
--- find
			float3 SkyLighting = GetSkyLightReflection(RayDirection, Roughness, SkyAverageBrightness);
+++ replace
			float3 SkyLighting = GetSkyLightReflectionSupportingBlend(RayDirection, Roughness, SkyAverageBrightness);
@@ material AO: kept for the game's direct lighting requires float MaterialAO = GetMaterialAmbientOcclusion(
--- find
	float MaterialAO = GetMaterialAmbientOcclusion(PixelMaterialInputs);
+++ replace
	float MaterialAO = GetMaterialAmbientOcclusion(PixelMaterialInputs);
	C4MaterialAO = MaterialAO;
@@ pixel depth offset: the depth from w itself (the game's SvPosition.w, and so ScreenPosition's scale, is 1/w: the fragment's w as its programs read it, spirv_front.h), where stock adds the offset to a w it reads as w requires float DeviceDepth = min(MaterialParameters.ScreenPosition.z / (MaterialParameters.ScreenPosition.w + PixelDepthOffset), MaterialParameters.SvPosition.z);
--- find
	float DeviceDepth = min(MaterialParameters.ScreenPosition.z / (MaterialParameters.ScreenPosition.w + PixelDepthOffset), MaterialParameters.SvPosition.z);
+++ replace
	float C4ViewW = 1 / MaterialParameters.SvPosition.w;
	float DeviceDepth = min(MaterialParameters.SvPosition.z * C4ViewW / (C4ViewW + PixelDepthOffset), MaterialParameters.SvPosition.z);
@@ pixel depth offset: the offset the clamped depth leaves, in w requires float C4ViewW = 1 / MaterialParameters.SvPosition.w;
--- find
	PixelDepthOffset = (MaterialParameters.ScreenPosition.z - DeviceDepth * MaterialParameters.ScreenPosition.w) / DeviceDepth;
+++ replace
	PixelDepthOffset = (MaterialParameters.SvPosition.z - DeviceDepth) * C4ViewW / DeviceDepth;
@@ pixel depth offset: the screen position's (and SvPosition's) w as the game's 1/w of the offset w requires float C4ViewW = 1 / MaterialParameters.SvPosition.w;
--- find
	MaterialParameters.ScreenPosition.w += PixelDepthOffset;
+++ replace
	MaterialParameters.ScreenPosition.w = 1 / (C4ViewW + PixelDepthOffset);
@@ translucency: no pixel where the material is fully transparent, on materials with bC4MaterialParam10
--- find
	float  Opacity = GetMaterialOpacity(PixelMaterialInputs);
+++ replace
	float  Opacity = GetMaterialOpacity(PixelMaterialInputs);
#if KADAN_TRANSLUCENT
	if (KADAN_bC4MaterialParam10) clip(Opacity > 0 ? 1 : -1);
#endif
@@ light grid: the pixel relative to the view rect, then scaled requires (uint2)(MaterialParameters.SvPosition.xy * View_LightProbeSizeRatioAndInvSizeRatio.zw - View_ViewRectMin.xy)
--- find
				GridIndex = ComputeLightGridCellIndex((uint2)(MaterialParameters.SvPosition.xy * View_LightProbeSizeRatioAndInvSizeRatio.zw - View_ViewRectMin.xy), MaterialParameters.SvPosition.w, EyeIndex);
+++ replace
				GridIndex = ComputeLightGridCellIndex((uint2)((MaterialParameters.SvPosition.xy - View_ViewRectMin.xy) * View_LightProbeSizeRatioAndInvSizeRatio.zw), MaterialParameters.SvPosition.w, EyeIndex);
@@ forward shadow factors: always read, at texel 0 when the mask is off requires if (OpaqueBasePass_UseForwardScreenSpaceShadowMask)
--- find
	[branch]
	if (OpaqueBasePass_UseForwardScreenSpaceShadowMask)
	{
		Value = OpaqueBasePass_ForwardScreenSpaceShadowMaskTexture.Load(int3(IntScreenUV, 0));
	}
+++ replace
	Value = OpaqueBasePass_ForwardScreenSpaceShadowMaskTexture.Load(int3(IntScreenUV * OpaqueBasePass_UseForwardScreenSpaceShadowMask, 0));
)KSP"),
TEXT(R"KSP(@@ capsule lights: the game's roughness floor requires FDirectLighting IntegrateBxDF( FGBufferData GBuffer,  float3  N,  float3  V, FCapsuleLight Capsule, FShadowTerms Shadow, bool bInverseSquared )
--- find
	GBuffer.Roughness = max( GBuffer.Roughness, View_MinRoughness );
+++ replace
	GBuffer.Roughness = max( GBuffer.Roughness, 0.02 );
@@ GGX specular: capped by the game's C4LightParam.z requires float3 SpecularGGX( float Roughness, float3 SpecularColor, BxDFContext Context, float NoL, FAreaLight AreaLight )
--- find
	float D = D_GGX( a2, Context.NoH ) * Energy;
	float Vis = Vis_SmithJointApprox( a2, Context.NoV, NoL );
	float3 F = F_Schlick( SpecularColor, Context.VoH );

	return (D * Vis) * F;
+++ replace
	float D = D_GGX( a2, Context.NoH ) * Energy;
	float Vis = Vis_SmithJointApprox( a2, Context.NoV, NoL );
	float3 F = F_Schlick( SpecularColor, Context.VoH );

	float C4Scale = D * Vis;
	// The game's water mode C4Custom1 caps at ten times the scene's cap.
	const float C4Cap = View_C4LightParam.z * (KADAN_C4TranslucencyLightingMode == 2 ? 10 : 1);
	if (C4Cap > 0) C4Scale = min(C4Scale, C4Cap);
	return C4Scale * F;
)KSP"),
TEXT(R"KSP(@@ directional light: the game's shadow channels, strength blend, AO and scale requires float WholeSceneShadowing = LightData.DistanceFadeMAD.y
--- find
			float4 PreviewShadowMapChannelMask = UnpackShadowMapChannelMask(DirectionalLightData.DirectionalLightShadowMapChannelMask >> 4);
			float DynamicShadowing = dot(PreviewShadowMapChannelMask, DynamicShadowFactors);




			float PerObjectShadowing = LightData.DistanceFadeMAD.y < 0.0f ? 1.0f : DynamicShadowing;
			float WholeSceneShadowing = LightData.DistanceFadeMAD.y < 0.0f ? DynamicShadowing : 1.0f;

			float4 LightAttenuation = float4(WholeSceneShadowing.xx, PerObjectShadowing.xx);
#line 168 "/Engine/Private/ForwardLightingCommon.ush"
		FDeferredLightingSplit NewLighting = GetDynamicLightingSplit(WorldPosition, -CameraVector, GBufferData, 1, GBufferData.ShadingModelID, LightData, LightAttenuation, Dither, uint2(0,0), RectTexture, OutDirectionalLightShadow);

		[flatten]
		if ((DirectionalLightData.DirectionalLightShadowMapChannelMask >> 8) & LightingChannelMask)
		{
			DirectLighting.DiffuseLighting += NewLighting.DiffuseLighting;
			DirectLighting.SpecularLighting += NewLighting.SpecularLighting;
		}
+++ replace
		// The game's directional shadow: the view's object shadow channels (C4LightParam6.x) cap the distance-faded
		// blend of the dynamic and static shadows; unshadowed, the surface shadow is the material AO. The raw surface
		// shadow goes to the reflection; the light uses it blended by C4LightParam1.w.
		float C4Surface = C4MaterialAO;
		float C4Transmission = 1;
		float4 C4ObjectChannels = UnpackShadowMapChannelMask(View_C4LightParam6.x);
		float C4Object = 1;
		[flatten]
		if (dot(C4ObjectChannels, 1) >= 1)
		{
			C4Object = dot(DynamicShadowFactors, C4ObjectChannels);
		}
		[branch]
		if (LightData.ShadowedBits)
		{
			float C4UsedChannels = dot(LightData.ShadowMapChannelMask, 1);
			float C4Static = dot(GBufferData.PrecomputedShadowFactors, LightData.ShadowMapChannelMask) * C4UsedChannels - C4UsedChannels + 1;
			float C4Dynamic = dot(UnpackShadowMapChannelMask(DirectionalLightData.DirectionalLightShadowMapChannelMask >> 4), DynamicShadowFactors);
			float C4Fade = DistanceFromCameraFade(GBufferData.Depth, LightData, WorldPosition, View_WorldCameraOrigin);
			float C4Combined = C4Fade * (C4Static - C4Dynamic) + C4Dynamic;
			C4Surface = min(C4Combined, C4Object);
			C4Transmission = min(C4Object, min(C4Combined, 1));
		}
		OutDirectionalLightShadow = C4Surface;
		// The shadow strength scaled by the material's C4ShaderParam1 (0.5 on the effects).
		float C4Strength = saturate(View_C4LightParam1.w * KADAN_C4ShaderParam1);
		C4Surface = C4Surface * C4Strength - C4Strength + 1;
		C4Transmission = C4Strength * C4Transmission - C4Strength + 1;
		FDeferredLightingSplit NewLighting = (FDeferredLightingSplit)0;
		[branch]
		if (C4Transmissive(GBufferData.ShadingModelID))
		{
			// Two-sided foliage (and the other transmissive models): the specular by the surface shadow kept above C4LightParam2.x and the material AO
			// raised to C4LightParam.y, the diffuse by the surface shadow, the transmission by its own; a light fully
			// in shadow still gives specular at that floor.
			// The floor only on materials with bC4MaterialParam0.
			const float C4Floor = KADAN_bC4MaterialParam0 ? View_C4LightParam2.x : 0;
			float C4SurfaceLit = C4Floor + (C4Surface - C4Floor * C4Surface);
			[branch]
			if (C4Surface + (C4Transmission - 1) > -1)
			{
				FShadowTerms C4Shadow = (FShadowTerms)0;
				FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, GetCapsule(LightData.Direction, LightData), C4Shadow, false);
				NewLighting.DiffuseLighting.rgb = C4Lighting.Diffuse * LightData.Color * C4Surface + C4Lighting.Transmission * LightData.Color * C4Transmission;
				NewLighting.SpecularLighting.rgb = C4Lighting.Specular * pow(C4MaterialAO, View_C4LightParam.y) * LightData.Color * C4SurfaceLit;
			}
			else if (C4Floor > 0)
			{
				FShadowTerms C4Shadow = (FShadowTerms)0;
				FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, GetCapsule(LightData.Direction, LightData), C4Shadow, false);
				NewLighting.SpecularLighting.rgb = C4Lighting.Specular * pow(C4MaterialAO, View_C4LightParam.y) * LightData.Color * C4SurfaceLit;
			}
		}
		else if (C4Surface + (C4Transmission - 1) > -1)
		{
			FShadowTerms C4Shadow = (FShadowTerms)0;
			FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, GetCapsule(LightData.Direction, LightData), C4Shadow, false);
			float3 C4Color = LightData.Color * C4Surface;
			NewLighting.DiffuseLighting.rgb = C4Lighting.Diffuse * C4MaterialAO * C4Color;
			NewLighting.SpecularLighting.rgb = C4Lighting.Specular * C4MaterialAO * C4Color;
		}

		[flatten]
		if ((DirectionalLightData.DirectionalLightShadowMapChannelMask >> 8) & LightingChannelMask)
		{
			DirectLighting.DiffuseLighting += C4DirectionalScale * NewLighting.DiffuseLighting;
			DirectLighting.SpecularLighting += C4DirectionalScale * NewLighting.SpecularLighting;
		}
)KSP"),
TEXT(R"KSP(@@ translucent directional light: the game's static and cascade shadow blend, strength and two-sided foliage terms requires float4 LightAttenuation = float4(DynamicShadowFactor.x, DynamicShadowFactor.x, 1, 1);
--- find
			float4 LightAttenuation = float4(DynamicShadowFactor.x, DynamicShadowFactor.x, 1, 1);


		FDeferredLightingSplit NewLighting = GetDynamicLightingSplit(WorldPosition, -CameraVector, GBufferData, 1, GBufferData.ShadingModelID, LightData, LightAttenuation, Dither, uint2(0,0), RectTexture, OutDirectionalLightShadow);

		[flatten]
		if ((DirectionalLightData.DirectionalLightShadowMapChannelMask >> 8) & LightingChannelMask)
		{
			DirectLighting.DiffuseLighting += NewLighting.DiffuseLighting;
			DirectLighting.SpecularLighting += NewLighting.SpecularLighting;
		}
+++ replace
		// As the opaque pass's directional light, with the cascade shadow as the dynamic shadow and no object shadow
		// channels (a translucent pass has no forward shadow mask).
		float C4UsedChannels = dot(LightData.ShadowMapChannelMask, 1);
		float C4Static = dot(GBufferData.PrecomputedShadowFactors, LightData.ShadowMapChannelMask) * C4UsedChannels - C4UsedChannels + 1;
		float C4Fade = DistanceFromCameraFade(GBufferData.Depth, LightData, WorldPosition, View_WorldCameraOrigin);
		float C4Combined = C4Fade * (C4Static - DynamicShadowFactor) + DynamicShadowFactor;
		float C4Surface = min(C4Combined, 1);
		float C4Transmission = min(C4Combined, 1);
		OutDirectionalLightShadow = C4Surface;
		// The shadow strength scaled by the material's C4ShaderParam1 (0 on the game's "ShadowOff" materials).
		float C4Strength = saturate(View_C4LightParam1.w * KADAN_C4ShaderParam1);
		C4Surface = C4Surface * C4Strength - C4Strength + 1;
		C4Transmission = C4Strength * C4Transmission - C4Strength + 1;
		FDeferredLightingSplit NewLighting = (FDeferredLightingSplit)0;
		[branch]
		if (C4Transmissive(GBufferData.ShadingModelID))
		{
			const float C4Floor = KADAN_bC4MaterialParam0 ? View_C4LightParam2.x : 0;
			float C4SurfaceLit = C4Floor + (C4Surface - C4Floor * C4Surface);
			[branch]
			if (C4Surface + (C4Transmission - 1) > -1)
			{
				FShadowTerms C4Shadow = (FShadowTerms)0;
				FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, GetCapsule(LightData.Direction, LightData), C4Shadow, false);
				NewLighting.DiffuseLighting.rgb = C4Lighting.Diffuse * LightData.Color * C4Surface + C4Lighting.Transmission * LightData.Color * C4Transmission;
				NewLighting.SpecularLighting.rgb = C4Lighting.Specular * pow(C4MaterialAO, View_C4LightParam.y) * LightData.Color * C4SurfaceLit;
			}
			else if (C4Floor > 0)
			{
				FShadowTerms C4Shadow = (FShadowTerms)0;
				FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, GetCapsule(LightData.Direction, LightData), C4Shadow, false);
				NewLighting.SpecularLighting.rgb = C4Lighting.Specular * pow(C4MaterialAO, View_C4LightParam.y) * LightData.Color * C4SurfaceLit;
			}
		}
		else if (C4Surface + (C4Transmission - 1) > -1)
		{
			float3 C4Color = LightData.Color * C4Surface;
#if KADAN_TLM == 0
			// Volumetric non-directional: the diffuse colour lit whatever its angle to the light, and no specular.
			NewLighting.DiffuseLighting.rgb = GBufferData.DiffuseColor / PI * C4MaterialAO * C4Color;
#else
			FShadowTerms C4Shadow = (FShadowTerms)0;
			FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, GetCapsule(LightData.Direction, LightData), C4Shadow, false);
			NewLighting.DiffuseLighting.rgb = C4Lighting.Diffuse * C4MaterialAO * C4Color;
			NewLighting.SpecularLighting.rgb = C4Lighting.Specular * C4MaterialAO * C4Color;
#endif
		}
#if KADAN_TLM <= 3
		// The volumetric modes have no specular.
		NewLighting.SpecularLighting = 0;
#endif

		[flatten]
		if ((DirectionalLightData.DirectionalLightShadowMapChannelMask >> 8) & LightingChannelMask)
		{
			DirectLighting.DiffuseLighting += NewLighting.DiffuseLighting;
			DirectLighting.SpecularLighting += NewLighting.SpecularLighting;
		}
)KSP"),
TEXT(R"KSP(@@ local lights: the game's shadow blend, point capsule, AO and scale requires float4 LightAttenuation = float4(1, 1, DynamicShadowing.x, DynamicShadowing.x);
--- find
		float4 LightAttenuation = float4(1, 1, DynamicShadowing.x, DynamicShadowing.x);
		float SurfaceShadow = 1.0f;
		FDeferredLightingSplit NewLighting = GetDynamicLightingSplit(WorldPosition, -CameraVector, GBufferData, 1, GBufferData.ShadingModelID, LightData, LightAttenuation, Dither, uint2(0,0), RectTexture, SurfaceShadow);

		[flatten]
		if ((LightTypeAndPackedShadowMapChannelMask >> 8) & LightingChannelMask)
		{
			DirectLighting.DiffuseLighting += NewLighting.DiffuseLighting;
			DirectLighting.SpecularLighting += NewLighting.SpecularLighting;
		}
+++ replace
		// The game's local light: the dynamic shadow blended by C4LightParam1.w, times the static shadow; unshadowed,
		// the material AO. The capsule is lit as a point at its first end (no line integration), with the material
		// AO, and accumulated by the capsule shadow's local light scale and the material AO again.
		// Scaled by the material's C4ShaderParam1.
		float C4Strength = saturate(View_C4LightParam1.w * KADAN_C4ShaderParam1);
		float C4Dynamic = DynamicShadowing * C4Strength - C4Strength + 1;
		float C4UsedChannels = dot(LightData.ShadowMapChannelMask, 1);
		float C4Static = dot(GBufferData.PrecomputedShadowFactors, LightData.ShadowMapChannelMask) * C4UsedChannels - C4UsedChannels + 1;
		float C4Shadow = LightData.ShadowedBits ? C4Dynamic * C4Static : C4MaterialAO;
		// In the translucent pass, a material with C4ShaderParam5 at 0 takes each light's own specular scale (the
		// record's RectBarnDoor.x in the game's light data).
		const float C4SpecularScale = KADAN_TRANSLUCENT && KADAN_C4ShaderParam5 == 0 ? saturate(LocalLight.RectBarnDoor.x) : 1;
		[branch]
		if (C4Transmissive(GBufferData.ShadingModelID))
		{
			// Two-sided foliage (and the other transmissive models): the transmission by the material's C4ShaderParam7 and its own shadow, the specular by the material AO
			// raised to C4LightParam.y; accumulated by the capsule shadow scale and the material AO once.
			float C4Transmission = LightData.ShadowedBits ? C4Shadow : 1;
			float3 C4ToLight = 0;
			float3 C4L = 0;
			float C4Mask = GetLocalLightAttenuation(WorldPosition, LightData, C4ToLight, C4L);
			[branch]
			if (((LightTypeAndPackedShadowMapChannelMask >> 8) & LightingChannelMask) && C4Mask > 0 && C4Shadow > -C4Transmission)
			{
				FCapsuleLight C4Capsule = GetCapsule(C4ToLight, LightData);
				C4Capsule.Length = 0;
				FShadowTerms C4ShadowTerms = (FShadowTerms)0;
				FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, C4Capsule, C4ShadowTerms, LightData.bInverseSquared);
				float3 C4Color = LightData.Color * C4Mask * C4LocalLightScale;
				DirectLighting.DiffuseLighting.rgb += C4MaterialAO * (C4Lighting.Diffuse * C4Color * C4Shadow + C4Lighting.Transmission * KADAN_C4ShaderParam7 * C4Color * C4Transmission);
#if !(KADAN_TRANSLUCENT && KADAN_TLM <= 3)   // the volumetric modes have no specular
				DirectLighting.SpecularLighting.rgb += C4MaterialAO * C4Lighting.Specular * pow(C4MaterialAO, View_C4LightParam.y) * C4Color * C4Shadow * C4SpecularScale;
#endif
			}
		}
		else if ((LightTypeAndPackedShadowMapChannelMask >> 8) & LightingChannelMask)
		{
			float3 C4ToLight = 0;
			float3 C4L = 0;
			float C4Mask = GetLocalLightAttenuation(WorldPosition, LightData, C4ToLight, C4L);
			[branch]
			if (C4Mask > 0 && C4Shadow > -C4Shadow)
			{
#if KADAN_TRANSLUCENT && (KADAN_TLM == 0 || KADAN_C4TranslucencyLightingMode == 1)
				// Volumetric non-directional, and the game's water mode C4Custom0: the diffuse colour lit whatever
				// its angle to the light (the capsule's falloff, as UE's), and no specular.
				FCapsuleLight C4Capsule = GetCapsule(C4ToLight, LightData);
				float C4Falloff = 1;
				if (LightData.bInverseSquared)
				{
					if (C4Capsule.Length > 0)
					{
						float C4Inv01 = rsqrt(dot(C4Capsule.LightPos[0], C4Capsule.LightPos[0])) * rsqrt(dot(C4Capsule.LightPos[1], C4Capsule.LightPos[1]));
						C4Falloff = C4Inv01 / (dot(C4Capsule.LightPos[0], C4Capsule.LightPos[1]) * C4Inv01 * 0.5 + 0.5 + C4Capsule.DistBiasSqr * C4Inv01);
					}
					else
					{
						C4Falloff = rcp(dot(C4Capsule.LightPos[0], C4Capsule.LightPos[0]) + C4Capsule.DistBiasSqr);
					}
				}
				DirectLighting.DiffuseLighting.rgb += GBufferData.DiffuseColor / PI * C4Falloff * LightData.Color * C4Mask * C4Shadow * C4LocalLightScale * C4MaterialAO * C4MaterialAO;
#else
				FCapsuleLight C4Capsule = GetCapsule(C4ToLight, LightData);
				C4Capsule.Length = 0;
				FShadowTerms C4ShadowTerms = (FShadowTerms)0;
				FDirectLighting C4Lighting = IntegrateBxDF(GBufferData, GBufferData.WorldNormal, CameraVector, C4Capsule, C4ShadowTerms, LightData.bInverseSquared);
				float3 C4Color = LightData.Color * C4Mask * C4Shadow;
				DirectLighting.DiffuseLighting.rgb += C4Lighting.Diffuse * C4MaterialAO * C4Color * C4LocalLightScale * C4MaterialAO;
#if !(KADAN_TRANSLUCENT && KADAN_TLM <= 3)
				DirectLighting.SpecularLighting.rgb += C4Lighting.Specular * C4MaterialAO * C4Color * C4LocalLightScale * C4MaterialAO * C4SpecularScale;
#endif
#endif
			}
		}
)KSP"),
TEXT(R"KSP(@@ material sky light direction: the game's directional light direction (pixel)
--- find
float3 MaterialExpressionSkyAtmosphereLightDirection(FMaterialPixelParameters Parameters, uint LightIndex) {return View_AtmosphereLightDirection[LightIndex].xyz;}
+++ replace
float3 MaterialExpressionSkyAtmosphereLightDirection(FMaterialPixelParameters Parameters, uint LightIndex) {return View_DirectionalLightDirection;}
@@ material sky light direction: the game's directional light direction (vertex)
--- find
float3 MaterialExpressionSkyAtmosphereLightDirection(FMaterialVertexParameters Parameters, uint LightIndex) {return View_AtmosphereLightDirection[LightIndex].xyz;}
+++ replace
float3 MaterialExpressionSkyAtmosphereLightDirection(FMaterialVertexParameters Parameters, uint LightIndex) {return View_DirectionalLightDirection;}
@@ subsurface colour: no diffuse override (a view mode the game's programs do not have)
--- find
			SubsurfaceColor = SubsurfaceData.rgb * View_DiffuseOverrideParameter.w + View_DiffuseOverrideParameter.xyz;
+++ replace
			SubsurfaceColor = SubsurfaceData.rgb;
@@ transmissive models: occlusion from the forward shadow mask, and the diffuse colour darkened toward the base colour requires GBuffer.DiffuseColor = BaseColor - BaseColor * Metallic;
--- find
	float3 BentNormal = MaterialParameters.WorldNormal;
+++ replace
	// Clear coat: the base colour the coat lets through, as UE's mobile renderer computes it, after the diffuse and
	// specular colours (it changes only what the base colour is used for after them).
	if (ShadingModel == 4)
	{
		const float C4ClearCoat = saturate(GetMaterialCustomData0(MaterialParameters));
		const float C4NoV = saturate(dot(MaterialParameters.WorldNormal, MaterialParameters.CameraVector));
		const float C4RefractionScale = ((C4NoV * 0.5 + 0.5) * C4NoV - 1) * saturate(1.25 - 1.25 * Roughness) + 1;
		const float3 C4AbsorptionColor = BaseColor * (1 / 0.9);
		const float3 C4Absorption = C4AbsorptionColor * ((C4NoV - 1) * 0.85 * (1 - lerp(C4AbsorptionColor, C4AbsorptionColor * C4AbsorptionColor, -0.78)) + 1);
		const float C4Fc = Pow5(1 - C4NoV);
		const float C4F = C4Fc + (1 - C4Fc) * 0.04;
		BaseColor = lerp(BaseColor * lerp(1, 1 - C4F, C4ClearCoat), 0.9 * C4Absorption * C4RefractionScale, Metallic * C4ClearCoat);
	}
	[branch]
	if (C4Transmissive(GBuffer.ShadingModelID))
	{
		// The occlusion is the material AO or the forward shadow mask's alpha, whichever is lower, and it darkens the
		// diffuse colour toward the base colour.
		float2 C4FoliageUV = max(View_BufferBilinearUVMinMax.xy, min(View_BufferBilinearUVMinMax.zw, (MaterialParameters.ScreenPosition.xy / MaterialParameters.ScreenPosition.w) * View_ScreenPositionScaleBias.xy + View_ScreenPositionScaleBias.wz));
#if KADAN_TRANSLUCENT
		float C4MaskAO = 1;   // a translucent pass has no forward shadow mask
#else
		float C4MaskAO = OpaqueBasePass_ForwardScreenSpaceShadowMaskTexture.Load(int3(int2(trunc(C4FoliageUV * View_BufferSizeAndInvSize.xy)) * OpaqueBasePass_UseForwardScreenSpaceShadowMask, 0)).a;
#endif
		C4MaterialAO = min(C4MaskAO, MaterialAO);
		float C4Occlusion = max(saturate(C4MaterialAO), 0.01);
		if (C4Occlusion < 1) C4Darkening = pow(BaseColor, 1 - C4Occlusion);
		GBuffer.DiffuseColor *= C4Darkening;
	}
	float3 BentNormal = MaterialParameters.WorldNormal;
@@ transmissive models' reflection: by the material AO alone (no multi-bounce) requires ReflectionColor = GetImageBasedReflectionLighting(
--- find
						* AOMultiBounce(GBuffer.SpecularColor, SpecOcclusion);
+++ replace
						* (C4Transmissive(GBuffer.ShadingModelID) ? SpecOcclusion : AOMultiBounce(GBuffer.SpecularColor, SpecOcclusion));
@@ volumetric lightmap: the game's own subsurface colour scale requires * View_C4IndirectLightingVolumetricLightmapColorScale / PI;
--- find
	OutSubsurfaceLighting *= View_IndirectLightingColorScale;
+++ replace
	OutSubsurfaceLighting *= View_C4SubsurfaceVolumetricLightmapColorScale;
@@ preintegrated skin: the transmission's colour is the darkened diffuse colour, darkened again
--- find
	float3 SubsurfaceColor = ExtractSubsurfaceColor(GBuffer);
	float Opacity = GBuffer.CustomData.a;

	float3 PreintegratedBRDF
+++ replace
	float3 SubsurfaceColor = GBuffer.DiffuseColor * C4Darkening;
	float Opacity = GBuffer.CustomData.a;

	float3 PreintegratedBRDF
)KSP"),
TEXT(R"KSP(@@ clear coat: the game's own lobes
--- find
FDirectLighting ClearCoatBxDF( FGBufferData GBuffer,  float3  N,  float3  V,  float3  L, float Falloff, float NoL, FAreaLight AreaLight, FShadowTerms Shadow )
{
+++ replace
// The game's clear coat: a coat lobe (D_GGX by the clear coat roughness, Kelemen visibility, Fresnel of 0.04) on the
// clear coat bottom normal's direction, the base layer's diffuse and GGX specular on the material normal scaled by
// what the coat lets through, and two-sided foliage's transmission of the darkened diffuse colour.
FDirectLighting ClearCoatBxDF( FGBufferData GBuffer,  float3  N,  float3  V,  float3  L, float Falloff, float NoL, FAreaLight AreaLight, FShadowTerms Shadow )
{
	const float ClearCoat = GBuffer.CustomData.x;
	const float ClearCoatRoughness = max(GBuffer.CustomData.y, 0.02f);
	const float VoL = dot(V, L);
	const float3 CoatN = OctahedronToUnitVector(((float2(GBuffer.CustomData.a, GBuffer.CustomData.z) * 2) - (256.0/255.0)) + UnitVectorToOctahedron(GBuffer.WorldNormal));
	// The coat's highlight of a light with a source radius, as stock's (its sphere scaled by the coat's roughness).
	BxDFContext CoatContext;
	Init(CoatContext, CoatN, V, L);
	const float CoatAlpha = Pow2(ClearCoatRoughness);
	float RoughnessCompensation = 1 - Pow2(GBuffer.Roughness);
	RoughnessCompensation = RoughnessCompensation > 0.0 ? (1 - CoatAlpha) / RoughnessCompensation : 0.0;
	FAreaLight CoatLight = AreaLight;
	CoatLight.SphereSinAlpha = saturate(AreaLight.SphereSinAlpha * RoughnessCompensation);
	SphereMaxNoH(CoatContext, CoatLight.SphereSinAlpha, true);
	const float VoH = CoatContext.VoH;
	const float Fc = Pow5(1 - VoH);
	const float Coat = ClearCoat * (Fc + (1 - Fc) * 0.04);
	const float CoatNoH = CoatContext.NoH;
	float CoatA2 = Pow2(CoatAlpha);
	const float CoatEnergy = EnergyNormalization(CoatA2, VoH, CoatLight);
	float CoatDVis = D_GGX(CoatA2, CoatNoH) * CoatEnergy / (4 * VoH * VoH + 1e-5);
	if (View_C4LightParam.z > 0) CoatDVis = min(CoatDVis, View_C4LightParam.z);
	FDirectLighting Lighting = DefaultLitBxDF(GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow);
	Lighting.Diffuse *= 1 - Coat;
	Lighting.Specular = Lighting.Specular * (1 - Coat) + AreaLight.FalloffColor * Falloff * Coat * CoatDVis * saturate(dot(CoatN, L));
	const float WrapNoL = saturate((0.5 - dot(N, L)) / 2.25);
	const float Scatter = D_GGX(0.36, saturate(-VoL));
	Lighting.Transmission = AreaLight.FalloffColor * (Falloff * WrapNoL * Scatter) * GBuffer.DiffuseColor;
	return Lighting;
}
FDirectLighting StockClearCoatBxDF( FGBufferData GBuffer,  float3  N,  float3  V,  float3  L, float Falloff, float NoL, FAreaLight AreaLight, FShadowTerms Shadow )
{
@@ clear coat reflection: about the clear coat bottom normal
--- find
					float3  ReflectionColor = GetImageBasedReflectionLighting(MaterialParameters, GBuffer.Roughness, GBuffer.SpecularColor, IndirectIrradiance, GridIndex, SingleCaptureIndex, EyeIndex)
+++ replace
					FMaterialPixelParameters C4ReflectionParameters = MaterialParameters;
					if (GBuffer.ShadingModelID == 4) C4ReflectionParameters.WorldNormal = BentNormal;
					float3  ReflectionColor = GetImageBasedReflectionLighting(C4ReflectionParameters, GBuffer.Roughness, GBuffer.SpecularColor, IndirectIrradiance, GridIndex, SingleCaptureIndex, EyeIndex)
@@ clear coat: the roughness not kept below 254/255
--- find
		GBuffer.CustomData.y = ClearCoatRoughness;


		GBuffer.Roughness = clamp(GBuffer.Roughness, 0.0, 254.0 / 255.0);
+++ replace
		GBuffer.CustomData.y = ClearCoatRoughness;
@@ clear coat: the base colour as the subsurface colour, as two-sided foliage's subsurface (lit from behind by the sky)
--- find
	float3 SubsurfaceColor = 0;
+++ replace
	float3 SubsurfaceColor = 0;
	if (ShadingModel == 4) SubsurfaceColor = BaseColor;
@@ clear coat: the sky from behind, as two-sided foliage's
--- find
	OutDiffuseLighting += DiffuseLookup * (SkyVisibility * GeometryTerm);
+++ replace
	OutDiffuseLighting += DiffuseLookup * (SkyVisibility * GeometryTerm);
	if (GBuffer.ShadingModelID == 4) OutSubsurfaceLighting += GetSkySHDiffuse(-WorldNormal) * View_SkyLightColor.rgb * SkyVisibility;
@@ clear coat: the volumetric lightmap from behind, as two-sided foliage's
--- find
			OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) * View_C4IndirectLightingVolumetricLightmapColorScale / PI;
+++ replace
			OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) * View_C4IndirectLightingVolumetricLightmapColorScale / PI;
			if (GBuffer.ShadingModelID == 4) OutSubsurfaceLighting += max(float3(0,0,0), DotSH3(IrradianceSH, CalcDiffuseTransferSH3(-DiffuseDir, 1))) / PI;
@@ subsurface: the back scatter by the material AO the game lights with
--- find
	float BackScatter = GBuffer.GBufferAO * NormalContribution / (PI * 2);
+++ replace
	float BackScatter = C4MaterialAO * NormalContribution / (PI * 2);
)KSP"),
TEXT(R"KSP(@@ shadow depth: the game's TC4ShadowDepthVS (its permutation 1, with KADAN_C4ShadowDepthBias, adds a bias of its own to the depth bias)
--- find
	const float MaxSlopeDepthBias =  ShadowDepthPass_ShadowParams .z;
	const float Slope = clamp(abs(NoL) > 0 ? sqrt(saturate(1 - NoL*NoL)) / NoL : MaxSlopeDepthBias, 0, MaxSlopeDepthBias);
+++ replace
#ifndef KADAN_C4ShadowDepthBias
#define KADAN_C4ShadowDepthBias 0
#endif
#if KADAN_C4ShadowDepthBias
	// A loose parameter of the game's permutation 1, added to the constant, slope and maximum slope biases.
	const float4 C4ShadowParams = ShadowDepthPass_ShadowParams + float4(C4ShadowDepthBias.xyz, 0);
#else
	const float4 C4ShadowParams = ShadowDepthPass_ShadowParams;
#endif
	const float MaxSlopeDepthBias = C4ShadowParams.z;
	// The slope kept below the maximum first, then above 0 (stock clamps in the other order).
	const float Slope = max(min(abs(NoL) > 0 ? sqrt(saturate(1 - NoL*NoL)) / NoL : MaxSlopeDepthBias, MaxSlopeDepthBias), 0);
@@ shadow depth: the slope bias of the game's permutation requires const float4 C4ShadowParams = ShadowDepthPass_ShadowParams
--- find
	const float SlopeDepthBias =  ShadowDepthPass_ShadowParams .y;
+++ replace
	const float SlopeDepthBias = C4ShadowParams.y;
@@ shadow depth: the constant bias of the game's permutation requires const float4 C4ShadowParams = ShadowDepthPass_ShadowParams
--- find
	const float ConstantDepthBias =  ShadowDepthPass_ShadowParams .x;
+++ replace
	const float ConstantDepthBias = C4ShadowParams.x;
@@ shadow depth: the game's permutation 1's own bias, a loose parameter requires const float4 C4ShadowParams = ShadowDepthPass_ShadowParams
--- find
void SetShadowDepthOutputs(
+++ replace
#if KADAN_C4ShadowDepthBias
float4 C4ShadowDepthBias;
#endif
void SetShadowDepthOutputs(
)KSP"),
TEXT(R"KSP(@@ GroundSoft: the emissive (the scene behind, as the material reads it) after the fog requires 	Color = Color * Fogging.a + Fogging.rgb;
--- find
	Color += Emissive;
+++ replace
	if (!(KADAN_C4MaterialDrawOrder == 1 && !KADAN_TRANSLUCENT)) Color += Emissive;
@@ GroundSoft: the fogged lighting by the material's GroundSoft alpha, then the emissive requires 	Color = Color * Fogging.a + Fogging.rgb;
--- find
			Color = Color * Fogging.a + Fogging.rgb;
+++ replace
			Color = Color * Fogging.a + Fogging.rgb;
			if (KADAN_C4MaterialDrawOrder == 1 && !KADAN_TRANSLUCENT)
			{
#if KADAN_GROUNDSOFT_ALPHA
				Color = Color * GetKadanGroundSoftAlpha0(MaterialParameters) + Emissive;
#else
				Color += Emissive;
#endif
			}
@@ the game's water mode C4Custom0: no indirect brightness for the reflection mixing (it has no indirect diffuse)
--- find
		GetPrecomputedIndirectLightingAndSkyLight(MaterialParameters, Interpolants, BasePassInterpolants, LightmapVTPageTableResult, GBuffer, DiffuseDir, VolumetricLightmapBrickTextureUVs, DiffuseIndirectLighting, SubsurfaceIndirectLighting, IndirectIrradiance);
+++ replace
		GetPrecomputedIndirectLightingAndSkyLight(MaterialParameters, Interpolants, BasePassInterpolants, LightmapVTPageTableResult, GBuffer, DiffuseDir, VolumetricLightmapBrickTextureUVs, DiffuseIndirectLighting, SubsurfaceIndirectLighting, IndirectIrradiance);
		if (KADAN_TRANSLUCENT && KADAN_C4TranslucencyLightingMode == 1) IndirectIrradiance = 0;
)KSP"),
TEXT(R"KSP(@@ decals: the game's mask target, the decal buffers it writes in red and its blend mode in alpha (1 for a stain, which the base pass multiplies) requires Out.MRT[3] = 1.0f;
--- find
		Out.MRT[3] = 1.0f;
+++ replace
#if defined(KADAN_DECAL_DBUFFER_MASK)
		Out.MRT[3] = float4(KADAN_DECAL_DBUFFER_MASK / 255.0f, 0, 0, KADAN_DECAL_C4_MODE / 255.0f);
#else
		Out.MRT[3] = 1.0f;
#endif
@@ decals: the game's custom depth (C4_USE_CUSTOM_DEPTH), the nearer of the scene and custom depths requires float4x4 SvPositionToDecal;
--- find
	In.SvPosition.z = LookupDeviceZ(ScreenUV);
+++ replace
	In.SvPosition.z = LookupDeviceZ(ScreenUV);
#if KADAN_C4_USE_CUSTOM_DEPTH
	In.SvPosition.z = max(Texture2DSampleLevel(SceneTexturesStruct_CustomDepthTexture, SceneTexturesStruct_PointClampSampler, ScreenUV, 0).r, In.SvPosition.z);
#endif
@@ decals: the game's distance fade parameters (C4_USE_DISTANCE_FADE), a loose parameter after stock's requires float4x4 SvPositionToDecal;
--- find
float2 DecalParams;
+++ replace
float2 DecalParams;
#if KADAN_C4_USE_DISTANCE_FADE
float4 C4DistanceFadeParams;
#endif
@@ decals: the game's distance fade, no pixel at or past the fade's end requires float4x4 SvPositionToDecal;
--- find
		WSPosition = SvPositionToWorld(In.SvPosition);
+++ replace
		WSPosition = SvPositionToWorld(In.SvPosition);
#if KADAN_C4_USE_DISTANCE_FADE
		if (saturate(length(WSPosition - View_WorldCameraOrigin) * C4DistanceFadeParams.x + C4DistanceFadeParams.y) >= 1) discard;
#endif
@@ light functions: the game's custom depth (C4_USE_CUSTOM_DEPTH), the nearer of the scene and custom depths requires float4x4 SvPositionToLight;
--- find
	SvPosition.z = LookupDeviceZ(ScreenUV);
+++ replace
	SvPosition.z = LookupDeviceZ(ScreenUV);
#if KADAN_C4_USE_CUSTOM_DEPTH
	SvPosition.z = max(Texture2DSampleLevel(SceneTexturesStruct_CustomDepthTexture, SceneTexturesStruct_PointClampSampler, ScreenUV, 0).r, SvPosition.z);
#endif
@@ light functions: the game's depth branch (C4_USE_LIGHT_FUNCTION_DEPTH_BRANCH), the function only where the distance fade leaves some of it requires float4x4 SvPositionToLight;
--- find
	float GreyScale;
	{
		float3 Color = GetLightFunctionColor(LightVector, AbsoluteWorldPosition);
		GreyScale = dot(Color, .3333f);
	}

	float DistanceFadeAlpha = saturate((LightFunctionParameters2.x - ViewDistance) / (LightFunctionParameters2.x * .2f));
+++ replace
#if KADAN_C4_USE_LIGHT_FUNCTION_DEPTH_BRANCH
	float DistanceFadeAlpha = saturate((LightFunctionParameters2.x - ViewDistance) / (LightFunctionParameters2.x * .2f));
	float GreyScale = 0;
	if (DistanceFadeAlpha > 0)
	{
		float3 Color = GetLightFunctionColor(LightVector, AbsoluteWorldPosition);
		GreyScale = dot(Color, .3333f);
	}
#else
	float GreyScale;
	{
		float3 Color = GetLightFunctionColor(LightVector, AbsoluteWorldPosition);
		GreyScale = dot(Color, .3333f);
	}

	float DistanceFadeAlpha = saturate((LightFunctionParameters2.x - ViewDistance) / (LightFunctionParameters2.x * .2f));
#endif
@@ light functions: no fade by the shadow fade fraction (the game's programs read no LightFunctionParameters) requires float4x4 SvPositionToLight;
--- find
	GreyScale = lerp(LightFunctionParameters2.y, GreyScale, LightFunctionParameters.y);
+++ replace
)KSP"),
TEXT(R"KSP(@@ translucency passes: the game's translucent pass members for its screen space reflection (SSR, MaterialSSR) requires cbuffer TranslucentBasePass
--- find
cbuffer TranslucentBasePass
{
+++ replace
#ifndef KADAN_MATERIAL_SSR
#define KADAN_MATERIAL_SSR 0
#endif
#ifndef KADAN_TRANSLUCENCY_PASS_TYPE
#define KADAN_TRANSLUCENCY_PASS_TYPE 0
#endif
// The game's translucent reflection packed as its SSR pass writes it: the reflection, and what is left of the
// reflection captures (1 - 0.9 of the reflection's alpha).
static float4 C4MaterialSSRPacked = float4(0, 0, 0, 1);
Texture2D TranslucentBasePass_MaterialSSRTexture;
SamplerState TranslucentBasePass_MaterialSSRSampler;
cbuffer TranslucentBasePass
{
	float TranslucentBasePass_SSRColorIntensity;
	float TranslucentBasePass_SSRDistanceThreshould;
	float2 TranslucentBasePass_MaterialSSR_ExtentInverse;
	float2 TranslucentBasePass_MaterialSSR_UVViewportMin;
	float2 TranslucentBasePass_MaterialSSR_UVViewportSize;
	float2 TranslucentBasePass_MaterialSSR_UVViewportBilinearMin;
	float2 TranslucentBasePass_MaterialSSR_UVViewportBilinearMax;
@@ translucency passes: the game's View member that turns its translucent screen space reflection on requires float View_C4XluGlobalClippingPlaneSign;
--- find
	float View_C4XluGlobalClippingPlaneSign;
+++ replace
	float View_C4XluGlobalClippingPlaneSign;
	float4 View_C4MaterialSSROverrideParameter;
@@ translucency passes: the game's screen space reflection of a translucent material, in forward shading too (stock 4.26 traces it only in deferred), with the game's distance threshold, current-frame UV, vertical vignette and colour scale; the SSR pass keeps it, the Recombine pass reads it back blurred requires SpecularColor = EnvBRDFApprox(SpecularColor, Roughness, NoV);
--- find
	SpecularColor = EnvBRDFApprox(SpecularColor, Roughness, NoV);
+++ replace
#if KADAN_MATERIAL_SSR && KADAN_TRANSLUCENCY_PASS_TYPE == 2
	{
		// The SSR pass's packed reflection, blurred 5 by 5 with the weights (0.1, 0.2, 0.4, 0.2, 0.1) in each direction.
		const float2 ViewportUV = (MaterialParameters.SvPosition.xy - View_ViewRectMin.xy) * View_ViewSizeAndInvSize.zw;
		const float2 Center = ViewportUV * TranslucentBasePass_MaterialSSR_UVViewportSize + TranslucentBasePass_MaterialSSR_UVViewportMin;
		const float Weights[5] = {0.1, 0.2, 0.4, 0.2, 0.1};
		float4 Sum = 0;
		for (int Y = 0; Y < 5; ++Y)
			for (int X = 0; X < 5; ++X)
				Sum += Texture2DSample(TranslucentBasePass_MaterialSSRTexture, TranslucentBasePass_MaterialSSRSampler,
					Center + float2(X - 2, Y - 2) * TranslucentBasePass_MaterialSSR_ExtentInverse) * (Weights[X] * Weights[Y]);
		C4MaterialSSRPacked = Sum;
	}
#elif KADAN_MATERIAL_SSR
	if (View_C4MaterialSSROverrideParameter.y >= 1)
	{
		// The game's jitter: no frame index, a quarter step.
		const float StepOffset = (InterleavedGradientNoise(MaterialParameters.SvPosition.xy, 0) - 0.5) * 0.25;
		float3 HitUVz;
		float Level = 0;
		const bool bHit = RayCast(
			TranslucentBasePass_HZBTexture, TranslucentBasePass_HZBSampler,
			MaterialParameters.WorldPosition_CamRelative, RayDirection, Roughness, MaterialParameters.ScreenPosition.w,
			8, StepOffset,
			TranslucentBasePass_HZBUvFactorAndInvFactor,
			false,
			HitUVz,
			Level);
		[branch] if (bHit && HitUVz.z > TranslucentBasePass_SSRDistanceThreshould)
		{
			// The hit in this frame's scene colour (no reprojection), faded toward the top and bottom edges only.
			const float2 SampleUV = HitUVz.xy;
			const float Edge = saturate(abs((SampleUV.y - View_ScreenPositionScaleBias.z) / View_ScreenPositionScaleBias.y) * 16 - 15);
			const float Vignette = saturate(1 - Edge * Edge);
			float4 SSR = SampleScreenColor(TranslucentBasePass_PrevSceneColor, TranslucentBasePass_PrevSceneColorSampler, SampleUV);
			SSR *= Vignette * saturate(2 - 6.6 * Roughness);
			SSR.rgb *= TranslucentBasePass_SSRColorIntensity;
			SSR.rgb *= TranslucentBasePass_PrevSceneColorPreExposureInv;
			C4MaterialSSRPacked = float4(SSR.rgb, 1 - 0.9 * SSR.a);
		}
	}
#endif
#if KADAN_MATERIAL_SSR
	SpecularIBL.rgb = SpecularIBL.rgb * C4MaterialSSRPacked.a + C4MaterialSSRPacked.rgb;
#endif
	SpecularColor = EnvBRDFApprox(SpecularColor, Roughness, NoV);
@@ translucency passes: the game's SSR pass writes only the packed reflection requires Out.MRT[0].rgb *= ViewPreExposure;
--- find
		Out.MRT[0].rgb *= ViewPreExposure;
+++ replace
		Out.MRT[0].rgb *= ViewPreExposure;
#if KADAN_MATERIAL_SSR && KADAN_TRANSLUCENCY_PASS_TYPE == 1
	Out.MRT[0] = C4MaterialSSRPacked;
#endif
@@ translucency passes: the game's ray of a translucent material's reflection starts at mip 0 requires bool CastScreenSpaceRay(
--- find
	Level = 1;
+++ replace
#if defined(KADAN_MATERIAL_SSR) && KADAN_MATERIAL_SSR
	Level = 0;
#else
	Level = 1;
#endif
)KSP"),
TEXT(R"KSP(
@@ a vertex buffer's colour as the game reads it, .bgra (FCOLOR_COMPONENT_SWIZZLE; this profile's is .rgba): Slate
--- find
InColor  .rgba
+++ replace
InColor  .bgra
@@ the same for the GPU skin vertex factory's colour input (the game's Cave_Black skin program: .zyxw)
--- find
Input.Color  .rgba
+++ replace
Input.Color  .bgra
@@ the same for the mesh particle vertex factory's colour input
--- find
Input.VertexColor  .rgba
+++ replace
Input.VertexColor  .bgra
@@ the dithered LOD transition's hash as the game's programs have it (3343.2839; stock UE's 3343.28371963)
--- find
float2(347.83451793, 3343.28371963)
+++ replace
float2(347.83451793, 3343.2839)
@@ the sprite particles' lighting offset hash as the game's programs have it (1361.456 and 1341.456; stock UE's 1361.456345 and 1341.456345)
--- find
1361.456345
+++ replace
1361.456
@@ the same for the GPU sprite particles
--- find
1341.456345
+++ replace
1341.456
@@ the GPU sprite's per-particle parameter texture of the game (C4ParamTexture: the game's FGPUSpriteVertexFactoryShaderParametersVS binds it), declared after the curve texture requires 	float2 Size = InitialSize * SizeScale * SizeScaleBySpeed * IsAlive.xx;
--- find
SamplerState CurveTextureSampler;
+++ replace
SamplerState CurveTextureSampler;
Texture2D C4ParamTexture;
SamplerState C4ParamTextureSampler;
@@ its x scales the GPU sprite's size requires 	float2 Size = InitialSize * SizeScale * SizeScaleBySpeed * IsAlive.xx;
--- find
	float2 Size = InitialSize * SizeScale * SizeScaleBySpeed * IsAlive.xx;
+++ replace
	float4 KadanC4Param = Texture2DSampleLevel(C4ParamTexture, C4ParamTextureSampler, ParticleIndex, 0);
	float2 Size = InitialSize * SizeScale * SizeScaleBySpeed * IsAlive.xx * KadanC4Param.x;
@@ and its y offsets the GPU sprite's sub image requires 	float4 KadanC4Param = Texture2DSampleLevel(C4ParamTexture, C4ParamTextureSampler, ParticleIndex, 0);
--- find
	float SubImageIndex = MiscCurve.z;
+++ replace
	float SubImageIndex = MiscCurve.z + KadanC4Param.y;
@@ decals (projected and mesh): the game's water wetness decal mode (C4DecalBlendMode 1, DBM_DBuffer_C4_WaterBox) writes only the mask target: where its base colour's red is not 0, blue, red and green and the mode 2 in alpha requires 		Out.MRT[3] = float4(KADAN_DECAL_DBUFFER_MASK / 255.0f, 0, 0, KADAN_DECAL_C4_MODE / 255.0f);
--- find
#if defined(KADAN_DECAL_DBUFFER_MASK)
		Out.MRT[3] = float4(KADAN_DECAL_DBUFFER_MASK / 255.0f, 0, 0, KADAN_DECAL_C4_MODE / 255.0f);
+++ replace
#if defined(KADAN_C4DecalBlendMode) && KADAN_C4DecalBlendMode == 1
		Out.MRT[0] = 0;
		Out.MRT[1] = 0;
		Out.MRT[2] = 0;
		Out.MRT[3] = Data.BaseColor.r != 0 ? float4(Data.BaseColor.b, Data.BaseColor.r, Data.BaseColor.g, 2 / 255.0f) : 0;
#elif defined(KADAN_DECAL_DBUFFER_MASK)
		Out.MRT[3] = float4(KADAN_DECAL_DBUFFER_MASK / 255.0f, 0, 0, KADAN_DECAL_C4_MODE / 255.0f);
@@ a UI material's translucent base pass: no volumetric fog (the game's UI masters apply only the vertex fog; the opaque base pass has no such find text)
--- find
	if ( TranslucentBasePass_Shared_Fog_ApplyVolumetricFog  > 0)
	{
		float3 VolumeUV = ComputeVolumeUV(MaterialParameters.AbsoluteWorldPosition, View_WorldToClip);
+++ replace
	if (!KADAN_MATERIAL_DOMAIN_UI && TranslucentBasePass_Shared_Fog_ApplyVolumetricFog  > 0)
	{
		float3 VolumeUV = ComputeVolumeUV(MaterialParameters.AbsoluteWorldPosition, View_WorldToClip);
@@ the material's constant default normal (KADAN_DEFAULT_NORMAL) folded as the game's compiler folds 0 * x: the world normal is the tangent basis's third row, and the program does not read the tangent X interpolant
--- find
	Parameters.WorldNormal = TransformTangentNormalToWorld(Parameters.TangentToWorld, MaterialNormal);
+++ replace
#if KADAN_DEFAULT_NORMAL
	Parameters.WorldNormal = normalize(Parameters.TangentToWorld[2]);
#else
	Parameters.WorldNormal = TransformTangentNormalToWorld(Parameters.TangentToWorld, MaterialNormal);
#endif
@@ Common.ush Texture2DSample_A8: the game's Switch A8 font coverage lives in red (Platform.ush's GLSL mask), not the Vulkan profile's alpha
--- find
	return Tex.Sample(Sampler, UV)  .a ;
+++ replace
	return Tex.Sample(Sampler, UV)  .r ;
@@ the same A8 mask when the stock helper explicitly selects a mip level
--- find
	return Tex.SampleLevel(Sampler, UV, ComputeRayConeLod(Tex) + GlobalTextureMipBias)  .a ;
+++ replace
	return Tex.SampleLevel(Sampler, UV, ComputeRayConeLod(Tex) + GlobalTextureMipBias)  .r ;
)KSP")

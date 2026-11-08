# Material coverage

Which Unreal Engine 4.26 material features Kadan exports to Pikmin 4, and which have been seen drawing
in the game. Made by `tools/material_coverage.py` from `docs/material-coverage.json`; do not edit by hand.

Kadan compiles a creator material with UE4's own material translator and shader source, then its own
SPIR-V and Maxwell compiler (docs/OWN_SHADER_MAPS.md). A row is **seen in the game** only after a material
using the feature was exported and looked at in Eden next to the UE4 editor's rendering, and the feature's own
effect was seen. **Draws** means such a material draws in the game but its effect was not checked there.
**Draws wrong** means it compiles and exports but the game draws it wrong; the note says how.
**Compiles** means
the game compile accepts it and nobody has looked at it in the game yet: it is not a promise that it draws
right. **Refused** rows name the message the export gives.

| Status | Rows |
| --- | ---: |
| seen in the game | 26 |
| draws in the game, its effect not checked yet | 44 |
| draws wrong in the game | 2 |
| compiles, not seen in the game yet | 203 |
| refused | 5 |
| UE4 refuses this setup too | 35 |

## Translucency settings

| Feature | Status | Notes |
| --- | --- | --- |
| Contact Shadows (translucent) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Allow Custom Depth Writes | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Disable Depth Test | seen in the game | Eden, Cave000_F00: a cube 2 m under the floor draws over the floor and a ridge; the same cube with the depth test on stays hidden (2026-11-08) |
| Apply Fogging off, Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Apply Fogging off, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: an unlit translucent cube draws blue with the mushrooms and floor behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Apply Fogging on, Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Apply Fogging on, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: an unlit translucent cube draws blue with the mushrooms and floor behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Compute Fog Per Pixel, Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Compute Fog Per Pixel, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: an unlit translucent cube draws blue with the mushrooms and floor behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Directional Lighting Intensity, shadow density, self-shadow and backscattering numbers | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Output Velocity | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Refraction input (index 1.33) | draws wrong in the game | Eden, Cave000_F00: the cube draws at opacity 0.3 with the mushrooms behind it showing through, but they are not bent: no refraction (2026-11-08) |
| Responsive AA | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Screen Space Reflections (translucent) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Render After DOF off, Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Render After DOF off, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: an unlit translucent cube draws blue with the mushrooms and floor behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Render After DOF on, Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Render After DOF on, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: an unlit translucent cube draws blue with the mushrooms and floor behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Lighting Mode TlmSurface (lit) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the lighting mode's look was not compared with the editor (2026-11-08) |
| Lighting Mode TlmSurfacePerPixelLighting (lit) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the lighting mode's look was not compared with the editor (2026-11-08) |
| Lighting Mode TlmVolumetricDirectional (lit) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the lighting mode's look was not compared with the editor (2026-11-08) |
| Lighting Mode TlmVolumetricNonDirectional (lit) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the lighting mode's look was not compared with the editor (2026-11-08) |
| Lighting Mode TlmVolumetricPerVertexDirectional (lit) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the lighting mode's look was not compared with the editor (2026-11-08) |
| Lighting Mode TlmVolumetricPerVertexNonDirectional (lit) | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the lighting mode's look was not compared with the editor (2026-11-08) |
| Two Sided, Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: a lit translucent cube draws with the cave behind it showing through; the setting's own effect was not checked (2026-11-08) |
| Two Sided, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: an unlit translucent cube draws blue with the mushrooms and floor behind it showing through; the setting's own effect was not checked (2026-11-08) |

## Blend modes

| Feature | Status | Notes |
| --- | --- | --- |
| Additive, Default Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: draws see-through over the cave; the blend's own result was not compared with the editor (2026-11-08) |
| Additive, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: draws see-through over the cave; the blend's own result was not compared with the editor (2026-11-08) |
| AlphaComposite, Default Lit | draws in the game, its effect not checked yet | Eden, Cave000_F00: draws see-through over the cave; the blend's own result was not compared with the editor (2026-11-08) |
| AlphaComposite, Unlit | draws in the game, its effect not checked yet | Eden, Cave000_F00: draws see-through over the cave; the blend's own result was not compared with the editor (2026-11-08) |
| AlphaHoldout, Default Lit | UE4 refuses this setup too | Alpha Holdout blend mode must use unlit shading model. |
| AlphaHoldout, Unlit | compiles, not seen in the game yet |  |
| Masked with Dither Opacity Mask | seen in the game | Eden, Cave000_F00: the cube draws as a fine dither pattern at mask 0.5, the cave behind showing through (2026-11-08) |
| Masked, Default Lit | seen in the game | Eden, Cave000_F00: the sine stripe mask cuts the cube into slats, as in the editor (2026-11-08) |
| Masked, Default Lit, Two Sided | seen in the game | Eden, Cave000_F00: green slats with the far faces' slats showing through the gaps (two-sided), as in the editor (2026-11-08) |
| Masked, Unlit | seen in the game | Eden, Cave000_F00: the sine stripe mask cuts the cube into slats, as in the editor (2026-11-08) |
| Modulate, Default Lit | UE4 refuses this setup too | Dynamically lit translucency is not supported for BLEND_Modulate materials. |
| Modulate, Unlit | compiles, not seen in the game yet |  |
| Opaque, Default Lit | seen in the game | Eden, Cave000_F00: draws as a solid cube, as in the editor (2026-11-08) |
| Opaque, Unlit | seen in the game | Eden, Cave000_F00: draws as a solid cube, as in the editor (2026-11-08) |
| Translucent, Default Lit | seen in the game | Eden, Cave000_F00: a translucent blue cube with the mushrooms behind it showing through (2026-11-08) |
| Translucent, Unlit | seen in the game | Eden, Cave000_F00: a translucent blue cube with the mushrooms behind it showing through (2026-11-08) |

## Shading models

| Feature | Status | Notes |
| --- | --- | --- |
| Shading model ClearCoat | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the face away from the sun whiter than Default Lit's (236,222,221 against 221,194,194): the coat's sky reflection (2026-11-08) |
| Shading model Cloth | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the face away from the sun reddened by the red fuzz colour (247,202,204 against Default Lit's 221,194,194) (2026-11-08) |
| Shading model DefaultLit | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the face toward the sun near white, the face away pale pink (221,194,194) (2026-11-08) |
| Shading model Eye | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws like Default Lit (219,194,193), while the editor's eye cube is darker: not matched yet (2026-11-08) |
| Shading model From Material Expression | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the Shading Model node's Clear Coat applies: the lavender cube draws blue-white with faint reflections, as the editor's coated top (2026-11-08) |
| Fully Rough | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the green cube shades with the sun angle; the missing specular was not checked (2026-11-08) |
| Shading model Hair | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: much darker than Default Lit (165,134,127 away from the sun, 123,90,90 toward it), as the editor's dark hair cube (2026-11-08) |
| Shading model PreintegratedSkin | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the face away from the sun redder than Default Lit's (about 246-251,196-202 against 221,194,194) from the red subsurface colour, as the editor's red sides (2026-11-08) |
| Shading model SingleLayerWater | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lavender-pink (189,162,188 away from the sun); the water look was not compared (2026-11-08) |
| Shading model Subsurface | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the face away from the sun redder than Default Lit's (about 246-251,196-202 against 221,194,194) from the red subsurface colour, as the editor's red sides (2026-11-08) |
| Shading model SubsurfaceProfile | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws like Default Lit (220,195,193); the profile's scattering was not seen (2026-11-08) |
| Shading model ThinTranslucent (translucent) | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: a blue cube with its far faces showing through; transmittance colour not compared (2026-11-08) |
| Shading model TwoSidedFoliage | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the face away from the sun redder than Default Lit's (about 246-251,196-202 against 221,194,194) from the red subsurface colour, as the editor's red sides (2026-11-08) |
| Shading model Unlit | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: both faces the same salmon whatever their angle to the sun (2026-11-08) |

## Material inputs and surface settings

| Feature | Status | Notes |
| --- | --- | --- |
| Ambient Occlusion | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Anisotropy and Tangent | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Customized UVs | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the checker repeats 3 by 3 with its blue dots, as in the editor (Texture 2D shows it once) (2026-11-08) |
| Dithered LOD Transition | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Emissive Color (lit) | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: glows orange-yellow on its shaded faces, as the editor's bright orange block (2026-11-08) |
| Use Full Precision | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| High Quality Forward Reflections, planar reflections | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: a mirror-like metal cube reflecting the surroundings; the high quality reflections themselves were not compared (2026-11-08) |
| Use Lightmap Directionality off | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Use Material Attributes (Make Material Attributes) | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: Make Material Attributes' pink base colour draws, as in the editor (2026-11-08) |
| Metallic | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: gold metal, its faces tinted reflections of the surroundings rather than diffuse shading (2026-11-08) |
| Allow Negative Emissive Color | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws black, as in the editor (2026-11-08) |
| Normal (tangent space) | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Normal Curvature To Roughness | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Normal (world space, Tangent Space Normal off) | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Pixel Depth Offset | draws wrong in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws nothing at all (shaders built, no Eden pipeline error). Likely the depth prepass draws it without the offset, so the base pass's farther depth fails the depth test (2026-11-08) |
| Specular | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Post process stencil test | compiles, not seen in the game yet |  |
| Tessellation (PN triangles), World Displacement, multiplier | draws in the game, its effect not checked yet | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: draws lit; the input's own effect was not compared with the editor (2026-11-08) |
| Wireframe | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: only the triangle edges draw, as the editor's wire cube (2026-11-08) |
| World Position Offset | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the cube moves up and down with time (its top at y 290 then 255 in two pictures moments apart) (2026-11-08) |

## Texture types and parameter collections

| Feature | Status | Notes |
| --- | --- | --- |
| Material parameter collection | refused | Compiles; export refuses MPC_Coverage: Export is not available for this asset type yet. |
| Render target sample | refused | Compiles; export refuses RT_Coverage: Export is not available for this asset type yet. |
| Texture 2D sample | seen in the game | Eden, Area500 in sunlight, cubes turned 45 degrees from the sun: the checker texture draws once per face, as in the editor (2026-11-08) |
| Texture 2D array sample | refused | Compiles; export refuses T_CoverageArray: Export is not available for this asset type yet. |
| Texture cube sample | refused | Compiles; export refuses T_CoverageCube: Export is not available for this asset type yet. |
| Volume texture sample | refused | Compiles; export refuses T_CoverageVolume: Export is not available for this asset type yet. |

## Material domains and decal blend modes

| Feature | Status | Notes |
| --- | --- | --- |
| Decal blend mode DecalAlphaComposite | compiles, not seen in the game yet |  |
| Decal blend mode DecalAmbientOcclusion | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferAlphaComposite | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferColor | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferColorNormal | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferColorNormalRoughness | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferColorRoughness | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferEmissive | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferEmissiveAlphaComposite | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferNormal | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferNormalRoughness | compiles, not seen in the game yet |  |
| Decal blend mode DecalDBufferRoughness | compiles, not seen in the game yet |  |
| Decal blend mode DecalEmissive | compiles, not seen in the game yet |  |
| Decal blend mode DecalNormal | compiles, not seen in the game yet |  |
| Decal blend mode DecalStain | compiles, not seen in the game yet |  |
| Decal blend mode DecalTranslucent | compiles, not seen in the game yet |  |
| Decal blend mode DecalVolumetricDistanceFunction | compiles, not seen in the game yet |  |
| Domain DeferredDecal | compiles, not seen in the game yet |  |
| Domain LightFunction | compiles, not seen in the game yet |  |
| Domain PostProcess | compiles, not seen in the game yet |  |
| Domain RuntimeVirtualTexture | compiles, not seen in the game yet |  |
| Domain UI | compiles, not seen in the game yet |  |
| Domain Volume | compiles, not seen in the game yet |  |

## Expression nodes

| Feature | Status | Notes |
| --- | --- | --- |
| Abs node | compiles, not seen in the game yet |  |
| ActorPositionWS node | compiles, not seen in the game yet |  |
| Add node | compiles, not seen in the game yet |  |
| AntialiasedTextureMask node | compiles, not seen in the game yet |  |
| AppendVector node | compiles, not seen in the game yet |  |
| Arccosine node | compiles, not seen in the game yet |  |
| ArccosineFast node | compiles, not seen in the game yet |  |
| Arcsine node | compiles, not seen in the game yet |  |
| ArcsineFast node | compiles, not seen in the game yet |  |
| Arctangent node | compiles, not seen in the game yet |  |
| Arctangent2 node | compiles, not seen in the game yet |  |
| Arctangent2Fast node | compiles, not seen in the game yet |  |
| ArctangentFast node | compiles, not seen in the game yet |  |
| AtmosphericFogColor node | compiles, not seen in the game yet |  |
| AtmosphericLightColor node | compiles, not seen in the game yet |  |
| AtmosphericLightVector node | compiles, not seen in the game yet |  |
| BentNormalCustomOutput node | compiles, not seen in the game yet |  |
| BlackBody node | compiles, not seen in the game yet |  |
| BlendMaterialAttributes node | compiles, not seen in the game yet |  |
| BreakMaterialAttributes node | compiles, not seen in the game yet |  |
| BumpOffset node | compiles, not seen in the game yet |  |
| CameraPositionWS node | compiles, not seen in the game yet |  |
| CameraVectorWS node | compiles, not seen in the game yet |  |
| Ceil node | compiles, not seen in the game yet |  |
| ChannelMaskParameter node | compiles, not seen in the game yet |  |
| Clamp node | compiles, not seen in the game yet |  |
| ClearCoatNormalCustomOutput node | compiles, not seen in the game yet |  |
| CloudSampleAttribute node | compiles, not seen in the game yet |  |
| CollectionParameter node | UE4 refuses this setup too | (Node CollectionParameter) CollectionParameter has invalid Collection! |
| ComponentMask node | UE4 refuses this setup too | (Node ComponentMask) Couldn't determine result type of component mask 0000 |
| Constant node | compiles, not seen in the game yet |  |
| Constant2Vector node | compiles, not seen in the game yet |  |
| Constant3Vector node | compiles, not seen in the game yet |  |
| Constant4Vector node | compiles, not seen in the game yet |  |
| ConstantBiasScale node | compiles, not seen in the game yet |  |
| Cosine node | compiles, not seen in the game yet |  |
| CrossProduct node | compiles, not seen in the game yet |  |
| CurveAtlasRowParameter node | UE4 refuses this setup too | (Node CurveAtlasRowParameter) > The curve and atlas are not currently set. |
| Custom node | compiles, not seen in the game yet |  |
| DDX node | compiles, not seen in the game yet |  |
| DDY node | compiles, not seen in the game yet |  |
| DecalDerivative node | UE4 refuses this setup too | (Node DecalDerivative) Decal derivatives only available in the decal material domain. |
| DecalLifetimeOpacity node | UE4 refuses this setup too | (Node DecalLifetimeOpacity) Decal Lifetime Opacity> Node only works for the deferred decal material domain. |
| DecalMipmapLevel node | UE4 refuses this setup too | (Node DecalMipmapLevel) Decal Mipmap Level> Node only works for the deferred decal material domain. |
| DeltaTime node | compiles, not seen in the game yet |  |
| DepthFade node | UE4 refuses this setup too | Only transparent or postprocess materials can read from scene depth. |
| DepthOfFieldFunction node | compiles, not seen in the game yet |  |
| DeriveNormalZ node | compiles, not seen in the game yet |  |
| Desaturation node | compiles, not seen in the game yet |  |
| Distance node | compiles, not seen in the game yet |  |
| DistanceCullFade node | compiles, not seen in the game yet |  |
| DistanceFieldGradient node | compiles, not seen in the game yet |  |
| DistanceToNearestSurface node | compiles, not seen in the game yet |  |
| Divide node | compiles, not seen in the game yet |  |
| DotProduct node | compiles, not seen in the game yet |  |
| DynamicParameter node | compiles, not seen in the game yet |  |
| EyeAdaptation node | compiles, not seen in the game yet |  |
| FeatureLevelSwitch node | compiles, not seen in the game yet |  |
| Floor node | compiles, not seen in the game yet |  |
| Fmod node | compiles, not seen in the game yet |  |
| FontSample node | UE4 refuses this setup too | (Node FontSample) Font Sample> Missing input Font |
| FontSampleParameter node | UE4 refuses this setup too | (Node FontSampleParameter) Font Param> Missing input Font |
| Frac node | compiles, not seen in the game yet |  |
| Fresnel node | compiles, not seen in the game yet |  |
| GIReplace node | compiles, not seen in the game yet |  |
| GetMaterialAttributes node | compiles, not seen in the game yet |  |
| HairAttributes node | compiles, not seen in the game yet |  |
| HairColor node | compiles, not seen in the game yet |  |
| If node | compiles, not seen in the game yet |  |
| InverseLinearInterpolate node | compiles, not seen in the game yet |  |
| KadanGameCustom node | compiles, not seen in the game yet |  |
| KadanGroundSoftAlpha node | compiles, not seen in the game yet |  |
| KadanReflectionCustom node | UE4 refuses this setup too | (Node KadanReflectionCustom) Original reflection Custom has no supported original reflection access. |
| KadanSceneColor node | compiles, not seen in the game yet |  |
| KadanSceneDepth node | compiles, not seen in the game yet |  |
| LandscapeGrassOutput node | compiles, not seen in the game yet |  |
| LandscapeLayerBlend node | compiles, not seen in the game yet |  |
| LandscapeLayerCoords node | compiles, not seen in the game yet |  |
| LandscapeLayerSample node | compiles, not seen in the game yet |  |
| LandscapeLayerSwitch node | compiles, not seen in the game yet |  |
| LandscapeLayerWeight node | compiles, not seen in the game yet |  |
| LandscapePhysicalMaterialOutput node | compiles, not seen in the game yet |  |
| LandscapeVisibilityMask node | compiles, not seen in the game yet |  |
| LightVector node | UE4 refuses this setup too | (Node LightVector) LightVector can only be used in LightFunction or DeferredDecal materials |
| LightmapUVs node | compiles, not seen in the game yet |  |
| LightmassReplace node | compiles, not seen in the game yet |  |
| LinearInterpolate node | compiles, not seen in the game yet |  |
| Logarithm10 node | compiles, not seen in the game yet |  |
| Logarithm2 node | compiles, not seen in the game yet |  |
| MakeMaterialAttributes node | compiles, not seen in the game yet |  |
| MapARPassthroughCameraUV node | compiles, not seen in the game yet |  |
| MaterialAttributeLayers node | compiles, not seen in the game yet |  |
| MaterialFunctionCall node | compiles, not seen in the game yet |  |
| MaterialLayerOutput node | compiles, not seen in the game yet |  |
| MaterialProxyReplace node | compiles, not seen in the game yet |  |
| Max node | compiles, not seen in the game yet |  |
| Min node | compiles, not seen in the game yet |  |
| Multiply node | compiles, not seen in the game yet |  |
| Noise node | compiles, not seen in the game yet |  |
| Normalize node | compiles, not seen in the game yet |  |
| ObjectBounds node | compiles, not seen in the game yet |  |
| ObjectOrientation node | compiles, not seen in the game yet |  |
| ObjectPositionWS node | compiles, not seen in the game yet |  |
| ObjectRadius node | compiles, not seen in the game yet |  |
| OneMinus node | compiles, not seen in the game yet |  |
| Panner node | compiles, not seen in the game yet |  |
| Parameter node | compiles, not seen in the game yet |  |
| ParticleColor node | compiles, not seen in the game yet |  |
| ParticleDirection node | compiles, not seen in the game yet |  |
| ParticleMacroUV node | compiles, not seen in the game yet |  |
| ParticleMotionBlurFade node | compiles, not seen in the game yet |  |
| ParticlePositionWS node | compiles, not seen in the game yet |  |
| ParticleRadius node | compiles, not seen in the game yet |  |
| ParticleRandom node | compiles, not seen in the game yet |  |
| ParticleRelativeTime node | compiles, not seen in the game yet |  |
| ParticleSize node | compiles, not seen in the game yet |  |
| ParticleSpeed node | compiles, not seen in the game yet |  |
| ParticleSubUV node | compiles, not seen in the game yet |  |
| ParticleSubUVProperties node | compiles, not seen in the game yet |  |
| PerInstanceCustomData node | UE4 refuses this setup too | (Node PerInstanceCustomData) Invalid node used in pixel/hull/domain shader input! |
| PerInstanceFadeAmount node | compiles, not seen in the game yet |  |
| PerInstanceRandom node | compiles, not seen in the game yet |  |
| PixelDepth node | compiles, not seen in the game yet |  |
| PixelNormalWS node | compiles, not seen in the game yet |  |
| Power node | compiles, not seen in the game yet |  |
| PreSkinnedLocalBounds node | compiles, not seen in the game yet |  |
| PreSkinnedNormal node | UE4 refuses this setup too | (Node PreSkinnedNormal) Pre-skinned normal is only available in the vertex shader, pass through custom interpolators if needed. |
| PreSkinnedPosition node | UE4 refuses this setup too | (Node PreSkinnedPosition) Pre-skinned position is only available in the vertex shader, pass through custom interpolators if needed. |
| PrecomputedAOMask node | compiles, not seen in the game yet |  |
| PreviousFrameSwitch node | compiles, not seen in the game yet |  |
| QualitySwitch node | compiles, not seen in the game yet |  |
| RayTracingQualitySwitch node | compiles, not seen in the game yet |  |
| ReflectionCapturePassSwitch node | compiles, not seen in the game yet |  |
| ReflectionVectorWS node | compiles, not seen in the game yet |  |
| Reroute node | compiles, not seen in the game yet |  |
| RotateAboutAxis node | compiles, not seen in the game yet |  |
| Rotator node | compiles, not seen in the game yet |  |
| Round node | compiles, not seen in the game yet |  |
| RuntimeVirtualTextureOutput node | compiles, not seen in the game yet |  |
| RuntimeVirtualTextureReplace node | compiles, not seen in the game yet |  |
| RuntimeVirtualTextureSample node | UE4 refuses this setup too | (Node RuntimeVirtualTextureSample) Missing input Virtual Texture |
| RuntimeVirtualTextureSampleParameter node | UE4 refuses this setup too | (Node RuntimeVirtualTextureSampleParameter) Missing input Virtual Texture |
| Saturate node | compiles, not seen in the game yet |  |
| ScalarParameter node | compiles, not seen in the game yet |  |
| SceneColor node | UE4 refuses this setup too | Only translucent materials can use the scene color node. |
| SceneDepth node | UE4 refuses this setup too | Only transparent or postprocess materials can read from scene depth. |
| SceneDepthWithoutWater node | UE4 refuses this setup too | (Node SceneDepthWithoutWater) Can only read scene depth below water when material Shading Model is Single Layer Water. |
| SceneTexelSize node | compiles, not seen in the game yet |  |
| SceneTexture node | UE4 refuses this setup too | Only translucent materials can use the scene color node. \| SceneTexture expressions cannot be used in opaque materials |
| ScreenPosition node | compiles, not seen in the game yet |  |
| SetMaterialAttributes node | compiles, not seen in the game yet |  |
| ShaderStageSwitch node | compiles, not seen in the game yet |  |
| ShadingModel node | UE4 refuses this setup too | Cannot force a cast between non-numeric types. |
| ShadingPathSwitch node | compiles, not seen in the game yet |  |
| ShadowReplace node | compiles, not seen in the game yet |  |
| Sign node | compiles, not seen in the game yet |  |
| Sine node | compiles, not seen in the game yet |  |
| SingleLayerWaterMaterialOutput node | compiles, not seen in the game yet |  |
| SkinningVertexOffsets node | UE4 refuses this setup too | (Node SkinningVertexOffsets) Pre Skin Offset only available in the vertex shader, pass through custom interpolators if needed. |
| SkyAtmosphereAerialPerspective node | compiles, not seen in the game yet |  |
| SkyAtmosphereDistantLightScatteredLuminance node | compiles, not seen in the game yet |  |
| SkyAtmosphereLightDirection node | compiles, not seen in the game yet |  |
| SkyAtmosphereLightDiskLuminance node | compiles, not seen in the game yet |  |
| SkyAtmosphereLightIlluminance node | compiles, not seen in the game yet |  |
| SkyAtmosphereViewLuminance node | compiles, not seen in the game yet |  |
| SmoothStep node | compiles, not seen in the game yet |  |
| Sobol node | compiles, not seen in the game yet |  |
| SpeedTree node | UE4 refuses this setup too | (Node SpeedTree) Invalid node used in pixel/hull/domain shader input! |
| SphereMask node | compiles, not seen in the game yet |  |
| SphericalParticleOpacity node | UE4 refuses this setup too | Only transparent or postprocess materials can read from scene depth. |
| SpriteTextureSampler node | compiles, not seen in the game yet |  |
| SquareRoot node | compiles, not seen in the game yet |  |
| StaticBool node | UE4 refuses this setup too | Cannot force a cast between non-numeric types. |
| StaticBoolParameter node | UE4 refuses this setup too | Cannot force a cast between non-numeric types. |
| StaticComponentMaskParameter node | UE4 refuses this setup too | (Node StaticComponentMaskParameter) Couldn't determine result type of component mask 0000 |
| StaticSwitch node | compiles, not seen in the game yet |  |
| StaticSwitchParameter node | compiles, not seen in the game yet |  |
| Step node | compiles, not seen in the game yet |  |
| Subtract node | compiles, not seen in the game yet |  |
| Tangent node | compiles, not seen in the game yet |  |
| TangentOutput node | compiles, not seen in the game yet |  |
| TemporalSobol node | compiles, not seen in the game yet |  |
| TextureCoordinate node | compiles, not seen in the game yet |  |
| TextureObject node | UE4 refuses this setup too | Cannot force a cast between non-numeric types. |
| TextureObjectParameter node | UE4 refuses this setup too | Cannot force a cast between non-numeric types. |
| TextureProperty node | UE4 refuses this setup too | (Node TextureProperty) Texture size only available for Texture2D, TextureVirtual, and VolumeTexture, not float2 |
| TextureSample node | compiles, not seen in the game yet |  |
| TextureSampleParameter2D node | compiles, not seen in the game yet |  |
| TextureSampleParameter2DArray node | UE4 refuses this setup too | (Node TextureSampleParameter2DArray) Param2DArray> 2D array sample needs UVW input |
| TextureSampleParameterCube node | UE4 refuses this setup too | (Node TextureSampleParameterCube) ParamCube> Cube sample needs UV input |
| TextureSampleParameterSubUV node | compiles, not seen in the game yet |  |
| TextureSampleParameterVolume node | UE4 refuses this setup too | (Node TextureSampleParameterVolume) ParamVolume> Volume sample needs UVW input |
| ThinTranslucentMaterialOutput node | compiles, not seen in the game yet |  |
| Time node | compiles, not seen in the game yet |  |
| Transform node | compiles, not seen in the game yet |  |
| TransformPosition node | compiles, not seen in the game yet |  |
| Truncate node | compiles, not seen in the game yet |  |
| TwoSidedSign node | compiles, not seen in the game yet |  |
| VectorNoise node | compiles, not seen in the game yet |  |
| VectorParameter node | compiles, not seen in the game yet |  |
| VertexColor node | compiles, not seen in the game yet |  |
| VertexInterpolator node | compiles, not seen in the game yet |  |
| VertexNormalWS node | compiles, not seen in the game yet |  |
| VertexTangentWS node | compiles, not seen in the game yet |  |
| ViewProperty node | compiles, not seen in the game yet |  |
| ViewSize node | compiles, not seen in the game yet |  |
| VirtualTextureFeatureSwitch node | compiles, not seen in the game yet |  |
| VolumetricAdvancedMaterialInput node | compiles, not seen in the game yet |  |
| VolumetricAdvancedMaterialOutput node | compiles, not seen in the game yet |  |
| WorldPosition node | compiles, not seen in the game yet |  |


// Kadan's record of what the game's environment controller and a few native classes do to a map when it is
// constructed or arrives in a level, read from the game's code (P4NativeEnvironmentPreview.cpp reads it).
// Kept in the plugin itself, so no installed file can be missing.
R"kadan({
  "format": "kadan-native-environment-v1",
  "source": "What the game's environment controller (AEnvController, /Script/Carrot4.EnvController) applies to its components from its scene, read from the game's code (FUN_01F61170: static pass FUN_01F675C0, lighting pass FUN_01F6BDC0, PostLoad 0x1F62D90). The game applies it on arrival in a level; its own editor did it in the viewport. Kadan applies it to an opened map (P4NativeEnvironmentPreview.cpp). Paths name the actor's properties: a section of the scene, or a component and its property. A value of the scene's FEnvParamFloat or FEnvParamLinearColor is its curve at the time when its curve is on, else its Value.",
  "class": "/Script/Carrot4.EnvController",
  "scene": "EnvParamSceneDay",
  "time": 0.5,
  "time_override": { "when": "bOverrideGameTime", "time": "OverrideGameTimeRatio" },
  "collection": "EnvMPC",
  "static": ["StaticSunLight"],
  "steps": [
    { "value": "EnvParamLighting.DirectionalLightIntensity", "min": 0, "set": "SunLight.Intensity" },
    { "value": "EnvParamLighting.DirectionalLightColor", "srgb": "EnvParamLighting.bDirectionalLightColorSRGB", "set": "SunLight.LightColor" },
    { "value": "EnvParamLighting.DirectionalLightIndirectIntensity", "set": "SunLight.IndirectLightingIntensity" },
    { "value": "EnvParamLighting.StaticDirectionalLightIntensity", "min": 0, "set": "StaticSunLight.Intensity" },
    { "value": "EnvParamLighting.StaticDirectionalLightColor", "srgb": true, "set": "StaticSunLight.LightColor" },
    { "value": "EnvParamLighting.StaticDirectionalLightIndirectIntensity", "set": "StaticSunLight.IndirectLightingIntensity" },
    { "value": "EnvParamLighting.SkyLightSourceType", "set": "SkyLight.SourceType" },
    { "value": "EnvParamLighting.SkyLightCubemap", "set": "SkyLight.Cubemap" },
    { "value": "EnvParamLighting.SkyLightIntensity", "min": 0, "set": "SkyLight.Intensity" },
    { "value": "EnvParamLighting.SkyLightColor", "srgb": false, "set": "SkyLight.LightColor" },
    { "value": "EnvParamLighting.SkyLightIndirectIntensity", "set": "SkyLight.IndirectLightingIntensity" },
    { "value": "EnvParamLighting.bEnableLowerHemisphereSolidColor", "set": "SkyLight.bLowerHemisphereIsBlack" },
    { "value": "EnvParamLighting.LowerHemisphereSolidColor", "set": "SkyLight.LowerHemisphereColor" },
    { "value": "EnvParamLighting.IndirectDiffuseLightingColor", "set": "PostProcess.Settings.IndirectLightingColor" },
    { "value": "EnvParamLighting.IndirectDiffuseIntensity", "set": "PostProcess.Settings.IndirectLightingIntensity" },
    { "value": "EnvParamFog.bEnable", "set": "HeightFog.bVisible" },
    { "value": "EnvParamFog.Density", "scale": "FogDensityScale", "set": "HeightFog.FogDensity" },
    { "value": "EnvParamFog.MaxOpacity", "set": "HeightFog.FogMaxOpacity" },
    { "value": "EnvParamFog.HeightFalloff", "set": "HeightFog.FogHeightFalloff" },
    { "value": "EnvParamFog.InscatteringColor", "set": "HeightFog.FogInscatteringColor" },
    { "value": "EnvParamFog.StartDistance", "set": "HeightFog.StartDistance" },
    { "value": "EnvParamFog.CutoffDistance", "set": "HeightFog.FogCutoffDistance" },
    { "value": "EnvParamFog.DirectionalInscatteringColor", "set": "HeightFog.DirectionalInscatteringColor" },
    { "value": "EnvParamFog.DirectionalInscatteringExponent", "set": "HeightFog.DirectionalInscatteringExponent" },
    { "value": "EnvParamFog.DirectionalInscatteringStartDistance", "set": "HeightFog.DirectionalInscatteringStartDistance" },
    { "value": "EnvParamMPC.EnvParamMPCEmissive.EmissiveIntensity", "scalar": "EmissiveIntensity" },
    { "value": "EnvParamMPC.EnvParamMPCEmissive.GameEmissiveIntensity", "scalar": "Game_EmissiveIntensity" },
    { "value": "EnvParamMPC.EnvParamMPCEmissive.EffectEmissiveIntensity", "scalar": "Eft_EmissiveIntensity" },
    { "value": "EnvParamMPC.EnvParamMPCAO.AOParam0", "when": "EnvParamMPC.EnvParamMPCAO.bEnableAOParam0", "otherwise": 1, "scalar": "AOParam0" },
    { "value": "EnvParamShadow.DynamicShadowCascadesNum", "set": "SunLight.DynamicShadowCascades" },
    { "value": "EnvParamProjectionShadow.LightFunctionMaterial", "set": "SunLight.LightFunctionMaterial" },
    { "value": "EnvParamProjectionShadow.C4Texture0", "set": "SunLight.C4Texture0" },
    { "value": "EnvParamProjectionShadow.LightFunctionFadeDistance", "set": "SunLight.LightFunctionFadeDistance" },
    { "value": "EnvParamSky.SkyMaterial", "keep_none": true, "set": "CarrotSkySphere.SkyMaterial" },
    { "value": "EnvParamSky.OverallIntensity", "set": "CarrotSkySphere.OverallIntensity" },
    { "value": "EnvParamSky.ZenithColor", "set": "CarrotSkySphere.ZenithColor" },
    { "value": "EnvParamSky.HorizonColor", "set": "CarrotSkySphere.HorizonColor" },
    { "value": "EnvParamSky.LowerHemisphereColor", "set": "CarrotSkySphere.LowerHemisphereColor" },
    { "value": "EnvParamSky.HorizonFalloff", "set": "CarrotSkySphere.HorizonFalloff" },
    { "value": "EnvParamSky.EmissionTintColor", "set": "CarrotSkySphere.SkyEmissionTintColor" },
    { "value": "EnvParamSky.CloudColor", "set": "CarrotSkySphere.CloudColor" },
    { "value": "EnvParamSky.CloudOpacity", "set": "CarrotSkySphere.CloudOpacity" },
    { "value": "EnvParamSky.SunDiscColor", "set": "CarrotSkySphere.SunDiscColor" },
    { "value": "EnvParamSky.SunDiscBrightness", "set": "CarrotSkySphere.SunDiscBrightness" },
    { "value": "EnvParamSky.SunDiscRadius", "set": "CarrotSkySphere.SunDiscRadius" },
    { "value": "EnvParamSky.SunDiscInscatteringColor", "set": "CarrotSkySphere.SunDiscInscatteringColor" },
    { "value": "EnvParamSky.SunDiscPhaseG", "set": "CarrotSkySphere.SunDiscPhaseG" },
    { "value": "EnvParamSky.StarsBrightness", "set": "CarrotSkySphere.StarsBrightness" }
  ],
  "sun": {
    "light": "SunLight",
    "altitude": "EnvParamLighting.DirectionalLightAltitudeAngle",
    "azimuth": "EnvParamLighting.DirectionalLightAzimuthAngle",
    "ignore_offsets": "EnvParamLighting.bIgnoreDirectionLightAngleOffset",
    "altitude_offset": "DirectionalLightAltitudeAngleOffset",
    "azimuth_offset": "DirectionalLightAzimuthAngleOffset",
    "override": "bOverrideDirectionalLightAngle",
    "override_altitude": "OverrideDirectionalLightAltitudeAngle",
    "override_azimuth": "OverrideDirectionalLightAzimuthAngle",
    "pitch_add": -180,
    "yaw_add": 180,
    "also_pitch": "CarrotSkySphere.SunDirectionPitch",
    "also_yaw": "CarrotSkySphere.SunDirectionYaw"
  },
  "collection_vectors": [
    { "vector": "ShadowProjectionParams01", "parts": [
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionSlopeBias.R",
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionSlopeBias.G",
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionSwingWidthScale",
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionTextureAlignedSize" ] },
    { "vector": "ShadowProjectionParams02", "parts": [
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionCenterPosition.R",
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionCenterPosition.G",
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionOpacity",
      "EnvParamMPC.EnvParamMPCShadow.ShadowProjectionCenterPosition.A" ] }
  ],
  "light_colors": [
    { "light": "SunLight", "vector": "SunLightColor" },
    { "light": "SkyLight", "vector": "SkyLightColor" }
  ],
  "recapture": "SkyLight",
  "material_instances_source": "What a class's construction gives one of its meshes, after the controllers above: an instance of the material a property names, on a slot of the component of that name, with parameters from the actor's properties (scale multiplies; direction is the unit vector of a (pitch, yaw) rotation, w = 1; a vector is four terms, each a property times scale and times another property; a render target is made in a format, cleared to a color and sized by two terms, and left as construction leaves it). ACarrotSkySphere::OnConstruction 0x1F5B2A0 (FUN_01F5B6A0, FUN_01F5BB10); AWaterBox::SetupRenderTargets 0xD17970 (in the editor too: the height field normal target RG16f, ClearRenderTarget2D (0, 0, 1, 0), so a flat surface until the game draws ripples into it).",
  "material_instances": [
    {
      "class": "/Script/Carrot4.CarrotSkySphere",
      "component": "SkySphereMesh",
      "slot": 0,
      "material": "SkyMaterial",
      "parameters": [
        { "name": "Zenith color", "value": "ZenithColor" },
        { "name": "Horizon color", "value": "HorizonColor" },
        { "name": "Lower Hemisphere Color", "value": "LowerHemisphereColor" },
        { "name": "Horizon falloff", "value": "HorizonFalloff" },
        { "name": "SkyEmissionTintColor", "value": "SkyEmissionTintColor" },
        { "name": "Sun color", "value": "SunDiscColor" },
        { "name": "Sun Brightness", "value": "SunDiscBrightness" },
        { "name": "Sun Radius", "value": "SunDiscRadius", "scale": 0.01 },
        { "name": "SunInscatteringColor", "value": "SunDiscInscatteringColor" },
        { "name": "SunPhaseG", "value": "SunDiscPhaseG" },
        { "name": "Light direction", "direction": ["SunDirectionPitch", "SunDirectionYaw"] },
        { "name": "Cloud color", "value": "CloudColor" },
        { "name": "Cloud opacity", "value": "CloudOpacity" },
        { "name": "OverallIntensity", "value": "OverallIntensity" },
        { "name": "Stars Brightness", "value": "StarsBrightness" }
      ]
    },
    {
      "class": "/Script/Carrot4.WATERBOX",
      "component": "WaterSurface",
      "slot": 0,
      "material": "WaterMaterial",
      "parameters": [
        { "name": "TexRippleParams", "vector": [
          { "value": "HeightMapWidth", "scale": 0.00390625, "times": "TexRippleUVScale" },
          { "value": "HeightMapHeight", "scale": 0.00390625, "times": "TexRippleUVScale" },
          { "value": "TexRippleScrollSpeed" },
          { "value": "TexRipplePower" }
        ] },
        { "name": "Heightfield Normal", "render_target": { "format": "RG16f", "clear": [0, 0, 1, 0],
          "width": { "value": "HeightMapWidth" }, "height": { "value": "HeightMapHeight" } } }
      ]
    },
    {
      "class": "/Script/Carrot4.WATERBOX",
      "component": "WaterSurface",
      "slot": 1,
      "material": "WaterToAirMaterial",
      "parameters": [
        { "name": "Heightfield Normal", "render_target": { "format": "RG16f", "clear": [0, 0, 1, 0],
          "width": { "value": "HeightMapWidth" }, "height": { "value": "HeightMapHeight" } } }
      ]
    }
  ]
})kadan"

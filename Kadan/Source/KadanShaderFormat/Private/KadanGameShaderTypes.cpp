// The game's own shader classes that UE 4.26 has no class for, compiled for the game only (SP_SWITCH).
//
// The game draws a material's shadow depth with TC4ShadowDepthVSVertexShadowDepth_<mode><0|1> where stock UE has
// TShadowDepthVSVertexShadowDepth_<mode>false: the game's renderer looks them up by those names, and a masked (or
// position-moving) material without them stops the game ("Material not found"). Each is stock's vertex shader
// (ShadowDepthVertexShader.usf, Main, with the game's slope clamp from KadanSwitchSourcePatches.inl); class 1 adds
// the game's loose bias to the depth biases (KADAN_C4ShadowDepthBias). Their objects hold stock's LayerId and then
// the bias, as the game's do. The export removes stock's classes from the map (P4SwitchShaderMapFreeze).
//
// Decals and light functions likewise: the game has FDeferredDecalPS<custom depth><distance fade> and
// FDeferredDecalEmissivePS<..> (00 to 11) where stock has FDeferredDecalPS and FDeferredDecalEmissivePS, and
// FLightFunctionPS_<custom depth><depth branch> (False/True) where stock has FLightFunctionPS. Each is stock's
// shader with stock's compilation environment and the game's two defines, also given as KADAN_C4_* (the format
// passes KADAN_ defines to DXC; KadanSwitchSourcePatches.inl reads them). The decal objects hold stock's five
// parameters and the game's C4DistanceFadeParams; the light function objects stock's three. (Read from the game's
// registrations, _INIT_509 and _INIT_535, and their constructors.)
//
// Shader types must exist before UE initializes them, so this module loads at PostConfigInit.
#include "CoreMinimal.h"
#include "DecalRenderingCommon.h"
#include "MaterialShaderType.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "MeshMaterialShader.h"
#include "MeshMaterialShaderType.h"
#include "Serialization/MemoryLayout.h"
#include <array>
#include <utility>
#include "RHIDefinitions.h"
#include "ShaderParameters.h"

enum class EKadanShadowDepthMode : uint8
{
    PerspectiveCorrect,
    OutputDepth,
    OnePassPointLight
};

class FKadanC4ShadowDepthVS : public FMeshMaterialShader
{
    DECLARE_INLINE_TYPE_LAYOUT(FKadanC4ShadowDepthVS, NonVirtual);

public:
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters&) { return false; }

    FKadanC4ShadowDepthVS() {}
    FKadanC4ShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FMeshMaterialShader(Initializer)
    {
        PassUniformBuffer.Bind(Initializer.ParameterMap, TEXT("ShadowDepthPass"));
        LayerId.Bind(Initializer.ParameterMap, TEXT("LayerId"));
        C4ShadowDepthBias.Bind(Initializer.ParameterMap, TEXT("C4ShadowDepthBias"));
    }

private:
    LAYOUT_FIELD(FShaderParameter, LayerId);
    LAYOUT_FIELD(FShaderParameter, C4ShadowDepthBias);
};

template <EKadanShadowDepthMode Mode, uint32 Bias>
class TKadanC4ShadowDepthVS : public FKadanC4ShadowDepthVS
{
    DECLARE_SHADER_TYPE(TKadanC4ShadowDepthVS, MeshMaterial);

public:
    TKadanC4ShadowDepthVS() {}
    TKadanC4ShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FKadanC4ShadowDepthVS(Initializer)
    {}

    // Where stock compiles TShadowDepthVS<Mode, false, false> (ShadowDepthRendering.cpp), for the game only.
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    {
        const auto& Material = Parameters.MaterialParameters;
        return Parameters.Platform == SP_SWITCH &&
            (Material.bIsSpecialEngineMaterial || !Material.bWritesEveryPixelShadowPass ||
                Material.bMaterialMayModifyMeshPosition) &&
            (Mode != EKadanShadowDepthMode::OnePassPointLight ||
                IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)) &&
            Material.bShouldCastDynamicShadows;
    }

    static void ModifyCompilationEnvironment(
        const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(
            TEXT("PERSPECTIVE_CORRECT_DEPTH"), uint32(Mode == EKadanShadowDepthMode::PerspectiveCorrect));
        Environment.SetDefine(
            TEXT("ONEPASS_POINTLIGHT_SHADOW"), uint32(Mode == EKadanShadowDepthMode::OnePassPointLight));
        Environment.SetDefine(TEXT("REFLECTIVE_SHADOW_MAP"), 0u);
        Environment.SetDefine(TEXT("POSITION_ONLY"), 0u);
        Environment.SetDefine(TEXT("IS_FOR_GEOMETRY_SHADER"), 0u);
        Environment.SetDefine(TEXT("KADAN_C4ShadowDepthBias"), Bias);
    }
};

#define KADAN_C4_SHADOW_DEPTH_VS(ModeName, Bias)                                                    \
    typedef TKadanC4ShadowDepthVS<EKadanShadowDepthMode::ModeName, Bias>                            \
        TC4ShadowDepthVSVertexShadowDepth_##ModeName##Bias;                                         \
    IMPLEMENT_MATERIAL_SHADER_TYPE(template <>, TC4ShadowDepthVSVertexShadowDepth_##ModeName##Bias, \
        TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("Main"), SF_Vertex);

KADAN_C4_SHADOW_DEPTH_VS(PerspectiveCorrect, 0)
KADAN_C4_SHADOW_DEPTH_VS(PerspectiveCorrect, 1)
KADAN_C4_SHADOW_DEPTH_VS(OutputDepth, 0)
KADAN_C4_SHADOW_DEPTH_VS(OutputDepth, 1)
KADAN_C4_SHADOW_DEPTH_VS(OnePassPointLight, 0)
KADAN_C4_SHADOW_DEPTH_VS(OnePassPointLight, 1)

// Stock's FDecalRenderingCommon::ComputeFinalDecalBlendMode for the game: the game shades forward with DBuffer
// decals, so every decal becomes a DBuffer decal. Stock reads that from the editor's settings for the platform
// (r.ForwardShading, r.DBuffer of the project, as there is no Switch target platform), which a creator's project
// need not have: without them a decal compiles as a deferred GBuffer decal, which the game's renderer cannot use.
static EDecalBlendMode KadanFinalDecalBlendMode(EDecalBlendMode Mode, bool bHasNormal)
{
    switch (Mode)
    {
        case DBM_AlphaComposite:
            Mode = DBM_DBuffer_AlphaComposite;
            break;
        case DBM_Stain:
        case DBM_Translucent:
            Mode = DBM_DBuffer_ColorNormalRoughness;
            break;
        case DBM_Normal:
            Mode = DBM_DBuffer_Normal;
            break;
        case DBM_Emissive:
            Mode = DBM_DBuffer_Emissive;
            break;
        default:
            break;
    }
    if (!bHasNormal)
    {
        if (Mode == DBM_DBuffer_ColorNormalRoughness)
            Mode = DBM_DBuffer_ColorRoughness;
        else if (Mode == DBM_DBuffer_NormalRoughness)
            Mode = DBM_DBuffer_Roughness;
    }
    return Mode;
}

// Stock's decal compilation environment (FDecalRendering::SetDecalCompilationEnvironment and the emissive one,
// DecalRenderingShared.cpp, not exported), which the game's classes call before their own defines.
static uint8 KadanDecalDBufferMask(EDecalBlendMode Mode)
{
    switch (Mode)
    {
        case DBM_DBuffer_AlphaComposite:
            return 1 + 4;
        case DBM_DBuffer_ColorNormalRoughness:
            return 1 + 2 + 4;
        case DBM_DBuffer_Emissive:
        case DBM_DBuffer_EmissiveAlphaComposite:
        case DBM_DBuffer_Color:
            return 1;
        case DBM_DBuffer_ColorNormal:
            return 1 + 2;
        case DBM_DBuffer_ColorRoughness:
            return 1 + 4;
        case DBM_DBuffer_Normal:
            return 2;
        case DBM_DBuffer_NormalRoughness:
            return 2 + 4;
        case DBM_DBuffer_Roughness:
            return 4;
        default:
            return 0;
    }
}

static void KadanDecalEnvironment(
    const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
{
    const bool bHasNormal = Parameters.MaterialParameters.bHasNormalConnected;
    const EDecalBlendMode Mode = KadanFinalDecalBlendMode(
        (EDecalBlendMode)Parameters.MaterialParameters.DecalBlendMode, bHasNormal);
    const EDecalRenderStage Stage = FDecalRenderingCommon::ComputeRenderStage(Parameters.Platform, Mode);
    const FDecalRenderingCommon::ERenderTargetMode Targets = FDecalRenderingCommon::ComputeRenderTargetMode(
        Parameters.Platform, Mode, bHasNormal);
    const bool bNoNormal = Targets == FDecalRenderingCommon::RTM_SceneColorAndGBufferNoNormal ||
        Targets == FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteNoNormal;
    Environment.SetDefine(TEXT("DECAL_OUTPUT_NORMAL"), uint32(bNoNormal ? 0 : 1));
    Environment.SetDefine(TEXT("DECAL_BLEND_MODE"), uint32(Mode));
    Environment.SetDefine(TEXT("DECAL_PROJECTION"), 1u);
    Environment.SetDefine(TEXT("DECAL_RENDERTARGET_COUNT"),
        FDecalRenderingCommon::ComputeRenderTargetCount(Parameters.Platform, Targets));
    Environment.SetDefine(TEXT("DECAL_RENDERSTAGE"), uint32(Stage));
    const uint8 Mask = KadanDecalDBufferMask(Mode);
    Environment.SetDefine(TEXT("MATERIAL_DBUFFERA"), uint32((Mask & 1) != 0));
    Environment.SetDefine(TEXT("MATERIAL_DBUFFERB"), uint32((Mask & 2) != 0));
    Environment.SetDefine(TEXT("MATERIAL_DBUFFERC"), uint32((Mask & 4) != 0));
    // The game's mask target (KadanSwitchSourcePatches.inl): the buffers the decal writes, and its blend mode, 1 for a
    // stain (which the game's base pass multiplies; stock UE draws a DBuffer stain as a translucent decal).
    Environment.SetDefine(TEXT("KADAN_DECAL_DBUFFER_MASK"), uint32(Mask));
    Environment.SetDefine(
        TEXT("KADAN_DECAL_C4_MODE"), uint32(Parameters.MaterialParameters.DecalBlendMode == DBM_Stain ? 1 : 0));
}

static void KadanEmissiveDecalEnvironment(FShaderCompilerEnvironment& Environment)
{
    Environment.SetDefine(TEXT("DECAL_OUTPUT_NORMAL"), 0u);
    Environment.SetDefine(TEXT("DECAL_BLEND_MODE"), uint32(DBM_DBuffer_Emissive));
    Environment.SetDefine(TEXT("DECAL_RENDERTARGET_COUNT"), 1u);
    Environment.SetDefine(TEXT("DECAL_RENDERSTAGE"), uint32(DRS_Emissive));
    Environment.SetDefine(TEXT("MATERIAL_DBUFFERA"), 0u);
    Environment.SetDefine(TEXT("MATERIAL_DBUFFERB"), 0u);
    Environment.SetDefine(TEXT("MATERIAL_DBUFFERC"), 0u);
    Environment.SetDefine(TEXT("KADAN_DECAL_DBUFFER_MASK"), 0u);
}

// FDeferredDecalPS<CustomDepth><DistanceFade>: stock's DeferredDecal.usf MainPS.
template <uint32 CustomDepth, uint32 DistanceFade>
class TKadanDeferredDecalPS : public FMaterialShader
{
    DECLARE_SHADER_TYPE(TKadanDeferredDecalPS, Material);

public:
    // As stock's: decal materials.
    static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
    {
        return Parameters.Platform == SP_SWITCH && Parameters.MaterialParameters.MaterialDomain == MD_DeferredDecal;
    }

    static void ModifyCompilationEnvironment(
        const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FMaterialShader::ModifyCompilationEnvironment(Parameters, Environment);
        KadanDecalEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("C4_USE_CUSTOM_DEPTH"), CustomDepth);
        Environment.SetDefine(TEXT("C4_USE_DISTANCE_FADE"), DistanceFade);
        Environment.SetDefine(TEXT("KADAN_C4_USE_CUSTOM_DEPTH"), CustomDepth);
        Environment.SetDefine(TEXT("KADAN_C4_USE_DISTANCE_FADE"), DistanceFade);
    }

    TKadanDeferredDecalPS() {}
    TKadanDeferredDecalPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FMaterialShader(Initializer)
    {
        SvPositionToDecal.Bind(Initializer.ParameterMap, TEXT("SvPositionToDecal"));
        DecalToWorld.Bind(Initializer.ParameterMap, TEXT("DecalToWorld"));
        WorldToDecal.Bind(Initializer.ParameterMap, TEXT("WorldToDecal"));
        DecalOrientation.Bind(Initializer.ParameterMap, TEXT("DecalOrientation"));
        DecalParams.Bind(Initializer.ParameterMap, TEXT("DecalParams"));
        C4DistanceFadeParams.Bind(Initializer.ParameterMap, TEXT("C4DistanceFadeParams"));
    }

private:
    LAYOUT_FIELD(FShaderParameter, SvPositionToDecal);
    LAYOUT_FIELD(FShaderParameter, DecalToWorld);
    LAYOUT_FIELD(FShaderParameter, WorldToDecal);
    LAYOUT_FIELD(FShaderParameter, DecalOrientation);
    LAYOUT_FIELD(FShaderParameter, DecalParams);
    LAYOUT_FIELD(FShaderParameter, C4DistanceFadeParams);
};

// FDeferredDecalEmissivePS<..>: derived from the decal class with the same digits, as stock's FDeferredDecalEmissivePS
// derives from FDeferredDecalPS (the game's frozen layouts hash that base in), for an emissive DBuffer decal only.
template <uint32 CustomDepth, uint32 DistanceFade>
class TKadanDeferredDecalEmissivePS : public TKadanDeferredDecalPS<CustomDepth, DistanceFade>
{
    DECLARE_SHADER_TYPE(TKadanDeferredDecalEmissivePS, Material);

public:
    using FDecalBase = TKadanDeferredDecalPS<CustomDepth, DistanceFade>;
    static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
    {
        const auto& Material = Parameters.MaterialParameters;
        return FDecalBase::ShouldCompilePermutation(Parameters) && Material.bHasEmissiveColorConnected &&
            IsDBufferDecalBlendMode(
                KadanFinalDecalBlendMode((EDecalBlendMode)Material.DecalBlendMode, Material.bHasNormalConnected));
    }

    static void ModifyCompilationEnvironment(
        const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FDecalBase::ModifyCompilationEnvironment(Parameters, Environment);
        KadanEmissiveDecalEnvironment(Environment);
    }

    TKadanDeferredDecalEmissivePS() {}
    TKadanDeferredDecalEmissivePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FDecalBase(Initializer)
    {}
};

#define KADAN_DEFERRED_DECAL_PS(Name, Class, CustomDepth, DistanceFade) \
    typedef Class<CustomDepth, DistanceFade> Name;                      \
    IMPLEMENT_MATERIAL_SHADER_TYPE(                                     \
        template <>, Name, TEXT("/Engine/Private/DeferredDecal.usf"), TEXT("MainPS"), SF_Pixel);

KADAN_DEFERRED_DECAL_PS(FDeferredDecalPS00, TKadanDeferredDecalPS, 0, 0)
KADAN_DEFERRED_DECAL_PS(FDeferredDecalPS01, TKadanDeferredDecalPS, 0, 1)
KADAN_DEFERRED_DECAL_PS(FDeferredDecalPS10, TKadanDeferredDecalPS, 1, 0)
KADAN_DEFERRED_DECAL_PS(FDeferredDecalPS11, TKadanDeferredDecalPS, 1, 1)
KADAN_DEFERRED_DECAL_PS(FDeferredDecalEmissivePS00, TKadanDeferredDecalEmissivePS, 0, 0)
KADAN_DEFERRED_DECAL_PS(FDeferredDecalEmissivePS01, TKadanDeferredDecalEmissivePS, 0, 1)
KADAN_DEFERRED_DECAL_PS(FDeferredDecalEmissivePS10, TKadanDeferredDecalEmissivePS, 1, 0)
KADAN_DEFERRED_DECAL_PS(FDeferredDecalEmissivePS11, TKadanDeferredDecalEmissivePS, 1, 1)

namespace KadanLayout
{
// Stock's FLightFunctionSharedParameters (the renderer's private LightRendering.h), which stock's light function object
// holds: a frozen layout's hash names each field's type, so the game's light function layouts need it by that name.
class FLightFunctionSharedParameters
{
    DECLARE_INLINE_TYPE_LAYOUT(FLightFunctionSharedParameters, NonVirtual);

public:
    void Bind(const FShaderParameterMap& ParameterMap)
    {
        LightFunctionParameters.Bind(ParameterMap, TEXT("LightFunctionParameters"));
    }

private:
    LAYOUT_FIELD(FShaderParameter, LightFunctionParameters);
};
} //namespace KadanLayout

// FLightFunctionPS_<CustomDepth><DepthBranch>: stock's LightFunctionPixelShader.usf Main.
template <uint32 CustomDepth, uint32 DepthBranch>
class TKadanLightFunctionPS : public FMaterialShader
{
    DECLARE_SHADER_TYPE(TKadanLightFunctionPS, Material);

public:
    static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
    {
        return Parameters.Platform == SP_SWITCH && Parameters.MaterialParameters.MaterialDomain == MD_LightFunction &&
            IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

    static void ModifyCompilationEnvironment(
        const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FMaterialShader::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("C4_USE_CUSTOM_DEPTH"), CustomDepth);
        Environment.SetDefine(TEXT("C4_USE_LIGHT_FUNCTION_DEPTH_BRANCH"), DepthBranch);
        Environment.SetDefine(TEXT("KADAN_C4_USE_CUSTOM_DEPTH"), CustomDepth);
        Environment.SetDefine(TEXT("KADAN_C4_USE_LIGHT_FUNCTION_DEPTH_BRANCH"), DepthBranch);
    }

    TKadanLightFunctionPS() {}
    TKadanLightFunctionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FMaterialShader(Initializer)
    {
        SvPositionToLight.Bind(Initializer.ParameterMap, TEXT("SvPositionToLight"));
        LightFunctionParameters.Bind(Initializer.ParameterMap);
        LightFunctionParameters2.Bind(Initializer.ParameterMap, TEXT("LightFunctionParameters2"));
    }

private:
    LAYOUT_FIELD(FShaderParameter, SvPositionToLight);
    LAYOUT_FIELD(KadanLayout::FLightFunctionSharedParameters, LightFunctionParameters);
    LAYOUT_FIELD(FShaderParameter, LightFunctionParameters2);
};

#define KADAN_LIGHT_FUNCTION_PS(Name, CustomDepth, DepthBranch)   \
    typedef TKadanLightFunctionPS<CustomDepth, DepthBranch> Name; \
    IMPLEMENT_MATERIAL_SHADER_TYPE(                               \
        template <>, Name, TEXT("/Engine/Private/LightFunctionPixelShader.usf"), TEXT("Main"), SF_Pixel);

KADAN_LIGHT_FUNCTION_PS(FLightFunctionPS_FalseFalse, 0, 0)
KADAN_LIGHT_FUNCTION_PS(FLightFunctionPS_FalseTrue, 0, 1)
KADAN_LIGHT_FUNCTION_PS(FLightFunctionPS_TrueFalse, 1, 0)
KADAN_LIGHT_FUNCTION_PS(FLightFunctionPS_TrueTrue, 1, 1)

// Stock classes the game has in a form of its own. Stock's FVolumetricFogLightFunctionPS compiles only where the platform reports volumetric fog
// (DoesPlatformSupportVolumetricFog, which no stock description of SP_SWITCH does); the game's light function maps hold
// it, since the game draws volumetric fog. For SP_SWITCH it compiles for every light function, as the game's; elsewhere
// stock's own rule. FShaderType's ShouldCompilePermutationRef and ModifyCompilationEnvironmentRef are private: reached
// through the explicit-instantiation idiom.
namespace
{
struct FShouldCompileRef
{
    using Type = FShaderType::ShouldCompilePermutationType FShaderType::*;
};
template <typename Tag>
struct TKadanMemberOf
{
    static typename Tag::Type Pointer;
};
template <typename Tag>
typename Tag::Type TKadanMemberOf<Tag>::Pointer = nullptr;
template <typename Tag, typename Tag::Type Member>
struct TKadanMemberGrant
{
    TKadanMemberGrant() { TKadanMemberOf<Tag>::Pointer = Member; }
    static TKadanMemberGrant Instance;
};
template <typename Tag, typename Tag::Type Member>
TKadanMemberGrant<Tag, Member> TKadanMemberGrant<Tag, Member>::Instance;
template struct TKadanMemberGrant<FShouldCompileRef, &FShaderType::ShouldCompilePermutationRef>;
struct FEnvironmentRef
{
    using Type = FShaderType::ModifyCompilationEnvironmentType FShaderType::*;
};
template struct TKadanMemberGrant<FEnvironmentRef, &FShaderType::ModifyCompilationEnvironmentRef>;
struct FConstructSerializedRef
{
    using Type = FShaderType::ConstructSerializedType FShaderType::*;
};
template struct TKadanMemberGrant<FConstructSerializedRef, &FShaderType::ConstructSerializedRef>;
struct FConstructCompiledRef
{
    using Type = FShaderType::ConstructCompiledType FShaderType::*;
};
template struct TKadanMemberGrant<FConstructCompiledRef, &FShaderType::ConstructCompiledRef>;
struct FValidateRef
{
    using Type = FShaderType::ValidateCompiledResultType FShaderType::*;
};
template struct TKadanMemberGrant<FValidateRef, &FShaderType::ValidateCompiledResultRef>;

FShaderType::ShouldCompilePermutationType GStockVolumetricFogLightFunction = nullptr;

bool KadanVolumetricFogLightFunctionShouldCompile(const FShaderPermutationParameters& Parameters)
{
    if (Parameters.Platform == SP_SWITCH)
        return static_cast<const FMaterialShaderPermutationParameters&>(Parameters).MaterialParameters.MaterialDomain ==
            MD_LightFunction;
    return GStockVolumetricFogLightFunction && GStockVolumetricFogLightFunction(Parameters);
}

// Stock's mesh decal pixel class (FMeshDecalsPS) keeps a stain or translucent decal a GBuffer decal and writes 1 to
// the decal mask target; the game's is a DBuffer decal, as all its decals are, and writes its mask (the decal buffers
// in red, the blend mode in alpha). For SP_SWITCH the game's decal environment, after stock's.
FShaderType::ModifyCompilationEnvironmentType GStockMeshDecals = nullptr;

void KadanMeshDecalsEnvironment(const FShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
{
    if (GStockMeshDecals)
        GStockMeshDecals(Parameters, Environment);
    if (Parameters.Platform != SP_SWITCH)
        return;
    KadanDecalEnvironment(static_cast<const FMaterialShaderPermutationParameters&>(Parameters), Environment);
}
} //namespace

bool KadanCompileGameOnlyStockShaders()
{
    FShaderType* Fog = FShaderType::GetShaderTypeByName(TEXT("FVolumetricFogLightFunctionPS"));
    FShaderType* MeshDecals = FShaderType::GetShaderTypeByName(TEXT("FMeshDecalsPS"));
    if (!Fog || !MeshDecals || !TKadanMemberOf<FShouldCompileRef>::Pointer || !TKadanMemberOf<FEnvironmentRef>::Pointer)
        return false;
    FShaderType::ShouldCompilePermutationType& Rule = Fog->*TKadanMemberOf<FShouldCompileRef>::Pointer;
    if (Rule != &KadanVolumetricFogLightFunctionShouldCompile)
    {
        GStockVolumetricFogLightFunction = Rule;
        Rule = &KadanVolumetricFogLightFunctionShouldCompile;
    }
    FShaderType::ModifyCompilationEnvironmentType& Environment = MeshDecals->*TKadanMemberOf<FEnvironmentRef>::Pointer;
    if (Environment != &KadanMeshDecalsEnvironment)
    {
        GStockMeshDecals = Environment;
        Environment = &KadanMeshDecalsEnvironment;
    }
    return true;
}

// The game's translucency passes (its MaterialSSR, under r.C4.MaterialSSR): for a translucent material with screen
// space reflections drawn with the scene colour behind it (C4MaterialDrawOrder XluUseSceneColor), the game draws the
// reflection in a pass of its own (XluPassSSR) and reads it back blurred in the material's pass (XluPassRecombine),
// with TBasePassPS<policy>[Skylight]XluPassSSR and ..XluPassRecombine for every base pass class. Each is stock's base
// pass class under the game's name: the game's frozen objects have stock's fields and layout sizes, and its
// environment is stock's with TRANSLUCENCY_PASS_TYPE 1 or 2 (0x407BB80, 0x407BE30). Here each is a shader type made
// from stock's base pass type when this module loads: stock's constructors and a copy of stock's layout under the
// game's name, the pass type added to stock's environment (also as KADAN_TRANSLUCENCY_PASS_TYPE, which
// KadanSwitchSourcePatches.inl reads), and the game's rule: stock's, for SP_SWITCH, a translucent blend mode and the
// game's material flag (bit 48 of its FMaterialShaderParameters flags, 0x40196D0), which stock UE's parameters do not
// have. The layout includes a deterministic superset; Kadan's owned material resource applies the flag in ShouldCache.
namespace KadanTranslucencyPasses
{
struct FStock
{
    FShaderType::ModifyCompilationEnvironmentType Environment = nullptr;
    FShaderType::ShouldCompilePermutationType ShouldCompile = nullptr;
    uint32 Pass = 0;
};
constexpr int32 MaxTypes = 128;
FStock GStock[MaxTypes];
// Legacy compile-context compatibility only: this state must not select a cached layout.
thread_local bool GMaterialSSR = false;

template <int32 I>
void Environment(const FShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Out)
{
    GStock[I].Environment(Parameters, Out);
    Out.SetDefine(TEXT("TRANSLUCENCY_PASS_TYPE"), GStock[I].Pass);
    Out.SetDefine(TEXT("KADAN_TRANSLUCENCY_PASS_TYPE"), GStock[I].Pass);
}

template <int32 I>
bool ShouldCompile(const FShaderPermutationParameters& Parameters)
{
    const FMaterialShaderParameters& Material =
        static_cast<const FMaterialShaderPermutationParameters&>(Parameters).MaterialParameters;
    // UE caches the layout by FMaterialShaderParameters, which has no SSR/C4 flag.
    // Keep this predicate deterministic; Kadan resources filter the superset in ShouldCache.
    return Parameters.Platform == SP_SWITCH && IsTranslucentBlendMode(Material.BlendMode) &&
        GStock[I].ShouldCompile(Parameters);
}

template <int32... Is>
constexpr std::array<FShaderType::ModifyCompilationEnvironmentType, sizeof...(Is)> Environments(
    std::integer_sequence<int32, Is...>)
{
    return {{&Environment<Is>...}};
}

template <int32... Is>
constexpr std::array<FShaderType::ShouldCompilePermutationType, sizeof...(Is)> Rules(
    std::integer_sequence<int32, Is...>)
{
    return {{&ShouldCompile<Is>...}};
}
} //namespace KadanTranslucencyPasses

void KadanSetMaterialSSR(bool bMaterialSSR)
{
    KadanTranslucencyPasses::GMaterialSSR = bMaterialSSR;
}

// Makes the game's translucency pass types: once, while this module loads (UE refuses shader types made after it
// initializes them). The number made, or -1 without stock's base pass types.
int32 KadanRegisterTranslucencyPassTypes()
{
    using namespace KadanTranslucencyPasses;
    static int32 Made = -1;
    if (Made >= 0)
        return Made;
    static const auto EnvironmentTable = Environments(std::make_integer_sequence<int32, MaxTypes>());
    static const auto RuleTable = Rules(std::make_integer_sequence<int32, MaxTypes>());
    TArray<FShaderType*> Stock;
    for (TLinkedList<FShaderType*>::TIterator It(FShaderType::GetTypeList()); It; It.Next())
    {
        FShaderType* Type = *It;
        const FString Name = Type->GetName();
        if (Type->GetMeshMaterialShaderType() && Type->GetFrequency() == SF_Pixel &&
            Name.StartsWith(TEXT("TBasePassPS"), ESearchCase::CaseSensitive) &&
            !Name.Contains(TEXT("XluPass"), ESearchCase::CaseSensitive))
            Stock.Add(Type);
    }
    if (!Stock.Num() || !TKadanMemberOf<FConstructSerializedRef>::Pointer)
        return -1;
    static TArray<FString> Names;
    Names.Reserve(MaxTypes);
    Made = 0;
    for (const TCHAR* Pass : {TEXT("XluPassSSR"), TEXT("XluPassRecombine")})
        for (FShaderType* Type : Stock)
        {
            if (Made >= MaxTypes)
                break;
            GStock[Made].Environment = Type->*TKadanMemberOf<FEnvironmentRef>::Pointer;
            GStock[Made].ShouldCompile = Type->*TKadanMemberOf<FShouldCompileRef>::Pointer;
            GStock[Made].Pass = FCString::Strcmp(Pass, TEXT("XluPassSSR")) == 0 ? 1 : 2;
            const FString& Name = Names.Add_GetRef(FString(Type->GetName()) + Pass);
            FTypeLayoutDesc* Layout = new FTypeLayoutDesc(Type->GetLayout());
            Layout->HashNext = nullptr;
            Layout->Name = *Name; // the shader type registers its layout (and the name's hash) itself
            new FMeshMaterialShaderType(*Layout, *Name, Type->GetShaderFilename(), Type->GetFunctionName(),
                Type->GetFrequency(), Type->GetPermutationCount(),
                Type->*TKadanMemberOf<FConstructSerializedRef>::Pointer,
                Type->*TKadanMemberOf<FConstructCompiledRef>::Pointer, EnvironmentTable[Made], RuleTable[Made],
                Type->*TKadanMemberOf<FValidateRef>::Pointer, Type->GetTypeSize(), Type->GetRootParametersMetadata());
            ++Made;
        }
    return Made;
}

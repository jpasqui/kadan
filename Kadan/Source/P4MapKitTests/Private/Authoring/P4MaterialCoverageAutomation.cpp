#if P4_MAPKIT_TESTS
// Material coverage survey (docs/MATERIAL_COVERAGE.md). One creator material per UE4.26 material feature: every
// domain, blend mode, shading model, Details setting, material input, texture type and expression node. Each is
// compiled twice: for the editor's own platform (what UE4 itself accepts) and for the game through Kadan's Switch
// format (what export ships). The report says which features the game route refuses that UE4 accepts.
//
// KADAN_COVERAGE_FAMILIES: comma list of families to run (default: every family except nodes; "all" adds nodes).
// KADAN_COVERAGE_ONLY: comma list of case ids (ThinTranslucent, TextureCube, ...): only those cases.
// KADAN_COVERAGE_SLICE=K/N: only every Nth expression node class from the Kth (0-based), so the nodes family fits in
// several runs.
// KADAN_COVERAGE_EXPORT=1: also export a mod with a cube and every material that compiled for the game, for the
// in-game gallery (tools/material_gallery.lua). The report and the gallery's layout go to
// Saved/P4MapKit/MaterialCoverage.
#include "P4ExportDriverTestShared.h"
#include "P4CreatorMaterialFactories.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/VolumeTexture.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionShadingModel.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/SkyLight.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

namespace
{
struct FCoverageContext
{
    UTexture2D* Texture2D = nullptr;
    UTextureCube* Cube = nullptr;
    UVolumeTexture* Volume = nullptr;
    UTexture2DArray* Array = nullptr;
    UTextureRenderTarget2D* Target = nullptr;
    UMaterialParameterCollection* Collection = nullptr;
};

struct FCoverageCase
{
    FString Id, Family, Feature;
    // Builds the graph on a fresh creator material. Returns false when the case cannot be built in this editor.
    TFunction<bool(UMaterial*, const FCoverageContext&)> Setup;
};

template <typename T> T* Node(UMaterial* Graph)
{
    T* Made = NewObject<T>(Graph);
    Made->Material = Graph;
    Graph->Expressions.Add(Made);
    return Made;
}
UMaterialExpressionConstant* Scalar(UMaterial* Graph, float Value)
{
    auto* Made = Node<UMaterialExpressionConstant>(Graph);
    Made->R = Value;
    return Made;
}
UMaterialExpressionConstant3Vector* Color(UMaterial* Graph, float R, float G, float B)
{
    auto* Made = Node<UMaterialExpressionConstant3Vector>(Graph);
    Made->Constant = FLinearColor(R, G, B, 1.f);
    return Made;
}
// A recognizable surface: a colour times a slow sine of time, so a frozen frame and a moving one differ.
UMaterialExpression* Pulse(UMaterial* Graph, float R, float G, float B)
{
    auto* Time = Node<UMaterialExpressionTime>(Graph);
    auto* Sine = Node<UMaterialExpressionSine>(Graph);
    Sine->Input.Expression = Time;
    Sine->Period = 4.f;
    auto* Swing = Node<UMaterialExpressionMultiply>(Graph); // 0.25 sin(t)
    Swing->A.Expression = Sine;
    Swing->B.Expression = Scalar(Graph, 0.25f);
    auto* Level = Node<UMaterialExpressionAdd>(Graph); // 0.75 + 0.25 sin(t)
    Level->A.Expression = Swing;
    Level->B.Expression = Scalar(Graph, 0.75f);
    auto* Base = Node<UMaterialExpressionMultiply>(Graph);
    Base->A.Expression = Color(Graph, R, G, B);
    Base->B.Expression = Level;
    return Base;
}

// A 64-pixel orange and cream checker with a blue corner, imported as a creator does (Unreal's texture factory).
UTexture2D* CheckerTexture(FAutomationTestBase& Test, const FString& Root)
{
    const FString Folder =
        FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialCoverage/source"));
    IFileManager::Get().MakeDirectory(*Folder, true);
    const int32 Size = 64;
    TArray<uint8> Tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8(Size), 0, uint8(Size), 0, 32, 40};
    for (int32 Y = 0; Y < Size; ++Y)
        for (int32 X = 0; X < Size; ++X)
        {
            const FColor C = (X < 16 && Y < 16)         ? FColor(40, 90, 250, 255)
                : ((X / 8 + Y / 8) & 1)                ? FColor(250, 140, 40, 255)
                                                       : FColor(250, 240, 210, 255);
            Tga.Append({C.B, C.G, C.R, C.A});
        }
    const FString File = Folder / TEXT("checker.tga");
    if (!Test.TestTrue(TEXT("The checker source is written"), FFileHelper::SaveArrayToFile(Tga, *File)))
        return nullptr;
    TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
    Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
    Factory->AssetImportTask->bAutomated = true;
    bool Cancelled = false;
    UObject* Asset = Factory->ImportObject(UTexture2D::StaticClass(), CreatePackage(*(Root / TEXT("T_Coverage"))),
        TEXT("T_Coverage"), RF_Public | RF_Standalone | RF_Transactional, File, nullptr, Cancelled);
    Factory->CleanUp();
    if (Asset)
        FAssetRegistryModule::AssetCreated(Asset);
    return Cancelled ? nullptr : Cast<UTexture2D>(Asset);
}

// The settings every surface case starts from: opaque Default Lit, one colour.
void LitSurface(UMaterial* Graph, float R, float G, float B)
{
    Graph->MaterialDomain = MD_Surface;
    Graph->BlendMode = BLEND_Opaque;
    Graph->SetShadingModel(MSM_DefaultLit);
    Graph->BaseColor.Expression = Color(Graph, R, G, B);
    Graph->Roughness.Expression = Scalar(Graph, 0.5f);
}
void Translucent(UMaterial* Graph, bool Lit, float Opacity = 0.5f)
{
    Graph->MaterialDomain = MD_Surface;
    Graph->BlendMode = BLEND_Translucent;
    Graph->SetShadingModel(Lit ? MSM_DefaultLit : MSM_Unlit);
    if (Lit)
    {
        Graph->BaseColor.Expression = Color(Graph, 0.2f, 0.6f, 1.f);
        Graph->Roughness.Expression = Scalar(Graph, 0.3f);
    }
    else
        Graph->EmissiveColor.Expression = Color(Graph, 0.2f, 0.6f, 1.f);
    Graph->Opacity.Expression = Scalar(Graph, Opacity);
}

TArray<FCoverageCase> SettingCases()
{
    TArray<FCoverageCase> Out;
    auto Add = [&Out](const TCHAR* Family, const TCHAR* Id, const TCHAR* Feature,
                   TFunction<bool(UMaterial*, const FCoverageContext&)> Setup)
    { Out.Add({FString(Id), FString(Family), FString(Feature), MoveTemp(Setup)}); };

    // Translucency: each lighting mode on a lit translucent surface, and each Details setting both ways.
    const struct
    {
        ETranslucencyLightingMode Mode;
        const TCHAR* Id;
    } Modes[] = {{TLM_VolumetricNonDirectional, TEXT("TlmVolumetricNonDirectional")},
        {TLM_VolumetricDirectional, TEXT("TlmVolumetricDirectional")},
        {TLM_VolumetricPerVertexNonDirectional, TEXT("TlmVolumetricPerVertexNonDirectional")},
        {TLM_VolumetricPerVertexDirectional, TEXT("TlmVolumetricPerVertexDirectional")},
        {TLM_Surface, TEXT("TlmSurface")}, {TLM_SurfacePerPixelLighting, TEXT("TlmSurfacePerPixelLighting")}};
    for (const auto& Mode : Modes)
    {
        const ETranslucencyLightingMode Value = Mode.Mode;
        Add(TEXT("translucency"), Mode.Id, *(FString(TEXT("Lighting Mode ")) + Mode.Id + TEXT(" (lit)")),
            [Value](UMaterial* G, const FCoverageContext&)
            {
                Translucent(G, true);
                G->TranslucencyLightingMode = Value;
                return true;
            });
    }
    for (const bool Lit : {true, false})
    {
        const TCHAR* Kind = Lit ? TEXT("Lit") : TEXT("Unlit");
        Add(TEXT("translucency"), *(FString(TEXT("SeparateOn")) + Kind),
            *(FString(TEXT("Render After DOF on, ")) + Kind),
            [Lit](UMaterial* G, const FCoverageContext&)
            {
                Translucent(G, Lit);
                G->bEnableSeparateTranslucency = true;
                return true;
            });
        Add(TEXT("translucency"), *(FString(TEXT("SeparateOff")) + Kind),
            *(FString(TEXT("Render After DOF off, ")) + Kind),
            [Lit](UMaterial* G, const FCoverageContext&)
            {
                Translucent(G, Lit);
                G->bEnableSeparateTranslucency = false;
                return true;
            });
        Add(TEXT("translucency"), *(FString(TEXT("FogOn")) + Kind), *(FString(TEXT("Apply Fogging on, ")) + Kind),
            [Lit](UMaterial* G, const FCoverageContext&)
            {
                Translucent(G, Lit);
                G->bUseTranslucencyVertexFog = true;
                return true;
            });
        Add(TEXT("translucency"), *(FString(TEXT("FogOff")) + Kind), *(FString(TEXT("Apply Fogging off, ")) + Kind),
            [Lit](UMaterial* G, const FCoverageContext&)
            {
                Translucent(G, Lit);
                G->bUseTranslucencyVertexFog = false;
                return true;
            });
        Add(TEXT("translucency"), *(FString(TEXT("FogPerPixel")) + Kind),
            *(FString(TEXT("Compute Fog Per Pixel, ")) + Kind),
            [Lit](UMaterial* G, const FCoverageContext&)
            {
                Translucent(G, Lit);
                G->bUseTranslucencyVertexFog = true;
                G->bComputeFogPerPixel = true;
                return true;
            });
        Add(TEXT("translucency"), *(FString(TEXT("TwoSided")) + Kind), *(FString(TEXT("Two Sided, ")) + Kind),
            [Lit](UMaterial* G, const FCoverageContext&)
            {
                Translucent(G, Lit);
                G->TwoSided = true;
                return true;
            });
    }
    Add(TEXT("translucency"), TEXT("ResponsiveAA"), TEXT("Responsive AA"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true);
            G->bEnableResponsiveAA = true;
            return true;
        });
    Add(TEXT("translucency"), TEXT("ScreenSpaceReflections"), TEXT("Screen Space Reflections (translucent)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true);
            G->bScreenSpaceReflections = true;
            return true;
        });
    Add(TEXT("translucency"), TEXT("ContactShadows"), TEXT("Contact Shadows (translucent)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true);
            G->bContactShadows = true;
            return true;
        });
    Add(TEXT("translucency"), TEXT("OutputVelocity"), TEXT("Output Velocity"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true);
            G->bOutputTranslucentVelocity = true;
            return true;
        });
    Add(TEXT("translucency"), TEXT("CustomDepthWrites"), TEXT("Allow Custom Depth Writes"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true, 0.8f);
            G->AllowTranslucentCustomDepthWrites = true;
            return true;
        });
    Add(TEXT("translucency"), TEXT("DisableDepthTest"), TEXT("Disable Depth Test"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, false);
            G->bDisableDepthTest = true;
            return true;
        });
    Add(TEXT("translucency"), TEXT("Numbers"),
        TEXT("Directional Lighting Intensity, shadow density, self-shadow and backscattering numbers"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true);
            G->TranslucencyLightingMode = TLM_VolumetricDirectional;
            G->TranslucencyDirectionalLightingIntensity = 2.f;
            G->TranslucentShadowDensityScale = 0.25f;
            G->TranslucentSelfShadowDensityScale = 4.f;
            G->TranslucentSelfShadowSecondDensityScale = 2.f;
            G->TranslucentSelfShadowSecondOpacity = 0.5f;
            G->TranslucentBackscatteringExponent = 10.f;
            G->TranslucentShadowStartOffset = 10.f;
            return true;
        });
    Add(TEXT("translucency"), TEXT("Refraction"), TEXT("Refraction input (index 1.33)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true, 0.3f);
            G->Refraction.Expression = Scalar(G, 1.33f);
            return true;
        });
    Add(TEXT("translucency"), TEXT("RefractionUnlit"), TEXT("Refraction input (index 1.5, unlit, nearly clear)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, false, 0.1f);
            G->Refraction.Expression = Scalar(G, 1.5f);
            return true;
        });

    // Blend modes, lit and unlit.
    const struct
    {
        EBlendMode Mode;
        const TCHAR* Id;
    } Blends[] = {{BLEND_Opaque, TEXT("Opaque")}, {BLEND_Masked, TEXT("Masked")},
        {BLEND_Translucent, TEXT("Translucent")}, {BLEND_Additive, TEXT("Additive")},
        {BLEND_Modulate, TEXT("Modulate")}, {BLEND_AlphaComposite, TEXT("AlphaComposite")},
        {BLEND_AlphaHoldout, TEXT("AlphaHoldout")}};
    for (const auto& Blend : Blends)
        for (const bool Lit : {true, false})
        {
            const EBlendMode Mode = Blend.Mode;
            Add(TEXT("blend"), *(FString(Blend.Id) + (Lit ? TEXT("Lit") : TEXT("Unlit"))),
                *(FString(Blend.Id) + (Lit ? TEXT(", Default Lit") : TEXT(", Unlit"))),
                [Mode, Lit](UMaterial* G, const FCoverageContext&)
                {
                    Translucent(G, Lit);
                    G->BlendMode = Mode;
                    if (Mode == BLEND_Masked)
                    {
                        G->Opacity.Expression = nullptr;
                        auto* U = Node<UMaterialExpressionTextureCoordinate>(G);
                        U->UTiling = 4.f;
                        auto* Mask = Node<UMaterialExpressionSine>(G);
                        Mask->Input.Expression = U;
                        Mask->Period = 1.f;
                        G->OpacityMask.Expression = Mask;
                        G->OpacityMask.Mask = 1;
                        G->OpacityMask.MaskR = 1;
                    }
                    return true;
                });
        }
    Add(TEXT("blend"), TEXT("MaskedTwoSided"), TEXT("Masked, Default Lit, Two Sided"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.3f, 0.8f, 0.3f);
            G->BlendMode = BLEND_Masked;
            G->TwoSided = true;
            auto* U = Node<UMaterialExpressionTextureCoordinate>(G);
            U->UTiling = 4.f;
            auto* Mask = Node<UMaterialExpressionSine>(G);
            Mask->Input.Expression = U;
            G->OpacityMask.Expression = Mask;
            G->OpacityMask.Mask = 1;
            G->OpacityMask.MaskR = 1;
            return true;
        });
    Add(TEXT("blend"), TEXT("DitherOpacityMask"), TEXT("Masked with Dither Opacity Mask"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.8f, 0.8f, 0.3f);
            G->BlendMode = BLEND_Masked;
            G->DitherOpacityMask = true;
            G->OpacityMask.Expression = Scalar(G, 0.5f);
            return true;
        });

    // Shading models (opaque), each with the inputs it reads.
    const struct
    {
        EMaterialShadingModel Model;
        const TCHAR* Id;
    } Models[] = {{MSM_Unlit, TEXT("Unlit")}, {MSM_DefaultLit, TEXT("DefaultLit")}, {MSM_Subsurface, TEXT("Subsurface")},
        {MSM_PreintegratedSkin, TEXT("PreintegratedSkin")}, {MSM_ClearCoat, TEXT("ClearCoat")},
        {MSM_SubsurfaceProfile, TEXT("SubsurfaceProfile")}, {MSM_TwoSidedFoliage, TEXT("TwoSidedFoliage")},
        {MSM_Hair, TEXT("Hair")}, {MSM_Cloth, TEXT("Cloth")}, {MSM_Eye, TEXT("Eye")},
        {MSM_SingleLayerWater, TEXT("SingleLayerWater")}};
    for (const auto& Model : Models)
    {
        const EMaterialShadingModel Value = Model.Model;
        Add(TEXT("shading"), Model.Id, *(FString(TEXT("Shading model ")) + Model.Id),
            [Value](UMaterial* G, const FCoverageContext&)
            {
                LitSurface(G, 0.9f, 0.5f, 0.4f);
                G->SetShadingModel(Value);
                if (Value == MSM_Unlit)
                    G->EmissiveColor.Expression = Color(G, 0.9f, 0.5f, 0.4f);
                if (Value == MSM_Subsurface || Value == MSM_PreintegratedSkin || Value == MSM_TwoSidedFoliage ||
                    Value == MSM_Cloth)
                    G->SubsurfaceColor.Expression = Color(G, 1.f, 0.1f, 0.1f);
                if (Value == MSM_Subsurface || Value == MSM_PreintegratedSkin)
                    G->Opacity.Expression = Scalar(G, 0.7f);
                if (Value == MSM_ClearCoat)
                {
                    G->ClearCoat.Expression = Scalar(G, 1.f);
                    G->ClearCoatRoughness.Expression = Scalar(G, 0.05f);
                }
                if (Value == MSM_Cloth)
                    G->ClearCoat.Expression = Scalar(G, 1.f); // Fuzz amount
                if (Value == MSM_SingleLayerWater)
                {
                    auto* Water = Node<UMaterialExpressionSingleLayerWaterMaterialOutput>(G);
                    Water->ScatteringCoefficients.Expression = Color(G, 0.02f, 0.05f, 0.1f);
                    Water->AbsorptionCoefficients.Expression = Color(G, 0.3f, 0.1f, 0.05f);
                    Water->PhaseG.Expression = Scalar(G, 0.f);
                    Water->ColorScaleBehindWater.Expression = Scalar(G, 1.f);
                }
                if (Value == MSM_Hair)
                {
                    G->bUsedWithSkeletalMesh = true;
                    G->Tangent.Expression = Color(G, 1.f, 0.f, 0.f);
                }
                return true;
            });
    }
    Add(TEXT("shading"), TEXT("ThinTranslucent"), TEXT("Shading model ThinTranslucent (translucent)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            Translucent(G, true);
            G->SetShadingModel(MSM_ThinTranslucent);
            auto* Thin = Node<UMaterialExpressionThinTranslucentMaterialOutput>(G);
            Thin->TransmittanceColor.Expression = Color(G, 0.2f, 0.6f, 1.f);
            G->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
            return true;
        });
    Add(TEXT("shading"), TEXT("FromMaterialExpression"), TEXT("Shading model From Material Expression"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.5f, 0.5f, 0.9f);
            // SetShadingModel refuses this value: the editor sets the (private) field, and the shading model field is
            // rebuilt from it on every recompile. A Shading Model node feeds it.
            FByteProperty* Field = FindFProperty<FByteProperty>(UMaterial::StaticClass(), TEXT("ShadingModel"));
            if (!Field)
                return false;
            Field->SetPropertyValue_InContainer(G, uint8(MSM_FromMaterialExpression));
            auto* Model = Node<UMaterialExpressionShadingModel>(G);
            Model->ShadingModel = MSM_ClearCoat;
            G->ShadingModelFromMaterialExpression.Expression = Model;
            G->ClearCoat.Expression = Scalar(G, 1.f);
            return true;
        });
    Add(TEXT("shading"), TEXT("FullyRough"), TEXT("Fully Rough"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.5f, 0.9f, 0.5f);
            G->bFullyRough = true;
            return true;
        });

    // Domains.
    const struct
    {
        EMaterialDomain Domain;
        const TCHAR* Id;
    } Domains[] = {{MD_DeferredDecal, TEXT("DeferredDecal")}, {MD_LightFunction, TEXT("LightFunction")},
        {MD_Volume, TEXT("Volume")}, {MD_PostProcess, TEXT("PostProcess")}, {MD_UI, TEXT("UI")},
        {MD_RuntimeVirtualTexture, TEXT("RuntimeVirtualTexture")}};
    for (const auto& Domain : Domains)
    {
        const EMaterialDomain Value = Domain.Domain;
        Add(TEXT("domain"), Domain.Id, *(FString(TEXT("Domain ")) + Domain.Id),
            [Value](UMaterial* G, const FCoverageContext&)
            {
                G->MaterialDomain = Value;
                G->SetShadingModel(Value == MD_DeferredDecal || Value == MD_RuntimeVirtualTexture ? MSM_DefaultLit
                                                                                                  : MSM_Unlit);
                G->BlendMode = Value == MD_DeferredDecal || Value == MD_UI ? BLEND_Translucent
                    : Value == MD_Volume                                   ? BLEND_Additive
                                                                           : BLEND_Opaque;
                G->BaseColor.Expression = Color(G, 0.2f, 0.9f, 0.3f);
                G->EmissiveColor.Expression = Color(G, 0.2f, 0.9f, 0.3f);
                G->Opacity.Expression = Scalar(G, 0.8f);
                return true;
            });
    }
    // The decal blend modes.
    const struct
    {
        EDecalBlendMode Mode;
        const TCHAR* Id;
    } Decals[] = {{DBM_Translucent, TEXT("DecalTranslucent")}, {DBM_Stain, TEXT("DecalStain")},
        {DBM_Normal, TEXT("DecalNormal")}, {DBM_Emissive, TEXT("DecalEmissive")},
        {DBM_DBuffer_ColorNormalRoughness, TEXT("DecalDBufferColorNormalRoughness")},
        {DBM_DBuffer_Color, TEXT("DecalDBufferColor")}, {DBM_DBuffer_ColorNormal, TEXT("DecalDBufferColorNormal")},
        {DBM_DBuffer_ColorRoughness, TEXT("DecalDBufferColorRoughness")},
        {DBM_DBuffer_Normal, TEXT("DecalDBufferNormal")}, {DBM_DBuffer_NormalRoughness, TEXT("DecalDBufferNormalRoughness")},
        {DBM_DBuffer_Roughness, TEXT("DecalDBufferRoughness")}, {DBM_DBuffer_Emissive, TEXT("DecalDBufferEmissive")},
        {DBM_DBuffer_AlphaComposite, TEXT("DecalDBufferAlphaComposite")},
        {DBM_DBuffer_EmissiveAlphaComposite, TEXT("DecalDBufferEmissiveAlphaComposite")},
        {DBM_Volumetric_DistanceFunction, TEXT("DecalVolumetricDistanceFunction")},
        {DBM_AlphaComposite, TEXT("DecalAlphaComposite")}, {DBM_AmbientOcclusion, TEXT("DecalAmbientOcclusion")}};
    for (const auto& Decal : Decals)
    {
        const EDecalBlendMode Value = Decal.Mode;
        Add(TEXT("domain"), Decal.Id, *(FString(TEXT("Decal blend mode ")) + Decal.Id),
            [Value](UMaterial* G, const FCoverageContext&)
            {
                G->MaterialDomain = MD_DeferredDecal;
                G->BlendMode = BLEND_Translucent;
                G->DecalBlendMode = Value;
                G->BaseColor.Expression = Color(G, 0.9f, 0.2f, 0.6f);
                G->EmissiveColor.Expression = Color(G, 0.3f, 0.f, 0.2f);
                G->Normal.Expression = Color(G, 0.3f, 0.f, 1.f);
                G->Roughness.Expression = Scalar(G, 0.2f);
                G->Opacity.Expression = Scalar(G, 0.8f);
                G->AmbientOcclusion.Expression = Scalar(G, 0.3f);
                return true;
            });
    }
    // The decal modes that change only the ground's normal, roughness or ambient occlusion, as stripes: the value
    // swings across its whole range every quarter of the decal, so a mode that works draws a pattern (a uniform
    // change on flat ground is lost in the scene's own frame-to-frame noise).
    auto Stripes = [](UMaterial* G)
    {
        auto* U = Node<UMaterialExpressionTextureCoordinate>(G);
        U->UTiling = 4.f;
        U->VTiling = 4.f;
        auto* Wave = Node<UMaterialExpressionSine>(G);
        Wave->Input.Expression = U;
        return Wave;
    };
    // Lerp(Mid, High, sine): Mid at the stripe edge, High and 2 Mid - High at its middles.
    auto Swing = [Stripes](UMaterial* G, UMaterialExpression* Mid, UMaterialExpression* High)
    {
        auto* L = Node<UMaterialExpressionLinearInterpolate>(G);
        L->A.Expression = Mid;
        L->B.Expression = High;
        L->Alpha.Expression = Stripes(G);
        L->Alpha.Mask = 1;
        L->Alpha.MaskR = 1;
        return L;
    };
    const struct
    {
        EDecalBlendMode Mode;
        const TCHAR* Id;
        bool Normal, Roughness, Occlusion;
    } Patterned[] = {{DBM_Normal, TEXT("DecalNormalStripes"), true, false, false},
        {DBM_DBuffer_Normal, TEXT("DecalDBufferNormalStripes"), true, false, false},
        {DBM_DBuffer_NormalRoughness, TEXT("DecalDBufferNormalRoughnessStripes"), true, true, false},
        {DBM_DBuffer_Roughness, TEXT("DecalDBufferRoughnessStripes"), false, true, false},
        {DBM_AmbientOcclusion, TEXT("DecalAmbientOcclusionStripes"), false, false, true}};
    for (const auto& Each : Patterned)
    {
        const auto Case = Each;
        Add(TEXT("domain"), Case.Id, *(FString(TEXT("Decal blend mode ")) + Case.Id),
            [Case, Swing](UMaterial* G, const FCoverageContext&)
            {
                G->MaterialDomain = MD_DeferredDecal;
                G->BlendMode = BLEND_Translucent;
                G->DecalBlendMode = Case.Mode;
                G->Opacity.Expression = Scalar(G, 1.f);
                // A normal tilted 63 degrees one way, then the other.
                if (Case.Normal)
                    G->Normal.Expression = Swing(G, Color(G, 0.f, 0.f, 1.f), Color(G, 0.9f, 0.f, 0.45f));
                if (Case.Roughness)
                    G->Roughness.Expression = Swing(G, Scalar(G, 0.5f), Scalar(G, 1.f));
                if (Case.Occlusion)
                    G->AmbientOcclusion.Expression = Swing(G, Scalar(G, 0.5f), Scalar(G, 1.f));
                return true;
            });
    }

    // Material inputs, each on an otherwise plain surface.
    Add(TEXT("inputs"), TEXT("Metallic"), TEXT("Metallic"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.9f, 0.7f, 0.3f);
            G->Metallic.Expression = Scalar(G, 1.f);
            G->Roughness.Expression = Scalar(G, 0.2f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("Specular"), TEXT("Specular"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.5f, 0.5f, 0.5f);
            G->Specular.Expression = Scalar(G, 1.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("Anisotropy"), TEXT("Anisotropy and Tangent"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.6f, 0.6f, 0.7f);
            G->Metallic.Expression = Scalar(G, 1.f);
            G->Anisotropy.Expression = Scalar(G, 0.8f);
            G->Tangent.Expression = Color(G, 1.f, 0.f, 0.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("Normal"), TEXT("Normal (tangent space)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.7f, 0.7f, 0.7f);
            G->Normal.Expression = Color(G, 0.5f, 0.f, 0.86f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("NormalWorld"), TEXT("Normal (world space, Tangent Space Normal off)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.7f, 0.7f, 0.7f);
            G->bTangentSpaceNormal = false;
            G->Normal.Expression = Color(G, 0.f, 0.f, 1.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("Emissive"), TEXT("Emissive Color (lit)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.1f, 0.1f, 0.1f);
            G->EmissiveColor.Expression = Pulse(G, 2.f, 0.5f, 0.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("NegativeEmissive"), TEXT("Allow Negative Emissive Color"),
        [](UMaterial* G, const FCoverageContext&)
        {
            G->MaterialDomain = MD_Surface;
            G->SetShadingModel(MSM_Unlit); // UE4 takes negative emissive on unlit materials only
            G->bAllowNegativeEmissiveColor = true;
            G->EmissiveColor.Expression = Color(G, -0.5f, 0.f, 0.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("AmbientOcclusion"), TEXT("Ambient Occlusion"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.8f, 0.8f, 0.8f);
            G->AmbientOcclusion.Expression = Scalar(G, 0.2f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("WorldPositionOffset"), TEXT("World Position Offset"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.4f, 0.8f, 0.9f);
            auto* Up = Node<UMaterialExpressionMultiply>(G); // up to 30 cm up and down with time
            Up->A.Expression = Pulse(G, 0.f, 0.f, 1.f);
            Up->B.Expression = Scalar(G, 30.f);
            G->WorldPositionOffset.Expression = Up;
            return true;
        });
    Add(TEXT("inputs"), TEXT("PixelDepthOffset"), TEXT("Pixel Depth Offset"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.4f, 0.4f, 0.9f);
            G->PixelDepthOffset.Expression = Scalar(G, 20.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("PixelDepthOffsetSmall"), TEXT("Pixel Depth Offset (0.75 cm)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.4f, 0.9f, 0.9f);
            G->PixelDepthOffset.Expression = Scalar(G, 0.75f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("PixelDepthOffsetMasked"), TEXT("Pixel Depth Offset on a masked material"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.9f, 0.4f, 0.9f);
            G->BlendMode = BLEND_Masked;
            G->OpacityMask.Expression = Scalar(G, 1.f);
            G->PixelDepthOffset.Expression = Scalar(G, 15.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("CustomizedUVs"), TEXT("Customized UVs"),
        [](UMaterial* G, const FCoverageContext& C)
        {
            LitSurface(G, 1.f, 1.f, 1.f);
            G->NumCustomizedUVs = 1;
            auto* U = Node<UMaterialExpressionTextureCoordinate>(G);
            U->UTiling = 3.f;
            U->VTiling = 3.f;
            G->CustomizedUVs[0].Expression = U;
            auto* Sample = Node<UMaterialExpressionTextureSample>(G);
            Sample->Texture = C.Texture2D;
            Sample->AutoSetSampleType();
            G->BaseColor.Expression = Sample;
            return C.Texture2D != nullptr;
        });
    Add(TEXT("inputs"), TEXT("Tessellation"), TEXT("Tessellation (PN triangles), World Displacement, multiplier"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.6f, 0.9f, 0.6f);
            G->D3D11TessellationMode = MTM_PNTriangles;
            G->WorldDisplacement.Expression = Color(G, 0.f, 0.f, 10.f);
            G->TessellationMultiplier.Expression = Scalar(G, 2.f);
            return true;
        });
    Add(TEXT("inputs"), TEXT("MaterialAttributes"), TEXT("Use Material Attributes (Make Material Attributes)"),
        [](UMaterial* G, const FCoverageContext&)
        {
            G->MaterialDomain = MD_Surface;
            G->BlendMode = BLEND_Opaque;
            G->SetShadingModel(MSM_DefaultLit);
            G->bUseMaterialAttributes = true;
            auto* Make = Node<UMaterialExpressionMakeMaterialAttributes>(G);
            Make->BaseColor.Expression = Color(G, 0.9f, 0.3f, 0.9f);
            Make->Roughness.Expression = Scalar(G, 0.4f);
            G->MaterialAttributes.Expression = Make;
            return true;
        });
    Add(TEXT("inputs"), TEXT("LightmapDirectionality"), TEXT("Use Lightmap Directionality off"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.7f, 0.7f, 0.5f);
            G->bUseLightmapDirectionality = false;
            return true;
        });
    Add(TEXT("inputs"), TEXT("HQForwardReflections"), TEXT("High Quality Forward Reflections, planar reflections"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.7f, 0.7f, 0.7f);
            G->Metallic.Expression = Scalar(G, 1.f);
            G->Roughness.Expression = Scalar(G, 0.1f);
            G->bUseHQForwardReflections = true;
            G->bUsePlanarForwardReflections = true;
            return true;
        });
    Add(TEXT("inputs"), TEXT("NormalCurvatureToRoughness"), TEXT("Normal Curvature To Roughness"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.7f, 0.7f, 0.7f);
            G->bNormalCurvatureToRoughness = true;
            return true;
        });
    Add(TEXT("inputs"), TEXT("FullPrecision"), TEXT("Use Full Precision"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.7f, 0.5f, 0.7f);
            G->bUseFullPrecision = true;
            return true;
        });
    Add(TEXT("inputs"), TEXT("DitheredLOD"), TEXT("Dithered LOD Transition"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.5f, 0.7f, 0.7f);
            G->DitheredLODTransition = true;
            return true;
        });
    Add(TEXT("inputs"), TEXT("Wireframe"), TEXT("Wireframe"),
        [](UMaterial* G, const FCoverageContext&)
        {
            LitSurface(G, 0.9f, 0.9f, 0.2f);
            G->Wireframe = true;
            return true;
        });
    Add(TEXT("inputs"), TEXT("StencilTest"), TEXT("Post process stencil test"),
        [](UMaterial* G, const FCoverageContext&)
        {
            G->MaterialDomain = MD_PostProcess;
            G->SetShadingModel(MSM_Unlit);
            G->EmissiveColor.Expression = Color(G, 0.5f, 0.5f, 1.f);
            G->bEnableStencilTest = true;
            return true;
        });

    // Texture types.
    auto Sampled = [&Add](const TCHAR* Id, const TCHAR* Feature, TFunction<UTexture*(const FCoverageContext&)> Pick)
    {
        Add(TEXT("textures"), Id, Feature,
            [Pick](UMaterial* G, const FCoverageContext& C)
            {
                UTexture* Texture = Pick(C);
                if (!Texture)
                    return false;
                LitSurface(G, 1.f, 1.f, 1.f);
                auto* Sample = Node<UMaterialExpressionTextureSample>(G);
                Sample->Texture = Texture;
                Sample->AutoSetSampleType();
                if (Texture->IsA<UTextureCube>() || Texture->IsA<UVolumeTexture>() || Texture->IsA<UTexture2DArray>())
                {
                    auto* U = Node<UMaterialExpressionTextureCoordinate>(G);
                    auto* Uvw = Node<UMaterialExpressionAppendVector>(G);
                    Uvw->A.Expression = U;
                    Uvw->B.Expression = Scalar(G, 0.5f);
                    Sample->Coordinates.Expression = Uvw;
                }
                G->BaseColor.Expression = Sample;
                return true;
            });
    };
    Sampled(TEXT("Texture2D"), TEXT("Texture 2D sample"), [](const FCoverageContext& C) { return C.Texture2D; });
    Sampled(TEXT("TextureCube"), TEXT("Texture cube sample"), [](const FCoverageContext& C) { return C.Cube; });
    Sampled(TEXT("VolumeTexture"), TEXT("Volume texture sample"), [](const FCoverageContext& C) { return C.Volume; });
    Sampled(TEXT("Texture2DArray"), TEXT("Texture 2D array sample"), [](const FCoverageContext& C) { return C.Array; });
    Sampled(TEXT("RenderTarget"), TEXT("Render target sample"), [](const FCoverageContext& C) { return C.Target; });
    Add(TEXT("textures"), TEXT("ParameterCollection"), TEXT("Material parameter collection"),
        [](UMaterial* G, const FCoverageContext& C)
        {
            if (!C.Collection)
                return false;
            LitSurface(G, 1.f, 1.f, 1.f);
            auto* Read = Node<UMaterialExpressionCollectionParameter>(G);
            Read->Collection = C.Collection;
            Read->ParameterName = TEXT("Glow");
            Read->ParameterId = C.Collection->ScalarParameters[0].Id;
            G->EmissiveColor.Expression = Read;
            return true;
        });
    return Out;
}

// Every expression class UE4 offers, each wired into the emissive of an unlit surface. Inputs are tried unconnected,
// then each fed a scalar, a colour or the texture coordinate, until UE4 itself compiles it for the editor.
TArray<FCoverageCase> NodeCases()
{
    TArray<UClass*> Classes;
    for (TObjectIterator<UClass> It; It; ++It)
        if (It->IsChildOf(UMaterialExpression::StaticClass()) &&
            !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) &&
            !It->GetName().StartsWith(TEXT("SKEL_")) && !It->GetName().StartsWith(TEXT("REINST_")) &&
            *It != UMaterialExpressionComment::StaticClass() && *It != UMaterialExpressionFunctionInput::StaticClass() &&
            *It != UMaterialExpressionFunctionOutput::StaticClass())
            Classes.Add(*It);
    Classes.Sort([](const UClass& A, const UClass& B) { return A.GetName() < B.GetName(); });
    FString Slice = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_COVERAGE_SLICE")), Part, Parts;
    if (Slice.Split(TEXT("/"), &Part, &Parts) && FCString::Atoi(*Parts) > 1)
    {
        const int32 K = FCString::Atoi(*Part), N = FCString::Atoi(*Parts);
        TArray<UClass*> Kept;
        for (int32 I = 0; I < Classes.Num(); ++I)
            if (I % N == K)
                Kept.Add(Classes[I]);
        Classes = MoveTemp(Kept);
    }
    TArray<FCoverageCase> Out;
    for (UClass* Class : Classes)
    {
        FString Name = Class->GetName();
        Name.RemoveFromStart(TEXT("MaterialExpression"));
        for (int32 Attempt = 0; Attempt < 4; ++Attempt)
        {
            FCoverageCase Case;
            Case.Id = Name;
            Case.Family = TEXT("nodes");
            Case.Feature = FString::Printf(TEXT("%s node (inputs: %s)"), *Name,
                Attempt == 0 ? TEXT("unconnected") : Attempt == 1 ? TEXT("scalar") : Attempt == 2 ? TEXT("colour")
                                                                                                    : TEXT("UV"));
            Case.Setup = [Class, Attempt](UMaterial* G, const FCoverageContext& C)
            {
                G->MaterialDomain = MD_Surface;
                G->BlendMode = BLEND_Opaque;
                G->SetShadingModel(MSM_Unlit);
                if (Class->GetName().StartsWith(TEXT("MaterialExpressionKadanScene")))
                {
                    // The game's scene reads are for translucent materials only, as stock's.
                    G->BlendMode = BLEND_Translucent;
                    G->Opacity.Expression = Scalar(G, 1.f);
                }
                UMaterialExpression* Made = NewObject<UMaterialExpression>(G, Class);
                Made->Material = G;
                G->Expressions.Add(Made);
                if (UMaterialExpressionParameter* Param = Cast<UMaterialExpressionParameter>(Made))
                    Param->ParameterName = TEXT("Coverage");
                if (UMaterialExpressionTextureSampleParameter* Param =
                        Cast<UMaterialExpressionTextureSampleParameter>(Made))
                    Param->ParameterName = TEXT("Coverage");
                if (UMaterialExpressionTextureBase* Texture = Cast<UMaterialExpressionTextureBase>(Made))
                {
                    UTexture* Picks[] = {C.Texture2D, C.Cube, C.Volume, C.Array};
                    Texture->Texture = Picks[Attempt];
                    if (!Texture->Texture)
                        return false;
                    Texture->AutoSetSampleType();
                }
                for (FExpressionInput* Input : Made->GetInputs())
                {
                    if (!Input || Attempt == 0)
                        continue;
                    if (Cast<UMaterialExpressionTextureBase>(Made))
                        break; // the attempt chose the texture kind; inputs stay at their defaults
                    Input->Expression = Attempt == 1 ? static_cast<UMaterialExpression*>(Scalar(G, 0.5f))
                        : Attempt == 2               ? static_cast<UMaterialExpression*>(Color(G, 0.3f, 0.6f, 0.9f))
                                                     : static_cast<UMaterialExpression*>(
                                                           Node<UMaterialExpressionTextureCoordinate>(G));
                }
                if (Made->IsA<UMaterialExpressionCustomOutput>() || Made->GetOutputs().Num() == 0)
                    G->EmissiveColor.Expression = Color(G, 0.5f, 0.5f, 0.5f);
                else
                    G->EmissiveColor.Expression = Made;
                return true;
            };
            Out.Add(MoveTemp(Case));
        }
    }
    return Out;
}

// The gallery's layout (tools/material_gallery.py): rows of six cubes 140 cm apart, left to right in the picture,
// rows going away from the camera along +Y.
constexpr int32 GalleryRow = 6;
constexpr float GallerySpacing = 140.f;
FVector GalleryPosition(const FVector& Origin, int32 Index)
{
    const int32 Column = Index % GalleryRow, Row = Index / GalleryRow;
    return Origin + FVector((GalleryRow - 1) * GallerySpacing / 2 - Column * GallerySpacing, Row * GallerySpacing, 0);
}

// The UE4 editor's own rendering of the gallery: the cubes in the editor world far from anything else, under a sun
// and a sky light, one picture per row with the other rows hidden. The editor compiles the
// materials for its own platform; this is the reference each game picture is compared with.
bool EditorGalleryShots(FAutomationTestBase& Test, UStaticMesh* Cube, const TArray<UMaterial*>& Materials,
    const TArray<FString>& Leaves, const FString& Out)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || !Cube)
        return false;
    const FVector Origin(0.f, 0.f, 200000.f);
    TArray<AActor*> Made;
    ON_SCOPE_EXIT
    {
        for (AActor* Actor : Made)
            if (IsValid(Actor))
                World->DestroyActor(Actor);
        FlushRenderingCommands();
    };
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags = RF_Transient;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(Origin, FRotator(-50.f, 30.f, 0.f), Spawn);
    ASkyLight* Sky = World->SpawnActor<ASkyLight>(Origin, FRotator::ZeroRotator, Spawn);
    if (!Sun || !Sky)
        return false;
    Made.Append({Sun, Sky});
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sun->GetLightComponent()->SetIntensity(6.f);
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SourceType = SLS_CapturedScene;
    Sky->GetLightComponent()->bLowerHemisphereIsBlack = false;
    Sky->GetLightComponent()->SetLowerHemisphereColor(FLinearColor(0.3f, 0.3f, 0.3f));
    Sky->GetLightComponent()->SetIntensity(1.f);
    Sky->GetLightComponent()->RecaptureSky();
    // A grey floor under the rows, and a dark wall behind them for translucency to show against.
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    const int32 Rows = (Materials.Num() + GalleryRow - 1) / GalleryRow;
    if (Plane)
    {
        AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(
            Origin + FVector(0, Rows * GallerySpacing / 2, -50.f), FRotator::ZeroRotator, Spawn);
        AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(
            Origin + FVector(0, Rows * GallerySpacing + 200.f, 200.f), FRotator(0.f, 0.f, 90.f), Spawn);
        for (AStaticMeshActor* Part : {Floor, Wall})
            if (Part)
            {
                Made.Add(Part);
                Part->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
                Part->GetStaticMeshComponent()->SetStaticMesh(Plane);
                Part->SetActorScale3D(FVector(20.f, Rows * 2.f + 10.f, 1.f));
            }
    }
    TArray<AActor*> Cubes;
    for (int32 Index = 0; Index < Materials.Num(); ++Index)
    {
        AStaticMeshActor* Part =
            World->SpawnActor<AStaticMeshActor>(GalleryPosition(Origin, Index), FRotator::ZeroRotator, Spawn);
        if (!Part)
            return false;
        Made.Add(Part);
        Cubes.Add(Part);
        Part->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Part->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Part->GetStaticMeshComponent()->SetMaterial(0, Materials[Index]);
    }
    ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>(Origin, FRotator::ZeroRotator, Spawn);
    if (!Camera)
        return false;
    Made.Add(Camera);
    USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Camera);
    Target->ClearColor = FLinearColor::Black;
    Target->InitCustomFormat(1280, 720, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->FOVAngle = 70.f;
    Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetLensFlares(false);
    Capture->ShowFlags.SetVignette(false);
    IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TArray<FString> Captions;
    // One picture per row, near enough to tell each cube's colour, as the game's gallery shows one row at a time.
    for (int32 Row = 0; Row < Rows; ++Row)
    {
        const FVector Look = Origin + FVector(0, Row * GallerySpacing, 0);
        const FVector Eye = Look + FVector(0, -560.f, 230.f);
        Camera->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
        // Only this row's cubes, so each picture shows the same six the game's picture of the row shows.
        Capture->HiddenActors.Reset();
        for (int32 Index = 0; Index < Cubes.Num(); ++Index)
            if (Index / GalleryRow != Row)
                Capture->HiddenActors.Add(Cubes[Index]);
        TArray<FColor> Pixels;
        for (int32 Frame = 0; Frame < 3; ++Frame) // the sky capture and new shaders land a frame later
        {
            World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
        }
        if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels) || Pixels.Num() != 1280 * 720)
            return false;
        for (FColor& Pixel : Pixels)
            Pixel.A = 255;
        TSharedPtr<IImageWrapper> Jpeg = Images.CreateImageWrapper(EImageFormat::JPEG);
        const FString File = Out / FString::Printf(TEXT("editor_rows_%02d.jpg"), Row);
        if (!Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * 4, 1280, 720, ERGBFormat::BGRA, 8) ||
            !FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(85), *File))
            return false;
        FString Caption = FString::Printf(TEXT("%s:"), *FPaths::GetCleanFilename(File));
        for (int32 Index = Row * GalleryRow; Index < FMath::Min(Materials.Num(), (Row + 1) * GalleryRow); ++Index)
            Caption += TEXT(" ") + Leaves[Index];
        Captions.Add(Caption);
    }
    FFileHelper::SaveStringArrayToFile(Captions, *(Out / TEXT("editor_rows.txt")));
    Test.AddInfo(FString::Printf(TEXT("Editor gallery: %d pictures in %s"), Captions.Num(), *Out));
    return true;
}

FString Joined(const TArray<FString>& Lines)
{
    FString Out;
    for (const FString& Line : Lines)
        Out += (Out.IsEmpty() ? TEXT("") : TEXT(" | ")) + Line;
    return Out.Left(1500);
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialCoverageTest, "P4MapKit.Materials.Coverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialCoverageTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    if (!P4UseSwitchShaderRoute())
    {
        AddWarning(TEXT("P4MapKit test not run. Creator materials are exported through UE's shaders only."));
        return true;
    }
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("kadan.material.coverage"), TEXT("Material coverage"), RequestData, Names))
        return false;
    const FString Families = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_COVERAGE_FAMILIES"));
    TArray<FString> Wanted;
    Families.ParseIntoArray(Wanted, TEXT(","), true);
    TArray<FString> OnlyIds;
    FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_COVERAGE_ONLY")).ParseIntoArray(OnlyIds, TEXT(","), true);
    auto Runs = [&Wanted](const FString& Family)
    {
        if (Wanted.Contains(TEXT("all")))
            return true;
        return Wanted.Num() ? Wanted.Contains(Family) : Family != TEXT("nodes");
    };
    const FString Out = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialCoverage"));
    IFileManager::Get().MakeDirectory(*Out, true);

    // Inputs every family can use: a checker texture, a cube, a volume and an array texture, a render target and a
    // parameter collection, all of the mod's own.
    FCoverageContext Context;
    {
        Context.Texture2D = CheckerTexture(*this, Names.PackageRoot);
        auto Pixels = [](int32 Size, int32 Slices)
        {
            TArray<uint8> Data;
            for (int32 S = 0; S < Slices; ++S)
                for (int32 Y = 0; Y < Size; ++Y)
                    for (int32 X = 0; X < Size; ++X)
                    {
                        const bool On = ((X / 4 + Y / 4 + S) & 1) != 0;
                        Data.Append({uint8(On ? 40 : 230), uint8(On ? 200 : 120), uint8(S * 40), 255});
                    }
            return Data;
        };
        auto Package = [&Names](const TCHAR* Leaf) { return CreatePackage(*(Names.PackageRoot / Leaf)); };
        const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
        Context.Cube = NewObject<UTextureCube>(Package(TEXT("T_CoverageCube")), TEXT("T_CoverageCube"), Flags);
        Context.Cube->Source.Init(16, 16, 6, 1, TSF_BGRA8, Pixels(16, 6).GetData());
        Context.Cube->PostEditChange();
        Context.Volume = NewObject<UVolumeTexture>(Package(TEXT("T_CoverageVolume")), TEXT("T_CoverageVolume"), Flags);
        Context.Volume->Source.Init(16, 16, 16, 1, TSF_BGRA8, Pixels(16, 16).GetData());
        Context.Volume->PostEditChange();
        Context.Array = NewObject<UTexture2DArray>(Package(TEXT("T_CoverageArray")), TEXT("T_CoverageArray"), Flags);
        Context.Array->Source.Init(16, 16, 4, 1, TSF_BGRA8, Pixels(16, 4).GetData());
        Context.Array->PostEditChange();
        Context.Target =
            NewObject<UTextureRenderTarget2D>(Package(TEXT("RT_Coverage")), TEXT("RT_Coverage"), Flags);
        Context.Target->InitAutoFormat(64, 64);
        Context.Collection =
            NewObject<UMaterialParameterCollection>(Package(TEXT("MPC_Coverage")), TEXT("MPC_Coverage"), Flags);
        FCollectionScalarParameter Glow;
        Glow.ParameterName = TEXT("Glow");
        Glow.DefaultValue = 0.8f;
        Context.Collection->ScalarParameters.Add(Glow);
        Context.Collection->PostEditChange();
        for (UObject* Made : TArray<UObject*>{Context.Cube, Context.Volume, Context.Array, Context.Target,
                 Context.Collection})
            FAssetRegistryModule::AssetCreated(Made);
    }

    TArray<FCoverageCase> Cases = SettingCases();
    if (Runs(TEXT("nodes")))
        Cases.Append(NodeCases());
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<TStrongObjectPtr<UMaterial>> Shipped;
    TArray<FString> ShippedIds;
    TSet<FString> NodesDone; // a node is done once one attempt compiled for the editor
    int32 Index = 0;
    auto Save = [&Rows, &Out]()
    {
        FString Text;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Rows, Writer);
        FFileHelper::SaveStringToFile(Text, *(Out / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    for (const FCoverageCase& Case : Cases)
    {
        if (!Runs(Case.Family) || (Case.Family == TEXT("nodes") && NodesDone.Contains(Case.Id)) ||
            (OnlyIds.Num() && !OnlyIds.Contains(Case.Id)))
            continue;
        const FString Leaf = FString::Printf(TEXT("M_Cov%03d_%s"), Index++, *Case.Id).Left(60);
        UMaterial* Graph =
            Cast<UMaterial>(Tools.CreateAsset(Leaf, Names.PackageRoot, UMaterial::StaticClass(), Factory.Get()));
        TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), Case.Id);
        Row->SetStringField(TEXT("family"), Case.Family);
        Row->SetStringField(TEXT("feature"), Case.Feature);
        Row->SetStringField(TEXT("material"), Leaf);
        if (!Graph)
        {
            Row->SetStringField(TEXT("result"), TEXT("not-built"));
            Rows.Add(MakeShared<FJsonValueObject>(Row));
            continue;
        }
        Graph->Expressions.Empty();
        Graph->BaseColor.Expression = nullptr;
        Graph->EmissiveColor.Expression = nullptr;
        if (!Case.Setup(Graph, Context))
        {
            Row->SetStringField(TEXT("result"), TEXT("not-built"));
            Rows.Add(MakeShared<FJsonValueObject>(Row));
            Save();
            continue;
        }
        Graph->PostEditChange();
        if (GShaderCompilingManager)
            GShaderCompilingManager->FinishAllCompilation();
        FMaterialResource* Editor = Graph->GetMaterialResource(GMaxRHIFeatureLevel);
        if (Editor)
            Editor->FinishCompilation();
        const TArray<FString> EditorErrors = Editor ? Editor->GetCompileErrors() : TArray<FString>{TEXT("no resource")};
        Row->SetBoolField(TEXT("editor_compiles"), EditorErrors.Num() == 0);
        Row->SetStringField(TEXT("editor_errors"), Joined(EditorErrors));
        if (EditorErrors.Num() && Case.Family == TEXT("nodes"))
        {
            // UE4 itself refuses this wiring: the next attempt tries other inputs.
            Row->SetStringField(TEXT("result"), TEXT("ue4-refuses"));
            Rows.Add(MakeShared<FJsonValueObject>(Row));
            Save();
            continue;
        }
        if (Case.Family == TEXT("nodes"))
            NodesDone.Add(Case.Id);
        FP4CreatorMaterialCompileRequest Compile;
        Compile.CreatorMaterial = Graph;
        Compile.RuntimeObjectPath = Graph->GetPathName();
        const double Start = FPlatformTime::Seconds();
        const FP4CreatorMaterialCompileResult Result = P4CompileCreatorMaterial(Compile);
        Row->SetNumberField(TEXT("game_seconds"), FPlatformTime::Seconds() - Start);
        Row->SetBoolField(TEXT("game_compiles"), Result.Succeeded);
        Row->SetStringField(TEXT("game_error"), Result.Error.Left(1500));
        Row->SetNumberField(TEXT("programs"), double(Result.Programs.size()));
        Row->SetStringField(TEXT("result"), Result.Succeeded ? TEXT("compiles")
                : EditorErrors.Num()                         ? TEXT("ue4-refuses")
                                                             : TEXT("game-refuses"));
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        Save();
        AddInfo(FString::Printf(TEXT("%s %s: %s %s"), *Case.Family, *Case.Id,
            *Row->GetStringField(TEXT("result")), *Result.Error.Left(300)));
        // What export says of the material and of each texture and parameter collection it reads.
        TArray<UObject*> Uses = {Graph};
        for (UObject* Texture : Graph->GetReferencedTextures())
            Uses.AddUnique(Texture);
        for (UMaterialExpression* Expression : Graph->Expressions)
            if (auto* Read = Cast<UMaterialExpressionCollectionParameter>(Expression))
                Uses.AddUnique(Read->Collection);
        FString ExportRefused;
        for (UObject* Used : Uses)
        {
            if (!Used || Used->GetOutermost()->GetName().StartsWith(TEXT("/Engine/")))
                continue;
            const FP4ExportAvailability Available = P4ExportAssetAvailabilityAtAGlance(Used, RequestData.Mod);
            if (!Available.IsAvailable())
            {
                ExportRefused = Used->GetName() + TEXT(": ") + Available.Reason;
                break;
            }
        }
        if (!ExportRefused.IsEmpty())
        {
            Row->SetStringField(TEXT("export_refused"), ExportRefused.Left(1500));
            Save();
        }
        if (Result.Succeeded && ExportRefused.IsEmpty())
        {
            Shipped.Emplace(Graph);
            ShippedIds.Add(Leaf);
        }
    }
    Save();

    const bool Export = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_COVERAGE_EXPORT")) == TEXT("1");
    const bool Shots = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_COVERAGE_EDITOR_SHOTS")) == TEXT("1");
    if (!Export && !Shots)
        return !HasAnyErrors();
    // The gallery: a cube and every material that compiled. In the game, tools/material_gallery.py's Lua puts one
    // cube per material in front of the captain, in the layout written here.
    UStaticMesh* Cube = Cast<UStaticMesh>(ImportOwn(*this, Names.PackageRoot, TEXT("SM_CoverageCube"), true));
    if (!TestNotNull(TEXT("The gallery cube is imported"), Cube))
        return false;
    if (Shots)
    {
        TArray<UMaterial*> Drawn;
        for (const TStrongObjectPtr<UMaterial>& Graph : Shipped)
            Drawn.Add(Graph.Get());
        TestTrue(TEXT("The editor's pictures of the gallery are taken"),
            EditorGalleryShots(*this, Cube, Drawn, ShippedIds, Out));
    }
    if (!Export)
        return !HasAnyErrors();
    FP4ExportRoot Root;
    Root.Path = Cube->GetOutermost()->GetName();
    RequestData.Roots.Add(Root);
    for (const TStrongObjectPtr<UMaterial>& Graph : Shipped)
    {
        FP4ExportRoot Material;
        Material.Path = Graph->GetOutermost()->GetName();
        RequestData.Roots.Add(Material);
    }
    FString Error;
    FP4ExportModResult Exported;
    const bool Published = P4ExportMod(RequestData, Exported, Error);
    for (const FString& Why : Exported.Refused)
        AddWarning(TEXT("Refused at export: ") + Why);
    if (!TestTrue(TEXT("The gallery mod exports"), Published))
    {
        AddError(Error);
        return false;
    }
    TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
    Layout->SetStringField(TEXT("mod"), Exported.Directory);
    Layout->SetStringField(TEXT("cube"), Cube->GetPathName());
    TArray<TSharedPtr<FJsonValue>> Materials;
    for (int32 At = 0; At < Shipped.Num(); ++At)
    {
        const FString Authored = Shipped[At]->GetPathName();
        const FString* Runtime = Exported.RuntimePaths.Find(Authored);
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("material"), ShippedIds[At]);
        Entry->SetStringField(TEXT("runtime"), Runtime ? *Runtime : Authored);
        Materials.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Layout->SetArrayField(TEXT("materials"), Materials);
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Layout.ToSharedRef(), Writer);
    FFileHelper::SaveStringToFile(Text, *(Out / TEXT("gallery.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(TEXT("Gallery mod ") + Exported.Directory);
    return !HasAnyErrors();
}
#endif

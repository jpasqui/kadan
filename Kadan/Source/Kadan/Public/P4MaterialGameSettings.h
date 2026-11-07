#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "P4MaterialGameSettings.generated.h"

class UMaterial;

// The game's own material settings: the properties Nintendo's engine adds to UMaterial, with the game's names,
// types and enums (decomp/ue4-recomp-bridge/engine_patch_edits.json, after RefractionDepthBias in its Material.h,
// and the cooked masters' properties). Stock Unreal 4.26's UMaterial has no such fields, so Kadan keeps them with
// the material as asset user data and shows them on the material's Details panel, in the category C4. Kadan's
// shader format compiles the material with each as a define, KADAN_<name> (tools/navigation/OWN_SHADER_MAPS.md).
// A value the game's cooked masters never hold is the game's default: every float there differs from 1, so the
// floats default to 1. The object is editor-only and never cooked. The settings marked C4MaterialField are fields of the
// game's UMaterial of the same name, which the export writes into the cooked material (P4GameMaterialModeCook.h).

UENUM()
enum EC4MaterialMeshType
{
    C4MMT_Default = 0, C4MMT_Terrain = 1, C4MMT_BG = 2, C4MMT_Landscape = 3, C4MMT_Foliage = 4, C4MMT_Plants = 5,
    C4MMT_Player = 6, C4MMT_Pikmin = 7, C4MMT_NPC = 8, C4MMT_Teki = 9, C4MMT_Objects = 10, C4MMT_ObjectsInstance = 11,
    C4MMT_Effects = 12, C4MMT_UI_2D = 13, C4MMT_UI_3D = 14, C4MMT_Otakara = 15, C4MMT_Others = 16, C4MMT_Sky = 17,
};

UENUM()
enum EC4MaterialDrawOrder
{
    C4MDO_Default = 0, C4MDO_GroundSoft = 1, C4MDO_XluUseSceneColor = 2,
};

UENUM()
enum EC4ShadowPCF
{
    C4PCF_1x1 = 0, C4PCF_NoFiltering = 1, C4PCF_2x2 = 2, C4PCF_3x3 = 3,
};

UENUM()
enum EC4XluCut
{
    C4XC_Default = 0, C4XC_Enable = 1,
};

UENUM()
enum EC4ShadowForwardPCF
{
    C4FWPCF_Disable = 0, C4FWPCF_NoFiltering = 1, C4FWPCF_1x1 = 2, C4FWPCF_2x2 = 3, C4FWPCF_3x3 = 4,
};

// Not a field of the game's Material.h: the game's engine adds two values to UMaterial's TranslucencyLightingMode
// (TLM_SurfacePerPixelLightingC4Custom0 and 1, after stock's six), which stock Unreal's enum cannot hold. The material
// keeps stock TLM_SurfacePerPixelLighting, which they extend, and this setting says which of the game's modes it is.
UENUM()
enum EC4TranslucencyLightingMode
{
    C4TLM_Stock = 0, C4TLM_SurfacePerPixelLightingC4Custom0 = 1, C4TLM_SurfacePerPixelLightingC4Custom1 = 2,
};

// Not a field of the game's Material.h either: the game's engine adds a decal blend mode to UMaterial's DecalBlendMode
// (DBM_DBuffer_C4_WaterBox, the water wetness decal), which stock Unreal's enum cannot hold. The material keeps stock
// DBM_DBuffer_Color in its place, and this setting says it is the game's mode (KadanSwitchSourcePatches.inl writes its
// targets).
UENUM()
enum EC4DecalBlendMode
{
    C4DBM_Stock = 0, C4DBM_DBuffer_C4_WaterBox = 1,
};

UCLASS(meta = (DisplayName = "C4 Material Settings"))
class KADAN_API UP4MaterialGameSettings : public UAssetUserData
{
    GENERATED_BODY()
public:
    /** The kind of mesh the material is for. The game sets it on its masters. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Mesh type")) TEnumAsByte<EC4MaterialMeshType> C4MaterialMeshType = C4MMT_Default;
    /** Where the game draws the material. GroundSoft materials read the scene behind them. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Draw order")) TEnumAsByte<EC4MaterialDrawOrder> C4MaterialDrawOrder = C4MDO_Default;
    /** Scales the capsule shadows on the material. The game sets 0 on characters and on objects without capsule shadows. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Capsule shadow scale")) float C4ShaderParam0 = 1.f;
    /** Scales received directional shadows and local dynamic shadows. Zero removes those terms. Baked local shadows, capsule shadows, and indirect light are separate. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Shadow strength")) float C4ShaderParam1 = 1.f;
    /** The game sets 0 on its Avatar materials. This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown C4ShaderParam2")) float C4ShaderParam2 = 1.f;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown C4ShaderParam3")) float C4ShaderParam3 = 1.f;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown C4ShaderParam4")) float C4ShaderParam4 = 1.f;
    /** At 0 a translucent material takes each local light's own specular scale. The game sets 0 on its Avatar materials. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Local light specular scale")) float C4ShaderParam5 = 1.f;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown C4ShaderParam6")) float C4ShaderParam6 = 1.f;
    /** Scales the light that local lights send through a two-sided foliage material. The game sets 0.5 on 26 masters. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Foliage light transmission")) float C4ShaderParam7 = 1.f;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown C4ShaderParam8")) float C4ShaderParam8 = 1.f;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown C4ShaderParam9")) float C4ShaderParam9 = 1.f;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown C4ShaderParam10")) float C4ShaderParam10 = 1.f;
    /** Keeps some specular on a two-sided foliage material in the sun's shadow, at the level the scene's lighting sets. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Foliage specular in shadow")) bool bC4MaterialParam0 = false;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown bC4MaterialParam1")) bool bC4MaterialParam1 = false;
    /** The filter of the material's shadow. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Shadow filter")) TEnumAsByte<EC4ShadowPCF> C4MaterialParam2 = C4PCF_1x1;
    /** Cuts a translucent material at the scene's clipping plane. Kadan does not compile this yet. */
    UPROPERTY(VisibleAnywhere, Category = "C4", meta = (DisplayName = "Scene clipping")) TEnumAsByte<EC4XluCut> C4MaterialParam3 = C4XC_Default;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown bC4MaterialParam4")) bool bC4MaterialParam4 = false;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown bC4MaterialParam5")) bool bC4MaterialParam5 = false;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown bC4MaterialParam6")) bool bC4MaterialParam6 = false;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown bC4MaterialParam7")) bool bC4MaterialParam7 = false;
    /** The filter of the material's forward shadow. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Forward shadow filter")) TEnumAsByte<EC4ShadowForwardPCF> C4MaterialParam8 = C4FWPCF_Disable;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown bC4MaterialParam9")) bool bC4MaterialParam9 = false;
    /** Leaves out the pixels of a translucent material where it is fully transparent. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Skip fully transparent pixels")) bool bC4MaterialParam10 = false;
    /** This setting's effect is not known. Keep its current value. */
    UPROPERTY(EditAnywhere, Category = "C4", AdvancedDisplay, meta = (DisplayName = "Unknown bC4MaterialParam11")) bool bC4MaterialParam11 = false;
    /** Puts a translucent material in the game's separate translucency pass named Full. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Separate translucency Full", C4MaterialField)) bool bC4EnableSeparateTranslucencyFull = false;
    /** Puts a translucent material in the game's separate translucency pass named Full Second. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Separate translucency Full Second", C4MaterialField)) bool bC4EnableSeparateTranslucencyFullSecond = false;
    /** Reverses the material's depth test. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Reverse depth test", C4MaterialField)) bool bC4ReverseDepthTest = false;
    /** Writes depth for a translucent material. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Translucent depth write", C4MaterialField)) bool bC4XluDepthWrite = false;
    /** The game's own lighting of a translucent surface, used by its water. It applies with the Translucency Lighting Mode Surface Per Pixel Lighting. Stock keeps that mode as Unreal lights it. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Translucency lighting mode")) TEnumAsByte<EC4TranslucencyLightingMode> C4TranslucencyLightingMode = C4TLM_Stock;
    /** This game decal mode writes the water wetness mask. Base Color's red controls wetness, and pixels with zero red are skipped. Green and blue feed the mask's blue and red channels. Use a Deferred Decal material. Stock keeps the material's Decal Blend Mode. */
    UPROPERTY(EditAnywhere, Category = "C4", meta = (DisplayName = "Decal blend mode")) TEnumAsByte<EC4DecalBlendMode> C4DecalBlendMode = C4DBM_Stock;

    virtual bool IsEditorOnly() const override { return true; }
    virtual bool NeedsLoadForClient() const override { return false; }
    virtual bool NeedsLoadForServer() const override { return false; }
    virtual void PostLoad() override;
    virtual void PostDuplicate(bool bDuplicateForPIE) override;
    virtual void BeginDestroy() override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
#endif

    // The settings as the format's defines: every setting by its name, as a number (an enum by its value).
    void Defines(TArray<TPair<FString, FString>>& Out) const;
    // Tells the format the material's current settings (after a change made in code).
    void Publish() const;

    static UP4MaterialGameSettings* Find(const UMaterial* Material);
    // The material's settings, added to it with the defaults when it has none.
    static UP4MaterialGameSettings* FindOrAdd(UMaterial* Material);
};

// Registers the settings with the shader format and their category on the material's Details panel.
void P4RegisterMaterialGameSettings();
void P4UnregisterMaterialGameSettings();

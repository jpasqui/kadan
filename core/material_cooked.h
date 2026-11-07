// Cooked material policy, inherited root-first without consulting material names.
#pragma once
#include "property_decoder.h"
#include <map>
#include <set>

namespace P4MapKit
{
struct FMaterialCookedRecord
{
    std::string Package, Class;
    FDecodedValue Properties;
};
inline bool ReadMaterialCookedRecord(const FPackageInventory& Package, const std::vector<std::uint8_t>& Body,
                                     FMaterialCookedRecord& Out, FReadError& Error)
{
    if (!Package.Error.Code.empty())
    {
        Error = Package.Error;
        return false;
    }
    for (const auto& Export : Package.Exports)
    {
        std::string Class;
        if (!Package.Reference(Export.Class, Class))
        {
            Error = {Export.Offset, "material-class-reference"};
            return false;
        }
        Class = Class.substr(Class.find_last_of('.') == std::string::npos ? 0 : Class.find_last_of('.') + 1);
        if (Class != "Material" && Class != "MaterialInstanceConstant")
            continue;
        if (Export.Offset > Body.size() || Export.Size > Body.size() - Export.Offset)
        {
            Error = {Export.Offset, "material-export-range"};
            return false;
        }
        FPropertyDecoder Decoder(Package, Body);
        FMaterialCookedRecord Record;
        Record.Package = Package.Package;
        Record.Class = Class;
        auto Position = Export.Offset;
        if (!Decoder.TypedProperties(Position, Export.Offset + Export.Size, Record.Properties))
        {
            Error = Decoder.Error;
            return false;
        }
        Error = {};
        Out = std::move(Record);
        return true;
    }
    Error = {0, "material-export-not-found"};
    return false;
}
struct FMaterialCookedSettings
{
    std::string Master, Domain = "MD_Surface", BlendMode = "BLEND_Opaque", ShadingModel = "MSM_DefaultLit";
    bool TwoSided = false;
    float Clip = 0.3333f;
    std::vector<std::string> Usage;
    std::map<std::string, FDecodedValue> GameProperties, GameOverrides, RenderSettings;
};
inline std::uint32_t MaterialFloatBits(float V)
{
    std::uint32_t Bits;
    std::memcpy(&Bits, &V, 4);
    return Bits;
}
inline bool MaterialNumber(const FDecodedValue& V, float& Out)
{
    if (V.Kind != FDecodedValue::EKind::Number)
        return false;
    std::istringstream S(V.Text);
    S.imbue(std::locale::classic());
    S >> Out;
    return !S.fail() && S.eof() && std::isfinite(Out);
}
inline bool MaterialBool(const FDecodedValue& V, bool& Out)
{
    if (V.Kind != FDecodedValue::EKind::Boolean || (V.Text != "true" && V.Text != "false"))
        return false;
    Out = V.Text == "true";
    return true;
}
inline bool MaterialEnum(const FDecodedValue& V, std::string& Out)
{
    if (V.Kind != FDecodedValue::EKind::Enum && V.Kind != FDecodedValue::EKind::Name &&
        V.Kind != FDecodedValue::EKind::String)
        return false;
    Out = V.Text;
    return true;
}
inline bool ReadMaterialCookedSettings(const std::vector<FMaterialCookedRecord>& Chain, FMaterialCookedSettings& Out,
                                       FReadError& Error)
{
    Error = {};
    FMaterialCookedSettings Result;
    const auto Reject = [&](const std::string& Code)
    {
        Error = {0, Code};
        return false;
    };
    if (Chain.empty() || Chain.size() > 1024 || Chain.front().Class != "Material")
        return Reject("material-parent-root");
    std::set<std::string> Packages;
    for (const auto& Record : Chain)
    {
        if (Record.Package.empty() || !Packages.insert(PropertyIdentity(Record.Package)).second)
            return Reject("material-parent-cycle");
        if (Record.Properties.Kind != FDecodedValue::EKind::Object)
            return Reject("material-properties-type");
    }
    Result.Master = Chain.front().Package;
    const auto& Master = Chain.front().Properties;
    const auto String = [&](const FDecodedValue& P, const char* Key, std::string& V)
    {
        auto F = P.Field(Key);
        return !F || MaterialEnum(*F, V);
    };
    const auto Boolean = [&](const FDecodedValue& P, const char* Key, bool& V)
    {
        auto F = P.Field(Key);
        return !F || MaterialBool(*F, V);
    };
    const auto Number = [&](const FDecodedValue& P, const char* Key, float& V)
    {
        auto F = P.Field(Key);
        return !F || MaterialNumber(*F, V);
    };
    if (!String(Master, "MaterialDomain", Result.Domain) || !String(Master, "BlendMode", Result.BlendMode) ||
        !String(Master, "ShadingModel", Result.ShadingModel) || !Boolean(Master, "TwoSided", Result.TwoSided) ||
        !Number(Master, "OpacityMaskClipValue", Result.Clip))
        return Reject("material-setting-type");
    static const char* Models[] = {"MSM_Unlit",
                                   "MSM_DefaultLit",
                                   "MSM_Subsurface",
                                   "MSM_PreintegratedSkin",
                                   "MSM_ClearCoat",
                                   "MSM_SubsurfaceProfile",
                                   "MSM_TwoSidedFoliage",
                                   "MSM_Hair",
                                   "MSM_Cloth",
                                   "MSM_Eye",
                                   "MSM_SingleLayerWater",
                                   "MSM_ThinTranslucent"};
    std::size_t Model = 0;
    while (Model < 12 && Result.ShadingModel != Models[Model])
        ++Model;
    if (Model == 12)
        return Reject("material-shading-model");
    if (const auto S = Master.Field("ShadingModels"))
        if (const auto Field = S->Field("ShadingModelField"))
        {
            // Integer fields are parsed as integers, never narrowed through float.
            std::uint64_t Bits = 0;
            std::istringstream Input(Field->Text);
            Input.imbue(std::locale::classic());
            Input >> Bits;
            if (Field->Kind != FDecodedValue::EKind::Number || Input.fail() || !Input.eof() ||
                Bits != (std::uint64_t(1) << Model))
                return Reject("material-shading-model-field");
        }
    if (Result.Domain == "MD_UI" || Result.Domain == "MD_PostProcess" || Result.Domain == "MD_LightFunction")
        Result.ShadingModel = "MSM_Unlit";
    else if (Result.Domain == "MD_DeferredDecal")
        Result.ShadingModel = "MSM_DefaultLit";
    static const char* Flags[] = {"bUsedWithSkeletalMesh",
                                  "bUsedWithMorphTargets",
                                  "bUsedWithParticleSprites",
                                  "bUsedWithBeamTrails",
                                  "bUsedWithMeshParticles",
                                  "bUsedWithNiagaraSprites",
                                  "bUsedWithNiagaraRibbons",
                                  "bUsedWithNiagaraMeshParticles",
                                  "bUsedWithInstancedStaticMeshes",
                                  "bUsedWithSplineMeshes",
                                  "bUsedWithClothing",
                                  "bUsedWithGeometryCache",
                                  "bUsedWithStaticLighting"};
    for (const auto Flag : Flags)
    {
        bool V = false;
        if (!Boolean(Master, Flag, V))
            return Reject("material-usage-type");
        if (V)
            Result.Usage.push_back(Flag);
    }
    std::sort(Result.Usage.begin(), Result.Usage.end());
    for (const auto& F : Master.Fields)
    {
        const auto At = F.first.find_first_not_of('b');
        const auto K = F.second.Kind;
        const bool Scalar = K == FDecodedValue::EKind::Number || K == FDecodedValue::EKind::Boolean ||
                            K == FDecodedValue::EKind::String || K == FDecodedValue::EKind::Name ||
                            K == FDecodedValue::EKind::Enum || K == FDecodedValue::EKind::SoftPath ||
                            (K == FDecodedValue::EKind::HardReference && F.second.ReferenceIndex != 0);
        if (At != std::string::npos && F.first.compare(At, 2, "C4") == 0 && Scalar)
            Result.GameProperties.insert(F);
    }
    const std::set<std::string> Ordinary = {"bOverride_BlendMode", "bOverride_ShadingModel", "bOverride_TwoSided",
                                            "bOverride_OpacityMaskClipValue"};
    for (std::size_t N = 1; N < Chain.size(); ++N)
    {
        if (Chain[N].Class != "MaterialInstanceConstant")
            return Reject("material-parent-instance");
        const FDecodedValue Empty;
        const auto Ptr = Chain[N].Properties.Field("BasePropertyOverrides");
        const auto& P = Ptr ? *Ptr : Empty;
        if (P.Kind != FDecodedValue::EKind::Object)
            return Reject("material-overrides-type");
        FMaterialCookedSettings Copied;
        Copied.Clip = 0.333f;
        if (!String(P, "BlendMode", Copied.BlendMode) || !String(P, "ShadingModel", Copied.ShadingModel) ||
            !Boolean(P, "TwoSided", Copied.TwoSided) || !Number(P, "OpacityMaskClipValue", Copied.Clip))
            return Reject("material-setting-type");
        bool B = false;
        if (!Boolean(P, "bOverride_BlendMode", B))
            return Reject("material-override-flag");
        if (B)
            Result.BlendMode = Copied.BlendMode;
        B = false;
        if (!Boolean(P, "bOverride_ShadingModel", B))
            return Reject("material-override-flag");
        if (B)
            Result.ShadingModel = Copied.ShadingModel;
        B = false;
        if (!Boolean(P, "bOverride_TwoSided", B))
            return Reject("material-override-flag");
        if (B)
            Result.TwoSided = Copied.TwoSided;
        B = false;
        if (!Boolean(P, "bOverride_OpacityMaskClipValue", B))
            return Reject("material-override-flag");
        if (B)
            Result.Clip = Copied.Clip;
        if (Copied.BlendMode != Result.BlendMode || Copied.ShadingModel != Result.ShadingModel ||
            Copied.TwoSided != Result.TwoSided || MaterialFloatBits(Copied.Clip) != MaterialFloatBits(Result.Clip))
            return Reject("material-copied-settings-disagree:" + Chain[N].Package);
        for (const auto& F : P.Fields)
            if (F.first.compare(0, 10, "bOverride_") == 0 && !Ordinary.count(F.first))
            {
                if (!MaterialBool(F.second, B))
                    return Reject("material-override-flag");
                if (B)
                {
                    const auto Key = F.first.substr(10);
                    auto V = P.Field(Key);
                    if (!V)
                        V = P.Field("b" + Key);
                    Result.GameOverrides[Key] = V ? *V : FDecodedValue::Atom(FDecodedValue::EKind::Null, "");
                }
            }
    }
    // Retain renderer fields as typed data. Their source-checked defaults below
    // apply only to lit translucent/additive surface policy, matching Python.
    const bool Lit = Result.Domain == "MD_Surface" && Result.ShadingModel != "MSM_Unlit" &&
                     (Result.BlendMode == "BLEND_Translucent" ||
                      (Result.BlendMode == "BLEND_Additive" && Result.ShadingModel == "MSM_DefaultLit"));
    if (Lit)
    {
        static const std::pair<const char*, const char*> Defaults[] = {
            {"TranslucencyLightingMode", "TLM_VolumetricNonDirectional"},
            {"bEnableSeparateTranslucency", "true"},
            {"bEnableMobileSeparateTranslucency", "false"},
            {"bUseTranslucencyVertexFog", "true"},
            {"bApplyCloudFogging", "false"},
            {"bEnableResponsiveAA", "false"},
            {"bScreenSpaceReflections", "false"},
            {"bContactShadows", "false"},
            {"bDisableDepthTest", "false"},
            {"bOutputTranslucentVelocity", "false"},
            {"RefractionDepthBias", "0"},
            {"TranslucencyDirectionalLightingIntensity", "1"},
            {"TranslucentShadowDensityScale", "0.5"},
            {"TranslucentSelfShadowDensityScale", "2"},
            {"TranslucentSelfShadowSecondDensityScale", "10"},
            {"TranslucentSelfShadowSecondOpacity", "0"},
            {"TranslucentBackscatteringExponent", "30"},
            {"TranslucentShadowStartOffset", "100"}};
        for (const auto& D : Defaults)
        {
            auto V = Master.Field(D.first);
            const auto Kind = D.first[0] == 'b'                                    ? FDecodedValue::EKind::Boolean
                              : std::string(D.first) == "TranslucencyLightingMode" ? FDecodedValue::EKind::Enum
                                                                                   : FDecodedValue::EKind::Number;
            Result.RenderSettings[D.first] = V ? *V : FDecodedValue::Atom(Kind, D.second);
        }
        FDecodedValue Ext;
        for (const auto& C :
             std::vector<std::pair<std::string, std::string>>{{"R", "1"}, {"G", "0.833"}, {"B", "0.588"}, {"A", "1"}})
            Ext.Fields.emplace_back(C.first, FDecodedValue::Atom(FDecodedValue::EKind::Number, C.second));
        const auto E = Master.Field("TranslucentMultipleScatteringExtinction");
        Result.RenderSettings["TranslucentMultipleScatteringExtinction"] = E ? *E : Ext;
        std::string Mode;
        if (!MaterialEnum(Result.RenderSettings["TranslucencyLightingMode"], Mode))
            return Reject("material-lighting-mode-type");
        const std::set<std::string> Modes = {"TLM_VolumetricNonDirectional",
                                             "TLM_VolumetricDirectional",
                                             "TLM_VolumetricPerVertexNonDirectional",
                                             "TLM_VolumetricPerVertexDirectional",
                                             "TLM_Surface",
                                             "TLM_SurfacePerPixelLighting",
                                             "TLM_SurfacePerPixelLightingC4Custom0",
                                             "TLM_SurfacePerPixelLightingC4Custom1"};
        if (!Modes.count(Mode))
            return Reject("material-lighting-mode");
    }
    for (const auto* Flag : {"bFullyRough", "bAllowNegativeEmissiveColor"})
    {
        bool V = false;
        const auto& P = std::string(Flag) == "bFullyRough" ? Master : Chain.back().Properties;
        if (!Boolean(P, Flag, V))
            return Reject("material-render-flag");
        if (V)
            Result.RenderSettings[Flag] = FDecodedValue::Atom(FDecodedValue::EKind::Boolean, "true");
    }
    Out = std::move(Result);
    return true;
}
inline bool ReadMaterialSamplerType(const std::string& Class, const FDecodedValue& Props, std::string& Out,
                                    FReadError& Error)
{
    Error = {};
    std::string Group, Compression = "TC_Default";
    bool SRGB = true;
    if (Class == "MediaTexture")
    {
        Out = "SAMPLERTYPE_External";
        return true;
    }
    auto G = Props.Field("LODGroup");
    auto C = Props.Field("CompressionSettings");
    auto S = Props.Field("SRGB");
    if ((G && !MaterialEnum(*G, Group)) || (C && !MaterialEnum(*C, Compression)) || (S && !MaterialBool(*S, SRGB)))
    {
        Error = {0, "material-sampler-setting-type"};
        return false;
    }
    if (Group == "TEXTUREGROUP_8BitData" || Group == "TEXTUREGROUP_16BitData")
        Out = "SAMPLERTYPE_Data";
    else if (Compression == "TC_Normalmap")
        Out = "SAMPLERTYPE_Normal";
    else if (Compression == "TC_Grayscale")
        Out = SRGB ? "SAMPLERTYPE_Grayscale" : "SAMPLERTYPE_LinearGrayscale";
    else if (Compression == "TC_Alpha")
        Out = "SAMPLERTYPE_Alpha";
    else if (Compression == "TC_Masks")
        Out = "SAMPLERTYPE_Masks";
    else if (Compression == "TC_DistanceFieldFont")
        Out = "SAMPLERTYPE_DistanceFieldFont";
    else
        Out = SRGB ? "SAMPLERTYPE_Color" : "SAMPLERTYPE_LinearColor";
    return true;
}
}

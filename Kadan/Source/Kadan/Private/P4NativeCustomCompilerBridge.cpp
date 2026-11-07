#include "P4NativeCustomCompilerBridge.h"
#include "P4GamePaths.h"
#include "P4SwitchShaderMapFreeze.h"
#include "P4AuthoringRows.h"
#include "P4OriginalRecord.h"
#include "P4GameReplacement.h"
#include "P4CreatorSource.h"
#include "P4GamePreviewWorld.h"
#include "P4MaterialGraph.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "UObject/UnrealType.h"
#include "P4MapKitCore/native_custom_backend_translucent.h"
#include "P4MapKitCore/native_custom_backend_translucent_lit.h"
#include "P4MapKitCore/own_export.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialEditor/PreviewMaterial.h"
namespace
{
using namespace P4MapKit::CustomMaterial;
using P4MapKit::Material::Json;
std::string Utf8(const FString& Text)
{
    FTCHARToUTF8 Value(*Text);
    return std::string(Value.Get(), Value.Length());
}
FString Wide(const std::string& Text)
{
    return FString(UTF8_TO_TCHAR(Text.c_str()));
}
struct FGraphCapture
{
    Json Graph; // the graph as the material checks read it (plain texture samples through aliases)
    Json OwnGraph; // the same with each plain texture sample as a texture parameter, as Kadan's own map names it
    bool Lit = false, Opaque = false, Masked = false;
    std::map<std::string, std::string> TextureAliases;
    std::vector<TextureParameter> TextureList;
    TMap<FName, UTexture*> Textures;
};
FGraphCapture CaptureGraph(UMaterial* Material, bool RequireCreatorOrigin = true)
{
    if (!IsInGameThread() || !IsValid(Material))
        throw std::runtime_error("Choose a material authored in this mod.");
    // Stock MaterialEditor duplicates the graph as UPreviewMaterial. Read it
    // only for UI feedback; producer entries still require exact UMaterial.
    const bool EditorPreview = !RequireCreatorOrigin && Material->GetClass() == UPreviewMaterial::StaticClass() &&
        Material->GetOutermost() == GetTransientPackage() && Material->bIsPreviewMaterial;
    if (Material->GetClass() != UMaterial::StaticClass() && !EditorPreview)
        throw std::runtime_error("Choose a material authored in this mod.");
    const bool CheckCreatorOrigin = RequireCreatorOrigin && !P4IsTrustedGamePreviewEngineCopy(Material);
    if (CheckCreatorOrigin)
    {
        FString OriginReason;
        if (!P4IsCreatorAsset(Material, &OriginReason))
            throw std::runtime_error(Utf8(
                OriginReason.IsEmpty() ? TEXT("Create this material with Kadan's material action.") : OriginReason));
        const FP4OriginalStatus Original = P4QueryOriginalAsset(Material);
        if (Original.IsOriginal() || P4HasCopiedGameData(Material) || P4IsVerifiedStandIn(Material) ||
            Original.State == EP4OriginalState::NeedsMigration || Original.State == EP4OriginalState::Unreadable ||
            Original.State == EP4OriginalState::UnsupportedRecord)
            throw std::runtime_error(
                "A game material or its copied graph cannot be compiled as a material made from scratch.");
    }
    const EBlendMode Blend = Material->GetBlendMode();
    if ((CheckCreatorOrigin && !P4IsModFolderPackage(Material->GetOutermost()->GetName())) ||
        Material->MaterialDomain != MD_Surface ||
        (Blend != BLEND_Translucent && Blend != BLEND_Opaque && Blend != BLEND_Masked) ||
        !(Material->GetShadingModels().HasShadingModel(MSM_Unlit) ||
            Material->GetShadingModels().HasShadingModel(MSM_DefaultLit)))
        throw std::runtime_error(
            "Choose a Surface material with Opaque, Masked or Translucent blend and either Unlit or Default Lit shading.");
    if (Material->TwoSided && Blend == BLEND_Translucent)
        throw std::runtime_error("Two-sided translucent materials cannot be exported yet. Turn off Two Sided.");
    const EMaterialShadingModel SelectedModel = Material->GetShadingModels().HasShadingModel(MSM_Unlit)
        ? MSM_Unlit
        : MSM_DefaultLit;
    for (int32 Model = 0; Model < int32(MSM_NUM); ++Model)
        if (Model != int32(SelectedModel) && Material->GetShadingModels().HasShadingModel(EMaterialShadingModel(Model)))
            throw std::runtime_error("Choose either Unlit or Default Lit as the material's only shading model.");
    const bool Lit = SelectedModel == MSM_DefaultLit;
    const bool Masked = Blend == BLEND_Masked;
    const bool Opaque = Blend == BLEND_Opaque || Masked;
    if (Masked && !Lit)
        throw std::runtime_error("Masked materials need Default Lit shading.");
    if (!Opaque && Lit && Material->TranslucencyLightingMode != TLM_Surface)
    {
        const FString Mode =
            StaticEnum<ETranslucencyLightingMode>()->GetDisplayNameTextByValue(int64(TLM_Surface)).ToString();
        throw std::runtime_error(
            Utf8(FString::Printf(TEXT("In the material's Translucency settings, set Lighting Mode to %s."), *Mode)));
    }
    const auto SettingName = [](const TCHAR* Name)
    {
        const FProperty* Property = FindFProperty<FProperty>(UMaterial::StaticClass(), Name);
        return Property ? Property->GetDisplayNameText().ToString() : FString(Name);
    };
    if (!Opaque && bool(Material->bEnableSeparateTranslucency) == Lit)
        throw std::runtime_error(Utf8(FString::Printf(TEXT("In Translucency settings, %s %s for this %s material."),
            Lit ? TEXT("disable") : TEXT("enable"), *SettingName(TEXT("bEnableSeparateTranslucency")),
            Lit ? TEXT("lit") : TEXT("unlit"))));
    if (!Opaque && bool(Material->bUseTranslucencyVertexFog) == Lit)
        throw std::runtime_error(Utf8(FString::Printf(TEXT("In Translucency settings, %s %s for this %s material."),
            Lit ? TEXT("disable") : TEXT("enable"), *SettingName(TEXT("bUseTranslucencyVertexFog")),
            Lit ? TEXT("lit") : TEXT("unlit"))));
    FString Error;
    const FString Source = P4MaterialGraphJson(Material, Error);
    if (!Error.IsEmpty() || Source.IsEmpty())
        throw std::runtime_error(Utf8(Error.IsEmpty() ? TEXT("The material graph could not be read.") : Error));
    FGraphCapture Out;
    Out.Lit = Lit;
    Out.Opaque = Opaque;
    Out.Masked = Masked;
    Out.Graph = Json::Parse(Utf8(Source), 1u << 20);
    // Read the current renderer settings directly even if an old recovery
    // record omitted them from its graph presentation.
    auto& Render = Out.Graph["render_settings"];
    if (Render.kind != Json::Object)
        Render = Json::MakeObject();
    Render["TranslucencyLightingMode"] = Json(Utf8(StaticEnum<ETranslucencyLightingMode>()->GetNameStringByValue(
        int64(Material->TranslucencyLightingMode.GetValue()))));
    Render["bEnableSeparateTranslucency"] = Json(bool(Material->bEnableSeparateTranslucency));
    Render["bUseTranslucencyVertexFog"] = Json(bool(Material->bUseTranslucencyVertexFog));
    Render["MaterialDecalResponse"] = Json(Utf8(
        StaticEnum<EMaterialDecalResponse>()->GetNameStringByValue(int64(Material->MaterialDecalResponse.GetValue()))));
    // OpacityMaskClipValue is an editor setting on every material; only a masked material clips on it.
    if (Masked)
        Out.Graph["clip"] = Json(double(Material->OpacityMaskClipValue));
    else
        Out.Graph.object.erase("clip");
    auto& Nodes = Out.Graph["nodes"];
    TMap<FString, int32> Counts;
    std::map<std::string, std::string> PlainSamples; // node id -> the parameter name it is given
    unsigned ConstantTexture = 0;
    for (UMaterialExpression* Expression : Material->Expressions)
    {
        if (!Expression || Expression->IsA<UMaterialExpressionClearCoatNormalCustomOutput>())
            continue;
        FString Type = Expression->GetClass()->GetName();
        Type.RemoveFromStart(TEXT("MaterialExpression"));
        const FString Id = FString::Printf(TEXT("%s_%d"), *Type, Counts.FindOrAdd(Type)++);
        if (!Nodes.Has(Utf8(Id)))
            throw std::runtime_error("The graph changed while its parameters were read.");
        auto& Props = Nodes[Utf8(Id)]["props"];
        if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
        {
            if (Scalar->ParameterName.IsNone())
                throw std::runtime_error("Give each scalar parameter a name.");
            Props["ParameterName"] = Json(Utf8(Scalar->ParameterName.ToString()));
            Props["DefaultValue"] = Json(double(Scalar->DefaultValue));
        }
        if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
        {
            if (Vector->ParameterName.IsNone())
                throw std::runtime_error("Give each vector parameter a name.");
            Props["ParameterName"] = Json(Utf8(Vector->ParameterName.ToString()));
            Json Color = Json::MakeArray();
            for (float Value :
                {Vector->DefaultValue.R, Vector->DefaultValue.G, Vector->DefaultValue.B, Vector->DefaultValue.A})
                Color.array.push_back(Json(double(Value)));
            Props["DefaultValue"] = Color;
        }
        if (UMaterialExpressionTextureSample* Texture = Cast<UMaterialExpressionTextureSample>(Expression))
        {
            if (!Texture->Texture || !Texture->Texture->IsA<UTexture2D>())
                throw std::runtime_error("Choose a 2D texture for each texture sample.");
            FString Parameter;
            if (const UMaterialExpressionTextureSampleParameter* Named =
                    Cast<UMaterialExpressionTextureSampleParameter>(Texture))
                Parameter = Named->ParameterName.ToString();
            else
                Parameter = FString::Printf(TEXT("KadanTexture%d"), ConstantTexture++);
            if (Parameter.IsEmpty() || Parameter == TEXT("None"))
                throw std::runtime_error("Give each texture parameter a name.");
            const FName Name(*Parameter);
            if (UTexture* const* Existing = Out.Textures.Find(Name))
            {
                if (*Existing != Texture->Texture)
                    throw std::runtime_error(
                        "Texture nodes with the same parameter name need the same default texture.");
            }
            else
            {
                Out.Textures.Add(Name, Texture->Texture);
                Out.TextureList.push_back({Utf8(Parameter), 0});
            }
            Props["Texture"] = Json(Utf8(Texture->Texture->GetPathName()));
            if (Cast<UMaterialExpressionTextureSampleParameter>(Texture))
                Props["ParameterName"] = Json(Utf8(Parameter));
            else
            {
                const std::string PackagePath = Utf8(Texture->Texture->GetOutermost()->GetName());
                const auto Existing = Out.TextureAliases.find(PackagePath);
                if (Existing != Out.TextureAliases.end() && Existing->second != Utf8(Parameter))
                    throw std::runtime_error(
                        "Use a texture parameter when several plain sample nodes share a texture.");
                Out.TextureAliases[PackagePath] = Utf8(Parameter);
                PlainSamples[Utf8(Id)] = Utf8(Parameter);
            }
        }
    }
    const std::string Issue = Out.Opaque
        ? NativeBackend::OpaqueAuthoringIssue(Out.Graph, Out.TextureAliases, Out.TextureList, Out.Masked)
        : Out.Lit ? NativeBackend::TranslucentLitAuthoringIssue(Out.Graph, Out.TextureAliases, Out.TextureList)
                  : NativeBackend::TranslucentAuthoringIssue(Out.Graph, Out.TextureAliases, Out.TextureList);
    if (!Issue.empty())
        throw std::runtime_error(Issue);
    // Kadan's own map names every texture by a parameter: a plain sample node becomes one, under the name above.
    Out.OwnGraph = Out.Graph;
    for (const auto& Plain : PlainSamples)
    {
        auto& Node = Out.OwnGraph["nodes"][Plain.first];
        Node["type"] = Json(std::string("TextureSampleParameter2D"));
        Node["props"]["ParameterName"] = Json(Plain.second);
    }
    const std::string Missing = OwnExport::NotBuilt(Out.OwnGraph, OwnExport::Usage());
    if (!Missing.empty())
        throw std::runtime_error(Missing);
    return Out;
}
} //namespace
namespace
{
// CaptureGraph's creator-origin checks without its checks of what Kadan's own emitter can build: the UE route compiles
// any graph UE compiles, but only a material the creator made in this mod.
std::string CreatorOriginIssue(UMaterial* Material)
{
    if (!IsInGameThread() || !IsValid(Material) || Material->GetClass() != UMaterial::StaticClass())
        return "Choose a material authored in this mod.";
    if (P4IsTrustedGamePreviewEngineCopy(Material))
        return std::string();
    FString OriginReason;
    if (!P4IsCreatorAsset(Material, &OriginReason))
        return Utf8(OriginReason.IsEmpty() ? TEXT("Create this material with Kadan's material action.") : OriginReason);
    const FP4OriginalStatus Original = P4QueryOriginalAsset(Material);
    if (Original.IsOriginal() || P4HasCopiedGameData(Material) || P4IsVerifiedStandIn(Material) ||
        Original.State == EP4OriginalState::NeedsMigration || Original.State == EP4OriginalState::Unreadable ||
        Original.State == EP4OriginalState::UnsupportedRecord)
        return "A game material or its copied graph cannot be compiled as a material made from scratch.";
    if (!P4IsModFolderPackage(Material->GetOutermost()->GetName()))
        return "Save this material in the mod's own folder.";
    return std::string();
}
} //namespace
FString P4CreatorMaterialGraphIssue(UMaterial* Material)
{
    if (P4UseSwitchShaderRoute())
        return Wide(CreatorOriginIssue(Material));
    try
    {
        (void)CaptureGraph(Material);
        return FString();
    }
    catch (const std::exception& Error)
    {
        return Wide(Error.what());
    }
}
FString P4CreatorMaterialEditorGraphIssue(UMaterial* Material)
{
    // MaterialEditor reads a transient preview graph. This helper only reports
    // graph/settings issues; it does not compile or export.
    try
    {
        (void)CaptureGraph(Material, false);
        return FString();
    }
    catch (const std::exception& Error)
    {
        return Wide(Error.what());
    }
}
FString P4GameParentCompileIssue(UMaterial* Master)
{
    const FP4OriginalStatus Source = IsValid(Master) ? P4QueryOriginalAsset(Master) : FP4OriginalStatus();
    if (!Source.IsOriginal())
    {
        return FString();
    }
    const bool Recovered = Source.Record.SourceDigestKind == TEXT("material-graph-v2-md5") &&
        !P4IsVerifiedStandIn(Master) && Source.State != EP4OriginalState::NeedsMigration &&
        Source.State != EP4OriginalState::Unreadable && Source.State != EP4OriginalState::UnsupportedRecord;
    if (Recovered)
    {
        return FString();
    }
    return TEXT("Reimport the game parent material to recover its graph before compiling this instance's permutation.");
}
FP4CreatorMaterialCompileResult P4CompileCreatorMaterial(const FP4CreatorMaterialCompileRequest& Request)
{
    // Stage 8: UE's own shaders compiled for the game (P4SwitchShaderMapFreeze.h), while it is being proven.
    if (P4UseSwitchShaderRoute())
    {
        if (Request.CreatorInstance)
        {
            FString Reason;
            if (!P4IsGameReplacement(Request.CreatorInstance) && !P4IsCreatorAsset(Request.CreatorInstance, &Reason))
            {
                FP4CreatorMaterialCompileResult Refused;
                Refused.Error = Reason;
                return Refused;
            }
        }
        // A creator's child of a recovered game material needs a new permutation, too.
        // The game master remains a runtime reference; only this child's compiled map ships.
        const FP4OriginalStatus ParentSource = IsValid(Request.CreatorMaterial)
            ? P4QueryOriginalAsset(Request.CreatorMaterial)
            : FP4OriginalStatus();
        const bool GameParent = Request.CreatorInstance && IsValid(Request.CreatorMaterial) &&
            Request.CreatorInstance->GetMaterial() == Request.CreatorMaterial && ParentSource.IsOriginal();
        if (GameParent)
        {
            const FString ParentIssue = P4GameParentCompileIssue(Request.CreatorMaterial);
            if (!ParentIssue.IsEmpty())
            {
                FP4CreatorMaterialCompileResult Refused;
                Refused.Error = ParentIssue;
                return Refused;
            }
        }
        const std::string Issue = GameParent ? std::string() : CreatorOriginIssue(Request.CreatorMaterial);
        if (!Issue.empty())
        {
            FP4CreatorMaterialCompileResult Refused;
            Refused.Error = Wide(Issue);
            return Refused;
        }
        return P4CompileCreatorMaterialSwitch(Request);
    }
    FP4CreatorMaterialCompileResult Out;
    if (Request.CreatorInstance)
    {
        Out.Error = TEXT("Material instance permutations require Kadan's Switch shader compiler.");
        return Out;
    }
    try
    {
        const FGraphCapture Captured = CaptureGraph(Request.CreatorMaterial);
        if (!AssetPath(Utf8(Request.RuntimeObjectPath), true) || !P4IsModFolderPackage(Request.RuntimeObjectPath))
            throw std::runtime_error("The runtime material must belong to this mod.");
        // A texture's index in the map is its place in the cooked material's referenced textures, which the runtime
        // copy carries over from this graph.
        std::map<std::string, std::int32_t> Indices;
        const TArrayView<UObject* const> Referenced = Request.CreatorMaterial->GetReferencedTextures();
        for (const auto& Pair : Captured.Textures)
        {
            int32 Index = INDEX_NONE;
            for (int32 At = 0; At < Referenced.Num(); ++At)
                if (Referenced[At] == Pair.Value)
                {
                    Index = At;
                    break;
                }
            if (Index == INDEX_NONE)
                throw std::runtime_error(Utf8(FString::Printf(
                    TEXT(
                        "The texture %s is not in the material's texture list yet. Apply or save the material, then export again."),
                    *Pair.Value->GetName())));
            Indices[Utf8(Pair.Key.ToString())] = Index;
        }
        OwnExport::Usage Usage;
        Usage.instanced = Request.Instanced;
        const auto Built = OwnExport::Build(Captured.OwnGraph, Usage, Indices);
        Out.Block = Built.map.block;
        Out.MapHash = Built.map.mapHash;
        Out.Programs = Built.map.programs;
        Out.Versions = int32(Built.versions);
        Out.Succeeded = true;
    }
    catch (const std::exception& Error)
    {
        Out = FP4CreatorMaterialCompileResult();
        Out.Error = Wide(Error.what());
    }
    return Out;
}

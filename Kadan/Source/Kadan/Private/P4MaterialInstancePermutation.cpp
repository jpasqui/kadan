#include "P4MaterialInstancePermutation.h"
#include "P4MaterialInstanceGameSettings.h"
#include "P4OriginalRecord.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "SceneView.h"
#include "StaticParameterSet.h"
#include "P4MapKitCore/own_game_layouts.h"

bool P4MaterialInstanceHasStructuralPermutation(const UMaterialInstanceConstant* Instance)
{
    if (!Instance)
    {
        return false;
    }
    const auto Override = [](const auto& P) { return P.bOverride; };
    const auto& S = Instance->GetStaticParameters();
    const auto& B = Instance->BasePropertyOverrides;
    return Instance->bHasStaticPermutationResource || S.StaticSwitchParameters.ContainsByPredicate(Override) ||
        S.StaticComponentMaskParameters.ContainsByPredicate(Override) ||
        S.TerrainLayerWeightParameters.ContainsByPredicate(Override) ||
        S.MaterialLayersParameters.ContainsByPredicate(Override) || B.bOverride_BlendMode || B.bOverride_ShadingModel ||
        B.bOverride_TwoSided || B.bOverride_OpacityMaskClipValue || B.bOverride_DitheredLODTransition ||
        B.bOverride_CastDynamicShadowAsMasked;
}

bool P4IsGameViewParameter(FName Name)
{
    // The game's View uniform layout, read from its executable, names its members View_<member>.
    const std::string Member = std::string("View_") + TCHAR_TO_UTF8(*Name.ToString());
    if (!P4MapKit::CustomMaterial::OwnSwitch::GameLayoutOffsets().count({"View", Member}))
    {
        return false;
    }
    // Members the engine's own View has are read through engine nodes, never through a parameter.
    for (const auto& Engine : FViewUniformShaderParameters::StaticStructMetadata.GetMembers())
    {
        if (Name == FName(Engine.GetName()))
        {
            return false;
        }
    }
    return true;
}

bool P4MaterialInstanceOverridesRecoveredView(const UMaterialInstanceConstant* Instance)
{
    if (!Instance)
    {
        return false;
    }
    UMaterial* Master = const_cast<UMaterialInstanceConstant*>(Instance)->GetMaterial();
    if (!Master || !P4QueryOriginalAsset(Master).IsOriginal())
    {
        return false;
    }
    // A recovered master reads a game View member through a vector parameter of the member's name.
    return Master->Expressions.ContainsByPredicate(
        [&](const UMaterialExpression* E)
        {
            const auto* View = Cast<UMaterialExpressionVectorParameter>(E);
            if (!View || !P4IsGameViewParameter(View->ParameterName))
            {
                return false;
            }
            return Instance->VectorParameterValues.ContainsByPredicate(
                [&](const auto& P)
                {
                    return P.ParameterInfo.Name == View->ParameterName &&
                        P.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter &&
                        P.ParameterInfo.Index == INDEX_NONE;
                });
        });
}

bool P4MaterialInstanceNeedsOwnShaderMap(const UMaterialInstanceConstant* Instance)
{
    return P4MaterialInstanceHasStructuralPermutation(Instance) || P4MaterialInstanceOverridesRecoveredView(Instance) ||
        P4MaterialInstanceHasC4Overrides(Instance);
}

void P4MaterialInstancePrepareRecoveredViewPermutation(
    const UMaterialInstanceConstant* Source, UMaterialInstanceConstant* Copy)
{
    check(Source && Copy && Source != Copy);
    if (P4MaterialInstanceHasStructuralPermutation(Source) ||
        (!P4MaterialInstanceOverridesRecoveredView(Source) && !P4MaterialInstanceHasC4Overrides(Source)))
    {
        return;
    }
    FStaticParameterSet Static;
    Copy->GetStaticParameterValues(Static);
    auto* Trigger = Static.StaticSwitchParameters.FindByPredicate(
        [](const auto& P)
        {
            return P.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter &&
                P.ParameterInfo.Index == INDEX_NONE;
        });
    if (Trigger)
    {
        Trigger->bOverride = true; // Preserve the resolved value; only the native resource identity changes.
    }
    else
    {
        Static.StaticSwitchParameters.Add(FStaticSwitchParameter(
            FMaterialParameterInfo(TEXT("__KadanViewParameterPermutation")), false, true, FGuid()));
    }
    Copy->UpdateStaticPermutation(Static, Copy->BasePropertyOverrides);
}

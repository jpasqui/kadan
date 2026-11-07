#include "P4GameMaterialInstanceExport.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4ExportDriver.h"
#include "P4GameReplacement.h"
#include "P4MaterialInstancePermutation.h"
#include "P4MaterialInstanceGameSettings.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4ModDescriptor.h"
#include "P4OriginalRecord.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

bool P4GameMaterialInstanceAvailability(UObject* Asset, const FString& Root, bool CustomMaterials,
                                        FP4ExportAvailability& Out)
{
    FP4GameReplacement Replacement;
    if (!P4IsGameReplacement(Asset, &Replacement) || Replacement.Kind != TEXT("edited-game-material-instance"))
    {
        return false;
    }
    Out.Reason = P4MaterialInstanceExportIssue(CastChecked<UMaterialInstanceConstant>(Asset), Root, CustomMaterials);
    if (Out.Reason.IsEmpty())
    {
        Out.Route = EP4ExportAssetRoute::CreatorMaterialInstance;
        Out.Reason = TEXT("This edited game material instance is cooked whole at its original path.");
    }
    return true;
}

FString P4MaterialInstanceExportIssue(UMaterialInstanceConstant* Instance, const FString& Root, bool CustomMaterials)
{
    TSet<const UMaterialInstanceConstant*> Seen;
    for (const UMaterialInstanceConstant* Current = Instance; Current && !Seen.Contains(Current);
         Current = Cast<UMaterialInstanceConstant>(Current->Parent))
    {
        Seen.Add(Current);
        if (UP4MaterialInstanceGameSettings::Find(Current))
        {
            continue;
        }
        const FString Transport =
            Current->GetOutermost()->GetMetaData()->GetValue(Current, TEXT("P4MaterialInstanceTransportV2"));
        if (Transport.IsEmpty())
        {
            continue;
        }
        TSharedPtr<FJsonObject> Stored;
        if (!P4ParseJson(Transport, Stored))
        {
            return TEXT("Reimport the material instance with unreadable C4 source settings before exporting.");
        }
        const TSharedPtr<FJsonObject>* Overrides = nullptr;
        if (Stored->TryGetObjectField(TEXT("game_overrides"), Overrides) && (*Overrides)->Values.Num())
        {
            return TEXT("Reimport ") + Current->GetName() +
                   TEXT(" to recover its typed C4 settings before exporting this material instance.");
        }
    }
    if (Instance->GetClass() != UMaterialInstanceConstant::StaticClass())
    {
        return TEXT("Only plain material instances can be exported.");
    }
    // The same checks the export makes before it compiles this instance's own map.
    if (P4MaterialInstanceNeedsOwnShaderMap(Instance))
    {
        if (!P4UseSwitchShaderRoute())
        {
            return TEXT("Enable Kadan's Switch shader compiler to export this material instance's shader permutation.");
        }
        const FString ParentIssue = P4GameParentCompileIssue(Instance->GetMaterial());
        if (!ParentIssue.IsEmpty())
        {
            return ParentIssue;
        }
    }
    UMaterialInterface* Parent = Instance->Parent;
    if (!Parent)
    {
        return TEXT("Give your material instance a parent material from the game.");
    }
    const FString ParentPackage = Parent->GetOutermost()->GetName();
    const bool Own = P4IsModContentPath(Root, ParentPackage) && !P4QueryOriginalAsset(Parent).IsOriginal();
    // A material of this mod that cannot be exported says why, under its own name.
    if (Own && CustomMaterials)
    {
        if (UMaterial* Graph = Cast<UMaterial>(Parent))
        {
            const FString GraphIssue = P4CreatorMaterialGraphIssue(Graph);
            if (!GraphIssue.IsEmpty())
            {
                return TEXT("Its parent ") + Graph->GetName() + TEXT(" cannot be exported. ") + GraphIssue;
            }
        }
    }
    if (Own ? !(Parent->IsA<UMaterialInstanceConstant>() ||
                (CustomMaterials && Cast<UMaterial>(Parent) &&
                 P4CreatorMaterialGraphIssue(Cast<UMaterial>(Parent)).IsEmpty()))
            : P4IsModFolderPackage(ParentPackage) ||
                  !P4IsContentPackage(ParentPackage))
    {
        return TEXT(
            "The parent of your material instance has to be a game material or another material instance of this mod.");
    }
    // Native parameters are valid authored defaults. Recovered View aliases also
    // need a compiled map: the original game shader does not read a MIC value.
    return FString();
}

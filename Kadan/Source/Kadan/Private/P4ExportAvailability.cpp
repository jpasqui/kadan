// Whether and how each asset of a selection can be exported.

#include "P4ExportInternal.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpaceBase.h"

namespace P4Export
{
FP4ExportAvailability AssetAvailability(
    UObject* Asset, const FP4ModDescriptor& Mod, bool CustomMaterials, bool CompareWithGame)
{
    FP4ExportAvailability Result;
    if (!IsInGameThread() || !IsValid(Asset))
    {
        Result.Reason = TEXT("An editor asset is required");
        return Result;
    }
    FP4ModNames Names;
    if (!P4ModDerivedNames(Mod, Names, Result.Reason))
        return Result;
    const FP4OriginalStatus Original = P4QueryOriginalAsset(Asset);
    FP4GameReplacement GameReplacement;
    if (P4GameMaterialInstanceAvailability(Asset, Names.PackageRoot, CustomMaterials, Result))
    {
        return Result;
    }
    if (Original.IsOriginal() && P4IsGameReplacement(Asset, &GameReplacement) &&
        GameReplacement.Kind == TEXT("edited-game-actor-blueprint"))
    {
        const UBlueprint* Blueprint = CastChecked<UBlueprint>(Asset);
        if (Blueprint->Status == BS_Error)
        {
            Result.Reason = TEXT("Compile this Blueprint and fix its errors before exporting.");
            return Result;
        }
        Result.Route = EP4ExportAssetRoute::GameActorBlueprint;
        Result.Reason = TEXT(
            "This edited game Blueprint is cooked whole at its original path, including defaults, components and graphs.");
        return Result;
    }
    if (Original.IsOriginal())
    {
        if (Asset->IsA<UWorld>())
        {
            Result.Route = EP4ExportAssetRoute::OriginalLevelChanges;
            Result.Reason = TEXT(
                "Added actors, moves and removals can be exported, and gameplay objects you place go into the map's placement list. Replace changed game assets with your own content.");
        }
        else if (Asset->IsA<UDataTable>())
        {
            Result.Route = EP4ExportAssetRoute::OriginalTableFields;
            Result.Reason = TEXT("Your table changes are written in the mod's manifest.");
        }
        else if (Asset->GetClass() == UTexture2D::StaticClass() || Asset->GetClass() == UStaticMesh::StaticClass())
            Result.Reason = TEXT(
                "To ship your own in its place, right-click it in the Content Browser and choose Replace with my own file.");
        else
            Result.Reason = TEXT("Replace this game asset with content you created before exporting.");
        return Result;
    }
    if (Original.State != EP4OriginalState::NotImported)
    {
        Result.Reason = TEXT("Original asset state must be resolved before export");
        return Result;
    }
    if (P4IsVerifiedStandIn(Asset))
    {
        Result.Reason = TEXT("A Blueprint stand-in cannot be exported.");
        return Result;
    }
    if (const UWorld* World = Cast<UWorld>(Asset))
        if (World->PersistentLevel)
            for (const AActor* Actor : World->PersistentLevel->Actors)
                if (P4IsGameplayActor(Actor))
                {
                    Result.Reason = FString::Printf(
                        TEXT(
                            "%s is a gameplay object in a level of your own, where the game would keep nothing of it in a save. Place it in the game's map instead, and export puts it in the map's placement list."),
                        *Actor->GetName());
                    return Result;
                }
    // The creator's file in place of the game's asset stays at the game's path (K9). Every other asset of the
    // creator's ships at the path it has, in another mod's folder too.
    const bool Replacement = P4IsGameReplacement(Asset);
    if (P4IsGamePreviewSceneAsset(Asset))
    {
        Result.Route = EP4ExportAssetRoute::PreviewObject;
        Result.Reason = TEXT("Private preview scene scaffold.");
        return Result;
    }
    if (P4IsGamePreviewDerivedCopy(Asset))
    {
        Result.Route = EP4ExportAssetRoute::PreviewObject;
        Result.Reason = TEXT("Private copy of the authored level's actual built lighting data.");
        return Result;
    }
    if (Asset->GetClass() == UCurveLinearColor::StaticClass() && P4IsTrustedGamePreviewEngineCopy(Asset))
    {
        Result.Route = EP4ExportAssetRoute::PreviewObject;
        Result.Reason = TEXT("Private copy of an installed engine sky color curve.");
        return Result;
    }
    if (const UMapBuildDataRegistry* Registry = Cast<UMapBuildDataRegistry>(Asset))
    {
        // Lightmass creates this asset without a creator factory/source record.
        // Its name alone is insufficient: the authored level must actually use it.
        const FString Package = Registry->GetOutermost()->GetName();
        const FString Suffix(TEXT("_BuiltData"));
        const FString WorldPackage = Package.EndsWith(Suffix) ? Package.LeftChop(Suffix.Len()) : FString();
        UWorld* World = WorldPackage.IsEmpty()
            ? nullptr
            : LoadObject<UWorld>(nullptr, *P4ObjectPathOf(WorldPackage), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!World || !World->PersistentLevel || World->PersistentLevel->MapBuildData != Registry ||
            P4IsGamePreviewGameSource(World) || !P4IsContentPackage(WorldPackage))
        {
            Result.Reason = TEXT("Built lighting must be the lighting data actually used by a level of your own.");
            return Result;
        }
        Result.Route = EP4ExportAssetRoute::CreatorLighting;
        Result.Reason = TEXT("Your level's baked lighting and stationary light shadow channels are included.");
        return Result;
    }
    if (const UP4AddedContent* Definition = Cast<UP4AddedContent>(Asset))
    {
        TSharedPtr<FJsonObject> Declaration;
        if (P4AddedContentDeclaration(Definition, Names.PackageRoot, Declaration, Result.Reason))
        {
            Result.Route = EP4ExportAssetRoute::CreatorContent;
            Result.Reason = TEXT("The new treasure or creature is declared in the manifest.");
        }
        return Result;
    }
    if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        Result.Reason = P4CreatorBlueprintIssue(Blueprint, Names.PackageRoot);
        if (Result.Reason.IsEmpty())
        {
            Result.Route = EP4ExportAssetRoute::CreatorBlueprint;
            Result.Reason = TEXT("Your new Blueprint child class is included.");
        }
        return Result;
    }
    if (UWorld* World = Cast<UWorld>(Asset))
    {
        if (!World->PersistentLevel)
        {
            Result.Reason = TEXT("Your level has no persistent level.");
            return Result;
        }
        if (const ULevelScriptBlueprint* Script = World->PersistentLevel->GetLevelScriptBlueprint(true))
            if (Script->Status == BS_Error)
            {
                Result.Reason = TEXT("Compile your level script and fix its errors before saving the mod.");
                return Result;
            }
        Result.Route = EP4ExportAssetRoute::CreatorLevel;
        Result.Reason = TEXT("Your level and its level script can be loaded by the selected game map.");
        return Result;
    }
    if (const USoundWave* Sound = Cast<USoundWave>(Asset))
    {
        if (Sound->RawData.GetBulkDataSize() <= 0)
        {
            Result.Reason = TEXT("Import your PCM16 WAV before exporting this sound.");
            return Result;
        }
        if (!P4IsCreatorAsset(Sound, &Result.Reason))
            return Result;
        FString SameAsGame;
        if (CompareWithGame && P4FindGameContentMatch(Asset, SameAsGame))
        {
            Result.Reason = TEXT("This holds exactly the sound of the game's ") + SameAsGame +
                TEXT(", which the game already has. Use the game's sound, or change yours.");
            return Result;
        }
        Result.Route = EP4ExportAssetRoute::CreatorSound;
        Result.Reason = TEXT("Your sound is exported into this mod's sound bank for Tsuchi playback.");
        return Result;
    }
    if (!P4IsCreatorAsset(Asset, &Result.Reason) && !P4IsTrustedGamePreviewEngineCopy(Asset))
        return Result;
    FString SameAsGame;
    // A copied MIC has a separate closed meaningful-content contract. Unreadable
    // is a refusal, never an absent game-content match that permits export.
    const bool CopiedMIC = Asset->IsA<UMaterialInstanceConstant>() && P4HasCopiedGameData(Asset) &&
        !P4HasCreatorSource(Asset);
    if (CompareWithGame && CopiedMIC)
    {
        Result.Reason = P4MaterialInstanceExportIssue(
            CastChecked<UMaterialInstanceConstant>(Asset), Names.PackageRoot, CustomMaterials);
        if (!Result.Reason.IsEmpty())
            return Result;
        const FP4CopiedMICContent Compared = P4CompareCopiedMaterialInstanceContent(Asset);
        if (Compared.State == EP4CopiedMICContent::Unreadable || Compared.State == EP4CopiedMICContent::NotCopied)
        {
            Result.Reason = TEXT(
                "This copied material instance contains content that cannot yet be verified for export. Use the game's instance, or make a new instance with supported parameters.");
            return Result;
        }
        if (Compared.State == EP4CopiedMICContent::Equal)
        {
            Result.Reason = TEXT("This holds exactly the content of the game's ") + Compared.OriginalPackage +
                TEXT(", which the game already has. Use the game's asset, or change yours.");
            return Result;
        }
    }
    if (CompareWithGame && !CopiedMIC && !P4IsTrustedGamePreviewEngineCopy(Asset) &&
        P4FindGameContentMatch(Asset, SameAsGame))
    {
        Result.Reason = TEXT("This holds exactly the content of the game's ") + SameAsGame +
            TEXT(", which the game already has. Use the game's asset, or change yours.");
        return Result;
    }
    // Montages, composites and blend spaces are made of the creator's clips, as the game's are of its own.
    if (Asset->IsA<USkeletalMesh>() || Asset->IsA<USkeleton>() || Asset->IsA<UAnimSequence>() ||
        Asset->IsA<UAnimMontage>() || Asset->IsA<UAnimComposite>() || Asset->IsA<UBlendSpaceBase>())
    {
        if (const USkeletalMesh* Skinned = Cast<USkeletalMesh>(Asset))
            if (!Skinned->Skeleton || !Skinned->GetImportedModel() || !Skinned->GetImportedModel()->LODModels.Num())
            {
                Result.Reason = TEXT(
                    "This skeletal mesh has no geometry or no skeleton. Import it from your file before exporting it.");
                return Result;
            }
        Result.Route = EP4ExportAssetRoute::CreatorSkinned;
        Result.Reason =
            TEXT("Your skeletal mesh, skeleton, clips, montages and blend spaces can be exported with this mod.");
        return Result;
    }
    if (const UTexture2D* Texture = Cast<UTexture2D>(Asset))
    {
        if (!Texture->Source.IsValid())
            Result.Reason = TEXT("Your texture has no pixels yet. Import an image into it before exporting it.");
        else
        {
            Result.Route = EP4ExportAssetRoute::CreatorTexture;
            Result.Reason = Replacement
                ? TEXT("Your texture replaces the game's at its path, everywhere the game uses it.")
                : TEXT("Your texture can be exported with this mod.");
        }
    }
    else if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
    {
        Result.Reason = MeshIssue(Mesh);
        if (Result.Reason.IsEmpty())
        {
            Result.Route = EP4ExportAssetRoute::CreatorStaticMesh;
            Result.Reason = Replacement
                ? TEXT("Your mesh and its collision replace the game's at its path, everywhere the game uses it.")
                : TEXT("Your mesh and its collision can be exported with this mod.");
        }
    }
    else if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset))
    {
        Result.Reason = P4MaterialInstanceExportIssue(Instance, Names.PackageRoot, CustomMaterials);
        if (Result.Reason.IsEmpty())
        {
            Result.Route = EP4ExportAssetRoute::CreatorMaterialInstance;
            Result.Reason = TEXT("Your material instance can be exported with this mod.");
        }
    }
    else if (UMaterial* Graph = Cast<UMaterial>(Asset))
    {
        Result.Reason = P4CreatorMaterialGraphIssue(Graph);
        if (Result.Reason.IsEmpty())
        {
            Result.Route = EP4ExportAssetRoute::CreatorMaterial;
            Result.Reason = TEXT("Your material and its shader archive can be exported.");
        }
    }
    else
        Result.Reason = TEXT("Export is not available for this asset type yet.");
    return Result;
}
} // namespace P4Export

using P4Export::AssetAvailability;

FP4ExportAvailability P4ExportAssetAvailability(UObject* Asset, const FP4ModDescriptor& Mod)
{
    return AssetAvailability(Asset, Mod, true);
}

FP4ExportAvailability P4ExportAssetAvailabilityAtAGlance(UObject* Asset, const FP4ModDescriptor& Mod)
{
    return AssetAvailability(Asset, Mod, true, false);
}

FP4ExportAvailability P4CreatorPreflightAvailability(UObject* Asset, const FP4ModDescriptor& Mod, bool CustomMaterials)
{
    return AssetAvailability(Asset, Mod, CustomMaterials);
}

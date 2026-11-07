#pragma once
#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "P4CreatorSource.generated.h"

// Intrinsic editor data is copied by Unreal with the creator asset and survives ordinary rename
// and save/reopen. It is not exposed as an editable origin field or shipped in cooked content.
// Defaults contain no source facts; completed supported source operations populate them.
UCLASS(NotBlueprintType, HideDropdown)
class KADAN_API UP4CreatorSourceData : public UAssetUserData
{
    GENERATED_BODY()
public:
    virtual bool IsEditorOnly() const override { return true; }
private:
    friend struct FP4CreatorSourceAccess;
#if WITH_EDITORONLY_DATA
    UPROPERTY() int32 Format = 0;
    UPROPERTY() FName Kind;
    UPROPERTY() FName SourceMode;
    UPROPERTY() FName AssetClass;
    UPROPERTY() FName OriginPackage;
#endif
};

// Positive creator-source evidence, independent of editability and original-asset records.
// Reads owned instanced editor data only. It does not load, save or cook an asset.
// Source facts do not imply support for every feature of the asset.
KADAN_API bool P4HasCreatorSource(const UObject* Asset, FString* Reason = nullptr);
// Whose asset this is, for export, Review and the editor's own features (decided 2026-10-16): an asset that
// carries the game tag of its own package, given when Kadan imported it, is the game's. Every other asset is
// the creator's, however it was made, copies of game assets included. Review separately refuses a creator
// asset whose content is identical to a game asset's (P4FindGameContentMatch).
KADAN_API bool P4IsCreatorAsset(const UObject* Asset, FString* Reason = nullptr);
// Whether a class is one the game has: a native class of its script package, or a class a Blueprint makes
// that is not the creator's (P4IsCreatorAsset).
KADAN_API bool P4IsGameClass(const UClass* Class);
// Negative source is stamped only by genuine game publication/verified original/proxy boundaries.
// It travels with native copies and renames; the original package is a fact, not a path heuristic.
KADAN_API bool P4HasIntrinsicGameSource(const UObject* Asset, FString* SourcePackage = nullptr);
KADAN_API bool P4HasIntrinsicGameProxy(const UObject* Asset);
// Internal editor publication hook. No metadata creation, dirtying or saved/GC/postload mutation.
// Returns false for objects without an instanced AssetUserData interface or creator replacements.
bool P4RecordKnownGameSource(UObject* Asset, const FString& OriginalPackage, bool Proxy);
// Internal verified native fill seam. Call only after actual source/field comparisons and
// current original record succeed, before requested publication. The uncommitted guard owns
// complete prior subobject state; a failed save rolls this provisional in-place role back.
class FP4StandInFill;
bool P4PrepareVerifiedGameFillSource(UObject* Asset, const FP4StandInFill& Guard, FString& Error);

// Installed after the editor's import subsystem exists. Only completed supported native
// file-factory operations record source facts for their exact output; AssetAdded and copied import data do not.
void P4InitializeCreatorSource();
void P4ShutdownCreatorSource();

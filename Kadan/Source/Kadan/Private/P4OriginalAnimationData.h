#pragma once

#include "EditorFramework/AssetImportData.h"
#include "P4OriginalAnimationData.generated.h"

// Original compressed derived data belongs to the editor project. Its key includes the raw
// tracks, skeleton and compression settings, so an edit asks Unreal for a different cache entry.
// The entire object is editor-only and never accompanies a cooked mod asset.
UCLASS()
class KADAN_API UP4OriginalAnimationData : public UAssetImportData
{
    GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
    UPROPERTY() FString CacheSuffix;
    UPROPERTY() TArray<uint8> CacheBytes;
#endif
    virtual bool IsEditorOnly() const override { return true; }
    virtual void PostLoad() override;
    bool Seed() const;
};

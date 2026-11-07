#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
class UTexture2D;

// This verifies creator source only. It cannot approve an export without container verification.
class KADAN_API FP4CreatorTextureSource
{
public:
    bool IsVerified() const { return Verified; }
    const FString& Digest() const { return SourceDigest; }
    UTexture2D* Texture() const;
private:
    bool Verified = false;
    FString SourceDigest;
    TWeakObjectPtr<UTexture2D> Asset;
    friend KADAN_API bool P4VerifyCreatorTextureSource(UTexture2D*, const FString&, FP4CreatorTextureSource&, FString&);
};
// Freshly decodes PNG/JPEG/TGA/BMP source with the engine factory and compares every source mip.
// An imported/edited game texture, embedded native package, or mixed source is never accepted.
// No editor object is overwritten, saved or dirtied by this check.
KADAN_API bool P4VerifyCreatorTextureSource(UTexture2D* Asset, const FString& SourceFile,
    FP4CreatorTextureSource& Out, FString& Error);

#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "P4MapKitCore/texture_pixels.h"

// Private ordinary UE source construction only. This does not admit an asset,
// qualify defaults/dependencies, reconstruct artist source or publish Content.
// Caller keeps original package/platform bytes in immutable custody and must
// qualify every property/default/dependency before moving this transient object.
struct FP4TextureSourceRequest
{
    FString NativeClass;
    P4MapKit::ETextureShape Shape = P4MapKit::ETextureShape::Plane;
    const P4MapKit::FTexturePlatformPayload* Platform = nullptr;
    uint32 LightMapFlags = 0;
    uint64 MaxDecodedBytes = 0;
    // Absolute qualified offsets, from frozen/held original sources. Never write.
    TFunction<bool(P4MapKit::EBulkSource, uint64, uint8*, SIZE_T)> ReadAt;
    TFunction<bool()> Cancel;
};

struct FP4TextureSourceResult
{
    UTexture* Texture = nullptr;
    uint64 DecodedBytes = 0;
    uint32 SurvivingFirstMip = 0;
    // Readback describes surviving cooked texels, never missing high mips/art.
    bool SourceReadbackVerified = false;
};

KADAN_API bool P4BuildPrivateTextureSource(const FP4TextureSourceRequest& Request,
    FP4TextureSourceResult& Out, P4MapKit::FReadError& Error);

// Additive native automation helper; synthetic literals plus ordinary engine
// GetMipData/LockMip readback, all supported formats, shapes and refusal cases.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
bool P4TestPrivateTextureSource(FString& Failure, uint32& Checks);
#endif

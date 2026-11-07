#include "P4TextureAuthoring.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/Texture2DArray.h"
#include "Engine/VolumeTexture.h"
#include "Engine/LightMapTexture2D.h"
#include "Engine/ShadowMapTexture2D.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
UClass* TextureSourceClass(const FP4TextureSourceRequest& Request)
{
    using P4MapKit::ETextureShape;
    if (Request.Shape == ETextureShape::Plane)
    {
        if (Request.NativeClass == TEXT("/Script/Engine.Texture2D"))
            return UTexture2D::StaticClass();
        if (Request.NativeClass == TEXT("/Script/Engine.LightMapTexture2D"))
            return ULightMapTexture2D::StaticClass();
        if (Request.NativeClass == TEXT("/Script/Engine.ShadowMapTexture2D"))
            return UShadowMapTexture2D::StaticClass();
    }
    if (Request.Shape == ETextureShape::Cube && Request.NativeClass == TEXT("/Script/Engine.TextureCube"))
        return UTextureCube::StaticClass();
    if (Request.Shape == ETextureShape::Array && Request.NativeClass == TEXT("/Script/Engine.Texture2DArray"))
        return UTexture2DArray::StaticClass();
    if (Request.Shape == ETextureShape::Volume && Request.NativeClass == TEXT("/Script/Engine.VolumeTexture"))
        return UVolumeTexture::StaticClass();
    return nullptr;
}
ETextureSourceFormat SourceFormat(P4MapKit::ETextureTexels Texels)
{
    using P4MapKit::ETextureTexels;
    switch (Texels)
    {
        case ETextureTexels::Gray8:
            return TSF_G8;
        case ETextureTexels::Gray16:
            return TSF_G16;
        case ETextureTexels::RGBA16F:
            return TSF_RGBA16F;
        default:
            return TSF_BGRA8; // FColor/ImageCore BGRA, not deprecated TSF_RGBA8.
    }
}
uint32 MipSlices(const FP4TextureSourceRequest& Request, const P4MapKit::FTextureMipPayload& Mip)
{
    return Request.Shape == P4MapKit::ETextureShape::Cube ? Request.Platform->Slices : Mip.LogicalDepth;
}
bool Cancelled(const FP4TextureSourceRequest& Request)
{
    return Request.Cancel && Request.Cancel();
}
bool DecodeMip(const FP4TextureSourceRequest& Request, const P4MapKit::FTextureMipPayload& Mip,
    const std::function<bool(uint32, uint32, const uint8*, std::size_t)>& Sink, P4MapKit::FReadError& Error)
{
    return P4MapKit::DecodeTextureRows(
        Request.Platform->Format, Mip.LogicalWidth, Mip.LogicalHeight, MipSlices(Request, Mip), Mip.Bulk.Size,
        Request.MaxDecodedBytes,
        [&](std::uint64_t At, std::uint8_t* Bytes, std::size_t Count)
        {
            if (At > Mip.Bulk.Size || Count > Mip.Bulk.Size - At || At > MAX_uint64 - Mip.Bulk.Offset)
                return false;
            return Request.ReadAt(Mip.Bulk.Source, Mip.Bulk.Offset + At, Bytes, Count);
        },
        Sink, [&]() { return Cancelled(Request); }, Error);
}
struct FLockedSourceMip
{
    FTextureSource& Source;
    int32 Index;
    uint8* Data;
    FLockedSourceMip(FTextureSource& InSource, int32 InIndex)
        : Source(InSource), Index(InIndex), Data(Source.LockMip(Index))
    {}
    ~FLockedSourceMip()
    {
        if (Data)
            Source.UnlockMip(Index);
    }
};
void WriteSourceRow(uint8* Dest, const uint8* Row, SIZE_T Count, ETextureSourceFormat Format)
{
    if (Format != TSF_BGRA8)
    {
        FMemory::Memcpy(Dest, Row, Count);
        return;
    }
    for (SIZE_T At = 0; At < Count; At += 4)
    {
        Dest[At] = Row[At + 2];
        Dest[At + 1] = Row[At + 1];
        Dest[At + 2] = Row[At];
        Dest[At + 3] = Row[At + 3];
    }
}
bool SourceRowMatches(const uint8* Stored, const uint8* Canonical, SIZE_T Count, ETextureSourceFormat Format)
{
    if (Format != TSF_BGRA8)
        return !FMemory::Memcmp(Stored, Canonical, Count);
    for (SIZE_T At = 0; At < Count; At += 4)
        if (Stored[At] != Canonical[At + 2] || Stored[At + 1] != Canonical[At + 1] || Stored[At + 2] != Canonical[At] ||
            Stored[At + 3] != Canonical[At + 3])
            return false;
    return true;
}
} //namespace

bool P4BuildPrivateTextureSource(
    const FP4TextureSourceRequest& Request, FP4TextureSourceResult& Out, P4MapKit::FReadError& Error)
{
    using namespace P4MapKit;
    Out = {};
    const auto Reject = [&](const char* Code)
    {
        if (Error.Code.empty())
            Error = {0, Code};
        return false;
    };
    if (!Error.Code.empty())
        return false;
    if (!IsInGameThread())
        return Reject("texture-source-construction-requires-game-thread");
    if (Cancelled(Request))
        return Reject("texture-pixel-cancelled");
    UClass* Class = TextureSourceClass(Request);
    if (!Class)
        return Reject("texture-source-native-class-unqualified");
    if (!Request.Platform || !Request.ReadAt)
        return Reject("missing-texture-source-input");
    const auto& Platform = *Request.Platform;
    FTextureDecodeFormat DecodeFormat;
    if (!TextureDecodeFormat(Platform.Format, DecodeFormat))
        return Reject("texture-pixel-codec-unqualified");
    if (Platform.Mips.empty() || Platform.Mips.size() > 32 || !Request.MaxDecodedBytes)
        return Reject("texture-source-mips-or-budget-unqualified");
    const auto& First = Platform.Mips.front();
    const auto Slices = MipSlices(Request, First);
    if (!First.LogicalWidth || !First.LogicalHeight || First.LogicalWidth > 16384 || First.LogicalHeight > 16384 ||
        !Slices || Slices > 2048)
        return Reject("texture-source-dimensions-unqualified");
    if ((Request.Shape == ETextureShape::Plane && Slices != 1) ||
        (Request.Shape == ETextureShape::Cube && (Slices != 6 || First.LogicalWidth != First.LogicalHeight)) ||
        (Request.Shape == ETextureShape::Array && Slices > MAX_ARRAY_SLICES))
        return Reject("texture-source-shape-mismatch");
    uint32 Width = First.LogicalWidth, Height = First.LogicalHeight;
    uint64 TotalBytes = 0;
    for (const auto& Mip : Platform.Mips)
    {
        if (Mip.LogicalWidth != Width || Mip.LogicalHeight != Height)
            return Reject("texture-source-mip-dimensions-mismatch");
        // Engine FTextureSource::CalcMipOffset/CalcLayerSize keeps NumSlices
        // constant. Never pad/repeat disappearing volume slices to fit it.
        if (MipSlices(Request, Mip) != Slices)
            return Reject("texture-source-varying-volume-depth-unqualified");
        if (Mip.Bulk.Source == EBulkSource::None)
            return Reject("texture-pixel-mip-unavailable");
        const uint64 BlocksX = (Width + DecodeFormat.Block - 1) / DecodeFormat.Block;
        const uint64 BlocksY = (Height + DecodeFormat.Block - 1) / DecodeFormat.Block;
        if (Mip.Bulk.Size != BlocksX * BlocksY * Slices * DecodeFormat.BlockBytes)
            return Reject("texture-pixel-source-size-mismatch");
        const uint64 Bytes = uint64(Width) * Height * Slices * DecodeFormat.PixelBytes;
        if (Bytes > MAX_int64 || Bytes > Request.MaxDecodedBytes || TotalBytes > Request.MaxDecodedBytes - Bytes)
            return Reject("texture-pixel-output-budget-exceeded");
        if (Mip.Bulk.Offset > MAX_uint64 - Mip.Bulk.Size)
            return Reject("texture-source-range-overflow");
        TotalBytes += Bytes;
        Width = FMath::Max<uint32>(1, Width >> 1);
        Height = FMath::Max<uint32>(1, Height >> 1);
    }
    const auto Format = SourceFormat(DecodeFormat.Texels);
    UTexture* Candidate = NewObject<UTexture>(GetTransientPackage(), Class, NAME_None, RF_Transient);
    if (!Candidate)
        return Reject("texture-source-object-construction-failed");
    TStrongObjectPtr<UTexture> CandidateGuard(Candidate);
    const auto FailCandidate = [&]()
    {
        Candidate->Source.Init(0, 0, 0, 0, TSF_Invalid);
        Candidate->MarkPendingKill();
        return false;
    };
    // Deliberately no PostEditChange/UpdateResource/save/registry notification:
    // they can generate/rewrite mips before settings/dependencies are qualified.
    Candidate->Source.Init(
        int32(First.LogicalWidth), int32(First.LogicalHeight), int32(Slices), int32(Platform.Mips.size()), Format);
    if (Request.NativeClass == TEXT("/Script/Engine.LightMapTexture2D"))
        CastChecked<ULightMapTexture2D>(Candidate)->LightmapFlags = ELightMapFlags(Request.LightMapFlags);
    for (SIZE_T Index = 0; Index < Platform.Mips.size(); ++Index)
    {
        const auto& Mip = Platform.Mips[Index];
        bool Decoded = false;
        {
            FLockedSourceMip Locked(Candidate->Source, int32(Index));
            if (!Locked.Data)
            {
                Reject("texture-source-lock-failed");
            }
            else
            {
                const SIZE_T RowBytes = SIZE_T(Mip.LogicalWidth) * DecodeFormat.PixelBytes;
                Decoded = DecodeMip(
                    Request, Mip,
                    [&](uint32 Slice, uint32 Row, const uint8* Bytes, std::size_t Count)
                    {
                        if (Count != RowBytes || Slice >= Slices || Row >= Mip.LogicalHeight)
                            return false;
                        WriteSourceRow(
                            Locked.Data + (uint64(Slice) * Mip.LogicalHeight + Row) * RowBytes, Bytes, Count, Format);
                        return true;
                    },
                    Error);
            }
        }
        if (!Decoded)
            return FailCandidate();
    }
    // Native engine readback independently selects the stored mip offsets and
    // lengths; decode again from held source rows without a second texture copy.
    if (Candidate->Source.GetFormat() != Format || Candidate->Source.GetSizeX() != int32(First.LogicalWidth) ||
        Candidate->Source.GetSizeY() != int32(First.LogicalHeight) ||
        Candidate->Source.GetNumSlices() != int32(Slices) ||
        Candidate->Source.GetNumMips() != int32(Platform.Mips.size()))
    {
        Reject("texture-source-readback-metadata-mismatch");
        return FailCandidate();
    }
    for (SIZE_T Index = 0; Index < Platform.Mips.size(); ++Index)
    {
        const auto& Mip = Platform.Mips[Index];
        bool Matches = false;
        {
            FLockedSourceMip Locked(Candidate->Source, int32(Index));
            const SIZE_T RowBytes = SIZE_T(Mip.LogicalWidth) * DecodeFormat.PixelBytes;
            const uint64 Expected = uint64(RowBytes) * Mip.LogicalHeight * Slices;
            if (!Locked.Data || Candidate->Source.CalcMipSize(int32(Index)) != int64(Expected))
                Reject("texture-source-readback-size-mismatch");
            else
                Matches = DecodeMip(
                    Request, Mip,
                    [&](uint32 Slice, uint32 Row, const uint8* Bytes, std::size_t Count)
                    {
                        return Count == RowBytes && Slice < Slices && Row < Mip.LogicalHeight &&
                            SourceRowMatches(Locked.Data + (uint64(Slice) * Mip.LogicalHeight + Row) * RowBytes, Bytes,
                                Count, Format);
                    },
                    Error);
        }
        if (!Matches)
        {
            if (Error.Code == "texture-pixel-write-failed")
                Error.Code = "texture-source-readback-bytes-mismatch";
            return FailCandidate();
        }
    }
    if (Cancelled(Request))
    {
        Reject("texture-pixel-cancelled");
        return FailCandidate();
    }
    Out.Texture = Candidate;
    Out.DecodedBytes = TotalBytes;
    Out.SurvivingFirstMip = Platform.FirstMip;
    Out.SourceReadbackVerified = true;
    return true;
}

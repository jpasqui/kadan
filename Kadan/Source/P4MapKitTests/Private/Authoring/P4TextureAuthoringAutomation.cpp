#include "P4TextureAuthoring.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Engine/LightMapTexture2D.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GarbageCollection.h"

bool P4TestPrivateTextureSource(FString& Failure, uint32& Checks)
{
    using namespace P4MapKit;
    Failure.Empty();
    Checks = 0;
    const auto Check = [&](bool Condition, const TCHAR* Label)
    {
        ++Checks;
        if (!Condition && Failure.IsEmpty())
            Failure = Label;
        return Condition;
    };
    struct FFormatFixture
    {
        const char* Name;
        std::vector<uint8> Block, StoredPixel;
        ETextureSourceFormat Format;
    };
    // Literal endpoints/indices, raw bytes and expected ordinary UE source bytes.
    // BC6/7 reserved modes have defined zeros; the core's separate mode oracle
    // qualifies non-reserved decoding. These fixtures qualify source precision.
    std::vector<FFormatFixture> Formats = {{"PF_DXT1", {0x00, 0xf8, 0, 0, 0, 0, 0, 0}, {0, 0, 255, 255}, TSF_BGRA8},
        {"PF_DXT3", {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0xf8, 0, 0, 0, 0, 0, 0}, {0, 0, 255, 136},
            TSF_BGRA8},
        {"PF_DXT5", {68, 170, 0, 0, 0, 0, 0, 0, 0x00, 0xf8, 0, 0, 0, 0, 0, 0}, {0, 0, 255, 68}, TSF_BGRA8},
        {"PF_BC4", {53, 27, 0, 0, 0, 0, 0, 0}, {53}, TSF_G8},
        {"PF_BC5", {53, 27, 0, 0, 0, 0, 0, 0, 79, 12, 0, 0, 0, 0, 0, 0}, {0, 79, 53, 255}, TSF_BGRA8},
        {"PF_BC6H", {0x13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0x3c}, TSF_RGBA16F},
        {"PF_BC7", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}, TSF_BGRA8},
        {"PF_B8G8R8A8", {3, 2, 1, 4}, {3, 2, 1, 4}, TSF_BGRA8}, {"PF_R8G8B8A8", {1, 2, 3, 4}, {3, 2, 1, 4}, TSF_BGRA8},
        {"PF_G8", {53}, {53}, TSF_G8}, {"PF_A8", {55}, {0, 0, 0, 55}, TSF_BGRA8},
        {"PF_R8G8", {1, 2}, {0, 2, 1, 255}, TSF_BGRA8}, {"PF_G16", {0x57, 0x13}, {0x57, 0x13}, TSF_G16},
        {"PF_FloatRGBA", {0, 0xc0, 0, 0x7c, 1, 0x7e, 0, 0x44}, {0, 0xc0, 0, 0x7c, 1, 0x7e, 0, 0x44}, TSF_RGBA16F}};
    const auto BuildFixture = [&](const FFormatFixture& Fixture, ETextureShape Shape, const FString& Class,
                                  uint32 Slices, uint32 MipCount, FP4TextureSourceResult& Result, FReadError& Error,
                                  bool UniqueSlices)
    {
        FTextureDecodeFormat Decode;
        if (!TextureDecodeFormat(Fixture.Name, Decode))
            return false;
        FTexturePlatformPayload Platform;
        Platform.Format = Fixture.Name;
        Platform.Width = Platform.Height = 4;
        Platform.Slices = Slices;
        Platform.FirstMip = 2;
        std::vector<uint8> Bytes, Expected;
        for (uint32 Index = 0, Size = 4; Index < MipCount; ++Index, Size = FMath::Max<uint32>(1, Size >> 1))
        {
            FTextureMipPayload Mip;
            Mip.LogicalWidth = Mip.LogicalHeight = Size;
            Mip.LogicalDepth = Shape == ETextureShape::Cube ? 1 : Slices;
            Mip.Bulk.Source = EBulkSource::Body;
            Mip.Bulk.Offset = Bytes.size();
            const uint32 BlockCount = ((Size + Decode.Block - 1) / Decode.Block) *
                ((Size + Decode.Block - 1) / Decode.Block);
            for (uint32 Slice = 0; Slice < Slices; ++Slice)
            {
                for (uint32 Block = 0; Block < BlockCount; ++Block)
                {
                    auto Value = Fixture.Block;
                    if (UniqueSlices)
                        Value[0] = uint8(20 + Slice + Index * 8);
                    Bytes.insert(Bytes.end(), Value.begin(), Value.end());
                }
                for (uint32 Pixel = 0; Pixel < Size * Size; ++Pixel)
                {
                    auto Value = Fixture.StoredPixel;
                    if (UniqueSlices)
                        Value[2] = uint8(20 + Slice + Index * 8);
                    Expected.insert(Expected.end(), Value.begin(), Value.end());
                }
            }
            Mip.Bulk.Size = Bytes.size() - Mip.Bulk.Offset;
            Platform.Mips.push_back(Mip);
        }
        FP4TextureSourceRequest Request;
        Request.NativeClass = Class;
        Request.Shape = Shape;
        Request.Platform = &Platform;
        Request.MaxDecodedBytes = 1024 * 1024;
        Request.LightMapFlags = uint32(LMF_Streamed);
        Request.ReadAt = [&](EBulkSource Source, uint64 At, uint8* Dest, SIZE_T Count)
        {
            if (Source != EBulkSource::Body || At > Bytes.size() || Count > Bytes.size() - At)
                return false;
            FMemory::Memcpy(Dest, Bytes.data() + SIZE_T(At), Count);
            return true;
        };
        if (!P4BuildPrivateTextureSource(Request, Result, Error))
            return false;
        if (!Check(Result.Texture && Result.SourceReadbackVerified && Result.DecodedBytes == Expected.size() &&
                    Result.SurvivingFirstMip == 2,
                TEXT("source-result-metadata")))
            return false;
        auto& Source = Result.Texture->Source;
        if (!Check(Source.GetFormat() == Fixture.Format && Source.GetNumMips() == int32(MipCount) &&
                    Source.GetNumSlices() == int32(Slices) && Source.GetSizeX() == 4 && Source.GetSizeY() == 4,
                TEXT("source-engine-metadata")))
            return false;
        SIZE_T At = 0;
        for (uint32 Index = 0; Index < MipCount; ++Index)
        {
            TArray64<uint8> Readback;
            if (!Check(Source.GetMipData(Readback, int32(Index)), TEXT("source-engine-getmip")))
                return false;
            if (!Check(uint64(Readback.Num()) <= Expected.size() - At &&
                        !FMemory::Memcmp(Readback.GetData(), Expected.data() + At, SIZE_T(Readback.Num())),
                    TEXT("source-independent-literal-bytes")))
                return false;
            At += SIZE_T(Readback.Num());
        }
        if (!Check(At == Expected.size() && Result.Texture->GetClass()->GetPathName() == Class &&
                    Result.Texture->GetOutermost() == GetTransientPackage() &&
                    Result.Texture->HasAnyFlags(RF_Transient),
                TEXT("source-exact-class-private-mips")))
            return false;
        if (Class == TEXT("/Script/Engine.LightMapTexture2D") &&
            !Check(CastChecked<ULightMapTexture2D>(Result.Texture)->LightmapFlags == LMF_Streamed,
                TEXT("source-native-lightmap-flags")))
            return false;
        Result.Texture->Source.Init(0, 0, 0, 0, TSF_Invalid);
        Result.Texture->MarkPendingKill();
        Result = {};
        return true;
    };
    for (const auto& Format : Formats)
    {
        FP4TextureSourceResult Result;
        FReadError Error;
        if (!Check(BuildFixture(
                       Format, ETextureShape::Plane, TEXT("/Script/Engine.Texture2D"), 1, 3, Result, Error, false),
                TEXT("source-all-fourteen-formats")))
        {
            if (!Error.Code.empty())
                Failure += TEXT(": ") + FString(UTF8_TO_TCHAR(Error.Code.c_str()));
            return false;
        }
    }
    const auto& RGBA = Formats[8];
    struct FShapeFixture
    {
        ETextureShape Shape;
        const TCHAR* Class;
        uint32 Slices, Mips;
    };
    const FShapeFixture Shapes[] = {{ETextureShape::Cube, TEXT("/Script/Engine.TextureCube"), 6, 3},
        {ETextureShape::Array, TEXT("/Script/Engine.Texture2DArray"), 3, 3},
        {ETextureShape::Volume, TEXT("/Script/Engine.VolumeTexture"), 2, 1},
        {ETextureShape::Plane, TEXT("/Script/Engine.LightMapTexture2D"), 1, 3},
        {ETextureShape::Plane, TEXT("/Script/Engine.ShadowMapTexture2D"), 1, 3}};
    for (const auto& Shape : Shapes)
    {
        FP4TextureSourceResult Result;
        FReadError Error;
        if (!Check(BuildFixture(RGBA, Shape.Shape, Shape.Class, Shape.Slices, Shape.Mips, Result, Error, true),
                TEXT("source-shape-slice-subclass-identity")))
        {
            if (!Error.Code.empty())
                Failure += TEXT(": ") + FString(UTF8_TO_TCHAR(Error.Code.c_str()));
            return false;
        }
    }
    FTexturePlatformPayload Platform;
    Platform.Format = "PF_G8";
    Platform.Slices = 1;
    FTextureMipPayload Mip;
    Mip.LogicalWidth = Mip.LogicalHeight = Mip.LogicalDepth = 1;
    Mip.Bulk.Source = EBulkSource::Body;
    Mip.Bulk.Size = 1;
    Platform.Mips.push_back(Mip);
    FP4TextureSourceRequest Request;
    Request.NativeClass = TEXT("/Script/Engine.Texture2D");
    Request.Platform = &Platform;
    Request.MaxDecodedBytes = 1;
    Request.ReadAt = [](EBulkSource, uint64, uint8* Dest, SIZE_T)
    {
        *Dest = 7;
        return true;
    };
    const auto Refuses = [&](const char* Code)
    {
        FP4TextureSourceResult Result;
        FReadError Error;
        const bool Passed = P4BuildPrivateTextureSource(Request, Result, Error);
        return Check(
            !Passed && !Result.Texture && !Result.SourceReadbackVerified && !Result.DecodedBytes && Error.Code == Code,
            TEXT("source-no-partial-result-refusal"));
    };
    Request.Cancel = []() { return true; };
    if (!Refuses("texture-pixel-cancelled"))
        return false;
    uint32 CancelCalls = 0;
    Request.Cancel = [&]() { return ++CancelCalls > 3; };
    if (!Refuses("texture-pixel-cancelled"))
        return false;
    Request.Cancel = {};
    bool Collected = false;
    Request.ReadAt = [&](EBulkSource, uint64, uint8* Dest, SIZE_T)
    {
        if (!Collected)
        {
            CollectGarbage(RF_NoFlags);
            Collected = true;
        }
        *Dest = 7;
        return true;
    };
    {
        FP4TextureSourceResult Result;
        FReadError Error;
        if (!Check(P4BuildPrivateTextureSource(Request, Result, Error) && Collected && Result.SourceReadbackVerified &&
                    Result.Texture,
                TEXT("source-callback-gc-keeps-staging-alive")))
            return false;
        Result.Texture->Source.Init(0, 0, 0, 0, TSF_Invalid);
        Result.Texture->MarkPendingKill();
    }
    Request.ReadAt = [](EBulkSource, uint64, uint8*, SIZE_T) { return false; };
    if (!Refuses("texture-pixel-read-failed"))
        return false;
    Request.MaxDecodedBytes = 0;
    if (!Refuses("texture-source-mips-or-budget-unqualified"))
        return false;
    Request.MaxDecodedBytes = 1;
    Request.NativeClass = TEXT("/Script/Engine.UnqualifiedTexture");
    if (!Refuses("texture-source-native-class-unqualified"))
        return false;
    Request.NativeClass = TEXT("/Script/Engine.VolumeTexture");
    Request.Shape = ETextureShape::Volume;
    Platform.Mips[0].LogicalDepth = 2;
    Platform.Mips[0].Bulk.Size = 2;
    Platform.Mips.push_back(Mip);
    Request.MaxDecodedBytes = 4;
    if (!Refuses("texture-source-varying-volume-depth-unqualified"))
        return false;
    return true;
}

#endif

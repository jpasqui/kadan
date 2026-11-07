#pragma once
#include "core/texture_payload.h"
struct FP4TextureFixture
{
    std::vector<std::uint8_t> Bytes;
    std::size_t Width = 0, Packed = 0, String = 0, First = 0, Count = 0, Virtual = 0, Skip = 0;
    std::vector<std::size_t> Cooked, Bulk, Dimensions;
};
static FP4TextureFixture P4TextureFixture(P4MapKit::ETextureShape Shape = P4MapKit::ETextureShape::Plane,
    bool Guid = false, bool LightMap = false)
{
    using namespace P4MapKit;
    FP4TextureFixture Result; auto& B = Result.Bytes;
    P4Put(B, Guid ? 1 : 0);
    if (Guid) for (std::uint32_t Word = 1; Word <= 4; ++Word) P4Put(B, Word);
    B.insert(B.end(), {1, 0, 1, 0}); P4Put(B, 1); P4Put(B, 1); P4Put(B, 0);
    Result.Skip = B.size(); P4Put(B, 0, 8);
    Result.Width = B.size(); P4Put(B, 4); P4Put(B, 4);
    Result.Packed = B.size(); P4Put(B, Shape == ETextureShape::Cube ? 0x80000006 :
        Shape == ETextureShape::Plane ? 1 : 2);
    Result.String = B.size(); P4String(B, "PF_B8G8R8A8");
    Result.First = B.size(); P4Put(B, 0); Result.Count = B.size(); P4Put(B, 3);
    for (std::uint32_t Index = 0; Index < 3; ++Index)
    {
        const std::uint32_t Dimension = 4 >> Index;
        const auto Depth = Shape == ETextureShape::Volume ? std::max(1u, 2u >> Index) :
            Shape == ETextureShape::Array ? 2u : 1u;
        const auto Bytes = Dimension * Dimension * Depth * 4 * (Shape == ETextureShape::Cube ? 6 : 1);
        Result.Cooked.push_back(B.size()); P4Put(B, 1);
        Result.Bulk.push_back(B.size());
        auto Header = P4BulkBytes(BulkInline, Bytes, Bytes, B.size() + 20);
        B.insert(B.end(), Header.begin(), Header.end());
        for (std::uint32_t At = 0; At < Bytes; ++At) B.push_back(std::uint8_t((At + Index) % 251));
        Result.Dimensions.push_back(B.size()); P4Put(B, Dimension); P4Put(B, Dimension); P4Put(B, Depth);
    }
    Result.Virtual = B.size(); P4Put(B, 0);
    P4Patch(B, Result.Skip, B.size(), 8); P4Put(B, 0); P4Put(B, 0);
    if (LightMap) P4Put(B, 123);
    return Result;
}
static void CheckTexturePayloadReaders()
{
    using namespace P4MapKit;
    const std::vector<std::string> Names{"None", "PF_B8G8R8A8"};
    const auto Layout = [](const std::string& Name, FTexturePixelLayout& Value) {
        if (Name != "PF_B8G8R8A8") return false;
        Value = {Name, 1, 1, 1, 4}; return true;
    };
    for (const auto Shape : {ETextureShape::Plane, ETextureShape::Cube, ETextureShape::Array, ETextureShape::Volume})
        for (const bool Guid : {false, true})
        {
            const auto Fixture = P4TextureFixture(Shape, Guid, true);
            FByteBulkContext Context; Context.FileSizes[0] = std::int64_t(Fixture.Bytes.size());
            FByteReader Reader(Fixture.Bytes); FTextureNativePayload Payload;
            Check(ReadCookedTexturePayload(Reader, Names, Shape, true, Context, Layout, Payload));
            Check(Payload.HasGuid == Guid); Check(Payload.Guid[0] == (Guid ? 1u : 0u));
            Check(Payload.LightMapFlags == 123); Check(Payload.End == Fixture.Bytes.size());
            Check(Payload.Platforms.size() == 1); Check(Payload.Platforms[0].Mips.size() == 3);
            const auto& P = Payload.Platforms[0]; Check(P.Width == 4); Check(P.FirstMip == 0);
            Check(P.SkipOffset == std::int64_t(Fixture.Virtual + 4));
            for (std::size_t Index = 0; Index < 3; ++Index)
            {
                const auto& Mip = P.Mips[Index];
                Check(Mip.Width == (4u >> Index)); Check(Mip.LogicalWidth == Mip.Width);
                Check(Mip.Bulk.Offset == Fixture.Bulk[Index] + 20);
                Check(Mip.Bulk.Source == EBulkSource::Body);
            }
            for (std::size_t Size = 0; Size < Fixture.Bytes.size(); ++Size)
            {
                auto Cut = Fixture.Bytes; Cut.resize(Size); FByteReader Short(Cut); FTextureNativePayload Reject;
                Reject.End = 999;
                Check(!ReadCookedTexturePayload(Short, Names, Shape, true, Context, Layout, Reject));
                Check(!Short.Error.Code.empty()); Check(Reject.End == 999);
            }
        }
    auto Good = P4TextureFixture();
    FByteBulkContext Context; Context.FileSizes[0] = std::int64_t(Good.Bytes.size());
    const auto Reject = [&](const std::vector<std::uint8_t>& Bytes, const char* Code) {
        FByteReader Reader(Bytes); FTextureNativePayload Payload; Payload.End = 999;
        Check(!ReadCookedTexturePayload(Reader, Names, ETextureShape::Plane, false, Context, Layout, Payload));
        Check(Reader.Error.Code == Code); Check(Payload.End == 999);
    };
    auto Bad = Good.Bytes; P4Patch(Bad, 0, 2); Reject(Bad, "invalid-texture-object-guid-flag");
    for (const std::size_t At : {std::size_t(4), std::size_t(5), std::size_t(6), std::size_t(7)})
    { Bad = Good.Bytes; Bad[At] = 4; Reject(Bad, "texture-strip-layout-unqualified"); }
    Bad = Good.Bytes; P4Patch(Bad, 8, 0); Reject(Bad, "uncooked-texture-platform-data");
    Bad = Good.Bytes; P4Patch(Bad, 12, 0); Reject(Bad, "empty-texture-platform-data");
    Bad = Good.Bytes; P4Patch(Bad, Good.Skip, UINT64_MAX, 8); Reject(Bad, "negative-texture-skip-offset");
    for (const auto Size : {0u, 16385u})
    { Bad = Good.Bytes; P4Patch(Bad, Good.Width, Size); Reject(Bad, "texture-dimensions-unqualified"); }
    for (const auto Packed : {0u, 2u, 0x80000006u, 2049u})
    { Bad = Good.Bytes; P4Patch(Bad, Good.Packed, Packed); Reject(Bad, "texture-shape-mismatch"); }
    Bad = Good.Bytes; Bad[Good.String + 4] = 'X'; Reject(Bad, "texture-format-name-mismatch");
    Bad = Good.Bytes; P4Patch(Bad, Good.First, UINT32_MAX); Reject(Bad, "invalid-texture-first-mip");
    Bad = Good.Bytes; P4Patch(Bad, Good.First, 32); Reject(Bad, "invalid-texture-first-mip");
    Bad = Good.Bytes; P4Patch(Bad, Good.Count, 33); Reject(Bad, "invalid-count");
    Bad = Good.Bytes; P4Patch(Bad, Good.Cooked[0], 0); Reject(Bad, "uncooked-texture-mip");
    Bad = Good.Bytes; P4Patch(Bad, Good.Dimensions[0], 0); Reject(Bad, "invalid-texture-mip-dimensions");
    Bad = Good.Bytes; P4Patch(Bad, Good.Dimensions[0], 3); Reject(Bad, "texture-mip-dimensions-mismatch");
    Bad = Good.Bytes; P4Patch(Bad, Good.Virtual, 1); Reject(Bad, "virtual-texture-layout-unqualified");
    Bad = Good.Bytes; Bad.push_back(0); Reject(Bad, "unconsumed-texture-native-data");
    Bad = Good.Bytes; Bad.insert(Bad.end() - 8, Good.Bytes.begin() + 12, Good.Bytes.end() - 8);
    Reject(Bad, "duplicate-texture-platform-format");
    // The optional fields live after the serialized pixel-format FString.
    for (const std::uint32_t Tail : {1u, 2u, 4u})
    {
        auto B = Good.Bytes; P4Patch(B, Good.Packed, 0x40000001);
        std::vector<std::uint8_t> Extra; P4Put(Extra, 987); P4Put(Extra, Tail);
        B.insert(B.begin() + std::ptrdiff_t(Good.First), Extra.begin(), Extra.end());
        auto Extended = Context; Extended.FileSizes[0] = std::int64_t(B.size());
        FByteReader Reader(B); FTextureNativePayload P;
        if (Tail == 1)
        { Check(ReadCookedTexturePayload(Reader, Names, ETextureShape::Plane, false, Extended, Layout, P));
          Check(P.Platforms[0].ExtraData == 987); Check(P.Platforms[0].MipsInTail == 1); }
        else
        { Check(!ReadCookedTexturePayload(Reader, Names, ETextureShape::Plane, false, Extended, Layout, P));
          Check(Reader.Error.Code == (Tail == 2 ? "packed-texture-mip-tail-unqualified" : "invalid-texture-mip-tail-count")); }
    }
    FTexturePixelLayout Pixel{"PF_B8G8R8A8", 1, 1, 1, 4};
    std::uint64_t Bytes = 999;
    Check(TexturePayloadBytes(Pixel, 3, 5, 2, 6, Bytes)); Check(Bytes == 720);
    Check(!TexturePayloadBytes(Pixel, UINT64_MAX, 3, 1, 1, Bytes)); Check(Bytes == 720);
    Check(!TexturePayloadBytes(Pixel, 0, 1, 1, 1, Bytes));
    Pixel.BlockX = 0; Check(!TexturePayloadBytes(Pixel, 1, 1, 1, 1, Bytes));
    FByteReader Reader(Good.Bytes); FTextureNativePayload P;
    Check(!ReadCookedTexturePayload(Reader, Names, ETextureShape::Plane, false, Context, {}, P));
    Check(Reader.Error.Code == "texture-pixel-layout-unqualified");
}

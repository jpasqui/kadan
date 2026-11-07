// Complete surviving cooked texture archive metadata and raw byte ranges.
// This is neither pixel decoding nor editable-source reconstruction.
#pragma once
#include "bulk_data.h"

namespace P4MapKit
{
enum class ETextureShape { Plane, Cube, Array, Volume };
struct FTexturePixelLayout
{
    std::string Name;
    std::uint32_t BlockX = 0, BlockY = 0, BlockZ = 0, Bytes = 0;
};
struct FTextureMipPayload
{
    std::uint32_t Width = 0, Height = 0, Depth = 0;
    std::uint32_t LogicalWidth = 0, LogicalHeight = 0, LogicalDepth = 0;
    FByteBulkRange Bulk;
};
struct FTexturePlatformPayload
{
    std::string Format;
    FTexturePixelLayout Layout;
    std::int64_t SkipOffset = 0;
    std::uint32_t Width = 0, Height = 0, Packed = 0, Slices = 0;
    std::uint32_t ExtraData = 0, MipsInTail = 0, FirstMip = 0;
    std::size_t Start = 0, End = 0;
    std::vector<FTextureMipPayload> Mips;
};
struct FTextureNativePayload
{
    std::array<std::uint8_t, 4> StripFlags{};
    bool HasGuid = false;
    std::array<std::uint32_t, 4> Guid{};
    std::size_t Start = 0, End = 0;
    std::uint32_t LightMapFlags = 0;
    std::vector<FTexturePlatformPayload> Platforms;
};
inline bool TexturePayloadBytes(const FTexturePixelLayout& Layout, std::uint64_t Width,
    std::uint64_t Height, std::uint64_t Depth, std::uint64_t Slices, std::uint64_t& Out)
{
    if (!Layout.BlockX || !Layout.BlockY || !Layout.BlockZ || !Layout.Bytes ||
        !Width || !Height || !Depth || !Slices) return false;
    // Ceil division without adding to potentially large source dimensions.
    const std::uint64_t Blocks[] = {Width / Layout.BlockX + (Width % Layout.BlockX != 0),
        Height / Layout.BlockY + (Height % Layout.BlockY != 0),
        Depth / Layout.BlockZ + (Depth % Layout.BlockZ != 0), Slices, Layout.Bytes};
    std::uint64_t Bytes = 1;
    for (const auto Count : Blocks)
    {
        if (Count > std::uint64_t(INT64_MAX) / Bytes) return false;
        Bytes *= Count;
    }
    Out = Bytes; return true;
}
inline bool ReadTexturePlatformPayload(FByteReader& Reader, const std::string& Format,
    ETextureShape Shape, const FTexturePixelLayout& Layout, const FByteBulkContext& Context,
    FTexturePlatformPayload& Out)
{
    FTexturePlatformPayload Platform;
    Platform.Start = Reader.Tell(); Platform.Format = Format; Platform.Layout = Layout;
    if (Layout.Name != Format || !Layout.BlockX || !Layout.BlockY || !Layout.BlockZ ||
        !Layout.Bytes || Layout.BlockX > 16 || Layout.BlockY > 16 || Layout.BlockZ > 16 || Layout.Bytes > 128)
        return Reader.Reject("texture-pixel-layout-unqualified");
    std::uint64_t Bits = 0;
    if (!Reader.Unsigned(8, Bits)) return false;
    if (Bits > std::uint64_t(INT64_MAX)) return Reader.Reject("negative-texture-skip-offset");
    Platform.SkipOffset = std::int64_t(Bits);
    std::int32_t Width = 0, Height = 0;
    if (!Reader.I32(Width) || !Reader.I32(Height) || !Reader.U32(Platform.Packed)) return false;
    if (Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384)
        return Reader.Reject("texture-dimensions-unqualified");
    Platform.Width = std::uint32_t(Width); Platform.Height = std::uint32_t(Height);
    Platform.Slices = Platform.Packed & 0x3fffffff;
    const bool Cube = (Platform.Packed & 0x80000000) != 0;
    if (!Platform.Slices || Platform.Slices > 2048 || Cube != (Shape == ETextureShape::Cube) ||
        (Shape == ETextureShape::Plane && Platform.Slices != 1) ||
        (Shape == ETextureShape::Cube && (Platform.Slices != 6 || Width != Height)))
        return Reader.Reject("texture-shape-mismatch");
    std::string PixelName;
    if (!Reader.String(PixelName)) return false;
    if (PixelName != Format) return Reader.Reject("texture-format-name-mismatch");
    if (Platform.Packed & 0x40000000)
        if (!Reader.U32(Platform.ExtraData) || !Reader.U32(Platform.MipsInTail)) return false;
    std::int32_t FirstMip = 0;
    if (!Reader.I32(FirstMip)) return false;
    if (FirstMip < 0 || FirstMip > 31) return Reader.Reject("invalid-texture-first-mip");
    Platform.FirstMip = std::uint32_t(FirstMip);
    std::size_t Count = 0;
    if (!Reader.Count(Count, 32)) return false;
    if (Platform.MipsInTail > Count) return Reader.Reject("invalid-texture-mip-tail-count");
    // Packed mip tails require their platform-specific layout, not a per-mip
    // byte-size guess. Keep them refused until that decoder is qualified.
    if (Platform.MipsInTail > 1) return Reader.Reject("packed-texture-mip-tail-unqualified");
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        std::int32_t Cooked = 0;
        FTextureMipPayload Mip;
        if (!Reader.I32(Cooked)) return false;
        if (Cooked != 1) return Reader.Reject("uncooked-texture-mip");
        if (!ReadByteBulk(Reader, Context, Mip.Bulk)) return false;
        std::int32_t X = 0, Y = 0, Z = 0;
        if (!Reader.I32(X) || !Reader.I32(Y) || !Reader.I32(Z)) return false;
        if (X <= 0 || Y <= 0 || Z <= 0) return Reader.Reject("invalid-texture-mip-dimensions");
        Mip.Width = std::uint32_t(X); Mip.Height = std::uint32_t(Y); Mip.Depth = std::uint32_t(Z);
        const auto Level = std::uint64_t(Platform.FirstMip) + Index;
        Mip.LogicalWidth = std::uint32_t(std::max<std::uint64_t>(1, std::uint64_t(Platform.Width) >> Level));
        Mip.LogicalHeight = std::uint32_t(std::max<std::uint64_t>(1, std::uint64_t(Platform.Height) >> Level));
        Mip.LogicalDepth = Shape == ETextureShape::Volume ?
            std::uint32_t(std::max<std::uint64_t>(1, std::uint64_t(Platform.Slices) >> Level)) :
            Shape == ETextureShape::Array ? Platform.Slices : 1;
        const auto DimensionMatches = [](std::uint32_t Stored, std::uint32_t Logical, std::uint32_t Block) {
            return Stored == Logical || Stored == std::max(Logical, Block);
        };
        if (!DimensionMatches(Mip.Width, Mip.LogicalWidth, Layout.BlockX) ||
            !DimensionMatches(Mip.Height, Mip.LogicalHeight, Layout.BlockY) ||
            !DimensionMatches(Mip.Depth, Mip.LogicalDepth, Layout.BlockZ))
            return Reader.Reject("texture-mip-dimensions-mismatch");
        std::uint64_t Expected = 0;
        if (!TexturePayloadBytes(Layout, Mip.Width, Mip.Height, Mip.Depth,
            Shape == ETextureShape::Cube ? Platform.Slices : 1, Expected))
            return Reader.Reject("texture-payload-size-overflow");
        if (Mip.Bulk.Source != EBulkSource::None && Mip.Bulk.Size != Expected)
            return Reader.Reject("texture-payload-size-mismatch");
        Platform.Mips.push_back(std::move(Mip));
    }
    std::int32_t Virtual = 0;
    if (!Reader.I32(Virtual)) return false;
    if (Virtual != 0) return Reader.Reject("virtual-texture-layout-unqualified");
    if (Platform.Mips.empty()) return Reader.Reject("empty-texture-platform-data");
    Platform.End = Reader.Tell(); Out = std::move(Platform); return true;
}
inline bool ReadCookedTexturePayload(FByteReader& Reader, const std::vector<std::string>& Names,
    ETextureShape Shape, bool LightMap, const FByteBulkContext& Context,
    const std::function<bool(const std::string&, FTexturePixelLayout&)>& PixelLayout,
    FTextureNativePayload& Out)
{
    FTextureNativePayload Payload;
    Payload.Start = Reader.Tell();
    std::int32_t HasGuid = 0, Cooked = 0;
    if (!Reader.I32(HasGuid)) return false;
    if (HasGuid != 0 && HasGuid != 1) return Reader.Reject("invalid-texture-object-guid-flag");
    Payload.HasGuid = HasGuid != 0;
    if (Payload.HasGuid)
        for (auto& Word : Payload.Guid) if (!Reader.U32(Word)) return false;
    for (auto& Flag : Payload.StripFlags)
    { std::uint64_t Byte = 0; if (!Reader.Unsigned(1, Byte)) return false; Flag = std::uint8_t(Byte); }
    for (const std::size_t Index : {std::size_t(0), std::size_t(2)})
        if ((Payload.StripFlags[Index] & ~3u) || !(Payload.StripFlags[Index] & 1) || Payload.StripFlags[Index + 1])
            return Reader.Reject("texture-strip-layout-unqualified");
    if (!Reader.I32(Cooked)) return false;
    if (Cooked != 1) return Reader.Reject("uncooked-texture-platform-data");
    std::unordered_set<std::string> Seen;
    for (;;)
    {
        std::string Format;
        if (!Reader.Name(Names, Format)) return false;
        if (Format == "None") break;
        if (Payload.Platforms.size() >= 32) return Reader.Reject("texture-platform-format-limit");
        if (!Seen.insert(Format).second) return Reader.Reject("duplicate-texture-platform-format");
        FTexturePixelLayout Layout;
        if (!PixelLayout || !PixelLayout(Format, Layout)) return Reader.Reject("texture-pixel-layout-unqualified");
        FTexturePlatformPayload Platform;
        if (!ReadTexturePlatformPayload(Reader, Format, Shape, Layout, Context, Platform)) return false;
        Payload.Platforms.push_back(std::move(Platform));
    }
    if (Payload.Platforms.empty()) return Reader.Reject("empty-texture-platform-data");
    if (LightMap && !Reader.U32(Payload.LightMapFlags)) return false;
    if (Reader.Tell() != Reader.Limit()) return Reader.Reject("unconsumed-texture-native-data");
    Payload.End = Reader.Tell(); Out = std::move(Payload); return true;
}
}

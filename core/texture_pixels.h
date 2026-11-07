// Native linear cooked texels. Source settings/color space remain the adapter's responsibility.
// Sinks are private staging: discard all rows on failure/cancellation, never publish partial assets.
#pragma once
#include "texture_payload.h"
#include "texture_format_tables.h"
#include <cstring>

namespace P4MapKit
{
enum class ETextureCodec { BC1, BC2, BC3, BC4, BC5, BC6U, BC6S, BC7,
    BGRA8, RGBA8, Gray8, Alpha8, RG8, Gray16, RGBA16F };
enum class ETextureTexels { Gray8, Gray16, RGBA8, RGBA16F };
struct FTextureDecodeFormat
{
    ETextureCodec Codec = ETextureCodec::RGBA8;
    ETextureTexels Texels = ETextureTexels::RGBA8;
    unsigned Block = 1, BlockBytes = 4, PixelBytes = 4;
};
inline bool TextureDecodeFormat(const std::string& Name, FTextureDecodeFormat& Out)
{
    const std::pair<const char*, ETextureCodec> Formats[] = {
        {"PF_DXT1", ETextureCodec::BC1}, {"PF_DXT3", ETextureCodec::BC2},
        {"PF_DXT5", ETextureCodec::BC3}, {"PF_BC4", ETextureCodec::BC4},
        {"PF_BC5", ETextureCodec::BC5}, {"PF_BC6H", ETextureCodec::BC6U},
        {"PF_BC7", ETextureCodec::BC7}, {"PF_B8G8R8A8", ETextureCodec::BGRA8},
        {"PF_R8G8B8A8", ETextureCodec::RGBA8}, {"PF_G8", ETextureCodec::Gray8},
        {"PF_A8", ETextureCodec::Alpha8}, {"PF_R8G8", ETextureCodec::RG8},
        {"PF_G16", ETextureCodec::Gray16}, {"PF_FloatRGBA", ETextureCodec::RGBA16F}};
    for (const auto& Format : Formats)
        if (Name == Format.first)
        {
            FTextureDecodeFormat Result;
            Result.Codec = Format.second;
            if (Result.Codec <= ETextureCodec::BC7)
            {
                Result.Block = 4;
                Result.BlockBytes = Result.Codec == ETextureCodec::BC1 || Result.Codec == ETextureCodec::BC4 ? 8 : 16;
            }
            if (Result.Codec == ETextureCodec::Gray8 || Result.Codec == ETextureCodec::BC4)
                { Result.Texels = ETextureTexels::Gray8; Result.PixelBytes = 1; }
            if (Result.Codec == ETextureCodec::Gray16)
                { Result.Texels = ETextureTexels::Gray16; Result.BlockBytes = 2; Result.PixelBytes = 2; }
            if (Result.Codec == ETextureCodec::RGBA16F || Result.Codec == ETextureCodec::BC6U)
                { Result.Texels = ETextureTexels::RGBA16F; Result.PixelBytes = 8; }
            if (Result.Codec == ETextureCodec::RGBA16F) Result.BlockBytes = 8;
            if (Result.Codec == ETextureCodec::Gray8 || Result.Codec == ETextureCodec::Alpha8) Result.BlockBytes = 1;
            if (Result.Codec == ETextureCodec::RG8) Result.BlockBytes = 2;
            Out = Result; return true;
        }
    return false;
}
namespace TextureDetail
{
struct FBits
{
    const std::uint8_t* Bytes;
    unsigned At = 0;
    unsigned Read(unsigned Count)
    {
        unsigned Value = 0;
        for (unsigned Index = 0; Index < Count; ++Index, ++At)
            Value |= unsigned((Bytes[At / 8] >> (At % 8)) & 1) << Index;
        return Value;
    }
};
inline unsigned U16(const std::uint8_t* Bytes) { return Bytes[0] | (unsigned(Bytes[1]) << 8); }
inline void Put16(std::uint8_t* Bytes, unsigned Value)
{ Bytes[0] = std::uint8_t(Value); Bytes[1] = std::uint8_t(Value >> 8); }
inline void BCColor(const std::uint8_t* Bytes, bool Four, std::uint8_t* Out)
{
    unsigned Colors[4][4]{};
    const unsigned C0 = U16(Bytes), C1 = U16(Bytes + 2);
    const unsigned C[] = {C0, C1};
    for (unsigned End = 0; End < 2; ++End)
    {
        const unsigned R = C[End] >> 11, G = (C[End] >> 5) & 63, B = C[End] & 31;
        Colors[End][0] = (R << 3) | (R >> 2); Colors[End][1] = (G << 2) | (G >> 4);
        Colors[End][2] = (B << 3) | (B >> 2); Colors[End][3] = 255;
    }
    if (C0 > C1 || Four)
        for (unsigned Channel = 0; Channel < 4; ++Channel)
        {
            Colors[2][Channel] = (2 * Colors[0][Channel] + Colors[1][Channel]) / 3;
            Colors[3][Channel] = (Colors[0][Channel] + 2 * Colors[1][Channel]) / 3;
        }
    else
        for (unsigned Channel = 0; Channel < 4; ++Channel)
            Colors[2][Channel] = (Colors[0][Channel] + Colors[1][Channel]) / 2;
    FBits Bits{Bytes + 4};
    for (unsigned Pixel = 0; Pixel < 16; ++Pixel)
    {
        const auto Index = Bits.Read(2);
        for (unsigned Channel = 0; Channel < 4; ++Channel)
            Out[Pixel * 4 + Channel] = std::uint8_t(Colors[Index][Channel]);
    }
}
inline void BCChannel(const std::uint8_t* Bytes, std::uint8_t* Out, unsigned Stride)
{
    unsigned Palette[8] = {Bytes[0], Bytes[1]};
    const unsigned Last = Palette[0] > Palette[1] ? 7 : 5;
    for (unsigned Index = 2; Index <= Last; ++Index)
        Palette[Index] = ((Last + 1 - Index) * Palette[0] + (Index - 1) * Palette[1]) / Last;
    if (Last == 5) { Palette[6] = 0; Palette[7] = 255; }
    FBits Bits{Bytes + 2};
    for (unsigned Pixel = 0; Pixel < 16; ++Pixel) Out[Pixel * Stride] = std::uint8_t(Palette[Bits.Read(3)]);
}
inline void BC7(const std::uint8_t* Bytes, std::uint8_t* Out)
{
    std::memset(Out, 0, 64);
    if (!Bytes[0]) return; // Reserved mode: transparent black, per D3D specification.
    FBits Bits{Bytes}; unsigned Mode = 0;
    while (!Bits.Read(1)) ++Mode;
    const auto& M = BC7Modes[Mode];
    const auto Part = Bits.Read(M.Partition), Rotation = Bits.Read(M.Rotation), Selection = Bits.Read(M.Selection);
    unsigned Ends[6][4]{};
    for (unsigned End = 0; End < 2 * M.Subsets; ++End) Ends[End][3] = 255;
    for (unsigned Channel = 0; Channel < 3; ++Channel)
        for (unsigned End = 0; End < 2 * M.Subsets; ++End) Ends[End][Channel] = Bits.Read(M.Color);
    if (M.Alpha)
        for (unsigned End = 0; End < 2 * M.Subsets; ++End) Ends[End][3] = Bits.Read(M.Alpha);
    const unsigned Channels = M.Alpha ? 4 : 3;
    if (M.EndpointP || M.SharedP)
        for (unsigned Sub = 0; Sub < M.Subsets; ++Sub)
        {
            const auto Shared = M.SharedP ? Bits.Read(1) : 0;
            for (unsigned End = 2 * Sub; End < 2 * Sub + 2; ++End)
            {
                const auto P = M.SharedP ? Shared : Bits.Read(1);
                for (unsigned Channel = 0; Channel < Channels; ++Channel) Ends[End][Channel] = (Ends[End][Channel] << 1) | P;
            }
        }
    for (unsigned End = 0; End < 2 * M.Subsets; ++End)
        for (unsigned Channel = 0; Channel < Channels; ++Channel)
        {
            const auto Precision = (Channel < 3 ? M.Color : M.Alpha) + unsigned(M.EndpointP || M.SharedP);
            const auto Value = Ends[End][Channel] << (8 - Precision);
            Ends[End][Channel] = Value | (Value >> Precision);
        }
    unsigned Subsets[16]{}, First[16]{}, Second[16]{};
    for (unsigned Pixel = 0; Pixel < 16; ++Pixel)
    {
        Subsets[Pixel] = M.Subsets == 1 ? 0 : M.Subsets == 2 ?
            (Partition2[Part] >> Pixel) & 1 : (Partition3[Part] >> (2 * Pixel)) & 3;
        const bool Anchor = Pixel == 0 || (M.Subsets == 2 ? Pixel == Anchor2[Part] :
            M.Subsets == 3 && (Pixel == Anchor3A[Part] || Pixel == Anchor3B[Part]));
        First[Pixel] = Bits.Read(M.Index - unsigned(Anchor));
    }
    for (unsigned Pixel = 0; Pixel < 16; ++Pixel)
        Second[Pixel] = M.Second ? Bits.Read(M.Second - unsigned(Pixel == 0)) : First[Pixel];
    for (unsigned Pixel = 0; Pixel < 16; ++Pixel)
    {
        const bool Swap = M.Second && Selection;
        const auto C = Weight(Swap ? M.Second : M.Index, Swap ? Second[Pixel] : First[Pixel]);
        const auto A = Weight(M.Second ? (Swap ? M.Index : M.Second) : M.Index, Swap ? First[Pixel] : Second[Pixel]);
        for (unsigned Channel = 0; Channel < 4; ++Channel)
        {
            const unsigned W = Channel == 3 ? A : C, E = 2 * Subsets[Pixel];
            Out[4 * Pixel + Channel] = std::uint8_t(((64 - W) * Ends[E][Channel] + W * Ends[E + 1][Channel] + 32) >> 6);
        }
        if (Rotation) std::swap(Out[4 * Pixel + Rotation - 1], Out[4 * Pixel + 3]);
    }
}
inline int SignExtend(unsigned Value, unsigned Precision)
{ return int(Value) - ((Value & (1u << (Precision - 1))) ? int(1u << Precision) : 0); }
inline int BC6Unquantize(int Value, unsigned Precision, bool Signed)
{
    if (!Signed)
    {
        if (Precision >= 15 || !Value) return Value;
        return Value == int((1u << Precision) - 1) ? 65535 : ((Value * 65536) + 32768) >> Precision;
    }
    if (Precision >= 16) return Value;
    const int Magnitude = Value < 0 ? -Value : Value;
    const int Q = !Magnitude ? 0 : Magnitude >= int((1u << (Precision - 1)) - 1) ? 32767 :
        ((Magnitude * 32768) + 16384) >> (Precision - 1);
    return Value < 0 ? -Q : Q;
}
// Defined floor division, independent of implementation-defined signed shifts.
inline int Floor64(int Value) { return Value >= 0 ? Value / 64 : -((-Value + 63) / 64); }
inline unsigned BC6Finish(int Value, bool Signed)
{
    if (!Signed) return unsigned(Value * 31 / 64);
    const auto Half = (Value < 0 ? -Value : Value) * 31 / 32;
    return unsigned(Half) | (Value < 0 ? 0x8000u : 0);
}
inline void BC6(const std::uint8_t* Bytes, bool Signed, std::uint8_t* Out)
{
    std::memset(Out, 0, 128);
    for (unsigned Pixel = 0; Pixel < 16; ++Pixel) Put16(Out + 8 * Pixel + 6, 0x3c00);
    const unsigned Value = (Bytes[0] & 3) < 2 ? Bytes[0] & 3 : Bytes[0] & 31;
    const FBC6Mode* M = nullptr;
    for (const auto& Spec : BC6Modes) if (Spec.Value == Value) { M = &Spec; break; }
    if (!M) return; // Reserved modes: RGB zero, alpha one.
    FBits Bits{Bytes, M->Head}; unsigned Raw[4][3]{}; int Ends[4][3]{};
    for (unsigned Index = 0; Index < M->Count; ++Index)
    {
        const auto& Run = M->Runs[Index];
        Raw[Run.Endpoint][Run.Channel] |= Bits.Read(Run.Count) << Run.Shift;
    }
    for (unsigned Channel = 0; Channel < 3; ++Channel)
    {
        Ends[0][Channel] = Signed ? SignExtend(Raw[0][Channel], M->Precision) : int(Raw[0][Channel]);
        for (unsigned End = 1; End < 2 * M->Subsets; ++End)
        {
            auto V = Raw[End][Channel];
            if (M->Transformed)
                V = unsigned(Ends[0][Channel] + SignExtend(V, M->Delta[Channel])) & ((1u << M->Precision) - 1);
            Ends[End][Channel] = Signed ? SignExtend(V, M->Precision) : int(V);
        }
    }
    const auto Part = M->Subsets == 2 ? Bits.Read(5) : 0;
    for (unsigned Pixel = 0; Pixel < 16; ++Pixel)
    {
        const bool Anchor = Pixel == 0 || (M->Subsets == 2 && Pixel == Anchor2[Part]);
        const unsigned Precision = M->Subsets == 2 ? 3 : 4;
        const auto W = int(Weight(Precision, Bits.Read(Precision - unsigned(Anchor))));
        const unsigned End = M->Subsets == 2 ? 2 * ((Partition2[Part] >> Pixel) & 1) : 0;
        for (unsigned Channel = 0; Channel < 3; ++Channel)
        {
            const auto A = BC6Unquantize(Ends[End][Channel], M->Precision, Signed);
            const auto B = BC6Unquantize(Ends[End + 1][Channel], M->Precision, Signed);
            Put16(Out + 8 * Pixel + 2 * Channel, BC6Finish(Floor64(A * (64 - W) + B * W + 32), Signed));
        }
    }
}
}
// Internal block inputs have already been bounded by DecodeTextureRows.
inline void DecodeTextureBlock(ETextureCodec Codec, const std::uint8_t* Bytes, std::uint8_t* Out)
{
    using namespace TextureDetail;
    switch (Codec)
    {
    case ETextureCodec::BC1: BCColor(Bytes, false, Out); break;
    case ETextureCodec::BC2:
    case ETextureCodec::BC3:
        BCColor(Bytes + 8, true, Out);
        if (Codec == ETextureCodec::BC3) BCChannel(Bytes, Out + 3, 4);
        else for (unsigned Pixel = 0; Pixel < 16; ++Pixel)
            Out[Pixel * 4 + 3] = std::uint8_t(((Bytes[Pixel / 2] >> (4 * (Pixel % 2))) & 15) * 17);
        break;
    case ETextureCodec::BC4: BCChannel(Bytes, Out, 1); break;
    case ETextureCodec::BC5:
        for (unsigned Pixel = 0; Pixel < 16; ++Pixel) { Out[Pixel * 4 + 2] = 0; Out[Pixel * 4 + 3] = 255; }
        BCChannel(Bytes, Out, 4); BCChannel(Bytes + 8, Out + 1, 4); break;
    case ETextureCodec::BC6U: BC6(Bytes, false, Out); break;
    case ETextureCodec::BC6S: BC6(Bytes, true, Out); break;
    case ETextureCodec::BC7: BC7(Bytes, Out); break;
    case ETextureCodec::BGRA8: Out[0] = Bytes[2]; Out[1] = Bytes[1]; Out[2] = Bytes[0]; Out[3] = Bytes[3]; break;
    case ETextureCodec::RGBA8: std::memcpy(Out, Bytes, 4); break;
    case ETextureCodec::Gray8: Out[0] = Bytes[0]; break;
    case ETextureCodec::Alpha8: Out[0] = Out[1] = Out[2] = 0; Out[3] = Bytes[0]; break;
    case ETextureCodec::RG8: Out[0] = Bytes[0]; Out[1] = Bytes[1]; Out[2] = 0; Out[3] = 255; break;
    case ETextureCodec::Gray16: std::memcpy(Out, Bytes, 2); break;
    case ETextureCodec::RGBA16F: std::memcpy(Out, Bytes, 8); break;
    }
}
// Source byte order: tightly packed block rows, then depth/array/cube slices.
// Width/Height are logical pixels; stored minimum blocks are naturally cropped.
inline bool DecodeTextureRows(const std::string& Name, std::uint32_t Width, std::uint32_t Height,
    std::uint32_t Slices, std::uint64_t SourceBytes, std::uint64_t MaxOutputBytes,
    const std::function<bool(std::uint64_t, std::uint8_t*, std::size_t)>& ReadAt,
    const std::function<bool(std::uint32_t, std::uint32_t, const std::uint8_t*, std::size_t)>& Sink,
    const std::function<bool()>& Cancel, FReadError& Error)
{
    const auto Reject = [&](const char* Code) { Error = {0, Code}; return false; };
    if (!Error.Code.empty()) return false;
    FTextureDecodeFormat Format;
    if (!TextureDecodeFormat(Name, Format)) return Reject("texture-pixel-codec-unqualified");
    if (!Width || !Height || !Slices || Width > 16384 || Height > 16384 || Slices > 2048)
        return Reject("texture-pixel-dimensions-unqualified");
    const auto BW = (Width + Format.Block - 1) / Format.Block, BH = (Height + Format.Block - 1) / Format.Block;
    const auto Expected = std::uint64_t(BW) * BH * Slices * Format.BlockBytes;
    if (SourceBytes != Expected) return Reject("texture-pixel-source-size-mismatch");
    const auto OutputBytes = std::uint64_t(Width) * Height * Slices * Format.PixelBytes;
    if (OutputBytes > MaxOutputBytes) return Reject("texture-pixel-output-budget-exceeded");
    if (!ReadAt || !Sink) return Reject("missing-texture-pixel-callback");
    const auto RowBytes = std::size_t(Width) * Format.PixelBytes;
    std::vector<std::uint8_t> Input(std::size_t(BW) * Format.BlockBytes);
    std::vector<std::uint8_t> Rows(RowBytes * Format.Block);
    std::array<std::uint8_t, 128> Block{};
    for (std::uint32_t Slice = 0; Slice < Slices; ++Slice)
        for (std::uint32_t Y = 0; Y < BH; ++Y)
        {
            if (Cancel && Cancel()) return Reject("texture-pixel-cancelled");
            if (!ReadAt((std::uint64_t(Slice) * BH + Y) * Input.size(), Input.data(), Input.size()))
                return Reject("texture-pixel-read-failed");
            if (Cancel && Cancel()) return Reject("texture-pixel-cancelled");
            for (std::uint32_t X = 0; X < BW; ++X)
            {
                DecodeTextureBlock(Format.Codec, Input.data() + X * Format.BlockBytes, Block.data());
                const auto Pixels = std::min(Format.Block, Width - X * Format.Block);
                for (unsigned Row = 0; Row < Format.Block && Y * Format.Block + Row < Height; ++Row)
                    std::memcpy(Rows.data() + Row * RowBytes + std::size_t(X) * Format.Block * Format.PixelBytes,
                        Block.data() + Row * Format.Block * Format.PixelBytes, std::size_t(Pixels) * Format.PixelBytes);
            }
            for (unsigned Row = 0; Row < Format.Block && Y * Format.Block + Row < Height; ++Row)
            {
                if (Cancel && Cancel()) return Reject("texture-pixel-cancelled");
                if (!Sink(Slice, Y * Format.Block + Row, Rows.data() + Row * RowBytes, RowBytes))
                    return Reject("texture-pixel-write-failed");
            }
        }
    if (Cancel && Cancel()) return Reject("texture-pixel-cancelled");
    return true;
}
}

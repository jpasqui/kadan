#include "core/texture_pixels.h"

static void CheckTexturePixels()
{
    using namespace P4MapKit;
    const char* Names[] = {"PF_DXT1", "PF_DXT3", "PF_DXT5", "PF_BC4", "PF_BC5", "PF_BC6H", "PF_BC7",
        "PF_B8G8R8A8", "PF_R8G8B8A8", "PF_G8", "PF_A8", "PF_R8G8", "PF_G16", "PF_FloatRGBA"};
    std::array<std::uint8_t, 128> Pixels{};
    std::array<std::uint8_t, 16> Bytes{};
    // Hand-derived endpoint/index vectors. Independent full format comparison uses D3D11 WARP.
    Bytes[0] = 0; Bytes[1] = 0xf8; Bytes[2] = 0x1f; Bytes[3] = 0;
    std::fill(Bytes.begin() + 4, Bytes.begin() + 8, 0xe4);
    DecodeTextureBlock(ETextureCodec::BC1, Bytes.data(), Pixels.data());
    const std::uint8_t Expected[] = {255, 0, 0, 255, 0, 0, 255, 255, 170, 0, 85, 255, 85, 0, 170, 255};
    for (unsigned Index = 0; Index < 64; ++Index) Check(Pixels[Index] == Expected[Index % 16]);
    std::swap(Bytes[0], Bytes[2]); std::swap(Bytes[1], Bytes[3]);
    DecodeTextureBlock(ETextureCodec::BC1, Bytes.data(), Pixels.data());
    Check(Pixels[8] == 127 && Pixels[10] == 127 && Pixels[15] == 0);
    Bytes.fill(0);
    for (unsigned I = 0; I < 8; ++I) Bytes[I] = std::uint8_t(2 * I | ((2 * I + 1) << 4));
    DecodeTextureBlock(ETextureCodec::BC2, Bytes.data(), Pixels.data());
    for (unsigned I = 0; I < 16; ++I) Check(Pixels[4 * I + 3] == I * 17);
    Bytes.fill(0); Bytes[0] = 255;
    std::uint64_t Indices = 0;
    for (unsigned I = 0; I < 16; ++I) Indices |= std::uint64_t(I % 8) << (3 * I);
    for (unsigned I = 0; I < 6; ++I) Bytes[I + 2] = std::uint8_t(Indices >> (8 * I));
    const unsigned Alpha[] = {255, 0, 218, 182, 145, 109, 72, 36};
    DecodeTextureBlock(ETextureCodec::BC3, Bytes.data(), Pixels.data());
    for (unsigned I = 0; I < 16; ++I) Check(Pixels[4 * I + 3] == Alpha[I % 8]);
    DecodeTextureBlock(ETextureCodec::BC4, Bytes.data(), Pixels.data());
    for (unsigned I = 0; I < 16; ++I) Check(Pixels[I] == Alpha[I % 8]);
    Bytes[0] = 0; Bytes[1] = 255;
    const unsigned AlphaLow[] = {0, 255, 51, 102, 153, 204, 0, 255};
    DecodeTextureBlock(ETextureCodec::BC5, Bytes.data(), Pixels.data());
    for (unsigned I = 0; I < 16; ++I)
    { Check(Pixels[4 * I] == AlphaLow[I % 8]); Check(Pixels[4 * I + 1] == 0); Check(Pixels[4 * I + 2] == 0 && Pixels[4 * I + 3] == 255); }
    // Every BC7/BC6 mode and partition, randomized endpoints/indices, reserve handling.
    std::mt19937 Random(74893);
    for (unsigned Trial = 0; Trial < 3000; ++Trial)
    {
        for (auto& Byte : Bytes) Byte = std::uint8_t(Random());
        DecodeTextureBlock(ETextureCodec::BC7, Bytes.data(), Pixels.data());
        for (const bool Signed : {false, true})
        {
            DecodeTextureBlock(Signed ? ETextureCodec::BC6S : ETextureCodec::BC6U, Bytes.data(), Pixels.data());
            for (unsigned I = 0; I < 16; ++I) Check(Pixels[8 * I + 6] == 0 && Pixels[8 * I + 7] == 0x3c);
        }
    }
    Bytes.fill(0); DecodeTextureBlock(ETextureCodec::BC7, Bytes.data(), Pixels.data());
    Check(std::all_of(Pixels.begin(), Pixels.begin() + 64, [](std::uint8_t V) { return V == 0; }));
    for (unsigned Mode : {19u, 23u, 27u, 31u})
    {
        Bytes[0] = std::uint8_t(Mode); DecodeTextureBlock(ETextureCodec::BC6U, Bytes.data(), Pixels.data());
        for (unsigned I = 0; I < 16; ++I) Check(TextureDetail::U16(Pixels.data() + 8 * I) == 0);
    }
    const auto Run = [&](const std::string& Name, unsigned W, unsigned H, unsigned S,
        const std::vector<std::uint8_t>& Input, std::uint64_t Budget, int FailRead, int FailWrite, int CancelAt,
        FReadError& Error, std::vector<std::uint8_t>& Result) {
        int Reads = 0, Writes = 0, Cancels = 0;
        return DecodeTextureRows(Name, W, H, S, Input.size(), Budget,
            [&](std::uint64_t At, std::uint8_t* Dest, std::size_t Count) {
                Check(Count <= 65536); if (++Reads == FailRead) return false;
                Check(At <= Input.size() && Count <= Input.size() - At);
                std::memcpy(Dest, Input.data() + At, Count); return true;
            }, [&](std::uint32_t Slice, std::uint32_t Y, const std::uint8_t* Row, std::size_t Count) {
                Check(Slice < S && Y < H); if (++Writes == FailWrite) return false;
                Result.insert(Result.end(), Row, Row + Count); return true;
            }, CancelAt < 0 ? std::function<bool()>{} : std::function<bool()>([&] { return ++Cancels == CancelAt; }), Error);
    };
    for (const auto* Name : Names)
    {
        FTextureDecodeFormat Format; Check(TextureDecodeFormat(Name, Format));
        for (unsigned W : {1u, 2u, 4u, 5u, 9u}) for (unsigned H : {1u, 3u, 4u, 7u})
        {
            const unsigned S = 3, BW = (W + Format.Block - 1) / Format.Block, BH = (H + Format.Block - 1) / Format.Block;
            std::vector<std::uint8_t> Input(std::size_t(BW) * BH * S * Format.BlockBytes);
            for (auto& Byte : Input) Byte = std::uint8_t(Random());
            std::vector<std::uint8_t> Result, ExpectedRows;
            for (unsigned Slice = 0; Slice < S; ++Slice) for (unsigned Y = 0; Y < H; ++Y) for (unsigned X = 0; X < W; ++X)
            {
                const auto At = ((std::size_t(Slice) * BH + Y / Format.Block) * BW + X / Format.Block) * Format.BlockBytes;
                DecodeTextureBlock(Format.Codec, Input.data() + At, Pixels.data());
                const auto Pixel = ((Y % Format.Block) * Format.Block + X % Format.Block) * Format.PixelBytes;
                ExpectedRows.insert(ExpectedRows.end(), Pixels.begin() + Pixel, Pixels.begin() + Pixel + Format.PixelBytes);
            }
            FReadError Error;
            Check(Run(Name, W, H, S, Input, UINT64_MAX, 0, 0, -1, Error, Result));
            Check(Result == ExpectedRows);
            for (const auto Failure : {std::array<int, 3>{1, 0, -1}, {0, 1, -1}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}, {0, 0, 99999}})
            {
                Result.clear(); Error = {};
                const auto Passed = Run(Name, W, H, S, Input, UINT64_MAX, Failure[0], Failure[1], Failure[2], Error, Result);
                Check(Passed == (Failure[2] == 99999));
            }
            // Cancellation after the final row still refuses completion.
            Result.clear(); Error = {}; int Calls = 0;
            DecodeTextureRows(Name, W, H, S, Input.size(), UINT64_MAX,
                [&](std::uint64_t At, std::uint8_t* Dest, std::size_t Count) { std::memcpy(Dest, Input.data() + At, Count); return true; },
                [&](std::uint32_t, std::uint32_t, const std::uint8_t*, std::size_t) { return true; },
                [&] { return ++Calls == int(S * BH * 2 + S * H + 1); }, Error);
            Check(Error.Code == "texture-pixel-cancelled");
            for (auto Budget : {std::uint64_t(0), std::uint64_t(W) * H * S * Format.PixelBytes - 1})
            { Result.clear(); Error = {}; Check(!Run(Name, W, H, S, Input, Budget, 0, 0, -1, Error, Result)); Check(Result.empty()); }
            Input.pop_back(); Result.clear(); Error = {};
            Check(!Run(Name, W, H, S, Input, UINT64_MAX, 0, 0, -1, Error, Result)); Check(Result.empty());
        }
    }
    std::vector<std::uint8_t> Input(4), Result; FReadError Error;
    for (const auto Dimensions : {std::array<unsigned, 3>{0, 1, 1}, {1, 0, 1}, {1, 1, 0}, {16385, 1, 1}, {1, 16385, 1}, {1, 1, 2049}})
    { Error = {}; Check(!Run("PF_R8G8B8A8", Dimensions[0], Dimensions[1], Dimensions[2], Input, UINT64_MAX, 0, 0, -1, Error, Result)); }
    Error = {}; Check(!Run("PF_BAD", 1, 1, 1, Input, UINT64_MAX, 0, 0, -1, Error, Result));
    Error = {0, "prior"}; Check(!Run("PF_R8G8B8A8", 1, 1, 1, Input, UINT64_MAX, 0, 0, -1, Error, Result)); Check(Error.Code == "prior");
    for (const bool Reader : {false, true})
    {
        Error = {};
        Check(!DecodeTextureRows("PF_R8G8B8A8", 1, 1, 1, 4, 4,
            Reader ? std::function<bool(std::uint64_t, std::uint8_t*, std::size_t)>([](std::uint64_t, std::uint8_t*, std::size_t) { return true; }) : std::function<bool(std::uint64_t, std::uint8_t*, std::size_t)>{},
            {}, {}, Error));
    }
    // Raw channel bytes remain exact (including signed zero, NaN payloads and HDR halves).
    const std::uint8_t Raw[] = {0, 128, 0x01, 0x7e, 0xff, 0x7b, 0, 0x3c};
    DecodeTextureBlock(ETextureCodec::RGBA16F, Raw, Pixels.data()); Check(std::memcmp(Raw, Pixels.data(), 8) == 0);
    DecodeTextureBlock(ETextureCodec::Gray16, Raw, Pixels.data()); Check(Pixels[0] == 0 && Pixels[1] == 128);
    const std::uint8_t Color[] = {11, 22, 33, 44};
    DecodeTextureBlock(ETextureCodec::RGBA8, Color, Pixels.data()); Check(std::memcmp(Color, Pixels.data(), 4) == 0);
    DecodeTextureBlock(ETextureCodec::BGRA8, Color, Pixels.data()); Check(Pixels[0] == 33 && Pixels[1] == 22 && Pixels[2] == 11 && Pixels[3] == 44);
    DecodeTextureBlock(ETextureCodec::Gray8, Color, Pixels.data()); Check(Pixels[0] == 11);
    DecodeTextureBlock(ETextureCodec::Alpha8, Color, Pixels.data()); Check(Pixels[0] == 0 && Pixels[1] == 0 && Pixels[2] == 0 && Pixels[3] == 11);
    DecodeTextureBlock(ETextureCodec::RG8, Color, Pixels.data()); Check(Pixels[0] == 11 && Pixels[1] == 22 && Pixels[2] == 0 && Pixels[3] == 255);
}

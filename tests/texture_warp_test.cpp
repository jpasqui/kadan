// Independent operating-system texture sampling oracle; WARP owns the decoder.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>
#include <random>
#include "core/texture_pixels.h"
using Microsoft::WRL::ComPtr;
using namespace P4MapKit;
static void Require(bool Value, const char* Message)
{ if (!Value) { std::cerr << Message << '\n'; std::exit(1); } }
static void HR(HRESULT Result, const char* Message)
{ if (FAILED(Result)) { std::cerr << Message << " HRESULT=" << std::hex << unsigned(Result) << '\n'; std::exit(1); } }
static float Half(unsigned Value)
{
    const unsigned Exp = (Value >> 10) & 31, Frac = Value & 1023;
    float Result = Exp ? Exp == 31 ? Frac ? NAN : INFINITY : std::ldexp(float(1024 + Frac), int(Exp) - 25) : std::ldexp(float(Frac), -24);
    return Value & 32768 ? -Result : Result;
}
static void SetBits(std::uint8_t* Bytes, unsigned At, unsigned Count, unsigned Value)
{
    for (unsigned I = 0; I < Count; ++I)
    { const auto Bit = std::uint8_t(1u << ((At + I) % 8)); auto& B = Bytes[(At + I) / 8]; B = std::uint8_t((B & ~Bit) | (((Value >> I) & 1) ? Bit : 0)); }
}
int main()
{
    ComPtr<ID3D11Device> Device; ComPtr<ID3D11DeviceContext> Context; D3D_FEATURE_LEVEL Level;
    HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &Device, &Level, &Context), "create WARP");
    Require(Level >= D3D_FEATURE_LEVEL_11_0, "WARP compute level required");
    const char Shader[] = "Texture2D<float4> Source:register(t0); RWTexture2D<float4> Dest:register(u0);"
        "[numthreads(8,8,1)] void Main(uint3 id:SV_DispatchThreadID){uint w,h; Dest.GetDimensions(w,h);"
        "if(id.x<w && id.y<h) Dest[id.xy]=Source.Load(int3(id.xy,0));}";
    ComPtr<ID3DBlob> Code, Errors;
    HR(D3DCompile(Shader, sizeof(Shader) - 1, nullptr, nullptr, nullptr, "Main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &Code, &Errors), "compile oracle shader");
    ComPtr<ID3D11ComputeShader> Compute;
    HR(Device->CreateComputeShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr, &Compute), "create shader");
    Context->CSSetShader(Compute.Get(), nullptr, 0);
    struct FCase { const char* Name; DXGI_FORMAT DXGI; };
    const FCase Cases[] = {{"PF_DXT1", DXGI_FORMAT_BC1_UNORM}, {"PF_DXT3", DXGI_FORMAT_BC2_UNORM},
        {"PF_DXT5", DXGI_FORMAT_BC3_UNORM}, {"PF_BC4", DXGI_FORMAT_BC4_UNORM}, {"PF_BC5", DXGI_FORMAT_BC5_UNORM},
        {"PF_BC6H", DXGI_FORMAT_BC6H_UF16}, {"PF_BC7", DXGI_FORMAT_BC7_UNORM},
        {"PF_B8G8R8A8", DXGI_FORMAT_B8G8R8A8_UNORM}, {"PF_R8G8B8A8", DXGI_FORMAT_R8G8B8A8_UNORM},
        {"PF_G8", DXGI_FORMAT_R8_UNORM}, {"PF_A8", DXGI_FORMAT_A8_UNORM}, {"PF_R8G8", DXGI_FORMAT_R8G8_UNORM},
        {"PF_G16", DXGI_FORMAT_R16_UNORM}, {"PF_FloatRGBA", DXGI_FORMAT_R16G16B16A16_FLOAT}};
    std::mt19937 Random(82173); std::uint64_t Compared = 0, Blocks = 0;
    for (unsigned Case = 0; Case <= 14; ++Case)
    {
        const auto& C = Cases[Case == 14 ? 5 : Case];
        FTextureDecodeFormat F; Require(TextureDecodeFormat(C.Name, F), "format available");
        if (Case == 14) F.Codec = ETextureCodec::BC6S;
        const unsigned Width = 8, Height = 2048, BW = Width / F.Block, BH = Height / F.Block;
        std::vector<std::uint8_t> Source(std::size_t(BW) * BH * F.BlockBytes);
        for (auto& Byte : Source) Byte = std::uint8_t(Random());
        for (unsigned Block = 0; Block < BW * BH; ++Block)
        {
            auto* B = Source.data() + Block * F.BlockBytes;
            if (F.Codec == ETextureCodec::BC7)
            {
                const unsigned Mode = Block % 8, Partition = (Block / 8) % 64;
                const auto& M = TextureDetail::BC7Modes[Mode];
                SetBits(B, 0, Mode + 1, 1u << Mode); SetBits(B, Mode + 1, M.Partition, Partition);
            }
            if (F.Codec == ETextureCodec::BC6U || F.Codec == ETextureCodec::BC6S)
            {
                const auto& M = TextureDetail::BC6Modes[Block % 14];
                SetBits(B, 0, M.Head, M.Value);
                if (M.Subsets == 2) SetBits(B, 77, 5, (Block / 14) % 32);
            }
        }
        // Include reserved BC modes and deliberately non-finite/negative-zero raw halfs.
        if (F.Codec == ETextureCodec::BC7) Source[0] = 0;
        if (F.Codec == ETextureCodec::BC6U || F.Codec == ETextureCodec::BC6S)
            for (unsigned I = 0; I < 4; ++I) SetBits(Source.data() + (BW * BH - 1 - I) * F.BlockBytes, 0, 5, 19 + 4 * I);
        D3D11_TEXTURE2D_DESC Desc{}; Desc.Width = Width; Desc.Height = Height; Desc.MipLevels = Desc.ArraySize = 1;
        Desc.Format = Case == 14 ? DXGI_FORMAT_BC6H_SF16 : C.DXGI; Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_IMMUTABLE; Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA Data{Source.data(), BW * F.BlockBytes, 0};
        ComPtr<ID3D11Texture2D> Texture; ComPtr<ID3D11ShaderResourceView> View;
        HR(Device->CreateTexture2D(&Desc, &Data, &Texture), "create source");
        HR(Device->CreateShaderResourceView(Texture.Get(), nullptr, &View), "create source view");
        Desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; Desc.Usage = D3D11_USAGE_DEFAULT; Desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> Target, Staging; ComPtr<ID3D11UnorderedAccessView> UAV;
        HR(Device->CreateTexture2D(&Desc, nullptr, &Target), "create target");
        HR(Device->CreateUnorderedAccessView(Target.Get(), nullptr, &UAV), "create UAV");
        auto* V = View.Get(); auto* U = UAV.Get(); Context->CSSetShaderResources(0, 1, &V); Context->CSSetUnorderedAccessViews(0, 1, &U, nullptr);
        Context->Dispatch((Width + 7) / 8, (Height + 7) / 8, 1);
        V = nullptr; U = nullptr; Context->CSSetShaderResources(0, 1, &V); Context->CSSetUnorderedAccessViews(0, 1, &U, nullptr);
        Desc.Usage = D3D11_USAGE_STAGING; Desc.BindFlags = 0; Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HR(Device->CreateTexture2D(&Desc, nullptr, &Staging), "create readback"); Context->CopyResource(Staging.Get(), Target.Get());
        D3D11_MAPPED_SUBRESOURCE Mapped{}; HR(Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped), "readback");
        std::array<std::uint8_t, 128> Native{};
        double MaxError = 0;
        for (unsigned Y = 0; Y < Height; ++Y) for (unsigned X = 0; X < Width; ++X)
        {
            const auto At = ((std::size_t(Y / F.Block) * BW) + X / F.Block) * F.BlockBytes;
            DecodeTextureBlock(F.Codec, Source.data() + At, Native.data());
            const auto I = ((Y % F.Block) * F.Block + X % F.Block) * F.PixelBytes;
            const auto* Actual = reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(Mapped.pData) + Y * Mapped.RowPitch) + 4 * X;
            float Expected[4] = {0, 0, 0, 1};
            if (F.Texels == ETextureTexels::RGBA8) for (unsigned Ch = 0; Ch < 4; ++Ch) Expected[Ch] = float(Native[I + Ch]) / 255;
            else if (F.Texels == ETextureTexels::RGBA16F) for (unsigned Ch = 0; Ch < 4; ++Ch) Expected[Ch] = Half(TextureDetail::U16(Native.data() + I + 2 * Ch));
            else Expected[0] = F.Texels == ETextureTexels::Gray8 ? float(Native[I]) / 255 : float(TextureDetail::U16(Native.data() + I)) / 65535;
            for (unsigned Ch = 0; Ch < 4; ++Ch)
            {
                ++Compared;
                if (F.Codec == ETextureCodec::BC6U || F.Codec == ETextureCodec::BC6S)
                {
                    std::uint32_t ExpectedBits = 0, ActualBits = 0;
                    std::memcpy(&ExpectedBits, Expected + Ch, sizeof(ExpectedBits));
                    std::memcpy(&ActualBits, Actual + Ch, sizeof(ActualBits));
                    Require(ExpectedBits == ActualBits, "BC6H floating bits differ from WARP (including signed zero)");
                }
                if (F.Codec == ETextureCodec::BC7)
                    Require(std::lround(Actual[Ch] * 255) == Native[I + Ch], "BC7 integer texel differs from WARP");
                const double Diff = std::abs(double(Expected[Ch]) - Actual[Ch]);
                const bool Approx = F.Codec <= ETextureCodec::BC5;
                const double Tolerance = Approx ? 1.0 / 255 + 1e-7 : 1e-7;
                if (std::isnan(Expected[Ch]) && std::isnan(Actual[Ch])) continue;
                if (Expected[Ch] == Actual[Ch]) continue;
                if (Diff > Tolerance || !std::isfinite(Diff))
                {
                    std::cerr << C.Name << (Case == 14 ? " signed" : "") << " at " << X << ',' << Y << " ch=" << Ch
                        << " expected=" << Expected[Ch] << " actual=" << Actual[Ch] << " diff=" << Diff << '\n'; return 1;
                }
                MaxError = std::max(MaxError, Diff);
            }
        }
        Context->Unmap(Staging.Get(), 0); Blocks += std::uint64_t(BW) * BH;
        std::cout << C.Name << (Case == 14 ? " signed" : "") << " compared=" << Width * Height * 4 << " max_error=" << MaxError << '\n';
    }
    std::cout << "PASS formats=14 signed_bc6=1 channels=" << Compared << " blocks=" << Blocks << '\n';
}

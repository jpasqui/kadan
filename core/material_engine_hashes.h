// Hashes the engine keys shader programs and their bindings with: the type names of the vertex
// factories and of the anisotropy pass shaders, and the layout and name of the View uniform buffer.
#pragma once
#include <cstdint>
namespace P4MapKit
{
namespace EngineHash
{
constexpr std::uint64_t LocalVertexFactory = 0x9f41d10d68bb52d8ull;
constexpr std::uint64_t InstancedStaticMeshVertexFactory = 0x7e6253dcc60d6ebbull;
constexpr std::uint64_t GpuSkinVertexFactory = 0x391c2b78c893a52aull;
constexpr std::uint32_t ViewLayout = 0x166705f7u;
constexpr std::uint64_t ViewUniformBuffer = 0xf10cd51552e37c76ull;
// The anisotropy pass (UE 4.26 AnisotropyRendering.cpp): its pixel and vertex shader types.
constexpr std::uint64_t AnisotropyPixelShader = 0x9bf32d7a7d526129ull;
constexpr std::uint64_t AnisotropyVertexShader = 0xeb0dacba05a54aedull;
} // namespace EngineHash
} // namespace P4MapKit

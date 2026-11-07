// Exact pointer-free UE4.26 component tails. This is independent of whether a
// native payload was decoded for comparison; general decoded LOD/bulk data does
// not qualify for relocation merely because its values can be read.
#pragma once
#include "level_native.h"

namespace P4MapKit {
enum class EPointerFreeComponentTail { None, SkyAtmosphereLightingGuid, EmptyStaticMeshLods };
struct FPointerFreeComponentTail {
    EPointerFreeComponentTail Kind = EPointerFreeComponentTail::None;
    std::array<std::uint32_t, 4> LightingGuid{};
    std::size_t End = 0;
    FReadError Error;
};
inline FPointerFreeComponentTail ReadPointerFreeComponentTail(const std::string& Class,
    const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End)
{
    FPointerFreeComponentTail Out;
    const auto Refuse = [&](const char* Why) { Out.Error = {Start, Why}; return Out; };
    if (Start > End || End > Body.size()) return Refuse("component-tail-invalid-bounds");
    if (Class == "/Script/Engine.SkyAtmosphereComponent")
    {
        // Stock Serialize calls Super, then writes only bStaticLightingBuiltGUID.
        // Restrict the UObject identity to the empty 4-byte marker; the remaining
        // 16 bytes are the actual GUID words, never offsets or source substitutes.
        if (End - Start != 20) return Refuse("sky-atmosphere-tail-size-not-qualified");
        FByteReader Reader(Body, Start, End); std::uint32_t Identity = 0;
        if (!Reader.U32(Identity) || Identity != 0) return Refuse("sky-atmosphere-object-id-not-empty");
        for (auto& Word : Out.LightingGuid) if (!Reader.U32(Word)) return Refuse("sky-atmosphere-guid-truncated");
        if (Reader.Tell() != End) return Refuse("sky-atmosphere-tail-not-consumed");
        Out.Kind = EPointerFreeComponentTail::SkyAtmosphereLightingGuid; Out.End = End; return Out;
    }
    if (Class == "/Script/Engine.StaticMeshComponent")
    {
        // Stock Serialize calls Super, then writes LODData. Only exactly the empty
        // identity marker and zero LOD count are admitted; no per-LOD/bulk layout.
        if (End - Start != 8) return Refuse("static-mesh-tail-size-not-qualified");
        for (std::size_t At = Start; At < End; ++At)
            if (Body[At]) return Refuse("static-mesh-tail-not-empty");
        const auto Mesh = ReadStaticMeshComponentNative(Body, Start, End);
        if (!Mesh.Error.Code.empty() || Mesh.End != End || Mesh.Lods || !Mesh.LightingIds.empty())
            return Refuse("static-mesh-empty-tail-not-consumed");
        Out.Kind = EPointerFreeComponentTail::EmptyStaticMeshLods; Out.End = End; return Out;
    }
    return Refuse("component-tail-class-not-qualified");
}
}

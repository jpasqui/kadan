// A creator material's own shader map, compiled for Switch 1 (GLSL_SWITCH), as a map for the Switch 2 builds
// (SF_GLSL_SAGE; docs/SWITCH2_SHADERS.md, section 8).
//
// Kadan compiles a creator material through UE's shaders for the Switch (P4SwitchShaderMapFreeze.cpp): a map block
// for the cooked material (its frozen FMaterialShaderMapContent) and the map's programs, in the order the map's
// shaders name them. For a game build of the switch2 family the export converts both:
// - each program with ConvertSwitchPayloadToSage (core/sage_convert.h), in the same order;
// - the block's frozen map: the shader platform, which every FShaderMapContent (bytes 88) and every shader's target
//   (bits 4 and up of the word at 192) record, is the 2.0.0 maps' (40) where Switch 1's is 25. The type layouts the
//   block depends on are the same in both builds (checked on Area001's materials: 22 of 22 entries equal);
// - the library map is named again from the converted programs, and the block's resource hash (its last 20 bytes,
//   which name the map in the library) follows.
// Everything else in the block stays: the map id, the uniform expressions, the parameter bindings.
#pragma once
#include "sage_convert.h"
#include "material_shader_map.h"

namespace P4MapKit { namespace CustomMaterial { namespace OwnSage {

constexpr std::uint32_t SwitchShaderPlatform = 25, SageShaderPlatform = 40;

struct SageOwnMap {
    Bytes block, map_hash;
    std::vector<Bytes> programs;
};

// The frozen map's offset in a block as P4SwitchShaderMapFreeze writes it (and the game's cooked materials hold):
// resource count, name table, locations, data size, then cooked, valid, quality, feature, id hash, alignment, flags,
// frozen size, frozen bytes.
inline std::pair<std::size_t, std::size_t> FrozenRange(const Bytes& block) {
    SageReader r(block);
    if (r.u32() != 1) throw std::runtime_error("sage map: a block with more than one map");
    const auto names = r.count(4096, "map names");
    for (std::uint32_t k = 0; k < names; ++k) {
        const auto length = r.i32();
        if (length < -65536 || length > 65536) throw std::runtime_error("sage map: name length");
        r.bytes(length >= 0 ? std::size_t(length) : std::size_t(-length) * 2);
        r.u32();
    }
    const auto locations = r.count(64, "map locations");
    if (locations != 1) throw std::runtime_error("sage map: a block with more than one location");
    r.bytes(6);
    const auto size = r.u32();
    const auto data = r.offset();
    if (size > block.size() - data) throw std::runtime_error("sage map: data size");
    if (r.u32() != 1 || r.u32() != 1) throw std::runtime_error("sage map: not a cooked map");
    r.bytes(8 + 20 + 4);
    if (r.u32() != 1) throw std::runtime_error("sage map: frozen flags");
    const auto frozen = r.u32();
    const auto at = r.offset();
    if (frozen > block.size() - at) throw std::runtime_error("sage map: frozen size");
    return {at, frozen};
}

// Sets the shader platform in a frozen material map: each content's and each shader's.
inline void SetFrozenPlatform(Bytes& frozen, std::uint32_t from, std::uint32_t to) {
    FMaterialFrozenReader r(frozen);
    std::vector<std::size_t> contents{0};
    std::size_t meshes = 0, count = 0;
    if (frozen.size() < 0x1fc || !r.Array(96, 8, meshes, count))
        throw std::runtime_error("sage map: frozen map without its mesh maps");
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t at = 0;
        bool present = false;
        if (!r.Pointer(meshes + i * 8, at, present)) throw std::runtime_error("sage map: mesh map pointer");
        if (present) contents.push_back(at);
    }
    unsigned shaders = 0;
    for (std::size_t base : contents) {
        if (base + 96 > frozen.size() || frozen[base + 88] != from)
            throw std::runtime_error("sage map: a shader map content of another platform");
        frozen[base + 88] = std::uint8_t(to);
        std::size_t list = 0, n = 0;
        if (!r.Array(base + 56, 8, list, n)) {
            if (r.Error.Code == "material-array-null") continue;
            throw std::runtime_error("sage map: shader list");
        }
        for (std::size_t k = 0; k < n; ++k) {
            std::size_t at = 0;
            bool present = false;
            if (!r.Pointer(list + k * 8, at, present) || !present || at + 196 > frozen.size())
                throw std::runtime_error("sage map: shader pointer");
            std::uint32_t target = Get32(&frozen[at + 192]);
            if ((target >> 4) != from) throw std::runtime_error("sage map: a shader of another platform");
            target = (target & 15u) | (to << 4);
            std::memcpy(&frozen[at + 192], &target, 4);
            ++shaders;
        }
    }
    if (!shaders) throw std::runtime_error("sage map: no shaders");
}

inline SageOwnMap ConvertOwnMapToSage(const Bytes& block, const Bytes& mapHash, const std::vector<Bytes>& programs) {
    if (mapHash.size() != 20 || block.size() < 20 || !std::equal(mapHash.begin(), mapHash.end(), block.end() - 20))
        throw std::runtime_error("sage map: the block does not name its map");
    SageOwnMap out;
    Bytes named;
    for (const char c : std::string("kadan-sage-map")) named.push_back(std::uint8_t(c));
    for (std::size_t k = 0; k < programs.size(); ++k) {
        try {
            out.programs.push_back(ConvertSwitchPayloadToSage(programs[k]).payload);
        } catch (const std::exception& e) {
            throw std::runtime_error("program " + std::to_string(k) + " of the map: " + e.what());
        }
        const auto digest = Sage::Sha1(out.programs.back());
        named.insert(named.end(), digest.begin(), digest.end());
    }
    const auto hash = Sage::Sha1(named);
    out.map_hash.assign(hash.begin(), hash.end());
    out.block = block;
    const auto range = FrozenRange(block);
    Bytes frozen(block.begin() + std::ptrdiff_t(range.first), block.begin() + std::ptrdiff_t(range.first + range.second));
    SetFrozenPlatform(frozen, SwitchShaderPlatform, SageShaderPlatform);
    std::copy(frozen.begin(), frozen.end(), out.block.begin() + std::ptrdiff_t(range.first));
    std::copy(out.map_hash.begin(), out.map_hash.end(), out.block.end() - 20);
    return out;
}

}}}  // namespace P4MapKit::CustomMaterial::OwnSage

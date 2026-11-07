// Shader objects of the base pass classes (shader_objects.py, shader_classes.py), for the own map writer.
// Part of own_map_writer.h, which includes every part.
#pragma once
#include "own_map_uniforms.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace OwnMap
{
// ----- shader objects (shader_objects.py, shader_classes.py, own_material.shader_node) -----------------------------
// The size a shader's binding records for a uniform buffer struct: its constant data in the game's layout. A struct the
// game's maps name only by its hash (the instanced static mesh vertex factory's buffer, stage 7) records none.
inline std::uint32_t StructSize(const std::string& name)
{
    const auto& sizes = OwnSwitch::GameConstantSizes();
    const auto it = sizes.find(name);
    if (it != sizes.end())
        return it->second;
    Need(name == "044188379042fabe", "no listed size for uniform buffer struct " + name);
    return 0;
}
inline std::size_t StructOrder(const std::string& name)
{
    static const std::vector<std::string> order{"DrawRectangleParameters", "View", "DeferredLightUniforms", "Primitive",
        "4ba67c34c6671332", "64c64e3dd42f6064", "3e44a39f6365f2b3", "83c95d3294e72e70", "8eb448214a2d2367",
        "LightmapResourceCluster", "LocalVF", "PrecomputedLightingBuffer", "044188379042fabe", "ReflectionCapture",
        "IndirectLightingCache", "ShadowDepthPass"};
    auto it = std::find(order.begin(), order.end(), name);
    Need(it != order.end(), "uniform buffer struct not in the engine's order: " + name);
    return std::size_t(it - order.begin());
}
// The base pass classes' own fields after FMeshMaterialShader (242 bytes): uniform buffer bindings, in order.
inline const std::map<std::string, std::vector<std::string>>& BasePassSlots()
{
    static const auto slots = []
    {
        std::map<std::string, std::vector<std::string>> s;
        const std::vector<std::string> uniform{
            "PrecomputedLightingBuffer", "IndirectLightingCache", "LightmapResourceCluster"};
        auto with = [](std::vector<std::string> a, const std::vector<std::string>& b)
        {
            a.insert(a.end(), b.begin(), b.end());
            return a;
        };
        for (const char* policy : {"FNoLightMapPolicy", "FPrecomputedVolumetricLightmapLightingPolicy",
                 "TLightMapPolicyHQ", "TDistanceFieldShadowsAndLightMapPolicyHQ"})
        {
            s[std::string("TBasePassVS") + policy] = with(uniform, {"ReflectionCapture"});
            for (const char* sky : {"", "Skylight"})
                s[std::string("TBasePassPS") + policy + sky] = with(uniform, {"ReflectionCapture"});
        }
        const std::vector<std::pair<std::string, std::vector<std::string>>> self{
            {"FSelfShadowedTranslucencyPolicy", {"TranslucentSelfShadow"}},
            {"FSelfShadowedVolumetricLightmapPolicy", with(uniform, {"TranslucentSelfShadow"})},
            {"FSelfShadowedCachedPointIndirectLightingPolicy", with(uniform, {"TranslucentSelfShadow"})}};
        for (const auto& [policy, ps] : self)
        {
            s["TBasePassVS" + policy] = {"ReflectionCapture"};
            for (const char* sky : {"", "Skylight"})
                s["TBasePassPS" + policy + sky] = with(ps, {"ReflectionCapture"});
        }
        return s;
    }();
    return slots;
}

using Binding = std::pair<std::uint16_t, std::uint32_t>; // (slot, size)
// A loose parameter buffer of an object: its base index, its size and its parameters (offset, size).
struct LooseBuffer
{
    std::uint16_t base = 0, size = 0;
    std::vector<std::pair<std::uint16_t, std::uint16_t>> parameters;
};
// One version of the map: a shader class with its frequency, the uniform buffers its program binds through the
// object ({slot: struct name}), its program, the Material buffer (slot, constants listed) and one entry per parameter
// collection of the material (its slot and size, or none when the program leaves it unbound). Stage 7: typeHash for a
// class known only by hash; tail, the class's own bytes after the pass buffer slot, for a depth, shadow depth or
// geometry class (else the base pass bindings); passSlot, the pass uniform buffer's slot (with a tail); loose
// parameter buffers; vfParams, the vertex factory parameter object of a vertex shader (else the local factory's 11
// zero bytes); vf, the vertex factory (else FLocalVertexFactory).
struct Version
{
    std::string cls;
    unsigned frequency = 0;
    std::map<std::uint16_t, std::string> buffers;
    std::string program;
    std::optional<Binding> material;
    std::vector<std::optional<Binding>> collections;
    std::optional<std::uint64_t> typeHash;
    std::optional<Bytes> tail;
    std::uint16_t passSlot = 0xFFFF;
    std::vector<LooseBuffer> loose;
    std::optional<Bytes> vfParams;
    std::optional<std::uint64_t> vf;
};
inline std::uint64_t TypeHashOf(const Version& v)
{
    return v.typeHash ? *v.typeHash : HashedName(v.cls);
}
inline std::uint64_t VertexFactoryOf(const Version& v)
{
    return v.vf ? *v.vf : HashedName("FLocalVertexFactory");
}

// A uniform buffer struct's name hash; a struct known only by its hash is named by it (16 lowercase hex digits).
inline std::uint64_t StructHash(const std::string& name)
{
    if (name.size() == 16 &&
        std::all_of(name.begin(), name.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        return std::stoull(name, nullptr, 16);
    return HashedName(name);
}

inline Bytes FrozenArray(std::size_t count)
{
    Bytes o(8, 0);
    Put32(o, count);
    Put32(o, count);
    return o;
}

// A mesh material shader object (base pass, depth, shadow depth or the one-pass point light geometry shader), as
// own_material.shader_node makes it (type and factory words are written by the block writer; the object's program
// is `resource` in the library map).
inline NodeRef ShaderNode(const Version& v, std::int32_t resource)
{
    std::map<std::uint16_t, std::uint32_t> listed;
    for (const auto& [slot, name] : v.buffers)
        listed[slot] = StructSize(name);
    if (v.material)
        listed[v.material->first] = v.material->second;
    for (const auto& c : v.collections)
        if (c)
            listed[c->first] = c->second;
    std::vector<std::pair<std::uint16_t, std::string>> structs(v.buffers.begin(), v.buffers.end());
    std::stable_sort(structs.begin(), structs.end(),
        [](const auto& a, const auto& b) { return StructOrder(a.second) < StructOrder(b.second); });
    Bytes o;
    for (int i = 0; i < 4; i++)
        PutBytes(o, FrozenArray(0)); // FShaderParameterBindings
    Put32(o, 0);
    Put16(o, 0xFFFF);
    Put16(o, 0); // StructureLayoutHash, RootParameterBufferIndex
    PutBytes(o, FrozenArray(listed.size())); // ParameterMapInfo.UniformBuffers
    for (int i = 0; i < 2; i++)
        PutBytes(o, FrozenArray(0)); // TextureSamplers, SRVs
    PutBytes(o, FrozenArray(v.loose.size())); // LooseParameterBuffers
    Put64(o, 0); // ParameterMapInfo.Hash
    PutBytes(o, FrozenArray(structs.size()));
    PutBytes(o, FrozenArray(structs.size()));
    Put64(o, 1);
    Put64(o, 1); // type index 0, vertex factory index 0
    Put32(o, (std::uint32_t(kShaderPlatformSwitch) << 4) | v.frequency);
    Put32(o, std::uint32_t(resource));
    Put32(o, 0);
    Put32(o, 0);
    PutBytes(o, FrozenArray(v.collections.size())); // FMaterialShader: collection buffers
    Put16(o, v.material ? v.material->first : 0xFFFF);
    Put16(o, 0);
    Put16(o, 0);
    Put16(o, 0);
    Put64(o, 0); // FMeshMaterialShader: VertexFactoryParameters
    if (v.tail)
    { // a depth, shadow depth or geometry class
        Put16(o, v.passSlot); // PassUniformBuffer
        PutBytes(o, *v.tail);
    }
    else
    { // a base pass class: its uniform buffer bindings
        Put16(o, 0xFFFF);
        auto slots = BasePassSlots().find(v.cls);
        Need(slots != BasePassSlots().end(), "not a base pass class written here: " + v.cls);
        std::map<std::string, std::uint16_t> byName;
        for (const auto& [slot, name] : v.buffers)
            byName[name] = slot;
        for (const auto& name : slots->second)
        {
            auto it = byName.find(name);
            Put16(o, it == byName.end() ? 0xFFFF : it->second);
        }
    }

    Bytes map, loose, hashes, bindings, collections;
    for (const auto& [slot, size] : listed)
    {
        Put16(map, slot);
        Put16(map, size);
    }
    std::vector<Node::Child> looseChildren;
    for (std::size_t row = 0; row < v.loose.size(); row++)
    {
        const auto& b = v.loose[row];
        Put16(loose, b.base);
        Put16(loose, b.size);
        Put32(loose, 0);
        PutBytes(loose, FrozenArray(b.parameters.size()));
        Bytes params;
        for (const auto& [offset, size] : b.parameters)
        {
            Put16(params, offset);
            Put16(params, size);
        }
        if (!params.empty())
            looseChildren.push_back({24 * row + 8, MakeNode("shader.loose-parameters", 2, params)});
    }
    for (const auto& [slot, name] : structs)
    {
        Put64(hashes, StructHash(name));
        Put16(bindings, slot);
    }
    for (const auto& c : v.collections)
        Put16(collections, c ? c->first : 0xFFFF);
    std::vector<Node::Child> children;
    if (!map.empty())
        children.push_back({72, MakeNode("shader.parameter-map", 2, map)});
    if (!loose.empty())
        children.push_back({120, MakeNode("shader.loose-buffers", 8, loose, looseChildren)});
    if (!hashes.empty())
        children.push_back({144, MakeNode("shader.buffer-structs", 8, hashes)});
    if (!bindings.empty())
        children.push_back({160, MakeNode("shader.buffer-parameters", 2, bindings)});
    if (!collections.empty())
        children.push_back({208, MakeNode("shader.collection-buffers", 2, collections)});
    // FLocalVertexFactoryShaderParameters: 11 bytes, the SpeedTree LOD parameter and whether it is bound (all zero);
    // or the version's own (the instanced static mesh factory's).
    if (v.frequency == 0)
        children.push_back(
            {SH_VF_PARAMETERS, MakeNode("vertex-factory-parameters", 8, v.vfParams ? *v.vfParams : Bytes(11, 0))});
    auto node = MakeNode("shader", 8, o, children);
    node->hasMeta = true;
    node->typeHash = TypeHashOf(v);
    node->vfHash = VertexFactoryOf(v);
    return node;
}

}
}
} // namespace P4MapKit::CustomMaterial::OwnMap

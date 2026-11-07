// The map, its library and the map of a material's versions (material_map_writer.py, own_material.py).
// Part of own_map_writer.h, which includes every part.
#pragma once
#include "own_map_shaders.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace OwnMap
{
// ----- the map (material_map_writer.py) ----------------------------------------------------------------------------
struct Shader
{
    std::uint64_t typeHash;
    std::int32_t permutation;
    NodeRef node;
};
struct Description
{
    std::vector<Shader> shaders; // the material's own shaders (no vertex factory)
    std::map<std::uint64_t, std::vector<Shader>> meshMaps; // vertex factory name hash -> its shaders
    UniformSet uniform;
};

inline std::uint32_t ShaderHashKey(std::uint64_t typeHash, std::int32_t permutation)
{
    return std::uint32_t(City::Hash128to64(typeHash, std::uint64_t(std::int64_t(permutation))) & 0xFFFF);
}
inline std::vector<Shader> SortShaders(std::vector<Shader> s)
{
    std::stable_sort(s.begin(), s.end(), [](const Shader& a, const Shader& b)
        { return std::tie(a.typeHash, a.permutation) < std::tie(b.typeHash, b.permutation); });
    return s;
}
// The FShaderMapContent part of a content (pointers zero) and its children, for sorted shaders and no pipelines.
inline std::pair<Bytes, std::vector<Node::Child>> ContentPart(const std::vector<Shader>& shaders, std::size_t size)
{
    const std::size_t n = shaders.size();
    std::size_t hashSize = 1;
    while (hashSize < std::max<std::size_t>((n * 3) / 2, 1))
        hashSize <<= 1;
    std::vector<std::uint32_t> buckets(hashSize, 0xFFFFFFFFu), nexts(n, 0xFFFFFFFFu);
    for (std::size_t i = 0; i < n; i++)
    {
        const auto key = ShaderHashKey(shaders[i].typeHash, shaders[i].permutation) & (hashSize - 1);
        nexts[i] = buckets[key];
        buckets[key] = std::uint32_t(i);
    }
    Bytes data(size, 0);
    auto words = [](const std::vector<std::uint32_t>& w)
    {
        Bytes o;
        for (auto x : w)
            Put32(o, x);
        return o;
    };
    std::vector<Node::Child> children{{0, MakeNode("hash.buckets", 4, words(buckets))}};
    if (n)
        children.push_back({8, MakeNode("hash.next", 4, words(nexts))});
    At32(data, 16, hashSize - 1);
    At32(data, 20, n);
    for (std::size_t at : {32, 48, 64})
    {
        At32(data, at, n);
        At32(data, at + 4, n);
    }
    if (n)
    {
        Bytes types, perms;
        std::vector<Node::Child> pointers;
        for (std::size_t i = 0; i < n; i++)
        {
            Put64(types, shaders[i].typeHash);
            Put32(perms, std::uint32_t(shaders[i].permutation));
            pointers.push_back({8 * i, shaders[i].node});
        }
        children.push_back({24, MakeNode("types", 8, types)});
        children.push_back({40, MakeNode("permutations", 4, perms)});
        children.push_back({56, MakeNode("shader-pointers", 8, Bytes(8 * n, 0), pointers)});
    }
    data[88] = kShaderPlatformSwitch;
    return {data, children};
}

inline Frozen WriteMap(const Description& d)
{
    const auto rootShaders = SortShaders(d.shaders);
    auto [root, children] = ContentPart(rootShaders, ROOT_SIZE);
    Bytes hashed; // FShaderMapContent::UpdateHash, root then each mesh map
    auto hashContent = [&](const std::vector<Shader>& s)
    {
        for (const auto& x : s)
        {
            Put64(hashed, x.typeHash);
            Put32(hashed, std::uint32_t(x.permutation));
        }
    };
    hashContent(rootShaders);
    std::vector<Node::Child> meshes;
    for (const auto& [vf, listed] : d.meshMaps)
    {
        const auto shaders = SortShaders(listed);
        auto [data, meshChildren] = ContentPart(shaders, MESH_MAP_SIZE);
        At64(data, 0x60, vf);
        meshes.push_back({8 * meshes.size(), MakeNode("mesh-map", 8, data, meshChildren)});
        hashContent(shaders);
    }
    if (!meshes.empty())
    {
        At32(root, ROOT_MESH_MAPS + 8, meshes.size());
        At32(root, ROOT_MESH_MAPS + 12, meshes.size());
        children.push_back({ROOT_MESH_MAPS, MakeNode("mesh-map-pointers", 8, Bytes(8 * meshes.size(), 0), meshes)});
    }
    Need(d.uniform.tail.size() == ROOT_CONTENT_HASH - USET, "uniform set size");
    std::copy(d.uniform.tail.begin(), d.uniform.tail.end(), root.begin() + std::ptrdiff_t(USET));
    for (const auto& [offset, node] : d.uniform.nodes)
        children.push_back({offset, node});
    const auto digest = Sha1(hashed);
    std::copy(digest.begin(), digest.end(), root.begin() + std::ptrdiff_t(ROOT_CONTENT_HASH));
    return Freeze(MakeNode("content", 8, root, children));
}

struct TypeLayoutEntry
{
    std::uint32_t size;
    Bytes hash;
};
inline std::map<std::uint64_t, TypeLayoutEntry> DefaultTypeLayouts()
{
    std::map<std::uint64_t, TypeLayoutEntry> out;
    for (const auto& t : kTypeLayouts)
        out[t.name] = {t.size, Bytes(t.hash, t.hash + 20)};
    return out;
}

// The vertex factory parameter class of each vertex factory and frequency (IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE).
inline std::optional<std::string> VertexFactoryParameters(std::uint64_t vfHash, unsigned frequency)
{
    static const std::vector<std::tuple<std::string, unsigned, std::string>> table{
        {"FLocalVertexFactory", 0, "FLocalVertexFactoryShaderParameters"},
        {"FInstancedStaticMeshVertexFactory", 0, "FInstancedStaticMeshVertexFactoryShaderParameters"},
        {"FSplineMeshVertexFactory", 0, "FSplineMeshVertexFactoryShaderParameters"},
        {"TGPUSkinVertexFactoryDefault", 0, "FGPUSkinVertexFactoryShaderParameters"},
        {"TGPUSkinMorphVertexFactoryDefault", 0, "FGPUSkinVertexFactoryShaderParameters"},
        {"FParticleSpriteVertexFactory", 0, "FParticleSpriteVertexFactoryShaderParametersVS"},
        {"FParticleSpriteVertexFactory", 3, "FParticleSpriteVertexFactoryShaderParametersPS"},
        {"FGPUSpriteVertexFactory", 0, "FGPUSpriteVertexFactoryShaderParametersVS"},
        {"FGPUSpriteVertexFactory", 3, "FGPUSpriteVertexFactoryShaderParametersPS"},
        {"FMeshParticleVertexFactory", 0, "FMeshParticleVertexFactoryShaderParameters"},
        {"FParticleBeamTrailVertexFactory", 0, "FParticleBeamTrailVertexFactoryShaderParameters"}};
    for (const auto& [vf, f, cls] : table)
        if (f == frequency && HashedName(vf) == vfHash)
            return cls;
    return std::nullopt;
}

// The type name hashes a frozen image holds (FMemoryImage::AddDependency).
inline std::set<std::uint64_t> DependencySet(const Image& image, const std::set<std::uint64_t>& vfParams)
{
    std::set<std::uint64_t> out{HashedName("FMaterialShaderMapContent"), HashedName("FHashedName"),
        HashedName("FShaderParameterInfo"), HashedName("FShaderUniformBufferParameter"),
        0xadb65cd16bb3690aULL}; // a 3-byte type every game map lists; its name is not found yet
    out.insert(vfParams.begin(), vfParams.end());
    const auto& root = image.sections.at(0).data;
    bool mesh = false, pipeline = false;
    for (const auto& s : image.sections)
    {
        mesh |= s.kind == "mesh-map";
        pipeline |= s.kind == "pipeline" || s.kind == "pipeline-pointers";
    }
    if (mesh)
        out.insert(HashedName("FMeshMaterialShaderMap"));
    if (pipeline)
        out.insert(HashedName("FShaderPipeline"));
    auto count = [&](std::size_t off) { return std::int32_t(Get32(root, off + 8)); };
    if (count(U_VECTOR_PRESHADERS) || count(U_SCALAR_PRESHADERS))
        out.insert(HashedName("FMaterialUniformPreshaderHeader"));
    if (count(U_SCALAR_PARAMS))
        out.insert(HashedName("FMaterialScalarParameterInfo"));
    if (count(U_VECTOR_PARAMS))
        out.insert(HashedName("FMaterialVectorParameterInfo"));
    bool textures = false;
    for (std::size_t t = 0; t < 5; t++)
        textures |= count(U_TEXTURES + 16 * t) != 0;
    if (textures)
        out.insert(HashedName("FMaterialTextureParameterInfo"));
    if (count(U_EXTERNAL))
        out.insert(HashedName("FMaterialExternalTextureParameterInfo"));
    for (const auto& s : image.sections)
        if (s.kind == "shader" && Get32(s.data, 120 + 8))
        {
            out.insert(HashedName("FShaderLooseParameterBufferInfo"));
            break;
        }
    return out;
}

// One serialized FMaterialResource (FMaterial::SerializeInlineShaderMap) around a frozen image, and the block
// SerializeInlineShaderMaps writes into the material's export: the name table, the resource's place, the resource.
inline Bytes FString(const std::string& text)
{ // ASCII with a length, or UTF-16 with a negative length
    const auto points = NeedName(text);
    Bytes o;
    if (std::all_of(points.begin(), points.end(), [](std::uint32_t v) { return v < 128; }))
    {
        Put32(o, text.size() + 1);
        PutBytes(o, Text(text));
        o.push_back(0);
        return o;
    }
    Put32(o, std::uint32_t(-std::int32_t(points.size() + 1))); // every accepted character is one UTF-16 unit
    for (auto v : points)
        Put16(o, v);
    Put16(o, 0);
    return o;
}
inline Bytes WriteBlock(const Description& d, const std::map<std::uint64_t, TypeLayoutEntry>& layouts,
    const Bytes& idHash, const Bytes& resourceHash)
{
    Need(idHash.size() == 20 && resourceHash.size() == 20, "map hashes are 20 bytes");
    auto frozen = WriteMap(d);
    auto& image = frozen.image;
    // the name table, sorted without case; name patches grouped by name, in the same order
    std::vector<std::string> texts;
    for (const auto& [place, key] : frozen.names)
        if (std::find(texts.begin(), texts.end(), key.text) == texts.end())
            texts.push_back(key.text);
    std::sort(texts.begin(), texts.end());
    std::stable_sort(
        texts.begin(), texts.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
    auto indexOf = [&](const std::string& t)
    { return std::size_t(std::find(texts.begin(), texts.end(), t) - texts.begin()); };
    std::map<std::pair<std::size_t, std::int64_t>, std::vector<std::pair<std::size_t, std::size_t>>> groups;
    for (const auto& [place, key] : frozen.names)
        groups[{indexOf(key.text), key.number}].push_back(place);
    const auto where = image.Offsets().first;
    std::vector<std::pair<std::pair<std::size_t, std::int64_t>, std::vector<std::pair<std::size_t, std::size_t>>>>
        scriptNames(groups.begin(), groups.end());
    for (auto& [key, places] : scriptNames)
        std::stable_sort(places.begin(), places.end(),
            [&](const auto& a, const auto& b) { return where[a.first] + a.second < where[b.first] + b.second; });
    std::stable_sort(scriptNames.begin(), scriptNames.end(),
        [&](const auto& a, const auto& b)
        {
            return std::make_pair(Lower(texts[a.first.first]), a.first.second) <
                std::make_pair(Lower(texts[b.first.first]), b.first.second);
        });
    // the pointer table: types and vertex factories in the order the image first names them
    std::vector<std::uint64_t> types, vfs;
    std::set<std::uint64_t> vfParams;
    for (std::size_t i = 0; i < image.sections.size(); i++)
    {
        auto& section = image.sections[i];
        if (section.kind != "shader")
            continue;
        const auto& node = frozen.nodes[i];
        Need(node->hasMeta, "a shader node without its type");
        auto pos = [](std::vector<std::uint64_t>& list, std::uint64_t h)
        {
            auto it = std::find(list.begin(), list.end(), h);
            if (it == list.end())
            {
                list.push_back(h);
                return list.size() - 1;
            }
            return std::size_t(it - list.begin());
        };
        At64(section.data, SH_TYPE, (std::uint64_t(pos(types, node->typeHash)) << 1) | 1);
        if (node->vfHash)
        {
            At64(section.data, SH_VF, (std::uint64_t(pos(vfs, *node->vfHash)) << 1) | 1);
            if (section.pointers.count(SH_VF_PARAMETERS))
                if (auto cls = VertexFactoryParameters(*node->vfHash, Get32(section.data, SH_TARGET) & 15))
                    vfParams.insert(HashedName(*cls));
        }
    }
    auto deps = DependencySet(image, vfParams);
    deps.insert(types.begin(), types.end());

    const auto frozenBytes = image.Write();
    Bytes res;
    Put32(res, 1);
    Put32(res, 1); // cooked, valid
    const std::int32_t quality = 4, feature = 3;
    Put32(res, std::uint32_t(quality));
    Put32(res, std::uint32_t(feature));
    PutBytes(res, idHash);
    Put32(res, 0xffffffffu);
    Put32(res, 1);
    Put32(res, frozenBytes.size());
    PutBytes(res, frozenBytes);
    Put32(res, 0);
    Put32(res, scriptNames.size());
    Put32(res, 0); // vtables, script names, minimal names
    for (const auto& [key, places] : scriptNames)
    {
        Put32(res, key.first);
        Put32(res, std::uint32_t(key.second));
        Put32(res, places.size());
        for (const auto& [s, off] : places)
            Put32(res, where[s] + off);
    }
    Put32(res, types.size());
    Put32(res, vfs.size());
    for (auto h : types)
        Put64(res, h);
    for (auto h : vfs)
        Put64(res, h);
    Put32(res, deps.size());
    for (auto h : deps)
    {
        auto it = layouts.find(h);
        if (it == layouts.end())
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
            throw Error(std::string("no type layout for ") + buf);
        }
        Put64(res, h);
        Put32(res, it->second.size);
        PutBytes(res, it->second.hash);
    }
    Put32(res, 1);
    PutBytes(res, resourceHash); // share code, resource hash

    Bytes out;
    Put32(out, 1);
    Put32(out, texts.size());
    for (const auto& t : texts)
    {
        PutBytes(out, FString(t));
        auto [a, b] = NameHashes(t);
        Put16(out, a);
        Put16(out, b);
    }
    Put32(out, 1);
    Put32(out, 0);
    Put8(out, feature);
    Put8(out, quality); // one location: offset, feature, quality
    Put32(out, res.size());
    PutBytes(out, res);
    return out;
}

// ----- the library (own_material.library) --------------------------------------------------------------------------
// FSerializedShaderArchive version 2 holding {map hash: payloads}, uncompressed; a shader is named by the SHA-1 of its
// payload and one stored twice is kept once.
inline Bytes Library(const std::vector<std::pair<Bytes, std::vector<Bytes>>>& maps)
{
    Bytes mapHashes, shaderHashes, mapEntries, shaderEntries, preload, indices, region;
    std::vector<Bytes> digests;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> entries;
    std::size_t shaderCount = 0, preloads = 0, indexCount = 0;
    for (const auto& [mapHash, payloads] : maps)
    {
        Need(mapHash.size() == 20, "map hashes are 20 bytes");
        const auto firstIndex = indexCount, firstPreload = preloads;
        for (const auto& payload : payloads)
        {
            const auto digest = Sha1(payload);
            auto it = std::find(digests.begin(), digests.end(), digest);
            if (it == digests.end())
            {
                digests.push_back(digest);
                PutBytes(shaderHashes, digest);
                // The payload's stage mark (SV, SG, SP), as the game's archive records it: vertex 0, geometry 4, pixel 3.
                const unsigned frequency = payload.size() >= 6 && payload[4] == 'S' ? (payload[5] == 'V'          ? 0
                                                                                              : payload[5] == 'G' ? 4
                                                                                                                  : 3)
                                                                                    : 3;
                Put64(shaderEntries, region.size());
                Put32(shaderEntries, payload.size());
                Put32(shaderEntries, payload.size());
                Put8(shaderEntries, frequency);
                entries.push_back({region.size(), std::uint32_t(payload.size())});
                PutBytes(region, payload);
                shaderCount++;
                it = digests.end() - 1;
            }
            const auto index = std::size_t(it - digests.begin());
            Put64(preload, entries[index].first);
            Put64(preload, entries[index].second);
            preloads++;
            Put32(indices, index);
            indexCount++;
        }
        PutBytes(mapHashes, mapHash);
        Put32(mapEntries, firstIndex);
        Put32(mapEntries, payloads.size());
        Put32(mapEntries, firstPreload);
        Put32(mapEntries, payloads.size());
    }
    Bytes out;
    Put32(out, 2);
    for (auto [items, count] :
        {std::pair{&mapHashes, maps.size()}, std::pair{&shaderHashes, shaderCount}, std::pair{&mapEntries, maps.size()},
            std::pair{&shaderEntries, shaderCount}, std::pair{&preload, preloads}, std::pair{&indices, indexCount}})
    {
        Put32(out, count);
        PutBytes(out, *items);
    }
    PutBytes(out, region);
    return out;
}

// ----- the map of a material's versions (own_material.build_versions) ----------------------------------------------
inline std::vector<std::string> ProgramsOf(const std::vector<Version>& versions)
{
    std::vector<std::string> order;
    for (const auto& v : versions)
        if (std::find(order.begin(), order.end(), v.program) == order.end())
            order.push_back(v.program);
    return order;
}
struct OwnMapResult
{
    Bytes block, library, mapHash, idHash;
    std::vector<Bytes> programs;
}; // programs: the library order

inline OwnMapResult BuildVersions(const std::vector<Version>& versions, const std::map<std::string, Bytes>& payloads,
    const UniformFacts& facts, const std::map<std::uint64_t, TypeLayoutEntry>& layouts = DefaultTypeLayouts())
{
    Need(!versions.empty(), "a map needs versions");
    const auto programs = ProgramsOf(versions);
    std::vector<Bytes> order;
    for (const auto& name : programs)
    {
        auto it = payloads.find(name);
        Need(it != payloads.end(), "no payload for program " + name);
        order.push_back(it->second);
    }
    // The map is named by what it holds, so a change to any program is a new map.
    Bytes named = Text("kadan-own-map");
    for (const auto& p : order)
        PutBytes(named, Sha1(p));
    for (const auto& c : facts.collections)
        PutBytes(named, c);
    OwnMapResult out;
    out.mapHash = Sha1(named);
    // The map's id also names its uniform set: two materials with the same programs and other defaults are two maps.
    auto uniform = WriteUniformSet(facts);
    out.idHash = Sha1(Cat({Text("kadan-own-id"), out.mapHash, uniform.tail, Text(ReprUniformNodes(uniform.nodes))}));
    Description d; // one mesh map per vertex factory
    for (const auto& v : versions)
    {
        const auto index = std::int32_t(std::find(programs.begin(), programs.end(), v.program) - programs.begin());
        d.meshMaps[VertexFactoryOf(v)].push_back({TypeHashOf(v), 0, ShaderNode(v, index)});
    }
    d.uniform = std::move(uniform);
    out.block = WriteBlock(d, layouts, out.idHash, out.mapHash);
    out.library = Library({{out.mapHash, order}});
    out.programs = std::move(order);
    return out;
}

}
}
} // namespace P4MapKit::CustomMaterial::OwnMap

// Offsets of the shader map image and the frozen tree (material_freeze.py), for the own map writer.
// Part of own_map_writer.h, which includes every part.
#pragma once
#include "own_map_bytes.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace OwnMap
{
// ----- offsets (material_image.py) ---------------------------------------------------------------------------------
constexpr std::size_t ROOT_SIZE = 0x1fc, MESH_MAP_SIZE = 0x68, ROOT_MESH_MAPS = 96, USET = 112;
constexpr std::size_t U_VECTOR_PRESHADERS = USET, U_SCALAR_PRESHADERS = USET + 16, U_SCALAR_PARAMS = USET + 32,
                      U_VECTOR_PARAMS = USET + 48, U_TEXTURES = USET + 64, U_EXTERNAL = USET + 144,
                      U_PRESHADER_NAMES = USET + 160, U_PRESHADER_DATA = USET + 176, U_VT_STACKS = USET + 192,
                      U_COLLECTIONS = USET + 208, LAYOUT = USET + 224, L_RESOURCES = LAYOUT + 16, L_NAME = LAYOUT + 120,
                      L_HASH = LAYOUT + 136;
constexpr std::size_t ROOT_SCENE_TEXTURES = 0x1dc, ROOT_CONTENT_HASH = 0x1e8;
constexpr std::size_t SH_TYPE = 176, SH_VF = 184, SH_TARGET = 192, SH_VF_PARAMETERS = 232;
constexpr std::uint8_t kShaderPlatformSwitch = 0x19, UBMT_TEXTURE = 5, UBMT_SAMPLER = 8;

// ----- the tree and the freeze (material_freeze.py) ----------------------------------------------------------------
struct NameKey
{
    std::string text;
    std::int64_t number = 0;
    bool operator==(const NameKey& o) const { return text == o.text && number == o.number; }
    bool operator!=(const NameKey& o) const { return !(*this == o); }
    bool operator<(const NameKey& o) const { return std::tie(text, number) < std::tie(o.text, o.number); }
};
using NamePatch = std::pair<std::size_t, NameKey>;
struct Node;
using NodeRef = std::shared_ptr<Node>;
struct Node
{
    struct Child
    {
        std::size_t offset;
        NodeRef node;
        std::int64_t inside = 0;
    };
    std::string kind;
    unsigned align = 8;
    Bytes data;
    std::vector<Child> children;
    std::vector<NamePatch> names;
    bool hasMeta = false;
    std::uint64_t typeHash = 0;
    std::optional<std::uint64_t> vfHash;
};
inline NodeRef MakeNode(std::string kind, unsigned align, Bytes data, std::vector<Node::Child> children = {},
    std::vector<NamePatch> names = {})
{
    auto n = std::make_shared<Node>();
    n->kind = std::move(kind);
    n->align = align;
    n->data = std::move(data);
    n->children = std::move(children);
    n->names = std::move(names);
    return n;
}

struct Section
{
    std::string kind;
    unsigned align = 8;
    Bytes data;
    std::map<std::size_t, std::pair<std::size_t, std::int64_t>> pointers; // offset -> (section, inside)
};
struct Image
{
    std::vector<Section> sections;
    std::pair<std::vector<std::size_t>, std::size_t> Offsets() const
    {
        std::size_t at = 0;
        std::vector<std::size_t> out;
        for (const auto& s : sections)
        {
            at = AlignUp(at, s.align);
            out.push_back(at);
            at += s.data.size();
        }
        return {out, at};
    }
    Bytes Write() const
    {
        const auto [where, total] = Offsets();
        Bytes out(total, 0);
        for (std::size_t i = 0; i < sections.size(); i++)
            std::copy(sections[i].data.begin(), sections[i].data.end(), out.begin() + std::ptrdiff_t(where[i]));
        for (std::size_t i = 0; i < sections.size(); i++)
            for (const auto& [off, link] : sections[i].pointers)
            {
                const std::int64_t delta = std::int64_t(where[link.first]) + link.second - std::int64_t(where[i] + off);
                At64(out, where[i] + off, (std::uint64_t(delta) << 1) | 1);
            }
        return out;
    }
};
struct Frozen
{
    Image image;
    std::map<std::pair<std::size_t, std::size_t>, NameKey> names; // (section, offset) -> name
    std::vector<NodeRef> nodes; // the node each section was made from
};

// FMemoryImageWriter allocation (each pointer field, in field order, depth first), then FMemoryImage::Flatten with
// sections of equal bytes, pointers and name patches merged.
inline Frozen Freeze(const NodeRef& root)
{
    using Pointer = std::tuple<std::size_t, std::size_t, std::int64_t>;
    struct Allocated
    {
        NodeRef node;
        std::vector<Pointer> pointers;
    };
    std::vector<Allocated> allocated;
    std::function<std::size_t(const NodeRef&)> allocate = [&](const NodeRef& node)
    {
        const std::size_t index = allocated.size();
        allocated.push_back({node, {}});
        auto children = node->children;
        std::stable_sort(children.begin(), children.end(),
            [](const Node::Child& a, const Node::Child& b) { return a.offset < b.offset; });
        for (const auto& c : children)
        {
            const auto target = allocate(c.node);
            allocated[index].pointers.emplace_back(c.offset, target, c.inside);
        }
        return index;
    };
    allocate(root);
    using Key = std::tuple<Bytes, std::vector<Pointer>, std::vector<NamePatch>>;
    std::map<Key, std::size_t> seen;
    std::vector<std::size_t> remap, unique;
    for (std::size_t i = 0; i < allocated.size(); i++)
    {
        auto names = allocated[i].node->names;
        std::sort(names.begin(), names.end());
        Key key{allocated[i].node->data, allocated[i].pointers, names};
        auto found = seen.find(key);
        if (found != seen.end())
        {
            remap.push_back(found->second);
            continue;
        }
        seen.emplace(std::move(key), unique.size());
        remap.push_back(unique.size());
        unique.push_back(i);
    }
    Frozen out;
    for (std::size_t u = 0; u < unique.size(); u++)
    {
        const auto& a = allocated[unique[u]];
        Section s;
        s.kind = a.node->kind;
        s.align = a.node->align;
        s.data = a.node->data;
        for (const auto& [off, target, inside] : a.pointers)
            s.pointers[off] = {remap[target], inside};
        for (const auto& [off, key] : a.node->names)
            out.names[{u, off}] = key;
        out.image.sections.push_back(std::move(s));
        out.nodes.push_back(a.node);
    }
    return out;
}

}
}
} // namespace P4MapKit::CustomMaterial::OwnMap

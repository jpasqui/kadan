// Bounded FMemoryImage section census and relocation for texture-list changes.
// Only the pointer fields proved by the existing material-image reader are used.
#pragma once
#include "custom_material_shader_set.h"
#include "material_shader_map.h"
namespace P4MapKit
{
namespace CustomMaterial
{
class FrozenSections
{
    const Bytes& source;
    P4MapKit::FMaterialFrozenReader reader;
    struct Start
    {
        std::set<unsigned> aligns;
        std::size_t size = 0;
        bool bounded = true;
    };
    std::map<std::size_t, Start> starts;
    std::set<std::size_t> shaders;
    void section(std::size_t at, unsigned align, std::size_t size, bool bounded = true)
    {
        auto i = starts.find(at);
        if (i == starts.end())
        {
            starts[at] = {{align}, size, bounded};
            return;
        }
        i->second.aligns.insert(align);
        if (bounded && i->second.bounded && i->second.size != size)
            throw std::runtime_error("frozen section size alias");
        if (bounded)
        {
            i->second.size = size;
            i->second.bounded = true;
        }
    }
    std::size_t pointer(std::size_t at)
    {
        std::uint64_t bits = 0;
        if (!reader.U(at, 8, bits))
            throw std::runtime_error(reader.Error.Code);
        if (!bits)
            return std::size_t(-1);
        if (!(bits & 1))
            throw std::runtime_error("unfrozen pointer");
        bool negative = (bits >> 63) != 0;
        auto magnitude = negative ? ((~bits + 1) >> 1) + 1 : bits >> 1;
        std::size_t target;
        if (negative)
        {
            if (magnitude > at)
                throw std::runtime_error("frozen pointer underflow");
            target = at - std::size_t(magnitude);
        }
        else
        {
            if (magnitude > source.size() || at > source.size() - magnitude)
                throw std::runtime_error("frozen pointer overflow");
            target = at + std::size_t(magnitude);
        }
        if (target >= source.size())
            throw std::runtime_error("frozen pointer leaves image");
        pointers[at] = target;
        return target;
    }
    std::pair<std::size_t, std::size_t> array(std::size_t at, unsigned stride, unsigned align)
    {
        std::size_t target = 0, n = 0;
        if (!reader.Array(at, stride, target, n))
            throw std::runtime_error(reader.Error.Code);
        if (n)
        {
            pointer(at);
            section(target, align, n * stride);
        }
        return {target, n};
    }
    void shader(std::size_t at, bool mesh)
    {
        if (!shaders.insert(at).second)
            return;
        section(at, 8, 0, false);
        for (auto p : std::vector<std::pair<unsigned, unsigned>>{{0, 8}, {16, 4}, {32, 4}, {48, 4}})
            array(at + p.first, p.second, 2);
        for (unsigned off : {72u, 88u, 104u})
            array(at + off, 4, 2);
        auto loose = array(at + 120, 24, 8);
        for (std::size_t i = 0; i < loose.second; i++)
            array(loose.first + 24 * i + 8, 4, 2);
        array(at + 144, 8, 8);
        array(at + 160, 2, 2);
        array(at + 208, 2, 2);
        if (mesh)
        {
            auto vf = pointer(at + 232);
            if (vf != std::size_t(-1))
            {
                unsigned align = 8;
                while (vf % align)
                    align /= 2;
                section(vf, align, 0, false);
            }
        }
    }
    void content(std::size_t at, bool mesh)
    {
        auto mask = Read32(source, at + 16), indexSize = Read32(source, at + 20);
        auto hash = pointer(at);
        if (hash == std::size_t(-1) || mask > 65535)
            throw std::runtime_error("missing shader hash buckets");
        section(hash, 4, 4 * (std::size_t(mask) + 1));
        auto next = pointer(at + 8);
        if (next != std::size_t(-1))
        {
            if (indexSize > 65536)
                throw std::runtime_error("shader hash next limit");
            section(next, 4, 4 * std::size_t(indexSize));
        }
        array(at + 24, 8, 8);
        array(at + 40, 4, 4);
        auto ss = array(at + 56, 8, 8);
        for (std::size_t i = 0; i < ss.second; i++)
        {
            auto s = pointer(ss.first + 8 * i);
            if (s != std::size_t(-1))
                shader(s, mesh);
        }
        auto ps = array(at + 72, 8, 8);
        for (std::size_t i = 0; i < ps.second; i++)
        {
            auto p = pointer(ps.first + 8 * i);
            if (p == std::size_t(-1))
                continue;
            section(p, 8, 72);
            for (unsigned j = 0; j < 5; j++)
            {
                auto s = pointer(p + 8 + 8 * j);
                if (s != std::size_t(-1))
                    shader(s, mesh);
            }
        }
    }

public:
    struct Section
    {
        std::size_t old_start = 0, old_size = 0;
        unsigned alignment = 1;
        Bytes bytes;
    };
    std::vector<Section> sections;
    std::map<std::size_t, std::size_t> pointers;
    std::map<std::size_t, std::size_t> relocated;
    explicit FrozenSections(const Bytes& b) : source(b), reader(b)
    {
        if (b.size() < 508 || b.size() > (1u << 18))
            throw std::runtime_error("frozen image size");
        section(0, 8, 508);
        content(0, false);
        auto meshes = array(96, 8, 8);
        for (std::size_t i = 0; i < meshes.second; i++)
        {
            auto m = pointer(meshes.first + 8 * i);
            if (m != std::size_t(-1))
            {
                section(m, 8, 104);
                content(m, true);
            }
        }
        for (auto a : std::vector<std::array<unsigned, 3>>{{{112, 8, 4}},
                                                           {{128, 8, 4}},
                                                           {{144, 24, 4}},
                                                           {{160, 36, 4}},
                                                           {{256, 32, 4}},
                                                           {{272, 12, 4}},
                                                           {{288, 1, 1}},
                                                           {{304, 40, 4}},
                                                           {{320, 16, 4}},
                                                           {{352, 4, 2}},
                                                           {{456, 2, 2}}})
            array(a[0], a[1], a[2]);
        for (unsigned i = 0; i < 5; i++)
        {
            array(176 + 16 * i, 28, 4);
            array(368 + 16 * i, 4, 2);
        }
        std::size_t before = 0;
        for (auto i = starts.begin(); i != starts.end(); ++i)
        {
            auto next = i;
            ++next;
            auto end = next == starts.end() ? b.size() : next->first;
            auto size = i->second.bounded ? i->second.size : end - i->first;
            if (size > end - i->first)
                throw std::runtime_error("frozen sections overlap");
            for (std::size_t k = i->first + size; k < end; k++)
                if (b[k])
                    throw std::runtime_error("unowned frozen section bytes");
            unsigned alignment = 0;
            for (auto a : i->second.aligns)
                if ((before + a - 1) / a * a == i->first)
                    alignment = std::max(alignment, a);
            if (!alignment)
                throw std::runtime_error("frozen section alignment mismatch");
            sections.push_back({i->first, size, alignment, Bytes(b.begin() + i->first, b.begin() + i->first + size)});
            before = i->first + size;
        }
        if (before != b.size())
            throw std::runtime_error("frozen trailing bytes");
        for (auto& p : pointers)
        {
            auto s = owner(p.first);
            if (p.first + 8 > sections[s].old_start + sections[s].bytes.size() || !starts.count(p.second))
                throw std::runtime_error("frozen pointer section missing");
        }
    }
    std::size_t owner(std::size_t at) const
    {
        for (std::size_t i = sections.size(); i-- > 0;)
            if (sections[i].old_start <= at && at - sections[i].old_start < sections[i].old_size)
                return i;
        throw std::runtime_error("frozen offset in no section");
    }
    // Own descriptors may introduce an array absent from the donor. New sections
    // have virtual source keys, never copy offsets into the original game image.
    void replace_or_insert_array(std::size_t field, Bytes bytes, unsigned count, unsigned alignment)
    {
        if (field + 16 > 508 || alignment == 0 || (alignment & (alignment - 1)) || alignment > 8 || count > 65536)
            throw std::runtime_error("owned frozen array shape");
        if (count == 0)
        {
            if (!bytes.empty())
                throw std::runtime_error("empty owned frozen array bytes");
            const auto pointer = pointers.find(field);
            if (pointer != pointers.end())
            {
                auto target = pointer->second;
                unsigned users = 0;
                for (const auto& old : pointers)
                    if (old.second == target)
                        ++users;
                if (users != 1)
                    throw std::runtime_error("owned empty array shares source bytes");
                sections.at(owner(target)).bytes.clear();
                pointers.erase(pointer);
            }
            for (unsigned i = 0; i < 8; ++i)
                sections[0].bytes[field + i] = 0;
            Set32(sections[0].bytes, field + 8, 0);
            Set32(sections[0].bytes, field + 12, 0);
            return;
        }
        if (pointers.count(field))
        {
            replace_array(field, std::move(bytes), count);
            return;
        }
        if (bytes.empty())
            throw std::runtime_error("owned frozen array missing bytes");
        std::size_t key = source.size() + sections.size() + 1;
        sections.push_back({key, 0, alignment, std::move(bytes)});
        pointers[field] = key;
        Set32(sections[0].bytes, field + 8, count);
        Set32(sections[0].bytes, field + 12, count);
    }
    void replace_array(std::size_t field, Bytes bytes, unsigned count)
    {
        auto p = pointers.find(field);
        if (p == pointers.end())
            throw std::runtime_error("new frozen array insertion unavailable for texture template");
        auto target = p->second;
        unsigned users = 0;
        for (auto q : pointers)
            if (q.second == target)
                ++users;
        if (users != 1)
            throw std::runtime_error("frozen array shares bytes");
        auto s = owner(target);
        if (sections[s].old_start != target)
            throw std::runtime_error("frozen array starts inside section");
        sections[s].bytes = std::move(bytes);
        Set32(sections[0].bytes, field + 8, count);
        Set32(sections[0].bytes, field + 12, count);
    }
    Bytes write()
    {
        Bytes out;
        for (auto& s : sections)
        {
            while (out.size() % s.alignment)
                out.push_back(0);
            relocated[s.old_start] = out.size();
            Append(out, s.bytes);
        }
        for (auto& p : pointers)
        {
            auto s = owner(p.first);
            auto at = relocated[sections[s].old_start] + (p.first - sections[s].old_start),
                 target = relocated.at(p.second);
            std::int64_t delta = std::int64_t(target) - std::int64_t(at);
            auto bits = (std::uint64_t(delta) << 1) | 1;
            for (unsigned i = 0; i < 8; i++)
                out[at + i] = std::uint8_t(bits >> (8 * i));
        }
        return out;
    }
    std::size_t relocate(std::size_t old) const
    {
        auto s = owner(old);
        return relocated.at(sections[s].old_start) + old - sections[s].old_start;
    }
};
}
}

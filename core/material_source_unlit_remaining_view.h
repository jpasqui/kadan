// Named original View matrix/vector rows. These identities describe a uniform
// ABI; they do not establish pass scheduling or renderer admission.
#pragma once
#include "material_source_unlit_scene.h"
#include "material_engine_hashes.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalViewRowFacts
{
    std::map<std::pair<int, int>, std::string> ConsumedFields;
    bool ConstructorAssociationProved = false;
    bool SchedulingProven = false, CompilerProven = false, RendererProven = false, ProductionAdmitted = false;
};
inline void InstallOriginalMaterialViewRows(SymbolicArena& arena, BindingContext& binding,
                                            const FMaterialShaderReference& reference,
                                            const CustomMaterial::ShaderBindings& header,
                                            bool originalAssociationProved,
                                            const std::shared_ptr<MaterialOriginalViewRowFacts>& facts = {})
{
    const auto previous = binding.LeafOverride;
    const auto buffers = binding.Buffers;
    binding.LeafOverride =
        [previous, buffers, reference, header, originalAssociationProved, facts, &arena](const Value& value, Value& out)
    {
        if (!is(value, "IN") || Args(value).size() != 3)
            return previous && previous(value, out);
        const auto& a = Args(value);
        if (a[0].kind != Value::Text || a[0].text != "buffer" || !IsNumber(a[1]) || !IsNumber(a[2]) ||
            !std::isfinite(a[1].number()) || !std::isfinite(a[2].number()) || a[1].number() < 0 ||
            a[1].number() > 255 || a[2].number() < 0 || a[2].number() > 65535 ||
            std::floor(a[1].number()) != a[1].number() || std::floor(a[2].number()) != a[2].number())
            return previous && previous(value, out);
        const int buffer = int(a[1].number()), offset = int(a[2].number());
        const auto named = buffers.find(buffer);
        if (named == buffers.end() || named->second != "View")
            return previous && previous(value, out);
        struct Field
        {
            const char* Name;
            int Offset, Count;
        };
        const Field fields[] = {{"TranslatedWorldToView", 192, 16},
                                {"TranslatedWorldToCameraView", 320, 16},
                                {"ViewToClip", 448, 16},
                                {"ClipToTranslatedWorld", 640, 16},
                                {"BufferSizeAndInvSize", 2128, 4}};
        for (const auto& field : fields)
        {
            if (offset < field.Offset || offset >= field.Offset + 4 * field.Count)
                continue;
            if ((offset - field.Offset) % 4)
                throw BindingRefused("original View row read is not word aligned", "View matrix bindings");
            const auto hashed = reference.UniformBufferHashes.find(std::uint8_t(buffer));
            if (!originalAssociationProved || reference.Frequency != 3 ||
                std::size_t(buffer) >= header.layout_hashes.size() ||
                header.layout_hashes[std::size_t(buffer)] != EngineHash::ViewLayout ||
                hashed == reference.UniformBufferHashes.end() || hashed->second != EngineHash::ViewUniformBuffer)
                throw BindingRefused("original View row lacks its checked constructor/layout association",
                                     "View matrix bindings");
            const int component = (offset - field.Offset) / 4;
            auto resolved =
                source(arena, "WaterViewRow", Value::tuple_value({field.Name, component / 4}), component % 4);
            Value other;
            if (previous && previous(value, other) && !same(other, resolved))
                throw BindingRefused("original View row conflicts with an existing input binding",
                                     "View matrix binding conflict");
            out = resolved;
            if (facts)
            {
                facts->ConstructorAssociationProved = true;
                facts->ConsumedFields[{buffer, offset}] = field.Name;
            }
            return true;
        }
        return previous && previous(value, out);
    };
}
}
} // P4MapKit::Material

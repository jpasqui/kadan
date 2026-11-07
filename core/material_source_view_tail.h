// Additional named original View inputs. Source graphs that replaced these
// fields with unknown zero remain separate, incomplete comparison baselines.
#pragma once
#include "material_source_unlit_remaining_view.h"
#include "material_engine_hashes.h"
#include "material_rewrite.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalViewTailFacts : MaterialEvidence
{
    std::map<std::pair<int, int>, std::string> ConsumedFields;
    bool ConstructorAssociationProved = false;
};
inline void InstallOriginalMaterialViewTail(SymbolicArena& arena, BindingContext& binding,
                                            const FMaterialShaderReference& reference,
                                            const CustomMaterial::ShaderBindings& header,
                                            bool originalAssociationProved,
                                            const std::shared_ptr<MaterialOriginalViewTailFacts>& facts = {})
{
    InstallOriginalMaterialViewRows(arena, binding, reference, header, originalAssociationProved);
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
        if (named == buffers.end() || named->second != "View" || !((offset >= 384 && offset < 448) || offset == 2444))
            return previous && previous(value, out);
        const auto hashed = reference.UniformBufferHashes.find(std::uint8_t(buffer));
        if (!originalAssociationProved || reference.Frequency != 3 ||
            std::size_t(buffer) >= header.layout_hashes.size() ||
            header.layout_hashes[std::size_t(buffer)] != EngineHash::ViewLayout ||
            hashed == reference.UniformBufferHashes.end() || hashed->second != EngineHash::ViewUniformBuffer ||
            (offset >= 384 && offset < 448 && (offset - 384) % 4))
            throw BindingRefused("additional original View input lacks checked word/layout association",
                                 "original View tail bindings");
        const bool matrix = offset != 2444;
        const int component = matrix ? (offset - 384) / 4 : 0;
        const std::string name = matrix ? "CameraViewToTranslatedWorld" : "RealTime";
        const auto resolved =
            source(arena, "OriginalViewTail", Value::tuple_value({name, component / 4}), component % 4);
        Value other;
        if (previous && previous(value, other) && !same(other, resolved))
            throw BindingRefused("original View tail conflicts with existing input binding",
                                 "original View tail conflict");
        out = resolved;
        if (facts)
        {
            facts->ConstructorAssociationProved = true;
            facts->ConsumedFields[{buffer, offset}] = name;
        }
        return true;
    };
}
// Seed exact native fields through the existing Lifter's public memo. No
// unknown source is accepted and no fabricated parameter replaces an input.
inline void SeedOriginalMaterialViewTail(Lifter& lifter, const std::vector<Value>& values)
{
    for (const auto& value : walk_formulas(values))
    {
        if (!is(value, "SRC") || Args(value).size() != 2 || Args(value)[0].kind != Value::Text ||
            Args(value)[0].text != "OriginalViewTail")
            continue;
        const auto& detail = Args(value)[1];
        if (detail.kind != Value::Tuple || detail.tuple.size() != 2 || detail.tuple[0].kind != Value::Text ||
            !IsNumber(detail.tuple[1]))
            throw Refused("invalid original View tail identity", "original View tail identity");
        const auto name = detail.tuple[0].text;
        const double row = detail.tuple[1].number();
        const bool matrix = name == "CameraViewToTranslatedWorld";
        if ((!matrix && name != "RealTime") || !std::isfinite(row) || row < 0 || row >= (matrix ? 4 : 1) ||
            std::floor(row) != row)
            throw Refused("unknown original View tail identity", "original View tail identity");
        const int width = matrix ? 4 : 1;
        const auto suffix = matrix ? "[" + std::to_string(int(row)) + "]" : "";
        const auto id = lifter.node("Custom", Json::MakeObject(),
                                    Object({{"Code", "return ResolvedView." + name + suffix + ";"},
                                            {"OutputType", "CMOT_Float" + std::to_string(width)},
                                            {"Description", "Original engine View: " + name + suffix}}));
        for (int count = 1; count <= 4; ++count)
        {
            int variants = 1;
            for (int n = 0; n < count; ++n)
                variants *= width;
            for (int variant = 0; variant < variants; ++variant)
            {
                int rest = variant;
                std::string key, mask;
                bool entire = count == width;
                for (int n = 0; n < count; ++n)
                {
                    const int component = rest % width;
                    rest /= width;
                    key += ValueKey(lifter.arena.mk("COMP", {value, component})) + ";";
                    mask += "RGBA"[component];
                    entire &= component == n;
                }
                lifter.memo[key] = lifter.link(id, entire ? "" : mask);
            }
        }
    }
}
inline MaterialRecoveryPlan
WithOriginalMaterialViewTail(MaterialRecoveryPlan plan, const FMaterialShaderReference& reference,
                             const CustomMaterial::ShaderBindings& header, bool originalAssociationProved,
                             const std::shared_ptr<MaterialOriginalViewTailFacts>& facts = {})
{
    const auto prior = plan.ConfigureBinding;
    plan.ConfigureBinding =
        [prior, reference, header, originalAssociationProved, facts](SymbolicArena& arena, BindingContext& binding)
    {
        if (prior)
            prior(arena, binding);
        InstallOriginalMaterialViewTail(arena, binding, reference, header, originalAssociationProved, facts);
    };
    const auto normals = plan.NormalSamples;
    plan.NormalSamples = [normals](Lifter& lifter, const std::vector<Value>& values)
    {
        SeedOriginalMaterialViewTail(lifter, values);
        return normals ? normals(lifter, values) : normal_sample_ids(lifter, values);
    };
    return plan;
}
}
} // P4MapKit::Material

// Source-only atmosphere disk recovery. Native View identity and immutable
// archive association are checked independently of the finite physical proof.
// This adapter never admits a renderer pass, compiler or complete shader family.
#pragma once
#include "material_source_owner.h"
#include "material_engine_hashes.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialSkyDiskResourceFacts : MaterialEvidence
{
    int ViewBuffer = -1, TextureSlot = -1, SamplerSlot = -1;
    std::size_t Consumers = 0, Replacements = 0;
    bool RequiresIndependentSkyProof = true;
};
namespace SkyDiskDetail
{
// The View uniform buffer's constructor members of the atmosphere LUT texture and its sampler.
constexpr unsigned AtmosphereLutTexture = 52, AtmosphereLutSampler = 53;
inline std::vector<Value> Formula(SymbolicArena& arena, int slot)
{
    const auto op = [&](const std::string& name, std::initializer_list<Value> args)
    {
        return simplify(arena, name, std::vector<Value>(args));
    };
    const auto test = [&](const std::string& name, const Value& a, const Value& b)
    {
        return arena.mk("T.NE", {arena.mk("TRUNC.U", {arena.mk(name, {a, b})})});
    };
    const auto choose = [&](const Value& condition, const Value& yes, const Value& no)
    {
        return arena.mk("SEL", {condition, yes, no});
    };
    const auto view = [&](const std::string& name, int c = 0)
    {
        return source(arena, "View", name, c);
    };
    const auto dot = [&](const std::vector<Value>& a, const std::vector<Value>& b)
    {
        Value total = 0;
        for (int k = 0; k < 3; ++k)
            total = op("ADD", {total, op("MUL", {a[k], b[k]})});
        return total;
    };
    std::vector<Value> direction, light, position;
    for (int k = 0; k < 3; ++k)
    {
        direction.push_back(op("NEG", {source(arena, "CameraVector", Value::null(), k)}));
        light.push_back(view("atmosphere_light_direction", k));
        position.push_back(
            op("MUL", {op("ADD", {view("sky_world_camera_origin", k), op("NEG", {view("sky_planet_center", k)})}),
                       9.99999975e-06}));
    }
    const auto bottom = view("sky_bottom_radius_km"), top = view("sky_top_radius_km"), a = dot(direction, direction),
               b = op("MUL", {dot(direction, position), 2.0});
    const auto c = op("ADD", {op("MUL", {op("NEG", {bottom}), bottom}), dot(position, position)}),
               b2 = op("MUL", {b, b}), fourac = op("MUL", {op("MUL", {a, c}), 4.0});
    const auto discriminant = op("ADD", {b2, op("NEG", {fourac})}), root = op("RCP", {op("RSQ", {discriminant})});
    std::vector<Value> roots;
    for (const double sign : {-1.0, 1.0})
        roots.push_back(choose(test("F.SGE", b2, fourac),
                               op("MUL", {op("DIV", {op("ADD", {op("MUL", {root, sign}), op("NEG", {b})}), a}), .5}),
                               -1.0));
    const auto blocked =
        arena.mk("T.NE", {arena.mk("I.OR", {arena.mk("TRUNC.U", {arena.mk("F.SGT", {roots[0], 0.0})}),
                                            arena.mk("TRUNC.U", {arena.mk("F.SGT", {roots[1], 0.0})})})});
    const auto inverseHeight = op("RSQ", {dot(position, position)}), height = op("RCP", {inverseHeight});
    std::vector<Value> up;
    for (const auto& p : position)
        up.push_back(op("MUL", {inverseHeight, p}));
    const auto zenith = dot(direction, up), squared = op("DIV", {height, inverseHeight});
    const auto reach =
        op("MAX",
           {op("ADD", {op("MUL", {op("NEG", {height}), zenith}),
                       op("RCP", {op("RSQ", {op("ADD", {op("MUL", {top, top}),
                                                        op("ADD", {op("MUL", {op("MUL", {zenith, zenith}), squared}),
                                                                   op("NEG", {squared})})})})})}),
            0.0});
    const auto bottom2 = op("MUL", {bottom, bottom}),
               inverseRho =
                   op("RSQ", {op("MAX", {op("ADD", {op("MUL", {height, height}), op("NEG", {bottom2})}), 0.0})});
    const auto inverseH = op("RSQ", {op("MAX", {op("ADD", {op("MUL", {top, top}), op("NEG", {bottom2})}), 0.0})}),
               least = op("ADD", {op("NEG", {height}), top});
    const auto u = op("DIV",
                      {op("ADD", {reach, op("NEG", {least})}),
                       op("ADD", {op("NEG", {least}), op("ADD", {op("RCP", {inverseRho}), op("RCP", {inverseH})})})}),
               v = op("DIV", {inverseH, inverseRho});
    const auto sample = arena.mk("SCENETEX", {slot, u, v});
    std::vector<Value> out;
    for (int k = 0; k < 3; ++k)
    {
        out.push_back(choose(test("F.SEQ", view("reflection_capture_mask"), 0.0),
                             choose(test("F.SGT", dot(direction, light), view("atmosphere_light_disc_cos_half_apex")),
                                    op("MUL", {choose(blocked, 0.0, arena.mk("COMP", {sample, k})),
                                               view("atmosphere_light_disc_luminance", k)}),
                                    0.0),
                             0.0));
    }
    return out;
}
inline std::vector<Value> RewriteFormula(SymbolicArena& arena, const std::vector<Value>& values, int textureSlot,
                                         SceneRewriteFacts* facts)
{
    const auto formula = Formula(arena, textureSlot);
    std::map<const Expression*, int> components;
    for (int k = 0; k < 3; ++k)
        components.emplace(formula[k].expression.get(), k);
    return SceneDetail::apply(
        arena, values,
        [&](const Value& value)
        {
            const auto at = components.find(value.expression.get());
            if (at == components.end())
                return value;
            if (facts)
                facts->requires_independent_sky_proof = true;
            return source(arena, "SkyAtmosphereLightDiskLuminance", Value(0), at->second);
        },
        facts, "sun_disk");
}
inline int ResourceSlot(const CustomMaterial::ResourceMap& table, unsigned buffer, unsigned ordinal, unsigned maximum)
{
    const auto row = table.find(buffer);
    int found = -1;
    if (row != table.end())
        for (const auto& entry : row->second)
            if (entry.first == ordinal)
            {
                if (found != -1 || entry.second > maximum)
                    throw Refused("atmosphere resource has an ambiguous or invalid native slot",
                                  "material sky resource");
                found = int(entry.second);
            }
    if (found < 0)
        throw Refused("atmosphere resource is absent from the original View SRT", "material sky resource");
    return found;
}
// These are ordered constructor members, not shader binding slots. The actual
// texture and sampler slots are independent values from the original SRT.
inline CustomMaterial::ResourceMap ResourceTable(const std::vector<std::uint32_t>& table)
{
    try
    {
        return CustomMaterial::ReadResourceTable(table);
    }
    catch (const std::exception& e)
    {
        throw Refused(e.what(), "material sky resource");
    }
}
inline MaterialSkyDiskResourceFacts ResourceIdentity(const MaterialPreparedProgram& program)
{
    MaterialSkyDiskResourceFacts out;
    if (program.Reference.Frequency != 3)
        throw Refused("atmosphere disk requires an original pixel reference", "material sky association");
    for (const auto& buffer : program.Reference.UniformBufferHashes)
        if (buffer.second == EngineHash::ViewUniformBuffer)
        {
            if (out.ViewBuffer >= 0 || buffer.first > 31)
                throw Refused("atmosphere View identity is not unique", "material sky View");
            out.ViewBuffer = int(buffer.first);
        }
    if (out.ViewBuffer < 0 || std::size_t(out.ViewBuffer) >= program.Header.layout_hashes.size() ||
        program.Header.layout_hashes[std::size_t(out.ViewBuffer)] != EngineHash::ViewLayout ||
        !(program.Header.resource_bits & (1u << out.ViewBuffer)))
        throw Refused("atmosphere View name, layout or resource bit differs", "material sky View");
    const auto named = program.BaseBinding.Buffers.find(out.ViewBuffer);
    if (named == program.BaseBinding.Buffers.end() || named->second != "View")
        throw Refused("atmosphere View semantic binding differs", "material sky View");
    const auto textures = ResourceTable(program.Header.textures);
    out.TextureSlot = ResourceSlot(textures, unsigned(out.ViewBuffer), AtmosphereLutTexture, 63);
    for (const auto& buffer : textures)
        for (const auto& field : buffer.second)
            if (int(field.second) == out.TextureSlot &&
                (int(buffer.first) != out.ViewBuffer || field.first != AtmosphereLutTexture))
                throw Refused("atmosphere texture slot aliases another original resource", "material sky resource");
    out.SamplerSlot =
        ResourceSlot(ResourceTable(program.Header.samplers), unsigned(out.ViewBuffer), AtmosphereLutSampler, 31);
    return out;
}
inline void CheckLevelZero(const std::vector<std::string>& lines, const MaterialSkyDiskResourceFacts& out)
{
    bool levelZero = false;
    try
    {
        const auto trace = symbolic_trace(lines);
        std::vector<Value> roots;
        for (const auto& r : trace.reg)
            roots.insert(roots.end(), r.second.begin(), r.second.end());
        for (const auto& value : walk_formulas(roots))
            if (is(value, "TEX"))
            {
                const auto& a = value.expression->args;
                if (a.size() == 8 && NumberIs(a[2], 328 + 8 * out.TextureSlot))
                {
                    if (a[0].kind != Value::Text || a[0].text != "TXL" || a[1].kind != Value::Text ||
                        a[1].text != "2D" || !NumberIs(a[3], 1352 + 8 * out.SamplerSlot) || !NumberIs(a[7], 0))
                        throw Refused("original atmosphere LUT LOD/target/sampler differs", "material sky source");
                    levelZero = true;
                }
            }
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material sky source trace");
    }
    if (!levelZero)
        throw Refused("original atmosphere LUT has no reaching level-zero source sample", "material sky source");
}
inline MaterialSkyDiskResourceFacts SampleResources(const MaterialPreparedProgram& program,
                                                    MaterialSkyDiskResourceFacts out)
{
    // Reuse bounded native descriptor-cache dataflow while checking TXL rather
    // than the unrelated SceneColorCopy TEX operation.
    const auto normalized = NormalizeMaterialTraceDeclarations(program.Lines);
    NativeSceneDetail::Registers registers;
    struct Branch
    {
        NativeSceneDetail::Registers Before, Then;
        bool HasElse = false;
    };
    std::vector<Branch> branches;
    for (const auto& text : normalized.Lines)
    {
        const auto space = text.find(' '), dot = text.find('.');
        const auto head = text.substr(0, space), op = head.substr(0, dot);
        const auto args = split_operands(space == std::string::npos ? "" : text.substr(space + 1));
        if (op == "IF")
        {
            branches.push_back({registers, {}, false});
            continue;
        }
        if (op == "ELSE")
        {
            if (branches.empty() || branches.back().HasElse)
                throw Refused("atmosphere handle branch differs", "material sky source");
            branches.back().Then = registers;
            branches.back().HasElse = true;
            registers = branches.back().Before;
            continue;
        }
        if (op == "ENDIF")
        {
            if (branches.empty())
                throw Refused("atmosphere handle branch differs", "material sky source");
            const auto b = branches.back();
            branches.pop_back();
            registers = NativeSceneDetail::merge(b.HasElse ? b.Then : b.Before, registers);
            continue;
        }
        if (IsMember(op, {"REP", "ENDREP", "BRK", "CAL", "RET", "BRA"}))
            throw Refused("atmosphere handle control flow is unsupported", "material sky source");
        NativeSceneDetail::Handles values;
        if (op == "LDC" && head.find(".U64") != std::string::npos && args.size() == 2)
        {
            static const std::regex word("^buf([0-9]+)\\[([0-9]+)\\]$");
            std::smatch m;
            if (std::regex_match(args[1], m, word))
            {
                const auto b = MaterialDeclarationIndex(m[1]), offset = MaterialDeclarationIndex(m[2]);
                if (b <= 31 && offset <= 4096)
                    for (auto& v : values)
                    {
                        v.Known = true;
                        v.Words = {{int(b), int(offset)}};
                    }
            }
        }
        else if (op == "MOV" && head.find(".SAT") == std::string::npos && args.size() == 2)
            values = NativeSceneDetail::read(registers, args[1]);
        else if (op == "OR" && head.find(".U64") != std::string::npos && args.size() == 3)
        {
            const auto a = NativeSceneDetail::read(registers, args[1]), b = NativeSceneDetail::read(registers, args[2]);
            for (int c = 0; c < 4; ++c)
                if (a[c].Known && b[c].Known)
                {
                    values[c].Known = true;
                    values[c].Words = a[c].Words;
                    values[c].Words.insert(values[c].Words.end(), b[c].Words.begin(), b[c].Words.end());
                    std::sort(values[c].Words.begin(), values[c].Words.end());
                    values[c].Words.erase(std::unique(values[c].Words.begin(), values[c].Words.end()),
                                          values[c].Words.end());
                }
        }
        else if (IsMember(op, {"TEX", "TXB", "TXL", "TXF", "TXD", "TXP", "TXFMS", "TXQ", "TXG"}))
        {
            static const std::regex handle("^handle\\(([A-Za-z_]\\w*)(?:\\.([xyzw]))?\\)$");
            std::smatch m;
            std::size_t handleAt = 0;
            for (std::size_t i = 2; i < args.size(); ++i)
                if (std::regex_match(args[i], m, handle))
                {
                    if (handleAt)
                        throw Refused("atmosphere sample has several handles", "material sky source");
                    handleAt = i;
                    break;
                }
            if (!handleAt)
                throw Refused("atmosphere sample has no immutable handle", "material sky source");
            auto h = NativeSceneDetail::read(registers, m[1].str() + "." + (m[2].matched ? m[2].str() : "x"))[0];
            for (auto& word : h.Words)
            {
                if (word.first != 14)
                    throw Refused("atmosphere handle is outside the original descriptor cache", "material sky source");
                word.first = 0;
            }
            const auto slots = NativeSceneDetail::cache(h);
            if (slots.first == out.TextureSlot)
            {
                if (op != "TXL" || handleAt != 2 || args.size() != 4 || args.back() != "2D" ||
                    slots.second != out.SamplerSlot)
                {
                    throw Refused("atmosphere LUT operation or independent sampler differs", "material sky source");
                }
                ++out.Consumers;
            }
        }
        if (!args.empty())
            NativeSceneDetail::write(registers, args[0], values);
    }
    if (!branches.empty() || out.Consumers != 1)
        throw Refused("atmosphere LUT does not have one bounded original consumer", "material sky source");
    // Binder's SCENETEX form deliberately omits LOD. Check that information on
    // the original source DAG before permitting the complete bound formula.
    CheckLevelZero(normalized.Lines, out);
    return out;
}
inline MaterialSkyDiskResourceFacts Resources(const MaterialPreparedProgram& program)
{
    return SampleResources(program, ResourceIdentity(program));
}
}
class MaterialOriginalSkyDiskContract
{
    MaterialPreparedProgram original;
    MaterialSkyDiskResourceFacts resources;
    MaterialOriginalSkyDiskContract(MaterialPreparedProgram p, MaterialSkyDiskResourceFacts r)
        : original(std::move(p)), resources(std::move(r))
    {
    }
    friend MaterialOriginalSkyDiskContract PrepareOriginalSkyDiskContract(const MaterialOriginalOwnerSource&,
                                                                          const MaterialOriginalArchiveAccess&);

public:
    const MaterialPreparedProgram& Original() const
    {
        return original;
    }
    const MaterialSkyDiskResourceFacts& ResourceFacts() const
    {
        return resources;
    }
    MaterialSurfaceRewrites Rewrites(const std::shared_ptr<MaterialSkyDiskResourceFacts>& facts = {}) const
    {
        MaterialSurfaceRewrites out;
        const auto checked = resources;
        out.SceneAndSun = [checked, facts](SymbolicArena& arena, const std::vector<Value>& values)
        {
            SceneRewriteFacts rewritten;
            auto result = SkyDiskDetail::RewriteFormula(arena, values, checked.TextureSlot, &rewritten);
            const auto replaced = rewritten.replacements.find("sun_disk");
            if (replaced == rewritten.replacements.end() || replaced->second != 3 ||
                !rewritten.requires_independent_sky_proof)
                throw Refused("complete atmosphere disk component outline or native LUT role differs",
                              "material sky formula");
            std::set<int> channels;
            for (const auto& value : walk_formulas(result))
                if (is(value, "COMP"))
                {
                    const auto& a = value.expression->args;
                    if (a.size() == 2 && is(a[0], "SRC"))
                    {
                        const auto& src = a[0].expression->args;
                        if (src.size() == 2 && src[0].kind == Value::Text &&
                            src[0].text == "SkyAtmosphereLightDiskLuminance")
                        {
                            if (!NumberIs(src[1], 0) || !SceneDetail::number(a[1]) || a[1].number() < 0 ||
                                a[1].number() > 2)
                                throw Refused("atmosphere light or component index differs", "material sky formula");
                            channels.insert(int(a[1].number()));
                        }
                    }
                }
            if (channels != std::set<int>{0, 1, 2})
                throw Refused("atmosphere disk does not supply all three channels", "material sky formula");
            if (facts)
            {
                *facts = checked;
                facts->Replacements = replaced->second;
            }
            return result;
        };
        return out;
    }
};
// An unlit opaque surface whose program of record binds the View atmosphere LUT: the sky disk, whose sun disk the
// scene rewrites take from that LUT.
inline bool OriginalSkyDiskOwner(const MaterialOriginalOwnerSource& owner, const FMaterialShaderReference& program,
                                 const MaterialOriginalArchiveAccess& archive)
{
    if (owner.User.Cooked.Domain != "MD_Surface" || owner.User.Cooked.ShadingModel != "MSM_Unlit" ||
        owner.User.Cooked.BlendMode != "BLEND_Opaque")
        return false;
    const auto prepared =
        PrepareOriginalMaterialArchiveProgram(owner, program, archive, MaterialDomainSourceScope::ReadOnlyExpressions);
    const auto textures = SkyDiskDetail::ResourceTable(prepared.Program.Header.textures);
    for (const auto& buffer : prepared.Program.Reference.UniformBufferHashes)
    {
        if (buffer.second != EngineHash::ViewUniformBuffer)
            continue;
        const auto row = textures.find(unsigned(buffer.first));
        if (row == textures.end())
            continue;
        for (const auto& entry : row->second)
            if (entry.first == SkyDiskDetail::AtmosphereLutTexture)
                return true;
    }
    return false;
}
inline MaterialOriginalSkyDiskContract PrepareOriginalSkyDiskContract(const MaterialOriginalOwnerSource& owner,
                                                                      const MaterialOriginalArchiveAccess& archive)
{
    if (owner.User.Cooked.Domain != "MD_Surface" || owner.User.Cooked.ShadingModel != "MSM_Unlit" ||
        owner.User.Cooked.BlendMode != "BLEND_Opaque")
        throw Refused("atmosphere source owner settings differ", "material sky owner");
    const auto primary = SelectOriginalMaterialPrimaryProgram(owner);
    auto prepared =
        PrepareOriginalMaterialArchiveProgram(owner, primary, archive, MaterialDomainSourceScope::ReadOnlyExpressions);
    if (prepared.Program.Reference.TypeHash != 0x4507f03a199c6526ull ||
        MaterialFactoryName(prepared.Program.Reference.VertexFactoryHash) != "FLocalVertexFactory" ||
        prepared.Program.Reference.Permutation != 0)
        throw Refused("atmosphere source primary specialization differs", "material sky owner");
    auto resources = SkyDiskDetail::Resources(prepared.Program);
    return MaterialOriginalSkyDiskContract(std::move(prepared.Program), resources);
}
}
} // P4MapKit::Material

// Source-bound auxiliary inputs. These expression facts do not qualify a
// renderer pass, vertex deformation, compiler match or complete shader family.
#pragma once
#include "material_source_owner.h"
#include "material_engine_hashes.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalComplementRecovery : MaterialEvidence
{
    Json Graph, Facts = Json::MakeObject();
    std::vector<MaterialPreparedProgram> Programs;
};
inline bool OriginalMaterialShadowType(std::uint64_t type)
{
    return type == 0x37a48ab631bfc57aull || type == 0x4bbfae84d2dbe82aull || type == 0x7557e6252b46eb81ull;
}
inline MaterialPreparedProgram PrepareOriginalMaterialComplement(const MaterialOriginalOwnerSource& owner,
                                                                 const FMaterialShaderReference& selected,
                                                                 const MaterialOriginalArchiveAccess& archive)
{
    if (!archive.MapIndices || !archive.Payload || !archive.Frequency)
        throw Refused("original archive access is incomplete", "material complementary archive");
    const auto ref = CanonicalOriginalMaterialReference(owner, selected);
    const auto index = ArchiveIndexOf(archive.MapIndices(owner.Map.ResourceHash), ref,
                                      "auxiliary resource index is outside its original archive map",
                                      "material complementary association");
    auto prepared =
        PrepareOriginalMaterialPixelProgram(owner.Map, ref, archive.Payload(index), archive.Frequency(index));
    prepared.BaseBinding.Collections = owner.User.Collections;
    return prepared;
}
inline std::string OriginalComplementFresh(const Json& graph, const std::string& stem)
{
    for (std::size_t i = 0; i < 100000; ++i)
    {
        const auto name = stem + std::to_string(i);
        if (!graph.At("nodes").Has(name))
            return name;
    }
    throw Refused("no bounded auxiliary graph node identity is available", "material complementary nodes");
}
inline Json OriginalComplementLink(const std::string& name)
{
    return Array({name, ""});
}
inline Json OriginalComplementNode(const std::string& type, const Json& inputs, const Json& props)
{
    return Object({{"type", type}, {"inputs", inputs}, {"props", props}});
}
inline GraphValidation ValidateOriginalComplementGraph(const Json& graph)
{
    // The common schema owns link/cycle checks, including unused selectors.
    RequireKind(graph, Json::Object, "material complementary graph");
    RequireKind(graph.At("nodes"), Json::Object, "material complementary nodes");
    for (const auto& pair : graph.At("nodes").object)
    {
        const auto& node = pair.second;
        if (node.At("type").Text() != "ShadowReplace")
            continue;
        if (node.object.size() != 3 || node.At("inputs").kind != Json::Object || node.At("inputs").object.size() != 2 ||
            !node.At("inputs").Has("Default") || !node.At("inputs").Has("Shadow") ||
            node.At("props").kind != Json::Object || !node.At("props").object.empty())
            throw Refused("ShadowReplace differs from the stock Default/Shadow interface",
                          "material complementary selector node");
    }
    return ValidateGraph(graph);
}
// Only a single direct, unlayered scalar program identifies a parameter word.
// Caller names or defaults alone cannot associate a native constant-buffer read.
inline std::string OriginalComplementScalar(const FMaterialUniformSet& uniforms, std::size_t byte)
{
    const auto prefix = uniforms.VectorPrograms.size() * 16;
    if (uniforms.VirtualStackCount || byte < prefix || (byte - prefix) % 4 ||
        (byte - prefix) / 4 >= uniforms.ScalarPrograms.size())
        throw Refused("auxiliary scalar word has no supported frozen uniform program",
                      "material complementary uniform word");
    const auto& code = uniforms.ScalarPrograms[(byte - prefix) / 4].Instructions;
    if (code.size() != 1 || code[0].Opcode != EMaterialPreshaderOpcode::ScalarParameter ||
        code[0].Parameter >= uniforms.Scalars.size())
        throw Refused("auxiliary word is not a direct scalar parameter", "material complementary scalar program");
    const auto& scalar = uniforms.Scalars[code[0].Parameter];
    if (scalar.NameIsNone || scalar.Name.empty() || scalar.LayerIndex != -1 || scalar.Association != 2 ||
        scalar.DefaultBits[0] != 0)
        throw Refused("auxiliary parameter identity/default differs from the retained direct input",
                      "material complementary scalar identity");
    return scalar.Name;
}
inline std::size_t OriginalComplementLoadByte(const AnchorInstruction& load, const std::string& reg, char component,
                                              int material)
{
    std::smatch match;
    static const std::regex address("^buf([0-9]+)\\[([0-9]+)\\]$");
    if (load.op != "LDC" || load.dst != reg || load.mask.find(component) == std::string::npos || load.src.size() != 1 ||
        load.src[0].negative || load.src[0].absolute || !std::regex_match(load.src[0].base, match, address) ||
        match[1].str() != std::to_string(material) || load.has("SAT") || (!load.has("F32") && !load.has("F32X4")))
        throw Refused("IOR coefficient is not loaded directly from Material", "material refraction coefficient load");
    const auto byte = std::stoul(match[2]);
    if (byte > 65536)
        throw Refused("auxiliary uniform byte exceeds its bounded layout", "material complementary uniform word");
    return byte + (load.has("F32X4") ? 4 * std::string("xyzw").find(component) : 0);
}
inline std::size_t OriginalRefractionCoefficientByte(const MaterialPreparedProgram& program)
{
    if (program.BaseBinding.Buffers != std::map<int, std::string>{{0, "View"}, {2, "Material"}} ||
        program.Header.layout_hashes.size() < 2 || program.Header.layout_hashes[0] != EngineHash::ViewLayout ||
        program.Header.layout_hashes[1] != 0x098f0609u)
        throw Refused("auxiliary View/Distortion/Material ABI is not supported", "material refraction buffer ABI");
    const auto code = ParseMaterialAnchorInstructions(program.Lines);
    std::size_t found = code.size(), byte = 0;
    for (std::size_t i = 1; i < code.size(); ++i)
    {
        const auto& line = code[i];
        if (line.op != "MAD" || line.mask != "xy" || line.src.size() != 3 || !line.has("F32") || line.src[0].negative ||
            line.src[0].absolute || line.src[1].negative || line.src[1].absolute || !line.src[2].negative ||
            line.src[2].absolute || line.reads(1, "xy") != line.reads(2, "xy"))
            continue;
        const auto coefficient = line.reads(0, "xy");
        if (coefficient.second.size() != 2 || coefficient.second[0] != coefficient.second[1])
            continue;
        const auto& load = code[i - 1];
        if (load.op != "LDC" || load.dst != coefficient.first)
            continue;
        if (found != code.size())
            throw Refused("distortion has multiple candidate IOR coefficients",
                          "material refraction coefficient ambiguity");
        byte = OriginalComplementLoadByte(load, coefficient.first, coefficient.second[0], 2);
        found = i;
    }
    if (found == code.size())
        throw Refused("original distortion has no normal * (IOR - 1) coefficient anchor",
                      "material refraction coefficient anchor");
    // The adjacent native normalization is part of this IOR branch. Different
    // offset modes and raw/non-normalized coefficients retain named refusals.
    if (found < 4 || code[found - 2].op != "MUL" || code[found - 3].op != "RSQ" || code[found - 4].op != "DP3")
        throw Refused("IOR is not applied to the adjacent normalized view normal", "material refraction normal anchor");
    const auto& mul = code[found - 2];
    const auto& rsq = code[found - 3];
    const auto& dot = code[found - 4];
    const auto normal = code[found].reads(1, "xy");
    if (mul.src.size() != 2 || rsq.src.size() != 1 || dot.src.size() != 2 || mul.dst != normal.first ||
        mul.mask != normal.second || rsq.mask.size() != 1 || dot.mask.size() != 1 ||
        rsq.reads(0, rsq.mask) != std::make_pair(dot.dst, dot.mask) || dot.reads(0, "xyz") != dot.reads(1, "xyz") ||
        mul.src[0].negative || mul.src[0].absolute || mul.src[1].negative || mul.src[1].absolute ||
        rsq.src[0].negative || rsq.src[0].absolute || dot.src[0].negative || dot.src[0].absolute ||
        dot.src[1].negative || dot.src[1].absolute)
        throw Refused("distortion normalization producer dataflow differs", "material refraction normal anchor");
    const auto inverse = std::make_pair(rsq.dst, rsq.mask + rsq.mask), vector = dot.reads(0, "xy");
    if (!((mul.reads(0, mul.mask) == inverse && mul.reads(1, mul.mask) == vector) ||
          (mul.reads(1, mul.mask) == inverse && mul.reads(0, mul.mask) == vector)))
        throw Refused("distortion normal multiplication does not consume its inverse length",
                      "material refraction normal anchor");
    const auto textures = CustomMaterial::ReadResourceTable(program.Header.textures),
               samplers = CustomMaterial::ReadResourceTable(program.Header.samplers);
    const unsigned slot = program.BaseBinding.TextureParameters.empty() ? 0 : 1;
    const auto t = textures.find(1), s = samplers.find(1);
    if (t == textures.end() || s == samplers.end() ||
        t->second != std::vector<std::pair<unsigned, unsigned>>{{1, slot}} ||
        s->second != std::vector<std::pair<unsigned, unsigned>>{{13, slot}})
        throw Refused("distortion depth resource and point sampler association differs",
                      "material refraction depth ABI");
    return byte;
}
// The original shadow mask differs from the camera mask at one MAX operand.
// Keeping the complete camera prefix retains its LOD/discard dependencies.
inline std::string OriginalComplementSemanticInstruction(const MaterialPreparedProgram& program,
                                                         const std::string& line)
{
    static const std::regex buffer("buf([0-9]+)\\[");
    std::string result;
    std::size_t end = 0;
    for (std::sregex_iterator it(line.begin(), line.end(), buffer), last; it != last; ++it)
    {
        const auto& match = *it;
        result += line.substr(end, std::size_t(match.position()) - end);
        const auto number = std::stoi(match[1]);
        const auto found = program.BaseBinding.Buffers.find(number);
        result += (found == program.BaseBinding.Buffers.end() ? match.str() : "buffer<" + found->second + ">[");
        end = std::size_t(match.position() + match.length());
    }
    return result + line.substr(end);
}
inline void CertifyOriginalShadowPixelDepthTail(const MaterialPreparedProgram& shadow)
{
    if (shadow.Lines.size() < 4)
        throw Refused("explicit shadow depth tail is truncated", "material shadow pixel depth tail");
    const auto tail = shadow.Lines.size() - 4;
    std::smatch m;
    static const std::regex load("^LDC\\.F32X4 R0\\.w, buf([0-9]+)\\[432\\]$");
    if (!std::regex_match(shadow.Lines[tail], m, load) || !shadow.BaseBinding.Buffers.count(std::stoi(m[1])) ||
        shadow.BaseBinding.Buffers.at(std::stoi(m[1])) != "Pass" ||
        !std::regex_match(shadow.Lines[tail + 1],
                          std::regex("^MUL\\.F32 R0\\.x, fragment\\.attrib\\[[0-9]+\\](\\.x)?, R0\\.w$")) ||
        shadow.Lines[tail + 2] != "MOV.F result_color0, {0, 0, 0, 0}.x" ||
        !std::regex_match(
            shadow.Lines[tail + 3],
            std::regex("^ADD\\.F32\\.SAT result\\.depth\\.z, R0\\.x, fragment\\.attrib\\[[0-9]+\\]\\.x$")))
        throw Refused("explicit shadow depth depends on an unqualified value tail", "material shadow pixel depth tail");
}
inline bool OriginalShadowUsesCameraMask(const MaterialPreparedProgram& camera, const MaterialPreparedProgram& shadow)
{
    if (camera.Lines.empty() ||
        (shadow.Lines.size() != camera.Lines.size() && shadow.Lines.size() != camera.Lines.size() + 3))
        return false;
    for (std::size_t i = 0; i < camera.Lines.size(); ++i)
    {
        if (shadow.Lines.size() != camera.Lines.size() && i + 1 == camera.Lines.size())
        {
            if (camera.Lines[i] != "MOV.F result_color0, {0, 0, 0, 0}.x")
                return false;
            CertifyOriginalShadowPixelDepthTail(shadow);
            continue;
        }
        if (OriginalComplementSemanticInstruction(camera, camera.Lines[i]) !=
            OriginalComplementSemanticInstruction(shadow, shadow.Lines[i]))
            return false;
    }
    return true;
}
inline void CertifyOriginalFullShadowCoverage(const MaterialPreparedProgram& camera,
                                              const MaterialPreparedProgram& shadow, double clip)
{
    if (!std::isfinite(clip) || float(clip) < 0 || float(clip) > 1)
        throw Refused("full shadow coverage requires a finite retained clip in [0,1]", "material shadow coverage clip");
    const auto a = ParseMaterialAnchorInstructions(camera.Lines), b = ParseMaterialAnchorInstructions(shadow.Lines);
    MaterialMaskAnchor mask;
    if (!FindMaterialMaskAnchor(a, mask) || !mask.constant_clip || float(mask.clip_value) != float(clip) || !mask.line)
        throw Refused("camera depth clip differs from the cooked value", "material shadow camera clip");
    if (b.size() != a.size() && b.size() != a.size() + 3)
        throw Refused("shadow has a different camera/depth dependency prefix", "material shadow coverage prefix");
    const auto at = mask.line - 1;
    const auto& before = a[at];
    const auto& after = b[at];
    if (before.op != "MAX" || after.op != "MAX" || !before.has("F") || !after.has("F") || before.src.size() != 2 ||
        after.src.size() != 2 || before.mods != after.mods || before.dst != after.dst || before.mask != after.mask ||
        before.reads(0, before.mask) != after.reads(0, after.mask) || before.src[0].negative != after.src[0].negative ||
        before.src[0].absolute != after.src[0].absolute || !before.src[1].literal || before.src[1].negative ||
        before.src[1].absolute || before.src[1].scalar() != 0 || !after.src[1].literal || after.src[1].negative ||
        after.src[1].absolute || after.src[1].scalar() != 1 || after.dst != mask.mask.first ||
        after.mask != mask.mask.second)
        throw Refused("shadow mask is not the native MAX(camera-mask,1) boundary", "material shadow coverage anchor");
    for (std::size_t i = 0; i < a.size(); ++i)
        if (i != at && OriginalComplementSemanticInstruction(camera, camera.Lines[i]) !=
                           OriginalComplementSemanticInstruction(shadow, shadow.Lines[i]))
        {
            // Explicit pixel depth replaces only the camera's final color write.
            if (b.size() == a.size() + 3 && i + 1 == a.size() &&
                camera.Lines[i] == "MOV.F result_color0, {0, 0, 0, 0}.x")
                continue;
            throw Refused("shadow changes a retained camera mask/LOD instruction", "material shadow coverage prefix");
        }
    if (b.size() != a.size())
        CertifyOriginalShadowPixelDepthTail(shadow);
}
inline MaterialOriginalComplementRecovery
RecoverOriginalMaterialComplements(const MaterialOriginalOwnerSource& owner,
                                   const MaterialOriginalArchiveAccess& archive, const Json& primaryGraph)
{
    ValidateOriginalComplementGraph(primaryGraph);
    MaterialOriginalComplementRecovery result;
    result.Graph = primaryGraph;
    const auto primary = SelectOriginalMaterialPrimaryProgram(owner);
    std::vector<FMaterialShaderReference> refs;
    FReadError error;
    if (!ReadMaterialShaderReferences(owner.Map, refs, error))
        throw Refused(error.Code, "material complementary original inventory");
    std::vector<FMaterialShaderReference> distortion, shadow;
    for (const auto& ref : refs)
        if (ref.Frequency == 3 && ref.VertexFactoryHash == primary.VertexFactoryHash)
        {
            if (ref.TypeHash == 0x8b7ebf7dbd1d1758ull)
                distortion.push_back(ref);
            if (OriginalMaterialShadowType(ref.TypeHash))
                shadow.push_back(ref);
        }
    if (!distortion.empty())
    {
        if (distortion.size() != 1)
            throw Refused("selected factory has ambiguous original distortion programs",
                          "material refraction original inventory");
        const auto mode = owner.User.Chain.front().Properties.Field("RefractionMode");
        if (mode)
        {
            std::string value;
            if (!MaterialEnum(*mode, value) || value != "RM_IndexOfRefraction")
                throw Refused("original material has a different refraction mode", "material refraction cooked mode");
        }
        auto program = PrepareOriginalMaterialComplement(owner, distortion.front(), archive);
        const auto byte = OriginalRefractionCoefficientByte(program);
        const auto parameter = OriginalComplementScalar(owner.Map.Uniforms, byte);
        if (!result.Graph.Has("render_settings") || !result.Graph.At("render_settings").Has("RefractionDepthBias"))
            throw Refused("original graph lacks retained refraction depth bias", "material refraction depth bias");
        if (!owner.User.Settings.Has("render_settings") ||
            !owner.User.Settings.At("render_settings").Has("RefractionDepthBias") ||
            result.Graph.At("render_settings").At("RefractionDepthBias").Dump() !=
                owner.User.Settings.At("render_settings").At("RefractionDepthBias").Dump())
            throw Refused("graph refraction depth bias differs from actual cooked settings",
                          "material refraction depth bias");
        const auto node = OriginalComplementNode("ScalarParameter", Json::MakeObject(),
                                                 Object({{"ParameterName", parameter}, {"DefaultValue", 0.}}));
        if (result.Graph.At("outputs").Has("Refraction"))
        {
            const auto link = result.Graph.At("outputs").At("Refraction");
            if (link.kind != Json::Array || link.array.size() != 2 || link.array[1].Text() != "" ||
                result.Graph.At("nodes").At(link.array[0].Text()).Dump() != node.Dump())
                throw Refused("Refraction already has an unqualified expression",
                              "material refraction existing output");
        }
        else
        {
            const auto name = OriginalComplementFresh(result.Graph, "RecoveredAuxiliaryRefraction");
            result.Graph["nodes"][name] = node;
            result.Graph["outputs"]["Refraction"] = OriginalComplementLink(name);
        }
        result.Graph["refraction_mode"] = "RM_IndexOfRefraction";
        result.Programs.push_back(std::move(program));
        result.Facts["refraction_coefficient_recovered"] = true;
        result.Facts["distortion_tail_proven"] = false;
    }
    if (owner.User.Cooked.BlendMode == "BLEND_Masked" && !shadow.empty())
    {
        const auto camera =
            PrepareOriginalMaterialComplement(owner, SelectOriginalMaterialDepthProgram(owner, primary), archive);
        std::size_t same = 0, full = 0;
        for (const auto& ref : shadow)
        {
            auto program = PrepareOriginalMaterialComplement(owner, ref, archive);
            if (OriginalShadowUsesCameraMask(camera, program))
                ++same;
            else
            {
                CertifyOriginalFullShadowCoverage(camera, program, owner.User.Settings.At("clip").Numeric());
                ++full;
            }
            result.Programs.push_back(std::move(program));
        }
        if (same && full)
            throw Refused("selected shadow variants require different opacity-mask expressions",
                          "material shadow coverage variants");
        if (!full)
        {
            result.Facts["shadow_camera_mask_retained"] = true;
            ValidateOriginalComplementGraph(result.Graph);
            return result;
        }
        const auto one = OriginalComplementFresh(result.Graph, "ShadowCoverageOne");
        result.Graph["nodes"][one] = OriginalComplementNode("Constant", Json::MakeObject(), Object({{"R", 1.}}));
        const auto link = OriginalComplementLink(one);
        const auto previous =
            result.Graph.At("outputs").Has("OpacityMask") ? result.Graph.At("outputs").At("OpacityMask") : link;
        const auto name = OriginalComplementFresh(result.Graph, "ShadowPass");
        result.Graph["nodes"][name] = OriginalComplementNode(
            "ShadowReplace", Object({{"Default", previous}, {"Shadow", link}}), Json::MakeObject());
        result.Graph["outputs"]["OpacityMask"] = OriginalComplementLink(name);
        result.Facts["shadow_coverage_recovered"] = true;
        result.Facts["authored_shadow_mask_underidentified"] = true;
    }
    ValidateOriginalComplementGraph(result.Graph);
    return result;
}
}
} // P4MapKit::Material

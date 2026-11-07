// Additive original anisotropy family recovery; no compiler/render admission.
#pragma once
#include "material_anisotropy_contract.h"
#include "material_engine_hashes.h"
#include "material_source_owner.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalAnisotropyRecovery : MaterialEvidence
{
    Json Graph = Json::MakeObject(), Facts = Json::MakeObject();
    std::vector<MaterialPreparedProgram> Programs;
};
inline std::string OriginalAnisotropyHashText(std::uint64_t value)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}
// An owner the engine compiled an anisotropy pass for: its map holds an anisotropy pixel shader.
inline bool OriginalAnisotropyFamily(const MaterialOriginalOwnerSource& owner)
{
    for (const auto& ref : owner.References)
        if (ref.Frequency == 3 && ref.TypeHash == EngineHash::AnisotropyPixelShader)
            return true;
    return false;
}
inline std::vector<CustomMaterial::Varying> ReadOriginalAnisotropyVertexOutputs(const CustomMaterial::Bytes& payload)
{
    if (payload.size() < 10 || payload.size() > (1u << 20))
        throw Refused("anisotropy vertex payload range invalid", "anisotropy vertex interface");
    CustomMaterial::BindingReader reader(payload);
    if (reader.u32() != 0x474c534c || reader.u16() != 0x5653)
        throw Refused("anisotropy vertex header marker/frequency differs", "anisotropy vertex interface");
    const auto count = reader.count(32);
    for (unsigned i = 0; i < count; ++i)
        reader.packed();
    reader.packed();
    reader.varyings();
    return reader.varyings();
}
inline Json OriginalAnisotropyPinGraph(const MaterialAnisotropyInputs& inputs, const Json& defaults)
{
    Lifter lifter(*inputs.Arena, defaults);
    lifter.normal_samples = normal_sample_ids(lifter, inputs.Tangent);
    auto outputs = Json::MakeObject();
    outputs["Anisotropy"] = lifter.lift({inputs.Scalar.Formula});
    outputs["Tangent"] = lifter.lift(inputs.Tangent);
    return Object({{"nodes", lifter.nodes}, {"outputs", outputs}});
}
// Every texture the Tangent pin samples is one the anisotropy pass binds, sampled as a normal map at UV0.
inline void CertifyOriginalAnisotropyGraphTextures(const Json& graph, const std::set<std::string>& passTextures)
{
    const auto& nodes = graph.At("nodes");
    std::vector<std::string> pending = {graph.At("outputs").At("Tangent").array.at(0).Text()};
    std::set<std::string> seen;
    const auto number = [](const Json& props, const char* name, double fallback)
    {
        return props.Has(name) ? props.At(name).Numeric() : fallback;
    };
    const auto flag = [](const Json& props, const char* name, bool fallback)
    {
        if (!props.Has(name))
            return fallback;
        RequireKind(props.At(name), Json::Boolean, "anisotropy graph boolean");
        return props.At(name).boolean;
    };
    while (!pending.empty())
    {
        const auto name = pending.back();
        pending.pop_back();
        if (!seen.insert(name).second)
            continue;
        const auto& node = nodes.At(name);
        for (const auto& input : node.At("inputs").object)
            pending.push_back(input.second.array.at(0).Text());
        if (node.At("type").Text() != "TextureSampleParameter2D")
            continue;
        const auto& props = node.At("props");
        if (!passTextures.count(props.At("ParameterName").Text()) ||
            props.At("SamplerType").Text() != "SAMPLERTYPE_Normal" ||
            props.At("SamplerSource").Text() != "SSM_FromTextureAsset" || !flag(props, "AutomaticViewMipBias", true) ||
            (props.Has("MipValueMode") && props.At("MipValueMode").Text() != "TMVM_None") ||
            node.At("inputs").object.size() != 1 || !node.At("inputs").Has("Coordinates"))
            throw Refused("anisotropy TangentMap graph sampler/mip contract differs", "anisotropy graph texture");
        const auto& link = node.At("inputs").At("Coordinates");
        const auto& uv = nodes.At(link.array.at(0).Text());
        const auto& uvProps = uv.At("props");
        if (link.array.size() != 2 || link.array.at(1).Text() != "" || uv.At("type").Text() != "TextureCoordinate" ||
            !uv.At("inputs").object.empty() || number(uvProps, "CoordinateIndex", 0) != 0 ||
            number(uvProps, "UTiling", 1) != 1 || number(uvProps, "VTiling", 1) != 1 ||
            flag(uvProps, "UnMirrorU", false) || flag(uvProps, "UnMirrorV", false))
            throw Refused("anisotropy TangentMap graph original UV0 contract differs", "anisotropy graph texture");
    }
}
inline MaterialPreparedProgram PrepareOriginalAnisotropyProgram(const MaterialOriginalOwnerSource& owner,
                                                                const FMaterialShaderReference& selected,
                                                                const MaterialOriginalArchiveAccess& archive)
{
    if (!archive.MapIndices || !archive.Payload || !archive.Frequency)
        throw Refused("anisotropy archive access incomplete", "anisotropy original archive");
    const auto ref = CanonicalOriginalMaterialReference(owner, selected);
    const auto index =
        ArchiveIndexOf(archive.MapIndices(owner.Map.ResourceHash), ref,
                       "anisotropy resource index is outside archive map", "anisotropy original association");
    auto program =
        PrepareOriginalMaterialPixelProgram(owner.Map, ref, archive.Payload(index), archive.Frequency(index));
    // STORAGE is metadata, not an executable read. Keep the source indices used
    // by the existing contract; reject every unreviewed declaration form.
    static const std::regex storage("^STORAGE sbo_buf[0-9]+\\[\\] = \\{ program\\.storage\\[[0-9]+\\] \\}$");
    std::vector<std::string> instructions;
    for (const auto& line : program.Lines)
    {
        if (starts(line, "STORAGE "))
        {
            if (!std::regex_match(line, storage))
                throw Refused("anisotropy storage declaration differs", "anisotropy source declaration");
        }
        else
            instructions.push_back(line);
    }
    program.Lines = std::move(instructions);
    program.BaseBinding.Collections = owner.User.Collections;
    ApplyOriginalMaterialFixedTextures(program.BaseBinding, owner.Fixed);
    return program;
}
inline Json CertifyOriginalAnisotropyAuxiliary(const MaterialOriginalOwnerSource& owner,
                                               const MaterialPreparedProgram& program,
                                               const MaterialOriginalArchiveAccess& archive)
{
    constexpr std::uint64_t pixel = EngineHash::AnisotropyPixelShader, vertex = EngineHash::AnisotropyVertexShader;
    const auto& ref = program.Reference;
    if (ref.TypeHash != pixel || ref.Frequency != 3 || ref.Permutation != 0 ||
        (ref.VertexFactoryHash != EngineHash::LocalVertexFactory &&
         ref.VertexFactoryHash != EngineHash::GpuSkinVertexFactory))
        throw Refused("anisotropy auxiliary class/factory/permutation differs", "anisotropy auxiliary identity");
    const auto& header = program.Header;
    if (header.layout_hashes.size() != 2 || header.layout_hashes[0] != EngineHash::ViewLayout || header.inout_mask != 1)
        throw Refused("anisotropy auxiliary layout/output differs", "anisotropy auxiliary layout");
    if (program.BaseBinding.Buffers != std::map<int, std::string>{{0, "View"}, {1, "Material"}} ||
        program.BaseBinding.Varyings !=
            std::map<std::string, int>{{"in_texcoord10", 0}, {"in_texcoord11", 1}, {"in_texcoord0", 2}})
        throw Refused("anisotropy auxiliary input interface differs", "anisotropy auxiliary interface");
    // The pass samples the normal map and, where the tangent comes from a texture, that tangent map: one or two
    // texture parameters, each sampled with its own sampler.
    const bool tangentMap = program.BaseBinding.TextureParameters.size() == 2;
    std::set<std::string> textures, samplers;
    for (const auto& texture : program.BaseBinding.TextureParameters)
        textures.insert(texture.second);
    for (const auto& sampler : program.SamplerParameters)
        samplers.insert(sampler.second);
    if (textures.empty() || textures.size() > 2 || textures.size() != program.BaseBinding.TextureParameters.size() ||
        samplers != textures || program.SamplerParameters.size() != textures.size())
        throw Refused("anisotropy auxiliary texture/sampler identity differs", "anisotropy auxiliary resource");
    const auto textureTable = CustomMaterial::ReadResourceTable(header.textures),
               samplerTable = CustomMaterial::ReadResourceTable(header.samplers);
    const auto textureRow = textureTable.find(1), samplerRow = samplerTable.find(1);
    const std::vector<std::pair<unsigned, unsigned>> expectedTextures =
                                                         tangentMap
                                                             ? std::vector<std::pair<unsigned, unsigned>>{{0, 1},
                                                                                                          {6, 0}}
                                                             : std::vector<std::pair<unsigned, unsigned>>{{0, 0}},
                                                     expectedSamplers =
                                                         tangentMap
                                                             ? std::vector<std::pair<unsigned, unsigned>>{{1, 0},
                                                                                                          {7, 1}}
                                                             : std::vector<std::pair<unsigned, unsigned>>{{1, 0}};
    if (textureRow == textureTable.end() || samplerRow == samplerTable.end() ||
        textureRow->second != expectedTextures || samplerRow->second != expectedSamplers)
        throw Refused("anisotropy auxiliary resource ordinals/slots differ", "anisotropy auxiliary resource");
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    auto vertices = Json::MakeArray();
    for (const auto& candidate : owner.References)
        if (candidate.TypeHash == vertex && candidate.VertexFactoryHash == ref.VertexFactoryHash)
        {
            const auto actual = CanonicalOriginalMaterialReference(owner, candidate);
            if (actual.Frequency != 0 || actual.Permutation != 0 || actual.ResourceIndex < 0 ||
                std::size_t(actual.ResourceIndex) >= indices.size())
                throw Refused("anisotropy vertex association/permutation differs", "anisotropy vertex association");
            const auto index = indices[std::size_t(actual.ResourceIndex)];
            if (archive.Frequency(index) != 0)
                throw Refused("anisotropy vertex archive frequency differs", "anisotropy vertex association");
            const auto payload = archive.Payload(index);
            const auto outputs = ReadOriginalAnisotropyVertexOutputs(payload);
            std::map<std::string, int> names;
            for (const auto& output : outputs)
            {
                const auto name = output.name.substr(0, output.name.find("_centroid"));
                const auto prior = names.find(name);
                if (prior != names.end() && prior->second != output.location)
                    throw Refused("anisotropy vertex varying alias differs", "anisotropy vertex interface");
                names[name] = output.location;
            }
            for (const auto& input : program.BaseBinding.Varyings)
            {
                const auto output = names.find("out_" + input.first.substr(3));
                if (output == names.end() || output->second != input.second)
                    throw Refused("anisotropy vertex does not supply pixel input", "anisotropy vertex interface");
            }
            vertices.array.push_back(Object({{"shader", double(index)},
                                             {"type_hash", OriginalAnisotropyHashText(actual.TypeHash)},
                                             {"factory_hash", OriginalAnisotropyHashText(actual.VertexFactoryHash)},
                                             {"resource_index", actual.ResourceIndex},
                                             {"payload_sha256", PayloadDigest(payload)}}));
        }
    if (vertices.array.size() != 1)
        throw Refused("anisotropy auxiliary has no unique vertex caller", "anisotropy vertex association");
    return vertices;
}
inline MaterialOriginalAnisotropyRecovery
RecoverOriginalMaterialAnisotropy(const MaterialOriginalOwnerSource& owner,
                                  const MaterialOriginalArchiveAccess& archive, const Json& primaryGraph)
{
    if (!archive.MapIndices || !archive.Payload || !archive.Frequency)
        throw Refused("anisotropy archive access incomplete", "anisotropy original archive");
    if (!OriginalAnisotropyFamily(owner))
        throw Refused("original owner is outside the anisotropy source families", "anisotropy source family");
    if (primaryGraph.At("outputs").Has("Anisotropy") || primaryGraph.At("outputs").Has("Tangent"))
        throw Refused("anisotropy pins already exist before auxiliary recovery", "anisotropy existing graph pins");
    RequireKind(primaryGraph.At("nodes"), Json::Object, "anisotropy graph nodes");
    const auto defaults = OriginalMaterialGraphDefaults(owner);
    std::vector<FMaterialShaderReference> auxiliary;
    std::map<std::pair<std::uint64_t, int>, std::vector<FMaterialShaderReference>> bases;
    for (const auto& ref : owner.References)
    {
        if (ref.Frequency != 3)
            continue;
        if (ref.TypeHash == EngineHash::AnisotropyPixelShader)
            auxiliary.push_back(ref);
        else if (IsBasePassShaderName(MaterialOriginalSourceShaderName(ref.TypeHash)))
            bases[{ref.VertexFactoryHash, ref.ResourceIndex}].push_back(ref);
    }
    if (auxiliary.size() != 2 ||
        std::set<std::uint64_t>{auxiliary[0].VertexFactoryHash, auxiliary[1].VertexFactoryHash} !=
            std::set<std::uint64_t>{EngineHash::LocalVertexFactory, EngineHash::GpuSkinVertexFactory})
        throw Refused("anisotropy family lacks the two actual auxiliary factories", "anisotropy auxiliary coverage");
    MaterialOriginalAnisotropyRecovery result;
    auto auxiliaryFacts = Json::MakeArray(), baseFacts = Json::MakeArray();
    Json candidate;
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    for (const auto& ref : auxiliary)
    {
        auto program = PrepareOriginalAnisotropyProgram(owner, ref, archive);
        auto vertices = CertifyOriginalAnisotropyAuxiliary(owner, program, archive);
        const auto inputs = RecoverOriginalAnisotropyInputs(program);
        const auto graph = OriginalAnisotropyPinGraph(inputs, defaults);
        if (candidate.kind != Json::Null && candidate.Dump() != graph.Dump())
            throw Refused("anisotropy factories disagree on raw material inputs", "anisotropy factory equality");
        candidate = graph;
        const auto index = indices.at(std::size_t(program.Reference.ResourceIndex));
        auxiliaryFacts.array.push_back(
            Object({{"shader", double(index)},
                    {"type_hash", OriginalAnisotropyHashText(program.Reference.TypeHash)},
                    {"factory_hash", OriginalAnisotropyHashText(program.Reference.VertexFactoryHash)},
                    {"resource_index", program.Reference.ResourceIndex},
                    {"factory", MaterialFactoryName(program.Reference.VertexFactoryHash)},
                    {"payload_sha256", PayloadDigest(archive.Payload(index))},
                    {"tangent_world_line", double(inputs.Projection.Line)},
                    {"normal_input_line", double(inputs.Projection.RawLine)},
                    {"anisotropy_line", double(inputs.Scalar.Line)},
                    {"vertices", vertices}}));
        result.Programs.push_back(std::move(program));
    }
    std::size_t local = 0, skin = 0;
    for (const auto& group : bases)
    {
        const auto& first = group.second.front();
        if (first.VertexFactoryHash == EngineHash::LocalVertexFactory)
            ++local;
        else if (first.VertexFactoryHash == EngineHash::GpuSkinVertexFactory)
            ++skin;
        else
            throw Refused("anisotropy base pass factory differs", "anisotropy base coverage");
        for (const auto& alias : group.second)
            if (alias.UniformBufferHashes != first.UniformBufferHashes || alias.NamedBuffers != first.NamedBuffers)
                throw Refused("anisotropy base aliases disagree on original input ABI", "anisotropy base aliases");
        auto program = PrepareOriginalAnisotropyProgram(owner, first, archive);
        const auto inputs = RecoverOriginalAnisotropyInputs(program);
        const auto code = ParseMaterialAnchorInstructions(program.Lines);
        const auto index = indices.at(std::size_t(program.Reference.ResourceIndex));
        auto aliases = Json::MakeArray();
        for (const auto& alias : group.second)
            aliases.array.push_back(Object({{"type_hash", OriginalAnisotropyHashText(alias.TypeHash)},
                                            {"factory_hash", OriginalAnisotropyHashText(alias.VertexFactoryHash)},
                                            {"resource_index", alias.ResourceIndex},
                                            {"permutation", alias.Permutation},
                                            {"type", MaterialOriginalSourceShaderName(alias.TypeHash)}}));
        baseFacts.array.push_back(Object(
            {{"shader", double(index)},
             {"type_hash", OriginalAnisotropyHashText(first.TypeHash)},
             {"factory_hash", OriginalAnisotropyHashText(first.VertexFactoryHash)},
             {"factory", MaterialFactoryName(first.VertexFactoryHash)},
             {"resource_index", first.ResourceIndex},
             {"payload_sha256", PayloadDigest(archive.Payload(index))},
             {"tangent_world_line", double(inputs.Projection.Line)},
             {"normal_input_line", double(inputs.Projection.RawLine)},
             {"anisotropy_line", double(inputs.Scalar.Line)},
             {"tangent_consumer_line",
              double(OriginalAnisotropyConsumer(code, inputs.Projection.Register, "xyz", inputs.Projection.Line))},
             {"anisotropy_consumer_line",
              double(OriginalAnisotropyConsumer(code, inputs.Scalar.Register, inputs.Scalar.Components,
                                                inputs.Scalar.Line))},
             {"aliases", aliases}}));
        result.Programs.push_back(std::move(program));
    }
    if (bases.size() != 12 || local != 8 || skin != 4)
        throw Refused("anisotropy original eight local/four skin base consumers differ", "anisotropy base coverage");
    result.Graph = primaryGraph;
    std::map<std::string, std::string> rename;
    for (const auto& node : candidate.At("nodes").object)
    {
        const auto name = "AuxAnisotropy_" + node.first;
        if (primaryGraph.At("nodes").Has(name))
            throw Refused("anisotropy auxiliary node identity collides", "anisotropy graph collision");
        rename[node.first] = name;
    }
    for (const auto& entry : candidate.At("nodes").object)
    {
        auto node = entry.second;
        for (auto& input : node["inputs"].object)
            input.second.array.at(0) = rename.at(input.second.array.at(0).Text());
        result.Graph["nodes"][rename.at(entry.first)] = node;
    }
    for (const auto& entry : candidate.At("outputs").object)
    {
        auto link = entry.second;
        link.array.at(0) = rename.at(link.array.at(0).Text());
        result.Graph["outputs"][entry.first] = link;
    }
    result.Facts = Object({{"version", 1},
                           {"source_family", MaterialSourcePackage(owner.User.Chain.front().Package)},
                           {"resource_hash", CustomMaterial::Hex(CustomMaterial::Bytes(owner.Map.ResourceHash.begin(),
                                                                                       owner.Map.ResourceHash.end()))},
                           {"pins", Array({"Anisotropy", "Tangent"})},
                           {"stages", auxiliaryFacts},
                           {"basepass_stages", baseFacts},
                           {"normal_coupled", true},
                           {"basepass_delegated", true},
                           {"raw_tangent_magnitude_authored", false},
                           {"compiler_proven", false},
                           {"family_complete", false},
                           {"renderer_proven", false},
                           {"production_admitted", false}});
    ValidateGraph(result.Graph);
    std::set<std::string> passTextures;
    for (const auto& program : result.Programs)
        for (const auto& texture : program.BaseBinding.TextureParameters)
            passTextures.insert(texture.second);
    CertifyOriginalAnisotropyGraphTextures(result.Graph, passTextures);
    return result;
}
}
} // P4MapKit::Material

// Interpret particle pixel inputs only after associated original vertex output
// structs agree. Numeric varying slots alone never establish SubUV semantics.
#pragma once
#include "material_source_particle.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialParticleArchive
{
    std::function<std::vector<std::uint32_t>(const std::array<std::uint8_t, 20>&)> MapIndices;
    std::function<CustomMaterial::Bytes(std::uint32_t)> Payload;
    std::function<std::uint8_t(std::uint32_t)> Frequency;
};
inline bool MaterialParticleBasePassVertexType(std::uint64_t hash)
{
    return hash == 0xe99e2d4408e39e36ull || hash == 0x65b5d8cde5c1f164ull || hash == 0x1a4855b2c0466f87ull ||
           hash == 0xba53fb0a20785ffCull || hash == 0xe6b842b7733dcb47ull;
}
inline CustomMaterial::ShaderBindings ReadOriginalParticleVertexBindings(const CustomMaterial::Bytes& payload)
{
    if (payload.size() < 6 || payload.size() > (1u << 20) || CustomMaterial::Read32(payload, 0) != 0x474c534c ||
        payload[4] != 0x53 || payload[5] != 0x56)
        throw Refused("original particle vertex payload marker mismatch", "material particle vertex payload");
    // The stock GL header and optional tail grammar are shared by VS and PS.
    // Use the qualified bounded structural parser with its PS marker locally.
    auto structural = payload;
    structural[5] = 0x50;
    try
    {
        return CustomMaterial::ReadPixelBindings(structural);
    }
    catch (const std::exception& e)
    {
        throw Refused(e.what(), "material particle vertex payload");
    }
}
inline std::map<std::string, int> MaterialParticleVaryingNames(const std::vector<CustomMaterial::Varying>& values)
{
    std::map<std::string, int> result;
    for (const auto& value : values)
    {
        const auto name = value.name.substr(0, value.name.find("_centroid"));
        const auto found = result.find(name);
        if (found != result.end() && found->second != value.location)
            throw Refused("particle varying semantic has conflicting locations", "material particle varying identity");
        result[name] = value.location;
    }
    return result;
}
inline std::array<std::string, 4> OriginalParticleVertexWrittenComponents(const std::vector<std::string>& lines,
                                                                          int index)
{
    if (index < 0 || index > 127 || lines.size() > 100000)
        throw Refused("particle vertex output or program exceeds its bound", "material particle vertex output");
    std::array<std::string, 4> result{{}};
    std::map<std::pair<std::string, char>, std::size_t> versions;
    int depth = 0;
    const std::regex exact("^(\\S+) result\\.attrib\\[" + std::to_string(index) + "\\](?:\\.([xyzw]+))?, (.+)$"),
        write("^\\S+ (R[0-9]+)(?:\\.([xyzw]+))?(?:\\([^)]*\\))?,"),
        output("^\\S+ result\\.attrib\\[" + std::to_string(index) + "\\](?:\\.([xyzw]+))?"),
        sourceRegister("^(R[0-9]+)(?:\\.([xyzw]+))?$");
    for (const auto& line : lines)
    {
        if (line.size() > 65536)
            throw Refused("particle vertex line exceeds bound", "material particle program budget");
        if (starts(line, "IF "))
            ++depth;
        else if (line == "ENDIF")
            --depth;
        std::smatch m;
        if (std::regex_search(line, m, write))
            for (const char c : m[2].matched ? m[2].str() : "xyzw")
                ++versions[{m[1].str(), c}];
        if (!std::regex_match(line, m, exact))
        {
            if (std::regex_search(line, m, output))
                for (const char c : m[1].matched ? m[1].str() : "xyzw")
                    result[std::string("xyzw").find(c)].clear();
            continue;
        }
        const std::string operation = m[1], mask = m[2].matched ? m[2].str() : "xyzw", operand = m[3];
        const auto clear = [&]()
        {
            for (const auto c : mask)
                result[std::string("xyzw").find(c)].clear();
        };
        if (depth || !starts(operation, "MOV.") || operand.find(',') != std::string::npos ||
            line.find('(') != std::string::npos || operation.find(".SAT") != std::string::npos ||
            !std::regex_match(operand, m, sourceRegister))
        {
            clear();
            continue;
        }
        const std::string reg = m[1];
        std::string swizzle = m[2].matched ? m[2].str() : "xyzw";
        if (swizzle.size() > 4)
        {
            clear();
            continue;
        }
        while (swizzle.size() < 4)
            swizzle += swizzle.back();
        for (const auto c : mask)
        {
            const auto k = std::string("xyzw").find(c);
            result[k] = reg + "." + swizzle[k] + "#" + std::to_string(versions[{reg, swizzle[k]}]);
        }
    }
    return result;
}
struct MaterialParticleSubUVSource : MaterialEvidence
{
    std::string Factory;
    bool SubUV = false, UV0Alias = false, GpuDynamicZero = false, TangentOutputsChecked = false,
         VertexFogChecked = false;
    std::vector<FMaterialShaderReference> VertexReferences;
};
inline MaterialParticleSubUVSource PrepareOriginalParticleSubUVSource(const FMaterialShaderMapEnvelope& map,
                                                                      const MaterialPreparedProgram& pixel,
                                                                      const MaterialParticleArchive& archive)
{
    FReadError error;
    FMaterialShaderParameters checked;
    if (pixel.Reference.Frequency != 3 || !ReadMaterialShaderParameters(map, pixel.Reference, checked, error))
        throw Refused(error.Code.empty() ? "particle source is not an original pixel reference" : error.Code,
                      "material particle pixel association");
    MaterialParticleSubUVSource result;
    result.Factory = MaterialFactoryName(pixel.Reference.VertexFactoryHash);
    if (!MaterialParticleFactory(result.Factory))
        return result;
    if (!archive.MapIndices || !archive.Payload || !archive.Frequency)
        throw Refused("particle original archive callbacks are incomplete", "material particle archive");
    std::vector<FMaterialShaderReference> refs;
    if (!ReadMaterialShaderReferences(map, refs, error))
        throw Refused(error.Code, "material particle shader map");
    const auto indices = archive.MapIndices(map.ResourceHash);
    if (indices.size() > 1000000)
        throw Refused("particle archive map exceeds its bound", "material particle archive");
    const auto inputs = MaterialParticleVaryingNames(pixel.Header.inputs);
    const bool gpu = result.Factory == "FGPUSpriteVertexFactory",
               beam = result.Factory == "FParticleBeamTrailVertexFactory",
               mesh = result.Factory == "FMeshParticleVertexFactory";
    const std::string tangentX = gpu ? "in_tangentx" : "in_texcoord10",
                      tangentZ = gpu ? "in_tangentz" : "in_texcoord11";
    std::set<std::string> required;
    if (inputs.count(tangentX))
        required.insert("out_" + tangentX.substr(3));
    if (inputs.count(tangentZ))
        required.insert("out_" + tangentZ.substr(3));
    if (inputs.count("in_texcoord7"))
        required.insert("out_texcoord7");
    bool first = true;
    for (const auto& ref : refs)
    {
        if (ref.Frequency != 0 || ref.VertexFactoryHash != pixel.Reference.VertexFactoryHash ||
            !MaterialParticleBasePassVertexType(ref.TypeHash))
            continue;
        if (ref.ResourceIndex < 0 || std::size_t(ref.ResourceIndex) >= indices.size())
            throw Refused("particle vertex ref exceeds original archive map", "material particle vertex association");
        const auto archiveIndex = indices[std::size_t(ref.ResourceIndex)];
        if (archive.Frequency(archiveIndex) != 0)
            throw Refused("particle vertex archive frequency disagrees with map",
                          "material particle vertex association");
        FMaterialShaderParameters parameters;
        if (!ReadMaterialShaderParameters(map, ref, parameters, error))
            throw Refused(error.Code, "material particle vertex reference");
        const auto payload = archive.Payload(archiveIndex);
        const auto header = ReadOriginalParticleVertexBindings(payload);
        const auto outputs = MaterialParticleVaryingNames(header.outputs);
        for (const auto& name : required)
            if (!outputs.count(name))
                throw Refused("native particle VS does not expose the consumed tangent/fog semantic: " + name,
                              "material particle vertex tangent frame");
        bool subuv = false, alias = false, zero = false;
        if (mesh)
            subuv = outputs.count("out_texcoord1") && outputs.count("out_texcoord2") && !outputs.count("out_texcoord0");
        else if (!beam)
        {
            subuv = outputs.count("out_particle_subuvs") != 0;
            const auto uv = outputs.find(gpu ? "out_texcoord0" : "out_texcoord3");
            if (subuv && uv != outputs.end())
            {
                const auto lines = MaterialInstructionLines(ReadOriginalMaterialProgramText(payload, header));
                const auto a = OriginalParticleVertexWrittenComponents(lines, uv->second),
                           b = OriginalParticleVertexWrittenComponents(lines, outputs.at("out_particle_subuvs"));
                alias = !a[0].empty() && !a[1].empty() && a[0] == b[0] && a[1] == b[1];
            }
            zero = gpu && !outputs.count("out_dynamic_parameter") && !outputs.count("out_color2") &&
                   !outputs.count("out_texcoord1");
        }
        if (!first && (result.SubUV != subuv || result.UV0Alias != alias || result.GpuDynamicZero != zero))
            throw Refused("particle vertex SubUV contracts disagree between actual policies",
                          "material particle vertex SubUV disagreement");
        first = false;
        result.SubUV = subuv;
        result.UV0Alias = alias;
        result.GpuDynamicZero = zero;
        result.VertexReferences.push_back(ref);
    }
    // Beam has no SubUV constructor assignment. It only needs native VS proof
    // when the selected PS actually consumes tangent or fog outputs.
    if (first && (!beam || !required.empty()))
        throw Refused("particle vertex SubUV contracts are missing", "material particle vertex SubUV missing");
    result.TangentOutputsChecked = inputs.count(tangentX) || inputs.count(tangentZ);
    result.VertexFogChecked = inputs.count("in_texcoord7") != 0;
    return result;
}
inline std::map<std::string, Vec> OriginalParticleSourceInterpolants(SymbolicArena& arena,
                                                                     const MaterialParticleSubUVSource& sourceContract)
{
    const auto& factory = sourceContract.Factory;
    auto result = MaterialFactoryInputContract(arena, factory).Interpolants;
    if (!MaterialParticleFactory(factory))
        return result;
    const auto row = [&](const std::string& kind, const Value& detail, int count)
    {
        Vec out = splat(0.0);
        for (int c = 0; c < count; ++c)
            out[c] = source(arena, kind, detail, c);
        return out;
    };
    const bool gpu = factory == "FGPUSpriteVertexFactory";
    const std::string tx = gpu ? "in_tangentx" : "in_texcoord10", tz = gpu ? "in_tangentz" : "in_texcoord11";
    result[tx] = row("TangentX", Value::null(), 3);
    auto z = row("TangentZ", Value::null(), 3);
    z[3] = source(arena, "TangentSign");
    result[tz] = z;
    auto wp = row("TranslatedWorldPositionNoOffsets", Value::null(), 3);
    wp[3] = 1.;
    result["in_texcoord9"] = wp;
    if (sourceContract.SubUV && factory == "FMeshParticleVertexFactory")
    {
        result["in_texcoord1"] = {{source(arena, "ParticleSubUVProperty", 0, 0),
                                   source(arena, "ParticleSubUVProperty", 0, 1), source(arena, "TexCoord", 0, 0),
                                   source(arena, "TexCoord", 0, 1)}};
        result["in_texcoord2"] = {
            {source(arena, "ParticleSubUVProperty", 1, 0), source(arena, "ParticleSubUVProperty", 1, 1),
             source(arena, "ParticleSubUVProperty", 2, 0), source(arena, "ParticleSubUVProperty", 2, 0)}};
    }
    if (sourceContract.SubUV && IsMember(factory, {"FParticleSpriteVertexFactory", "FGPUSpriteVertexFactory"}))
    {
        result["in_particle_subuvs"] = {
            {source(arena, "ParticleSubUVProperty", 0, 0), source(arena, "ParticleSubUVProperty", 0, 1),
             source(arena, "ParticleSubUVProperty", 1, 0), source(arena, "ParticleSubUVProperty", 1, 1)}};
        if (gpu)
            result["in_texcoord4"] = {
                {source(arena, "ParticleSubUVProperty", 2, 0), source(arena, "Interpolant", "in_texcoord4", 0),
                 source(arena, "Interpolant", "in_texcoord4", 1), source(arena, "Interpolant", "in_texcoord4", 2)}};
        else
            result["in_texcoord10"][3] = source(arena, "ParticleSubUVProperty", 2, 0);
    }
    if (factory == "FMeshParticleVertexFactory")
        result["in_color2"] = row("ParticleDynamicParameter", 0, 4);
    else if (factory == "FParticleSpriteVertexFactory")
        result["in_texcoord1"] = row("ParticleDynamicParameter", 0, 4);
    else if (factory == "FParticleBeamTrailVertexFactory")
        result["in_texcoord4"] = row("ParticleDynamicParameter", 0, 4);
    return result;
}
inline void InstallOriginalParticleSourceInterpolants(SymbolicArena& arena, BindingContext& binding,
                                                      const MaterialParticleSubUVSource& contract)
{
    const auto expected = MaterialFactoryInputContract(arena, contract.Factory).Interpolants;
    for (const auto& entry : binding.Interpolants)
    {
        const auto known = expected.find(entry.first);
        if (known == expected.end())
            throw Refused("particle inputs already have an independent mapping",
                          "material particle interpolant conflict");
        for (std::size_t c = 0; c < 4; ++c)
            if (!same(entry.second[c], known->second[c]))
                throw Refused("particle inputs conflict with factory input contract",
                              "material particle interpolant conflict");
    }
    binding.Interpolants = OriginalParticleSourceInterpolants(arena, contract);
}
}
} // P4MapKit::Material

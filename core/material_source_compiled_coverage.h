// Compiled mask coverage: a masked lit owner whose retained programs, in every stage, hold no discard, no sample mask
// and no clip distance or coverage output covers every pixel it draws at its shipped clip. Its compiled mask is the
// constant 1, whatever authored OpacityMask the compiler erased (material_hair_coverage.py checks the same absence).
// Only the shipped cooked clip and the shipped permutations are covered; an edited clip or an uncompiled static
// permutation is not.
#pragma once
#include "material_source_recovery.h"
#include "material_source_common.h"
#include <cctype>
namespace P4MapKit
{
namespace Material
{
// One retained program of any stage: the names of its declared outputs and its assembly text.
struct MaterialRetainedStageProgram
{
    std::vector<std::string> Outputs;
    std::string Text;
};
// Every stage's payload starts with the same binding header (the stage marker differs), then the code and control
// blocks; the control block holds the program's assembly text.
inline MaterialRetainedStageProgram ReadOriginalRetainedStageProgram(const CustomMaterial::Bytes& payload)
{
    const char* name = "compiled mask coverage";
    if (payload.size() < 10 || payload.size() > (1u << 20))
        throw Refused("retained program payload is outside its bound", name);
    MaterialRetainedStageProgram result;
    std::string failure;
    std::size_t at = 0;
    try
    {
        CustomMaterial::BindingReader reader(payload);
        if (reader.u32() != 0x474c534c)
            throw std::runtime_error("retained program header marker differs");
        reader.u16();
        const auto buffers = reader.count(32);
        for (unsigned i = 0; i < buffers; ++i)
            reader.packed();
        reader.packed();
        reader.varyings();
        for (const auto& output : reader.varyings())
            result.Outputs.push_back(output.name);
        reader.u32();
        for (int table = 0; table < 5; ++table)
            reader.words();
        reader.u16();
        for (int count = 0; count < 5; ++count)
            reader.u8();
        reader.u32();
        reader.u32();
        reader.u32();
        at = reader.offset();
    }
    catch (const std::exception& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, name);
    const auto require = [&](std::size_t n)
    {
        if (at > payload.size() || n > payload.size() - at)
            throw Refused("retained program payload is truncated", name);
    };
    require(4);
    const auto codeSize = CustomMaterial::Read32(payload, at);
    at += 4;
    require(codeSize);
    at += codeSize;
    require(4);
    const auto controlSize = CustomMaterial::Read32(payload, at);
    at += 4;
    require(controlSize);
    if (controlSize < 28)
        throw Refused("retained program control header is truncated", name);
    const auto textAt = CustomMaterial::Read32(payload, at + 20), textSize = CustomMaterial::Read32(payload, at + 24);
    if (textAt < 28 || textAt > controlSize || textSize > controlSize - textAt || textSize > 1024u * 1024u)
        throw Refused("retained program text is outside its control block", name);
    const auto begin = payload.begin() + at + textAt, end = begin + textSize;
    result.Text.assign(begin, std::find(begin, end, std::uint8_t(0)));
    return result;
}
// True when a declared output or assembly token names a clip distance, coverage or sample mask.
inline bool MaterialCoverageOutputName(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text.find("clip") != std::string::npos || text.find("coverage") != std::string::npos ||
           text.find("samplemask") != std::string::npos;
}
// True when a program's assembly text holds a discard or writes a clip distance, coverage or sample mask.
inline bool MaterialProgramLimitsCoverage(const MaterialRetainedStageProgram& program)
{
    for (const auto& output : program.Outputs)
        if (MaterialCoverageOutputName(output))
            return true;
    std::istringstream lines(program.Text);
    std::string line;
    while (std::getline(lines, line))
    {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        const auto opcode = line.substr(first, line.find_first_of(" .;\t", first) - first);
        if (opcode == "KIL")
            return true;
        std::size_t at = 0;
        while ((at = line.find("result.", at)) != std::string::npos)
        {
            const auto stop = line.find_first_of(" ,;[}", at);
            if (MaterialCoverageOutputName(line.substr(at, stop == std::string::npos ? std::string::npos : stop - at)))
                return true;
            at += 7;
        }
    }
    return false;
}
// The certificate: false for an owner it does not apply to (not a masked lit surface, or a retained program limits
// coverage); a refusal when the owner's shader map cannot be read whole.
inline bool CertifyOriginalCompiledCoverage(const MaterialOriginalOwnerSource& owner,
                                            const MaterialOriginalArchiveAccess& archive)
{
    const auto& cooked = owner.User.Cooked;
    if (cooked.Domain != "MD_Surface" || cooked.BlendMode != "BLEND_Masked" || cooked.ShadingModel == "MSM_Unlit" ||
        owner.References.empty())
        return false;
    for (const auto& item : owner.User.Chain)
    {
        const auto* field = item.Properties.Field("bUseAlphaToCoverage");
        if (field && (field->Kind != FDecodedValue::EKind::Boolean || field->Text != "false"))
            return false;
    }
    RequireOriginalArchiveAccess(archive);
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    std::set<std::uint32_t> read;
    for (const auto& ref : owner.References)
    {
        const auto index =
            ArchiveIndexOf(indices, ref, "compiled coverage resource is outside its map", "compiled mask coverage");
        if (archive.Frequency(index) != ref.Frequency)
            throw Refused("compiled coverage archive stage differs from its reference", "compiled mask coverage");
        if (!read.insert(index).second)
            continue;
        if (MaterialProgramLimitsCoverage(ReadOriginalRetainedStageProgram(archive.Payload(index))))
            return false;
    }
    return true;
}
// The certified owner's mask: the constant 1 at its shipped clip.
inline MaterialDepthMaskInputs OriginalCompiledCoverageInputs(const MaterialRecoveryPlan& plan)
{
    MaterialDepthMaskInputs out;
    out.Clip = plan.Settings.At("clip").Numeric();
    out.Bound.Arena = std::make_shared<SymbolicArena>();
    out.Bound.Pins = {"OpacityMask"};
    out.Bound.Values = {{1.}};
    return out;
}
}
} // P4MapKit::Material

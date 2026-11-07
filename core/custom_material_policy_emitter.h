// Native first-party forward policy stages. Complete lighting admission remains
// governed by custom_material_support.h and separate original/game proof.
#pragma once
#include "custom_material_lighting.h"
#include "custom_material_policy_bindings.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct ReservedDiagnosticBuffer
{
    unsigned index;
    bool original_program_absent;
};
inline void ValidateDiagnosticBuffer(const ShaderBindings& header, const ReservedDiagnosticBuffer& input)
{
    if (input.index > 14 || input.index == 14 || !input.original_program_absent ||
        (header.resource_bits & (1u << input.index)) ||
        (input.index < header.layout_hashes.size() && header.layout_hashes[input.index] != 0))
        throw std::runtime_error("diagnostic input uniform buffer unqualified");
}
struct PolicyEmitter
{
    Emitter& emitter;
    PolicyBindings bindings;
    std::set<unsigned> buffers;
    explicit PolicyEmitter(Emitter& output, const ShaderBindings& header)
        : emitter(output), bindings(ReadForwardPolicyBindings(header))
    {
    }
    PolicyEmitter(Emitter& output, PolicyBindings roles) : emitter(output), bindings(std::move(roles))
    {
    }
    Value Load(unsigned buffer, unsigned offset, int size)
    {
        if (buffer > 14 || offset % 4 || size < 1 || size > 4)
            throw std::runtime_error("policy uniform load range");
        buffers.insert(buffer);
        auto reg = emitter.temp();
        emitter.emit("LDC." +
                     std::string(size == 1   ? "F32"
                                 : size == 2 ? "F32X2"
                                             : "F32X4") +
                     " " + reg + "." + Comp(size) + ", buf" + std::to_string(buffer) + "[" + std::to_string(offset) +
                     "]");
        return {reg, size};
    }
    Value SampleIntegratedFog(Value coordinates)
    {
        if (coordinates.size != 3)
            throw std::runtime_error("volumetric fog coordinates require xyz");
        auto texture = bindings.RequireTexture("IntegratedFog"),
             sampler = bindings.RequireSampler("SharedBilinearClamped");
        emitter.uses_texture = true;
        emitter.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * sampler) + "]");
        emitter.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * texture) + "]");
        emitter.emit("OR.U64 D0.x, D0, D1");
        auto uv = emitter.append(coordinates, F32({0}));
        auto sample = emitter.temp();
        emitter.emit("TXL.F " + sample + ", " + uv.text + ", handle(D0.x), 3D");
        // Creator texture emission keeps its own cached handle identity.
        emitter.handle.clear();
        return {sample, 4};
    }
    Value ApplyVolumetricFog(Value worldPosition, Value vertexFog)
    {
        if (worldPosition.size != 3 || vertexFog.size != 4)
            throw std::runtime_error("volumetric fog stage input shape");
        auto apply = Load(2, 1836, 1), condition = emitter.op("SGT", 1, {apply, F32({0})});
        auto result = emitter.temp(), conditionRegister = emitter.temp();
        emitter.emit("MOV.F32 " + result + ", " + vertexFog.text);
        emitter.emit("TRUNC.U.CC " + conditionRegister + ".x, " + Emitter::src(condition, 1));
        emitter.emit("IF NE.x");
        auto row0 = Load(0, 64, 4), row1 = Load(0, 80, 4), row2 = Load(0, 96, 4), row3 = Load(0, 112, 4),
             depth = Load(0, 3840, 3), scale = Load(0, 3832, 1);
        auto coordinates = VolumetricFogCoordinates(emitter, worldPosition, row0, row1, row2, row3, depth, scale);
        auto volume = SampleIntegratedFog(coordinates), fog = ComposeVolumetricFog(emitter, vertexFog, volume);
        emitter.emit("MOV.F32 " + result + ", " + fog.text);
        emitter.emit("ENDIF");
        return {result, 4};
    }
    std::string Program() const
    {
        auto text = emitter.program();
        auto position = text.find("CBUFFER buf0[] = { program.buffer[0] };\n");
        if (position == std::string::npos)
            throw std::runtime_error("policy emitter declaration boundary missing");
        position += std::string("CBUFFER buf0[] = { program.buffer[0] };\n").size();
        std::string declarations;
        for (auto buffer : buffers)
            if (buffer != 0 && buffer != 14 && !(buffer == 1 && emitter.uses_primitive))
                declarations += "CBUFFER buf" + std::to_string(buffer) + "[] = { program.buffer[" +
                                std::to_string(buffer) + "] };\n";
        text.insert(position, declarations);
        return text;
    }
};
// Diagnostic complete fog stage with explicit input words in a reserved buffer.
// The caller proves its absence from original program declarations/references.
// qualifies stage/control/resource emission; it is not a complete material.
inline std::string FogStageProgram(const ShaderBindings& header, const ReservedDiagnosticBuffer& input)
{
    ValidateDiagnosticBuffer(header, input);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto position = policy.Load(input.index, 0, 3), vertex = policy.Load(input.index, 16, 4);
    auto fog = policy.ApplyVolumetricFog(position, vertex);
    emitter.emit("MOV.F32 result_color0, " + fog.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

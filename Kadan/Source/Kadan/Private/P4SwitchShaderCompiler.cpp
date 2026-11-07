// The compiler half of Kadan's GLSL_SWITCH shader format (KadanShaderFormat; stage 8.5 of
// tools/navigation/OWN_SHADER_MAPS.md): UE's SPIR-V to the game's program and payload, by our SPIR-V front end and
// Maxwell compiler (P4MapKitCore/own_switch_program.h). Registered as a modular feature for the format module.
#include "P4SwitchShaderCompiler.h"
#include "P4SwitchGpuFactoryParameters.h"
#include "Features/IModularFeatures.h"
#include "Misc/Crc.h"
#include "KadanSwitchShaderCompiler.h"
#include "P4MapKitCore/own_switch_program.h"
#include <exception>

namespace
{

namespace Own = P4MapKit::CustomMaterial;

FString Text(const std::string& S)
{
    return FString(UTF8_TO_TCHAR(S.c_str()));
}
std::string Narrow(const FString& S)
{
    return std::string(TCHAR_TO_UTF8(*S));
}

class FP4SwitchShaderCompiler final : public IKadanSwitchShaderCompiler
{
public:
    // Changes with every build of this file, which the build redoes whenever the front end, the compiler or the
    // payload writer (its headers) change: a cached program is never one of an older compiler.
    virtual uint32 Version() const override { return FCrc::StrCrc32(TEXT(__DATE__ " " __TIME__)); }

    virtual bool Translate(const TArray<uint32>& Spirv, FKadanSwitchProgram& Out, FString& Error) const override
    {
        try
        {
            const std::vector<std::uint32_t> Words(Spirv.GetData(), Spirv.GetData() + Spirv.Num());
            const Own::OwnSwitch::Program P = Own::OwnSwitch::Translate(Words);
            Out.Assembly = Text(P.assembly);
            Out.Stage = P.stage == Own::OwnPayload::Stage::Vertex ? 0
                : P.stage == Own::OwnPayload::Stage::Pixel        ? 1
                                                                  : 2;
            for (const auto& B : P.iface.blocks)
            {
                FKadanSwitchBuffer Buffer;
                Buffer.Name = Text(B.name);
                Buffer.Slot = B.slot;
                Buffer.Size = Own::OwnSwitch::BufferSize(B);
                if (B.name == "$Globals" || B.name == "$Globals.u")
                    for (const auto& M : B.members)
                        Buffer.Members.Add({Text(M.name), M.offset, M.size});
                Out.Buffers.Add(MoveTemp(Buffer));
            }
            for (const auto& T : P.iface.textures)
                Out.Textures.Add({Text(T.name), T.slot, T.target == "BUFFER"});
            for (const auto& S : P.iface.samplers)
                Out.Samplers.Add({Text(S.name), S.slot, false});
            for (const auto& S : P.iface.storage)
                Out.Storage.Add({Text(S.name), S.slot, true});
            for (const auto& V : P.inputs)
                Out.Inputs.Add({Text(V.name), V.location});
            for (const auto& V : P.outputs)
                Out.Outputs.Add({Text(V.name), V.location});
            Out.InOutMask = P.inout_mask;
            Out.DefaultSampler = P.iface.default_sampler;
            return true;
        }
        catch (const std::exception& E)
        {
            Error = FString::Printf(TEXT("Kadan's Switch compiler: %s"), *Text(E.what()));
            return false;
        }
    }

    virtual bool Payload(const FKadanSwitchProgram& Program, const FKadanSwitchBindings& Bindings, TArray<uint8>& Out,
        FString& Error) const override
    {
        try
        {
            Own::OwnPayload::Description D;
            D.stage = Program.Stage == 0 ? Own::OwnPayload::Stage::Vertex
                : Program.Stage == 1     ? Own::OwnPayload::Stage::Pixel
                                         : Own::OwnPayload::Stage::Geometry;
            for (const auto& V : Program.Inputs)
                D.inputs.push_back({Narrow(V.Name), V.Location});
            for (const auto& V : Program.Outputs)
                D.outputs.push_back({Narrow(V.Name), V.Location});
            for (int32 Slot = 0; Slot < Bindings.LayoutHashes.Num(); ++Slot)
            {
                const std::string Name = Slot < Bindings.BufferNames.Num() ? Narrow(Bindings.BufferNames[Slot])
                                                                           : std::string();
                D.layout_hashes.push_back(Own::OwnSwitch::GameLayoutHash(Name, Bindings.LayoutHashes[Slot]));
            }
            // Resource indices in the game's struct layouts (its structs have resources of their own).
            const auto Rows = [](const TArray<FKadanSwitchTableEntry>& In, std::vector<Own::OwnPayload::Resource>& Out)
            {
                for (const auto& R : In)
                    Out.push_back({R.Buffer, Own::OwnSwitch::GameResourceIndex(Narrow(R.Name), R.Resource), R.Slot});
            };
            Rows(Bindings.Textures, D.textures);
            Rows(Bindings.Samplers, D.samplers);
            Rows(Bindings.SRVs, D.srvs);
            Rows(Bindings.UAVs, D.uavs);
            D.texture_slots = int(Bindings.TextureSlots);
            D.sampler_slots = Bindings.SamplerSlots;
            D.uniform_buffers = Bindings.UniformBuffers;
            D.storage_buffers = Bindings.StorageBuffers;
            for (const auto& A : Bindings.GlobalArrays)
                D.global_arrays.push_back({A.Size, A.TypeName, A.TypeIndex});
            for (const auto& S : Bindings.PackedGlobalSlots)
                D.packed_global_slots.push_back({S.Key, S.Value});
            D.default_sampler = Program.DefaultSampler;
            D.inout_mask = Program.InOutMask;
            const auto Compiled = Own::OwnPayload::Compile(Narrow(Program.Assembly), D);
            Out.Append(Compiled.payload.data(), int32(Compiled.payload.size()));
            return true;
        }
        catch (const std::exception& E)
        {
            Error = FString::Printf(TEXT("Kadan's Switch compiler: %s"), *Text(E.what()));
            return false;
        }
    }
};

FP4SwitchShaderCompiler Compiler;
bool bRegistered = false;

} // namespace

void P4RegisterSwitchShaderCompiler()
{
    if (bRegistered)
        return;
    // ShaderTypesReady installs the GPU factory bindings after the engine registers its layouts. Keep the compiler
    // available during ordinary module startup; explicit Switch compile contexts require the installation too.
    FString DeferredError;
    (void)P4InstallSwitchGpuFactoryParameters(DeferredError); // also covers a module loaded after ShaderTypesReady
    IModularFeatures::Get().RegisterModularFeature(IKadanSwitchShaderCompiler::FeatureName(), &Compiler);
    bRegistered = true;
}

void P4UnregisterSwitchShaderCompiler()
{
    if (!bRegistered)
        return;
    IModularFeatures::Get().UnregisterModularFeature(IKadanSwitchShaderCompiler::FeatureName(), &Compiler);
    bRegistered = false;
    P4UninstallSwitchGpuFactoryParameters();
}

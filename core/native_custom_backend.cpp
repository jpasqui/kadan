#include "native_custom_backend.h"
#include "material_engine_hashes.h"
#include "KadanMaxwell/nvasm.h"
#include "KadanMaxwell/nvasm_ir.h"
#include "KadanMaxwell/nvasm_lower.h"
#include "KadanMaxwell/nvasm_program.h"
#include "custom_material_policy_bindings.h"
#include "native_custom_backend_depth.h"
#include <set>

namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
namespace {
unsigned SkyStorageSlot(const ShaderBindings& Bindings, int MaterialBuffer) {
    if (MaterialBuffer < 0) return Lighting::ReadForwardPolicyBindings(Bindings).RequireSRV("SkyIrradiance");
    // 5 or 6 behind a parameter collection; 4 in a translucent program of Kadan's own, which binds none.
    if (MaterialBuffer < 4 || MaterialBuffer > 6 || Bindings.layout_hashes.size() <= unsigned(MaterialBuffer) ||
        Bindings.layout_hashes[0] != EngineHash::ViewLayout || Bindings.layout_hashes[2] != 0x05ec0deeu ||
        (Bindings.resource_bits & 5u) != 5u || !Bindings.global_arrays.empty() ||
        !Bindings.packed_global_slots.empty())
        throw std::runtime_error("native-maxwell: translucent sky View/BasePass/Material layout unqualified");
    for (const auto& Arrays : Bindings.uniform_buffers) if (!Arrays.empty())
        throw std::runtime_error("native-maxwell: translucent sky packed uniforms unavailable");
    const auto SRVs = ReadResourceTable(Bindings.srv);
    const auto UAVs = ReadResourceTable(Bindings.uav);
    for (const auto* Table : {&SRVs, &UAVs}) {
        const auto Row = Table->find(unsigned(MaterialBuffer));
        if (Row != Table->end() && !Row->second.empty())
            throw std::runtime_error("native-maxwell: translucent Material storage resources unavailable");
    }
    std::map<std::string,unsigned> Roles;
    Lighting::PolicyResourceNames(SRVs, 0, {{51,"SkyIrradiance"}}, 63, Roles);
    const auto Role = Roles.find("SkyIrradiance");
    if (Role == Roles.end()) throw std::runtime_error("native-maxwell: translucent sky SRV absent");
    return Role->second;
}
std::string StorageStructuralHash(const ShaderBindings& Bindings, int MaterialBuffer=-1, bool OwnTangentFrame=false, int OwnedBuffer=-1) {
    (void)SkyStorageSlot(Bindings, MaterialBuffer);
    auto Normalized = Bindings;
    // An opaque creator program reads the sky through the forward policy role and
    // owns only its Material buffer, which is rebound before compilation.
    if (MaterialBuffer >= 0) OwnedBuffer = MaterialBuffer;
    // Only the separately validated authored Material descriptor may change.
    // Keep all View/BasePass/other scene resource rows and their slot identities.
    if (OwnedBuffer >= 0) {
        const auto NormalizeTable = [&](const std::vector<std::uint32_t>& Words, unsigned* Removed) {
            auto Rows = ReadResourceTable(Words);
            if (Removed) *Removed = unsigned(Rows[unsigned(OwnedBuffer)].size());
            Rows.erase(unsigned(OwnedBuffer));
            return BuildResourceTable(Rows, unsigned(Bindings.layout_hashes.size()));
        };
        unsigned Textures=0, Samplers=0;
        Normalized.textures=NormalizeTable(Bindings.textures,&Textures);
        Normalized.samplers=NormalizeTable(Bindings.samplers,&Samplers);
        Normalized.srv=NormalizeTable(Bindings.srv,nullptr);
        Normalized.uav=NormalizeTable(Bindings.uav,nullptr);
        if (Textures>Bindings.counts[0] || Samplers>Bindings.counts[1])
            throw std::runtime_error("native-maxwell: translucent Material resource count mismatch");
        Normalized.counts[0] -= std::uint8_t(Textures);
        Normalized.counts[1] -= std::uint8_t(Samplers);
        Normalized.layout_hashes[unsigned(OwnedBuffer)]=0;
        Normalized.resource_bits &= ~(1u << unsigned(OwnedBuffer));
    }
    if (OwnTangentFrame) {
        unsigned Tangents=0;
        for (auto It=Normalized.inputs.begin(); It!=Normalized.inputs.end();) {
            if (It->location==0 || It->name=="in_texcoord10_centroid") {
                if (It->location!=0 || It->name!="in_texcoord10_centroid" || ++Tangents!=1)
                    throw std::runtime_error("native-maxwell: authored tangent-X varying conflicts with retained inputs");
                It=Normalized.inputs.erase(It);
            } else ++It;
        }
    }
    // A proved diagnostic UB can extend an otherwise empty packed inventory.
    Normalized.uniform_buffers.clear();
    auto Header=ShaderHeader(Normalized);
    if (MaterialBuffer >= 0) U32(Header,std::uint32_t(Normalized.default_sampler));
    return Hex(Sha256(Header));
}
std::set<int> ValidateSkyStorageReads(const nvasm::detail::Program& Parsed, unsigned Slot) {
    std::set<int> Offsets;
    for (const auto& Instruction : Parsed.code) for (const auto& Operand : Instruction.ops) {
        const auto Buffer = Parsed.storage.find(Operand.name);
        if (Operand.kind != nvasm::detail::Operand::Var || Buffer == Parsed.storage.end() ||
            Buffer->second < 0 || unsigned(Buffer->second) != Slot) continue;
        if (Instruction.op != "LDB" || Instruction.mods != std::vector<std::string>{"F32X4"} ||
            !Operand.indexed || !Operand.index_reg.empty() || Operand.index < 0 ||
            Operand.index > 96 || Operand.index % 16)
            throw std::runtime_error("native-maxwell: SkyIrradiance admits only bounded static F32X4 reads");
        Offsets.insert(Operand.index);
    }
    return Offsets;
}
PixelProgram CompilePixelAssemblyImpl(const std::string& Assembly, const ShaderBindings& Bindings,
    const unsigned* StorageSlot) {
    if (Assembly.empty() || Assembly.size() > (1u << 18)) throw std::runtime_error("native-maxwell: pixel assembly size limit");
    for (unsigned char C : Assembly) if ((C < 32 && C != '\n' && C != '\t' && C != '\r') || C > 126)
        throw std::runtime_error("native-maxwell: assembly must be plain ASCII text");
    // Validate the structural header before spending time compiling. No game control or code enters.
    (void)ShaderHeader(Bindings);
    nvasm::detail::Program Parsed = nvasm::detail::parse(Assembly);
    if (Parsed.stage != nvasm::Stage::Fragment) throw std::runtime_error("native-maxwell: only custom pixel programs are admitted");
    // Program buffers occupy physical constant banks 3 + index. The encoder
    // supports banks 0..17; reject before narrowing a large index to uint8_t.
    for (const auto& Buffer : Parsed.cbuffers)
        if (Buffer.second < 0 || Buffer.second > 14)
            throw std::runtime_error("native-maxwell: program buffer index must be 0..14");
    if (StorageSlot) for (const auto& Buffer : Parsed.storage)
        if (Buffer.second < 0 || unsigned(Buffer.second) != *StorageSlot)
            throw std::runtime_error("native-maxwell: storage declaration lacks exact SkyIrradiance proof");
    if (StorageSlot) (void)ValidateSkyStorageReads(Parsed, *StorageSlot);
    static const std::set<std::string> Known = {"NV_internal", "NV_bindless_texture", "NV_gpu_program_fp64",
        "NV_shader_storage_buffer", "ARB_draw_buffers", "ARB_fragment_program_shadow", "NV_early_fragment_tests",
        "NV_texture_multisample", "ARB_shader_image_size", "NV_shader_atomic_float"};
    for (const auto& Option : Parsed.options) if (!Known.count(Option)) throw nvasm::Error(0, "unsupported OPTION " + Option);
    nvasm::Options Options;
    Options.glsl = false; Options.spirv = false; Options.maxwell = true; Options.demote = false;
    nvasm::Result Compiled;
    nvasm::ir::Function Function = nvasm::lower::lower(Parsed, Options, Compiled);
    for (const auto& Resource : Compiled.resources) {
        if (Resource.kind == nvasm::ResourceKind::StorageImage)
            throw std::runtime_error("native-maxwell: custom materials cannot access storage images");
        if (Resource.kind == nvasm::ResourceKind::StorageBuffer &&
            (!StorageSlot || Resource.index != *StorageSlot || !Resource.read || Resource.written))
            throw std::runtime_error("native-maxwell: storage access requires exact read-only SkyIrradiance proof");
    }
    std::set<int> Locations;
    for (const auto& Input : Bindings.inputs) Locations.insert(Input.location);
    for (const auto& Input : Compiled.inputs) if (!Locations.count(int(Input.location)))
        throw std::runtime_error("native-maxwell: a compiled varying is absent from the shader's structural bindings");
    nvasm::ir::write_maxwell(Function, Compiled);
    if (Compiled.maxwell.size() > (1u << 20) || Compiled.maxwell_program_size > Compiled.maxwell.size() || Compiled.maxwell_registers > 255)
        throw std::runtime_error("native-maxwell: compiled program exceeds payload limits");
    Bytes Computed(0x878, 0);
    Set32(Computed,0x6f8,Compiled.maxwell_program_size); Set32(Computed,0x6fc,0);
    Set32(Computed,0x700,std::uint32_t(Compiled.maxwell.size())); Set32(Computed,0x704,std::uint32_t(Compiled.maxwell.size()));
    Set32(Computed,0x70c,Compiled.maxwell_registers); Set32(Computed,0x710,Compiled.maxwell_scratch); Set32(Computed,0x714,1);
    PixelProgram Out;
    Out.code = std::move(Compiled.maxwell);
    Out.control = CleanControl(Computed, Assembly, Out.code);
    Out.payload = PixelPayload(Bindings, Out.code, Computed, Assembly);
    Out.program_size = Compiled.maxwell_program_size; Out.registers = Compiled.maxwell_registers; Out.scratch = Compiled.maxwell_scratch;
    Out.code_sha256 = Hex(Sha256(Out.code)); Out.control_sha256 = Hex(Sha256(Out.control)); Out.payload_sha1 = Hex(Sha1(Out.payload));
    return Out;
}
}
static std::pair<unsigned,std::string> ReadSkyStorageFacts(const Bytes& OriginalPixelPayload,
    const std::map<std::uint16_t,std::uint64_t>& OriginalUniformHashes, int MaterialBuffer, bool OwnTangentFrame=false, int OwnedBuffer=-1) {
    const auto View = OriginalUniformHashes.find(0);
    if (View == OriginalUniformHashes.end() || View->second != EngineHash::ViewUniformBuffer)
        throw std::runtime_error("native-maxwell: original View uniform identity is unproved");
    const auto Bindings = ReadPixelBindings(OriginalPixelPayload);
    if (OwnTangentFrame) for (const auto& Input : Bindings.inputs)
        if (Input.location==0 || Input.name=="in_texcoord10_centroid")
            throw std::runtime_error("native-maxwell: original tangent-X varying is not an added-input contract");
    const auto Slot = SkyStorageSlot(Bindings, MaterialBuffer);
    const auto Original = OriginalAssemblyMetadata(OriginalPixelPayload, Bindings);
    auto Parsed = nvasm::detail::parse(Original);
    if (Parsed.stage != nvasm::Stage::Fragment)
        throw std::runtime_error("native-maxwell: storage proof requires original pixel program");
    if (ValidateSkyStorageReads(Parsed, Slot) != std::set<int>{0,16,32,48,64,80,96})
        throw std::runtime_error("native-maxwell: original SkyIrradiance seven-word range is unproved");
    nvasm::Options Options; Options.glsl=false; Options.spirv=false; Options.maxwell=true;
    nvasm::Result Resources;
    (void)nvasm::lower::lower(Parsed, Options, Resources);
    bool Matched = false;
    for (const auto& Resource : Resources.resources)
        if (Resource.kind == nvasm::ResourceKind::StorageBuffer && Resource.index == Slot) {
            if (!Resource.read || Resource.written)
                throw std::runtime_error("native-maxwell: original SkyIrradiance storage is not read-only");
            Matched = true;
        }
    if (!Matched) throw std::runtime_error("native-maxwell: original SkyIrradiance STORAGE index is unproved");
    return {Slot, StorageStructuralHash(Bindings, MaterialBuffer, OwnTangentFrame, OwnedBuffer)};
}
StorageReadProof ReadSkyIrradianceStorageProof(const Bytes& Payload,
    const std::map<std::uint16_t,std::uint64_t>& Hashes) {
    const auto Facts=ReadSkyStorageFacts(Payload,Hashes,-1);
    return StorageReadProof(Facts.first,Facts.second);
}
StorageReadProof ReadOpaqueSkyIrradianceStorageProof(const Bytes& Payload,
    const std::map<std::uint16_t,std::uint64_t>& Hashes, unsigned MaterialBuffer) {
    if (MaterialBuffer >= 32) throw std::runtime_error("native-maxwell: opaque Material buffer slot is invalid");
    const auto Facts=ReadSkyStorageFacts(Payload,Hashes,-1,false,int(MaterialBuffer));
    StorageReadProof Proof(Facts.first,Facts.second); Proof.owned_buffer=int(MaterialBuffer); return Proof;
}
StorageReadProof OwnSkyIrradianceStorage(const ShaderBindings& Bindings, unsigned MaterialBuffer) {
    if (MaterialBuffer >= 32) throw std::runtime_error("native-maxwell: own Material buffer slot is invalid");
    StorageReadProof Proof(SkyStorageSlot(Bindings, -1), StorageStructuralHash(Bindings, -1, false, int(MaterialBuffer)));
    Proof.owned_buffer = int(MaterialBuffer);
    return Proof;
}
StorageReadProof OwnTranslucentSkyIrradianceStorage(const ShaderBindings& Bindings, unsigned MaterialBuffer) {
    if (MaterialBuffer >= 32) throw std::runtime_error("native-maxwell: own Material buffer slot is invalid");
    return StorageReadProof(SkyStorageSlot(Bindings, int(MaterialBuffer)), StorageStructuralHash(Bindings, int(MaterialBuffer)),
                            int(MaterialBuffer));
}
StorageReadProof ReadTranslucentSkyIrradianceStorageProof(const Bytes& Payload,
    const std::map<std::uint16_t,std::uint64_t>& Hashes,unsigned MaterialBuffer,bool OwnTangentFrame) {
    const auto Primitive=Hashes.find(1);
    if (Primitive==Hashes.end() || Primitive->second!=0x60929d69929b2b59ull)
        throw std::runtime_error("native-maxwell: original translucent Primitive identity is unproved");
    const auto Facts=ReadSkyStorageFacts(Payload,Hashes,int(MaterialBuffer),OwnTangentFrame);
    return StorageReadProof(Facts.first,Facts.second,int(MaterialBuffer),OwnTangentFrame);
}
PixelProgram CompilePixelAssembly(const std::string& Assembly, const ShaderBindings& Bindings) {
    return CompilePixelAssemblyImpl(Assembly, Bindings, nullptr);
}
PixelProgram CompilePixelAssembly(const std::string& Assembly, const ShaderBindings& Bindings,
    const StorageReadProof& Proof) {
    if (Proof.structural_hash != StorageStructuralHash(Bindings, Proof.material_buffer, Proof.own_tangent_frame, Proof.owned_buffer) ||
        SkyStorageSlot(Bindings, Proof.material_buffer) != Proof.slot)
        throw std::runtime_error("native-maxwell: SkyIrradiance proof belongs to different structural bindings");
    return CompilePixelAssemblyImpl(Assembly, Bindings, &Proof.slot);
}
PixelProgram ResolveAndCompilePixel(const std::string& Assembly,
    const std::map<std::string,std::pair<int,int>>& TextureSlots,
    const std::map<std::string,int>& Varyings, const ShaderBindings& Bindings) {
    return CompilePixelAssembly(Resolve(Assembly,TextureSlots,Varyings),Bindings);
}
} } }

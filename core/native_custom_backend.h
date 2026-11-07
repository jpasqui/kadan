#pragma once
// Existing first-party Maxwell compiler as a library, with wholly compiler-owned control/payload.
#include "custom_material_shader_set.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
struct PixelProgram {
    Bytes code, control, payload;
    std::string code_sha256, control_sha256, payload_sha1;
    std::uint32_t program_size = 0, registers = 0, scratch = 0;
};
// A selected original pixel payload proves the read-only SkyIrradiance SRT
// role and its actual program.storage index. This never admits general storage.
class StorageReadProof {
public:
    unsigned Slot() const { return slot; }
private:
    unsigned slot = 0;
    int material_buffer = -1, owned_buffer = -1;
    bool own_tangent_frame = false;
    std::string structural_hash;
    StorageReadProof(unsigned InSlot, std::string InHash, int MaterialBuffer=-1, bool OwnTangentFrame=false):slot(InSlot),material_buffer(MaterialBuffer),own_tangent_frame(OwnTangentFrame),structural_hash(std::move(InHash)){}
    friend StorageReadProof ReadSkyIrradianceStorageProof(const Bytes&, const std::map<std::uint16_t,std::uint64_t>&);
    friend StorageReadProof ReadTranslucentSkyIrradianceStorageProof(const Bytes&, const std::map<std::uint16_t,std::uint64_t>&, unsigned, bool);
    friend StorageReadProof ReadOpaqueSkyIrradianceStorageProof(const Bytes&, const std::map<std::uint16_t,std::uint64_t>&, unsigned);
    friend StorageReadProof OwnSkyIrradianceStorage(const ShaderBindings&, unsigned);
    friend StorageReadProof OwnTranslucentSkyIrradianceStorage(const ShaderBindings&, unsigned);
    friend PixelProgram CompilePixelAssembly(const std::string&, const ShaderBindings&, const StorageReadProof&);
};
StorageReadProof ReadSkyIrradianceStorageProof(const Bytes& OriginalPixelPayload,
    const std::map<std::uint16_t,std::uint64_t>& OriginalUniformHashes);
// The opaque creator program owns only its rebound Material buffer.
StorageReadProof ReadOpaqueSkyIrradianceStorageProof(const Bytes& OriginalPixelPayload,
    const std::map<std::uint16_t,std::uint64_t>& OriginalUniformHashes, unsigned MaterialBuffer);
// A program of Kadan's own reads the View's sky irradiance in the buffer-view slot its own binding description
// gives it (OwnOpaque): no original program to prove the slot from.
StorageReadProof OwnSkyIrradianceStorage(const ShaderBindings& OwnBindings, unsigned MaterialBuffer);
// The same for a translucent program of Kadan's own (OwnTranslucent): the sky irradiance in the View's buffer-view slot
// its description gives it, the Material buffer its own.
StorageReadProof OwnTranslucentSkyIrradianceStorage(const ShaderBindings& OwnBindings, unsigned MaterialBuffer);
StorageReadProof ReadTranslucentSkyIrradianceStorageProof(const Bytes& OriginalPixelPayload,
    const std::map<std::uint16_t,std::uint64_t>& OriginalUniformHashes, unsigned MaterialBuffer, bool OwnTangentFrame=false);
// Fragment programs only. Throws a named refusal and returns no partial payload on unsupported input.
// Callers supply structural bindings read from the selected original shader, never copied shader code.
PixelProgram CompilePixelAssembly(const std::string& Assembly, const ShaderBindings& Bindings);
PixelProgram CompilePixelAssembly(const std::string& Assembly, const ShaderBindings& Bindings,
    const StorageReadProof& ReadOnlySkyIrradiance);
PixelProgram ResolveAndCompilePixel(const std::string& Assembly,
    const std::map<std::string,std::pair<int,int>>& TextureSlots,
    const std::map<std::string,int>& Varyings, const ShaderBindings& Bindings);
} } }

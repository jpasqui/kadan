#pragma once
// The compiler half of Kadan's GLSL_SWITCH shader format (stage 8.5 of tools/navigation/OWN_SHADER_MAPS.md).
//
// The format module (KadanShaderFormat) preprocesses UE's shader source for the Switch and compiles it with DXC to
// SPIR-V; the Kadan module, which holds our SPIR-V front end and Maxwell compiler, registers this feature and turns
// the SPIR-V into the game's program. Kept apart so UE can find and load the small format module early (it looks
// shader formats up by module name) without loading Kadan's editor module with it.
#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

// What a program reads, from its SPIR-V. Slots are the game's: uniform buffers bufN, texture and sampler handle slots.
struct FKadanSwitchMember
{
    FString Name;
    uint32 Offset = 0;
    uint32 Size = 0;
};
struct FKadanSwitchBuffer
{
    FString Name;
    int32 Slot = 0;
    uint32 Size = 0;
    TArray<FKadanSwitchMember> Members;
};
struct FKadanSwitchResource
{
    FString Name;
    int32 Slot = 0;
    bool bBuffer = false;
};
struct FKadanSwitchVarying
{
    FString Name;
    int32 Location = 0;
};

struct FKadanSwitchProgram
{
    FString Assembly; // NV assembly in the game's form
    int32 Stage = 0; // 0 vertex, 1 pixel, 2 geometry
    TArray<FKadanSwitchBuffer> Buffers; // uniform blocks, "$Globals" for loose parameters
    TArray<FKadanSwitchResource> Textures, Samplers;
    TArray<FKadanSwitchResource> Storage; // structured buffers, in storage slots
    TArray<FKadanSwitchVarying> Inputs, Outputs; // user varyings (the game's in_/out_ names)
    uint32 InOutMask = 0; // vertex attributes read, or render targets written (0x8000 depth)
    int32 DefaultSampler = -1; // the sampler slot reads without a sampler take
};

// A resource a payload binds through a uniform buffer: resource `Resource` of the struct in slot `Buffer`, in `Slot`.
// Name: the resource member's shader name (OpaqueBasePass_DBufferATexture), for the game's own resource index.
struct FKadanSwitchTableEntry
{
    uint32 Buffer = 0;
    uint32 Resource = 0;
    uint32 Slot = 0;
    FString Name;
};
struct FKadanSwitchPackedArray
{
    uint16 Size = 0;
    uint8 TypeName = 0;
    uint8 TypeIndex = 0;
};

// The payload's binding header, in the game's terms (the OpenGL RHI's model: real uniform buffers, resource tables
// per buffer slot, packed global arrays uploaded to a buffer slot of their own).
struct FKadanSwitchBindings
{
    TArray<FString> BufferNames; // per uniform buffer slot: the struct's shader name ("View"), or empty
    TArray<uint32> LayoutHashes; // per uniform buffer slot (UE's); 0 for a struct without resources
    TArray<FKadanSwitchTableEntry> Textures, Samplers, SRVs, UAVs;
    uint32 SamplerSlots = 0, UniformBuffers = 0, StorageBuffers = 0, TextureSlots = 0;
    TArray<FKadanSwitchPackedArray> GlobalArrays;
    TArray<TPair<uint8, int32>> PackedGlobalSlots; // (packed array type index, buffer slot)
};

// A material's game-only settings (the properties Nintendo's engine adds to UMaterial), which Kadan keeps with the
// material and shows on its Details panel. The format asks for them as it queues each of the material's shader
// jobs and compiles the job with each as a define, KADAN_<name>.
class IKadanMaterialSettings : public IModularFeature
{
public:
    static FName FeatureName() { return TEXT("KadanMaterialSettings"); }
    // By the material's object name (a shader job's material, as UE names it). False when it has none.
    virtual bool Find(const FString& Material, TArray<TPair<FString, FString>>& Out) const = 0;
};

// What Kadan tells the format about the material it compiles for the game, around that material's shader map
// compile and on its thread. MaterialSSR: the game's translucent material with screen space reflections drawn with
// the scene colour behind it (C4MaterialDrawOrder XluUseSceneColor), whose map holds the game's translucency pass
// classes (XluPassSSR, XluPassRecombine).
class IKadanSwitchCompileContext : public IModularFeature
{
public:
    static FName FeatureName() { return TEXT("KadanSwitchCompileContext"); }
    virtual void SetMaterialSSR(bool bMaterialSSR) = 0;
};

class IKadanSwitchShaderCompiler : public IModularFeature
{
public:
    static FName FeatureName() { return TEXT("KadanSwitchShaderCompiler"); }
    // The format's version, part of UE's Derived Data Cache key: changes when the compiler's output changes.
    virtual uint32 Version() const = 0;
    // SPIR-V to the program and what it reads. False with an error the material editor shows.
    virtual bool Translate(const TArray<uint32>& Spirv, FKadanSwitchProgram& Out, FString& Error) const = 0;
    // The program compiled by our Maxwell compiler, as the game's payload with its binding header.
    virtual bool Payload(const FKadanSwitchProgram& Program, const FKadanSwitchBindings& Bindings, TArray<uint8>& Out,
        FString& Error) const = 0;
};

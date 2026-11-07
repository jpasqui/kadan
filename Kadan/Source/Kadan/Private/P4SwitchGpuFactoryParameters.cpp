#include "P4SwitchGpuFactoryParameters.h"
#include "Misc/DelayedAutoRegister.h"
#include "Serialization/MemoryImage.h"
#include "Serialization/MemoryImageWriter.h"
#include "Serialization/MemoryLayout.h"
#include "Shader.h"
#include "ShaderCore.h"
#include "ShaderParameters.h"
#include "VertexFactory.h"

namespace
{
// The stock prefix, in the order ParticleGpuSimulation.cpp declares it, followed by the game's two resource bindings.
// These are FShaderParameter/FShaderResourceParameter objects, bound by name through the actual compile output.
class FKadanGPUSpriteShaderParametersVS : public FVertexFactoryShaderParameters
{
    DECLARE_INLINE_TYPE_LAYOUT(FKadanGPUSpriteShaderParametersVS, NonVirtual);

public:
    void Bind(const FShaderParameterMap& Map)
    {
        ParticleIndices.Bind(Map, TEXT("ParticleIndices"));
        ParticleIndicesOffset.Bind(Map, TEXT("ParticleIndicesOffset"));
        PositionTexture.Bind(Map, TEXT("PositionTexture"));
        PositionTextureSampler.Bind(Map, TEXT("PositionTextureSampler"));
        VelocityTexture.Bind(Map, TEXT("VelocityTexture"));
        VelocityTextureSampler.Bind(Map, TEXT("VelocityTextureSampler"));
        AttributesTexture.Bind(Map, TEXT("AttributesTexture"));
        AttributesTextureSampler.Bind(Map, TEXT("AttributesTextureSampler"));
        CurveTexture.Bind(Map, TEXT("CurveTexture"));
        CurveTextureSampler.Bind(Map, TEXT("CurveTextureSampler"));
        C4ParamTexture.Bind(Map, TEXT("C4ParamTexture"));
        C4ParamTextureSampler.Bind(Map, TEXT("C4ParamTextureSampler"));
    }
    LAYOUT_FIELD(FShaderResourceParameter, ParticleIndices);
    LAYOUT_FIELD(FShaderParameter, ParticleIndicesOffset);
    LAYOUT_FIELD(FShaderResourceParameter, PositionTexture);
    LAYOUT_FIELD(FShaderResourceParameter, PositionTextureSampler);
    LAYOUT_FIELD(FShaderResourceParameter, VelocityTexture);
    LAYOUT_FIELD(FShaderResourceParameter, VelocityTextureSampler);
    LAYOUT_FIELD(FShaderResourceParameter, AttributesTexture);
    LAYOUT_FIELD(FShaderResourceParameter, AttributesTextureSampler);
    LAYOUT_FIELD(FShaderResourceParameter, CurveTexture);
    LAYOUT_FIELD(FShaderResourceParameter, CurveTextureSampler);
    LAYOUT_FIELD(FShaderResourceParameter, C4ParamTexture);
    LAYOUT_FIELD(FShaderResourceParameter, C4ParamTextureSampler);
};

// Size_DEPRECATED is the base's existing four-byte field. A stamp in the editor's copies permits checking an old
// stock allocation without reading beyond it. The game image writes that field back to its original value, zero.
constexpr uint32 ParametersStamp = 0x4b475056u;
uint32 Stamp(const FVertexFactoryShaderParameters* Parameters)
{
    uint32 Value = 0;
    if (Parameters)
        FMemory::Memcpy(&Value, Parameters, sizeof(Value));
    return Value;
}
void SetStamp(FVertexFactoryShaderParameters* Parameters, uint32 Value)
{
    FMemory::Memcpy(Parameters, &Value, sizeof(Value));
}

struct FConstructRef
{
    using Type = FVertexFactoryType::ConstructParametersType FVertexFactoryType::*;
};
template <typename Tag>
struct TMemberOf
{
    static typename Tag::Type Pointer;
};
template <typename Tag>
typename Tag::Type TMemberOf<Tag>::Pointer = nullptr;
template <typename Tag, typename Tag::Type Member>
struct TMemberGrant
{
    TMemberGrant() { TMemberOf<Tag>::Pointer = Member; }
    static TMemberGrant Instance;
};
template <typename Tag, typename Tag::Type Member>
TMemberGrant<Tag, Member> TMemberGrant<Tag, Member>::Instance;
template struct TMemberGrant<FConstructRef, &FVertexFactoryType::ConstructParameters>;

FVertexFactoryType* GPUFactory = nullptr;
FVertexFactoryType::ConstructParametersType StockConstruct = nullptr;
FFieldLayoutDesc* ParametersField = nullptr;
FFieldLayoutDesc::FWriteFrozenMemoryImageFunc* StockWrite = nullptr;
const FTypeLayoutDesc* StockLayout = nullptr;
FTypeLayoutDesc GameLayout{};
uint32 ShaderBaseOffset = 0;
bool Installed = false;

const FFieldLayoutDesc* Field(const FTypeLayoutDesc& Type, const TCHAR* Name)
{
    for (const FFieldLayoutDesc* Each = Type.Fields; Each; Each = Each->Next)
        if (FCString::Strcmp(Each->Name, Name) == 0)
            return Each;
    return nullptr;
}

bool IsGameGPU(const FShader& Shader, const FVertexFactoryType* Factory)
{
    return Shader.GetShaderPlatform() == SP_SWITCH && Shader.GetFrequency() == SF_Vertex && Factory == GPUFactory;
}

FVertexFactoryShaderParameters* Construct(EShaderFrequency Frequency, const FShaderParameterMap& Map)
{
    // The Switch format maps loose uint parameters to 'u' (117). C4ParamTexture itself can be optimized away in a
    // shader that does not need the factory's position. Other shader platforms keep their original constructor.
    uint16 Buffer = 0, Base = 0, Size = 0;
    const bool SwitchIndices = Map.FindParameterAllocation(TEXT("ParticleIndicesOffset"), Buffer, Base, Size) &&
        Buffer == uint16('u');
    if (Frequency != SF_Vertex || (!Map.ContainsParameterAllocation(TEXT("C4ParamTexture")) && !SwitchIndices))
        return StockConstruct(Frequency, Map);
    FKadanGPUSpriteShaderParametersVS* Parameters = new FKadanGPUSpriteShaderParametersVS();
    Parameters->Bind(Map);
    SetStamp(Parameters, ParametersStamp);
    return Parameters;
}

void Write(FMemoryImageWriter& Writer, const void* Object, const void* FieldObject, const FTypeLayoutDesc& Type,
    const FTypeLayoutDesc& Derived)
{
    const FShader& Shader = *reinterpret_cast<const FShader*>(static_cast<const uint8*>(Object) + ShaderBaseOffset);
    const FVertexFactoryType* Factory = Shader.GetVertexFactoryType(Writer.TryGetPrevPointerTable());
    const auto& Parameters = *static_cast<const TMemoryImagePtr<FVertexFactoryShaderParameters>*>(FieldObject);
    const bool Marked = Stamp(Parameters.Get()) == ParametersStamp;
    if (!IsGameGPU(Shader, Factory))
    {
        if (Marked)
        {
            // A different OpenGL target can also pack uints as 'u'. Its stock writer still sees exactly the stock
            // prefix, including the original deprecated zero; the added fields never enter its image or DDC key.
            FKadanGPUSpriteShaderParametersVS Copy = *static_cast<const FKadanGPUSpriteShaderParametersVS*>(
                Parameters.Get());
            SetStamp(&Copy, 0);
            TMemoryImagePtr<FVertexFactoryShaderParameters> HostParameters(&Copy);
            StockWrite(Writer, Object, &HostParameters, Type, Derived);
        }
        else
            StockWrite(Writer, Object, FieldObject, Type, Derived);
        return;
    }
    if (!Marked)
    {
        // In particular, never read a tail from an old cached stock object. Export validates the stamp and refuses
        // such a map; the format version changes with this binding layout so new Switch jobs construct the new type.
        StockWrite(Writer, Object, FieldObject, Type, Derived);
        return;
    }
    if (Writer.GetTargetLayoutParams().WithEditorOnly())
    {
        // UE's own editor/DDC image uses a separately registered type, so its dependency checks resolve the same
        // 56-byte class instead of resolving the stock 48-byte class under the game's name.
        Parameters.WriteMemoryImageWithDerivedType(
            Writer, &StaticGetTypeLayoutDesc<FKadanGPUSpriteShaderParametersVS>());
    }
    else
    {
        FKadanGPUSpriteShaderParametersVS Copy = *static_cast<const FKadanGPUSpriteShaderParametersVS*>(
            Parameters.Get());
        SetStamp(&Copy, 0);
        TMemoryImagePtr<FVertexFactoryShaderParameters> GameParameters(&Copy);
        GameParameters.WriteMemoryImageWithDerivedType(Writer, &GameLayout);
    }
}

bool CheckLayout(FString& Error)
{
    FTypeLayoutDesc& Custom = FKadanGPUSpriteShaderParametersVS::StaticGetTypeLayout();
    const uint64 CustomHash = FHashedName(Custom.Name).GetHash();
    const FTypeLayoutDesc* Registered = FTypeLayoutDesc::Find(CustomHash);
    if (!Registered)
        FTypeLayoutDesc::Register(Custom); // DECLARE_INLINE_TYPE_LAYOUT initializes but does not register
    if (FTypeLayoutDesc::Find(CustomHash) != &Custom)
    {
        Error = TEXT("switch-gpu-factory-editor-layout-registration");
        return false;
    }
    const FTypeLayoutDesc& Base = StaticGetTypeLayoutDesc<FVertexFactoryShaderParameters>();
    const FFieldLayoutDesc* Size = Field(Base, TEXT("Size_DEPRECATED"));
    if (!StockLayout || StockLayout->Size != 48 || StockLayout->Alignment != 4 || Base.Size != 4 || !Size ||
        Size->Offset != 0 || Size->Type->Size != 4 || Custom.Size != 56 || Custom.Alignment != 4)
    {
        Error = TEXT("switch-gpu-factory-stock-layout-size");
        return false;
    }
    uint32 Fields = 0;
    for (const FFieldLayoutDesc* Original = StockLayout->Fields; Original; Original = Original->Next)
    {
        const FFieldLayoutDesc* Match = Field(Custom, Original->Name);
        if (!Match || Match->Offset != Original->Offset || Match->Type != Original->Type ||
            Match->NumArray != Original->NumArray || Match->Flags != Original->Flags ||
            Match->BitFieldSize != Original->BitFieldSize)
        {
            Error = TEXT("switch-gpu-factory-stock-prefix");
            return false;
        }
        ++Fields;
    }
    const FFieldLayoutDesc* Texture = Field(Custom, TEXT("C4ParamTexture"));
    const FFieldLayoutDesc* Sampler = Field(Custom, TEXT("C4ParamTextureSampler"));
    if (Fields != 11 || !Texture || !Sampler || Texture->Offset != 46 || Sampler->Offset != 50 ||
        Texture->Type != &StaticGetTypeLayoutDesc<FShaderResourceParameter>() || Sampler->Type != Texture->Type)
    {
        Error = TEXT("switch-gpu-factory-game-tail");
        return false;
    }
    GameLayout = Custom;
    GameLayout.Name = StockLayout->Name;
    // Stock's inline VF parameter type is unregistered (NameHash zero), as in the game's maps. Preserve that absence
    // from game dependencies; only the separately named editor/DDC descriptor above is registered in this process.
    GameLayout.NameHash = StockLayout->NameHash;
    GameLayout.HashNext = nullptr; // an output descriptor only; do not replace the engine's registered stock type
    FPlatformTypeLayoutParameters Game;
    Game.MaxFieldAlignment = 0xffffffffu;
    Game.Flags = FPlatformTypeLayoutParameters::Flag_Initialized;
    FSHAHash Hash;
    // HashLayout returns the last field's end (54); the object/section rounds that up to alignment four (56).
    if (Freeze::HashLayout(GameLayout, Game, Hash) != 54)
    {
        Error = TEXT("switch-gpu-factory-game-frozen-size");
        return false;
    }
    return true;
}

bool CheckBindingImage(FString& Error)
{
    // A synthetic compile output exercises every field, both resource kinds, the uint buffer and the two new names.
    FShaderParameterMap Map;
    Map.AddParameterAllocation(TEXT("ParticleIndices"), 0, 5, 1, EShaderParameterType::SRV);
    Map.AddParameterAllocation(TEXT("ParticleIndicesOffset"), uint16('u'), 0, 4, EShaderParameterType::LooseData);
    const TCHAR* Textures[] = {TEXT("PositionTexture"), TEXT("VelocityTexture"), TEXT("AttributesTexture"),
        TEXT("CurveTexture"), TEXT("C4ParamTexture")};
    const TCHAR* Samplers[] = {TEXT("PositionTextureSampler"), TEXT("VelocityTextureSampler"),
        TEXT("AttributesTextureSampler"), TEXT("CurveTextureSampler"), TEXT("C4ParamTextureSampler")};
    for (uint16 Index = 0; Index < 5; ++Index)
    {
        Map.AddParameterAllocation(Textures[Index], 0, 4 - Index, 1, EShaderParameterType::SRV);
        Map.AddParameterAllocation(Samplers[Index], 0, Index, 1, EShaderParameterType::Sampler);
    }
    FVertexFactoryShaderParameters* Made = Construct(SF_Vertex, Map);
    if (!Made || Stamp(Made) != ParametersStamp)
    {
        Error = TEXT("switch-gpu-factory-bind-constructor");
        return false;
    }
    FKadanGPUSpriteShaderParametersVS Copy = *static_cast<FKadanGPUSpriteShaderParametersVS*>(Made);
    delete static_cast<FKadanGPUSpriteShaderParametersVS*>(Made);
    SetStamp(&Copy, 0);
    FMemoryImage Image;
    Image.TargetLayoutParameters.MaxFieldAlignment = 0xffffffffu;
    Image.TargetLayoutParameters.Flags = FPlatformTypeLayoutParameters::Flag_Initialized;
    FMemoryImageWriter Writer(Image);
    Writer.WriteObject(&Copy, GameLayout);
    FMemoryImageResult Result;
    Image.Flatten(Result, true);
    const uint16 Expected[] = {0, 0, 5, 1, 117, 0, 4, 4, 1, 0, 1, 3, 1, 1, 1, 2, 1, 2, 1, 1, 1, 3, 1, 0, 1, 4, 1};
    if (Result.Bytes.Num() != int32(sizeof(Expected)) ||
        FMemory::Memcmp(Result.Bytes.GetData(), Expected, sizeof(Expected)) != 0)
    {
        Error = TEXT("switch-gpu-factory-bind-frozen-fields");
        return false;
    }
    FShaderParameterMap HostMap;
    FVertexFactoryShaderParameters* Host = Construct(SF_Vertex, HostMap);
    const bool HostStock = Host && Stamp(Host) != ParametersStamp;
    if (Host)
    {
        if (StockLayout->DestroyFunc)
            StockLayout->DestroyFunc(Host, *StockLayout);
        ::operator delete(Host);
    }
    if (!HostStock || GPUFactory->GetShaderParameterLayout(SF_Vertex) != StockLayout)
    {
        Error = TEXT("switch-gpu-factory-host-delegation");
        return false;
    }
    FSHAHash Hash;
    Freeze::HashLayout(GameLayout, Image.TargetLayoutParameters, Hash);
    UE_LOG(LogTemp, Display,
        TEXT(
            "Kadan: GPU sprite factory binding self-check: game 56 bytes (54 field bytes), tail 46/50, layout %s; stock host 48 bytes unchanged"),
        *Hash.ToString());
    return true;
}

// Restore only slots that still point to this module when its static state is destroyed.
struct FRestore
{
    ~FRestore()
    {
        if (GPUFactory && TMemberOf<FConstructRef>::Pointer &&
            GPUFactory->*TMemberOf<FConstructRef>::Pointer == &Construct)
            GPUFactory->*TMemberOf<FConstructRef>::Pointer = StockConstruct;
        if (ParametersField && ParametersField->WriteFrozenMemoryImageFunc == &Write)
            ParametersField->WriteFrozenMemoryImageFunc = StockWrite;
    }
} Restore;

// Kadan's Default loading phase can precede registration of FMeshMaterialShader's layout. The callback runs after
// the engine's type-layout callbacks; eager callers also check installation before any Switch shader jobs begin.
FDelayedAutoRegisterHelper RegisterGpuParameters(EDelayedRegisterRunPhase::ShaderTypesReady,
    []
    {
        FString Error;
        if (!P4InstallSwitchGpuFactoryParameters(Error))
            UE_LOG(LogTemp, Display, TEXT("Kadan: GPU sprite binding installation deferred: %s"), *Error);
    });
} //namespace

bool P4InstallSwitchGpuFactoryParameters(FString& Error)
{
    if (Installed)
        return true;
    if (!IsInGameThread())
    {
        Error = TEXT("switch-gpu-factory-install-thread");
        return false;
    }
    GPUFactory = FVertexFactoryType::GetVFByName(FHashedName(TEXT("FGPUSpriteVertexFactory")));
    const FTypeLayoutDesc* Mesh = FTypeLayoutDesc::Find(FHashedName(TEXT("FMeshMaterialShader")).GetHash());
    if (!GPUFactory || !Mesh || !TMemberOf<FConstructRef>::Pointer)
    {
        Error = TEXT("switch-gpu-factory-type-missing");
        return false;
    }
    ParametersField = const_cast<FFieldLayoutDesc*>(Field(*Mesh, TEXT("VertexFactoryParameters")));
    StockLayout = GPUFactory->GetShaderParameterLayout(SF_Vertex);
    if (!ParametersField || !ParametersField->WriteFrozenMemoryImageFunc || !CheckLayout(Error))
        return false;
    ShaderBaseOffset = Mesh->GetOffsetToBase(StaticGetTypeLayoutDesc<FShader>());
    StockConstruct = GPUFactory->*TMemberOf<FConstructRef>::Pointer;
    StockWrite = ParametersField->WriteFrozenMemoryImageFunc;
    if (!StockConstruct || !CheckBindingImage(Error))
        return false;
    GPUFactory->*TMemberOf<FConstructRef>::Pointer = &Construct;
    ParametersField->WriteFrozenMemoryImageFunc = &Write;
    Installed = true;
    return true;
}

void P4UninstallSwitchGpuFactoryParameters()
{
    if (!Installed)
        return;
    if (GPUFactory && TMemberOf<FConstructRef>::Pointer && GPUFactory->*TMemberOf<FConstructRef>::Pointer == &Construct)
        GPUFactory->*TMemberOf<FConstructRef>::Pointer = StockConstruct;
    if (ParametersField && ParametersField->WriteFrozenMemoryImageFunc == &Write)
        ParametersField->WriteFrozenMemoryImageFunc = StockWrite;
    Installed = false;
}

const FTypeLayoutDesc* P4SwitchFactoryParameterLayout(const FShader& Shader, const FShaderMapPointerTable& Table)
{
    const FVertexFactoryType* Factory = Shader.GetVertexFactoryType(Table);
    if (IsGameGPU(Shader, Factory))
        return &StaticGetTypeLayoutDesc<FKadanGPUSpriteShaderParametersVS>();
    return Factory ? Factory->GetShaderParameterLayout(Shader.GetFrequency()) : nullptr;
}

bool P4ValidateSwitchGpuFactoryParameters(const FShader& Shader, const FShaderMapPointerTable& Table,
    const FVertexFactoryShaderParameters* Parameters, FString& Error)
{
    if (!IsGameGPU(Shader, Shader.GetVertexFactoryType(Table)))
        return true;
    if (!Parameters || Stamp(Parameters) != ParametersStamp)
    {
        Error = TEXT("switch-gpu-factory-cached-stock-parameters-recompile");
        return false;
    }
    const auto* GPU = static_cast<const FKadanGPUSpriteShaderParametersVS*>(Parameters);
    if (GPU->C4ParamTexture.IsBound() != GPU->C4ParamTextureSampler.IsBound() ||
        (GPU->C4ParamTexture.IsBound() &&
            (GPU->C4ParamTexture.GetNumResources() != 1 || GPU->C4ParamTexture.GetBaseIndex() >= 64 ||
                GPU->C4ParamTextureSampler.GetNumResources() != 1 || GPU->C4ParamTextureSampler.GetBaseIndex() >= 32)))
    {
        Error = TEXT("switch-gpu-factory-c4-resource-bindings");
        return false;
    }
    return true;
}

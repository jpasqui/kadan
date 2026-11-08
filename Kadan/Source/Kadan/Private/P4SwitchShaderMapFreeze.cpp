#include "P4SwitchShaderMapFreeze.h"
#include "P4MaterialInstancePermutation.h"
#include "P4MaterialFactoryContext.h"
#include "P4KadanMaterialExpressions.h"
#include "P4MaterialGameSettings.h"
#include "P4MaterialInstanceGameSettings.h"
#include "P4SwitchGpuFactoryParameters.h"
#include "KadanSwitchShaderCompiler.h"
#include "Features/IModularFeatures.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpressionQualitySwitch.h"
#include "Serialization/MemoryImage.h"
#include "Serialization/MemoryImageWriter.h"
#include "Serialization/MemoryLayout.h"
#include "Serialization/MemoryWriter.h"
#include "Shader.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Compression.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VertexFactory.h"
#include "P4MapKitCore/own_map_writer.h"

namespace
{
// FMaterialResourceMemoryWriter's names (it is not exported): each as its index in the name table and its number.
class FNameIndexWriter final : public FMemoryWriter
{
public:
    FNameIndexWriter(TArray<uint8>& Bytes, TArray<FName>& InNames) : FMemoryWriter(Bytes, true), Names(InNames)
    {
        for (int32 Index = 0; Index < Names.Num(); ++Index)
            Indices.Add(Names[Index].GetDisplayIndex(), Index);
    }
    virtual FArchive& operator<<(FName& Name) override
    {
        int32 Index;
        if (const int32* Found = Indices.Find(Name.GetDisplayIndex()))
            Index = *Found;
        else
        {
            Index = Names.Add(FName(Name, 0));
            Indices.Add(Name.GetDisplayIndex(), Index);
        }
        int32 Number = Name.GetNumber();
        *this << Index << Number;
        return *this;
    }

private:
    TArray<FName>& Names;
    TMap<FNameEntryId, int32> Indices;
};

// A field of a frozen-layout type by its name (FTypeLayoutDesc's own field list): for the private fields of
// FMaterialShaderMapContent, whose class is not exported.
void* LayoutField(void* Object, const FTypeLayoutDesc& Type, const TCHAR* Name)
{
    for (const FFieldLayoutDesc* Field = Type.Fields; Field; Field = Field->Next)
        if (FCString::Strcmp(Field->Name, Name) == 0)
            return static_cast<uint8*>(Object) + Field->Offset;
    return nullptr;
}

using FFactoryParametersPtr = TMemoryImagePtr<FVertexFactoryShaderParameters>;

// Each mesh shader's vertex factory parameters, in the order of the content's mesh maps, their shaders and their
// pipelines' shaders. Visit gets the shader and its parameters' pointer.
template <typename FVisit>
bool ForEachFactoryParameters(FShaderMapContent& Content, const FTypeLayoutDesc& ContentLayout, FVisit&& Visit)
{
    const FTypeLayoutDesc* MeshShaderLayout = FTypeLayoutDesc::Find(FHashedName(TEXT("FMeshMaterialShader")).GetHash());
    auto* MeshMaps = static_cast<TMemoryImageArray<TMemoryImagePtr<FMeshMaterialShaderMap>>*>(
        LayoutField(&Content, ContentLayout, TEXT("OrderedMeshShaderMaps")));
    if (!MeshShaderLayout || !MeshMaps)
        return false;
    auto One = [&](FShader* Shader)
    {
        if (!Shader)
            return true;
        void* Field = LayoutField(Shader, *MeshShaderLayout, TEXT("VertexFactoryParameters"));
        if (!Field)
            return false;
        Visit(*Shader, *static_cast<FFactoryParametersPtr*>(Field));
        return true;
    };
    for (FMeshMaterialShaderMap* Mesh : *MeshMaps)
    {
        for (const TMemoryImagePtr<FShader>& Shader : Mesh->GetShaders())
            if (!One(Shader.Get()))
                return false;
        for (const TMemoryImagePtr<FShaderPipeline>& Pipeline : Mesh->GetShaderPipelines())
            for (uint32 Stage = 0; Stage < SF_NumGraphicsFrequencies; ++Stage)
                if (!One(Pipeline->Shaders[Stage].Get()))
                    return false;
    }
    return true;
}

// Per-material policy belongs in FMaterial::ShouldCache, after UE's shared layout lookup.
// FMaterialShaderParameters omits both this SSR flag and C4 draw order.
bool IsGameSSR(const UMaterial* Material, EBlendMode Blend)
{
    const UP4MaterialGameSettings* Settings = Material ? UP4MaterialGameSettings::Find(Material) : nullptr;
    return Material && Settings && IsTranslucentBlendMode(Blend) && Material->bScreenSpaceReflections &&
        Settings->C4MaterialDrawOrder == C4MDO_XluUseSceneColor;
}

bool IsGameXluShader(const FShaderType* Type)
{
    if (!Type)
        return false;
    const FString Name = Type->GetName();
    return Name.StartsWith(TEXT("TBasePassPS"), ESearchCase::CaseSensitive) &&
        (Name.EndsWith(TEXT("XluPassSSR"), ESearchCase::CaseSensitive) ||
            Name.EndsWith(TEXT("XluPassRecombine"), ESearchCase::CaseSensitive));
}

class FP4SwitchMaterialResource final : public FMaterialResource
{
public:
    explicit FP4SwitchMaterialResource(const UMaterial* InMaterial, UMaterialInstance* Instance = nullptr)
        : bGameSSR(IsGameSSR(InMaterial, Instance ? Instance->GetBlendMode() : InMaterial->GetBlendMode()))
    {}

    virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType,
        const FVertexFactoryType* VertexFactoryType) const override
    {
        if (Platform == SP_SWITCH && IsGameXluShader(ShaderType) && !bGameSSR)
            return false;
        return FMaterialResource::ShouldCache(Platform, ShaderType, VertexFactoryType);
    }

private:
    // Both owned callers hold an unchanged material for this resource's lifetime.
    const bool bGameSSR;
};

// The shader types UE 4.26 compiles that the game's engine leaves out: the indirect lighting cache's two policies
// (its renderer never selects them; stock UE compiles them whenever static lighting is allowed), the translucent self
// shadow, velocity and lightmap density classes (none of the game's 621 cooked master maps has them: no velocity
// pass in its renderer, and lightmap density is an editor view), and stock's shadow
// depth vertex classes, which the game has as its own TC4ShadowDepthVS classes (KadanGameShaderTypes.cpp), and
// stock's decal and light function pixel classes (FDeferredDecalPS, FDeferredDecalEmissivePS, FLightFunctionPS),
// which the game has as its FDeferredDecalPS00.., FDeferredDecalEmissivePS00.. and FLightFunctionPS_.. classes. The map is
// unfrozen, they leave their mesh maps, and the content hash is computed again over what is left, as UE's own
// FMaterialShaderMap::FinalizeContent computes it. Their programs stay in the code, unused.
//
// UE's unfreeze copies an object behind a pointer by its run-time type, which a type without a vtable cannot tell:
// each shader's vertex factory parameters come back as the empty FVertexFactoryShaderParameters, and freezing them
// again with the factory's parameter layout (FMeshMaterialShader::WriteFrozenVertexFactoryParameters) reads heap
// memory past that copy. Before the unfreeze each is copied with its factory's layout, and the copies replace them.
bool PruneGameAbsentShaders(
    FMaterialShaderMap& Map, const FTypeLayoutDesc& ContentLayout, int32& Removed, FString& Error)
{
    TMap<FShaderId, TShaderRef<FShader>> Shaders;
    Map.GetShaderList(Shaders);
    TArray<TTuple<FHashedName, const FShaderType*, int32>> Drop;
    // The map's own (not per vertex factory) shaders the game has under its names.
    TArray<TTuple<const FShaderType*, int32>> DropOwn;
    for (const auto& Pair : Shaders)
    {
        const FString Type = Pair.Key.Type ? FString(Pair.Key.Type->GetName()) : FString();
        if (Pair.Key.VFType &&
            (Type.StartsWith(TEXT("TBasePassVSFCached")) || Type.StartsWith(TEXT("TBasePassPSFCached")) ||
                Type.StartsWith(TEXT("TShadowDepthVSVertexShadowDepth_"), ESearchCase::CaseSensitive) ||
                Type.StartsWith(TEXT("TTranslucencyShadowDepth"), ESearchCase::CaseSensitive) ||
                Type.StartsWith(TEXT("FVelocity"), ESearchCase::CaseSensitive) ||
                Type.StartsWith(TEXT("TLightMapDensity"), ESearchCase::CaseSensitive)))
            Drop.Emplace(Pair.Key.VFType->GetHashedName(), Pair.Key.Type, Pair.Key.PermutationId);
        if (!Pair.Key.VFType &&
            (Type == TEXT("FDeferredDecalPS") || Type == TEXT("FDeferredDecalEmissivePS") ||
                Type == TEXT("FLightFunctionPS")))
            DropOwn.Emplace(Pair.Key.Type, Pair.Key.PermutationId);
    }
    Removed = Drop.Num() + DropOwn.Num();
    int32 GameShadow = 0;
    for (const auto& Pair : Shaders)
        if (Pair.Key.Type &&
            FString(Pair.Key.Type->GetName()).StartsWith(TEXT("TC4ShadowDepthVS"), ESearchCase::CaseSensitive))
            ++GameShadow;
    UE_LOG(LogTemp, Display,
        TEXT("Kadan: the game's map leaves out %d of %d shaders; %d are the game's shadow depth classes"), Removed,
        Shaders.Num(), GameShadow);
    if (!Drop.Num() && !DropOwn.Num())
        return true;
    TArray<FVertexFactoryShaderParameters*> Parameters;
    {
        FMemoryUnfreezeContent Context;
        Context.PrevPointerTable = &Map.GetPointerTable();
        bool bKnown = true;
        const bool bWalked = ForEachFactoryParameters(
            *const_cast<FShaderMapContent*>(static_cast<const FShaderMapBase&>(Map).GetContent()), ContentLayout,
            [&](const FShader& Shader, const FFactoryParametersPtr& Frozen)
            {
                const FTypeLayoutDesc* Layout = P4SwitchFactoryParameterLayout(Shader, Map.GetPointerTable());
                FVertexFactoryShaderParameters* Copy = nullptr;
                if (Frozen.Get())
                {
                    if (!Layout)
                    {
                        bKnown = false;
                        return;
                    }
                    void* Memory = FMemory::Malloc(Layout->Size, Layout->Alignment);
                    Context.UnfreezeObject(Frozen.Get(), *Layout, Memory);
                    Copy = static_cast<FVertexFactoryShaderParameters*>(Memory);
                }
                Parameters.Add(Copy);
            });
        if (!bWalked || !bKnown)
        {
            for (FVertexFactoryShaderParameters* Copy : Parameters)
                FMemory::Free(Copy);
            Error = TEXT("switch-shader-map-factory-parameters-unknown");
            return false;
        }
    }
    FShaderMapContent* Content = Map.GetMutableContent();
    int32 Next = 0;
    ForEachFactoryParameters(*Content, ContentLayout,
        [&](const FShader&, FFactoryParametersPtr& Unfrozen)
        {
            if (Unfrozen.Get())
                FMemory::Free(Unfrozen.Get());
            Unfrozen = FFactoryParametersPtr(Next < Parameters.Num() ? Parameters[Next] : nullptr);
            ++Next;
        });
    if (Next != Parameters.Num())
    {
        Error = TEXT("switch-shader-map-factory-parameters-count");
        return false;
    }
    auto* MeshMaps = static_cast<TMemoryImageArray<TMemoryImagePtr<FMeshMaterialShaderMap>>*>(
        LayoutField(Content, ContentLayout, TEXT("OrderedMeshShaderMaps")));
    auto* ContentHash = static_cast<FSHAHash*>(LayoutField(Content, ContentLayout, TEXT("ShaderContentHash")));
    if (!MeshMaps || !ContentHash)
    {
        Error = TEXT("switch-shader-map-content-fields-unknown");
        return false;
    }
    for (const auto& Each : Drop)
        for (FMeshMaterialShaderMap* Mesh : *MeshMaps)
            if (Mesh->GetVertexFactoryTypeName() == Each.Get<0>())
                Mesh->RemoveShaderTypePermutaion(Each.Get<1>(), Each.Get<2>());
    for (const auto& Each : DropOwn)
        Content->RemoveShaderTypePermutaion(Each.Get<0>(), Each.Get<1>());
    const FShaderMapResourceCode* Code = Map.GetResourceCode();
    FSHA1 Hasher;
    Content->Finalize(Code);
    Content->UpdateHash(Hasher);
    for (FMeshMaterialShaderMap* Mesh : *MeshMaps)
    {
        Mesh->Finalize(Code);
        Mesh->UpdateHash(Hasher);
    }
    Hasher.Final();
    Hasher.GetHash(ContentHash->Hash);
    return true;
}
} //namespace

bool P4FreezeSwitchShaderMap(const FMaterialResource& Resource, const TArray<uint8>& IdHash,
    const TArray<uint8>& LibraryHash, TArray<FName>& Names, TArray<uint8>& Out, FString& Error)
{
    FMaterialShaderMap* Map = Resource.GetGameThreadShaderMap();
    if (!Map || !Map->IsCompilationFinalized() || !Map->CompiledSuccessfully() || !Map->GetContent())
    {
        Error = TEXT("switch-shader-map-not-compiled");
        return false;
    }
    if (IdHash.Num() != 20 || LibraryHash.Num() != 20)
    {
        Error = TEXT("switch-shader-map-hash-size");
        return false;
    }
    // Refuse a stale/unfiltered map. Never repair per-material policy by mutating a
    // possibly shared cached map, and do not hide a wrong pre-freeze shader index.
    if (!IsGameSSR(Resource.GetMaterial(), Resource.GetBlendMode()))
    {
        TMap<FShaderId, TShaderRef<FShader>> Shaders;
        Map->GetShaderList(Shaders);
        for (const auto& Pair : Shaders)
            if (IsGameXluShader(Pair.Key.Type))
            {
                Error = TEXT("switch-shader-map-false-ssr-has-xlu-shader");
                return false;
            }
    }
    if (!P4InstallSwitchGpuFactoryParameters(Error))
        return false;
    // The content's type (FMaterialShaderMapContent; its layout is not exported, so found by its name).
    const FTypeLayoutDesc* ContentLayout = FTypeLayoutDesc::Find(
        FHashedName(TEXT("FMaterialShaderMapContent")).GetHash());
    if (!ContentLayout)
    {
        Error = TEXT("switch-shader-map-content-layout-unknown");
        return false;
    }

    bool ParametersValid = true;
    if (!ForEachFactoryParameters(
            *const_cast<FShaderMapContent*>(static_cast<const FShaderMapBase&>(*Map).GetContent()), *ContentLayout,
            [&](const FShader& Shader, const FFactoryParametersPtr& Parameters)
            {
                if (ParametersValid)
                    ParametersValid = P4ValidateSwitchGpuFactoryParameters(
                        Shader, Map->GetPointerTable(), Parameters.Get(), Error);
            }) ||
        !ParametersValid)
    {
        if (Error.IsEmpty())
            Error = TEXT("switch-shader-map-factory-parameters-fields");
        return false;
    }
    int32 Removed = 0;
    if (!PruneGameAbsentShaders(*Map, *ContentLayout, Removed, Error))
        return false;

    FNameIndexWriter Ar(Out, Names);
    uint32 Cooked = 1, Valid = 1;
    int32 Quality = int32(Resource.GetQualityLevel()), Feature = int32(Resource.GetFeatureLevel());
    Ar << Cooked << Valid << Quality << Feature;
    Ar.Serialize(const_cast<uint8*>(IdHash.GetData()), 20);

    FMemoryImage Image;
    Image.TargetLayoutParameters.MaxFieldAlignment = 0xffffffffu;
    Image.TargetLayoutParameters.Flags = FPlatformTypeLayoutParameters::Flag_Initialized;
    Ar << Image.TargetLayoutParameters.MaxFieldAlignment << Image.TargetLayoutParameters.Flags;

    // As FShaderMapBase::Serialize saving, with the game's layout in place of the archive's.
    FShaderMapPointerTable Table;
    Image.PrevPointerTable = &Map->GetPointerTable();
    Image.PointerTable = &Table;
    FMemoryImageWriter Writer(Image);
    Writer.WriteObject(Map->GetContent(), *ContentLayout);
    FMemoryImageResult Result;
    Image.Flatten(Result, true);
    uint32 Size = uint32(Result.Bytes.Num());
    Ar << Size;
    Ar.Serialize(Result.Bytes.GetData(), Size);
    Result.SaveToArchive(Ar);
    Table.SaveToArchive(Ar, Result.Bytes.GetData(), false);
    int32 Dependencies = Image.TypeDependencies.Num();
    Ar << Dependencies;
    for (const FTypeLayoutDesc* Type : Image.TypeDependencies)
    {
        uint64 NameHash = Type->NameHash;
        FSHAHash LayoutHash;
        uint32 LayoutSize = Freeze::HashLayout(*Type, Image.TargetLayoutParameters, LayoutHash);
        Ar << NameHash << LayoutSize << LayoutHash;
    }
    // The code is in the shader library, found by the map's hash.
    uint32 ShareCode = 1;
    Ar << ShareCode;
    Ar.Serialize(const_cast<uint8*>(LibraryHash.GetData()), 20);
    return true;
}

namespace
{
IKadanSwitchCompileContext* SwitchCompileContext()
{
    IModularFeatures& Features = IModularFeatures::Get();
    return Features.IsModularFeatureAvailable(IKadanSwitchCompileContext::FeatureName())
        ? &Features.GetModularFeature<IKadanSwitchCompileContext>(IKadanSwitchCompileContext::FeatureName())
        : nullptr;
}
} //namespace

FP4SwitchMaterialContext::FP4SwitchMaterialContext(const UMaterial* Material)
{
    FString FactoryError;
    if (!P4InstallSwitchGpuFactoryParameters(FactoryError))
        UE_LOG(LogTemp, Error, TEXT("Kadan: GPU sprite factory parameters were not installed: %s"), *FactoryError);
    const UP4MaterialGameSettings* Settings = Material ? UP4MaterialGameSettings::Find(Material) : nullptr;
    const bool bMaterialSSR = Material && Settings && IsTranslucentBlendMode(Material->GetBlendMode()) &&
        Material->bScreenSpaceReflections && Settings->C4MaterialDrawOrder == C4MDO_XluUseSceneColor;
    if (IKadanSwitchCompileContext* Context = SwitchCompileContext())
        Context->SetMaterialSSR(bMaterialSSR);
}

FP4SwitchMaterialContext::~FP4SwitchMaterialContext()
{
    if (IKadanSwitchCompileContext* Context = SwitchCompileContext())
        Context->SetMaterialSSR(false);
}

FMaterialResource* P4CacheMaterialForPlatform(
    UMaterial* Material, EShaderPlatform Platform, UMaterialInstance* Instance)
{
    if (!Material)
        return nullptr;
    // A derived UMaterial can override AllocateResource. Do not silently discard
    // its resource/compiler behavior; the owned route currently supports stock UMaterial.
    if (Platform == SP_SWITCH && Material->GetClass() != UMaterial::StaticClass())
    {
        UE_LOG(LogTemp, Error, TEXT("Kadan: GLSL_SWITCH does not support custom material resource allocators (%s)"),
            *Material->GetClass()->GetPathName());
        return nullptr;
    }
    FP4SwitchMaterialContext Context(Material);
    FMaterialResource* Resource = Platform == SP_SWITCH
        ? static_cast<FMaterialResource*>(new FP4SwitchMaterialResource(Material, Instance))
        : Material->AllocateResource();
    // The game renders at material quality High (r.MaterialQualityLevel 1, read live in Eden; none of the game's configs
    // or device profiles, console or handheld, sets another). For a material whose Quality Switch uses
    // that level, its runtime asks for a High resource only (Material.cpp FindOrCreateMaterialResource, no fallback to
    // the default quality), so the one resource exported is compiled and named for High. Otherwise the default quality,
    // which the game uses at every level the material does not use.
    EMaterialQualityLevel::Type Quality = EMaterialQualityLevel::Num;
    if (Platform == SP_SWITCH)
    {
        // As stock's quality level usage (not exported): a level is used when a Quality Switch has its input.
        TArray<UMaterialExpressionQualitySwitch*> Switches;
        Material->GetAllExpressionsInMaterialAndFunctionsOfType(Switches);
        for (UMaterialExpressionQualitySwitch* Switch : Switches)
            if (Switch && Switch->Inputs[EMaterialQualityLevel::High].GetTracedInput().Expression)
                Quality = EMaterialQualityLevel::High;
    }
    Resource->SetMaterial(Material, Instance, GetMaxSupportedFeatureLevel(Platform), Quality);
    Resource->CacheShaders(Platform, nullptr);
    return Resource;
}

void P4ReleaseMaterialResource(FMaterialResource* Resource)
{
    if (!Resource)
    {
        return;
    }
    // With no target platform the compiled map is applied for rendering: SetShaderMapsOnMaterialResources
    // enqueues a command that writes the resource's rendering-thread map. Render commands run in order,
    // so a command of its own frees the resource after that one, as UE frees unused material resources.
    ENQUEUE_RENDER_COMMAND(P4ReleaseMaterialResource)([Resource](FRHICommandListImmediate&) { delete Resource; });
}

namespace
{
// UE's shader compile workers (ShaderCompileWorker.exe) load only the engine's own shader formats: given a GLSL_SWITCH
// job they answer "Can't compile shaders for format GLSL_SWITCH" and the editor fails reading their results. Kadan's
// format compiles in the editor's process (the Kadan module holds its compiler), so while a material is compiled for
// the game the manager compiles locally, as UE itself switches it when its workers fail; the flag is restored after.
// FShaderCompilingManager::bAllowCompilingThroughWorkers is private: reached through the explicit-instantiation idiom.
struct FWorkersFlag
{
    using Type = bool FShaderCompilingManager::*;
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
template struct TMemberGrant<FWorkersFlag, &FShaderCompilingManager::bAllowCompilingThroughWorkers>;

using FCompileInEditorProcess = FP4SwitchShadersInEditorProcess;
} //namespace

FP4SwitchShadersInEditorProcess::FP4SwitchShadersInEditorProcess()
{
    if (!GShaderCompilingManager || !TMemberOf<FWorkersFlag>::Pointer)
        return;
    GShaderCompilingManager->FinishAllCompilation(); // nothing queued before is left for a worker
    bool& Allow = GShaderCompilingManager->*TMemberOf<FWorkersFlag>::Pointer;
    bWas = Allow;
    Allow = false;
    bHeld = true;
}

FP4SwitchShadersInEditorProcess::~FP4SwitchShadersInEditorProcess()
{
    if (!bHeld || !GShaderCompilingManager)
        return;
    GShaderCompilingManager->FinishAllCompilation();
    GShaderCompilingManager->*TMemberOf<FWorkersFlag>::Pointer = bWas;
}

bool P4UseSwitchShaderRoute()
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SWITCH_ROUTE")) == TEXT("0"))
        return false;
    return FModuleManager::Get().IsModuleLoaded(TEXT("KadanShaderFormat"));
}

namespace
{
using P4Bytes = std::vector<std::uint8_t>;
P4Bytes Std(const TArray<uint8>& In)
{
    return P4Bytes(In.GetData(), In.GetData() + In.Num());
}
TArray<uint8> Ue(const P4Bytes& In)
{
    return TArray<uint8>(In.data(), int32(In.size()));
}
std::string Utf8(const FString& Text)
{
    FTCHARToUTF8 Value(*Text);
    return std::string(Value.Get(), Value.Length());
}

bool Uncompressed(const FShaderMapResourceCode::FShaderEntry& Entry, TArray<uint8>& Out)
{
    if (Entry.Code.Num() == Entry.UncompressedSize)
    {
        Out = Entry.Code;
        return true;
    }
    Out.SetNumUninitialized(Entry.UncompressedSize);
    return FCompression::UncompressMemory(
        NAME_LZ4, Out.GetData(), Entry.UncompressedSize, Entry.Code.GetData(), Entry.Code.Num());
}
} //namespace

FP4CreatorMaterialCompileResult P4CompileCreatorMaterialSwitch(const FP4CreatorMaterialCompileRequest& Request)
{
    namespace OwnMap = P4MapKit::CustomMaterial::OwnMap;
    FP4CreatorMaterialCompileResult Out;
    auto Fail = [&Out](const FString& Why)
    {
        Out = FP4CreatorMaterialCompileResult();
        Out.Error = Why;
        return Out;
    };
    if (!IsInGameThread() || !IsValid(Request.CreatorMaterial))
        return Fail(TEXT("Choose a material authored in this mod."));
    FString FactoryError;
    if (!P4InstallSwitchGpuFactoryParameters(FactoryError))
        return Fail(FactoryError);
    // The runtime material's usage, on a transient copy (its C4 settings come with it).
    TStrongObjectPtr<UMaterial> Copy(DuplicateObject<UMaterial>(Request.CreatorMaterial, GetTransientPackage(),
        FName(*(TEXT("KadanCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)))));
    if (!Copy.IsValid())
        return Fail(TEXT("The material could not be copied for compiling."));
    Copy->ClearFlags(RF_Public | RF_Standalone);
    P4ApplyMaterialInstanceC4ForCompile(Request.CreatorInstance, Copy.Get());
    Copy->bUsedWithStaticLighting = true;
    Copy->bUsedWithInstancedStaticMeshes = Request.Instanced;
    P4ApplyMaterialFactoryUsage(Copy.Get(), Request.Factories);
    P4ApplyGameRenderLimits(Copy.Get());
    P4ApplyGameRefraction(Copy.Get());
    // Compile a private copy of the complete chain. Flattening local overrides loses inherited
    // switches, layer associations and base-property overrides from intermediate instances.
    TArray<UMaterialInstanceConstant*> Chain;
    TSet<UMaterialInterface*> Seen;
    for (UMaterialInterface* At = Request.CreatorInstance; At;)
    {
        if (Seen.Contains(At) || Seen.Num() >= 64)
            return Fail(TEXT("The material instance parent chain is cyclic or too deep."));
        Seen.Add(At);
        if (At == Request.CreatorMaterial)
            break;
        auto* Instance = Cast<UMaterialInstanceConstant>(At);
        if (!Instance)
            return Fail(TEXT("The material instance needs a recovered or authored master graph."));
        Chain.Add(Instance);
        At = Instance->Parent;
        if (!At)
            return Fail(TEXT("The material instance has no master graph."));
    }
    TArray<TStrongObjectPtr<UMaterialInstanceConstant>> Copies;
    UMaterialInterface* Parent = Copy.Get();
    for (int32 Index = Chain.Num(); Index-- > 0;)
    {
        auto* Instance = DuplicateObject<UMaterialInstanceConstant>(Chain[Index], GetTransientPackage(),
            FName(*(TEXT("KadanInstanceCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
        if (!Instance)
            return Fail(TEXT("The material instance could not be copied for compilation."));
        Instance->ClearFlags(RF_Public | RF_Standalone);
        Copies.Emplace(Instance);
        Instance->SetParentEditorOnly(Parent);
        if (const auto* Settings = UP4MaterialInstanceGameSettings::Find(Instance))
        {
            Settings->Publish();
        }
        Instance->UpdateOverridableBaseProperties();
        P4MaterialInstancePrepareRecoveredViewPermutation(Chain[Index], Instance);
        Parent = Instance;
    }
    // Stock 4.26 draws a volumetric distance function decal only in its deferred renderer, into the GBuffer; a forward
    // renderer (the game's, or a forward-shading UE4 project) draws no such decal.
    if (Copy->MaterialDomain == MD_DeferredDecal && Copy->DecalBlendMode == DBM_Volumetric_DistanceFunction)
        return Fail(TEXT("Volumetric distance function decals draw only in UE4's deferred renderer. The game renders ")
                    TEXT("forward, where UE4 draws no such decal either. Use a DBuffer decal blend mode."));
    // The game's scene reads anywhere in the graph, material functions included, where the nodes cannot see the
    // material they compile for: only translucent and post process materials, with the instance chain's blend mode.
    {
        TArray<UMaterialExpressionKadanSceneDepth*> Depths;
        TArray<UMaterialExpressionKadanSceneColor*> Colors;
        Copy->GetAllExpressionsInMaterialAndFunctionsOfType(Depths);
        Copy->GetAllExpressionsInMaterialAndFunctionsOfType(Colors);
        const UMaterialInterface* Effective = Parent;
        if ((Depths.Num() || Colors.Num()) && Copy->MaterialDomain != MD_PostProcess &&
            !IsTranslucentBlendMode(Effective->GetBlendMode()))
            return Fail(TEXT("Only translucent and post process materials can read the game's scene depth or color."));
    }
    FMaterialResource* Resource = nullptr;
    {
        FCompileInEditorProcess Local;
        Resource = P4CacheMaterialForPlatform(Copy.Get(), SP_SWITCH, Copies.Num() ? Copies.Last().Get() : nullptr);
        if (!Resource)
            return Fail(TEXT("This material class uses an unsupported shader resource allocator."));
        if (GShaderCompilingManager)
            GShaderCompilingManager->FinishAllCompilation();
        Resource->FinishCompilation();
    }
    ON_SCOPE_EXIT
    {
        P4ReleaseMaterialResource(Resource);
    };
    FMaterialShaderMap* Map = Resource->GetGameThreadShaderMap();
    if (!Map || !Map->IsCompilationFinalized() || !Map->CompiledSuccessfully())
    {
        FString Why = TEXT("The material did not compile for the game.");
        for (const FString& Line : Resource->GetCompileErrors())
            Why += TEXT(" ") + Line;
        return Fail(Why);
    }
    // The programs, in the order the map's shaders name them (their resource index).
    const FShaderMapResourceCode* Code = Map->GetResourceCode();
    P4Bytes Named(OwnMap::Text("kadan-switch-map"));
    for (const FShaderMapResourceCode::FShaderEntry& Entry : Code->ShaderEntries)
    {
        TArray<uint8> Program;
        if (!Uncompressed(Entry, Program))
            return Fail(TEXT("A compiled program of the material could not be read."));
        Out.Programs.push_back(Std(Program));
        OwnMap::PutBytes(Named, P4MapKit::CustomMaterial::Sha1(Out.Programs.back()));
    }
    Out.MapHash = P4MapKit::CustomMaterial::Sha1(Named);

    // Once for its names, which the block keeps sorted without case (as the game's maps), and again with them.
    TArray<uint8> First, Zero, Resource1;
    Zero.SetNumZeroed(20);
    TArray<FName> Names;
    FString Error;
    if (!P4FreezeSwitchShaderMap(*Resource, Zero, Ue(Out.MapHash), Names, First, Error))
        return Fail(Error);
    std::vector<std::string> Texts;
    for (const FName& Name : Names)
        Texts.push_back(Utf8(Name.ToString()));
    std::sort(Texts.begin(), Texts.end());
    std::stable_sort(Texts.begin(), Texts.end(),
        [](const std::string& A, const std::string& B) { return OwnMap::Lower(A) < OwnMap::Lower(B); });
    TArray<FName> Sorted;
    for (const std::string& Text : Texts)
        Sorted.Add(FName(UTF8_TO_TCHAR(Text.c_str())));
    // The map's id names its whole content, so two materials never share an id: the programs, the frozen map, and the
    // names its name indexes stand for (parameter names among them), in the order of those indexes.
    P4Bytes IdInput(OwnMap::Text("kadan-switch-id"));
    OwnMap::PutBytes(IdInput, Out.MapHash);
    OwnMap::PutBytes(IdInput, Std(First));
    OwnMap::Put32(IdInput, std::uint64_t(Names.Num()));
    for (const FName& Name : Names)
        OwnMap::PutBytes(IdInput, OwnMap::FString(Utf8(Name.ToString())));
    const P4Bytes IdHash = P4MapKit::CustomMaterial::Sha1(IdInput);
    Out.MapId = IdHash;
    TArray<uint8> Final;
    if (!P4FreezeSwitchShaderMap(*Resource, Ue(IdHash), Ue(Out.MapHash), Sorted, Final, Error))
        return Fail(Error);
    if (Sorted.Num() != int32(Texts.size()))
        return Fail(TEXT("The map's names changed between its two writes."));

    P4Bytes Block;
    OwnMap::Put32(Block, 1);
    OwnMap::Put32(Block, Texts.size());
    for (const std::string& Text : Texts)
    {
        OwnMap::PutBytes(Block, OwnMap::FString(Text));
        const auto [A, B] = OwnMap::NameHashes(Text);
        OwnMap::Put16(Block, A);
        OwnMap::Put16(Block, B);
    }
    OwnMap::Put32(Block, 1); // one location: its offset, feature and quality levels
    OwnMap::Put32(Block, 0);
    OwnMap::Put8(Block, std::uint64_t(Resource->GetFeatureLevel()));
    OwnMap::Put8(Block, std::uint64_t(Resource->GetQualityLevel()));
    OwnMap::Put32(Block, std::uint64_t(Final.Num()));
    OwnMap::PutBytes(Block, Std(Final));
    UE_LOG(LogTemp, Display, TEXT("Kadan: %s through UE's shaders: %d programs, map %s, block %d bytes (SHA-1 %s)"),
        *Request.CreatorMaterial->GetPathName(), int32(Out.Programs.size()), *BytesToHex(Out.MapHash.data(), 20),
        int32(Block.size()), *BytesToHex(P4MapKit::CustomMaterial::Sha1(Block).data(), 20));
    Out.Block = MoveTemp(Block);
    Out.Versions = int32(Map->GetContent()->GetNumShaders());
    Out.Succeeded = true;
    return Out;
}

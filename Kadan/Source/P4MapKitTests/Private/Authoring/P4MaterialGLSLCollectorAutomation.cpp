#if P4_MAPKIT_TESTS
#include "P4CreatorMaterialFactories.h"
#include "AssetToolsModule.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "MaterialShared.h"
#include "Shader.h"
#include "ShaderCompiler.h"
#include "ShaderCompilerCommon.h"
#include "VertexFactory.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
class FCollectedMaterialResource final : public FMaterialResource
{
public:
    FString Name;
    virtual FString GetFriendlyName() const override { return Name; }
};
struct FCollectedHlslccHeader : CrossCompiler::FHlslccHeader
{
    TArray<FString> ExternalTextures;
    virtual bool ParseCustomHeaderEntries(const ANSICHAR*& Source) override
    {
        const ANSICHAR* Prefix = "// @ExternalTextures: ";
        if (FCStringAnsi::Strncmp(Source, Prefix, FCStringAnsi::Strlen(Prefix)) != 0)
            return true;
        Source += FCStringAnsi::Strlen(Prefix);
        while (*Source && *Source != '\n')
        {
            FString TextureName;
            if (ExternalTextures.Num() >= 256 || !CrossCompiler::ParseIdentifier(Source, TextureName))
                return false;
            ExternalTextures.Add(TextureName);
            if (*Source == '\n')
            {
                ++Source;
                break;
            }
            if (*Source++ != ',')
                return false;
        }
        return true;
    }
};
struct FCollectedGLSL
{
    FString ShaderType, VertexFactory, Source, DebugFile, OutputHash, NativeBindings;
    int32 Permutation = 0;
    EShaderFrequency Frequency = SF_Pixel;
    FCollectedHlslccHeader Header;
};
bool CollectorTree(const FString& Path, const FString& Saved)
{
    const FString Full = FPaths::ConvertRelativePathToFull(Path);
    if (!FPaths::IsUnderDirectory(Full, Saved))
        return false;
#if PLATFORM_WINDOWS
    for (FString At = Full; !At.IsEmpty();)
    {
        const DWORD Attributes = GetFileAttributesW(*At);
        if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        const FString Parent = FPaths::GetPath(At);
        if (Parent == At)
            break;
        At = Parent;
    }
    TArray<FString> Pending = {Full};
    int32 Count = 0;
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        TArray<FString> Children;
        IFileManager::Get().FindFiles(Children, *(Pending[Index] / TEXT("*")), true, true);
        if ((Count += Children.Num()) > 65536)
            return false;
        for (const FString& Child : Children)
        {
            const FString File = Pending[Index] / Child;
            const DWORD Attributes = GetFileAttributesW(*File);
            if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                return false;
            if (Attributes & FILE_ATTRIBUTE_DIRECTORY)
                Pending.Add(File);
        }
    }
    return true;
#else
    return false;
#endif
}
const TCHAR* StageExtension(EShaderFrequency Stage)
{
    switch (Stage)
    {
        case SF_Vertex:
            return TEXT("vert");
        case SF_Pixel:
            return TEXT("frag");
        case SF_Hull:
            return TEXT("tesc");
        case SF_Domain:
            return TEXT("tese");
        case SF_Geometry:
            return TEXT("geom");
        case SF_Compute:
            return TEXT("comp");
        default:
            return nullptr;
    }
}

// Private compiler evidence only: no export route, actor action, source admission,
// runtime payload, or assertion that Vulkan and the game's bindings are equal.
bool CollectMaterialGLSL(UMaterial* Material, TArray<FCollectedGLSL>& Programs, FString& Error)
{
    Programs.Reset();
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Material) || !GShaderCompilingManager)
    {
        Error = TEXT("GLSL collection requires a live native material on the editor thread.");
        return false;
    }
    GShaderCompilingManager->FinishAllCompilation();
    IConsoleVariable* Dump = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugInfo"));
    IConsoleVariable* Short = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));
    if (!Dump || !Short)
    {
        Error = TEXT("The installed shader debug controls are unavailable.");
        return false;
    }
    const int32 BeforeDump = Dump->GetInt(), BeforeShort = Short->GetInt();
    const EConsoleVariableFlags DumpPriority = EConsoleVariableFlags(Dump->GetFlags() & ECVF_SetByMask);
    const EConsoleVariableFlags ShortPriority = EConsoleVariableFlags(Short->GetFlags() & ECVF_SetByMask);
    ON_SCOPE_EXIT
    {
        GShaderCompilingManager->FinishAllCompilation();
        Dump->ClearFlags(ECVF_SetByMask);
        Short->ClearFlags(ECVF_SetByMask);
        Dump->Set(BeforeDump, DumpPriority);
        Short->Set(BeforeShort, ShortPriority);
    };
    Dump->Set(1, ECVF_SetByConsole);
    Short->Set(0, ECVF_SetByConsole);
    FCollectedMaterialResource Resource;
    Resource.Name = TEXT("KadanGLSLCollect_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Resource.SetMaterial(Material, nullptr, ERHIFeatureLevel::SM5, EMaterialQualityLevel::High);
    const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    const FString DebugRoot = FPaths::ConvertRelativePathToFull(
        GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() /
        LegacyShaderPlatformToShaderFormat(SP_VULKAN_SM5).ToString());
    const FString ScopedDebug = DebugRoot / Resource.Name;
    if (!CollectorTree(ScopedDebug, Saved) || IFileManager::Get().DirectoryExists(*ScopedDebug))
    {
        Error = TEXT("The exact shader collection destination is outside project Saved or already exists.");
        return false;
    }
    ON_SCOPE_EXIT
    {
        // All jobs finish before removing this exact fresh resource's temporary
        // debug directory; retained input is a single separate owned folder.
        GShaderCompilingManager->FinishAllCompilation();
        if (CollectorTree(ScopedDebug, Saved))
            IFileManager::Get().DeleteDirectory(*ScopedDebug, false, true);
    };
    FMaterialShaderMapId Id;
    Resource.GetShaderMapId(SP_VULKAN_SM5, nullptr, Id);
    Id.BaseMaterialId = FGuid::NewGuid(); // Compile cache identity local to this resource.
    if (!Resource.CacheShaders(Id, SP_VULKAN_SM5))
    {
        Error = TEXT("The installed Vulkan material compiler refused this resource.");
        return false;
    }
    GShaderCompilingManager->FinishAllCompilation();
    FMaterialShaderMap* Map = Resource.GetGameThreadShaderMap();
    if (!Map || !Resource.HasValidGameThreadShaderMap())
    {
        Error = TEXT("The Vulkan resource did not compile. ") + FString::Join(Resource.GetCompileErrors(), TEXT("; "));
        return false;
    }
    TMap<FShaderId, TShaderRef<FShader>> Shaders;
    Map->GetShaderList(Shaders);
    FShaderMapResourceCode* Code = Map->GetResourceCode();
    if (!Code || Shaders.Num() == 0 || Shaders.Num() > 4096)
    {
        Error = TEXT("The Vulkan resource has no bounded shader identity table.");
        return false;
    }
    TArray<FString> HashFiles;
    IFileManager::Get().FindFilesRecursive(HashFiles, *ScopedDebug, TEXT("OutputHash.txt"), true, false);
    if (HashFiles.Num() == 0 || HashFiles.Num() > 4096)
    {
        Error = TEXT("The exact fresh Vulkan resource emitted no bounded debug output.");
        return false;
    }
    int64 Total = 0;
    for (const auto& Pair : Shaders)
    {
        const int32 Index = Pair.Value->GetResourceIndex();
        const EShaderFrequency Stage = Pair.Value->GetFrequency();
        const TCHAR* Extension = StageExtension(Stage);
        if (!Extension)
            continue;
        if (!Code->ShaderHashes.IsValidIndex(Index) || !Code->ShaderEntries.IsValidIndex(Index) ||
            Code->ShaderEntries[Index].Frequency != Stage)
        {
            Error = TEXT("The Vulkan shader identity and code tables disagree.");
            return false;
        }
        const FString Type = Pair.Key.Type->GetName();
        const FString Factory = Pair.Key.VFType ? Pair.Key.VFType->GetName() : FString();
        int32 Matches = 0;
        for (const FString& HashFile : HashFiles)
        {
            const FString Folder = FPaths::GetPath(HashFile);
            const FString Permutation = FPaths::GetCleanFilename(Folder);
            if (!(Permutation == FString::FromInt(Pair.Key.PermutationId) ||
                    Permutation.StartsWith(FString::FromInt(Pair.Key.PermutationId) + TEXT("_"))) ||
                FPaths::GetCleanFilename(FPaths::GetPath(Folder)) != Type)
                continue;
            if (!Factory.IsEmpty() && FPaths::GetCleanFilename(FPaths::GetPath(FPaths::GetPath(Folder))) != Factory)
                continue;
            FString Hash;
            if (!FFileHelper::LoadFileToString(Hash, *HashFile) ||
                !Hash.TrimStartAndEnd().Equals(Code->ShaderHashes[Index].ToString(), ESearchCase::IgnoreCase))
                continue;
            const FString SourceFile = Folder / (FString(TEXT("Output.")) + Extension);
            const int64 Size = IFileManager::Get().FileSize(*SourceFile);
            if (Size <= 0 || Size > 4 * 1024 * 1024 || Total + Size > 64 * 1024 * 1024)
            {
                Error = TEXT("The actual GLSL source exceeds the bounded collection size.");
                return false;
            }
            FCollectedGLSL Program;
            Program.ShaderType = Type;
            Program.VertexFactory = Factory;
            Program.Permutation = Pair.Key.PermutationId;
            Program.Frequency = Stage;
            Program.DebugFile = SourceFile;
            Program.OutputHash = Code->ShaderHashes[Index].ToString();
            Program.NativeBindings = TEXT("kind\tbuffer\tbase_index\tsize\n");
            const auto& MapInfo = Pair.Value->ParameterMapInfo;
            int32 BindingCount = 0;
            auto AppendBindings = [&](const TCHAR* Kind, const auto& Entries) -> bool
            {
                if ((BindingCount += Entries.Num()) > 4096)
                    return false;
                for (const auto& Entry : Entries)
                    Program.NativeBindings += FString::Printf(
                        TEXT("%s\t-\t%u\t%u\n"), Kind, uint32(Entry.BaseIndex), uint32(Entry.Size));
                return true;
            };
            if (!AppendBindings(TEXT("uniform_buffer"), MapInfo.UniformBuffers) ||
                !AppendBindings(TEXT("texture_sampler"), MapInfo.TextureSamplers) ||
                !AppendBindings(TEXT("srv"), MapInfo.SRVs) || MapInfo.LooseParameterBuffers.Num() > 256)
            {
                Error = TEXT("The typed native shader parameter table exceeds the collection limits.");
                return false;
            }
            for (const auto& Buffer : MapInfo.LooseParameterBuffers)
            {
                if ((BindingCount += Buffer.Parameters.Num()) > 4096)
                {
                    Error = TEXT("The typed native loose parameters exceed the collection limit.");
                    return false;
                }
                for (const auto& Parameter : Buffer.Parameters)
                    Program.NativeBindings += FString::Printf(TEXT("loose_parameter\t%u\t%u\t%u\n"),
                        uint32(Buffer.BaseIndex), uint32(Parameter.BaseIndex), uint32(Parameter.Size));
            }
            if (!FFileHelper::LoadFileToString(Program.Source, *SourceFile))
            {
                Error = TEXT("The actual GLSL debug source is unreadable.");
                return false;
            }
            FTCHARToUTF8 Utf8(*Program.Source);
            const ANSICHAR* HeaderSource = Utf8.Get();
            if (!Program.Header.Read(HeaderSource, Utf8.Length()))
            {
                Error = TEXT("The real Hlslcc resource metadata is unreadable.");
                return false;
            }
            if (Program.Header.Inputs.Num() > 256 || Program.Header.Outputs.Num() > 256 ||
                Program.Header.UniformBlocks.Num() > 256 || Program.Header.Samplers.Num() > 256 ||
                Program.Header.UAVs.Num() > 256 || Program.Header.PackedGlobals.Num() > 4096 ||
                Program.Header.PackedUBs.Num() > 256 || Program.Header.PackedUBCopies.Num() > 4096 ||
                Program.Header.PackedUBGlobalCopies.Num() > 4096)
            {
                Error = TEXT("The real Hlslcc metadata exceeds the inspection limits.");
                return false;
            }
            for (const auto& UB : Program.Header.PackedUBs)
                if (UB.Members.Num() > 4096)
                {
                    Error = TEXT("The real Hlslcc uniform members exceed the inspection limit.");
                    return false;
                }
            const int64 BindingBytes = int64(Program.NativeBindings.Len()) * sizeof(TCHAR);
            if (Total + Size + BindingBytes > 64 * 1024 * 1024)
            {
                Error = TEXT("The combined GLSL and native binding evidence exceeds the collection limit.");
                return false;
            }
            Total += Size + BindingBytes;
            Programs.Add(MoveTemp(Program));
            ++Matches;
        }
        if (!Matches)
        {
            Error =
                TEXT("No fresh GLSL output matches the exact resource, stage, shader type, factory and permutation. ") +
                Type;
            return false;
        }
    }
    // Persist only compiler-owned text inputs, in one owned reusable directory.
    const FString Input = Saved / TEXT("KadanGLSLCollector/input");
    const FString Owner = Input / TEXT("owner.txt");
    FString ExistingOwner;
    if (IFileManager::Get().DirectoryExists(*Input))
    {
        if (!FFileHelper::LoadFileToString(ExistingOwner, *Owner) ||
            ExistingOwner != TEXT("Kadan native material GLSL input\n") || !CollectorTree(Input, Saved))
        {
            Error = TEXT("The GLSL input folder is not owned by this collector.");
            return false;
        }
        if (!IFileManager::Get().DeleteDirectory(*Input, false, true))
        {
            Error = TEXT("The prior owned GLSL input cannot be replaced.");
            return false;
        }
    }
    if (!CollectorTree(Input, Saved) || !IFileManager::Get().MakeDirectory(*Input, true) ||
        !FFileHelper::SaveStringToFile(TEXT("Kadan native material GLSL input\n"), *Owner))
        return false;
    FString Manifest = TEXT("file\tstage\tshader_type\tvertex_factory\tpermutation\toutput_hash\tdebug_source\n");
    for (int32 Index = 0; Index < Programs.Num(); ++Index)
    {
        auto& Program = Programs[Index];
        const FString File = Input / FString::Printf(TEXT("program_%04d.%s"), Index, StageExtension(Program.Frequency));
        if (!FFileHelper::SaveStringToFile(Program.Source, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            return false;
        if (!FFileHelper::SaveStringToFile(Program.NativeBindings, *FPaths::ChangeExtension(File, TEXT("bindings.tsv")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            return false;
        Manifest += FString::Printf(TEXT("%s\t%s\t%s\t%s\t%d\t%s\t%s\n"), *FPaths::GetCleanFilename(File),
            StageExtension(Program.Frequency), *Program.ShaderType, *Program.VertexFactory, Program.Permutation,
            *Program.OutputHash, *Program.DebugFile);
        Program.DebugFile = File;
    }
    if (!FFileHelper::SaveStringToFile(
            Manifest, *(Input / TEXT("programs.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return false;
    return Programs.Num() > 0;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialGLSLCollectorTest, "P4MapKit.Kadan.MaterialGLSLCollector",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialGLSLCollectorTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_COLLECT_MATERIAL_GLSL")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "The private generic GLSL collector was not exercised. Set P4_COLLECT_MATERIAL_GLSL=1 for its scheduled native run."));
        return true;
    }
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Material(Cast<UMaterial>(Factory->FactoryCreateNew(UMaterial::StaticClass(),
        GetTransientPackage(), FName(TEXT("KadanGenericVulkanMaterial")), RF_Transient, nullptr, GWarn)));
    if (!TestNotNull(TEXT("The native material factory creates the generic source"), Material.Get()))
        return false;
    Material->BlendMode = BLEND_Opaque;
    Material->SetShadingModel(MSM_DefaultLit);
    auto* Color = NewObject<UMaterialExpressionConstant3Vector>(Material.Get());
    Color->Constant = FLinearColor(.2f, .4f, .7f);
    auto* Metallic = NewObject<UMaterialExpressionScalarParameter>(Material.Get());
    Metallic->ParameterName = TEXT("CreatorMetallic");
    Metallic->DefaultValue = .6f;
    Material->Expressions = {Color, Metallic};
    Material->BaseColor.Expression = Color;
    Material->Metallic.Expression = Metallic;
    Material->PostEditChange();
    const FGuid State = Material->StateId;
    const bool Dirty = Material->GetOutermost()->IsDirty();
    const FGuid ParameterGuid = Metallic->ExpressionGUID;
    const auto Domain = Material->MaterialDomain;
    const auto Blend = Material->BlendMode;
    const auto ShadingModels = Material->GetShadingModels();
    const bool TwoSided = Material->TwoSided, TangentNormal = Material->bTangentSpaceNormal;
    const float Clip = Material->OpacityMaskClipValue;
    const TArray<UMaterialExpression*> Expressions = Material->Expressions;
    IConsoleVariable* DumpControl = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugInfo"));
    IConsoleVariable* ShortControl = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));
    if (!TestNotNull(TEXT("The installed compiler exposes its dump control"), DumpControl) ||
        !TestNotNull(TEXT("The installed compiler exposes its debug name control"), ShortControl))
        return false;
    const int32 Dump = DumpControl->GetInt();
    const int32 Short = ShortControl->GetInt();
    const uint32 DumpPriority = DumpControl->GetFlags() & ECVF_SetByMask,
                 ShortPriority = ShortControl->GetFlags() & ECVF_SetByMask;
    TArray<FCollectedGLSL> Programs;
    FString Error;
    const bool Collected = CollectMaterialGLSL(Material.Get(), Programs, Error);
    TestEqual(TEXT("The collector preserves authored material identity"), Material->StateId, State);
    TestEqual(TEXT("The collector preserves authored dirty state"), Material->GetOutermost()->IsDirty(), Dirty);
    TestEqual(TEXT("The collector preserves the authored parameter GUID"), Metallic->ExpressionGUID, ParameterGuid);
    TestTrue(TEXT("The collector preserves authored domain, blend and shading models"),
        Material->MaterialDomain == Domain && Material->BlendMode == Blend &&
            Material->GetShadingModels() == ShadingModels);
    TestTrue(TEXT("The collector preserves authored sidedness, normal space and mask clip"),
        Material->TwoSided == TwoSided && Material->bTangentSpaceNormal == TangentNormal &&
            Material->OpacityMaskClipValue == Clip);
    TestTrue(TEXT("The collector preserves the exact authored graph and connected inputs"),
        Material->Expressions == Expressions && Material->BaseColor.Expression == Color &&
            Material->Metallic.Expression == Metallic);
    TestEqual(TEXT("The collector restores the shader dump control"), DumpControl->GetInt(), Dump);
    TestEqual(TEXT("The collector restores the debug name control"), ShortControl->GetInt(), Short);
    TestTrue(TEXT("The collector restores both original console priorities"),
        uint32(DumpControl->GetFlags() & ECVF_SetByMask) == DumpPriority &&
            uint32(ShortControl->GetFlags() & ECVF_SetByMask) == ShortPriority);
    if (!TestTrue(TEXT("A genuine generic material emits current Vulkan GLSL and compiler metadata"), Collected))
    {
        AddError(Error);
        return false;
    }
    int32 Pixels = 0, ResourceBlocks = 0;
    for (const auto& Program : Programs)
    {
        Pixels += Program.Frequency == SF_Pixel;
        ResourceBlocks += Program.Header.UniformBlocks.Num() + Program.Header.PackedUBs.Num();
    }
    TestTrue(
        TEXT("The exact resource includes pixel programs and real uniform metadata"), Pixels > 0 && ResourceBlocks > 0);
    AddInfo(FString::Printf(
        TEXT(
            "Collected %d actual Vulkan GLSL programs (%d pixels). Game binding reconciliation and NVIDIA compilation remain untested."),
        Programs.Num(), Pixels));
    return !HasAnyErrors();
}
#endif

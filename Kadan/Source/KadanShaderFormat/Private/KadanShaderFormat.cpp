// Kadan's GLSL_SWITCH shader format (stage 8.5 of tools/navigation/OWN_SHADER_MAPS.md).
//
// Stock UE 4.26 knows the Switch as shader platform SP_SWITCH (format GLSL_SWITCH): its renderer chooses the game's
// permutations for it and its shader source has the Switch branches. Only the format module, Nintendo's, is missing
// from a stock install. This one supplies it: UE's own shader source for each permutation, preprocessed for the
// Switch, DXC (UE's ShaderConductor) to SPIR-V, then Kadan's SPIR-V front end and Maxwell compiler
// (IKadanSwitchShaderCompiler, in the Kadan module) give the game's payload. The parameter map and resource tables
// follow UE's OpenGL format, whose binding model the game's renderer has: real uniform buffers, textures bound through
// each buffer's resource table, loose parameters in a packed array uploaded to a buffer slot of its own.
#include "CoreMinimal.h"
#include "Misc/CoreDelegates.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCompilerCommon.h"
#include "ShaderCore.h"
#include "ShaderPreprocessor.h"
#include "hlslcc.h"
#include "KadanSwitchShaderCompiler.h"

// RenderUtils.h declares these inside its inline accessors only.
extern RENDERCORE_API uint64 GForwardShadingPlatformMask;
extern RENDERCORE_API uint64 GDBufferPlatformMask;
// KadanGameShaderTypes.cpp
bool KadanCompileGameOnlyStockShaders();
int32 KadanRegisterTranslucencyPassTypes();
void KadanSetMaterialSSR(bool bMaterialSSR);

namespace
{

const FName NAME_GLSL_SWITCH(TEXT("GLSL_SWITCH"));

// Bumped when this file's output changes; the compiler's own version is added to it.
constexpr uint32 FormatVersion = 28; // Round of a vector constant rounded as UE's CPU rounds it too.

// The uniform buffer slot the game's renderer keeps for its own handles (textures, samplers): never a program's.
constexpr int32 DriverBufferSlot = 14;

// Loose parameters: one packed array of type 'h' (32-bit words; the renderer copies bytes), packed array index 0,
// and the integer ones in an array of type 'u', index 4 ("$Globals.u"), as the game's compiler packs them: the 'u'
// array in the first slot after the struct buffers, the 'h' array in the next.
constexpr uint8 LoosePackedType = 'h';
constexpr uint8 LoosePackedIndex = 0;
constexpr uint8 LooseIntegerType = 'u';
constexpr uint8 LooseIntegerIndex = 4;

const IKadanSwitchShaderCompiler* FindCompiler()
{
    IModularFeatures& Features = IModularFeatures::Get();
    if (!Features.IsModularFeatureAvailable(IKadanSwitchShaderCompiler::FeatureName()))
        return nullptr;
    return &Features.GetModularFeature<IKadanSwitchShaderCompiler>(IKadanSwitchShaderCompiler::FeatureName());
}

// The game's changes to UE 4.26's shader source (KadanSwitchSourcePatches.inl), applied to each job's preprocessed
// source. KADAN_SWITCH_PATCHES=<file> replaces them with a file's, for trying patches without a build.
//   @@ <name> [requires <text>] [unless <text>]
//                                    a patch; with requires, only for a source that holds <text>, which must hold the find
//                                    text; with unless, never for a source that holds that <text>
//   --- find                         the exact lines to find (every occurrence is replaced)
//   +++ replace                      the lines that replace them, up to the next @@
struct FSourcePatch
{
    FString Name, Requires, Unless, Find, Replace;
};

const TCHAR* const BuiltInPatchParts[] = {
#include "KadanSwitchSourcePatches.inl"
};

const FString& BuiltInPatches()
{
    static const FString Joined = []
    {
        FString Out;
        for (const TCHAR* Part : BuiltInPatchParts)
            Out += Part;
        return Out;
    }();
    return Joined;
}

TArray<FSourcePatch> ParsePatches(const FString& Text)
{
    TArray<FSourcePatch> Out;
    TArray<FString> Lines;
    Text.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), false);
    int32 Part = 0; // 1 find, 2 replace
    TArray<FString> Find, Replace;
    auto Finish = [&]()
    {
        if (Out.Num())
        {
            Out.Last().Find = FString::Join(Find, TEXT("\n"));
            Out.Last().Replace = FString::Join(Replace, TEXT("\n"));
        }
        Find.Reset();
        Replace.Reset();
        Part = 0;
    };
    for (const FString& Line : Lines)
    {
        if (Line.StartsWith(TEXT("@@ ")))
        {
            Finish();
            FSourcePatch Patch;
            FString Head = Line.Mid(3);
            int32 At = Head.Find(TEXT(" unless "));
            if (At != INDEX_NONE)
            {
                Patch.Unless = Head.Mid(At + 8);
                Head = Head.Left(At);
            }
            At = Head.Find(TEXT(" requires "));
            if (At != INDEX_NONE)
            {
                Patch.Requires = Head.Mid(At + 10);
                Head = Head.Left(At);
            }
            Patch.Name = Head.TrimStartAndEnd();
            Out.Add(Patch);
        }
        else if (Line == TEXT("--- find"))
            Part = 1;
        else if (Line == TEXT("+++ replace"))
            Part = 2;
        else if (Part == 1)
            Find.Add(Line);
        else if (Part == 2)
            Replace.Add(Line);
    }
    Finish();
    return Out;
}

// A Round node of a constant: UE writes a constant uniform expression's code into the shader, "round(0.50000000)".
// UE rounds the same value given by a parameter on the CPU, half up (FMaterialUniformExpressionRound,
// FMath::RoundToFloat), and the editor's compiler folds the literal to that value too; DXC rounds a half to even. A
// round() whose argument holds only numbers and vector constructors is rounded half up: a vector constant's
// MaterialFloat3(...) reaches here preprocessed, as float3 or half3. Engine code's round() always has a variable in
// it and keeps the GPU's rounding, as the game's own programs have it.
void RoundConstantsAsUE(FString& Source)
{
    static const FString Call = TEXT("round(");
    bool bAny = false;
    for (int32 From = 0;;)
    {
        const int32 At = Source.Find(Call, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
        if (At == INDEX_NONE)
            break;
        From = At + Call.Len();
        if (At > 0 && (FChar::IsAlnum(Source[At - 1]) || Source[At - 1] == TEXT('_')))
            continue;
        int32 Depth = 1, End = From;
        for (; End < Source.Len() && Depth; ++End)
            Depth += Source[End] == TEXT('(') ? 1 : Source[End] == TEXT(')') ? -1 : 0;
        if (Depth)
            break;
        FString Argument = Source.Mid(From, End - 1 - From);
        for (const TCHAR* Type : {TEXT("MaterialFloat"), TEXT("min16float"), TEXT("float"), TEXT("half")})
            Argument.ReplaceInline(Type, TEXT(""), ESearchCase::CaseSensitive);
        bool bConstant = !Argument.TrimStartAndEnd().IsEmpty();
        for (const TCHAR Char : Argument)
            bConstant &= !FChar::IsAlpha(Char) && Char != TEXT('_');
        if (!bConstant)
            continue;
        Source = Source.Left(At) + TEXT("C4RoundAsUE(") + Source.Mid(From);
        From = At + 12;
        bAny = true;
    }
    if (bAny)
        Source = TEXT("float C4RoundAsUE(float X) { return floor(X + 0.5); }\n"
                      "float2 C4RoundAsUE(float2 X) { return floor(X + 0.5); }\n"
                      "float3 C4RoundAsUE(float3 X) { return floor(X + 0.5); }\n"
                      "float4 C4RoundAsUE(float4 X) { return floor(X + 0.5); }\n") +
            Source;
}

bool ApplySourcePatches(FString& Source, FString& Error)
{
    static const TArray<FSourcePatch> BuiltIn = ParsePatches(BuiltInPatches());
    TArray<FSourcePatch> FromFile;
    const FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SWITCH_PATCHES"));
    if (!File.IsEmpty())
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *File))
        {
            Error = TEXT("Kadan's Switch format: cannot read ") + File;
            return false;
        }
        FromFile = ParsePatches(Text);
    }
    Source.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
    // Which base pass this is, for patches whose change differs between the two: KADAN_TRANSLUCENT in their code.
    // Both passes declare both passes' buffers; the forward lighting code reads its own pass's (UE's ForwardLightData).
    const bool bTranslucent = Source.Contains(
        TEXT("InitRectTexture( TranslucentBasePass_"), ESearchCase::CaseSensitive);
    Source = FString(bTranslucent ? TEXT("#define KADAN_TRANSLUCENT 1\n") : TEXT("#define KADAN_TRANSLUCENT 0\n")) +
        Source;
    for (const FSourcePatch& Patch : File.IsEmpty() ? BuiltIn : FromFile)
    {
        if (Patch.Find.IsEmpty())
            continue;
        if (!Patch.Requires.IsEmpty() && !Source.Contains(Patch.Requires, ESearchCase::CaseSensitive))
            continue;
        if (!Patch.Unless.IsEmpty() && Source.Contains(Patch.Unless, ESearchCase::CaseSensitive))
            continue;
        if (Source.Contains(Patch.Find, ESearchCase::CaseSensitive))
            Source.ReplaceInline(*Patch.Find, *Patch.Replace, ESearchCase::CaseSensitive);
        else if (!Patch.Requires.IsEmpty())
        {
            Error = FString::Printf(
                TEXT("Kadan's Switch format: the shader source no longer holds what patch '%s' changes."), *Patch.Name);
            return false;
        }
    }
    RoundConstantsAsUE(Source);
    return true;
}

// The game's own material settings (C4ShaderParam7 and the like, which stock UE's material has no field for), by
// material: KADAN_SWITCH_MATERIAL_SETTINGS=<file>, one line per material, "<material name>\t<Key>=<Value>\t...". Each
// is given to DXC as KADAN_<Key>; the patches default the ones a material does not set.
const TMap<FString, TArray<TPair<FString, FString>>>& MaterialSettings()
{
    static TMap<FString, TArray<TPair<FString, FString>>> Settings;
    static bool bRead = false;
    static FCriticalSection Lock;
    FScopeLock Scope(&Lock);
    if (bRead)
        return Settings;
    bRead = true;
    const FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SWITCH_MATERIAL_SETTINGS"));
    TArray<FString> Lines;
    if (File.IsEmpty() || !FFileHelper::LoadFileToStringArray(Lines, *File))
        return Settings;
    for (const FString& Line : Lines)
    {
        TArray<FString> Fields;
        Line.ParseIntoArray(Fields, TEXT("\t"));
        if (Fields.Num() < 2)
            continue;
        TArray<TPair<FString, FString>>& Values = Settings.FindOrAdd(Fields[0]);
        for (int32 i = 1; i < Fields.Num(); ++i)
        {
            FString Key, Value;
            if (Fields[i].Split(TEXT("="), &Key, &Value))
                Values.Add(TPair<FString, FString>(Key, Value));
        }
    }
    return Settings;
}

// The scene textures of the game's OpaqueBasePass (its SceneTextures member) that a scene read uses.
const TCHAR* const GroundSoftSceneTextures[] = {
    TEXT("SceneDepthTexture"), TEXT("PointClampSampler"), TEXT("SceneColorTexture")};

// A base pass pixel shader of an opaque or masked material the game draws after the ground (its C4MaterialDrawOrder
// GroundSoft, from the material's C4 settings).
bool IsGroundSoftOpaqueBasePass(const FShaderCompilerInput& Input)
{
    if (Input.Target.Frequency != SF_Pixel || !Input.VirtualSourceFilePath.EndsWith(TEXT("BasePassPixelShader.usf")))
        return false;
    const TMap<FString, FString>& Defines = Input.Environment.GetDefinitions();
    for (const TCHAR* Translucent :
        {TEXT("MATERIALBLENDING_TRANSLUCENT"), TEXT("MATERIALBLENDING_ADDITIVE"), TEXT("MATERIALBLENDING_MODULATE")})
        if (const FString* Value = Defines.Find(Translucent))
            if (*Value == TEXT("1"))
                return false;
    const FString* Order = Defines.Find(TEXT("KADAN_C4MaterialDrawOrder"));
    return Order && *Order == TEXT("1");
}

void AddError(FShaderCompilerOutput& Output, const FString& Message)
{
    FShaderCompilerError* Error = new (Output.Errors) FShaderCompilerError();
    Error->StrippedErrorMessage = Message;
}

// Each row with its resource's name: the struct in its buffer slot and UE's resource index name a member of the
// environment's resource table map.
void DecodeTable(const TArray<uint32>& Entries, const TArray<FString>& SlotNames,
    const TMap<FString, FResourceTableEntry>& ResourceTableMap, TArray<FKadanSwitchTableEntry>& Out)
{
    for (uint32 Entry : Entries)
    {
        if (Entry == FRHIResourceTableEntry::GetEndOfStreamToken())
            continue;
        FKadanSwitchTableEntry Row;
        Row.Buffer = FRHIResourceTableEntry::GetUniformBufferIndex(Entry);
        Row.Resource = FRHIResourceTableEntry::GetResourceIndex(Entry);
        Row.Slot = FRHIResourceTableEntry::GetBindIndex(Entry);
        const FString Struct = SlotNames.IsValidIndex(Row.Buffer) ? SlotNames[Row.Buffer] : FString();
        for (const auto& Pair : ResourceTableMap)
            if (Pair.Value.UniformBufferName == Struct && Pair.Value.ResourceIndex == Row.Resource)
            {
                Row.Name = Pair.Key;
                break;
            }
        Out.Add(Row);
    }
}

class FKadanShaderFormat : public IShaderFormat
{
public:
    virtual uint32 GetVersion(FName Format) const override
    {
        const IKadanSwitchShaderCompiler* Compiler = FindCompiler();
        // The patches' text too, so a change of them never reuses programs cached with the old ones.
        static const uint32 PatchesHash = FCrc::StrCrc32(*BuiltInPatches());
        // And the material settings file's: the cache keys a material's programs by the material, not by these.
        static const uint32 SettingsHash = []
        {
            FString Text;
            const FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SWITCH_MATERIAL_SETTINGS"));
            return File.IsEmpty() || !FFileHelper::LoadFileToString(Text, *File) ? 0u : FCrc::StrCrc32(*Text);
        }();
        return HashCombine(
            HashCombine(HashCombine(FormatVersion, Compiler ? Compiler->Version() : 0), PatchesHash), SettingsHash);
    }

    // The material's game-only settings, from Kadan, into the job's environment as KADAN_<name>: a job carries
    // them to wherever it compiles. Changing one changes the material's id, so its programs are compiled again.
    virtual void ModifyShaderCompilerInput(FShaderCompilerInput& Input) const override
    {
        IModularFeatures& Features = IModularFeatures::Get();
        if (!Features.IsModularFeatureAvailable(IKadanMaterialSettings::FeatureName()))
            return;
        FString Material, Rest;
        if (!Input.DebugGroupName.Split(TEXT("/"), &Material, &Rest))
            Material = Input.DebugGroupName;
        TArray<TPair<FString, FString>> Values;
        if (Features.GetModularFeature<IKadanMaterialSettings>(IKadanMaterialSettings::FeatureName())
                .Find(Material, Values))
            for (const TPair<FString, FString>& Value : Values)
                Input.Environment.SetDefine(*(TEXT("KADAN_") + Value.Key), *Value.Value);
    }

    virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override { OutFormats.Add(NAME_GLSL_SWITCH); }

    virtual const TCHAR* GetPlatformIncludeDirectory() const override { return TEXT(""); }

    virtual void CompileShader(FName Format, const FShaderCompilerInput& Input, FShaderCompilerOutput& Output,
        const FString& WorkingDirectory) const override
    {
        FString Source;
        CompileOne(Input, Output, Source);
        // KADAN_SWITCH_RESULTS=<folder>: each job's outcome, in <folder>/<material>/<rest of its debug group>_<entry>:
        // .payload when it compiled; .error (the messages) and .usf (its preprocessed source, before the patches)
        // when it did not. A material whose map fails on one shader still shows what each of its others made.
        const FString Results = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SWITCH_RESULTS"));
        if (Results.IsEmpty())
            return;
        FString Group = Input.DebugGroupName, Material, Rest;
        if (!Group.Split(TEXT("/"), &Material, &Rest))
        {
            Material = TEXT("_");
            Rest = Group;
        }
        FString Name = Rest + TEXT("_") + Input.EntryPointName;
        for (TCHAR& C : Name)
            if (!FChar::IsAlnum(C) && C != TEXT('_'))
                C = TEXT('_');
        for (TCHAR& C : Material)
            if (!FChar::IsAlnum(C) && C != TEXT('_'))
                C = TEXT('_');
        const FString Base = Results / Material / Name;
        if (Output.bSucceeded)
        {
            FFileHelper::SaveArrayToFile(Output.ShaderCode.GetReadAccess(), *(Base + TEXT(".payload")));
            return;
        }
        FString Messages;
        for (const FShaderCompilerError& Error : Output.Errors)
            Messages += Error.GetErrorString() + TEXT("\n");
        FFileHelper::SaveStringToFile(
            Messages, *(Base + TEXT(".error")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (!Source.IsEmpty())
            FFileHelper::SaveStringToFile(
                Source, *(Base + TEXT(".usf")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

private:
    // One job. KeptSource: its preprocessed source before the patches, once it has one.
    void CompileOne(const FShaderCompilerInput& Input, FShaderCompilerOutput& Output, FString& KeptSource) const
    {
        Output.Target = Input.Target;
        Output.bSucceeded = false;
        const IKadanSwitchShaderCompiler* Compiler = FindCompiler();
        if (!Compiler)
        {
            AddError(Output, TEXT("Kadan's Switch shader compiler is not loaded. Enable the Kadan plugin."));
            return;
        }

        EHlslShaderFrequency Frequency = HSF_InvalidFrequency;
        switch (Input.Target.Frequency)
        {
            case SF_Vertex:
                Frequency = HSF_VertexShader;
                break;
            case SF_Pixel:
                Frequency = HSF_PixelShader;
                break;
            case SF_Geometry:
                Frequency = HSF_GeometryShader;
                break;
            default:
                break;
        }
        if (Frequency == HSF_InvalidFrequency)
        {
            AddError(Output,
                FString::Printf(TEXT("%s shaders are not part of the game's material passes."),
                    CrossCompiler::GetFrequencyName(EShaderFrequency(Input.Target.Frequency))));
            return;
        }

        // The Switch's own branches of UE's shader source, as Nintendo's format compiled them (UE's HLSL cross
        // compiler path), with DXC's needs as UE's Vulkan format sets them.
        FShaderCompilerDefinitions Defines;
        Defines.SetDefine(TEXT("SWITCH_PROFILE"), 1);
        Defines.SetDefine(TEXT("COMPILER_SWITCH"), 1);
        Defines.SetDefine(TEXT("COMPILER_HLSLCC"), 1);
        // A GLSL target, as Nintendo's format is: no pixel coverage output (masked materials discard instead).
        Defines.SetDefine(TEXT("COMPILER_GLSL"), 1);
        // The game's programs take a texture handle and a sampler handle of their own (stock Platform.ush turns this
        // on only for the profiles it knows).
        Defines.SetDefine(TEXT("SUPPORTS_INDEPENDENT_SAMPLERS"), 1);
        Defines.SetDefine(TEXT("row_major"), TEXT(""));
        Defines.SetDefine(TEXT("COMPILER_SUPPORTS_ATTRIBUTES"), uint32(1));
        if (Input.Environment.CompilerFlags.Contains(CFLAG_UseFullPrecisionInPS))
            Defines.SetDefine(TEXT("FORCE_FLOATS"), uint32(1));

        FString Source;
        // The game's programs are shipping shaders: no development code (view mode overrides and the like), whatever
        // the editor compiles for itself.
        FShaderCompilerInput SwitchInput = Input;
        SwitchInput.Environment.SetDefine(TEXT("COMPILE_SHADERS_FOR_DEVELOPMENT"), 0);
        // ShaderCompiler.cpp appends the host/target FORWARD_SHADING after a
        // shader type's environment. The platform masks below select forward
        // permutations but do not set this define. C4 always renders forward:
        // deferred LF output leaves R/G white, and deferred receivers write a
        // GBuffer instead of consuming the forward light/shadow mask.
        SwitchInput.Environment.SetDefine(TEXT("FORWARD_SHADING"), 1);
        // What the game's own Switch platform declares where stock UE's description of it differs: volumetric fog
        // (its base pass programs fog per pixel with the volumetric fog volume).
        SwitchInput.Environment.SetDefine(TEXT("COMPILE_BASEPASS_PIXEL_VOLUMETRIC_FOGGING"), 1);
        // A GroundSoft material (the game draws it after the ground) reads the scene behind it in its opaque base
        // pass: the game's OpaqueBasePass has the scene color copy and the scene textures (stock's has neither).
        const bool bGroundSoft = IsGroundSoftOpaqueBasePass(Input);
        if (bGroundSoft)
        {
            Defines.SetDefine(TEXT("SceneColorCopyTexture"), TEXT("OpaqueBasePass_SceneColorCopyTexture"));
            Defines.SetDefine(TEXT("SceneColorCopySampler"), TEXT("OpaqueBasePass_SceneColorCopySampler"));
        }
        if (!PreprocessShader(Source, Output, SwitchInput, Defines))
            return;
        if (!(Source.Contains(TEXT("SV_ViewID")) || Source.Contains(TEXT("VIEW_ID"))))
            StripInstancedStereo(Source);
        FShaderParameterParser Parser;
        if (!Parser.ParseAndMoveShaderParametersToRootConstantBuffer(Input, Output, Source, nullptr))
            return;
        RemoveUniformBuffersFromSource(Input.Environment, Source);
        if (bGroundSoft)
        {
            for (const TCHAR* Name : GroundSoftSceneTextures)
                Source.ReplaceInline(*(FString(TEXT("SceneTexturesStruct_")) + Name),
                    *(FString(TEXT("OpaqueBasePass_SceneTextures_")) + Name), ESearchCase::CaseSensitive);
            Source =
                TEXT(
                    "Texture2D OpaqueBasePass_SceneColorCopyTexture;\nSamplerState OpaqueBasePass_SceneColorCopySampler;\n") +
                Source;
        }
        const bool bDump = !Input.DumpDebugInfoPath.IsEmpty();
        if (bDump)
            DumpDebugUSF(Input, Source);
        // KADAN_SWITCH_DUMP=<folder>: each job's preprocessed source and our program, named by its debug group.
        // KADAN_SWITCH_DUMP_FILTER=<name substrings separated by ;>: retain jobs matching every part for a bounded replay.
        const FString OwnDump = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SWITCH_DUMP"));
        FString OwnDumpBase;
        if (!OwnDump.IsEmpty())
        {
            FString Name = Input.DebugGroupName + TEXT("_") + Input.EntryPointName;
            for (TCHAR& C : Name)
                if (!FChar::IsAlnum(C) && C != TEXT('_'))
                    C = TEXT('_');
            const FString Filter = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SWITCH_DUMP_FILTER"));
            TArray<FString> Parts;
            Filter.ParseIntoArray(Parts, TEXT(";"), true);
            bool bMatches = true;
            for (const FString& Part : Parts)
                bMatches &= Name.Contains(Part, ESearchCase::CaseSensitive);
            if (bMatches)
            {
                OwnDumpBase = OwnDump / Name;
                FFileHelper::SaveStringToFile(Source, *(OwnDumpBase + TEXT(".usf")));
            }
        }
        KeptSource = Source;
        // The game's own changes to UE's shader source, as patches of the preprocessed source.
        {
            FString PatchError;
            if (!ApplySourcePatches(Source, PatchError))
            {
                AddError(Output, PatchError);
                return;
            }
        }

        // DXC through UE's ShaderConductor, as UE's Vulkan format calls it.
        CrossCompiler::FShaderConductorContext Conductor;
        FShaderCompilerDefinitions ConductorDefines;
        ConductorDefines.SetDefine(TEXT("TextureExternal"), TEXT("Texture2D"));
        // The material's decal response for the patches (the preprocessed source no longer has the material's defines).
        const FString* DecalResponse = Input.Environment.GetDefinitions().Find(TEXT("MATERIALDECALRESPONSEMASK"));
        ConductorDefines.SetDefine(TEXT("MATERIALDECALRESPONSEMASK"), DecalResponse ? **DecalResponse : TEXT("7"));
        // Whether the material traces screen space reflections (the preprocessed source no longer has MATERIAL_SSR): the
        // game traces a translucent material's in forward shading too (KadanSwitchSourcePatches.inl).
        const FString* MaterialSSR = Input.Environment.GetDefinitions().Find(TEXT("MATERIAL_SSR"));
        ConductorDefines.SetDefine(TEXT("KADAN_MATERIAL_SSR"), MaterialSSR && *MaterialSSR == TEXT("1") ? 1 : 0);
        // Whether the material's normal is the constant default (0, 0, 1), which the game's compiler folds (KadanSwitchSourcePatches.inl).
        ConductorDefines.SetDefine(TEXT("KADAN_DEFAULT_NORMAL"),
            Source.Contains(TEXT("\tPixelMaterialInputs.Normal =  float3 (0.00000000,0.00000000,1.00000000);"),
                ESearchCase::CaseSensitive)
                ? 1
                : 0);
        // Whether the material is a UI material (MATERIAL_DOMAIN_UI; the preprocessed source no longer has its define).
        {
            const FString* DomainUI = Input.Environment.GetDefinitions().Find(TEXT("MATERIAL_DOMAIN_UI"));
            ConductorDefines.SetDefine(TEXT("KADAN_MATERIAL_DOMAIN_UI"), DomainUI && *DomainUI == TEXT("1") ? 1 : 0);
        }
        // Whether the material has Kadan's GroundSoft alpha output (the preprocessed source no longer has its define).
        ConductorDefines.SetDefine(TEXT("KADAN_GROUNDSOFT_ALPHA"),
            Source.Contains(TEXT("GetKadanGroundSoftAlpha0(FMaterialPixelParameters"), ESearchCase::CaseSensitive) ? 1
                                                                                                                   : 0);
        // The material's translucency lighting mode as UE's ETranslucencyLightingMode number (KADAN_TLM): with forward
        // shading every mode compiles to the same source, and the game lights some of them differently.
        {
            static const TCHAR* Modes[] = {TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_NONDIRECTIONAL"),
                TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_DIRECTIONAL"),
                TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_PERVERTEX_NONDIRECTIONAL"),
                TEXT("TRANSLUCENCY_LIGHTING_VOLUMETRIC_PERVERTEX_DIRECTIONAL"),
                TEXT("TRANSLUCENCY_LIGHTING_SURFACE_LIGHTINGVOLUME"),
                TEXT("TRANSLUCENCY_LIGHTING_SURFACE_FORWARDSHADING")};
            for (int32 Mode = 0; Mode < UE_ARRAY_COUNT(Modes); ++Mode)
            {
                const FString* Value = Input.Environment.GetDefinitions().Find(Modes[Mode]);
                if (Value && *Value == TEXT("1"))
                    ConductorDefines.SetDefine(TEXT("KADAN_TLM"), Mode);
            }
        }
        {
            FString Material, Rest;
            if (!Input.DebugGroupName.Split(TEXT("/"), &Material, &Rest))
                Material = Input.DebugGroupName;
            if (const TArray<TPair<FString, FString>>* Values = MaterialSettings().Find(Material))
                for (const TPair<FString, FString>& Value : *Values)
                    ConductorDefines.SetDefine(*(TEXT("KADAN_") + Value.Key), *Value.Value);
            // The settings the material itself holds (ModifyShaderCompilerInput), over the file's.
            for (const TPair<FString, FString>& Define : Input.Environment.GetDefinitions())
                if (Define.Key.StartsWith(TEXT("KADAN_"), ESearchCase::CaseSensitive) &&
                    Define.Key != TEXT("KADAN_TLM"))
                    ConductorDefines.SetDefine(*Define.Key, *Define.Value);
        }
        Conductor.LoadSource(Source, Input.VirtualSourceFilePath, Input.EntryPointName, Frequency, &ConductorDefines);
        CrossCompiler::FShaderConductorOptions Options;
        Options.TargetProfile = HCT_FeatureLevelSM5;
        if (!Input.IsUsingTessellation())
        {
            FString Rewritten;
            Options.bRemoveUnusedGlobals = true;
            if (!Conductor.RewriteHlsl(Options, bDump ? &Rewritten : nullptr))
            {
                Conductor.FlushErrors(Output.Errors);
                return;
            }
            Options.bRemoveUnusedGlobals = false;
            if (bDump)
                DumpDebugShaderText(Input, Rewritten, TEXT("rewritten.hlsl"));
        }
        TArray<uint32> Spirv;
        if (!Conductor.CompileHlslToSpirv(Options, Spirv))
        {
            Conductor.FlushErrors(Output.Errors);
            return;
        }
        Conductor.FlushErrors(Output.Errors);
        if (bDump)
            DumpDebugShaderBinary(Input, Spirv.GetData(), Spirv.Num() * sizeof(uint32), TEXT("spv"));
        if (!OwnDumpBase.IsEmpty())
            FFileHelper::SaveArrayToFile(
                TArrayView<const uint8>(reinterpret_cast<const uint8*>(Spirv.GetData()), Spirv.Num() * sizeof(uint32)),
                *(OwnDumpBase + TEXT(".spv")));

        FKadanSwitchProgram Program;
        FString Error;
        if (!Compiler->Translate(Spirv, Program, Error))
        {
            AddError(Output, Error);
            return;
        }
        if (bDump)
            DumpDebugShaderText(Input, Program.Assembly, TEXT("nvasm"));
        if (!OwnDumpBase.IsEmpty())
            FFileHelper::SaveStringToFile(Program.Assembly, *(OwnDumpBase + TEXT(".nvasm")));

        // The parameter map, as UE's OpenGL format writes it from the same facts.
        FShaderParameterMap& ParameterMap = Output.ParameterMap;
        TBitArray<> UsedSlots(false, 32);
        UsedSlots[DriverBufferSlot] = true;
        int32 GlobalsSlot = INDEX_NONE, IntegersSlot = INDEX_NONE;
        uint32 GlobalsSize = 0, IntegersSize = 0;
        for (const FKadanSwitchBuffer& Buffer : Program.Buffers)
        {
            if (Buffer.Slot < 0 || Buffer.Slot >= 32 || Buffer.Slot == DriverBufferSlot)
            {
                AddError(Output,
                    FString::Printf(TEXT("Uniform buffer %s has no slot the game's renderer binds."), *Buffer.Name));
                return;
            }
            UsedSlots[Buffer.Slot] = true;
            if (Buffer.Name == TEXT("$Globals.u"))
            {
                IntegersSlot = Buffer.Slot;
                for (const FKadanSwitchMember& Member : Buffer.Members)
                {
                    ParameterMap.AddParameterAllocation(
                        *Member.Name, LooseIntegerType, Member.Offset, Member.Size, EShaderParameterType::LooseData);
                    IntegersSize = FMath::Max(IntegersSize, Member.Offset + Member.Size);
                }
            }
            else if (Buffer.Name == TEXT("$Globals"))
            {
                GlobalsSlot = Buffer.Slot;
                for (const FKadanSwitchMember& Member : Buffer.Members)
                {
                    ParameterMap.AddParameterAllocation(
                        *Member.Name, LoosePackedType, Member.Offset, Member.Size, EShaderParameterType::LooseData);
                    GlobalsSize = FMath::Max(GlobalsSize, Member.Offset + Member.Size);
                }
            }
            else
            {
                ParameterMap.AddParameterAllocation(*Buffer.Name, Buffer.Slot, 0,
                    uint16(FMath::Min<uint32>(Buffer.Size, 0xFFFF)), EShaderParameterType::UniformBuffer);
            }
        }
        for (const FKadanSwitchResource& Texture : Program.Textures)
            ParameterMap.AddParameterAllocation(*Texture.Name, 0, Texture.Slot, 1, EShaderParameterType::SRV);
        for (const FKadanSwitchResource& Sampler : Program.Samplers)
            ParameterMap.AddParameterAllocation(*Sampler.Name, 0, Sampler.Slot, 1, EShaderParameterType::Sampler);
        // Structured buffers: views bound in storage slots (the game's programs read them as storage).
        for (const FKadanSwitchResource& Buffer : Program.Storage)
            ParameterMap.AddParameterAllocation(*Buffer.Name, 0, Buffer.Slot, 1, EShaderParameterType::SRV);

        // The game's own OpaqueBasePass members a GroundSoft material reads, in the struct's resource table (the
        // payload names each by the game's index).
        TMap<FString, FResourceTableEntry> Resources = Input.Environment.ResourceTableMap;
        if (bGroundSoft)
        {
            uint16 Next = 0;
            for (const auto& Pair : Resources)
                if (Pair.Value.UniformBufferName == TEXT("OpaqueBasePass"))
                    Next = FMath::Max<uint16>(Next, Pair.Value.ResourceIndex + 1);
            auto Add = [&](const FString& Name, uint16 Type)
            {
                if (Resources.Contains(Name))
                    return;
                FResourceTableEntry Entry;
                Entry.UniformBufferName = TEXT("OpaqueBasePass");
                Entry.Type = Type;
                Entry.ResourceIndex = Next++;
                Resources.Add(Name, Entry);
            };
            Add(TEXT("OpaqueBasePass_SceneColorCopyTexture"), UBMT_TEXTURE);
            Add(TEXT("OpaqueBasePass_SceneColorCopySampler"), UBMT_SAMPLER);
            for (const TCHAR* Name : GroundSoftSceneTextures)
                Add(FString(TEXT("OpaqueBasePass_SceneTextures_")) + Name,
                    FString(Name).EndsWith(TEXT("Sampler")) ? UBMT_SAMPLER : UBMT_TEXTURE);
        }
        FShaderCompilerResourceTable Table;
        if (!BuildResourceTableMapping(
                Resources, Input.Environment.ResourceTableLayoutHashes, UsedSlots, ParameterMap, Table))
        {
            AddError(Output, TEXT("UE's resource table mapping failed for this shader."));
            return;
        }
        // Each slot's struct, including buffers the mapping allocated (read only for their resources), before the
        // global ones leave the map.
        TArray<FString> SlotNames;
        SlotNames.SetNum(32);
        for (const auto& Pair : ParameterMap.GetParameterMap())
            if (Pair.Value.Type == EShaderParameterType::UniformBuffer && Pair.Value.BufferIndex < 32)
                SlotNames[Pair.Value.BufferIndex] = Pair.Key;
        CullGlobalUniformBuffers(Input.Environment.ResourceTableLayoutSlots, ParameterMap);
        // A base pass reads these only through its own pass buffer: UE's base pass shaders assert (a crash, not a
        // compile error) on a program that binds them itself, as a material's custom code naming them does.
        if (Input.VirtualSourceFilePath.EndsWith(TEXT("BasePassPixelShader.usf")))
            for (const TCHAR* Nested : {TEXT("ReflectionStruct"), TEXT("FogStruct"), TEXT("PlanarReflectionStruct")})
                if (ParameterMap.ContainsParameterAllocation(Nested))
                {
                    AddError(Output,
                        FString::Printf(
                            TEXT(
                                "The material's code reads %s directly; a base pass reads it only through its pass buffer."),
                            Nested));
                    return;
                }

        FKadanSwitchBindings Bindings;
        Bindings.LayoutHashes = Table.ResourceTableLayoutHashes;
        DecodeTable(Table.TextureMap, SlotNames, Resources, Bindings.Textures);
        DecodeTable(Table.SamplerMap, SlotNames, Resources, Bindings.Samplers);
        DecodeTable(Table.ShaderResourceViewMap, SlotNames, Resources, Bindings.SRVs);
        DecodeTable(Table.UnorderedAccessViewMap, SlotNames, Resources, Bindings.UAVs);
        int32 LastSlot = UsedSlots.FindLast(true);
        if (LastSlot == DriverBufferSlot)
        {
            UsedSlots[DriverBufferSlot] = false;
            LastSlot = UsedSlots.FindLast(true);
        }
        Bindings.UniformBuffers = uint32(FMath::Max(LastSlot + 1, Bindings.LayoutHashes.Num()));
        SlotNames.SetNum(Bindings.UniformBuffers);
        if (GlobalsSlot != INDEX_NONE)
            SlotNames[GlobalsSlot] = TEXT("$Globals");
        if (IntegersSlot != INDEX_NONE)
            SlotNames[IntegersSlot] = TEXT("$Globals.u");
        Bindings.BufferNames = SlotNames;
        Bindings.TextureSlots = uint32(Program.Textures.Num());
        Bindings.StorageBuffers = uint32(Program.Storage.Num());
        Bindings.SamplerSlots = uint32(FMath::Max(Program.Samplers.Num(), Program.DefaultSampler + 1));
        if (GlobalsSlot != INDEX_NONE)
        {
            FKadanSwitchPackedArray Array;
            Array.Size = uint16(Align(GlobalsSize, 16u));
            Array.TypeName = LoosePackedType;
            Array.TypeIndex = LoosePackedIndex;
            Bindings.GlobalArrays.Add(Array);
            Bindings.PackedGlobalSlots.Add(TPair<uint8, int32>(LoosePackedIndex, GlobalsSlot));
        }
        if (IntegersSlot != INDEX_NONE)
        {
            FKadanSwitchPackedArray Array;
            Array.Size = uint16(Align(IntegersSize, 16u));
            Array.TypeName = LooseIntegerType;
            Array.TypeIndex = LooseIntegerIndex;
            Bindings.GlobalArrays.Add(Array);
            Bindings.PackedGlobalSlots.Add(TPair<uint8, int32>(LooseIntegerIndex, IntegersSlot));
        }

        TArray<uint8> Payload;
        if (!Compiler->Payload(Program, Bindings, Payload, Error))
        {
            AddError(Output, Error);
            return;
        }
        Output.ShaderCode.GetWriteAccess().Append(Payload);
        Output.NumTextureSamplers = Bindings.SamplerSlots;
        Output.bSucceeded = true;
        Parser.ValidateShaderParameterTypes(Input, Output);
    }
};

// What Kadan's compile of a material for the game tells the format (KadanSwitchShaderCompiler.h).
class FKadanSwitchCompileContext : public IKadanSwitchCompileContext
{
public:
    virtual void SetMaterialSSR(bool bMaterialSSR) override { KadanSetMaterialSSR(bMaterialSSR); }
};

class FKadanShaderFormatModule : public IShaderFormatModule
{
    FKadanSwitchCompileContext CompileContext;

public:
    // The game renders forward with DBuffer decals (r.ForwardShading and r.DBuffer in its DefaultEngine.ini). UE takes
    // a shader platform's settings from its target platform, and without one (a creator's editor has no Switch target
    // platform) from the editor's own r.ForwardShading and r.DBuffer. These masks select the game's permutations;
    // CompileOne also overrides the later job define before preprocessing. A decal is then a DBuffer decal, as the game's renderer draws it, and the stock classes the game's maps hold
    // for it (FMeshDecalsEmissivePS) are compiled too.
    virtual void StartupModule() override
    {
        GForwardShadingPlatformMask |= 1ull << SP_SWITCH;
        GDBufferPlatformMask |= 1ull << SP_SWITCH;
        // The game's classes stock UE has but does not compile for SP_SWITCH (KadanGameShaderTypes.cpp); the renderer's
        // types are registered when its module loads, which this one links.
        if (!KadanCompileGameOnlyStockShaders())
            FCoreDelegates::OnPostEngineInit.AddLambda([] { KadanCompileGameOnlyStockShaders(); });
        // The game's translucency pass classes, made from stock's base pass types now: UE takes no shader type later.
        const int32 Passes = KadanRegisterTranslucencyPassTypes();
        if (Passes < 0)
            UE_LOG(
                LogTemp, Warning, TEXT("Kadan: no base pass types to make the game's translucency pass classes from"));
        IModularFeatures::Get().RegisterModularFeature(IKadanSwitchCompileContext::FeatureName(), &CompileContext);
    }

    virtual void ShutdownModule() override
    {
        IModularFeatures::Get().UnregisterModularFeature(IKadanSwitchCompileContext::FeatureName(), &CompileContext);
    }

    virtual IShaderFormat* GetShaderFormat() override
    {
        static FKadanShaderFormat Format;
        return &Format;
    }
};

} // namespace

IMPLEMENT_MODULE(FKadanShaderFormatModule, KadanShaderFormat);

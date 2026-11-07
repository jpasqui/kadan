#include "P4KadanMaterialExpressions.h"
#include "MaterialCompiler.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"

namespace
{
const TCHAR* const PreviewOnly = TEXT("Preview only");
}

UMaterialExpressionKadanSceneDepth::UMaterialExpressionKadanSceneDepth(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
    MenuCategories.Add(NSLOCTEXT("Kadan", "Game", "Kadan Game"));
    Outputs.Reset();
    Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
#endif
    // As stock's Scene Depth node: an offset from the pixel.
    InputMode = EMaterialSceneAttributeInputMode::OffsetFraction;
    ConstInput = FVector2D(0.f, 0.f);
}

UMaterialExpressionKadanSceneColor::UMaterialExpressionKadanSceneColor(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
    MenuCategories.Add(NSLOCTEXT("Kadan", "Game", "Kadan Game"));
#endif
    // As stock's Scene Color node: an offset from the pixel.
    InputMode = EMaterialSceneAttributeInputMode::OffsetFraction;
    ConstInput = FVector2D(0.f, 0.f);
}

UMaterialExpressionKadanGroundSoftAlpha::UMaterialExpressionKadanGroundSoftAlpha(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
    MenuCategories.Add(NSLOCTEXT("Kadan", "Game", "Kadan Game"));
    Outputs.Reset();
    bCollapsed = true;
#endif
}

UMaterialExpressionKadanGameCustom::UMaterialExpressionKadanGameCustom(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
    MenuCategories.Add(NSLOCTEXT("Kadan", "Game", "Kadan Game"));
#endif
    OutputType = CMOT_Float1;
}

UMaterialExpressionKadanReflectionCustom::UMaterialExpressionKadanReflectionCustom(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    NativeCompileExpression = CreateDefaultSubobject<UMaterialExpressionCustom>(TEXT("NativeReflectionCompiler"));
    NativeCompileExpression->SetFlags(RF_Transient);
    // Read defaults from the installed native class, not invented compatibility values.
    const UMaterialExpressionCustom* Defaults = GetDefault<UMaterialExpressionCustom>();
    Code = Defaults->Code;
    Description = Defaults->Description;
    OutputType = Defaults->OutputType;
    Inputs.Reset();
    for (const FCustomInput& Input : Defaults->Inputs)
    {
        FKadanGameCustomInput Own;
        Own.InputName = Input.InputName;
        Own.Input = Input.Input;
        Inputs.Add(Own);
    }
#if WITH_EDITORONLY_DATA
    bCollapsed = Defaults->bCollapsed;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionKadanReflectionCustom::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    const UMaterial* Owner = GetTypedOuter<UMaterial>();
    if (!Owner || Owner->MaterialDomain != MD_Surface || Compiler->GetCurrentShaderFrequency() != SF_Pixel)
        return Compiler->Errorf(TEXT(
            "Original reflection Custom requires the Surface pixel base-pass binding; this domain or stage is unsupported."));
    if (!Code.Contains(TEXT("ReflectionStruct.")) || Code.Contains(TEXT("KadanBasePassReflection")))
        return Compiler->Errorf(TEXT("Original reflection Custom has no supported original reflection access."));
    // Material.ush (and thus Custom functions) precedes BasePassCommon.ush.
    // Use exactly its real alias, never the forbidden standalone Reflection UB.
    // MaterialShared.cpp defines the same translucent macro for AlphaComposite
    // and AlphaHoldout. This is an editor compile adapter, not a game binding claim.
    if (!NativeCompileExpression || NativeCompileExpression->GetOuter() != this || Inputs.Num() > 256 ||
        AdditionalOutputs.Num() > 256 || AdditionalDefines.Num() > 256 || IncludeFilePaths.Num() > 256 ||
        OutputIndex < 0 || OutputIndex > AdditionalOutputs.Num())
        return Compiler->Errorf(TEXT("Original reflection Custom has invalid compiler input/output data."));
    TArray<int32> CompiledInputs;
    for (const FKadanGameCustomInput& Input : Inputs)
    {
        if (Input.InputName.IsNone())
        {
            CompiledInputs.Add(INDEX_NONE);
            continue;
        }
        if (!Input.Input.GetTracedInput().Expression)
            return Compiler->Errorf(
                TEXT("Original reflection Custom input %s is unconnected."), *Input.InputName.ToString());
        FExpressionInput CompileInput = Input.Input;
        const int32 Value = CompileInput.Compile(Compiler);
        if (Value < 0)
            return Value;
        CompiledInputs.Add(Value);
    }
    UMaterialExpressionCustom* Native = NativeCompileExpression;
    const FString SavedCode = Native->Code, SavedDescription = Native->Description;
    const auto SavedInputs = Native->Inputs;
    const auto SavedOutputs = Native->AdditionalOutputs;
    const auto SavedDefines = Native->AdditionalDefines;
    const auto SavedIncludes = Native->IncludeFilePaths;
    const auto SavedType = Native->OutputType;
    const FGuid SavedGuid = Native->MaterialExpressionGuid;
    ON_SCOPE_EXIT
    {
        Native->Code = SavedCode;
        Native->Description = SavedDescription;
        Native->Inputs = SavedInputs;
        Native->AdditionalOutputs = SavedOutputs;
        Native->AdditionalDefines = SavedDefines;
        Native->IncludeFilePaths = SavedIncludes;
        Native->OutputType = SavedType;
        Native->MaterialExpressionGuid = SavedGuid;
    };
    FString Adapted = Code;
    Adapted.ReplaceInline(TEXT("ReflectionStruct."), TEXT("KadanBasePassReflection."), ESearchCase::CaseSensitive);
    Native->Code = TEXT(
                       "\n#if MATERIALBLENDING_TRANSLUCENT || MATERIALBLENDING_ADDITIVE || MATERIALBLENDING_MODULATE\n")
                       TEXT("#define KadanBasePassReflection TranslucentBasePass.Shared.Reflection\n#else\n")
                           TEXT("#define KadanBasePassReflection OpaqueBasePass.Shared.Reflection\n#endif\n") +
        Adapted + TEXT("\n#undef KadanBasePassReflection\n");
    Native->Description = Description;
    Native->OutputType = OutputType;
    Native->MaterialExpressionGuid = MaterialExpressionGuid;
    Native->Inputs.Reset();
    for (const FKadanGameCustomInput& Input : Inputs)
    {
        FCustomInput Entry;
        Entry.InputName = Input.InputName;
        Entry.Input = Input.Input;
        Native->Inputs.Add(Entry);
    }
    Native->AdditionalOutputs.Reset();
    for (const FKadanReflectionCustomOutput& Output : AdditionalOutputs)
    {
        FCustomOutput Entry;
        Entry.OutputName = Output.OutputName;
        Entry.OutputType = Output.OutputType;
        Native->AdditionalOutputs.Add(Entry);
    }
    Native->AdditionalDefines.Reset();
    for (const FKadanReflectionCustomDefine& Define : AdditionalDefines)
    {
        FCustomDefine Entry;
        Entry.DefineName = Define.DefineName;
        Entry.DefineValue = Define.DefineValue;
        Native->AdditionalDefines.Add(Entry);
    }
    Native->IncludeFilePaths = IncludeFilePaths;
    // Public virtual dispatch to the actual translator. It snapshots the native
    // expression synchronously. Authored Code, Inputs and GUID are never changed.
    return Compiler->CustomExpression(Native, OutputIndex, CompiledInputs);
}

void UMaterialExpressionKadanReflectionCustom::GetCaption(TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(Description.IsEmpty() ? FString(TEXT("Game Reflection Custom")) : Description);
}
void UMaterialExpressionKadanReflectionCustom::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
    OutToolTip.Add(TEXT(
        "Keeps the original formula and uses the editor surface base-pass reflection binding during compilation."));
}
uint32 UMaterialExpressionKadanReflectionCustom::GetOutputType(int32 OutputIndex)
{
    const auto Type = OutputIndex == 0                    ? OutputType.GetValue()
        : AdditionalOutputs.IsValidIndex(OutputIndex - 1) ? AdditionalOutputs[OutputIndex - 1].OutputType.GetValue()
                                                          : CMOT_MAX;
    switch (Type)
    {
        case CMOT_Float1:
            return MCT_Float;
        case CMOT_Float2:
            return MCT_Float2;
        case CMOT_Float3:
            return MCT_Float3;
        case CMOT_Float4:
            return MCT_Float4;
        case CMOT_MaterialAttributes:
            return MCT_MaterialAttributes;
        default:
            return MCT_Unknown;
    }
}
bool UMaterialExpressionKadanReflectionCustom::IsResultMaterialAttributes(int32 OutputIndex)
{
    return GetOutputType(OutputIndex) == MCT_MaterialAttributes;
}
TArray<FExpressionOutput>& UMaterialExpressionKadanReflectionCustom::GetOutputs()
{
    Outputs.Reset();
    Outputs.Add(FExpressionOutput(TEXT("")));
    for (const FKadanReflectionCustomOutput& Output : AdditionalOutputs)
        Outputs.Add(FExpressionOutput(Output.OutputName));
    return Outputs;
}

namespace
{
// A game scene read compiled for the game's platform: stock's own code for the read (MaterialTemplate.ush), through a
// transient stock Custom expression. The screen position is the input's: an offset from the pixel (stock's default),
// or the viewport UV. With no offset the game's programs read at the pixel's screen-aligned position, unclamped.
int32 CompileGameSceneRead(FMaterialCompiler* Compiler, UObject* Owner, UMaterialExpressionCustom*& Native,
    const FGuid& Guid, EMaterialSceneAttributeInputMode::Type Mode, FExpressionInput& Input, const FVector2D& Constant,
    const TCHAR* Read, ECustomMaterialOutputType Type)
{
    const bool bConnected = Input.GetTracedInput().Expression != nullptr;
    const int32 Coordinates = bConnected ? Input.Compile(Compiler) : Compiler->Constant2(Constant.X, Constant.Y);
    if (Coordinates < 0)
        return Coordinates;
    if (!Native || Native->GetOuter() != Owner)
        Native = NewObject<UMaterialExpressionCustom>(Owner, NAME_None, RF_Transient);
    const bool bAtThePixel = Mode == EMaterialSceneAttributeInputMode::OffsetFraction && !bConnected &&
        Constant.IsZero();
    const FString UV = Mode == EMaterialSceneAttributeInputMode::Coordinates ? TEXT("ViewportUVToBufferUV(UV)")
        : bAtThePixel ? TEXT("ScreenAlignedPosition(GetScreenPosition(Parameters))")
                      : TEXT("CalcScreenUVFromOffsetFraction(GetScreenPosition(Parameters), UV)");
    Native->Code = FString::Printf(TEXT("return %s(%s);"), Read, *UV);
    Native->OutputType = Type;
    Native->Description = Read;
    Native->MaterialExpressionGuid = Guid;
    Native->Inputs.Reset();
    FCustomInput Entry;
    Entry.InputName = TEXT("UV");
    Native->Inputs.Add(Entry);
    Native->AdditionalOutputs.Reset();
    Native->AdditionalDefines.Reset();
    Native->IncludeFilePaths.Reset();
    TArray<int32> Compiled;
    Compiled.Add(Coordinates);
    return Compiler->CustomExpression(Native, 0, Compiled);
}

bool ForTheGame(FMaterialCompiler* Compiler)
{
    return Compiler->GetShaderPlatform() == SP_SWITCH && Compiler->GetCurrentShaderFrequency() == SF_Pixel;
}
} //namespace

int32 UMaterialExpressionKadanSceneDepth::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    if (ForTheGame(Compiler))
        return CompileGameSceneRead(Compiler, this, CompileExpression, MaterialExpressionGuid, InputMode, Input,
            ConstInput, TEXT("CalcSceneDepth"), CMOT_Float1);
    return Compiler->Constant(KadanFarSceneDepth);
}

void UMaterialExpressionKadanSceneDepth::GetCaption(TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(TEXT("Game Scene Depth"));
    OutCaptions.Add(PreviewOnly);
}

void UMaterialExpressionKadanSceneDepth::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
    OutToolTip.Add(TEXT(
        "The editor can't read the scene depth here, so this draws a stand-in value as if nothing were close behind the surface. The game reads the real scene depth."));
}

int32 UMaterialExpressionKadanSceneColor::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    if (ForTheGame(Compiler))
        return CompileGameSceneRead(Compiler, this, CompileExpression, MaterialExpressionGuid, InputMode, Input,
            ConstInput, TEXT("DecodeSceneColorForMaterialNode"), CMOT_Float3);
    return Compiler->Constant3(0.f, 0.f, 0.f);
}

void UMaterialExpressionKadanSceneColor::GetCaption(TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(TEXT("Game Scene Color"));
    OutCaptions.Add(PreviewOnly);
}

void UMaterialExpressionKadanSceneColor::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
    OutToolTip.Add(TEXT(
        "The editor can't read the scene color here, so this draws a stand-in value of black. The game reads the real scene color."));
}

int32 UMaterialExpressionKadanGroundSoftAlpha::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    if (!Input.GetTracedInput().Expression)
        return Compiler->Errorf(TEXT("Kadan Ground Soft Alpha needs an input."));
    return Compiler->CustomOutput(this, OutputIndex, Input.Compile(Compiler));
}

void UMaterialExpressionKadanGroundSoftAlpha::GetCaption(TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(TEXT("Kadan Ground Soft Alpha"));
}

void UMaterialExpressionKadanGroundSoftAlpha::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
    OutToolTip.Add(TEXT(
        "How much of the lit color shows over the scene behind the material. The game uses it on materials drawn after the ground (C4MaterialDrawOrder GroundSoft). The editor ignores it."));
}

int32 UMaterialExpressionKadanGameCustom::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    // The game's scene color copy read is stock Unreal 4.26's own decode of the copy (MaterialTemplate.ush,
    // DecodeSceneColorForMaterialNode: pow(copy, 4) * 10 * OneOverPreExposure). A stock translucent
    // material's base pass pixel shader has the copy (BasePassPixelShader.usf defines SceneColorCopyTexture);
    // every other shader the material compiles has none and reads zero, under stock's own guard. The scene's
    // own stencil is the game engine's alone and stays zero.
    const bool Own = GetClass() == UMaterialExpressionKadanGameCustom::StaticClass() && OutputIndex == 0 &&
        Inputs.Num() <= 256;
    const bool SceneColorCopy = Own && Code.Contains(TEXT("SceneColorCopyTexture")) &&
        !Code.Contains(TEXT("SceneStencilTexture"));
    // The game's XluCut (a material with C4MaterialParam3 = C4XC_Enable): the pixel is clipped below the global
    // clipping plane of the game's View (C4GlobalClippingPlane, C4XluGlobalClippingPlaneSign) by the material's own
    // height and switch. Kadan's Switch format declares those View members at the game's offsets; anywhere else
    // (the editor's own preview) the plane is off and the formula passes its value through.
    const bool XluCut = Own && !SceneColorCopy &&
        (Code.Contains(TEXT("ResolvedView.C4GlobalClippingPlane")) ||
            Code.Contains(TEXT("ResolvedView.C4XluGlobalClippingPlaneSign"))) &&
        !Code.Contains(TEXT("SceneStencilTexture"));
    bool Connected = SceneColorCopy || XluCut;
    for (const FKadanGameCustomInput& Input : Inputs)
        Connected &= Input.InputName.IsNone() || Input.Input.GetTracedInput().Expression != nullptr;
    if (Connected)
    {
        TArray<int32> CompiledInputs;
        for (const FKadanGameCustomInput& Input : Inputs)
        {
            if (Input.InputName.IsNone())
            {
                CompiledInputs.Add(INDEX_NONE);
                continue;
            }
            FExpressionInput CompileInput = Input.Input;
            const int32 Value = CompileInput.Compile(Compiler);
            if (Value < 0)
                return Value;
            CompiledInputs.Add(Value);
        }
        if (!SceneColorCompileExpression || SceneColorCompileExpression->GetOuter() != this)
            SceneColorCompileExpression = NewObject<UMaterialExpressionCustom>(this, NAME_None, RF_Transient);
        UMaterialExpressionCustom* Native = SceneColorCompileExpression;
        if (SceneColorCopy)
            Native->Code = TEXT("\n#if !defined(SceneColorCopyTexture)\nreturn 0;\n#else\n") + Code +
                TEXT("\n#endif\n");
        else
        {
            FString Adapted = Code;
            Adapted.ReplaceInline(TEXT("ResolvedView.C4GlobalClippingPlane"), TEXT("View_C4GlobalClippingPlane"),
                ESearchCase::CaseSensitive);
            Adapted.ReplaceInline(TEXT("ResolvedView.C4XluGlobalClippingPlaneSign"),
                TEXT("View_C4XluGlobalClippingPlaneSign"), ESearchCase::CaseSensitive);
            Native->Code = TEXT("\n#if !COMPILER_SWITCH\n#define View_C4GlobalClippingPlane float4(0, 0, 0, 0)\n")
                               TEXT("#define View_C4XluGlobalClippingPlaneSign 0.0\n#endif\n") +
                Adapted + TEXT("\n");
        }
        Native->Description = Description;
        Native->OutputType = OutputType;
        Native->MaterialExpressionGuid = MaterialExpressionGuid;
        Native->Inputs.Reset();
        for (const FKadanGameCustomInput& Input : Inputs)
        {
            FCustomInput Entry;
            Entry.InputName = Input.InputName;
            Entry.Input = Input.Input;
            Native->Inputs.Add(Entry);
        }
        Native->AdditionalOutputs.Reset();
        Native->AdditionalDefines.Reset();
        Native->IncludeFilePaths.Reset();
        return Compiler->CustomExpression(Native, 0, CompiledInputs);
    }
    switch (OutputType)
    {
        case CMOT_Float2:
            return Compiler->Constant2(0.f, 0.f);
        case CMOT_Float3:
            return Compiler->Constant3(0.f, 0.f, 0.f);
        case CMOT_Float4:
            return Compiler->Constant4(0.f, 0.f, 0.f, 0.f);
        default:
            return Compiler->Constant(0.f);
    }
}

void UMaterialExpressionKadanGameCustom::GetCaption(TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(Description.IsEmpty() ? FString(TEXT("Game Binding")) : Description);
    OutCaptions.Add(PreviewOnly);
}

void UMaterialExpressionKadanGameCustom::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
    if (!Code.Contains(TEXT("SceneColorCopyTexture")) && !Code.Contains(TEXT("SceneStencilTexture")) &&
        (Code.Contains(TEXT("ResolvedView.C4GlobalClippingPlane")) ||
            Code.Contains(TEXT("ResolvedView.C4XluGlobalClippingPlaneSign"))))
    {
        OutToolTip.Add(TEXT(
            "The game's view clipping fields. The editor has no clipping plane, so they read zero and the water-height cut keeps every pixel here. The game reads its real values."));
        return;
    }
    OutToolTip.Add(Code.Contains(TEXT("SceneColorCopyTexture")) && !Code.Contains(TEXT("SceneStencilTexture"))
            ? TEXT(
                  "The game's scene color copy. A translucent material reads the editor's own copy, as Unreal's Scene Color node does; elsewhere the editor has no copy and draws zero. The game reads its real value.")
            : Code.Contains(TEXT("ResolvedView.C4GlobalClippingPlane"))
            ? TEXT(
                  "The game's water cut. In the game the pixel is cut away on the far side of the game's clipping plane, by the material's own height and switch. The editor has no clipping plane and draws the pixel.")
            : TEXT(
                  "The editor can't read what the game's renderer gives this formula, so this draws a stand-in value of zero. The game reads the real value."));
}

const TArray<FExpressionInput*> UMaterialExpressionKadanGameCustom::GetInputs()
{
    TArray<FExpressionInput*> Result;
    for (FKadanGameCustomInput& Item : Inputs)
        Result.Add(&Item.Input);
    return Result;
}

FExpressionInput* UMaterialExpressionKadanGameCustom::GetInput(int32 InputIndex)
{
    return Inputs.IsValidIndex(InputIndex) ? &Inputs[InputIndex].Input : nullptr;
}

FName UMaterialExpressionKadanGameCustom::GetInputName(int32 InputIndex) const
{
    return Inputs.IsValidIndex(InputIndex) ? Inputs[InputIndex].InputName : NAME_None;
}
#endif

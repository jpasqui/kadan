#pragma once
#include "CoreMinimal.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "P4KadanMaterialExpressions.generated.h"

// Material nodes for what the game's shaders read and a stock material of the same kind cannot: an
// opaque game material that reads the scene's depth or color, and a game Custom formula that reads a
// binding only the game's renderer has. The recovered graph keeps the game's formula with these nodes
// where the game reads the scene. Scene preview nodes use fixed values; the reflection Custom adapter
// uses the stock renderer's real nested binding. KadanGameReadMeaning in material_game_reads.h restores
// the original node meaning for the game's shader path.

// The game's scene depth read. The editor draws it as KadanFarSceneDepth: as if nothing were close
// behind the surface, so a soft fade over (scene depth - pixel depth) is fully faded in. Compiled for the
// game (SP_SWITCH, Kadan's format), a pixel shader reads the scene depth as stock's Scene Depth node does;
// the format binds it where the game's renderer has it (a GroundSoft material's opaque base pass).
UCLASS(collapsecategories, hidecategories = Object)
class KADAN_API UMaterialExpressionKadanSceneDepth : public UMaterialExpression
{
    GENERATED_UCLASS_BODY()

    UPROPERTY(EditAnywhere, Category = MaterialExpressionSceneDepth)
    TEnumAsByte<EMaterialSceneAttributeInputMode::Type> InputMode;

    UPROPERTY(meta = (RequiredInput = "false"))
    FExpressionInput Input;

    UPROPERTY(EditAnywhere, Category = MaterialExpressionSceneDepth)
    FVector2D ConstInput;

#if WITH_EDITOR
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
#endif
private:
    // The stock Custom expression the game's read compiles through. Not authored graph data.
    UPROPERTY(Transient) UMaterialExpressionCustom* CompileExpression = nullptr;
};

// The game's scene color read. The editor draws it as black: with the scene far behind the surface the
// recovered formulas do not use it. Compiled for the game (SP_SWITCH), a pixel shader decodes the scene color
// copy as stock's Scene Color node does, where the format binds one (zero elsewhere, under stock's guard).
UCLASS(collapsecategories, hidecategories = Object)
class KADAN_API UMaterialExpressionKadanSceneColor : public UMaterialExpression
{
    GENERATED_UCLASS_BODY()

    UPROPERTY(EditAnywhere, Category = MaterialExpressionSceneColor)
    TEnumAsByte<EMaterialSceneAttributeInputMode::Type> InputMode;

    UPROPERTY(meta = (RequiredInput = "false"))
    FExpressionInput Input;

    UPROPERTY(EditAnywhere, Category = MaterialExpressionSceneColor)
    FVector2D ConstInput;

#if WITH_EDITOR
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
#endif
private:
    // The stock Custom expression the game's read compiles through. Not authored graph data.
    UPROPERTY(Transient) UMaterialExpressionCustom* CompileExpression = nullptr;
};

// How much of a GroundSoft material's lit color shows over the scene behind it (the game's soft blend into the ground).
// The game's base pass gives a GroundSoft material its fogged lighting times this, plus its emissive unfogged (the
// scene behind it, which the material reads). A custom output, as stock's Clear Coat Bottom Normal: Kadan's format
// applies it when the material's C4MaterialDrawOrder is GroundSoft; the editor's own shaders ignore it.
UCLASS(collapsecategories, hidecategories = Object)
class KADAN_API UMaterialExpressionKadanGroundSoftAlpha : public UMaterialExpressionCustomOutput
{
    GENERATED_UCLASS_BODY()

    UPROPERTY(meta = (RequiredInput = "true"))
    FExpressionInput Input;

#if WITH_EDITOR
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
    virtual uint32 GetInputType(int32 InputIndex) override { return MCT_Float; }
#endif
    virtual int32 GetNumOutputs() const override { return 1; }
    virtual FString GetFunctionName() const override { return TEXT("GetKadanGroundSoftAlpha"); }
    virtual FString GetDisplayName() const override { return TEXT("Kadan Ground Soft Alpha"); }
};

USTRUCT()
struct KADAN_API FKadanGameCustomInput
{
    GENERATED_BODY()
    UPROPERTY()
    FName InputName;
    UPROPERTY()
    FExpressionInput Input;
};

// A game Custom formula that reads a binding only the game's renderer has (the scene color copy of an
// opaque game material, the scene's own stencil). It keeps the Custom node's code, output type and named
// inputs. The editor draws it as zero, except the game's scene color copy decode where the editor has the
// copy itself (a translucent material's base pass): that is stock Unreal's own decode, compiled as it is.
UCLASS(collapsecategories, hidecategories = Object)
class KADAN_API UMaterialExpressionKadanGameCustom : public UMaterialExpression
{
    GENERATED_UCLASS_BODY()

    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom, meta = (MultiLine = true))
    FString Code;

    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom)
    TEnumAsByte<ECustomMaterialOutputType> OutputType;

    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom)
    FString Description;

    UPROPERTY()
    TArray<FKadanGameCustomInput> Inputs;

#if WITH_EDITOR
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
    virtual const TArray<FExpressionInput*> GetInputs() override;
    virtual FExpressionInput* GetInput(int32 InputIndex) override;
    virtual FName GetInputName(int32 InputIndex) const override;
#endif
private:
    // The stock Custom expression the scene color copy decode compiles through. Not authored graph data.
    UPROPERTY(Transient) UMaterialExpressionCustom* SceneColorCompileExpression = nullptr;
};

USTRUCT()
struct KADAN_API FKadanReflectionCustomOutput
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom) FName OutputName;
    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom) TEnumAsByte<ECustomMaterialOutputType> OutputType;
};
USTRUCT()
struct KADAN_API FKadanReflectionCustomDefine
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom) FString DefineName;
    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom) FString DefineValue;
};
// Owns the original formula/input data. Delegates compilation to the genuine
// native Custom expression through FMaterialCompiler's public virtual API;
// it never derives from the Engine Custom class's nonexported virtuals.
UCLASS(collapsecategories, hidecategories = Object)
class KADAN_API UMaterialExpressionKadanReflectionCustom : public UMaterialExpressionKadanGameCustom
{
    GENERATED_UCLASS_BODY()
    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom) TArray<FKadanReflectionCustomOutput> AdditionalOutputs;
    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom) TArray<FKadanReflectionCustomDefine> AdditionalDefines;
    UPROPERTY(EditAnywhere, Category = MaterialExpressionCustom) TArray<FString> IncludeFilePaths;
#if WITH_EDITOR
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
    virtual uint32 GetOutputType(int32 OutputIndex) override;
    virtual bool IsResultMaterialAttributes(int32 OutputIndex) override;
    virtual TArray<FExpressionOutput>& GetOutputs() override;
#endif
private:
    // Stable native identity for the translator's per-expression/input cache.
    // It is absent from authored graph JSON and package export serialization.
    UPROPERTY(Transient, Instanced) UMaterialExpressionCustom* NativeCompileExpression;
};

// The scene depth the editor draws for a game scene depth read: far behind any surface of a map.
constexpr float KadanFarSceneDepth = 1.0e8f;

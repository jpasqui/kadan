#pragma once
#include "CoreMinimal.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionComponentMask.h"

namespace P4OriginalPositiveInfinityOperation
{
inline const TCHAR* Description()
{
    return TEXT("Original positive infinity equality");
}
inline const TCHAR* Code()
{
    return TEXT("return asuint(Value) == 0x7f800000u ? Equal : Unequal;");
}

inline bool Scalar(const FExpressionInput& Input, TSet<UMaterialExpression*>& Active)
{
    UMaterialExpression* Expression = Input.Expression;
    if (!Expression || Input.OutputIndex < 0 || Input.OutputIndex >= Expression->GetOutputs().Num() ||
        Active.Contains(Expression) || Active.Num() >= 128)
        return false;
    if (Input.Mask)
        return int32(Input.MaskR != 0) + int32(Input.MaskG != 0) + int32(Input.MaskB != 0) + int32(Input.MaskA != 0) ==
            1;
    if (Input.OutputIndex != 0)
        return false;
    const FString Kind = Expression->GetClass()->GetName();
    if (Kind == TEXT("MaterialExpressionConstant") || Kind == TEXT("MaterialExpressionScalarParameter") ||
        Kind == TEXT("MaterialExpressionDotProduct"))
        return true;
    if (const auto* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
        return int32(Mask->R) + int32(Mask->G) + int32(Mask->B) + int32(Mask->A) == 1;
    const TSet<FString> Arithmetic = {TEXT("MaterialExpressionAdd"), TEXT("MaterialExpressionSubtract"),
        TEXT("MaterialExpressionMultiply"), TEXT("MaterialExpressionDivide"), TEXT("MaterialExpressionPower"),
        TEXT("MaterialExpressionMin"), TEXT("MaterialExpressionMax"), TEXT("MaterialExpressionLinearInterpolate"),
        TEXT("MaterialExpressionAbs"), TEXT("MaterialExpressionSine"), TEXT("MaterialExpressionCosine"),
        TEXT("MaterialExpressionFloor"), TEXT("MaterialExpressionCeil"), TEXT("MaterialExpressionFrac"),
        TEXT("MaterialExpressionSaturate"), TEXT("MaterialExpressionOneMinus"), TEXT("MaterialExpressionSquareRoot"),
        TEXT("MaterialExpressionNormalize"), TEXT("MaterialExpressionIf"),
        TEXT("MaterialExpressionStaticSwitchParameter")};
    const auto* Choice = Cast<UMaterialExpressionCustom>(Expression);
    if (!Arithmetic.Contains(Kind) &&
        (!Choice || Choice->Code != Code() || Choice->Description != Description() ||
            Choice->OutputType != CMOT_Float1 || Choice->AdditionalOutputs.Num() != 0))
        return false;
    Active.Add(Expression);
    for (const FExpressionInput* Child : Expression->GetInputs())
        if (Child && Child->Expression && !Scalar(*Child, Active))
        {
            Active.Remove(Expression);
            return false;
        }
    Active.Remove(Expression);
    return true;
}

// The producer separately proves the complete original programs and identical
// unequal arms. This checks the real editor operation and scalar branch inputs;
// it never licenses an Inf Constant or arbitrary nonfinite JSON property.
inline bool Matches(const UMaterialExpressionCustom* Choice, FString& Error)
{
    const auto Refuse = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (!Choice || Choice->Code != Code() || Choice->OutputType != CMOT_Float1 || Choice->Inputs.Num() != 3 ||
        Choice->AdditionalOutputs.Num() != 0)
        return Refuse(TEXT("material-graph-original-positive-infinity-operation-differs"));
    const FName Names[] = {FName(TEXT("Value")), FName(TEXT("Equal")), FName(TEXT("Unequal"))};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        TSet<UMaterialExpression*> Active;
        if (Choice->Inputs[Index].InputName != Names[Index] || !Scalar(Choice->Inputs[Index].Input, Active))
            return Refuse(TEXT("material-graph-original-positive-infinity-scalar-inputs-differ"));
    }
    return true;
}
} //namespace P4OriginalPositiveInfinityOperation

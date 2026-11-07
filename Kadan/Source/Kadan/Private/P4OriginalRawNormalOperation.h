#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"

namespace P4OriginalRawNormalOperation
{
inline const TCHAR* Description()
{
    return TEXT("Original encoded normal texture channels");
}
inline const TCHAR* Code()
{
    return TEXT("return Texture2DSample(Texture, TextureSampler, UV);");
}

// The original POSTFIXED producer has proved TEX 2D, the asset sampler, the
// exact UV expression and no automatic mip bias. Verify the real constructed
// resource too. TextureObject retains Normal compression; Custom never calls
// the TextureSample compiler's UnpackNormalMap path.
inline bool Matches(const UMaterialExpressionCustom* Sample, FString& Error)
{
    const auto Refuse = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (!Sample || Sample->Code != Code() || Sample->OutputType != CMOT_Float4 || Sample->Inputs.Num() != 2 ||
        Sample->AdditionalOutputs.Num() != 0)
        return Refuse(TEXT("material-graph-original-raw-normal-operation-differs"));
    const FCustomInput& Resource = Sample->Inputs[0];
    const FCustomInput& UV = Sample->Inputs[1];
    if (Resource.InputName != FName(TEXT("Texture")) || UV.InputName != FName(TEXT("UV")) ||
        Resource.Input.OutputIndex != 0 || Resource.Input.Mask || !UV.Input.Expression)
        return Refuse(TEXT("material-graph-original-raw-normal-inputs-differ"));
    const UMaterialExpressionTextureObject* Object = Cast<UMaterialExpressionTextureObject>(Resource.Input.Expression);
    const UTexture2D* Texture = Object ? Cast<UTexture2D>(Object->Texture) : nullptr;
    if (!Object || Object->GetClass() != UMaterialExpressionTextureObject::StaticClass() ||
        Object->SamplerType != SAMPLERTYPE_Normal || !Texture || Texture->CompressionSettings != TC_Normalmap ||
        Texture->SRGB)
        return Refuse(TEXT("material-graph-original-raw-normal-resource-differs"));
    return true;
}
} //namespace P4OriginalRawNormalOperation

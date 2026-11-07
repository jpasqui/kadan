#include "P4OriginalRawNormalOperation.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4MaterialGraph.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalRawNormalSampling, "P4MapKit.Material.OriginalRawNormalSampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalRawNormalSampling::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const FString Prefix = TEXT("/Game/P4RawNormalTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UTexture2D* Texture = NewObject<UTexture2D>(
        CreatePackage(*(Prefix + TEXT("_Texture"))), TEXT("Normal"), RF_Public | RF_Standalone);
    UMaterial* Material = nullptr;
    ON_SCOPE_EXIT
    {
        Texture->ClearFlags(RF_Public | RF_Standalone);
        Texture->MarkPendingKill();
        if (Material)
        {
            Material->ClearFlags(RF_Public | RF_Standalone);
            Material->MarkPendingKill();
        }
    };
    Texture->CompressionSettings = TC_Normalmap;
    Texture->SRGB = false;
    const FString Graph = FString::Printf(
        TEXT(
            "{\"format\":2,\"domain\":\"MD_PostProcess\",\"shading_model\":\"MSM_Unlit\",\"nodes\":{"
            "\"Resource\":{\"type\":\"TextureObject\",\"inputs\":{},\"props\":{\"Texture\":\"%s\",\"SamplerType\":\"SAMPLERTYPE_Normal\"}},"
            "\"UV\":{\"type\":\"Constant2Vector\",\"inputs\":{},\"props\":{\"R\":0.17,\"G\":0.83}},"
            "\"Raw\":{\"type\":\"Custom\",\"inputs\":{\"Texture\":[\"Resource\",\"\"],\"UV\":[\"UV\",\"\"]},"
            "\"props\":{\"Code\":\"return Texture2DSample(Texture, TextureSampler, UV);\",\"OutputType\":\"CMOT_Float4\","
            "\"Description\":\"Original encoded normal texture channels\",\"Inputs\":[{\"InputName\":\"Texture\"},{\"InputName\":\"UV\"}]}},"
            "\"Other\":{\"type\":\"Constant\",\"inputs\":{},\"props\":{\"R\":0.31}}},"
            "\"outputs\":{\"EmissiveColor\":[\"Raw\",\"RGB\"],\"Opacity\":[\"Other\",\"\"]}}"),
        *Texture->GetPathName());
    FString Error;
    Material = P4BuildMaterialFromGraph(Graph, Prefix + TEXT("_Material"), Error);
    if (!TestNotNull(TEXT("The actual importer constructs the fixed raw normal operation"), Material))
    {
        AddError(Error);
        return false;
    }
    UMaterialExpressionCustom* Sample = nullptr;
    for (UMaterialExpression* Expression : Material->Expressions)
        if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
            Sample = Custom;
    TestTrue(TEXT("Constructed operation retains direct sample, resource type, and UV input"),
        P4OriginalRawNormalOperation::Matches(Sample, Error));
    if (!Sample)
        return false;
    TestFalse(TEXT("The direct code has no normal unpack"), Sample->Code.Contains(TEXT("UnpackNormalMap")));
    // Connect makes an RGB ComponentMask for a float4 Custom without a dedicated RGB pin.
    // Require that one exact helper, its ordered RGB selection, and the original whole sample.
    auto* RGB = Cast<UMaterialExpressionComponentMask>(Material->EmissiveColor.Expression);
    const auto WholePin = [](const FExpressionInput& Input, UMaterialExpression* Producer)
    {
        if (!Producer || Input.Expression != Producer || Input.OutputIndex != 0 || Producer->GetOutputs().Num() == 0)
            return false;
        const FExpressionOutput& Pin = Producer->GetOutputs()[0];
        return Input.Mask == Pin.Mask && Input.MaskR == Pin.MaskR && Input.MaskG == Pin.MaskG &&
            Input.MaskB == Pin.MaskB && Input.MaskA == Pin.MaskA;
    };
    const auto OriginalRGBRoute = [&]()
    {
        return RGB && RGB->GetClass() == UMaterialExpressionComponentMask::StaticClass() && RGB->R && RGB->G &&
            RGB->B && !RGB->A && WholePin(RGB->Input, Sample) && WholePin(Material->EmissiveColor, RGB);
    };
    TestTrue(TEXT("Emissive selects original raw RGB through exactly one importer mask"), OriginalRGBRoute());
    const auto* Other = Cast<UMaterialExpressionConstant>(Material->Opacity.Expression);
    TestTrue(TEXT("Unrelated output keeps its authored constant"), Other && Other->R == float(.31));
    if (RGB)
    {
        RGB->A = true;
        TestFalse(TEXT("Including raw alpha changes the original RGB output"), OriginalRGBRoute());
        RGB->A = false;
        RGB->G = false;
        TestFalse(TEXT("Dropping green changes the original RGB output"), OriginalRGBRoute());
        RGB->G = true;
        const FExpressionInput SavedInput = RGB->Input;
        RGB->Input.Expression = Material->Opacity.Expression;
        TestFalse(TEXT("A different producer cannot stand in for the original sample"), OriginalRGBRoute());
        RGB->Input = SavedInput;
        RGB->Input.OutputIndex = 1;
        TestFalse(TEXT("A different sample output cannot establish whole raw channels"), OriginalRGBRoute());
        RGB->Input = SavedInput;
        RGB->Input.MaskR = !SavedInput.MaskR;
        TestFalse(TEXT("An extra source channel mask changes the route"), OriginalRGBRoute());
        RGB->Input = SavedInput;
        const FColorMaterialInput SavedRoot = Material->EmissiveColor;
        Material->EmissiveColor.Expression = Sample;
        TestFalse(TEXT("Bypassing the RGB helper changes the declared output route"), OriginalRGBRoute());
        Material->EmissiveColor = SavedRoot;
        TestTrue(TEXT("Connection controls restore the exact original RGB route"), OriginalRGBRoute());
    }
    const auto* UV = Cast<UMaterialExpressionConstant2Vector>(Sample->Inputs[1].Input.Expression);
    TestTrue(TEXT("UV producer keeps both authored values"), UV && UV->R == float(.17) && UV->G == float(.83));
    const FString Readback = P4MaterialGraphJson(Material, Error);
    TSharedPtr<FJsonObject> Decoded;
    TestTrue(TEXT("The genuine editor expression graph is independently readable"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Readback), Decoded));
    TestTrue(
        TEXT("Readback keeps the exact raw sampling code"), Readback.Contains(P4OriginalRawNormalOperation::Code()));
    TestTrue(TEXT("Readback keeps the actual original texture identity"), Readback.Contains(Texture->GetPathName()));
    TestTrue(TEXT("Readback keeps Normal sampler type"), Readback.Contains(TEXT("SAMPLERTYPE_Normal")));

    Texture->CompressionSettings = TC_Default;
    TestFalse(TEXT("A different compression type cannot establish the original raw normal resource"),
        P4OriginalRawNormalOperation::Matches(Sample, Error));
    Texture->CompressionSettings = TC_Normalmap;
    Texture->SRGB = true;
    TestFalse(TEXT("sRGB conversion changes the encoded-channel contract"),
        P4OriginalRawNormalOperation::Matches(Sample, Error));
    Texture->SRGB = false;
    Sample->Code = TEXT("return UnpackNormalMap(Texture2DSample(Texture, TextureSampler, UV));");
    TestFalse(TEXT("A second normal unpack is a meaningful operation change"),
        P4OriginalRawNormalOperation::Matches(Sample, Error));
    Sample->Code = P4OriginalRawNormalOperation::Code();
    Sample->Inputs[0].Input.Mask = 1;
    Sample->Inputs[0].Input.MaskR = 1;
    TestFalse(TEXT("A channel value cannot stand in for a texture resource"),
        P4OriginalRawNormalOperation::Matches(Sample, Error));
    Sample->Inputs[0].Input.Mask = 0;
    Sample->Inputs[0].Input.MaskR = 0;
    TestTrue(TEXT("Negative controls restore the original resource and operation"),
        P4OriginalRawNormalOperation::Matches(Sample, Error));
    return !HasAnyErrors();
}
#endif

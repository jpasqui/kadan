#include "P4BlenderData.h"
#include "P4BlenderExport.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "AssetExportTask.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/Compression.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealExporter.h"

// The data checks compare with what the Python originals give for the same input: material_t3d.parse
// and json.dumps(indent=1) of Python 3.11 (the expected texts below are their output, unchanged).
namespace
{
const TCHAR* const FixtureT3D = TEXT(R"T3D(Begin Object Class=/Script/Engine.Material Name="M_Fixture"
   Begin Object Class=/Script/Engine.MaterialExpressionMultiply Name="MaterialExpressionMultiply_0"
   End Object
   Begin Object Class=/Script/Engine.MaterialExpressionVertexColor Name="MaterialExpressionVertexColor_0"
   End Object
   Begin Object Class=/Script/Engine.MaterialExpressionComment Name="MaterialExpressionComment_0"
   End Object
   Begin Object Class=/Script/Engine.MaterialExpressionMaterialFunctionCall Name="MaterialExpressionMaterialFunctionCall_0"
   End Object
   Begin Object Class=/Script/Engine.MaterialExpressionStaticSwitch Name="MaterialExpressionStaticSwitch_0"
   End Object
   Begin Object Class=/Script/Engine.MaterialExpressionScalarParameter Name="MaterialExpressionScalarParameter_0"
   End Object
   Begin Object Name="MaterialExpressionMultiply_0"
      A=(Expression=MaterialExpressionVertexColor'"MaterialExpressionVertexColor_0"',OutputIndex=4,Mask=1,MaskA=1)
      B=(Expression=MaterialExpressionScalarParameter'"MaterialExpressionScalarParameter_0"')
      ConstB=1.500000
      MaterialExpressionEditorX=-400
      MaterialExpressionEditorY=16
      Material=Material'"M_Fixture"'
   End Object
   Begin Object Name="MaterialExpressionVertexColor_0"
      MaterialExpressionEditorX=-800
      Desc="say \"hi\""
   End Object
   Begin Object Name="MaterialExpressionComment_0"
      Text="a note"
   End Object
   Begin Object Name="MaterialExpressionMaterialFunctionCall_0"
      FunctionInputs(0)=(ExpressionInputId=1234ABCD,Input=(Expression=MaterialExpressionMultiply'"MaterialExpressionMultiply_0"',InputName="UVs"))
      FunctionInputs(2)=(ExpressionInputId=99,Input=(OutputIndex=0,InputName="Tint"))
      FunctionOutputs(0)=(ExpressionOutputId=55,Output=(OutputName="Result"))
      MaterialFunction=MaterialFunction'"/Engine/Functions/MF_Thing.MF_Thing"'
   End Object
   Begin Object Name="MaterialExpressionStaticSwitch_0"
      A=(Expression=MaterialExpressionMaterialFunctionCall'"MaterialExpressionMaterialFunctionCall_0"',OutputIndex=1)
      Inputs(0)=(Expression=MaterialExpressionVertexColor'"MaterialExpressionVertexColor_0"',Mask=1,MaskR=1,MaskG=1)
      DefaultValue=True
      Position=(X=1.0,Y=-2.5,Z=3)
      Weird=(X=1,Q=2)
   End Object
   Begin Object Name="MaterialExpressionScalarParameter_0"
      ParameterName="7"
      Group="Look"
      DefaultValue=0.250000
      SortPriority=32
      Color=(R=0.5,G=1.000000,B=0.000000,A=1.000000)
   End Object
   BaseColor=(Expression=MaterialExpressionStaticSwitch'"MaterialExpressionStaticSwitch_0"')
   Opacity=(Expression=MaterialExpressionVertexColor'"MaterialExpressionVertexColor_0"',OutputIndex=4,Mask=1,MaskA=1)
   AmbientOcclusion=(Expression=MaterialExpressionMultiply'"MaterialExpressionMultiply_0"')
   BlendMode=BLEND_Translucent
End Object)T3D");
const TCHAR* const FixtureGraph = TEXT(R"T3D({
 "nodes": {
  "MaterialExpressionMultiply_0": {
   "type": "Multiply",
   "inputs": {
    "A": [
     "MaterialExpressionVertexColor_0",
     "A",
     4
    ],
    "B": [
     "MaterialExpressionScalarParameter_0",
     ""
    ]
   },
   "props": {
    "ConstB": 1.5,
    "MaterialExpressionEditorX": -400.0,
    "MaterialExpressionEditorY": 16.0,
    "Material": "Material'\"M_Fixture\"'"
   }
  },
  "MaterialExpressionVertexColor_0": {
   "type": "VertexColor",
   "inputs": {},
   "props": {
    "MaterialExpressionEditorX": -800.0,
    "Desc": "say \\\"hi\\"
   }
  },
  "MaterialExpressionMaterialFunctionCall_0": {
   "type": "MaterialFunctionCall",
   "inputs": {
    "FunctionInputs0": [
     "MaterialExpressionMultiply_0",
     ""
    ]
   },
   "props": {
    "MaterialFunction": "MaterialFunction'\"/Engine/Functions/MF_Thing.MF_Thing\"'",
    "FunctionInputs": [
     [
      "UVs",
      "1234ABCD"
     ],
     [
      "",
      ""
     ],
     [
      "Tint",
      "99"
     ]
    ],
    "FunctionOutputs": [
     [
      "Result",
      "55"
     ]
    ]
   }
  },
  "MaterialExpressionStaticSwitch_0": {
   "type": "StaticSwitch",
   "inputs": {
    "A": [
     "MaterialExpressionMaterialFunctionCall_0",
     "",
     1
    ],
    "Inputs0": [
     "MaterialExpressionVertexColor_0",
     "RG"
    ]
   },
   "props": {
    "DefaultValue": "True",
    "Position": [
     1.0,
     -2.5,
     3.0,
     0.0
    ]
   }
  },
  "MaterialExpressionScalarParameter_0": {
   "type": "ScalarParameter",
   "inputs": {},
   "props": {
    "ParameterName": "7",
    "Group": "Look",
    "DefaultValue": 0.25,
    "SortPriority": 32.0,
    "Color": [
     0.5,
     1.0,
     0.0,
     1.0
    ]
   }
  }
 },
 "outputs": {
  "BaseColor": [
   "MaterialExpressionStaticSwitch_0",
   ""
  ],
  "Opacity": [
   "MaterialExpressionVertexColor_0",
   "A",
   4
  ]
 }
})T3D");
const TCHAR* const DumpExpected = TEXT(R"T3D({
 "a": [
  1,
  2.0,
  "x\u00e9\u007f\"\\\n"
 ],
 "b": {},
 "c": [],
 "d": null,
 "e": true,
 "f": -0.0,
 "g": Infinity,
 "h": [
  0.1,
  1e-05,
  1e+16,
  1000000000000000.0,
  0.0001,
  1.2345678901234568e+17,
  0.10000000149011612,
  5e-324,
  1.7976931348623157e+308,
  100.0,
  2.5
 ]
})T3D");

TSharedPtr<FJsonObject> ParseJson(const FString& Text)
{
    TSharedPtr<FJsonObject> Object;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    FJsonSerializer::Deserialize(Reader, Object);
    return Object;
}

// The rows of an 8-bit PNG the export wrote: (color type, width, height, unfiltered rows).
bool ReadPng(const TArray<uint8>& Png, uint8& Color, int32& Width, int32& Height, TArray<uint8>& Rows)
{
    if (Png.Num() < 8 || Png[0] != 0x89 || Png[1] != 'P')
        return false;
    TArray<uint8> Data;
    for (int32 At = 8; At + 12 <= Png.Num();)
    {
        const int32 Length = (Png[At] << 24) | (Png[At + 1] << 16) | (Png[At + 2] << 8) | Png[At + 3];
        const FString Tag = FString::Printf(TEXT("%c%c%c%c"), Png[At + 4], Png[At + 5], Png[At + 6], Png[At + 7]);
        const uint8* Body = &Png[At + 8];
        if (Tag == TEXT("IHDR"))
        {
            Width = (Body[0] << 24) | (Body[1] << 16) | (Body[2] << 8) | Body[3];
            Height = (Body[4] << 24) | (Body[5] << 16) | (Body[6] << 8) | Body[7];
            if (Body[8] != 8)
                return false;
            Color = Body[9];
        }
        else if (Tag == TEXT("IDAT"))
            Data.Append(Body, Length);
        At += 12 + Length;
    }
    const int32 Channels = Color == 0 ? 1 : Color == 2 ? 3 : 4;
    TArray<uint8> Raw;
    Raw.SetNumUninitialized((Width * Channels + 1) * Height);
    if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), Raw.Num(), Data.GetData(), Data.Num()))
        return false;
    Rows.Reset();
    for (int32 Row = 0; Row < Height; ++Row)
    {
        if (Raw[Row * (Width * Channels + 1)] != 0)
            return false;
        Rows.Append(&Raw[Row * (Width * Channels + 1) + 1], Width * Channels);
    }
    return true;
}

TArray<uint8> TgaHeader(uint8 Kind, int32 Width, int32 Height, uint8 Bits, uint8 Descriptor)
{
    return {0, 0, Kind, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8(Width), uint8(Width >> 8), uint8(Height), uint8(Height >> 8),
        Bits, Descriptor};
}

const TArray<double>* NumberList(const TSharedPtr<FJsonObject>& Object, const FString& Key, TArray<double>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Items;
    if (!Object.IsValid() || !Object->TryGetArrayField(Key, Items))
        return nullptr;
    Out.Reset();
    for (const auto& Item : *Items)
    {
        double V = 0;
        Item->TryGetNumber(V);
        Out.Add(V);
    }
    return &Out;
}

FString Strings(const TSharedPtr<FJsonObject>& Object, const FString& Key)
{
    const TArray<TSharedPtr<FJsonValue>>* Items;
    if (!Object.IsValid() || !Object->TryGetArrayField(Key, Items))
        return TEXT("<none>");
    FString Out;
    bool First = true;
    for (const auto& Item : *Items)
    {
        double Number;
        const FString Text = Item->Type == EJson::Number && Item->TryGetNumber(Number)
            ? FString::Printf(TEXT("%g"), Number)
            : Item->AsString();
        Out += (First ? TEXT("") : TEXT(" ")) + Text;
        First = false;
    }
    return Out;
}

// A material built with Unreal's own calls, in a package of its own under /Temp, and a texture with source pixels.
struct FMaterialFixture
{
    TStrongObjectPtr<UTexture2D> Texture;
    TStrongObjectPtr<UMaterial> Material;
    TStrongObjectPtr<UMaterialInstanceConstant> Instance;
    TMap<FString, UMaterialExpression*> Nodes;

    FMaterialFixture()
    {
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
        UPackage* TexturePackage = CreatePackage(*(TEXT("/Temp/KadanBlender/T_Blender") + Id));
        Texture.Reset(NewObject<UTexture2D>(TexturePackage, *(TEXT("T_Blender") + Id), RF_Public));
        TArray<uint8> Pixels;
        for (int32 I = 0; I < 4 * 2 * 4; ++I)
            Pixels.Add(uint8(I * 7));
        Texture->Source.Init(4, 2, 1, 1, TSF_BGRA8, Pixels.GetData());
        Texture->SRGB = false;
        Texture->PostEditChange();
        UPackage* Package = CreatePackage(*(TEXT("/Temp/KadanBlender/M_Blender") + Id));
        Material.Reset(NewObject<UMaterial>(Package, *(TEXT("M_Blender") + Id), RF_Public));
        UMaterial* M = Material.Get();
        auto Make = [&](UClass* Class, int32 X, int32 Y)
        {
            UMaterialExpression* Node = UMaterialEditingLibrary::CreateMaterialExpression(M, Class, X, Y);
            Nodes.Add(Class->GetName().Mid(18), Node);
            return Node;
        };
        auto* UV = Cast<UMaterialExpressionTextureCoordinate>(
            Make(UMaterialExpressionTextureCoordinate::StaticClass(), -900, 40));
        UV->UTiling = 2.0f;
        auto* Mask = Cast<UMaterialExpressionComponentMask>(
            Make(UMaterialExpressionComponentMask::StaticClass(), -700, 40));
        Mask->R = true;
        Mask->G = false;
        Mask->B = false;
        Mask->A = false;
        auto* Color = Cast<UMaterialExpressionConstant3Vector>(
            Make(UMaterialExpressionConstant3Vector::StaticClass(), -700, -200));
        Color->Constant = FLinearColor(0.25f, 0.5f, 1.0f, 1.0f);
        auto* Multiply = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -400, 0));
        Multiply->ConstB = 0.1f;
        auto* Sample = Cast<UMaterialExpressionTextureSample>(
            Make(UMaterialExpressionTextureSample::StaticClass(), -400, 300));
        Sample->Texture = Texture.Get();
        Sample->SamplerType = SAMPLERTYPE_LinearColor;
        auto* Strength = Cast<UMaterialExpressionScalarParameter>(
            Make(UMaterialExpressionScalarParameter::StaticClass(), -400, 600));
        Strength->ParameterName = TEXT("Strength");
        Strength->DefaultValue = 0.3f;
        UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), Mask, TEXT(""));
        UMaterialEditingLibrary::ConnectMaterialExpressions(Color, TEXT(""), Multiply, TEXT("A"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(Mask, TEXT(""), Multiply, TEXT("B"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), Sample, TEXT("UVs"));
        UMaterialEditingLibrary::ConnectMaterialProperty(Multiply, TEXT(""), MP_BaseColor);
        UMaterialEditingLibrary::ConnectMaterialProperty(Sample, TEXT("G"), MP_EmissiveColor);
        UMaterialEditingLibrary::ConnectMaterialProperty(Strength, TEXT(""), MP_Roughness);
        UMaterialEditingLibrary::ConnectMaterialProperty(Mask, TEXT(""), MP_AmbientOcclusion);
        M->BlendMode = BLEND_Masked;
        M->TwoSided = true;
        M->OpacityMaskClipValue = 0.25f;
        M->UpdateCachedExpressionData();
        UPackage* InstancePackage = CreatePackage(*(TEXT("/Temp/KadanBlender/MI_Blender") + Id));
        Instance.Reset(NewObject<UMaterialInstanceConstant>(InstancePackage, *(TEXT("MI_Blender") + Id), RF_Public));
        Instance->SetParentEditorOnly(M);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Strength")), 0.8f);
    }
};
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderPythonJson, "P4MapKit.Blender.PythonJson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderPythonJson::RunTest(const FString&)
{
    FP4PyJson Value = FP4PyJson::Dict();
    FP4PyJson A = FP4PyJson::List();
    A.Add(FP4PyJson::Integer(1));
    A.Add(FP4PyJson::Number(2.0));
    FString Text = TEXT("x");
    Text += TCHAR(0xe9);
    Text += TCHAR(0x7f);
    Text += TEXT("\"\\\n");
    A.Add(FP4PyJson::Text(Text));
    Value.Set(TEXT("a"), MoveTemp(A));
    Value.Set(TEXT("b"), FP4PyJson::Dict());
    Value.Set(TEXT("c"), FP4PyJson::List());
    Value.Set(TEXT("d"), FP4PyJson::Null());
    Value.Set(TEXT("e"), FP4PyJson::Boolean(true));
    Value.Set(TEXT("f"), FP4PyJson::Number(-0.0));
    Value.Set(TEXT("g"), FP4PyJson::Number(INFINITY));
    FP4PyJson H = FP4PyJson::List();
    for (double V : {0.1, 1e-05, 1e16, 1e15, 0.0001, 123456789012345680.0, (double)0.1f, 5e-324, 1.7976931348623157e308,
             100.0, 2.5})
        H.Add(FP4PyJson::Number(V));
    Value.Set(TEXT("h"), MoveTemp(H));
    TestEqual(TEXT("The JSON is what Python's json.dumps(indent=1) writes"), Value.Dump(),
        FString(DumpExpected).TrimEnd().Replace(TEXT("\r\n"), TEXT("\n")));
    FP4PyJson Order = FP4PyJson::Dict();
    Order.Set(TEXT("z"), FP4PyJson::Integer(1));
    Order.Set(TEXT("a"), FP4PyJson::Integer(2));
    Order.Set(TEXT("z"), FP4PyJson::Integer(3));
    TestEqual(TEXT("A key set again keeps its place, as in a Python dict"), Order.Dump(),
        FString(TEXT("{\n \"z\": 3,\n \"a\": 2\n}")));
    double Parsed = 0;
    TestTrue(
        TEXT("float() reads a T3D number"), P4Blender::PythonParseFloat(TEXT(" -1.500000 "), Parsed) && Parsed == -1.5);
    TestTrue(TEXT("float() reads an exponent and underscores"),
        P4Blender::PythonParseFloat(TEXT("1_0e-1"), Parsed) && Parsed == 1.0);
    TestTrue(TEXT("float() reads inf"),
        P4Blender::PythonParseFloat(TEXT("-Infinity"), Parsed) && Parsed < 0 && !FMath::IsFinite(Parsed));
    TestFalse(TEXT("float() refuses True"), P4Blender::PythonParseFloat(TEXT("True"), Parsed));
    TestFalse(TEXT("float() refuses a lone dot"), P4Blender::PythonParseFloat(TEXT("."), Parsed));
    TestFalse(TEXT("float() refuses a trailing underscore"), P4Blender::PythonParseFloat(TEXT("1_"), Parsed));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderMaterialText, "P4MapKit.Blender.MaterialText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderMaterialText::RunTest(const FString&)
{
    const FString Text = FString(FixtureT3D).Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\n"), TEXT("\r\n"));
    TestEqual(TEXT("The graph is the one material_t3d.parse reads"), P4Blender::ParseMaterialT3D(Text).Dump(),
        FString(FixtureGraph).Replace(TEXT("\r\n"), TEXT("\n")));
    const TMap<FString, TArray<FString>> Blocks = P4Blender::TextBlocks(Text);
    TestTrue(TEXT("The material's own block holds its pins"),
        Blocks.Contains(TEXT("M_Fixture")) && Blocks[TEXT("M_Fixture")].Contains(TEXT("BlendMode=BLEND_Translucent")));
    TestEqual(TEXT("A declared and defined node has one block"), Blocks[TEXT("MaterialExpressionMultiply_0")].Num(), 6);
    TestEqual(TEXT("A link without an expression is not a link"), P4Blender::ParseLink(TEXT("(OutputIndex=1)")).Dump(),
        FString(TEXT("null")));
    TestEqual(TEXT("A short link"),
        P4Blender::ParseLink(TEXT("(Expression=MaterialExpressionAdd'\"MaterialExpressionAdd_3\"',Mask=1,MaskB=1)"))
            .Dump(),
        FString(TEXT("[\n \"MaterialExpressionAdd_3\",\n \"B\"\n]")));
    TestEqual(TEXT("A full path link"),
        P4Blender::ParseLink(
            TEXT("(Expression=MaterialExpressionAdd'/Game/M.M:MaterialExpressionAdd_3',OutputIndex=2)"))
            .Dump(),
        FString(TEXT("[\n \"MaterialExpressionAdd_3\",\n \"\",\n 2\n]")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderPythonNames, "P4MapKit.Blender.PythonNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderPythonNames::RunTest(const FString&)
{
    TestEqual(TEXT("bTwoSided"), P4Blender::KitPythonName(TEXT("bTwoSided")), FString(TEXT("two_sided")));
    TestEqual(TEXT("UTiling"), P4Blender::KitPythonName(TEXT("UTiling")), FString(TEXT("u_tiling")));
    TestEqual(TEXT("ConstB"), P4Blender::KitPythonName(TEXT("ConstB")), FString(TEXT("const_b")));
    TestEqual(TEXT("SamplerType"), P4Blender::KitPythonName(TEXT("SamplerType")), FString(TEXT("sampler_type")));
    TestEqual(TEXT("R"), P4Blender::KitPythonName(TEXT("R")), FString(TEXT("r")));
    const auto Property = [](UClass* Class, const TCHAR* Name) { return Class->FindPropertyByName(Name); };
    TestEqual(TEXT("Python's name of bTwoSided"),
        P4Blender::PropertyPythonName(Property(UMaterial::StaticClass(), TEXT("TwoSided"))),
        FString(TEXT("two_sided")));
    TestEqual(TEXT("Python's name of UTiling"),
        P4Blender::PropertyPythonName(Property(UMaterialExpressionTextureCoordinate::StaticClass(), TEXT("UTiling"))),
        FString(TEXT("u_tiling")));
    UEnum* Blend = StaticEnum<EBlendMode>();
    TestEqual(TEXT("BlendMode"), P4Blender::EnumPythonName(Blend), FString(TEXT("BlendMode")));
    TestEqual(TEXT("BLEND_MASKED"), P4Blender::EnumEntryPythonName(Blend, Blend->GetIndexByValue(BLEND_Masked)),
        FString(TEXT("BLEND_MASKED")));
    UEnum* Sampler = StaticEnum<EMaterialSamplerType>();
    TestEqual(TEXT("SAMPLERTYPE_LINEAR_COLOR"),
        P4Blender::EnumEntryPythonName(Sampler, Sampler->GetIndexByValue(SAMPLERTYPE_LinearColor)),
        FString(TEXT("SAMPLERTYPE_LINEAR_COLOR")));
    UEnum* Shading = StaticEnum<EMaterialShadingModel>();
    TestEqual(TEXT("MSM_DEFAULT_LIT"),
        P4Blender::EnumEntryPythonName(Shading, Shading->GetIndexByValue(MSM_DefaultLit)),
        FString(TEXT("MSM_DEFAULT_LIT")));
    TestTrue(TEXT("get_editor_property('two_sided') reads TwoSided"),
        P4Blender::EditorProperty(UMaterial::StaticClass(), TEXT("two_sided")) ==
            Property(UMaterial::StaticClass(), TEXT("TwoSided")));
    TestTrue(TEXT("get_editor_property('r') reads R"),
        P4Blender::EditorProperty(UMaterialExpressionComponentMask::StaticClass(), TEXT("r")) ==
            Property(UMaterialExpressionComponentMask::StaticClass(), TEXT("R")));
    TestNull(TEXT("get_editor_property of a name no property has"),
        P4Blender::EditorProperty(UMaterial::StaticClass(), TEXT("no_such_thing")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderTgaToPng, "P4MapKit.Blender.TgaToPng",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderTgaToPng::RunTest(const FString&)
{
    // 24 bits, bottom row first: BGR pixels become RGB rows from the top.
    TArray<uint8> Tga = TgaHeader(2, 2, 2, 24, 0);
    Tga.Append({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    TArray<uint8> Png, Rows;
    FString Error;
    uint8 Color = 9;
    int32 Width = 0, Height = 0;
    TestTrue(TEXT("A plain TGA converts"), P4Blender::TgaToPng(Tga, Png, Error));
    TestTrue(TEXT("The PNG reads back"), ReadPng(Png, Color, Width, Height, Rows));
    TestEqual(TEXT("RGB"), (int32)Color, 2);
    TestTrue(TEXT("Rows are top first and RGB"), Rows == TArray<uint8>({9, 8, 7, 12, 11, 10, 3, 2, 1, 6, 5, 4}));
    // 32 bits, run-length packets, top row first.
    Tga = TgaHeader(10, 3, 1, 32, 0x28);
    Tga.Append({0x81, 10, 20, 30, 40, 0x00, 1, 2, 3, 4});
    TestTrue(TEXT("A run-length TGA converts"),
        P4Blender::TgaToPng(Tga, Png, Error) && ReadPng(Png, Color, Width, Height, Rows));
    TestEqual(TEXT("RGBA"), (int32)Color, 6);
    TestTrue(TEXT("Runs and raw packets"), Rows == TArray<uint8>({30, 20, 10, 40, 30, 20, 10, 40, 3, 2, 1, 4}));
    // 8 bits, gray.
    Tga = TgaHeader(3, 2, 1, 8, 0x20);
    Tga.Append({5, 250});
    TestTrue(
        TEXT("A gray TGA converts"), P4Blender::TgaToPng(Tga, Png, Error) && ReadPng(Png, Color, Width, Height, Rows));
    TestTrue(TEXT("Gray"), Color == 0 && Rows == TArray<uint8>({5, 250}));
    Tga = TgaHeader(1, 2, 1, 8, 0);
    TestFalse(TEXT("A color-mapped TGA is refused"), P4Blender::TgaToPng(Tga, Png, Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderMaterialJson, "P4MapKit.Blender.MaterialJson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderMaterialJson::RunTest(const FString&)
{
    FMaterialFixture F;
    const TSharedPtr<FJsonObject> Data = ParseJson(UKadanBlender::BlenderMaterialJson(F.Material.Get()));
    if (!TestTrue(TEXT("The material's JSON reads"), Data.IsValid()))
        return false;
    TestEqual(TEXT("class"), Data->GetStringField(TEXT("class")), FString(TEXT("Material")));
    TestEqual(TEXT("root"), Data->GetStringField(TEXT("root")), F.Material->GetPathName());
    TestEqual(TEXT("parent"), Data->GetStringField(TEXT("parent")), FString());
    const TSharedPtr<FJsonObject> Settings = Data->GetObjectField(TEXT("material"));
    TestEqual(TEXT("blend_mode"), Strings(Settings, TEXT("blend_mode")), FString(TEXT("enum BlendMode BLEND_MASKED")));
    TestEqual(TEXT("shading_model"), Strings(Settings, TEXT("shading_model")),
        FString(TEXT("enum MaterialShadingModel MSM_DEFAULT_LIT")));
    TestTrue(TEXT("two_sided"), Settings->GetBoolField(TEXT("two_sided")));
    TestEqual(TEXT("opacity_mask_clip_value"), Settings->GetNumberField(TEXT("opacity_mask_clip_value")), 0.25);
    const TSharedPtr<FJsonObject> Graph = Data->GetObjectField(TEXT("graph"));
    const TSharedPtr<FJsonObject> Nodes = Graph->GetObjectField(TEXT("nodes"));
    TestEqual(TEXT("Every node is in the graph"), Nodes->Values.Num(), 6);
    auto NodeOf = [&](const FString& Type)
    {
        UMaterialExpression* Expression = F.Nodes.FindRef(Type);
        return Expression && Nodes->HasField(Expression->GetName()) ? Nodes->GetObjectField(Expression->GetName())
                                                                    : TSharedPtr<FJsonObject>();
    };
    const TSharedPtr<FJsonObject> UV = NodeOf(TEXT("TextureCoordinate")), Mask = NodeOf(TEXT("ComponentMask")),
                                  Color = NodeOf(TEXT("Constant3Vector")), Multiply = NodeOf(TEXT("Multiply")),
                                  Sample = NodeOf(TEXT("TextureSample"));
    if (!TestTrue(TEXT("The nodes are named as in Unreal"), UV && Mask && Color && Multiply && Sample))
        return false;
    TestEqual(TEXT("type"), UV->GetStringField(TEXT("type")), FString(TEXT("TextureCoordinate")));
    TestEqual(TEXT("u_tiling"), UV->GetObjectField(TEXT("py"))->GetNumberField(TEXT("u_tiling")), 2.0);
    TArray<double> Numbers;
    TestTrue(TEXT("pos"), NumberList(UV, TEXT("pos"), Numbers) && Numbers == TArray<double>({-900.0, 40.0}));
    TestTrue(TEXT("r"), Mask->GetObjectField(TEXT("py"))->GetBoolField(TEXT("r")));
    TestFalse(TEXT("g is left at its default, so the text export and py leave it out"),
        Mask->GetObjectField(TEXT("py"))->HasField(TEXT("g")));
    TestEqual(TEXT("constant"), Strings(Color->GetObjectField(TEXT("py")), TEXT("constant")),
        FString(TEXT("color 0.25 0.5 1 1")));
    TestEqual(TEXT("const_b keeps its 32-bit value"),
        Multiply->GetObjectField(TEXT("py"))->GetNumberField(TEXT("const_b")), (double)0.1f);
    TestEqual(TEXT("Multiply A"), Strings(Multiply->GetObjectField(TEXT("inputs")), TEXT("A")),
        F.Nodes[TEXT("Constant3Vector")]->GetName() + TEXT(" "));
    TestEqual(TEXT("texture"), Strings(Sample->GetObjectField(TEXT("py")), TEXT("texture")),
        TEXT("asset ") + F.Texture->GetPathName());
    TestEqual(TEXT("sampler_type"), Strings(Sample->GetObjectField(TEXT("py")), TEXT("sampler_type")),
        FString(TEXT("enum MaterialSamplerType SAMPLERTYPE_LINEAR_COLOR")));
    const TSharedPtr<FJsonObject> Outputs = Graph->GetObjectField(TEXT("outputs"));
    TestEqual(TEXT("BaseColor"), Strings(Outputs, TEXT("BaseColor")),
        Multiply.IsValid() ? F.Nodes[TEXT("Multiply")]->GetName() + TEXT(" ") : FString());
    TestEqual(TEXT("EmissiveColor takes the G pin"), Strings(Outputs, TEXT("EmissiveColor")),
        F.Nodes[TEXT("TextureSample")]->GetName() + TEXT(" G 2"));
    TestTrue(TEXT("AmbientOcclusion, an output material_t3d leaves out"), Outputs->HasField(TEXT("AmbientOcclusion")));
    const TSharedPtr<FJsonObject> Instance = ParseJson(UKadanBlender::BlenderMaterialJson(F.Instance.Get()));
    if (!TestTrue(TEXT("The instance's JSON reads"), Instance.IsValid()))
        return false;
    TestEqual(
        TEXT("instance class"), Instance->GetStringField(TEXT("class")), FString(TEXT("MaterialInstanceConstant")));
    TestEqual(TEXT("instance parent"), Instance->GetStringField(TEXT("parent")), F.Material->GetPathName());
    TestEqual(TEXT("instance root"), Instance->GetStringField(TEXT("root")), F.Material->GetPathName());
    TestEqual(TEXT("The instance's own value"),
        Instance->GetObjectField(TEXT("parameters"))->GetObjectField(TEXT("scalars"))->GetNumberField(TEXT("Strength")),
        (double)0.8f);
    TestEqual(TEXT("No value for a material that is not an instance"), UKadanBlender::BlenderMaterialJson(nullptr),
        FString());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderSceneExport, "P4MapKit.Blender.SceneExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderSceneExport::RunTest(const FString&)
{
    FMaterialFixture F;
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("The engine's cube"), Cube))
        return false;
    UPackage* WorldPackage = CreatePackage(
        *(TEXT("/Temp/KadanBlender/BlenderWorld") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(EWorldType::Editor, false, TEXT("BlenderWorld"), WorldPackage));
    if (!TestNotNull(TEXT("A level"), World.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        if (World.IsValid())
            World->DestroyWorld(false);
    };
    const FVector Location(100.f, 200.f, 50.f);
    const FRotator Rotation(20.f, 30.f, 10.f);
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation);
    Actor->SetActorLabel(TEXT("BlenderCube"));
    Actor->SetActorScale3D(FVector(1.f, 2.f, 0.5f));
    Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Actor->GetStaticMeshComponent()->SetMaterial(0, F.Instance.Get());
    // An actor that places its meshes with components, as the game's Blueprints do: one plain, one instanced.
    AActor* Group = World->SpawnActor<AActor>(FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator);
    Group->SetActorLabel(TEXT("Group"));
    USceneComponent* Root = NewObject<USceneComponent>(Group, TEXT("Root"));
    Group->SetRootComponent(Root);
    Group->AddInstanceComponent(Root);
    Root->RegisterComponent();
    Group->SetActorLocation(FVector(1000.f, 0.f, 0.f));
    UStaticMeshComponent* Rock = NewObject<UStaticMeshComponent>(Group, TEXT("Rock"));
    Rock->SetStaticMesh(Cube);
    Rock->SetupAttachment(Root);
    Rock->SetRelativeLocation(FVector(10.f, 0.f, 0.f));
    Group->AddInstanceComponent(Rock);
    Rock->RegisterComponent();
    UInstancedStaticMeshComponent* Leaves = NewObject<UInstancedStaticMeshComponent>(Group, TEXT("Leaves"));
    Leaves->SetStaticMesh(Cube);
    Leaves->SetupAttachment(Root);
    Group->AddInstanceComponent(Leaves);
    Leaves->RegisterComponent();
    Leaves->AddInstance(FTransform(FVector(0.f, 50.f, 0.f)));
    Leaves->AddInstance(FTransform(FRotator(0.f, 90.f, 0.f), FVector(0.f, -50.f, 0.f), FVector(2.f, 1.f, 1.f)));
    TestEqual(TEXT("A Static Mesh Actor is one part"), P4BlenderParts(Actor).Num(), 1);
    TestEqual(TEXT("A component actor is a part for each mesh and instance"), P4BlenderParts(Group).Num(), 3);
    UExporter* Exporter = UExporter::FindExporter(World.Get(), TEXT("json"));
    TestTrue(TEXT("Unreal's export finds the Blender scene exporter for a level"),
        Exporter && Exporter->IsA<UP4BlenderSceneExporter>());
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Automation/KadanBlender"));
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Folder, false, true);
    };
    const FString SceneFile = Folder / TEXT("Level.v2.json");
    UAssetExportTask* Task = NewObject<UAssetExportTask>();
    TStrongObjectPtr<UAssetExportTask> Hold(Task);
    Task->Object = World.Get();
    Task->Filename = SceneFile;
    Task->bSelected = false;
    Task->bReplaceIdentical = true;
    Task->bPrompt = false;
    Task->bAutomated = true;
    Task->bUseFileArchive = false;
    Task->bWriteEmptyFiles = false;
    TestTrue(TEXT("File > Export All runs"), UExporter::RunAssetExportTask(Task));
    FString Text;
    if (!TestTrue(
            TEXT("The scene file is written where the creator chose"), FFileHelper::LoadFileToString(Text, *SceneFile)))
        return false;
    const TSharedPtr<FJsonObject> Scene = ParseJson(Text);
    if (!TestTrue(TEXT("The scene reads"), Scene.IsValid()))
        return false;
    TestEqual(TEXT("unit"), Scene->GetStringField(TEXT("unit")), FString(TEXT("cm")));
    TestEqual(TEXT("level"), Scene->GetStringField(TEXT("level")), WorldPackage->GetName());
    const TArray<TSharedPtr<FJsonValue>>& Actors = Scene->GetArrayField(TEXT("actors"));
    if (!TestEqual(TEXT("A row for the Static Mesh Actor, the component and each instance"), Actors.Num(), 4))
        return false;
    TMap<FString, TSharedPtr<FJsonObject>> ByLabel;
    for (const auto& Item : Actors)
        ByLabel.Add(Item->AsObject()->GetStringField(TEXT("label")), Item->AsObject());
    const TSharedPtr<FJsonObject> Row = ByLabel.FindRef(TEXT("BlenderCube"));
    if (!TestTrue(TEXT("The rows are named after the actor and component"),
            Row && ByLabel.Contains(TEXT("Group.Rock")) && ByLabel.Contains(TEXT("Group.Leaves.0")) &&
                ByLabel.Contains(TEXT("Group.Leaves.1"))))
        return false;
    TestFalse(TEXT("A Static Mesh Actor's row is the Python kit's, without component keys"),
        Row->HasField(TEXT("component")));
    TArray<double> Place;
    TestTrue(TEXT("A component's row has its world place"),
        NumberList(ByLabel[TEXT("Group.Rock")], TEXT("location"), Place) &&
            Place == TArray<double>({1010.0, 0.0, 0.0}));
    const TSharedPtr<FJsonObject> Instance = ByLabel[TEXT("Group.Leaves.1")];
    const auto Near = [](const TArray<double>& A, std::initializer_list<double> B)
    {
        int32 I = 0;
        for (double V : B)
            if (I >= A.Num() || !FMath::IsNearlyEqual(A[I++], V, 1e-3))
                return false;
        return I == A.Num();
    };
    TArray<double> Scale;
    NumberList(Instance, TEXT("location"), Place);
    NumberList(Instance, TEXT("scale"), Scale);
    AddInfo(FString::Printf(TEXT("Instance 1 is at %s with scale %s."),
        *FString::JoinBy(Place, TEXT(" "), [](double V) { return FString::SanitizeFloat(V); }),
        *FString::JoinBy(Scale, TEXT(" "), [](double V) { return FString::SanitizeFloat(V); })));
    TestTrue(TEXT("An instance's row has its world place and scale"),
        Near(Place, {1000.0, -50.0, 0.0}) && Near(Scale, {2.0, 1.0, 1.0}));
    TestTrue(TEXT("An instance's row names its actor, component and index"),
        Instance->GetStringField(TEXT("actor")) == TEXT("Group") &&
            Instance->GetStringField(TEXT("component")) == TEXT("Leaves") &&
            Instance->GetIntegerField(TEXT("instance")) == 1);
    TestTrue(TEXT("An instance's bounds are its own"),
        NumberList(Instance, TEXT("origin"), Place) && FMath::IsNearlyEqual(Place[0], 1000.0, 0.01) &&
            FMath::IsNearlyEqual(Place[1], -50.0, 0.01));
    TestEqual(TEXT("label"), Row->GetStringField(TEXT("label")), FString(TEXT("BlenderCube")));
    TestEqual(TEXT("mesh"), Row->GetStringField(TEXT("mesh")), FString(TEXT("/Engine/BasicShapes/Cube")));
    TestEqual(TEXT("The actor's own material"), Strings(Row, TEXT("materials")), F.Instance->GetOutermost()->GetName());
    TArray<double> Numbers;
    TestTrue(TEXT("location"),
        NumberList(Row, TEXT("location"), Numbers) && Numbers == TArray<double>({100.0, 200.0, 50.0}));
    TestTrue(TEXT("scale"), NumberList(Row, TEXT("scale"), Numbers) && Numbers == TArray<double>({1.0, 2.0, 0.5}));
    const FQuat Quat = Actor->GetActorQuat();
    TestTrue(TEXT("rotation is x y z w"),
        NumberList(Row, TEXT("rotation"), Numbers) &&
            Numbers == TArray<double>({(double)Quat.X, (double)Quat.Y, (double)Quat.Z, (double)Quat.W}));
    const TSharedPtr<FJsonObject> Mesh =
        Scene->GetObjectField(TEXT("meshes"))->GetObjectField(TEXT("/Engine/BasicShapes/Cube"));
    TestEqual(TEXT("mesh file"), Mesh->GetStringField(TEXT("file")), FString(TEXT("meshes/Cube.fbx")));
    TestTrue(
        TEXT("The FBX is next to the scene"), IFileManager::Get().FileSize(*(Folder / TEXT("meshes/Cube.fbx"))) > 1000);
    const TSharedPtr<FJsonObject> Materials = Scene->GetObjectField(TEXT("materials"));
    TestEqual(TEXT("The instance and the cube's own material"), Materials->Values.Num(), 2);
    FString Written;
    const FString InstanceFile = Materials->GetStringField(F.Instance->GetOutermost()->GetName());
    TestTrue(TEXT("The instance file is the material's JSON"),
        FFileHelper::LoadFileToString(Written, *(Folder / InstanceFile)) &&
            Written == UKadanBlender::BlenderMaterialJson(F.Instance.Get()));
    const TSharedPtr<FJsonObject> Textures = Scene->GetObjectField(TEXT("textures"));
    const FString TexturePath = F.Texture->GetOutermost()->GetName();
    if (!TestTrue(TEXT("The texture the material samples"), Textures->HasField(TexturePath)))
        return false;
    const TSharedPtr<FJsonObject> Facts = Textures->GetObjectField(TexturePath);
    TestFalse(TEXT("srgb"), Facts->GetBoolField(TEXT("srgb")));
    TestFalse(TEXT("normal"), Facts->GetBoolField(TEXT("normal")));
    TArray<uint8> Png, Rows;
    uint8 Color = 0;
    int32 Width = 0, Height = 0;
    TestTrue(TEXT("The PNG reads"),
        FFileHelper::LoadFileToArray(Png, *(Folder / Facts->GetStringField(TEXT("file")))) &&
            ReadPng(Png, Color, Width, Height, Rows));
    TestTrue(TEXT("The PNG has the texture's size"), Width == 4 && Height == 2 && Color == 6);
    TArray<uint8> Expected;
    for (int32 I = 0; I < 8; ++I)
        Expected.Append({uint8((I * 4 + 2) * 7), uint8((I * 4 + 1) * 7), uint8(I * 4 * 7), uint8((I * 4 + 3) * 7)});
    TestTrue(TEXT("The PNG has the texture's source pixels"), Rows == Expected);
    TestFalse(TEXT("The TGA Unreal wrote is gone"),
        FPaths::FileExists(
            Folder / TEXT("textures") / FPaths::GetBaseFilename(Facts->GetStringField(TEXT("file"))) + TEXT(".tga")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderMaterialExport, "P4MapKit.Blender.MaterialExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderMaterialExport::RunTest(const FString&)
{
    FMaterialFixture F;
    UExporter* Exporter = UExporter::FindExporter(F.Instance.Get(), TEXT("json"));
    TestTrue(TEXT("Asset Actions > Export finds the Blender materials exporter for an instance"),
        Exporter && Exporter->IsA<UP4BlenderMaterialExporter>());
    Exporter = UExporter::FindExporter(F.Material.Get(), TEXT("json"));
    TestTrue(TEXT("and for a material"), Exporter && Exporter->IsA<UP4BlenderMaterialExporter>());
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Automation/KadanBlenderMaterials"));
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Folder, false, true);
    };
    // The export task Asset Actions > Export and Bulk Export run for each selected asset.
    const FString SceneFile = Folder / TEXT("Game/Temp") / (F.Instance->GetName() + TEXT(".json"));
    UAssetExportTask* Task = NewObject<UAssetExportTask>();
    TStrongObjectPtr<UAssetExportTask> Hold(Task);
    Task->Object = F.Instance.Get();
    Task->Filename = SceneFile;
    Task->bSelected = false;
    Task->bReplaceIdentical = true;
    Task->bPrompt = false;
    Task->bAutomated = true;
    TestTrue(TEXT("The asset export runs"), UExporter::RunAssetExportTask(Task));
    FString Text;
    if (!TestTrue(TEXT("The scene file is written"), FFileHelper::LoadFileToString(Text, *SceneFile)))
        return false;
    const TSharedPtr<FJsonObject> Scene = ParseJson(Text);
    if (!TestTrue(TEXT("The scene reads"), Scene.IsValid()))
        return false;
    TestEqual(TEXT("No actors"), Scene->GetArrayField(TEXT("actors")).Num(), 0);
    TestEqual(TEXT("No meshes"), Scene->GetObjectField(TEXT("meshes"))->Values.Num(), 0);
    TestFalse(
        TEXT("No meshes folder"), IFileManager::Get().DirectoryExists(*(FPaths::GetPath(SceneFile) / TEXT("meshes"))));
    const TSharedPtr<FJsonObject> Materials = Scene->GetObjectField(TEXT("materials"));
    TestEqual(TEXT("The one material"), Materials->Values.Num(), 1);
    FString Written;
    TestTrue(TEXT("Its JSON is the material's"),
        Materials->HasField(F.Instance->GetOutermost()->GetName()) &&
            FFileHelper::LoadFileToString(Written,
                *(FPaths::GetPath(SceneFile) / Materials->GetStringField(F.Instance->GetOutermost()->GetName()))) &&
            Written == UKadanBlender::BlenderMaterialJson(F.Instance.Get()));
    const TSharedPtr<FJsonObject> Textures = Scene->GetObjectField(TEXT("textures"));
    TestTrue(TEXT("The texture its root material samples"),
        Textures->HasField(F.Texture->GetOutermost()->GetName()) &&
            FPaths::FileExists(FPaths::GetPath(SceneFile) /
                Textures->GetObjectField(F.Texture->GetOutermost()->GetName())->GetStringField(TEXT("file"))));
    // Several at once from a script.
    const FString Both = Folder / TEXT("Both/Materials.json");
    const TSharedPtr<FJsonObject> Pair = ParseJson(
        UKadanBlender::ExportMaterialsForBlender(Both, {F.Material.Get(), F.Instance.Get()}));
    TestTrue(TEXT("Two materials in one scene"),
        Pair.IsValid() && Pair->GetObjectField(TEXT("materials"))->Values.Num() == 2 &&
            Pair->GetArrayField(TEXT("actors")).Num() == 0 && FPaths::FileExists(Both));
    TestEqual(TEXT("Nothing is written for no materials"),
        UKadanBlender::ExportMaterialsForBlender(Folder / TEXT("None/Materials.json"), {}), FString());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlenderGameMaterialParent, "P4MapKit.Blender.GameMaterialParent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlenderGameMaterialParent::RunTest(const FString&)
{
    FMaterialFixture F;
    TestEqual(TEXT("A material the project holds needs no import"),
        UKadanBlender::ImportGameMaterial(F.Material->GetPathName()), FString());
    // No game folder: a clear message, nothing imported. The project's own setting is put back.
    FString Before;
    const bool Had = GConfig->GetString(TEXT("Kadan"), TEXT("GameFolder"), Before, GEditorPerProjectIni);
    GConfig->SetString(TEXT("Kadan"), TEXT("GameFolder"), TEXT(""), GEditorPerProjectIni);
    ON_SCOPE_EXIT
    {
        if (Had)
            GConfig->SetString(TEXT("Kadan"), TEXT("GameFolder"), *Before, GEditorPerProjectIni);
        else
            GConfig->RemoveKey(TEXT("Kadan"), TEXT("GameFolder"), GEditorPerProjectIni);
    };
    const FString Missing = TEXT("/Game/KadanBlenderTest/M_NotInThisProject");
    const FString Message = UKadanBlender::ImportGameMaterial(Missing);
    TestEqual(TEXT("The message names the material and the setting"), Message,
        FString(
            TEXT("The game material M_NotInThisProject is not in this project, and no game folder is set in Kadan. ")
                TEXT("Set the game folder on the Import page of the Kadan panel and run the script again.")));
    TestFalse(TEXT("Kit text has no colons or semicolons"), Message.Contains(TEXT(":")) || Message.Contains(TEXT(";")));
    TestFalse(TEXT("Nothing was made"), FPackageName::DoesPackageExist(Missing));
    return !HasAnyErrors();
}
#endif

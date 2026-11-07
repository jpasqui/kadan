#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "P4MapKitCore/quality.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Async/Async.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"
#include "P4NativeHash.h"
#include "Misc/PackageName.h"
#include "AssetRegistryModule.h"
#include "HAL/ThreadSafeBool.h"
#include "P4OriginalDecode.h"
#include "HAL/ThreadSafeCounter.h"
#include "P4NativeValueStaging.h"
#include "HAL/PlatformFileManager.h"
#include "P4MapKitCore/bulk_data.h"
#include "Serialization/BulkData.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "P4MapKitCore/texture_payload.h"
#include "Engine/Texture2D.h"
#include "P4TextureAuthoring.h"
#include "P4TextureProperties.h"
#include "P4StaticMeshStaging.h"
#include "P4TextureContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS)
namespace P4MaterialGraphTests
{
const TCHAR* const Graph = TEXT(R"JSON({
 "nodes": {
  "TextureCoordinate_0": {"type": "TextureCoordinate", "inputs": {}, "props": {"CoordinateIndex": 1}},
  "TextureSampleParameter2D_0": {"type": "TextureSampleParameter2D", "inputs": {"Coordinates": ["TextureCoordinate_0", ""]}, "props": {"ParameterName": "Diffuse_01"}},
  "ScalarParameter_0": {"type": "ScalarParameter", "inputs": {}, "props": {"ParameterName": "Amount", "DefaultValue": 0.35}},
  "VectorParameter_0": {"type": "VectorParameter", "inputs": {}, "props": {"ParameterName": "Tint", "DefaultValue": [0.25, 0.5, 0.75, 1.0]}},
  "LinearInterpolate_0": {"type": "LinearInterpolate", "inputs": {"A": ["TextureSampleParameter2D_0", "RGB"], "B": ["VectorParameter_0", "RGB"], "Alpha": ["TextureSampleParameter2D_0", "A"]}, "props": {}},
  "Multiply_0": {"type": "Multiply", "inputs": {"A": ["LinearInterpolate_0", ""], "B": ["ScalarParameter_0", ""]}, "props": {}},
  "Constant_0": {"type": "Constant", "inputs": {}, "props": {"R": 0.5}},
  "OneMinus_0": {"type": "OneMinus", "inputs": {"Input": ["TextureSampleParameter2D_0", "G"]}, "props": {}},
  "Sine_0": {"type": "Sine", "inputs": {"Input": ["TextureCoordinate_0", "G"]}, "props": {"Period": 0}},
  "Transform_0": {"type": "Transform", "inputs": {"Input": ["VectorParameter_0", "RGB"]}, "props": {"TransformSourceType": "TRANSFORMSOURCE_World", "TransformType": "TRANSFORM_Tangent"}}
 },
 "outputs": {"BaseColor": ["Multiply_0", ""], "Specular": ["Constant_0", ""], "Roughness": ["OneMinus_0", ""], "Metallic": ["Multiply_0", "R"],
             "OpacityMask": ["Sine_0", ""], "Normal": ["Transform_0", ""]},
 "blend_mode": "BLEND_Masked", "two_sided": true, "shading_model": "MSM_DefaultLit", "clip": 0.25, "tangent_space_normal": true
})JSON");

TSharedPtr<FJsonObject> Parse(const FString& Json)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Out);
    return Out.IsValid() ? Out : MakeShared<FJsonObject>();
}
bool Same(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
{
    return FJsonValue::CompareEqual(FJsonValueObject(A), FJsonValueObject(B));
}
FString LinkText(const TSharedPtr<FJsonObject>& Owner, const TCHAR* Name)
{
    FString Out;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (Owner.IsValid() && Owner->TryGetArrayField(Name, Items))
        for (const auto& Item : *Items)
            Out += Item->AsString() + TEXT("|");
    return Out;
}
// The graph with one text replaced, for the refusals.
FString With(const TCHAR* From, const TCHAR* To)
{
    const FString Out = FString(Graph).Replace(From, To, ESearchCase::CaseSensitive);
    check(!Out.Equals(Graph, ESearchCase::CaseSensitive));
    return Out;
}
} //namespace P4MaterialGraphTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialGraphTest, "P4MapKit.Quality.MaterialGraphBuiltAndReadBack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialGraphTest::RunTest(const FString& Parameters)
{
    using namespace P4MaterialGraphTests;
    (void)Parameters;
    const FString Folder = TEXT("/Game/NativeJobTests/Materials/");
    const TArray<FString> Packages{Folder + TEXT("M_Graph"), Folder + TEXT("M_GraphAgain"),
        Folder + TEXT("M_GraphSaved"), TEXT("/Temp/P4MaterialGraphReload")};
    FString SavedFile;
    FPackageName::TryConvertLongPackageNameToFilename(Packages[2], SavedFile, TEXT(".uasset"));
    SavedFile = FPaths::ConvertRelativePathToFull(SavedFile);
    for (int32 Index = 0; Index < 3; ++Index)
        if (!TestFalse(TEXT("nothing is at the test's places before it"),
                FPackageName::DoesPackageExist(Packages[Index]) || FindPackage(nullptr, *Packages[Index]) != nullptr))
            return false;
    ON_SCOPE_EXIT
    {
        for (const FString& Package : Packages)
            if (UPackage* Made = FindPackage(nullptr, *Package))
            {
                TArray<UObject*> Inside;
                GetObjectsWithOuter(Made, Inside, false);
                for (UObject* Object : Inside)
                    if (Object->IsAsset())
                    {
                        if (!Package.StartsWith(TEXT("/Temp/")))
                            FAssetRegistryModule::AssetDeleted(Object);
                        Object->ClearFlags(RF_Public | RF_Standalone);
                        Object->MarkPendingKill();
                    }
                ResetLoaders(Made);
                Made->SetDirtyFlag(false);
            }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        IFileManager::Get().Delete(*SavedFile, false, true, true);
        IFileManager::Get().DeleteDirectory(*FPaths::GetPath(SavedFile), false, false);
    };

    FString Error;
    UMaterial* Material = P4BuildMaterialFromGraph(Graph, Packages[0], Error);
    if (!TestNotNull(TEXT("the material is made"), Material))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("it is an asset at its path"),
        Material->IsAsset() && Material->GetPathName() == Packages[0] + TEXT(".M_Graph"));
    TestEqual(TEXT("ten nodes and the two masks their links need"), Material->Expressions.Num(), 12);
    TestEqual(TEXT("blend mode"), int32(Material->BlendMode.GetValue()), int32(BLEND_Masked));
    TestTrue(TEXT("two sided"), Material->TwoSided != 0);
    TestEqual(TEXT("clip value"), Material->OpacityMaskClipValue, 0.25f);
    const FString ReadText = P4MaterialGraphJson(Material, Error);
    if (!TestFalse(TEXT("its graph can be read back"), ReadText.IsEmpty()))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("reading it twice says the same"), P4MaterialGraphJson(Material, Error), ReadText);
    const TSharedPtr<FJsonObject> Read = Parse(ReadText);
    const TSharedPtr<FJsonObject> Nodes = Read->GetObjectField(TEXT("nodes")),
                                  Outputs = Read->GetObjectField(TEXT("outputs"));
    TestEqual(TEXT("nodes read back"), Nodes->Values.Num(), 12);
    const auto Node = [&](const TCHAR* Id)
    { return Nodes->HasField(Id) ? Nodes->GetObjectField(Id) : MakeShared<FJsonObject>(); };
    const auto Props = [&](const TCHAR* Id)
    { return Node(Id)->HasField(TEXT("props")) ? Node(Id)->GetObjectField(TEXT("props")) : MakeShared<FJsonObject>(); };
    const auto Inputs = [&](const TCHAR* Id)
    {
        return Node(Id)->HasField(TEXT("inputs")) ? Node(Id)->GetObjectField(TEXT("inputs"))
                                                  : MakeShared<FJsonObject>();
    };
    TestEqual(TEXT("a parameter's name"), Props(TEXT("ScalarParameter_0"))->GetStringField(TEXT("ParameterName")),
        FString(TEXT("Amount")));
    TestEqual(TEXT("a parameter's default, to the float"),
        Props(TEXT("ScalarParameter_0"))->GetNumberField(TEXT("DefaultValue")), double(0.35f));
    TestEqual(TEXT("a color"), LinkText(Props(TEXT("VectorParameter_0")), TEXT("DefaultValue")),
        FString(TEXT("0.25|0.5|0.75|1|")));
    TestEqual(TEXT("a whole number"), Props(TEXT("TextureCoordinate_0"))->GetIntegerField(TEXT("CoordinateIndex")), 1);
    TestEqual(TEXT("a number that differs from the node's default"),
        Props(TEXT("Sine_0"))->GetNumberField(TEXT("Period")), 0.0);
    TestEqual(TEXT("a named choice"), Props(TEXT("Transform_0"))->GetStringField(TEXT("TransformType")),
        FString(TEXT("TRANSFORM_Tangent")));
    TestEqual(TEXT("a link to the node's own pin for the channels"),
        LinkText(Inputs(TEXT("LinearInterpolate_0")), TEXT("A")), FString(TEXT("TextureSampleParameter2D_0|RGB|")));
    TestEqual(TEXT("a link to the alpha pin"), LinkText(Inputs(TEXT("LinearInterpolate_0")), TEXT("Alpha")),
        FString(TEXT("TextureSampleParameter2D_0|A|4|")));
    TestEqual(
        TEXT("a whole link"), LinkText(Inputs(TEXT("Multiply_0")), TEXT("A")), FString(TEXT("LinearInterpolate_0||")));
    TestEqual(TEXT("channels with no pin go through a mask"), LinkText(Outputs, TEXT("Metallic")),
        FString(TEXT("ComponentMask_1||")));
    TestEqual(TEXT("the mask takes the node"), LinkText(Inputs(TEXT("ComponentMask_1")), TEXT("Input")),
        FString(TEXT("Multiply_0||")));
    TestTrue(TEXT("the mask picks the channel"),
        Props(TEXT("ComponentMask_1"))->GetBoolField(TEXT("R")) &&
            !Props(TEXT("ComponentMask_1"))->HasField(TEXT("G")));
    TestEqual(TEXT("settings read back"),
        Read->GetStringField(TEXT("blend_mode")) + TEXT(" ") + Read->GetStringField(TEXT("shading_model")),
        FString(TEXT("BLEND_Masked MSM_DefaultLit")));
    TestTrue(TEXT("two sided read back"),
        Read->GetBoolField(TEXT("two_sided")) && Read->GetBoolField(TEXT("tangent_space_normal")));
    TestEqual(TEXT("clip read back"), Read->GetNumberField(TEXT("clip")), 0.25);

    // Built again from what was read, it reads the same.
    UMaterial* Again = P4BuildMaterialFromGraph(ReadText, Packages[1], Error);
    if (TestNotNull(TEXT("a material is made from the graph read back"), Again))
    {
        TestEqual(TEXT("with no node added"), Again->Expressions.Num(), 12);
        TestTrue(TEXT("and the same graph"), Same(Parse(P4MaterialGraphJson(Again, Error)), Read));
    }
    else
        AddError(Error);

    // Saved through the plugin's save, and read from the saved file.
    const TSharedPtr<FJsonObject> Report = Parse(P4BuildMaterialGraphReport(Graph, Packages[2], true));
    if (TestTrue(TEXT("built and saved"), Report->GetBoolField(TEXT("valid")) && Report->GetBoolField(TEXT("saved"))))
    {
        TestTrue(TEXT("the report holds the graph"), Same(Report->GetObjectField(TEXT("graph")), Read));
        TestTrue(TEXT("the file is there"), FPaths::FileExists(SavedFile));
        UPackage* Second = LoadPackage(CreatePackage(*Packages[3]), *SavedFile, LOAD_NoWarn);
        UMaterial* Loaded = Second ? FindObject<UMaterial>(Second, TEXT("M_GraphSaved")) : nullptr;
        if (TestNotNull(TEXT("the saved file loads"), Loaded))
            TestTrue(
                TEXT("the saved material's graph is the same"), Same(Parse(P4MaterialGraphJson(Loaded, Error)), Read));
    }
    else
        AddError(Report->GetStringField(TEXT("error")));

    // What is refused, each for its own reason, and nothing is left behind.
    const FString Refused = Folder + TEXT("M_GraphRefused");
    const auto Refuses = [&](const FString& Bad, const FString& Destination, const TCHAR* Why)
    {
        FString Reason;
        TestNull(Why, P4BuildMaterialFromGraph(Bad, Destination, Reason));
        TestTrue(FString(Why) + TEXT(": ") + Reason, Reason.StartsWith(Why));
    };
    Refuses(TEXT("not json"), Refused, TEXT("material-graph-not-a-graph"));
    Refuses(Graph, TEXT("/Engine/M_Graph"), TEXT("material-graph-destination-not-in-content"));
    Refuses(Graph, Packages[0], TEXT("material-graph-destination-holds-another-asset"));
    Refuses(With(TEXT("\"type\": \"OneMinus\""), TEXT("\"type\": \"NoSuchNode\"")), Refused,
        TEXT("material-graph-node-type-the-engine-does-not-have"));
    Refuses(
        With(TEXT("\"Period\": 0"), TEXT("\"Nope\": 0")), Refused, TEXT("material-graph-node-has-no-such-property"));
    Refuses(With(TEXT("\"Period\": 0"), TEXT("\"Period\": \"fast\"")), Refused,
        TEXT("material-graph-property-needs-a-number"));
    Refuses(With(TEXT("\"TRANSFORM_Tangent\""), TEXT("\"TRANSFORM_Sideways\"")), Refused,
        TEXT("material-graph-property-needs-one-of-its-names"));
    Refuses(With(TEXT("\"Input\": [\"TextureSampleParameter2D_0\", \"G\"]"),
                TEXT("\"Nope\": [\"TextureSampleParameter2D_0\", \"G\"]")),
        Refused, TEXT("material-graph-node-has-no-such-input"));
    Refuses(With(TEXT("[\"LinearInterpolate_0\", \"\"]"), TEXT("[\"Missing_0\", \"\"]")), Refused,
        TEXT("material-graph-link-to-a-node-that-is-not-there"));
    Refuses(With(TEXT("[\"Multiply_0\", \"R\"]"), TEXT("[\"Multiply_0\", \"RRRRR\"]")), Refused,
        TEXT("material-graph-swizzle-too-wide"));
    Refuses(With(TEXT("[\"Multiply_0\", \"R\"]"), TEXT("[\"Multiply_0\"]")), Refused,
        TEXT("material-graph-link-malformed"));
    Refuses(With(TEXT("\"Specular\": [\"Constant_0\", \"\"]"), TEXT("\"Shininess\": [\"Constant_0\", \"\"]")), Refused,
        TEXT("material-graph-material-has-no-such-pin"));
    Refuses(With(TEXT("\"ParameterName\": \"Diffuse_01\""),
                TEXT("\"ParameterName\": \"Diffuse_01\", \"Texture\": \"/Game/NativeJobTests/Materials/T_NotThere\"")),
        Refused, TEXT("material-graph-asset-not-in-the-project"));
    Refuses(
        With(TEXT("\"BLEND_Masked\""), TEXT("\"BLEND_Sideways\"")), Refused, TEXT("material-graph-blend-mode-unknown"));
    TestNull(TEXT("a refused graph leaves no material"),
        FindObject<UMaterial>(nullptr, *(Refused + TEXT(".M_GraphRefused"))));
    return true;
}
// A material instance made from the kit's JSON on a material made from a graph: its parent, the
// parameters and the static switch it sets, what it overrides, what the parent has no place for, the
// instance read back, saved with its parent, and read from the saved file.
namespace P4MaterialInstanceTests
{
const TCHAR* const Parent = TEXT(R"JSON({
 "nodes": {
  "TextureCoordinate_0": {"type": "TextureCoordinate", "inputs": {}, "props": {}},
  "TextureSampleParameter2D_0": {"type": "TextureSampleParameter2D", "inputs": {"Coordinates": ["TextureCoordinate_0", ""]}, "props": {"ParameterName": "Diffuse_01"}},
  "ScalarParameter_0": {"type": "ScalarParameter", "inputs": {}, "props": {"ParameterName": "Amount", "DefaultValue": 0.35}},
  "VectorParameter_0": {"type": "VectorParameter", "inputs": {}, "props": {"ParameterName": "Tint", "DefaultValue": [0.25, 0.5, 0.75, 1.0]}},
  "StaticSwitchParameter_0": {"type": "StaticSwitchParameter", "inputs": {"A": ["VectorParameter_0", "RGB"], "B": ["TextureSampleParameter2D_0", "RGB"]}, "props": {"ParameterName": "Use_Tint", "DefaultValue": false}},
  "Multiply_0": {"type": "Multiply", "inputs": {"A": ["StaticSwitchParameter_0", ""], "B": ["ScalarParameter_0", ""]}, "props": {}}
 },
 "outputs": {"BaseColor": ["Multiply_0", ""]},
 "blend_mode": "BLEND_Masked", "two_sided": false, "shading_model": "MSM_DefaultLit", "clip": 0.3333, "tangent_space_normal": true
})JSON");
const TCHAR* const Instance = TEXT(R"JSON({
 "parent": "/Game/NativeJobTests/Materials/M_InstanceParent.M_InstanceParent",
 "scalars": {"Amount": 0.75, "Ghost": 2.0},
 "vectors": {"Tint": [1.0, 0.5, 0.25, 1.0]},
 "textures": {"Diffuse_01": "/Engine/EngineResources/DefaultTexture.DefaultTexture"},
 "switches": {"Use_Tint": true},
 "overrides": {"two_sided": true, "clip": 0.5}
})JSON");
FString With(const TCHAR* From, const TCHAR* To)
{
    const FString Out = FString(Instance).Replace(From, To, ESearchCase::CaseSensitive);
    check(!Out.Equals(Instance, ESearchCase::CaseSensitive));
    return Out;
}
} //namespace P4MaterialInstanceTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstanceTest, "P4MapKit.Quality.MaterialInstanceBuiltAndReadBack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstanceTest::RunTest(const FString& Parameters)
{
    using P4MaterialGraphTests::Parse;
    using P4MaterialGraphTests::Same;
    (void)Parameters;
    const FString Folder = TEXT("/Game/NativeJobTests/Materials/");
    const TArray<FString> Packages{Folder + TEXT("M_InstanceParent"), Folder + TEXT("MI_Instance"),
        Folder + TEXT("MI_InstanceSaved"), TEXT("/Temp/P4MaterialInstanceReload")};
    TArray<FString> Files;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FString File;
        FPackageName::TryConvertLongPackageNameToFilename(Packages[Index], File, TEXT(".uasset"));
        Files.Add(FPaths::ConvertRelativePathToFull(File));
        if (!TestFalse(TEXT("nothing is at the test's places before it"),
                FPaths::FileExists(Files.Last()) || FindPackage(nullptr, *Packages[Index]) != nullptr))
            return false;
    }
    ON_SCOPE_EXIT
    {
        // The instances first: they hold on to the parent.
        for (int32 Index = Packages.Num() - 1; Index >= 0; --Index)
            if (UPackage* Made = FindPackage(nullptr, *Packages[Index]))
            {
                TArray<UObject*> Inside;
                GetObjectsWithOuter(Made, Inside, false);
                for (UObject* Object : Inside)
                    if (Object->IsAsset())
                    {
                        if (!Packages[Index].StartsWith(TEXT("/Temp/")))
                            FAssetRegistryModule::AssetDeleted(Object);
                        Object->ClearFlags(RF_Public | RF_Standalone);
                        Object->MarkPendingKill();
                    }
                ResetLoaders(Made);
                Made->SetDirtyFlag(false);
            }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        for (const FString& File : Files)
            IFileManager::Get().Delete(*File, false, true, true);
        IFileManager::Get().DeleteDirectory(*FPaths::GetPath(Files[0]), false, false);
    };

    FString Error;
    TArray<FString> NotInParent;
    UMaterial* Material = P4BuildMaterialFromGraph(P4MaterialInstanceTests::Parent, Packages[0], Error);
    if (!TestNotNull(TEXT("the parent material is made"), Material))
    {
        AddError(Error);
        return false;
    }
    UMaterialInstanceConstant* Made = P4BuildMaterialInstance(
        P4MaterialInstanceTests::Instance, Packages[1], NotInParent, Error);
    if (!TestNotNull(TEXT("the instance is made"), Made))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("it is an asset at its path"),
        Made->IsAsset() && Made->GetPathName() == Packages[1] + TEXT(".MI_Instance"));
    TestTrue(TEXT("its parent is the material"), Made->Parent == Material);
    TestEqual(TEXT("what the parent has no place for is named"), FString::Join(NotInParent, TEXT(", ")),
        FString(TEXT("scalar Ghost")));
    float Scalar = 0;
    FLinearColor Vector(ForceInit);
    UTexture* Texture = nullptr;
    bool Switch = false;
    FGuid Id;
    TestTrue(TEXT("the scalar is set"),
        Made->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Amount")), Scalar) && Scalar == 0.75f);
    TestTrue(TEXT("the vector is set"),
        Made->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), Vector) &&
            Vector == FLinearColor(1.0f, 0.5f, 0.25f, 1.0f));
    TestTrue(TEXT("the texture is set"),
        Made->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Diffuse_01")), Texture) && Texture &&
            Texture->GetPathName() == TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
    TestTrue(TEXT("the switch is set"),
        Made->GetStaticSwitchParameterValue(FMaterialParameterInfo(TEXT("Use_Tint")), Switch, Id) && Switch);
    TestTrue(TEXT("the switch gives the instance shaders of its own"), Made->bHasStaticPermutationResource != 0);
    TestTrue(TEXT("two sided is overridden"), Made->IsTwoSided() && !Material->IsTwoSided());
    TestEqual(TEXT("the clip value is overridden"), Made->GetOpacityMaskClipValue(), 0.5f);
    // The parent is masked with nothing on its mask pin, which the engine draws as opaque.
    TestEqual(TEXT("the blend mode is the parent's"), int32(Made->GetBlendMode()), int32(Material->GetBlendMode()));

    const FString ReadText = P4MaterialInstanceJson(Made, Error);
    if (!TestFalse(TEXT("the instance can be read back"), ReadText.IsEmpty()))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("read back, it is what it was made from"),
        Same(Parse(ReadText), Parse(P4MaterialInstanceTests::Instance)));
    TestEqual(TEXT("reading it twice says the same"), P4MaterialInstanceJson(Made, Error), ReadText);

    // Saved through the plugin's save, which takes the unsaved parent with it, and read from the file.
    const TSharedPtr<FJsonObject> Report = Parse(
        P4BuildMaterialInstanceReport(P4MaterialInstanceTests::Instance, Packages[2], true));
    if (TestTrue(TEXT("built and saved"), Report->GetBoolField(TEXT("valid")) && Report->GetBoolField(TEXT("saved"))))
    {
        TestTrue(
            TEXT("the report holds the instance"), Same(Report->GetObjectField(TEXT("instance")), Parse(ReadText)));
        TestTrue(TEXT("the instance's file and its parent's are there"),
            FPaths::FileExists(Files[2]) && FPaths::FileExists(Files[0]));
        UPackage* Second = LoadPackage(CreatePackage(*Packages[3]), *Files[2], LOAD_NoWarn);
        UMaterialInstanceConstant* Loaded = Second
            ? FindObject<UMaterialInstanceConstant>(Second, TEXT("MI_InstanceSaved"))
            : nullptr;
        if (TestNotNull(TEXT("the saved file loads"), Loaded))
            TestTrue(TEXT("the saved instance is the same"),
                Same(Parse(P4MaterialInstanceJson(Loaded, Error)), Parse(ReadText)));
    }
    else
        AddError(Report->GetStringField(TEXT("error")));

    const FString Refused = Folder + TEXT("MI_InstanceRefused");
    const auto Refuses = [&](const FString& Bad, const FString& Destination, const TCHAR* Why)
    {
        FString Reason;
        TArray<FString> Unused;
        TestNull(Why, P4BuildMaterialInstance(Bad, Destination, Unused, Reason));
        TestTrue(FString(Why) + TEXT(": ") + Reason, Reason.StartsWith(Why));
    };
    using P4MaterialInstanceTests::With;
    Refuses(TEXT("not json"), Refused, TEXT("material-instance-not-an-instance"));
    Refuses(TEXT("{\"scalars\": {}}"), Refused, TEXT("material-instance-not-an-instance"));
    Refuses(With(TEXT("\"scalars\": {\"Amount\": 0.75, \"Ghost\": 2.0}"), TEXT("\"scalars\": 3")), Refused,
        TEXT("material-instance-not-an-instance"));
    Refuses(P4MaterialInstanceTests::Instance, TEXT("/Engine/MI_Instance"),
        TEXT("material-instance-destination-not-in-content"));
    Refuses(P4MaterialInstanceTests::Instance, Packages[1], TEXT("material-instance-destination-holds-another-asset"));
    Refuses(With(TEXT("M_InstanceParent.M_InstanceParent"), TEXT("M_NotThere.M_NotThere")), Refused,
        TEXT("material-instance-parent-not-in-the-project"));
    Refuses(With(TEXT("\"Amount\": 0.75"), TEXT("\"Amount\": \"much\"")), Refused,
        TEXT("material-instance-scalar-needs-a-number"));
    Refuses(With(TEXT("[1.0, 0.5, 0.25, 1.0]"), TEXT("[1.0, 0.5]")), Refused,
        TEXT("material-instance-vector-needs-four-numbers"));
    Refuses(With(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"),
                TEXT("/Game/NativeJobTests/Materials/T_NotThere.T_NotThere")),
        Refused, TEXT("material-instance-texture-not-in-the-project"));
    Refuses(With(TEXT("\"Use_Tint\": true"), TEXT("\"Use_Tint\": 1")), Refused,
        TEXT("material-instance-switch-needs-true-or-false"));
    Refuses(
        With(TEXT("\"clip\": 0.5"), TEXT("\"shininess\": 0.5")), Refused, TEXT("material-instance-override-unknown"));
    Refuses(With(TEXT("\"two_sided\": true"), TEXT("\"two_sided\": \"yes\"")), Refused,
        TEXT("material-instance-override-malformed"));
    TestNull(TEXT("a refused instance leaves nothing"),
        FindObject<UMaterialInstanceConstant>(nullptr, *(Refused + TEXT(".MI_InstanceRefused"))));
    return true;
}
#endif

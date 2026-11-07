#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialGameSettings.h"
#include "P4MaterialGraph.h"
#include "AssetRegistryModule.h"
#include "IDetailTreeNode.h"
#include "PropertyHandle.h"
#include "IPropertyRowGenerator.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ShaderCompilerCore.h"
#include "UObject/StrongObjectPtr.h"

// A recovered master's C4 settings: the graph's render_settings set them on the material's C4 settings, the graph
// reads them back, Kadan's shader format gives them to each of the material's shader jobs, an edit changes the
// material's id and the jobs' defines, and the material's Details panel shows them in the category C4.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialGameSettingsTest, "P4MapKit.Kadan.MaterialGameSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialGameSettingsTest::RunTest(const FString&)
{
    const FString Package = TEXT("/Game/P4MapKitMaterialGameSettings/C4Settings");
    const FString ObjectPath = Package + TEXT(".C4Settings");
    if (!TestTrue(TEXT("The unsaved test namespace is empty"),
            !FPackageName::DoesPackageExist(Package) && !FindObject<UObject>(nullptr, *ObjectPath)))
        return false;
    const FString Source = TEXT(
        R"JSON({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Translucent","two_sided":false,
        "shading_model":"MSM_DefaultLit","clip":0.3333,"tangent_space_normal":true,
        "render_settings":{"C4MaterialMeshType":"C4MMT_Effects","C4ShaderParam1":0.5,"bC4MaterialParam10":true},
        "nodes":{"Constant_0":{"type":"Constant","inputs":{},"props":{"R":0.5}}},
        "outputs":{"BaseColor":["Constant_0",""],"Opacity":["Constant_0",""]}})JSON");
    FString Error;
    TStrongObjectPtr<UMaterial> Material(P4BuildMaterialFromGraph(Source, Package, Error));
    if (!TestTrue(TEXT("The material is built"), Material.IsValid()))
    {
        AddError(Error);
        return false;
    }
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Material.Get());
        Material->ClearFlags(RF_Public | RF_Standalone);
        if (Material->Rename(nullptr, GetTransientPackage(),
                REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
            Material->MarkPendingKill();
    };

    UP4MaterialGameSettings* Settings = UP4MaterialGameSettings::Find(Material.Get());
    if (!TestNotNull(TEXT("The material keeps C4 settings"), Settings))
        return false;
    TestEqual(TEXT("C4MaterialMeshType"), int32(Settings->C4MaterialMeshType), int32(C4MMT_Effects));
    TestEqual(TEXT("C4ShaderParam1"), Settings->C4ShaderParam1, 0.5f);
    TestTrue(TEXT("bC4MaterialParam10"), Settings->bC4MaterialParam10);
    TestEqual(TEXT("An unset setting keeps the game's default"), Settings->C4ShaderParam0, 1.f);
    TestTrue(TEXT("The settings are not cooked"), Settings->IsEditorOnly());

    const FString Graph = P4MaterialGraphJson(Material.Get(), Error);
    TestTrue(TEXT("The graph reads the C4 settings back"),
        Graph.Contains(TEXT("\"C4ShaderParam1\":0.5")) &&
            Graph.Contains(TEXT("\"C4MaterialMeshType\":\"C4MMT_Effects\"")) &&
            Graph.Contains(TEXT("\"bC4MaterialParam10\":true")));

    // The defines of one of the material's shader jobs, as the engine queues it (named by the material).
    const IShaderFormat* Format = GetTargetPlatformManagerRef().FindShaderFormat(TEXT("GLSL_SWITCH"));
    if (!TestNotNull(TEXT("Kadan's shader format is loaded"), Format))
        return false;
    auto JobDefines = [&]()
    {
        FShaderCompilerInput Input;
        Input.DebugGroupName = Material->GetName() / TEXT("FLocalVertexFactory") /
            TEXT("TBasePassPSFNoLightMapPolicy") / TEXT("0");
        Format->ModifyShaderCompilerInput(Input);
        return Input.Environment.GetDefinitions();
    };
    TMap<FString, FString> Defines = JobDefines();
    TestEqual(TEXT("The job gets C4ShaderParam1"), Defines.FindRef(TEXT("KADAN_C4ShaderParam1")), FString(TEXT("0.5")));
    TestEqual(TEXT("The job gets C4MaterialMeshType by number"), Defines.FindRef(TEXT("KADAN_C4MaterialMeshType")),
        FString(TEXT("12")));
    TestEqual(
        TEXT("The job gets bC4MaterialParam10"), Defines.FindRef(TEXT("KADAN_bC4MaterialParam10")), FString(TEXT("1")));
    TestEqual(TEXT("The job gets a default"), Defines.FindRef(TEXT("KADAN_C4ShaderParam0")), FString(TEXT("1.0")));

    // An edit as the Details panel makes it.
    const FGuid Before = Material->StateId;
    Settings->C4ShaderParam1 = 0.f;
    FProperty* Param1 = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), TEXT("C4ShaderParam1"));
    FPropertyChangedEvent Event(Param1, EPropertyChangeType::ValueSet);
    Settings->PostEditChangeProperty(Event);
    TestNotEqual(
        TEXT("An edit gives the material a new id, so its shaders are compiled again"), Material->StateId, Before);
    TestEqual(TEXT("The job gets the edited value"), JobDefines().FindRef(TEXT("KADAN_C4ShaderParam1")),
        FString(TEXT("0.0")));

    // The material's Details panel.
    FPropertyEditorModule& Editor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    TSharedRef<IPropertyRowGenerator> Rows = Editor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
    Rows->SetObjects({Material.Get()});
    TSharedPtr<IDetailTreeNode> Category;
    for (const TSharedRef<IDetailTreeNode>& Node : Rows->GetRootTreeNodes())
        if (Node->GetNodeType() == EDetailNodeType::Category && Node->GetNodeName() == TEXT("C4"))
            Category = Node;
    if (!TestTrue(TEXT("The Details panel has the category C4"), Category.IsValid()))
        return false;
    TArray<TSharedRef<IDetailTreeNode>> Children;
    Category->GetChildren(Children);
    TSet<FName> Shown;
    TArray<FString> Seen;
    for (const TSharedRef<IDetailTreeNode>& Child : Children)
    {
        const TSharedPtr<IPropertyHandle> Handle = Child->CreatePropertyHandle();
        const FProperty* Property = Handle.IsValid() ? Handle->GetProperty() : nullptr;
        if (Property)
            Shown.Add(Property->GetFName());
        Seen.Add(FString::Printf(
            TEXT("%s=%s"), *Child->GetNodeName().ToString(), Property ? *Property->GetName() : TEXT("-")));
    }
    AddInfo(TEXT("C4 rows: ") + FString::Join(Seen, TEXT(", ")));
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(UP4MaterialGameSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It;
        ++It, ++Count)
        TestTrue(FString::Printf(TEXT("C4 shows %s"), *It->GetName()), Shown.Contains(It->GetFName()));
    TestEqual(TEXT("C4 shows every setting once"), Shown.Num(), Count);
    return true;
}
#endif

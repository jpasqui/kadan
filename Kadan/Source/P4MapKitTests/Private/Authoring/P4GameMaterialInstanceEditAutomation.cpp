#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialInstanceGameSettings.h"
#include "P4MaterialInstance.h"
#include "P4MaterialInstancePermutation.h"
#include "P4MaterialGraph.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanReadOnly.h"
#include "P4GameReplacement.h"
#include "P4GameMaterialInstanceExport.h"
#include "P4AuthoringRows.h"
#include "P4ExportDriver.h"
#include "P4OriginalRecord.h"
#include "MaterialEditingLibrary.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Modules/ModuleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "StaticParameterSet.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InstanceC4Inheritance, "P4MapKit.Kadan.MaterialInstanceC4Inheritance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InstanceC4Inheritance::RunTest(const FString&)
{
    UMaterial* Master = NewObject<UMaterial>();
    auto* MasterSettings = UP4MaterialGameSettings::FindOrAdd(Master);
    MasterSettings->C4ShaderParam1 = 0.5f;
    auto* Parent = NewObject<UMaterialInstanceConstant>();
    Parent->SetParentEditorOnly(Master);
    auto* Child = NewObject<UMaterialInstanceConstant>();
    Child->SetParentEditorOnly(Parent);
    auto* ParentSettings = UP4MaterialInstanceGameSettings::FindOrAdd(Parent);
    ParentSettings->Overrides.Add(TEXT("C4ShaderParam1"));
    ParentSettings->C4ShaderParam1 = 0.25f;
    auto* Settings = UP4MaterialInstanceGameSettings::FindOrAdd(Child);
    auto* Effective = NewObject<UP4MaterialGameSettings>();
    Settings->Resolve(Effective);
    TestEqual(TEXT("An unchecked child inherits the intermediate parent"), Effective->C4ShaderParam1, 0.25f);
    Settings->Overrides.Add(TEXT("C4ShaderParam1"));
    Settings->C4ShaderParam1 = 0.f;
    Settings->Edited.Add(TEXT("C4ShaderParam1"));
    Settings->Resolve(Effective);
    TestEqual(TEXT("An explicit zero overrides the parent"), Effective->C4ShaderParam1, 0.f);
    TestTrue(TEXT("C4-only edit requires an owned map"), P4MaterialInstanceNeedsOwnShaderMap(Child));
    const auto Records = Settings->Records();
    auto Legacy = MakeShared<FJsonObject>();
    Legacy->SetObjectField(TEXT("game_overrides"), Records);
    FString LegacyText;
    FJsonSerializer::Serialize(Legacy, TJsonWriterFactory<>::Create(&LegacyText));
    Parent->RemoveUserDataOfClass(UP4MaterialInstanceGameSettings::StaticClass());
    Parent->GetOutermost()->GetMetaData()->SetValue(Parent, TEXT("P4MaterialInstanceTransportV2"), *LegacyText);
    TestTrue(TEXT("An older parent with untyped C4 transport refuses export"),
        P4MaterialInstanceExportIssue(Child, TEXT("/Game/Mods/InstanceProof"), true)
            .Contains(TEXT("typed C4 settings")));
    TestNull(TEXT("Validation does not invent empty typed settings on the older parent"),
        UP4MaterialInstanceGameSettings::Find(Parent));
    Parent->AddAssetUserData(ParentSettings);
    Parent->GetOutermost()->GetMetaData()->RemoveValue(Parent, TEXT("P4MaterialInstanceTransportV2"));
    auto* Roundtrip = NewObject<UP4MaterialInstanceGameSettings>(Child);
    FString Error;
    TestTrue(TEXT("Typed authored records import"), Roundtrip->Import(Records, Error));
    TestEqual(TEXT("Float zero round-trips"), Roundtrip->C4ShaderParam1, 0.f);
    TestTrue(TEXT("Override bit round-trips"), Roundtrip->Overrides.Contains(TEXT("C4ShaderParam1")));
    auto Omitted = MakeShared<FJsonObject>();
    auto Local = MakeShared<FJsonObject>();
    auto Value = MakeShared<FJsonObject>();
    Local->SetStringField(TEXT("flag"), TEXT("bOverride_C4ShaderParam1"));
    Local->SetBoolField(TEXT("enabled"), true);
    Local->SetBoolField(TEXT("value_present"), false);
    Local->SetField(TEXT("value_property"), MakeShared<FJsonValueNull>());
    Value->SetStringField(TEXT("type"), TEXT("omitted-cooked-value"));
    Local->SetObjectField(TEXT("value"), Value);
    Omitted->SetObjectField(TEXT("C4ShaderParam1"), Local);
    TestTrue(TEXT("An omitted native value imports"), Roundtrip->Import(Omitted, Error));
    TestEqual(TEXT("An enabled omitted value uses the native default, rather than the parent"),
        Roundtrip->C4ShaderParam1, 1.f);
    Local->SetBoolField(TEXT("enabled"), false);
    TestTrue(TEXT("An unchecked omitted value imports"), Roundtrip->Import(Omitted, Error));
    TestEqual(TEXT("An unchecked omitted value inherits"), Roundtrip->C4ShaderParam1, 0.25f);
    Settings->Overrides.Remove(TEXT("C4ShaderParam1"));
    Settings->Resolve(Effective);
    TestEqual(TEXT("Unchecked child restores inheritance"), Effective->C4ShaderParam1, 0.25f);
    Effective->C4ShaderParam1 = 0.12345678f;
    TArray<TPair<FString, FString>> Defines;
    Effective->Defines(Defines);
    for (const auto& Define : Defines)
    {
        if (Define.Key == TEXT("C4ShaderParam1"))
        {
            const float Parsed = FCString::Atof(*Define.Value);
            TestTrue(TEXT("A shader define preserves all float32 bits"),
                FMemory::Memcmp(&Parsed, &Effective->C4ShaderParam1, sizeof(float)) == 0);
        }
    }
    UMaterial* FirstCopy = DuplicateObject<UMaterial>(Master, GetTransientPackage(),
        FName(*(TEXT("C4IdentityA_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UMaterial* SecondCopy = DuplicateObject<UMaterial>(Master, GetTransientPackage(),
        FName(*(TEXT("C4IdentityB_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    P4ApplyMaterialInstanceC4ForCompile(Child, FirstCopy);
    P4ApplyMaterialInstanceC4ForCompile(Child, SecondCopy);
    TestEqual(
        TEXT("Unchanged C4 settings reuse the same private shader identity"), FirstCopy->StateId, SecondCopy->StateId);
    TestFalse(TEXT("Material mesh type is master-only"), P4IsC4InstanceProperty(TEXT("C4MaterialMeshType")));
    TestFalse(TEXT("Capsule-shadow scale is master-only"), P4IsC4InstanceProperty(TEXT("C4ShaderParam0")));
    TestTrue(TEXT("The material-instance family is separate"),
        P4Kadan::GameAssetFamily(Child) == P4Kadan::EGameAssetFamily::MaterialInstance);
    auto* Proxy = NewObject<UMaterialEditorInstanceConstant>();
    Proxy->SourceInstance = Child;
    auto& Editor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    auto LayerRows = Editor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
    LayerRows->SetObjects({Proxy});
    bool LayerHasC4Category = false;
    for (const auto& Node : LayerRows->GetRootTreeNodes())
    {
        LayerHasC4Category |= Node->GetNodeName() == TEXT("C4");
    }
    TestFalse(TEXT("The stock layer generator receives no added category"), LayerHasC4Category);
    auto Rows = Editor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
    Rows->SetObjects({Child});
    TSharedPtr<IDetailTreeNode> Category;
    for (const auto& Node : Rows->GetRootTreeNodes())
    {
        if (Node->GetNodeName() == TEXT("C4"))
        {
            Category = Node;
        }
    }
    if (TestTrue(TEXT("Instance property rows have C4 Details"), Category.IsValid()))
    {
        TArray<TSharedRef<IDetailTreeNode>> Fields;
        Category->GetChildren(Fields);
        TSet<FName> Shown;
        for (const auto& Row : Fields)
        {
            const auto Handle = Row->CreatePropertyHandle();
            const auto* Property = Handle.IsValid() ? Handle->GetProperty() : nullptr;
            AddInfo(TEXT("C4 Details row: ") + Row->GetNodeName().ToString() + TEXT(" / ") +
                (Property ? Property->GetName() : TEXT("object header")));
            if (Property)
            {
                Shown.Add(Property->GetFName());
            }
        }
        TestEqual(TEXT("Details exposes exactly the fourteen native instance fields"), Shown.Num(), 14);
        for (FName Name : Shown)
        {
            TestTrue(TEXT("The displayed C4 property is a native instance field: ") + Name.ToString(),
                P4IsC4InstanceProperty(Name));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InstanceC4Compile, "P4MapKit.Kadan.MaterialInstanceC4Compile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InstanceC4Compile::RunTest(const FString&)
{
    const FString Root = TEXT("/Game/Mods/c4compile_1025");
    FString Error;
    const FString Graph = TEXT(
        R"JSON({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Translucent","two_sided":false,
        "shading_model":"MSM_DefaultLit","clip":0.3333,"tangent_space_normal":true,
        "nodes":{"Constant_0":{"type":"Constant","inputs":{},"props":{"R":0.5}}},
        "outputs":{"BaseColor":["Constant_0",""],"Opacity":["Constant_0",""]}})JSON");
    UMaterial* Master = P4BuildMaterialFromGraph(Graph, Root / TEXT("M_C4"), Error);
    if (!TestNotNull(TEXT("Native translucent master"), Master))
    {
        AddError(Error);
        return false;
    }
    TArray<FString> Missing;
    auto* Instance = P4BuildMaterialInstance(
        TEXT("{\"parent\":\"") + Master->GetPathName() + TEXT("\"}"), Root / TEXT("MI_C4"), Missing, Error);
    if (!TestNotNull(TEXT("Native creator instance"), Instance))
    {
        AddError(Error);
        return false;
    }
    auto* Settings = UP4MaterialInstanceGameSettings::FindOrAdd(Instance);
    Settings->Overrides.Add(TEXT("C4ShaderParam1"));
    Settings->Edited.Add(TEXT("C4ShaderParam1"));
    Settings->C4ShaderParam1 = 0.f;
    FP4CreatorMaterialCompileRequest Request;
    Request.CreatorMaterial = Master;
    Request.CreatorInstance = Instance;
    Request.RuntimeObjectPath = Root / TEXT("Runtime.Runtime");
    const FString Before = P4MaterialInstanceJson(Instance, Error);
    const FGuid MasterState = Master->StateId;
    const auto Zero = P4CompileCreatorMaterial(Request);
    if (!TestTrue(TEXT("C4 zero compiles"), Zero.Succeeded))
    {
        AddError(Zero.Error);
        return false;
    }
    TestEqual(TEXT("Compilation preserves the authored override"), P4MaterialInstanceJson(Instance, Error), Before);
    Settings->C4ShaderParam1 = 1.f;
    const auto One = P4CompileCreatorMaterial(Request);
    if (!TestTrue(TEXT("C4 one compiles"), One.Succeeded))
    {
        AddError(One.Error);
        return false;
    }
    TestTrue(TEXT("Changing the instance C4 strength changes actual compiled programs"), Zero.Programs != One.Programs);
    TestEqual(TEXT("The authored master state is preserved"), Master->StateId, MasterState);
    return true;
}

// Explicit disposable fixture. Production policy remains locked until the game ladder qualifies it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4EditGameMaterialInstance, "P4MapKit.Kadan.EditGameMaterialInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4EditGameMaterialInstance::RunTest(const FString&)
{
    const auto Env = [](const TCHAR* Key) { return FPlatformMisc::GetEnvironmentVariable(Key); };
    if (Env(TEXT("P4_MI_EDIT_PROOF")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in disposable original-instance fixture: set P4_MI_EDIT_PROOF=1."));
        return true;
    }
    const FString Mode = Env(TEXT("P4_MI_EDIT_MODE")), Package = Env(TEXT("P4_MI_EDIT_PACKAGE"));
    if (Mode != TEXT("none") && Mode != TEXT("value") && Mode != TEXT("texture") && Mode != TEXT("switch") &&
        Mode != TEXT("base") && Mode != TEXT("c4") && Mode != TEXT("all"))
    {
        AddError(TEXT("Unknown material-instance control mode."));
        return false;
    }
    if (FPackageName::DoesPackageExist(Package))
    {
        AddError(TEXT("Each rung requires a freshly imported target in the owned disposable project."));
        return false;
    }
    const FString Imported = UP4AuthoringTools::NativeKadanImportMapFromContainers(Env(TEXT("P4_MI_EDIT_GAMEFILES")),
        Package, Env(TEXT("P4_MI_EDIT_PAKS")), Env(TEXT("P4_MI_EDIT_RETOC")), Env(TEXT("P4_MI_EDIT_REPAK")));
    FFileHelper::SaveStringToFile(Imported, *(Env(TEXT("P4_MI_EDIT_REPORT")) + TEXT(".import.json")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    auto* Instance = LoadObject<UMaterialInstanceConstant>(
        nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
    if (!TestNotNull(TEXT("Normal imported instance"), Instance) ||
        !TestTrue(TEXT("Recovered native original"), P4Kadan::IsRecoveredGameMaterialInstance(Instance)) ||
        !TestTrue(TEXT("Disposable build has enabled only this family"), P4Kadan::GetAssetPolicy(Instance).AllowEdit))
    {
        return false;
    }
    P4Kadan::UpdateReadOnlyAsset(Instance);
    FString Error;
    const FString Before = P4MaterialInstanceJson(Instance, Error);
    if (!TestTrue(TEXT("Imported recipe reads"), Error.IsEmpty()))
    {
        return false;
    }
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("before"), Before);
    const auto EffectiveC4 = [Instance]()
    {
        auto* Effective = NewObject<UP4MaterialGameSettings>();
        P4ResolveMaterialC4(Instance, Effective);
        TArray<TPair<FString, FString>> Defines;
        Effective->Defines(Defines);
        auto Json = MakeShared<FJsonObject>();
        for (const auto& Define : Defines)
        {
            Json->SetStringField(Define.Key, Define.Value);
        }
        return Json;
    };
    Report->SetObjectField(TEXT("effective_c4_before"), EffectiveC4());
    Instance->Modify();
    P4RecordGameMaterialInstanceEdit(Instance);
    if (Mode == TEXT("value") || Mode == TEXT("all"))
    {
        Instance->GetAllVectorParameterInfo(Infos, Ids);
        TArray<FString> VectorNames;
        for (const auto& P : Infos)
        {
            VectorNames.Add(P.Name.ToString());
        }
        AddInfo(TEXT("Available vector parameters: ") + FString::Join(VectorNames, TEXT(", ")));
        const auto* Tint = Infos.FindByPredicate(
            [](const auto& P)
            {
                return !P.Name.ToString().StartsWith(TEXT("C4")) &&
                    P.Name.ToString().Contains(TEXT("Color"), ESearchCase::IgnoreCase);
            });
        if (Tint)
        {
            Instance->SetVectorParameterValueEditorOnly(*Tint, FLinearColor(0.03f, 1.f, 0.03f, 1.f));
            Report->SetStringField(TEXT("value_parameter"), Tint->Name.ToString());
        }
        else
        {
            Infos.Reset();
            Ids.Reset();
            Instance->GetAllScalarParameterInfo(Infos, Ids);
            TArray<FString> ScalarNames;
            for (const auto& P : Infos)
            {
                ScalarNames.Add(P.Name.ToString());
            }
            AddInfo(TEXT("Available scalar parameters: ") + FString::Join(ScalarNames, TEXT(", ")));
            const FMaterialParameterInfo* Scalar = nullptr;
            for (FName Candidate :
                {FName(TEXT("BaseColor_Scale")), FName(TEXT("BaseColor_Intensity")), FName(TEXT("Diffuse_Intensity")),
                    FName(TEXT("SubsurfaceColor_Multiply")), FName(TEXT("Roughness"))})
            {
                Scalar = Infos.FindByPredicate([Candidate](const auto& P) { return P.Name == Candidate; });
                if (Scalar)
                {
                    break;
                }
            }
            if (!Scalar)
            {
                if (Mode == TEXT("value"))
                {
                    AddError(TEXT("No selected visible value parameter; do not invent an unused proof value."));
                    return false;
                }
                Report->SetStringField(
                    TEXT("value_not_applicable"), TEXT("The recovered graph has no selected value input."));
            }
            else
            {
                Instance->SetScalarParameterValueEditorOnly(*Scalar, 0.f);
                Report->SetStringField(TEXT("value_parameter"), Scalar->Name.ToString());
            }
        }
    }
    if (Mode == TEXT("texture") || Mode == TEXT("all"))
    {
        auto* Texture = LoadObject<UTexture>(nullptr, TEXT("/Game/Carrot4/MasterMaterial/Texture/T_Base_D.T_Base_D"));
        if (!TestNotNull(TEXT("Normal imported replacement texture"), Texture))
        {
            return false;
        }
        Infos.Reset();
        Ids.Reset();
        Instance->GetAllTextureParameterInfo(Infos, Ids);
        const auto* Diffuse = Infos.FindByPredicate([](const auto& P) { return P.Name == TEXT("Diffuse_01"); });
        if (!TestNotNull(TEXT("Active diffuse parameter"), Diffuse))
        {
            return false;
        }
        Instance->SetTextureParameterValueEditorOnly(*Diffuse, Texture);
    }
    FStaticParameterSet Static = Instance->GetStaticParameters();
    auto Base = Instance->BasePropertyOverrides;
    if (Mode == TEXT("switch") || Mode == TEXT("all"))
    {
        const FString RequestedSwitch = Env(TEXT("P4_MI_EDIT_SWITCH"));
        if (!RequestedSwitch.IsEmpty())
        {
            Infos.Reset();
            Ids.Reset();
            Instance->GetAllStaticSwitchParameterInfo(Infos, Ids);
            const auto* Info = Infos.FindByPredicate(
                [&RequestedSwitch](const auto& P) { return P.Name.ToString() == RequestedSwitch; });
            bool Value = false;
            FGuid Id;
            if (!Info || !Instance->GetStaticSwitchParameterValue(*Info, Value, Id))
            {
                AddError(TEXT("The requested switch is absent from the recovered graph."));
                return false;
            }
            auto* Local = Static.StaticSwitchParameters.FindByPredicate(
                [Info](const auto& P) { return P.ParameterInfo == *Info; });
            if (!Local)
            {
                FStaticSwitchParameter Parameter;
                Parameter.ParameterInfo = *Info;
                Parameter.Value = Value;
                Parameter.ExpressionGUID = Id;
                Static.StaticSwitchParameters.Add(Parameter);
            }
            else
            {
                Local->Value = Value;
                Local->ExpressionGUID = Id;
            }
        }
        auto* Switch = Static.StaticSwitchParameters.FindByPredicate(
            [&RequestedSwitch](const auto& P)
            {
                if (!RequestedSwitch.IsEmpty())
                {
                    return P.ParameterInfo.Name.ToString() == RequestedSwitch;
                }
                return P.ParameterInfo.Name == TEXT("Use_MaskMap") || P.ParameterInfo.Name == TEXT("Use_SubsurfaceMap");
            });
        if (!Switch)
        {
            if (Mode == TEXT("switch"))
            {
                AddError(TEXT("No selected active switch."));
                return false;
            }
            Report->SetStringField(
                TEXT("switch_not_applicable"), TEXT("The recovered graph has no selected switch input."));
        }
        else
        {
            Switch->bOverride = true;
            Switch->Value = !Switch->Value;
            Report->SetStringField(TEXT("switch_parameter"), Switch->ParameterInfo.Name.ToString());
            Report->SetBoolField(TEXT("switch_value"), Switch->Value);
        }
    }
    if (Mode == TEXT("base") || Mode == TEXT("all"))
    {
        Base.bOverride_ShadingModel = true;
        Base.ShadingModel = MSM_Unlit;
    }
    if (Mode == TEXT("c4") || Mode == TEXT("all"))
    {
        auto* Settings = UP4MaterialInstanceGameSettings::FindOrAdd(Instance);
        Settings->SetOverride(TEXT("C4ShaderParam1"), true);
        Settings->Modify();
        const FString RequestedStrength = Env(TEXT("P4_MI_EDIT_C4_STRENGTH"));
        Settings->C4ShaderParam1 = RequestedStrength.IsEmpty() ? 0.f : FCString::Atof(*RequestedStrength);
        FPropertyChangedEvent Event(
            FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), TEXT("C4ShaderParam1")));
        Settings->PostEditChangeProperty(Event);
        if (Mode == TEXT("c4"))
        {
            const auto Diagnostic = P4DiagnoseOriginalAsset(Instance);
            if (!TestTrue(TEXT("C4-only edits are protected by the original comparison"),
                    Diagnostic.Comparison == EP4OriginalComparison::Different))
            {
                AddError(Diagnostic.Error);
                return false;
            }
        }
    }
    if (Mode == TEXT("switch") || Mode == TEXT("base") || Mode == TEXT("all"))
    {
        Instance->UpdateStaticPermutation(Static, Base);
    }
    UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
    Instance->MarkPackageDirty();
    if (!TestTrue(TEXT("Whole original instance saves"),
            UEditorLoadingAndSavingUtils::SavePackages({Instance->GetOutermost()}, false)))
    {
        return false;
    }
    const FString AuthoredAfter = P4MaterialInstanceJson(Instance, Error);
    if (Mode == TEXT("none"))
    {
        TestEqual(TEXT("The unedited control preserves every native local record"), AuthoredAfter, Before);
    }
    if (Mode == TEXT("c4"))
    {
        const auto WithoutC4 = [](const FString& Recipe)
        {
            TSharedPtr<FJsonObject> Json;
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Recipe), Json);
            Json->RemoveField(TEXT("game_overrides"));
            FString Text;
            FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
            return Text;
        };
        TestEqual(TEXT("A C4-only control preserves every other native local record"), WithoutC4(AuthoredAfter),
            WithoutC4(Before));
    }
    const bool AuthoredPermutation = Instance->bHasStaticPermutationResource;
    Report->SetStringField(TEXT("after"), AuthoredAfter);
    Report->SetObjectField(TEXT("effective_c4_after"), EffectiveC4());
    const FString Id = TEXT("miedit.proof");
    UP4AuthoringTools::NativeSaveMod(Id, TEXT("Material instance proof"), TEXT("1.0.0"), true);
    const FString Export = UP4AuthoringTools::NativeExportMod(Id, Package, FString(), Env(TEXT("P4_MI_EDIT_PAKS")),
        Env(TEXT("P4_MI_EDIT_OUT")), Env(TEXT("P4_MI_EDIT_WORK")), Env(TEXT("P4_MI_EDIT_RETOC")),
        Env(TEXT("P4_MI_EDIT_REPAK")), Env(TEXT("P4_MI_EDIT_GAMEFILES")));
    TSharedPtr<FJsonObject> ExportJson;
    bool Published = false;
    const bool Read = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Export), ExportJson) &&
        ExportJson.IsValid();
    const bool Preserved = P4MaterialInstanceJson(Instance, Error) == AuthoredAfter &&
        Instance->bHasStaticPermutationResource == AuthoredPermutation;
    Report->SetBoolField(TEXT("authored_recipe_and_permutation_preserved"), Preserved);
    TestTrue(TEXT("Export restores the authored recipe and permutation flag"), Preserved);
    if (Read)
    {
        Report->SetObjectField(TEXT("export"), ExportJson);
    }
    else
    {
        Report->SetStringField(TEXT("export_error"), Export);
    }
    Report->SetStringField(TEXT("mode"), Mode);
    Report->SetStringField(TEXT("package"), Package);
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(
        Text, *Env(TEXT("P4_MI_EDIT_REPORT")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return TestTrue(TEXT("Normal panel exporter published"),
        Read && ExportJson->TryGetBoolField(TEXT("published"), Published) && Published);
}
#endif

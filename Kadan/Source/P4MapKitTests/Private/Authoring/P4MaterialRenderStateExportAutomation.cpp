#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"
#include "P4CreatorMaterialFactories.h"
#include "P4MaterialGameSettings.h"
#include "P4TranslucentMaterialStaging.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"

// A translucent creator material with the game's render-state settings on (reverse depth test, translucent depth
// write and both separate translucency passes). The shipped runtime material holds each as the game's UMaterial field
// of that name, a true bool tag, as the game's cooked masters hold them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialRenderStateExportTest, "P4MapKit.Export.MaterialRenderStates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialRenderStateExportTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    if (!P4UseSwitchShaderRoute())
    {
        AddWarning(TEXT("P4MapKit test not run. Creator materials are exported through UE's shaders only."));
        return true;
    }
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("test.renderstates"), TEXT("Render states"), RequestData, Names))
        return false;
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    UMaterial* Graph = Cast<UMaterial>(
        Tools.CreateAsset(TEXT("M_States"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get()));
    if (!TestNotNull(TEXT("Kadan's material action made the graph"), Graph))
        return false;
    ON_SCOPE_EXIT
    {
        UPackage* Package = Graph->GetOutermost();
        FAssetRegistryModule::AssetDeleted(Graph);
        Graph->ClearFlags(RF_Public | RF_Standalone);
        Graph->MarkPendingKill();
        Package->SetDirtyFlag(false);
        Package->MarkPendingKill();
    };
    Graph->BlendMode = BLEND_Translucent;
    UMaterialExpressionScalarParameter* Alpha = NewObject<UMaterialExpressionScalarParameter>(Graph);
    Alpha->ParameterName = TEXT("Alpha");
    Alpha->DefaultValue = 0.5f;
    UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph);
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor(0.f, 0.5f, 1.f, 1.f);
    Graph->Expressions = {Alpha, Tint};
    Graph->BaseColor.Expression = Tint;
    Graph->Opacity.Expression = Alpha;
    UP4MaterialGameSettings* Settings = UP4MaterialGameSettings::FindOrAdd(Graph);
    TArray<FString> Fields;
    for (TFieldIterator<FBoolProperty> It(UP4MaterialGameSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper);
        It; ++It)
        if (It->HasMetaData(TEXT("C4MaterialField")))
        {
            It->SetPropertyValue_InContainer(Settings, true);
            Fields.Add(It->GetName());
        }
    Fields.Sort();
    TestEqual(TEXT("The four render-state settings are the game's material fields"), Fields,
        TArray<FString>({TEXT("bC4EnableSeparateTranslucencyFull"), TEXT("bC4EnableSeparateTranslucencyFullSecond"),
            TEXT("bC4ReverseDepthTest"), TEXT("bC4XluDepthWrite")}));
    Settings->Publish();
    Graph->PostEditChange();
    FP4ExportRoot Root;
    Root.Path = Graph->GetOutermost()->GetName();
    RequestData.Roots.Add(Root);
    FString Error;
    FP4ExportModResult Exported;
    if (!TestTrue(
            TEXT("The material with the game's render states exports"), P4ExportMod(RequestData, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Runtime = FP4TranslucentMaterialStaging::RuntimePackage(Names.PackageRoot, Root.Path);
    if (!TestTrue(TEXT("The runtime material ships"), Exported.Packages.Contains(Runtime)))
        return false;
    const FString Folder = Exported.Directory / TEXT("romfs/tsuchi/mods") / RequestData.Mod.Id;
    const FString Readback = RequestData.WorkDirectory / TEXT("render-state-readback");
    if (!TestTrue(TEXT("The container is extracted for readback"),
            P4ExtractExportReadback(RequestData.Tools, Folder / (Names.Container + TEXT("_P.utoc")),
                RequestData.GamePaksDirectory, RequestData.WorkDirectory, Readback, TEXT(""), Error)))
    {
        AddError(Error);
        return false;
    }
    const FString HeaderPath = Readback / TEXT("Carrot4/Content") / (Runtime.Mid(6) + TEXT(".uasset"));
    TArray<uint8> Header;
    TArray<uint8> Body;
    if (!TestTrue(TEXT("The runtime material is read back"),
            FFileHelper::LoadFileToArray(Header, *HeaderPath) &&
                FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(HeaderPath, TEXT("uexp")))))
        return false;
    const std::vector<uint8> HeaderBytes(Header.GetData(), Header.GetData() + Header.Num());
    const std::vector<uint8> BodyBytes(Body.GetData(), Body.GetData() + Body.Num());
    const auto Package = P4MapKit::ReadPackage(HeaderBytes, BodyBytes);
    if (!TestTrue(TEXT("The runtime material parses"), Package.Error.Code.empty()))
        return false;
    TArray<FString> Shipped;
    const std::string Leaf = TCHAR_TO_UTF8(*FPackageName::GetShortName(Runtime));
    for (const auto& Export : Package.Exports)
    {
        if (Export.Name != Leaf)
            continue;
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            BodyBytes, Package.Names, Export.Offset, Export.Offset + Export.Size);
        TestTrue(TEXT("The material's tags parse"), Tags.Error.Code.empty());
        for (const auto& Tag : Tags.Properties)
            if (Tag.Type == "BoolProperty" && Tag.BoolValue && Fields.Contains(UTF8_TO_TCHAR(Tag.Name.c_str())))
                Shipped.Add(UTF8_TO_TCHAR(Tag.Name.c_str()));
    }
    Shipped.Sort();
    TestEqual(TEXT("The shipped material holds each render state once, set"), Shipped, Fields);
    return !HasAnyErrors();
}
#endif

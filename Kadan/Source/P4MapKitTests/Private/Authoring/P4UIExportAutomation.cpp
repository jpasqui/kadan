#if P4_MAPKIT_TESTS
#include "P4ExportDriver.h"
#include "P4ModDescriptor.h"
#include "P4CreatorMaterialFactories.h"
#include "P4MaterialInstance.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4TranslucentMaterialStaging.h"
#include "AssetImportTask.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// A creator User Interface material compiled through UE's Slate material shaders for the game.
// The exported child draws a green checker; the editor/export result alone is not in-game evidence.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4UIExportTest, "P4MapKit.Export.UICreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4UIExportTest::RunTest(const FString&)
{
    if (!P4UseSwitchShaderRoute())
    {
        AddWarning(TEXT(
            "P4MapKit test not run. UI materials of your own are exported through UE's shaders only (KADAN_SWITCH_ROUTE)."));
        return true;
    }
    if (!GEditor)
    {
        AddError(TEXT("The UI export requires the native editor."));
        return false;
    }
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    if (NativeRoot.IsEmpty() || GamePaks.IsEmpty())
    {
        AddError(TEXT("The native source and game container fixture are required."));
        return false;
    }
    FP4ExportModRequest Request;
    Request.Mod.Id = TEXT("native.ui.test");
    Request.Mod.Name = TEXT("Native UI Test");
    Request.Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Request.Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    const FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KadanUIExport"));
    Request.ProjectDirectory = Work / TEXT("project");
    Request.OutputDirectory = Work / TEXT("out");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit/KadanUIExport/work");
    Request.GamePaksDirectory = GamePaks;
    Request.ExtractedGameDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_TRANSPARENT_GAMEFILES"));
    if (Request.ExtractedGameDirectory.IsEmpty())
        Request.ExtractedGameDirectory = NativeRoot / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    const FString Repo = FPaths::GetPath(FPaths::GetPath(NativeRoot));
    Request.Tools.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Request.Tools.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Request.Tools.Retoc.IsEmpty())
        Request.Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Request.Tools.Repak.IsEmpty())
        Request.Tools.Repak = Repo / TEXT("private/repak/repak.exe");
    if (!IFileManager::Get().FileExists(
            *(Request.ProjectDirectory / TEXT("Config/KadanMods") / (Request.Mod.Id + TEXT(".json")))) &&
        !P4WriteModDescriptor(Request.ProjectDirectory, Request.Mod, nullptr, Error))
    {
        AddError(Error);
        return false;
    }
    for (const TCHAR* Name : {TEXT("M_Own"), TEXT("MI_Own"), TEXT("T_Own")})
        if (FindPackage(nullptr, *(Names.PackageRoot / Name)) ||
            FPackageName::DoesPackageExist(Names.PackageRoot / Name))
        {
            AddError(TEXT("A UI fixture already occupies the selected package; it will not be overwritten."));
            return false;
        }
    TArray<UObject*> Made;
    ON_SCOPE_EXIT
    {
        for (UObject* Asset : Made)
        {
            if (!IsValid(Asset))
                continue;
            UPackage* Package = Asset->GetOutermost();
            FAssetRegistryModule::AssetDeleted(Asset);
            Asset->ClearFlags(RF_Public | RF_Standalone);
            Asset->Rename(*MakeUniqueObjectName(
                              GetTransientPackage(), Asset->GetClass(), FName(*(Asset->GetName() + TEXT("_Released"))))
                              .ToString(),
                GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
            Asset->MarkPendingKill();
            if (IsValid(Package) && Package != GetTransientPackage())
            {
                Package->SetDirtyFlag(false);
                Package->Rename(*(TEXT("/Temp/KadanUIFixture/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
                Package->MarkPendingKill();
            }
        }
    };
    IFileManager::Get().MakeDirectory(*Work, true);
    const FString Image = Work / TEXT("creator.tga");
    const TArray<uint8> Pixels = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40, 255, 255, 255, 255, 0, 0, 0,
        255, 0, 0, 0, 255, 255, 255, 255, 255};
    if (!FFileHelper::SaveArrayToFile(Pixels, *Image))
    {
        AddError(TEXT("The creator texture could not be written."));
        return false;
    }
    TStrongObjectPtr<UTextureFactory> TextureFactory(NewObject<UTextureFactory>());
    TextureFactory->AssetImportTask = NewObject<UAssetImportTask>(TextureFactory.Get());
    TextureFactory->AssetImportTask->bAutomated = true;
    bool Cancelled = false;
    UTexture2D* Texture = Cast<UTexture2D>(
        TextureFactory->ImportObject(UTexture2D::StaticClass(), CreatePackage(*(Names.PackageRoot / TEXT("T_Own"))),
            TEXT("T_Own"), RF_Public | RF_Standalone | RF_Transactional, Image, nullptr, Cancelled));
    TextureFactory->CleanUp();
    if (!TestTrue(TEXT("Creator UI checker texture imported"), Texture && !Cancelled))
        return false;
    Texture->Filter = TF_Nearest;
    Texture->PostEditChange();
    FAssetRegistryModule::AssetCreated(Texture);
    Made.Add(Texture);
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    UMaterial* Graph = Cast<UMaterial>(
        Tools.CreateAsset(TEXT("M_Own"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get()));
    if (!TestNotNull(TEXT("Kadan's material action made the UI graph"), Graph))
        return false;
    Made.Add(Graph);
    Graph->MaterialDomain = MD_UI;
    Graph->BlendMode = BLEND_Translucent;
    Graph->SetShadingModel(MSM_Unlit);
    UMaterialExpressionTextureSampleParameter2D* Checker = NewObject<UMaterialExpressionTextureSampleParameter2D>(
        Graph);
    Checker->ParameterName = TEXT("Checker");
    Checker->Texture = Texture;
    UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph);
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor::White;
    UMaterialExpressionMultiply* Color = NewObject<UMaterialExpressionMultiply>(Graph);
    Color->A.Expression = Checker;
    Color->B.Expression = Tint;
    UMaterialExpressionScalarParameter* Alpha = NewObject<UMaterialExpressionScalarParameter>(Graph);
    Alpha->ParameterName = TEXT("Alpha");
    Alpha->DefaultValue = 1.f;
    Graph->Expressions = {Checker, Tint, Color, Alpha};
    Graph->EmissiveColor.Expression = Color;
    Graph->Opacity.Expression = Alpha;
    Graph->PostEditChange();
    TArray<FString> Unknown;
    const FString InstanceJson = TEXT("{\"parent\":\"") + Graph->GetPathName() +
        TEXT("\",\"vectors\":{\"Tint\":[0.125,1,0.0625,1]}}");
    UMaterialInstanceConstant* Instance = P4BuildMaterialInstance(
        InstanceJson, Names.PackageRoot / TEXT("MI_Own"), Unknown, Error);
    if (!TestNotNull(TEXT("The UI instance is built"), Instance))
    {
        AddError(Error);
        return false;
    }
    Made.Add(Instance);
    FAssetRegistryModule::AssetCreated(Instance);
    FP4CreatorMaterialCompileRequest Compile;
    Compile.CreatorMaterial = Graph;
    const FP4CreatorMaterialCompileResult Compiled = P4CompileCreatorMaterial(Compile);
    if (!TestTrue(TEXT("The UI graph compiles for the game through UE's shaders"), Compiled.Succeeded))
    {
        AddError(Compiled.Error);
        return false;
    }
    if (!TestTrue(
            TEXT("The UI map has programs and a frozen block"), !Compiled.Programs.empty() && !Compiled.Block.empty()))
        return false;
    AddInfo(FString::Printf(TEXT("Kadan's UI map: %d versions, %d programs, block %d bytes"), Compiled.Versions,
        int32(Compiled.Programs.size()), int32(Compiled.Block.size())));
    FP4ExportRoot Root;
    Root.Path = Names.PackageRoot / TEXT("MI_Own");
    Request.Roots.Add(Root);
    FP4ExportModResult Result;
    if (!TestTrue(TEXT("The UI material is compiled, cooked and exported"), P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString RuntimePackage = FP4TranslucentMaterialStaging::RuntimePackage(
        Names.PackageRoot, Graph->GetOutermost()->GetName());
    TestTrue(TEXT("The runtime UI material is shipped"), Result.Packages.Contains(RuntimePackage));
    TestTrue(TEXT("The UI instance is shipped"), Result.Packages.Contains(Names.PackageRoot / TEXT("MI_Own")));
    TestTrue(TEXT("The creator UI texture is shipped"), Result.Packages.Contains(Names.PackageRoot / TEXT("T_Own")));
    TestFalse(TEXT("The editable graph is not shipped"), Result.Packages.Contains(Graph->GetOutermost()->GetName()));
    TestTrue(TEXT("The instance's editor parent is restored"), Instance->Parent == Graph);
    TestTrue(TEXT("The editable graph keeps its UI domain"), Graph->MaterialDomain == MD_UI);
    return !HasAnyErrors();
}
#endif

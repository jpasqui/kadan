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
#include "MaterialSceneTextureId.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// A creator post process material (Post Process domain, after tonemapping) exported through UE's shaders compiled for the
// game. The mod ships M_Own's runtime copy and MI_Own (Tint green, which tints the picture);
// private/own-export-eden/make_renderers.py adds MI_Own to a PostProcessComponent's blendables in Eden.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PostProcessExportTest, "P4MapKit.Export.PostProcessCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PostProcessExportTest::RunTest(const FString&)
{
    if (!P4UseSwitchShaderRoute())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Post process materials of your own are exported through UE's shaders only (KADAN_SWITCH_ROUTE)."));
        return true;
    }
    if (!GEditor)
    {
        AddError(TEXT("The post process export requires the native editor."));
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
    Request.Mod.Id = TEXT("native.postprocess.test");
    Request.Mod.Name = TEXT("Native Post Process Test");
    Request.Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Request.Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    const FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KadanPostProcessExport"));
    Request.ProjectDirectory = Work / TEXT("project");
    Request.OutputDirectory = Work / TEXT("out");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit/KadanPostProcessExport/work");
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
            AddError(TEXT("A post process fixture already occupies the selected package; it will not be overwritten."));
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
                Package->Rename(
                    *(TEXT("/Temp/KadanPostProcessFixture/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
                Package->MarkPendingKill();
            }
        }
    };
    IFileManager::Get().MakeDirectory(*Work, true);
    // A 2x2 texture, as the other fixtures make (the graph tints the picture and does not read it).
    const FString Image = Work / TEXT("creator.tga");
    const TArray<uint8> Pixels = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40, 255, 255, 255, 255, 255, 255,
        255, 128, 255, 255, 255, 64, 255, 255, 255, 0};
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
    if (!TestTrue(TEXT("Creator texture imported"), Texture && !Cancelled))
        return false;
    FAssetRegistryModule::AssetCreated(Texture);
    Made.Add(Texture);
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    UMaterial* Graph = Cast<UMaterial>(
        Tools.CreateAsset(TEXT("M_Own"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get()));
    if (!TestNotNull(TEXT("Kadan's material action made the post process graph"), Graph))
        return false;
    Made.Add(Graph);
    // The picture after tonemapping times Tint.
    Graph->MaterialDomain = MD_PostProcess;
    Graph->BlendableLocation = BL_AfterTonemapping;
    Graph->SetShadingModel(MSM_Unlit);
    // UMaterialExpressionSceneTexture is not exported from the engine: made by its class, its texture set by its property.
    UClass* SceneClass = FindObject<UClass>(ANY_PACKAGE, TEXT("MaterialExpressionSceneTexture"));
    if (!TestNotNull(TEXT("The scene texture expression class is found"), SceneClass))
        return false;
    UMaterialExpression* Scene = NewObject<UMaterialExpression>(Graph, SceneClass);
    FByteProperty* SceneTextureId = FindFProperty<FByteProperty>(SceneClass, TEXT("SceneTextureId"));
    if (!TestNotNull(TEXT("The scene texture expression has its texture id"), SceneTextureId))
        return false;
    SceneTextureId->SetPropertyValue_InContainer(Scene, uint8(PPI_PostProcessInput0));
    UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph);
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor(1.f, 1.f, 1.f, 1.f);
    UMaterialExpressionMultiply* Tinted = NewObject<UMaterialExpressionMultiply>(Graph);
    Tinted->A.Expression = Scene;
    Tinted->B.Expression = Tint;
    Graph->Expressions = {Scene, Tint, Tinted};
    Graph->EmissiveColor.Expression = Tinted;
    Graph->PostEditChange();
    TArray<FString> Unknown;
    const FString InstanceJson = TEXT("{\"parent\":\"") + Graph->GetPathName() +
        TEXT("\",\"vectors\":{\"Tint\":[0.4,1,0.4,1]}}");
    UMaterialInstanceConstant* Instance = P4BuildMaterialInstance(
        InstanceJson, Names.PackageRoot / TEXT("MI_Own"), Unknown, Error);
    if (!TestNotNull(TEXT("The post process instance is built"), Instance))
    {
        AddError(Error);
        return false;
    }
    Made.Add(Instance);
    FAssetRegistryModule::AssetCreated(Instance);
    // The map through UE's shaders: the post process material's pixel shader.
    FP4CreatorMaterialCompileRequest Compile;
    Compile.CreatorMaterial = Graph;
    const FP4CreatorMaterialCompileResult Compiled = P4CompileCreatorMaterial(Compile);
    if (!TestTrue(TEXT("The post process graph compiles for the game through UE's shaders"), Compiled.Succeeded))
    {
        AddError(Compiled.Error);
        return false;
    }
    AddInfo(FString::Printf(TEXT("Kadan's post process map: %d versions, %d programs, block %d bytes"),
        Compiled.Versions, int32(Compiled.Programs.size()), int32(Compiled.Block.size())));
    FP4ExportRoot Root;
    Root.Path = Names.PackageRoot / TEXT("MI_Own");
    Request.Roots.Add(Root);
    FP4ExportModResult Result;
    if (!TestTrue(
            TEXT("The post process material is compiled, cooked and exported"), P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString RuntimePackage = FP4TranslucentMaterialStaging::RuntimePackage(
        Names.PackageRoot, Graph->GetOutermost()->GetName());
    TestTrue(TEXT("The runtime post process material is shipped"), Result.Packages.Contains(RuntimePackage));
    TestTrue(
        TEXT("The post process instance is shipped"), Result.Packages.Contains(Names.PackageRoot / TEXT("MI_Own")));
    TestFalse(TEXT("The editable graph is not shipped"), Result.Packages.Contains(Graph->GetOutermost()->GetName()));
    TestTrue(TEXT("The instance's editor parent is restored"), Instance->Parent == Graph);
    return !HasAnyErrors();
}
#endif

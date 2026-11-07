#if P4_MAPKIT_TESTS
#include "P4ExportDriver.h"
#include "P4ModDescriptor.h"
#include "P4CreatorMaterialFactories.h"
#include "P4MaterialInstance.h"
#include "P4MaterialGameSettings.h"
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

// Creator decal materials (Deferred Decal domain) exported through UE's shaders compiled for the game, whose maps hold
// the game's decal classes (FDeferredDecalPS00..11 and FDeferredDecalEmissivePS00..11, KadanGameShaderTypes.cpp). Each
// mod ships M_Own's runtime copy and MI_Own (Tint green); private/own-export-eden/make_decal.py puts MI_Own on a
// DecalComponent in Eden. The water wet edge decal is the game's own mode (C4DecalBlendMode), which the shipped
// material holds as the game's DecalBlendMode value.
enum class EDecalExport
{
    Translucent,
    Emissive,
    WaterBox
};

static bool RunDecalExport(FAutomationTestBase& Test, EDecalExport Kind)
{
    const bool bEmissive = Kind == EDecalExport::Emissive, bWaterBox = Kind == EDecalExport::WaterBox;
    if (!P4UseSwitchShaderRoute())
    {
        Test.AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Decal materials of your own are exported through UE's shaders only (KADAN_SWITCH_ROUTE)."));
        return true;
    }
    if (!GEditor)
    {
        Test.AddError(TEXT("The decal export requires the native editor."));
        return false;
    }
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    if (NativeRoot.IsEmpty() || GamePaks.IsEmpty())
    {
        Test.AddError(TEXT("The native source and game container fixture are required."));
        return false;
    }
    FP4ExportModRequest Request;
    Request.Mod.Id = bEmissive ? TEXT("native.decal.emissive.test")
        : bWaterBox            ? TEXT("native.decal.waterbox.test")
                               : TEXT("native.decal.test");
    Request.Mod.Name = bEmissive ? TEXT("Native Emissive Decal Test")
        : bWaterBox              ? TEXT("Native Water Wet Edge Decal Test")
                                 : TEXT("Native Decal Test");
    Request.Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Request.Mod, Names, Error))
    {
        Test.AddError(Error);
        return false;
    }
    const FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
        (bEmissive          ? TEXT("KadanEmissiveDecalExport")
                : bWaterBox ? TEXT("KadanWaterBoxDecalExport")
                            : TEXT("KadanDecalExport")));
    Request.ProjectDirectory = Work / TEXT("project");
    Request.OutputDirectory = Work / TEXT("out");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit") / FPaths::GetCleanFilename(Work) /
        TEXT("work");
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
        Test.AddError(Error);
        return false;
    }
    for (const TCHAR* Name : {TEXT("M_Own"), TEXT("MI_Own"), TEXT("T_Own")})
        if (FindPackage(nullptr, *(Names.PackageRoot / Name)) ||
            FPackageName::DoesPackageExist(Names.PackageRoot / Name))
        {
            Test.AddError(TEXT("A decal fixture already occupies the selected package; it will not be overwritten."));
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
                Package->Rename(*(TEXT("/Temp/KadanDecalFixture/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
                Package->MarkPendingKill();
            }
        }
    };
    IFileManager::Get().MakeDirectory(*Work, true);
    // A 2x2 mask: the decal's opacity is its alpha (1, 1/2, 1/4, 0).
    const FString Image = Work / TEXT("creator.tga");
    const TArray<uint8> Pixels = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40, 255, 255, 255, 255, 255, 255,
        255, 128, 255, 255, 255, 64, 255, 255, 255, 0};
    if (!FFileHelper::SaveArrayToFile(Pixels, *Image))
    {
        Test.AddError(TEXT("The creator texture could not be written."));
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
    if (!Test.TestTrue(TEXT("Creator texture imported"), Texture && !Cancelled))
        return false;
    FAssetRegistryModule::AssetCreated(Texture);
    Made.Add(Texture);
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    UMaterial* Graph = Cast<UMaterial>(
        Tools.CreateAsset(TEXT("M_Own"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get()));
    if (!Test.TestNotNull(TEXT("Kadan's material action made the decal graph"), Graph))
        return false;
    Made.Add(Graph);
    // A translucent decal (base colour, opacity), which the game's renderer draws as a DBuffer decal; or an emissive
    // one (emissive colour, opacity), drawn into the scene colour after the base pass.
    Graph->MaterialDomain = MD_DeferredDecal;
    Graph->DecalBlendMode = bEmissive ? DBM_Emissive : DBM_Translucent;
    Graph->BlendMode = BLEND_Translucent;
    UMaterialExpressionScalarParameter* Alpha = NewObject<UMaterialExpressionScalarParameter>(Graph);
    Alpha->ParameterName = TEXT("Alpha");
    Alpha->DefaultValue = 1.f;
    UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph);
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor(1.f, 0.125f, 0.0625f, 1.f);
    UMaterialExpressionTextureSampleParameter2D* Mask = NewObject<UMaterialExpressionTextureSampleParameter2D>(Graph);
    Mask->ParameterName = TEXT("Mask");
    Mask->Texture = Texture;
    UMaterialExpressionMultiply* Opacity = NewObject<UMaterialExpressionMultiply>(Graph);
    Opacity->A.Expression = Alpha;
    Opacity->B.Connect(4, Mask);
    Graph->Expressions = {Alpha, Tint, Mask, Opacity};
    if (bEmissive)
        Graph->EmissiveColor.Expression = Tint;
    else
        Graph->BaseColor.Expression = Tint;
    // The emissive decal's opacity is the Alpha parameter alone (no texture), so what it draws is plain to see.
    if (bEmissive)
        Graph->Opacity.Expression = Alpha;
    else
        Graph->Opacity.Expression = Opacity;
    // The water wet edge: stock's translucent decal in the editor, the game's mode in its C4 settings. Base Color red is
    // the wetness, green and blue go to the mask's blue and red (KadanSwitchSourcePatches.inl).
    if (bWaterBox)
    {
        UP4MaterialGameSettings* Settings = UP4MaterialGameSettings::FindOrAdd(Graph);
        Settings->C4DecalBlendMode = C4DBM_DBuffer_C4_WaterBox;
        Settings->Publish();
    }
    Graph->PostEditChange();
    TArray<FString> Unknown;
    const FString InstanceJson = TEXT("{\"parent\":\"") + Graph->GetPathName() + TEXT("\",\"vectors\":{\"Tint\":") +
        (bEmissive ? TEXT("[0.5,4,0.25,1]") : TEXT("[0.125,1,0.0625,1]")) + TEXT("}}");
    UMaterialInstanceConstant* Instance = P4BuildMaterialInstance(
        InstanceJson, Names.PackageRoot / TEXT("MI_Own"), Unknown, Error);
    if (!Test.TestNotNull(TEXT("The decal's instance is built"), Instance))
    {
        Test.AddError(Error);
        return false;
    }
    Made.Add(Instance);
    FAssetRegistryModule::AssetCreated(Instance);
    // The map through UE's shaders: the game's decal classes, no surface classes for the decal pass.
    FP4CreatorMaterialCompileRequest Compile;
    Compile.CreatorMaterial = Graph;
    const FP4CreatorMaterialCompileResult Compiled = P4CompileCreatorMaterial(Compile);
    if (!Test.TestTrue(TEXT("The decal graph compiles for the game through UE's shaders"), Compiled.Succeeded))
    {
        Test.AddError(Compiled.Error);
        return false;
    }
    Test.AddInfo(FString::Printf(TEXT("Kadan's decal map: %d versions, %d programs, block %d bytes"), Compiled.Versions,
        int32(Compiled.Programs.size()), int32(Compiled.Block.size())));
    FP4ExportRoot Root;
    Root.Path = Names.PackageRoot / TEXT("MI_Own");
    Request.Roots.Add(Root);
    FP4ExportModResult Result;
    if (!Test.TestTrue(
            TEXT("The decal material is compiled, cooked and exported"), P4ExportMod(Request, Result, Error)))
    {
        Test.AddError(Error);
        return false;
    }
    const FString RuntimePackage = FP4TranslucentMaterialStaging::RuntimePackage(
        Names.PackageRoot, Graph->GetOutermost()->GetName());
    Test.TestTrue(TEXT("The runtime decal material is shipped"), Result.Packages.Contains(RuntimePackage));
    Test.TestTrue(
        TEXT("The decal's instance is shipped"), Result.Packages.Contains(Names.PackageRoot / TEXT("MI_Own")));
    Test.TestFalse(
        TEXT("The editable graph is not shipped"), Result.Packages.Contains(Graph->GetOutermost()->GetName()));
    Test.TestTrue(TEXT("The instance's editor parent is restored"), Instance->Parent == Graph);
    if (bWaterBox)
    {
        // The shipped container holds the game's decal mode by name (the cook wrote it in the stand-in's place).
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Request.OutputDirectory, TEXT("*"), true, false);
        bool bNamed = false;
        for (const FString& File : Files)
        {
            TArray<uint8> Bytes;
            if (!File.EndsWith(TEXT(".ucas")) && !File.EndsWith(TEXT(".pak")))
                continue;
            if (!FFileHelper::LoadFileToArray(Bytes, *File))
                continue;
            const ANSICHAR* Name = "DBM_DBuffer_C4_WaterBox";
            const int32 Length = FCStringAnsi::Strlen(Name);
            for (int32 At = 0; !bNamed && At + Length <= Bytes.Num(); ++At)
                bNamed = FMemory::Memcmp(Bytes.GetData() + At, Name, Length) == 0;
        }
        Test.TestTrue(
            TEXT("The shipped decal material holds the game's DecalBlendMode DBM_DBuffer_C4_WaterBox"), bNamed);
    }
    return !Test.HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4DecalExportTest, "P4MapKit.Export.DecalCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4DecalExportTest::RunTest(const FString&)
{
    return RunDecalExport(*this, EDecalExport::Translucent);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4EmissiveDecalExportTest, "P4MapKit.Export.EmissiveDecalCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4EmissiveDecalExportTest::RunTest(const FString&)
{
    return RunDecalExport(*this, EDecalExport::Emissive);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WaterBoxDecalExportTest, "P4MapKit.Export.WaterBoxDecalCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WaterBoxDecalExportTest::RunTest(const FString&)
{
    return RunDecalExport(*this, EDecalExport::WaterBox);
}
#endif

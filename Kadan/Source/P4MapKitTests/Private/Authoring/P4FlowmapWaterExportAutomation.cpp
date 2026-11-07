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
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// An ordinary creator graph: linear RG flowmap -> signed UV velocity, two half-cycle samples and a continuous
// triangular crossfade. Time remains UE's real advancing GameTime input. This tests export, not in-game visibility.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4FlowmapWaterExportTest, "P4MapKit.Export.FlowmapWaterCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4FlowmapWaterExportTest::RunTest(const FString&)
{
    if (!P4UseSwitchShaderRoute())
    {
        AddWarning(
            TEXT("P4MapKit test not run. Flowmap water requires UE's Switch shader route (KADAN_SWITCH_ROUTE)."));
        return true;
    }
    if (!GEditor)
    {
        AddError(TEXT("The flowmap water export requires the native editor."));
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
    Request.Mod.Id = TEXT("native.flowmap.water.test");
    Request.Mod.Name = TEXT("Native Flowmap Water Test");
    Request.Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Request.Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    const FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KadanFlowmapWaterExport"));
    Request.ProjectDirectory = Work / TEXT("project");
    Request.OutputDirectory = Work / TEXT("out");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit/KadanFlowmapWaterExport/work");
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
    for (const TCHAR* Name : {TEXT("M_Own"), TEXT("MI_Own"), TEXT("T_Flow"), TEXT("T_Pattern")})
        if (FindPackage(nullptr, *(Names.PackageRoot / Name)) ||
            FPackageName::DoesPackageExist(Names.PackageRoot / Name))
        {
            AddError(
                TEXT("A flowmap water fixture already occupies the selected package; it will not be overwritten."));
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
                    *(TEXT("/Temp/KadanFlowmapWaterFixture/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
                Package->MarkPendingKill();
            }
        }
    };
    IFileManager::Get().MakeDirectory(*Work, true);
    auto MakeTexture = [&](const TCHAR* Name, bool IsFlow) -> UTexture2D*
    {
        // Tiny, repeatable source images: a spatially varying direction field and a high-contrast ripple pattern.
        TArray<uint8> Pixels = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 16, 0, 32, 40};
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 X = 0; X < 16; ++X)
            {
                const uint8 V = uint8(48 + ((X * 3 + Y * 5) % 11) * 18);
                Pixels.Add(IsFlow ? 128 : V); // BGRA; blue is unused by the flowmap.
                Pixels.Add(IsFlow ? uint8(96 + Y * 8) : V);
                Pixels.Add(IsFlow ? uint8(160 + X * 4) : V);
                Pixels.Add(255);
            }
        const FString Image = Work / (FString(Name) + TEXT(".tga"));
        if (!FFileHelper::SaveArrayToFile(Pixels, *Image))
        {
            AddError(TEXT("The flowmap water texture could not be written."));
            return nullptr;
        }
        TStrongObjectPtr<UTextureFactory> Importer(NewObject<UTextureFactory>());
        Importer->AssetImportTask = NewObject<UAssetImportTask>(Importer.Get());
        Importer->AssetImportTask->bAutomated = true;
        bool Cancelled = false;
        UTexture2D* Texture = Cast<UTexture2D>(
            Importer->ImportObject(UTexture2D::StaticClass(), CreatePackage(*(Names.PackageRoot / Name)), FName(Name),
                RF_Public | RF_Standalone | RF_Transactional, Image, nullptr, Cancelled));
        Importer->CleanUp();
        if (!Texture || Cancelled)
        {
            AddError(TEXT("A flowmap water texture could not be imported."));
            return nullptr;
        }
        Texture->SRGB = !IsFlow;
        if (IsFlow)
            Texture->CompressionSettings = TC_VectorDisplacementmap; // Preserve linear RG direction data.
        Texture->AddressX = TA_Wrap;
        Texture->AddressY = TA_Wrap;
        Texture->PostEditChange();
        Made.Add(Texture);
        FAssetRegistryModule::AssetCreated(Texture);
        return Texture;
    };
    UTexture2D* FlowTexture = MakeTexture(TEXT("T_Flow"), true);
    UTexture2D* PatternTexture = MakeTexture(TEXT("T_Pattern"), false);
    if (!FlowTexture || !PatternTexture)
        return false;
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    UMaterial* Graph = Cast<UMaterial>(
        Tools.CreateAsset(TEXT("M_Own"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get()));
    if (!TestNotNull(TEXT("Kadan's material action made the flowmap graph"), Graph))
        return false;
    Made.Add(Graph);
    Graph->MaterialDomain = MD_Surface;
    Graph->BlendMode = BLEND_Translucent;
    Graph->SetShadingModel(MSM_DefaultLit);
    Graph->TranslucencyLightingMode = TLM_Surface;
    Graph->bEnableSeparateTranslucency = false;
    Graph->bUseTranslucencyVertexFog = false;
    auto Scalar = [&](const TCHAR* Name, float Value)
    {
        auto* Node = NewObject<UMaterialExpressionScalarParameter>(Graph);
        Node->ParameterName = Name;
        Node->DefaultValue = Value;
        Graph->Expressions.Add(Node);
        return Node;
    };
    auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Graph);
    auto* Flow = NewObject<UMaterialExpressionTextureSampleParameter2D>(Graph);
    Flow->ParameterName = TEXT("Flowmap");
    Flow->Texture = FlowTexture;
    Flow->SamplerType = SAMPLERTYPE_LinearColor;
    Flow->Coordinates.Expression = UV;
    auto* RG = NewObject<UMaterialExpressionComponentMask>(Graph);
    RG->Input.Expression = Flow;
    RG->R = true;
    RG->G = true;
    RG->B = false;
    RG->A = false;
    auto* Centered = NewObject<UMaterialExpressionSubtract>(Graph);
    Centered->A.Expression = RG;
    Centered->ConstB = 0.5f;
    auto* Signed = NewObject<UMaterialExpressionMultiply>(Graph);
    Signed->A.Expression = Centered;
    Signed->ConstB = 2.f;
    auto* Velocity = NewObject<UMaterialExpressionMultiply>(Graph);
    Velocity->A.Expression = Signed;
    Velocity->B.Expression = Scalar(TEXT("FlowStrength"), 0.65f);
    auto* TiledUV = NewObject<UMaterialExpressionMultiply>(Graph);
    TiledUV->A.Expression = UV;
    TiledUV->B.Expression = Scalar(TEXT("Tiling"), 5.f);
    auto* Time = NewObject<UMaterialExpressionTime>(Graph);
    Time->bIgnorePause = false;
    Time->bOverride_Period = false;
    auto* Clock = NewObject<UMaterialExpressionMultiply>(Graph);
    Clock->A.Expression = Time;
    Clock->B.Expression = Scalar(TEXT("Speed"), 0.137f);
    auto* Phase0 = NewObject<UMaterialExpressionFrac>(Graph);
    Phase0->Input.Expression = Clock;
    auto* HalfCycle = NewObject<UMaterialExpressionAdd>(Graph);
    HalfCycle->A.Expression = Clock;
    HalfCycle->ConstB = 0.5f;
    auto* Phase1 = NewObject<UMaterialExpressionFrac>(Graph);
    Phase1->Input.Expression = HalfCycle;
    auto Sample = [&](UMaterialExpression* Phase)
    {
        auto* Offset = NewObject<UMaterialExpressionMultiply>(Graph);
        Offset->A.Expression = Velocity;
        Offset->B.Expression = Phase;
        auto* Displaced = NewObject<UMaterialExpressionSubtract>(Graph);
        Displaced->A.Expression = TiledUV;
        Displaced->B.Expression = Offset;
        auto* Pattern = NewObject<UMaterialExpressionTextureSampleParameter2D>(Graph);
        Pattern->ParameterName = TEXT("Pattern");
        Pattern->Texture = PatternTexture;
        Pattern->SamplerType = SAMPLERTYPE_Color;
        Pattern->Coordinates.Expression = Displaced;
        Graph->Expressions.Append({Offset, Displaced, Pattern});
        return Pattern;
    };
    auto* Pattern0 = Sample(Phase0);
    auto* Pattern1 = Sample(Phase1);
    auto* TwicePhase = NewObject<UMaterialExpressionMultiply>(Graph);
    TwicePhase->A.Expression = Phase0;
    TwicePhase->ConstB = 2.f;
    auto* Ramp = NewObject<UMaterialExpressionSubtract>(Graph);
    Ramp->A.Expression = TwicePhase;
    Ramp->ConstB = 1.f;
    auto* Weight = NewObject<UMaterialExpressionAbs>(Graph);
    Weight->Input.Expression = Ramp;
    auto* Blend = NewObject<UMaterialExpressionLinearInterpolate>(Graph);
    Blend->A.Expression = Pattern0;
    Blend->B.Expression = Pattern1;
    Blend->Alpha.Expression = Weight;
    auto* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph);
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor(0.025f, 0.5f, 0.85f, 1.f);
    auto* Color = NewObject<UMaterialExpressionMultiply>(Graph);
    Color->A.Expression = Blend;
    Color->B.Expression = Tint;
    auto* Glow = NewObject<UMaterialExpressionMultiply>(Graph);
    Glow->A.Expression = Color;
    Glow->ConstB = 0.12f;
    Graph->Expressions.Append({UV, Flow, RG, Centered, Signed, Velocity, TiledUV, Time, Clock, Phase0, HalfCycle,
        Phase1, TwicePhase, Ramp, Weight, Blend, Tint, Color, Glow});
    Graph->BaseColor.Expression = Color;
    Graph->EmissiveColor.Expression = Glow;
    Graph->Opacity.Expression = Scalar(TEXT("Alpha"), 0.78f);
    Graph->Roughness.Expression = Scalar(TEXT("Roughness"), 0.18f);
    Graph->Specular.Expression = Scalar(TEXT("Specular"), 0.5f);
    Graph->PostEditChange();
    TArray<FString> Unknown;
    const FString InstanceJson = TEXT("{\"parent\":\"") + Graph->GetPathName() +
        TEXT(
            "\",\"vectors\":{\"Tint\":[0.025,0.65,0.85,1]},\"scalars\":{\"Speed\":0.137},\"textures\":{\"Flowmap\":\"") +
        FlowTexture->GetPathName() + TEXT("\",\"Pattern\":\"") + PatternTexture->GetPathName() + TEXT("\"}}");
    UMaterialInstanceConstant* Instance = P4BuildMaterialInstance(
        InstanceJson, Names.PackageRoot / TEXT("MI_Own"), Unknown, Error);
    if (!TestNotNull(TEXT("The flowmap water instance is built"), Instance))
    {
        AddError(Error);
        return false;
    }
    Made.Add(Instance);
    FAssetRegistryModule::AssetCreated(Instance);
    TestTrue(TEXT("The instance recognizes all flowmap water parameters"), Unknown.Num() == 0);
    UTexture* ReadFlow = nullptr;
    TestTrue(TEXT("The flowmap texture is bound on the instance"),
        Instance->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Flowmap")), ReadFlow) &&
            ReadFlow == FlowTexture);
    FP4CreatorMaterialCompileRequest Compile;
    Compile.CreatorMaterial = Graph;
    const FP4CreatorMaterialCompileResult Compiled = P4CompileCreatorMaterial(Compile);
    if (!TestTrue(TEXT("The animated flowmap graph compiles through UE's Switch shaders"), Compiled.Succeeded))
    {
        AddError(Compiled.Error);
        return false;
    }
    if (!TestTrue(TEXT("The flowmap map has programs and a frozen block"),
            !Compiled.Programs.empty() && !Compiled.Block.empty()))
        return false;
    AddInfo(FString::Printf(
        TEXT("Kadan's flowmap water map: %d versions, %d programs, block %d bytes; Speed=0.137 cycles/s"),
        Compiled.Versions, int32(Compiled.Programs.size()), int32(Compiled.Block.size())));
    FP4ExportRoot Root;
    Root.Path = Names.PackageRoot / TEXT("MI_Own");
    Request.Roots.Add(Root);
    FP4ExportModResult Result;
    if (!TestTrue(
            TEXT("The flowmap water graph is compiled, cooked and exported"), P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString RuntimePackage = FP4TranslucentMaterialStaging::RuntimePackage(
        Names.PackageRoot, Graph->GetOutermost()->GetName());
    TestTrue(TEXT("The runtime water material is shipped"), Result.Packages.Contains(RuntimePackage));
    for (const TCHAR* Name : {TEXT("MI_Own"), TEXT("T_Flow"), TEXT("T_Pattern")})
        TestTrue(FString::Printf(TEXT("The water dependency %s is shipped"), Name),
            Result.Packages.Contains(Names.PackageRoot / Name));
    TestFalse(
        TEXT("The editable water graph is not shipped"), Result.Packages.Contains(Graph->GetOutermost()->GetName()));
    TestTrue(TEXT("The instance's editor parent is restored"), Instance->Parent == Graph);
    TestTrue(TEXT("The editor graph remains lit translucent"),
        Graph->BlendMode == BLEND_Translucent && Graph->GetShadingModels().HasShadingModel(MSM_DefaultLit));
    return !HasAnyErrors();
}
#endif

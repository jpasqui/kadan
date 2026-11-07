#if P4_MAPKIT_TESTS
// A creator material made from scratch, exported by the panel's exporter for a game run.
// Two textures (one panned), a Fresnel rim, opaque Default Lit, on a 100 cm cube, plus an
// instance of the material with other values. The output folder is kept for the game run.
#include "P4ExportDriver.h"
#include "P4AuthoringRows.h"
#include "P4CreatorSource.h"
#include "P4CreatorMaterialFactories.h"
#include "P4MaterialInstance.h"
#include "P4ModDescriptor.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4MaterialGraph.h"
#include "P4KadanImport.h"
#include "P4KadanSourceAcquire.h"
#include "AssetImportTask.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TArray<uint8> Tga(int32 Size, TFunctionRef<FColor(int32, int32)> Pixel)
{
    TArray<uint8> Out = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8(Size & 255), uint8(Size >> 8), uint8(Size & 255),
        uint8(Size >> 8), 32, 40};
    for (int32 Y = 0; Y < Size; ++Y)
        for (int32 X = 0; X < Size; ++X)
        {
            const FColor C = Pixel(X, Y);
            Out.Append({C.B, C.G, C.R, C.A});
        }
    return Out;
}

FString CubeObj()
{
    // Six faces, each with its own normal and a full 0..1 UV square.
    const float S = 50.f;
    const FVector Normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    FString Text = TEXT("o CreatorCube\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n");
    int32 Vertex = 1;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FVector N = Normals[Face];
        const FVector U = FMath::Abs(N.Z) > 0.5f ? FVector(1, 0, 0) : FVector::CrossProduct(FVector(0, 0, 1), N);
        const FVector V = FVector::CrossProduct(N, U);
        const FVector Corners[4] = {
            N * S - U * S - V * S, N * S + U * S - V * S, N * S + U * S + V * S, N * S - U * S + V * S};
        for (const FVector& C : Corners)
            Text += FString::Printf(TEXT("v %g %g %g\n"), C.X, C.Y, C.Z);
        Text += FString::Printf(TEXT("vn %g %g %g\n"), N.X, N.Y, N.Z);
        Text += FString::Printf(TEXT("f %d/1/%d %d/2/%d %d/3/%d\nf %d/1/%d %d/3/%d %d/4/%d\n"), Vertex, Face + 1,
            Vertex + 1, Face + 1, Vertex + 2, Face + 1, Vertex, Face + 1, Vertex + 2, Face + 1, Vertex + 3, Face + 1);
        Vertex += 4;
    }
    return Text;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorMaterialGameExportTest, "P4MapKit.Export.CreatorMaterialForGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorMaterialGameExportTest::RunTest(const FString&)
{
    if (!GEditor)
    {
        AddError(TEXT("The editor is required."));
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
    Request.Mod.Id = TEXT("kadan.creator.material");
    Request.Mod.Name = TEXT("Kadan Creator Material");
    Request.Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Request.Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    const FString Work = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("KadanCreatorMaterialGame"));
    Request.ProjectDirectory = Work / TEXT("project");
    Request.OutputDirectory = Work / TEXT("out");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit/KadanCreatorMaterialGame/work");
    Request.GamePaksDirectory = GamePaks;
    Request.ExtractedGameDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_TRANSPARENT_GAMEFILES"));
    if (Request.ExtractedGameDirectory.IsEmpty())
        Request.ExtractedGameDirectory = NativeRoot / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    Request.ShaderArchiveFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_TRANSPARENT_SHADER_ARCHIVE"));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(NativeRoot));
    Request.Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    Request.Tools.Repak = Repo / TEXT("private/repak/repak.exe");
    if (!IFileManager::Get().FileExists(
            *(Request.ProjectDirectory / TEXT("Config/KadanMods") / (Request.Mod.Id + TEXT(".json")))) &&
        !P4WriteModDescriptor(Request.ProjectDirectory, Request.Mod, nullptr, Error))
    {
        AddError(Error);
        return false;
    }
    for (const TCHAR* Leaf : {TEXT("M_Lead"), TEXT("MI_Lead"), TEXT("M_Match"), TEXT("M_GlassUnlit"),
             TEXT("M_GlassLit"), TEXT("SM_Cube"), TEXT("T_Pattern"), TEXT("T_Detail")})
        if (FindPackage(nullptr, *(Names.PackageRoot / Leaf)) ||
            FPackageName::DoesPackageExist(Names.PackageRoot / Leaf))
        {
            AddError(TEXT("A fixture package already exists and will not be overwritten."));
            return false;
        }
    IFileManager::Get().MakeDirectory(*Work, true);
    // Pattern: an orange and cream checker. Detail: soft vertical bands.
    const TArray<uint8> Pattern = Tga(64, [](int32 X, int32 Y)
        { return ((X / 8 + Y / 8) & 1) ? FColor(250, 140, 40, 255) : FColor(250, 240, 210, 255); });
    const TArray<uint8> Detail = Tga(64,
        [](int32 X, int32)
        {
            const uint8 V = uint8(150 + 105 * (0.5f + 0.5f * FMath::Sin(X * 2.f * PI / 16.f)));
            return FColor(V, V, V, 255);
        });
    const FString PatternFile = Work / TEXT("pattern.tga"), DetailFile = Work / TEXT("detail.tga"),
                  MeshFile = Work / TEXT("cube.obj");
    if (!FFileHelper::SaveArrayToFile(Pattern, *PatternFile) || !FFileHelper::SaveArrayToFile(Detail, *DetailFile) ||
        !FFileHelper::SaveStringToFile(CubeObj(), *MeshFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        AddError(TEXT("The creator inputs could not be written."));
        return false;
    }
    auto Import = [&](const FString& File, const FString& Leaf, bool Mesh) -> UObject*
    {
        TStrongObjectPtr<UFactory> Factory(Mesh ? static_cast<UFactory*>(NewObject<UFbxFactory>())
                                                : static_cast<UFactory*>(NewObject<UTextureFactory>()));
        Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
        Factory->AssetImportTask->bAutomated = true;
        if (UFbxFactory* Fbx = Cast<UFbxFactory>(Factory.Get()))
        {
            Fbx->SetDetectImportTypeOnImport(false);
            Fbx->ImportUI->MeshTypeToImport = FBXIT_StaticMesh;
            Fbx->ImportUI->bAutomatedImportShouldDetectType = false;
            Fbx->ImportUI->bImportMaterials = false;
            Fbx->ImportUI->bImportTextures = false;
        }
        bool Cancelled = false;
        UObject* Asset = Factory->ImportObject(Mesh ? UStaticMesh::StaticClass() : UTexture2D::StaticClass(),
            CreatePackage(*(Names.PackageRoot / Leaf)), FName(*Leaf), RF_Public | RF_Standalone | RF_Transactional,
            File, nullptr, Cancelled);
        Factory->CleanUp();
        if (Asset && !Cancelled)
        {
            FAssetRegistryModule::AssetCreated(Asset);
            return Asset;
        }
        return nullptr;
    };
    TStrongObjectPtr<UTexture2D> PatternTexture(Cast<UTexture2D>(Import(PatternFile, TEXT("T_Pattern"), false)));
    TStrongObjectPtr<UTexture2D> DetailTexture(Cast<UTexture2D>(Import(DetailFile, TEXT("T_Detail"), false)));
    TStrongObjectPtr<UStaticMesh> Cube(Cast<UStaticMesh>(Import(MeshFile, TEXT("SM_Cube"), true)));
    if (!TestNotNull(TEXT("Pattern texture imported"), PatternTexture.Get()) ||
        !TestNotNull(TEXT("Detail texture imported"), DetailTexture.Get()) ||
        !TestNotNull(TEXT("Cube imported"), Cube.Get()))
        return false;
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Graph(
        Cast<UMaterial>(Tools.CreateAsset(TEXT("M_Lead"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get())));
    if (!TestNotNull(TEXT("Kadan's material action made the graph"), Graph.Get()))
        return false;
    Graph->BlendMode = BLEND_Opaque;
    Graph->SetShadingModel(MSM_DefaultLit);
    Graph->TwoSided = false;
    Graph->MaterialDecalResponse = MDR_ColorRoughness;
    UMaterialExpressionTextureCoordinate* Coordinate = NewObject<UMaterialExpressionTextureCoordinate>(Graph.Get());
    Coordinate->UTiling = 2.f;
    Coordinate->VTiling = 2.f;
    UMaterialExpressionPanner* Pan = NewObject<UMaterialExpressionPanner>(Graph.Get());
    Pan->SpeedX = 0.1f;
    Pan->SpeedY = 0.05f;
    Pan->Coordinate.Expression = Coordinate;
    UMaterialExpressionTextureSampleParameter2D* PatternSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(
        Graph.Get());
    PatternSample->ParameterName = TEXT("Pattern");
    PatternSample->Texture = PatternTexture.Get();
    PatternSample->Coordinates.Expression = Pan;
    UMaterialExpressionTextureSampleParameter2D* DetailSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(
        Graph.Get());
    DetailSample->ParameterName = TEXT("Detail");
    DetailSample->Texture = DetailTexture.Get();
    UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph.Get());
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor(1.f, 1.f, 1.f, 1.f);
    UMaterialExpressionMultiply* Mix = NewObject<UMaterialExpressionMultiply>(Graph.Get());
    Mix->A.Expression = PatternSample;
    Mix->B.Expression = DetailSample;
    UMaterialExpressionMultiply* Base = NewObject<UMaterialExpressionMultiply>(Graph.Get());
    Base->A.Expression = Mix;
    Base->B.Expression = Tint;
    UMaterialExpressionFresnel* Rim = NewObject<UMaterialExpressionFresnel>(Graph.Get());
    Rim->Exponent = 3.f;
    Rim->BaseReflectFraction = 0.04f;
    UMaterialExpressionVectorParameter* RimColor = NewObject<UMaterialExpressionVectorParameter>(Graph.Get());
    RimColor->ParameterName = TEXT("RimColor");
    RimColor->DefaultValue = FLinearColor(0.1f, 0.3f, 0.6f, 1.f);
    UMaterialExpressionMultiply* Glow = NewObject<UMaterialExpressionMultiply>(Graph.Get());
    Glow->A.Expression = Rim;
    Glow->B.Expression = RimColor;
    UMaterialExpressionScalarParameter* Roughness = NewObject<UMaterialExpressionScalarParameter>(Graph.Get());
    Roughness->ParameterName = TEXT("Roughness");
    Roughness->DefaultValue = 0.6f;
    Graph->Expressions = {
        Coordinate, Pan, PatternSample, DetailSample, Tint, Mix, Base, Rim, RimColor, Glow, Roughness};
    Graph->BaseColor.Expression = Base;
    Graph->EmissiveColor.Expression = Glow;
    Graph->Roughness.Expression = Roughness;
    Graph->PostEditChange();
    if (!TestTrue(TEXT("The new graph is the creator's own"), P4HasCreatorSource(Graph.Get(), &Error)))
    {
        AddError(Error);
        return false;
    }
    {
        FString GraphError;
        const FString GraphText = P4MaterialGraphJson(Graph.Get(), GraphError);
        FFileHelper::SaveStringToFile(
            GraphText, *(Work / TEXT("graph.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        const FString Issue = P4CreatorMaterialEditorGraphIssue(Graph.Get());
        if (!Issue.IsEmpty())
        {
            AddError(TEXT("Editor material check: ") + Issue);
            return false;
        }
    }
    Cube->SetMaterial(0, Graph.Get());
    // A second graph of the creator's that reads the donor game material's own diffuse texture by reference
    // (no game data ships), with base color and roughness taken from it as that game material does. Beside
    // the game material in the game, the two show whether the creator shaders light a surface as the game does.
    const FString Donor = TEXT(
        "/Game/Carrot4/Terrain/Area/Area001/Kakiwari/stg_Area001_BayWindow/MI_stg_Area001_BayWindow01");
    const FString WoodPath = TEXT(
        "/Game/Carrot4/Terrain/Area/Area001/Kakiwari/stg_Area001_BayWindow/T_stg_Area001_BayWindow01_D.T_stg_Area001_BayWindow01_D");
    UTexture2D* Wood = LoadObject<UTexture2D>(nullptr, *WoodPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Wood)
    {
        FP4KadanImportSettings Settings;
        Settings.GameFolder = Request.ExtractedGameDirectory;
        Settings.GamePaks = GamePaks;
        Settings.Retoc = Request.Tools.Retoc;
        Settings.Repak = Request.Tools.Repak;
        Settings.ShaderArchive = Request.ShaderArchiveFile;
        FP4KadanSourceInputs Inputs;
        if (!P4KadanAcquireSourceClosure(Settings, {Donor}, Inputs, Error))
        {
            AddError(Error);
            return false;
        }
        Settings.GameFolder = Inputs.Root;
        Settings.ShaderArchive = Inputs.ShaderArchive;
        (void)P4KadanImportMap(Settings, Donor, true);
        Wood = LoadObject<UTexture2D>(nullptr, *WoodPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    }
    if (!TestNotNull(TEXT("The game's diffuse texture is imported for reference"), Wood))
        return false;
    TStrongObjectPtr<UMaterial> Match(Cast<UMaterial>(
        Tools.CreateAsset(TEXT("M_Match"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get())));
    if (!TestNotNull(TEXT("Kadan's material action made the matching graph"), Match.Get()))
        return false;
    Match->BlendMode = BLEND_Opaque;
    Match->SetShadingModel(MSM_DefaultLit);
    Match->TwoSided = false;
    Match->MaterialDecalResponse = MDR_ColorRoughness;
    UMaterialExpressionTextureSampleParameter2D* WoodSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(
        Match.Get());
    WoodSample->ParameterName = TEXT("Wood");
    WoodSample->Texture = Wood;
    Match->Expressions = {WoodSample};
    Match->BaseColor.Expression = WoodSample;
    Match->Roughness.Expression = WoodSample;
    Match->Roughness.OutputIndex = 4;
    Match->Roughness.Mask = 1;
    Match->Roughness.MaskA = 1;
    Match->PostEditChange();
    {
        FString GraphError;
        const FString GraphText = P4MaterialGraphJson(Match.Get(), GraphError);
        FFileHelper::SaveStringToFile(
            GraphText, *(Work / TEXT("match.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        const FString Issue = P4CreatorMaterialEditorGraphIssue(Match.Get());
        if (!Issue.IsEmpty())
        {
            AddError(TEXT("Matching material check: ") + Issue);
            return false;
        }
    }
    // Two translucent graphs: tinted glass with the pattern texture and partial opacity, one unlit and one lit,
    // with the translucency settings each translucent shader route needs.
    auto Glass = [&](const TCHAR* Leaf, bool Lit) -> UMaterial*
    {
        UMaterial* Made = Cast<UMaterial>(
            Tools.CreateAsset(Leaf, Names.PackageRoot, UMaterial::StaticClass(), Factory.Get()));
        if (!Made)
            return nullptr;
        Made->BlendMode = BLEND_Translucent;
        Made->SetShadingModel(Lit ? MSM_DefaultLit : MSM_Unlit);
        Made->TwoSided = false;
        Made->TranslucencyLightingMode = TLM_Surface;
        Made->bEnableSeparateTranslucency = !Lit;
        Made->bUseTranslucencyVertexFog = !Lit;
        UMaterialExpressionTextureSampleParameter2D* Sample = NewObject<UMaterialExpressionTextureSampleParameter2D>(
            Made);
        Sample->ParameterName = TEXT("Pattern");
        Sample->Texture = PatternTexture.Get();
        UMaterialExpressionVectorParameter* GlassTint = NewObject<UMaterialExpressionVectorParameter>(Made);
        GlassTint->ParameterName = TEXT("Tint");
        GlassTint->DefaultValue = Lit ? FLinearColor(0.2f, 0.9f, 0.4f, 1.f) : FLinearColor(0.3f, 0.6f, 1.f, 1.f);
        UMaterialExpressionMultiply* Color = NewObject<UMaterialExpressionMultiply>(Made);
        Color->A.Expression = Sample;
        Color->B.Expression = GlassTint;
        UMaterialExpressionScalarParameter* Alpha = NewObject<UMaterialExpressionScalarParameter>(Made);
        Alpha->ParameterName = TEXT("Alpha");
        Alpha->DefaultValue = 0.45f;
        Made->Expressions = {Sample, GlassTint, Color, Alpha};
        Made->Opacity.Expression = Alpha;
        if (Lit)
        {
            UMaterialExpressionScalarParameter* GlassRough = NewObject<UMaterialExpressionScalarParameter>(Made);
            GlassRough->ParameterName = TEXT("Roughness");
            GlassRough->DefaultValue = 0.2f;
            UMaterialExpressionScalarParameter* Metal = NewObject<UMaterialExpressionScalarParameter>(Made);
            Metal->ParameterName = TEXT("Metallic");
            Metal->DefaultValue = 0.f;
            UMaterialExpressionScalarParameter* Spec = NewObject<UMaterialExpressionScalarParameter>(Made);
            Spec->ParameterName = TEXT("Specular");
            Spec->DefaultValue = 0.5f;
            UMaterialExpressionVectorParameter* Flat = NewObject<UMaterialExpressionVectorParameter>(Made);
            Flat->ParameterName = TEXT("Normal");
            Flat->DefaultValue = FLinearColor(0.f, 0.f, 1.f, 1.f);
            Made->Expressions.Append({GlassRough, Metal, Spec, Flat});
            Made->bTangentSpaceNormal = true;
            Made->BaseColor.Expression = Color;
            Made->Roughness.Expression = GlassRough;
            Made->Metallic.Expression = Metal;
            Made->Specular.Expression = Spec;
            Made->Normal.Expression = Flat;
        }
        else
            Made->EmissiveColor.Expression = Color;
        Made->PostEditChange();
        return Made;
    };
    TStrongObjectPtr<UMaterial> GlassUnlit(Glass(TEXT("M_GlassUnlit"), false)),
        GlassLit(Glass(TEXT("M_GlassLit"), true));
    for (UMaterial* Made : {GlassUnlit.Get(), GlassLit.Get()})
    {
        if (!TestNotNull(TEXT("Kadan's material action made the glass graph"), Made))
            return false;
        const FString Issue = P4CreatorMaterialEditorGraphIssue(Made);
        if (!Issue.IsEmpty())
        {
            AddError(Made->GetName() + TEXT(" check: ") + Issue);
            return false;
        }
    }
    TArray<FString> Unknown;
    const FString InstanceJson = TEXT("{\"parent\":\"") + Graph->GetPathName() +
        TEXT("\",\"scalars\":{\"Roughness\":0.3},\"vectors\":{\"Tint\":[0.35,0.75,1,1],\"RimColor\":[0.6,0.2,0.1,1]}}");
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(
        P4BuildMaterialInstance(InstanceJson, Names.PackageRoot / TEXT("MI_Lead"), Unknown, Error));
    if (!TestNotNull(TEXT("The creator's instance of the material is built"), Instance.Get()))
    {
        AddError(Error);
        return false;
    }
    FAssetRegistryModule::AssetCreated(Instance.Get());
    for (const TCHAR* Leaf :
        {TEXT("SM_Cube"), TEXT("MI_Lead"), TEXT("M_Match"), TEXT("M_GlassUnlit"), TEXT("M_GlassLit")})
    {
        FP4ExportRoot Root;
        Root.Path = Names.PackageRoot / Leaf;
        Request.Roots.Add(Root);
    }
    FP4ExportModResult Result;
    if (!TestTrue(
            TEXT("The panel's exporter compiles, cooks and publishes the mod"), P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        for (const FString& Why : Result.Refused)
            AddError(Why);
        return false;
    }
    for (const FString& Package : Result.Packages)
        AddInfo(TEXT("Shipped ") + Package);
    for (const FString& File : Result.Files)
        AddInfo(TEXT("Wrote ") + File);
    AddInfo(TEXT("Mod folder ") + Result.Directory);
    TestTrue(TEXT("The cube's mesh ships"), Result.Packages.Contains(Names.PackageRoot / TEXT("SM_Cube")));
    TestTrue(TEXT("The instance ships"), Result.Packages.Contains(Names.PackageRoot / TEXT("MI_Lead")));
    TestTrue(TEXT("The editor still has the creator's graph on the cube"),
        Cube->GetMaterial(0) == Graph.Get() && Instance->Parent == Graph.Get());
    return !HasAnyErrors();
}
#endif

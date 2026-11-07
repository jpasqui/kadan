#if P4_MAPKIT_TESTS
// The sweep's material row (P4_NATIVE_SWEEP_MATERIAL_ROW=1): one creator cube per everyday material a creator makes
// from scratch, so a game run shows each one drawn. Every material is a new UMaterial built from nodes, as in the
// material editor; the last two are instances of the row's own master.
#include "P4ExportDriverTestShared.h"
#include "AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace P4ExportDriverTest
{
// A 64x64 TGA through Unreal's texture factory, as a creator's import does.
UTexture2D* ImportTga(FAutomationTestBase& Test, const FString& Root, const FString& Name,
    TFunctionRef<FColor(int32 X, int32 Y)> Pixel, bool Normal)
{
    const FString Package = Root / Name;
    const FString File =
        FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/ExportDriverTest/source") / (Name + TEXT(".tga")));
    TArray<uint8> Tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 64, 0, 32, 40};
    for (int32 Y = 0; Y < 64; ++Y)
        for (int32 X = 0; X < 64; ++X)
        {
            const FColor C = Pixel(X, Y);
            Tga.Append({C.B, C.G, C.R, C.A});
        }
    if (!Test.TestTrue(TEXT("the material row's source image is written: ") + Name,
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) && FFileHelper::SaveArrayToFile(Tga, *File)))
        return nullptr;
    TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
    Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
    Factory->AssetImportTask->bAutomated = true;
    bool Cancelled = false;
    auto* Texture = Cast<UTexture2D>(Factory->ImportObject(UTexture2D::StaticClass(), CreatePackage(*Package), FName(*Name),
        RF_Public | RF_Standalone | RF_Transactional, File, nullptr, Cancelled));
    Factory->CleanUp();
    if (!Test.TestNotNull(TEXT("the material row's texture imports: ") + Name, Texture))
        return nullptr;
    FAssetRegistryModule::AssetCreated(Texture);
    if (Normal)
    {
        Texture->CompressionSettings = TC_Normalmap;
        Texture->SRGB = false;
        Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
        Texture->PostEditChange();
    }
    return Texture;
}

namespace
{
template <typename T> T* Node(UMaterial* Material)
{
    T* Made = NewObject<T>(Material);
    Material->Expressions.Add(Made);
    return Made;
}

UMaterialExpressionConstant3Vector* Color(UMaterial* Material, const FLinearColor& Value)
{
    auto* Made = Node<UMaterialExpressionConstant3Vector>(Material);
    Made->Constant = Value;
    return Made;
}

UMaterialExpressionConstant* Number(UMaterial* Material, float Value)
{
    auto* Made = Node<UMaterialExpressionConstant>(Material);
    Made->R = Value;
    return Made;
}

UMaterialExpressionTextureSample* Sample(UMaterial* Material, UTexture* Texture)
{
    auto* Made = Node<UMaterialExpressionTextureSample>(Material);
    Made->Texture = Texture;
    Made->AutoSetSampleType();
    return Made;
}
} // namespace

bool AddMaterialRow(FAutomationTestBase& Test, UWorld* World, const FString& Root, const FVector& At,
    TArray<UObject*>& Made, TArray<FString>& Names, TArray<AActor*>& Actors)
{
    // Yellow and blue cells; the blue cells are see-through for the masked material.
    UTexture2D* Checker = ImportTga(Test, Root, TEXT("T_RowChecker"),
        [](int32 X, int32 Y)
        { return ((X / 16 + Y / 16) % 2) ? FColor(40, 80, 230, 0) : FColor(250, 210, 40, 255); },
        false);
    // Round bumps: each cell's normal leans away from its centre.
    UTexture2D* Bumps = ImportTga(Test, Root, TEXT("T_RowBumps_N"),
        [](int32 X, int32 Y)
        {
            const float U = (X % 16 - 7.5f) / 7.5f, V = (Y % 16 - 7.5f) / 7.5f;
            const FVector N = FVector(U * 0.8f, V * 0.8f, 1.f).GetSafeNormal();
            return FColor(uint8((N.X * 0.5f + 0.5f) * 255), uint8((N.Y * 0.5f + 0.5f) * 255),
                uint8((N.Z * 0.5f + 0.5f) * 255), 255);
        },
        true);
    if (!Checker || !Bumps)
        return false;
    Made.Append({Checker, Bumps});
    TStrongObjectPtr<UStaticMesh> Cube(Cast<UStaticMesh>(ImportOwn(Test, Root, TEXT("SM_RowCube"), true)));
    if (!Test.TestTrue(TEXT("the material row's cube imports"), Cube.IsValid()))
        return false;
    Cube->CreateBodySetup();
    FKBoxElem Box;
    Box.X = Box.Y = Box.Z = 100;
    Cube->BodySetup->AggGeom.BoxElems.Add(Box);
    Cube->BodySetup->InvalidatePhysicsData();
    Cube->BodySetup->CreatePhysicsMeshes();
    Cube->PostEditChange();
    Made.Add(Cube.Get());

    TArray<TPair<FString, UMaterialInterface*>> Row;
    const auto Make = [&](const TCHAR* Name, TFunctionRef<void(UMaterial*)> Build)
    {
        auto* Material = NewObject<UMaterial>(CreatePackage(*(Root / Name)), Name, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Material);
        Build(Material);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Made.Add(Material);
        Row.Emplace(Name, Material);
        return Material;
    };
    Make(TEXT("M_Row01_Color"), [&](UMaterial* M) { M->BaseColor.Connect(0, Color(M, FLinearColor(0.8f, 0.05f, 0.05f))); });
    Make(TEXT("M_Row02_Texture"), [&](UMaterial* M) { M->BaseColor.Connect(0, Sample(M, Checker)); });
    UMaterial* Master = Make(TEXT("M_Row03_Parameters"),
        [&](UMaterial* M)
        {
            auto* Tint = Node<UMaterialExpressionVectorParameter>(M);
            Tint->ParameterName = TEXT("Tint");
            Tint->DefaultValue = FLinearColor(0.2f, 0.9f, 0.3f);
            auto* Image = Node<UMaterialExpressionTextureSampleParameter2D>(M);
            Image->ParameterName = TEXT("Image");
            Image->Texture = Checker;
            Image->AutoSetSampleType();
            auto* Times = Node<UMaterialExpressionMultiply>(M);
            Times->A.Connect(0, Image);
            Times->B.Connect(0, Tint);
            M->BaseColor.Connect(0, Times);
            auto* Rough = Node<UMaterialExpressionScalarParameter>(M);
            Rough->ParameterName = TEXT("Roughness");
            Rough->DefaultValue = 0.3f;
            M->Roughness.Connect(0, Rough);
            auto* Metal = Node<UMaterialExpressionScalarParameter>(M);
            Metal->ParameterName = TEXT("Metallic");
            Metal->DefaultValue = 0.f;
            M->Metallic.Connect(0, Metal);
        });
    Make(TEXT("M_Row04_NormalMap"),
        [&](UMaterial* M)
        {
            M->BaseColor.Connect(0, Color(M, FLinearColor(0.6f, 0.6f, 0.6f)));
            M->Normal.Connect(0, Sample(M, Bumps));
        });
    Make(TEXT("M_Row05_Emissive"),
        [&](UMaterial* M)
        {
            M->BaseColor.Connect(0, Color(M, FLinearColor(0.05f, 0.05f, 0.05f)));
            auto* Glow = Node<UMaterialExpressionMultiply>(M);
            Glow->A.Connect(0, Color(M, FLinearColor(0.1f, 0.9f, 1.f)));
            Glow->B.Connect(0, Number(M, 4.f));
            M->EmissiveColor.Connect(0, Glow);
        });
    Make(TEXT("M_Row06_Unlit"),
        [&](UMaterial* M)
        {
            M->SetShadingModel(MSM_Unlit);
            M->EmissiveColor.Connect(0, Sample(M, Checker));
        });
    Make(TEXT("M_Row07_Masked"),
        [&](UMaterial* M)
        {
            M->BlendMode = BLEND_Masked;
            auto* Image = Sample(M, Checker);
            M->BaseColor.Connect(0, Image);
            M->OpacityMask.Connect(4, Image);
        });
    Make(TEXT("M_Row08_Translucent"),
        [&](UMaterial* M)
        {
            M->BlendMode = BLEND_Translucent;
            M->BaseColor.Connect(0, Color(M, FLinearColor(0.1f, 0.3f, 1.f)));
            M->Opacity.Connect(0, Number(M, 0.4f));
        });
    Make(TEXT("M_Row09_Additive"),
        [&](UMaterial* M)
        {
            M->BlendMode = BLEND_Additive;
            M->SetShadingModel(MSM_Unlit);
            M->EmissiveColor.Connect(0, Color(M, FLinearColor(1.f, 0.4f, 0.05f)));
        });
    Make(TEXT("M_Row10_TwoSided"),
        [&](UMaterial* M)
        {
            M->TwoSided = true;
            M->BaseColor.Connect(0, Color(M, FLinearColor(0.9f, 0.4f, 0.8f)));
        });
    Make(TEXT("M_Row11_Panner"),
        [&](UMaterial* M)
        {
            auto* Pan = Node<UMaterialExpressionPanner>(M);
            Pan->SpeedX = 0.25f;
            Pan->SpeedY = 0.1f;
            auto* Image = Sample(M, Checker);
            Image->Coordinates.Connect(0, Pan);
            M->BaseColor.Connect(0, Image);
        });
    Make(TEXT("M_Row12_Lerp"),
        [&](UMaterial* M)
        {
            auto* Mix = Node<UMaterialExpressionLinearInterpolate>(M);
            Mix->A.Connect(0, Color(M, FLinearColor(1.f, 0.5f, 0.f)));
            Mix->B.Connect(0, Color(M, FLinearColor(0.3f, 0.f, 0.6f)));
            Mix->Alpha.Connect(1, Sample(M, Checker));
            M->BaseColor.Connect(0, Mix);
        });
    Make(TEXT("M_Row13_VertexColor"),
        [&](UMaterial* M)
        {
            auto* Times = Node<UMaterialExpressionMultiply>(M);
            Times->A.Connect(0, Node<UMaterialExpressionVertexColor>(M));
            Times->B.Connect(0, Color(M, FLinearColor(0.2f, 0.8f, 0.8f)));
            M->BaseColor.Connect(0, Times);
        });
    Make(TEXT("M_Row14_Fresnel"),
        [&](UMaterial* M)
        {
            M->BaseColor.Connect(0, Color(M, FLinearColor(0.05f, 0.1f, 0.3f)));
            auto* Rim = Node<UMaterialExpressionMultiply>(M);
            Rim->A.Connect(0, Node<UMaterialExpressionFresnel>(M));
            Rim->B.Connect(0, Color(M, FLinearColor(1.f, 1.f, 1.f)));
            M->EmissiveColor.Connect(0, Rim);
        });
    Make(TEXT("M_Row15_WorldPosition"),
        [&](UMaterial* M)
        {
            auto* Scale = Node<UMaterialExpressionMultiply>(M);
            Scale->A.Connect(0, Node<UMaterialExpressionWorldPosition>(M));
            Scale->B.Connect(0, Number(M, 0.01f));
            auto* Bands = Node<UMaterialExpressionFrac>(M);
            Bands->Input.Connect(0, Scale);
            M->BaseColor.Connect(0, Bands);
        });
    // An instance of the row's own master with other values, made by the creator instance builder.
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>(), Vectors = MakeShared<FJsonObject>(),
                                Scalars = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("parent"), Master->GetPathName());
        TArray<TSharedPtr<FJsonValue>> Tint;
        for (double Part : {1.0, 0.2, 0.9, 1.0})
            Tint.Add(MakeShared<FJsonValueNumber>(Part));
        Vectors->SetArrayField(TEXT("Tint"), Tint);
        Scalars->SetNumberField(TEXT("Metallic"), 1.0);
        Json->SetObjectField(TEXT("vectors"), Vectors);
        Json->SetObjectField(TEXT("scalars"), Scalars);
        FString Input, Error;
        FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Input));
        TArray<FString> Missing;
        UMaterialInstanceConstant* Instance =
            P4BuildMaterialInstance(Input, Root / TEXT("MI_Row16_OwnMaster"), Missing, Error);
        if (!Test.TestTrue(TEXT("the creator instance builder makes an instance of a creator master: ") + Error,
                Instance != nullptr))
            return false;
        Made.Add(Instance);
        Row.Emplace(TEXT("MI_Row16_OwnMaster"), Instance);
    }

    TArray<UPackage*> Packages;
    for (UObject* Asset : Made)
        Packages.AddUnique(Asset->GetOutermost());
    if (!Test.TestTrue(TEXT("The material row's assets save through the normal editor"),
            UEditorLoadingAndSavingUtils::SavePackages(Packages, true)))
        return false;
    // Two rows of eight, 1.5 m apart, facing the same way.
    for (int32 Index = 0; Index < Row.Num(); ++Index)
    {
        FActorSpawnParameters Spawn;
        Spawn.Name = FName(*(TEXT("P4MaterialRow_") + Row[Index].Key));
        Spawn.OverrideLevel = World->PersistentLevel;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Where = At + FVector((Index % 8) * 150.f, (Index / 8) * 200.f, 0.f);
        auto* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Where), Spawn);
        if (!Test.TestNotNull(TEXT("A material row cube is placed: ") + Row[Index].Key, Actor))
            return false;
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube.Get());
        Actor->GetStaticMeshComponent()->SetMaterial(0, Row[Index].Value);
        Actor->SetActorLabel(Row[Index].Key);
        Names.Add(Row[Index].Key + TEXT(" at ") + Where.ToString());
        Actors.Add(Actor);
    }
    return true;
}
} // namespace P4ExportDriverTest
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "StaticParameterSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "P4KadanMaterialExpressions.h"
#include "P4MaterialGraph.h"
#include "P4OriginalRecord.h"
#include "P4OriginalLandscapePreview.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "Serialization/JsonSerializer.h"

namespace P4SweepCapture
{
bool Capture(UWorld*, const FString&, const TSharedRef<FJsonObject>&);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LandscapeRenderDiagnostics, "P4MapKit.Editor.LandscapeRenderDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LandscapeRenderDiagnostics::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_DDB_FLOOR_DIAGNOSTIC")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned black-floor diagnostic fixture."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Carrot4/Maps/Madori/Ddb/DDB_AI005/DDB_AI005_P"));
    if (!TestNotNull(TEXT("The actual DDB_AI005 map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Components;
    TSet<UMaterialInterface*> Seen;
    TArray<TSharedPtr<FJsonValue>> MaterialRows;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<ULandscapeComponent*> Parts;
                    Actor->GetComponents(Parts);
                    for (ULandscapeComponent* Part : Parts)
                    {
                        auto Row = MakeShared<FJsonObject>();
                        Row->SetStringField(TEXT("component"), Part->GetPathName());
                        TArray<TSharedPtr<FJsonValue>> Layers;
                        for (const auto& Layer : Part->GetWeightmapLayerAllocations())
                            Layers.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("texture=%d channel=%d"),
                                int32(Layer.WeightmapTextureIndex), int32(Layer.WeightmapTextureChannel))));
                        Row->SetArrayField(TEXT("layer_allocations"), Layers);
                        TArray<TSharedPtr<FJsonValue>> Weightmaps;
                        for (UTexture2D* Texture : Part->GetWeightmapTextures())
                            if (Texture)
                            {
                                auto Map = MakeShared<FJsonObject>();
                                Map->SetStringField(TEXT("texture"), Texture->GetPathName());
                                Map->SetNumberField(TEXT("format"), int32(Texture->Source.GetFormat()));
                                Map->SetNumberField(TEXT("width"), Texture->Source.GetSizeX());
                                Map->SetNumberField(TEXT("height"), Texture->Source.GetSizeY());
                                TArray64<uint8> Bytes;
                                if (Weightmaps.Num() == 0 && Components.Num() == 0 &&
                                    Texture->Source.GetMipData(Bytes, 0))
                                {
                                    TArray<TSharedPtr<FJsonValue>> Channels;
                                    if (Texture->Source.GetFormat() == TSF_BGRA8)
                                        for (int32 Channel = 0; Channel < 4; ++Channel)
                                        {
                                            uint8 Min = 255, Max = 0;
                                            uint64 Sum = 0;
                                            for (int64 At = Channel; At < Bytes.Num(); At += 4)
                                            {
                                                Min = FMath::Min(Min, Bytes[At]);
                                                Max = FMath::Max(Max, Bytes[At]);
                                                Sum += Bytes[At];
                                            }
                                            Channels.Add(MakeShared<FJsonValueString>(
                                                FString::Printf(TEXT("BGRA[%d] min=%d max=%d mean=%.3f"), Channel, Min,
                                                    Max, double(Sum) / FMath::Max<int64>(1, Bytes.Num() / 4))));
                                        }
                                    Map->SetArrayField(TEXT("source_channels"), Channels);
                                }
                                Weightmaps.Add(MakeShared<FJsonValueObject>(Map));
                            }
                        Row->SetArrayField(TEXT("weightmaps"), Weightmaps);
                        TArray<TSharedPtr<FJsonValue>> Slots;
                        for (UMaterialInstanceConstant* Slot : Part->MaterialInstances)
                        {
                            Slots.Add(MakeShared<FJsonValueString>(Slot ? Slot->GetPathName() : TEXT("null")));
                            for (UMaterialInterface* Material = Slot; Material && !Seen.Contains(Material);)
                            {
                                Seen.Add(Material);
                                auto Item = MakeShared<FJsonObject>();
                                Item->SetStringField(TEXT("material"), Material->GetPathName());
                                TArray<FMaterialParameterInfo> Infos;
                                TArray<FGuid> Ids;
                                TArray<TSharedPtr<FJsonValue>> Textures;
                                Material->GetAllTextureParameterInfo(Infos, Ids);
                                for (const auto& Info : Infos)
                                {
                                    UTexture* Texture = nullptr;
                                    Material->GetTextureParameterValue(Info, Texture);
                                    Textures.Add(MakeShared<FJsonValueString>(Info.Name.ToString() + TEXT("=") +
                                        (Texture ? Texture->GetPathName() : TEXT("null"))));
                                }
                                Item->SetArrayField(TEXT("effective_textures"), Textures);
                                if (auto* Instance = Cast<UMaterialInstanceConstant>(Material))
                                {
                                    TArray<TSharedPtr<FJsonValue>> StoredTextures;
                                    for (const auto& Texture : Instance->TextureParameterValues)
                                        StoredTextures.Add(MakeShared<FJsonValueString>(
                                            Texture.ParameterInfo.Name.ToString() + TEXT("=") +
                                            (Texture.ParameterValue ? Texture.ParameterValue->GetPathName()
                                                                    : TEXT("null"))));
                                    Item->SetArrayField(TEXT("stored_texture_overrides"), StoredTextures);
                                    FStaticParameterSet Static;
                                    Instance->GetStaticParameterValues(Static);
                                    TArray<TSharedPtr<FJsonValue>> Weights;
                                    for (const auto& Weight : Static.TerrainLayerWeightParameters)
                                        Weights.Add(MakeShared<FJsonValueString>(
                                            FString::Printf(TEXT("%s index=%d override=%d blend=%d"),
                                                *Weight.ParameterInfo.Name.ToString(), Weight.WeightmapIndex,
                                                int32(Weight.bOverride), int32(Weight.bWeightBasedBlend))));
                                    Item->SetArrayField(TEXT("terrain_static_weights"), Weights);
                                    Item->SetStringField(TEXT("parent"),
                                        Instance->Parent ? Instance->Parent->GetPathName() : TEXT("null"));
                                    Item->SetNumberField(TEXT("serialized_terrain_static_weight_count"),
                                        Instance->GetStaticParameters().TerrainLayerWeightParameters.Num());
                                    if (auto* Resource = Instance->GetMaterialResource(GMaxRHIFeatureLevel))
                                        if (auto* Owner = Resource->GetMaterialInterface())
                                            Item->SetStringField(TEXT("resource_owner"), Owner->GetPathName());
                                    Material = Instance->Parent;
                                }
                                else
                                {
                                    TArray<TSharedPtr<FJsonValue>> Expressions;
                                    if (auto* Base = Cast<UMaterial>(Material))
                                    {
                                        Item->SetStringField(TEXT("base_color_expression"),
                                            Base->BaseColor.Expression ? Base->BaseColor.Expression->GetName()
                                                                       : TEXT("null"));
                                        Item->SetStringField(TEXT("emissive_expression"),
                                            Base->EmissiveColor.Expression ? Base->EmissiveColor.Expression->GetName()
                                                                           : TEXT("null"));
                                        for (UMaterialExpression* Expression : Base->Expressions)
                                            if (Expression)
                                            {
                                                FString Value = Expression->GetName() + TEXT(" ") +
                                                    Expression->GetClass()->GetName();
                                                if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
                                                    Value += TEXT(" ") + Custom->Code.Left(2048);
                                                if (auto* Custom = Cast<UMaterialExpressionKadanGameCustom>(Expression))
                                                    Value += TEXT(" ") + Custom->Code.Left(2048);
                                                Expressions.Add(MakeShared<FJsonValueString>(Value));
                                            }
                                    }
                                    Item->SetArrayField(TEXT("expression_classes"), Expressions);
                                    Material = nullptr;
                                }
                                MaterialRows.Add(MakeShared<FJsonValueObject>(Item));
                            }
                        }
                        Row->SetArrayField(TEXT("material_slots"), Slots);
                        Components.Add(MakeShared<FJsonValueObject>(Row));
                    }
                }
    Report->SetArrayField(TEXT("landscape_components"), Components);
    Report->SetArrayField(TEXT("landscape_materials"), MaterialRows);
    TestTrue(TEXT("The black-floor source contains actual landscape components"), Components.Num() > 0);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/DdbFloorDiagnostic");
    Report->SetNumberField(TEXT("previews_after_open"), P4OriginalLandscapeColorPreviewCount());
    TestTrue(TEXT("The current material is captured with four real SceneCapture views"),
        P4SweepCapture::Capture(World, Folder, Report));
    // The same map with the previews applied by hand, once the streaming levels are in.
    Report->SetNumberField(TEXT("previews_applied_after_streaming"), P4ApplyOriginalLandscapeColorPreviews(World));
    Report->SetNumberField(TEXT("previews_held"), P4OriginalLandscapeColorPreviewCount());
    auto Applied = MakeShared<FJsonObject>();
    TestTrue(TEXT("The applied previews are captured too"), P4SweepCapture::Capture(World, Folder / TEXT("applied"), Applied));
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("The actual material and weightmap diagnostics are retained"),
        FFileHelper::SaveStringToFile(
            Json, *(Folder / TEXT("diagnostic.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}

// A diagnostic experiment, not an importer admission path. The private recipe is
// recovered separately from the actual inline source shader, and never saved here.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LandscapeBaseColorResourceProbe, "P4MapKit.Editor.LandscapeBaseColorResourceProbe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LandscapeBaseColorResourceProbe::RunTest(const FString&)
{
    const FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_DDB_BASE_COLOR_RECIPE"));
    if (File.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned inline-source base-color recipe."));
        return true;
    }
    FString Json, Error;
    if (!TestTrue(
            TEXT("The owned source-derived probe recipe is readable"), FFileHelper::LoadFileToString(Json, *File)))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Carrot4/Maps/Madori/Ddb/DDB_AI005/DDB_AI005_P"));
    if (!TestNotNull(TEXT("The actual black-floor map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    TSet<UMaterialInstanceConstant*> Slots;
    UWorld* LandscapeWorld = nullptr;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<ULandscapeComponent*> Parts;
                    Actor->GetComponents(Parts);
                    for (auto* Part : Parts)
                    {
                        LandscapeWorld = Cast<UWorld>(Level->GetOuter());
                        for (auto* Slot : Part->MaterialInstances)
                            if (Slot)
                                Slots.Add(Slot);
                    }
                }
    if (!TestTrue(TEXT("The actual floor has material slots and a source world"), Slots.Num() > 0 && LandscapeWorld))
        return false;
    const auto BeforeState = P4DiagnoseOriginalAsset(LandscapeWorld);
    if (!TestTrue(TEXT("The floor starts source-identical before the render-only experiment"),
            BeforeState.Error.IsEmpty() && BeforeState.Comparison == EP4OriginalComparison::Equal))
        return false;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/DdbBaseColorResourceProbe");
    auto Before = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("The unchanged black-floor control is captured"),
            P4SweepCapture::Capture(World, Folder / TEXT("before"), Before)))
        return false;
    UMaterial* Material = P4BuildMaterialFromGraph(Json, TEXT("/Game/P4MapKitTests/InlineLandscapeColorProbe"), Error);
    if (!TestNotNull(TEXT("The actual source base-color graph builds: ") + Error, Material))
        return false;
    ON_SCOPE_EXIT
    {
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->MarkPendingKill();
    };
    auto* Permutation = NewObject<ULandscapeMaterialInstanceConstant>(GetTransientPackage());
    Permutation->AddToRoot();
    ON_SCOPE_EXIT
    {
        Permutation->RemoveFromRoot();
    };
    Permutation->SetParentEditorOnly(Material);
    FStaticParameterSet Static;
    (*Slots.CreateConstIterator())->GetStaticParameterValues(Static);
    Permutation->UpdateStaticPermutation(Static);
    Permutation->PostEditChange();
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
    FlushRenderingCommands();
    FMaterialResource* Preview = Permutation->GetMaterialResource(GMaxRHIFeatureLevel);
    if (!TestTrue(TEXT("The source graph has a real compiled Landscape permutation"),
            Preview && Preview->HasValidGameThreadShaderMap() && !Preview->GetCompileErrors().Num()))
        return false;
    TMap<FMaterialResource*, TRefCountPtr<FMaterialShaderMap>> Original;
    TMap<UMaterialInstanceConstant*, UMaterialInterface*> Parents;
    for (auto* Slot : Slots)
    {
        Parents.Add(Slot, Slot->Parent);
        if (auto* Resource = Slot->GetMaterialResource(GMaxRHIFeatureLevel))
            Original.FindOrAdd(Resource) = Resource->GetGameThreadShaderMap();
    }
    const auto Refresh = [&]()
    {
        FMaterialUpdateContext Update;
        for (auto* Slot : Slots)
        {
            Update.AddMaterialInstance(Slot);
            Slot->GetRenderProxy()->CacheUniformExpressions_GameThread(true);
        }
    };
    ON_SCOPE_EXIT
    {
        TMap<FMaterial*, FMaterialShaderMap*> Restore;
        for (const auto& Pair : Original)
            Restore.Add(Pair.Key, Pair.Value);
        SetShaderMapsOnMaterialResources(Restore);
        Refresh();
        FlushRenderingCommands();
    };
    TMap<FMaterial*, FMaterialShaderMap*> Replace;
    for (const auto& Pair : Original)
        Replace.Add(Pair.Key, Preview->GetGameThreadShaderMap());
    SetShaderMapsOnMaterialResources(Replace);
    Refresh();
    FlushRenderingCommands();
    const auto AfterState = P4DiagnoseOriginalAsset(LandscapeWorld);
    TestTrue(TEXT("A render-resource experiment changes no original map state"),
        AfterState.Error.IsEmpty() && AfterState.Comparison == EP4OriginalComparison::Equal);
    for (const auto& Parent : Parents)
        TestTrue(TEXT("The serialized original material parent is untouched"), Parent.Key->Parent == Parent.Value);
    auto After = MakeShared<FJsonObject>();
    After->SetNumberField(TEXT("borrowed_resource_count"), Original.Num());
    TArray<TSharedPtr<FJsonValue>> ResourceOwners;
    for (const auto& Pair : Original)
        if (auto* Owner = Pair.Key->GetMaterialInterface())
            ResourceOwners.Add(MakeShared<FJsonValueString>(Owner->GetPathName()));
    After->SetArrayField(TEXT("borrowed_resource_owners"), ResourceOwners);
    if (!TestTrue(TEXT("The borrowed source-derived shader is captured"),
            P4SweepCapture::Capture(World, Folder / TEXT("after"), After)))
        return false;
    const double BeforeLit = Before->GetArrayField(TEXT("captures"))[0]->AsObject()->GetNumberField(TEXT("lit_pixels"));
    const double AfterLit = After->GetArrayField(TEXT("captures"))[0]->AsObject()->GetNumberField(TEXT("lit_pixels"));
    TestTrue(
        TEXT("The source base color makes the formerly black floor visible"), AfterLit > BeforeLit + 1280 * 720 / 5);
    return !HasAnyErrors();
}
#endif

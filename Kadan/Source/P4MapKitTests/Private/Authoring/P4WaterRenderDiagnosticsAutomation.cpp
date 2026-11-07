#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/MapBuildDataRegistry.h"
#include "P4OriginalBuiltLighting.h"
#include "P4MaterialGraph.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "P4KadanMaterialExpressions.h"

namespace P4SweepCapture
{
bool Capture(UWorld*, const FString&, const TSharedRef<FJsonObject>&);
}

// A diagnostic experiment, not an importer path. The named map is captured as the sweep captures it, then again with
// the named masters changed (P4_NATIVE_WATER_DIAGNOSTIC_VARIANTS picks the variants, "all" by default): outputs split
// or replaced by constants, other translucent lighting modes, inputs checked for range, stock slabs over the water
// ("slabs", placed by P4_NATIVE_WATER_DIAGNOSTIC_SLABS_AT) and expression classes stubbed out
// (P4_NATIVE_WATER_DIAGNOSTIC_STUBS). The report keeps the instances' parameter values and the map's directional lights.
// The masters are changed in memory only and put back; nothing is saved.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WaterRenderDiagnostics, "P4MapKit.Editor.WaterRenderDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WaterRenderDiagnostics::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WATER_DIAGNOSTIC"));
    TArray<FString> MasterPaths;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WATER_DIAGNOSTIC_MASTERS")).ParseIntoArray(MasterPaths, TEXT(";"));
    if (Map.IsEmpty() || !MasterPaths.Num())
    {
        AddWarning(TEXT("P4MapKit test not run. Name a map and its water masters."));
        return true;
    }
    // The game's built lighting packages named here are brought in first, as an import does.
    TArray<FString> Lighting;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_BUILT_LIGHTING_HEADERS")).ParseIntoArray(Lighting, TEXT(";"));
    TArray<TSharedPtr<FJsonValue>> LightingRows;
    for (const FString& Header : Lighting)
    {
        const double Began = FPlatformTime::Seconds();
        const FP4OriginalBuiltLightingResult Imported = P4ImportOriginalBuiltLighting(Header, FString(), true);
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("header"), Header);
        Row->SetStringField(TEXT("package"), Imported.Package);
        Row->SetStringField(TEXT("error"), Imported.Error);
        Row->SetBoolField(TEXT("made"), Imported.Registry != nullptr);
        Row->SetNumberField(TEXT("textures"), Imported.Textures);
        Row->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Began);
        LightingRows.Add(MakeShared<FJsonValueObject>(Row));
        TestTrue(TEXT("The built lighting is brought in: ") + Header + TEXT(" ") + Imported.Error, Imported.Registry != nullptr);
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    TArray<UMaterial*> Masters;
    for (const FString& Path : MasterPaths)
        if (UMaterial* Master = LoadObject<UMaterial>(nullptr, *Path))
            Masters.Add(Master);
    TestEqual(TEXT("Every named master loads"), Masters.Num(), MasterPaths.Num());
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/WaterDiagnostic");
    auto Report = MakeShared<FJsonObject>();
    Report->SetArrayField(TEXT("built_lighting_imports"), LightingRows);
    // The instances the map draws with these masters, with every parameter value they resolve to.
    TArray<TSharedPtr<FJsonValue>> Rows;
    TSet<UMaterialInterface*> Seen;
    TArray<FBox> WaterBounds;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<UMeshComponent*> Meshes(Actor);
                    for (UMeshComponent* Mesh : Meshes)
                        for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
                        {
                            UMaterialInterface* Material = Mesh->GetMaterial(Slot);
                            if (!Material || Seen.Contains(Material) || !Masters.Contains(Material->GetMaterial()))
                                continue;
                            Seen.Add(Material);
                            auto Row = MakeShared<FJsonObject>();
                            Row->SetStringField(TEXT("material"), Material->GetPathName());
                            Row->SetStringField(TEXT("component"), Mesh->GetPathName());
                            Row->SetStringField(TEXT("transform"), Mesh->GetComponentTransform().ToString());
                            Row->SetStringField(TEXT("bounds"), Mesh->Bounds.GetBox().ToString());
                            WaterBounds.Add(Mesh->Bounds.GetBox());
                            // The water actor's own values (the game's native water sets material parameters from them).
                            auto Values = MakeShared<FJsonObject>();
                            for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
                            {
                                const FString Name = It->GetName();
                                if (!It->IsA<FNumericProperty>() && !It->IsA<FStructProperty>() && !It->IsA<FBoolProperty>() && !It->IsA<FObjectPropertyBase>())
                                    continue;
                                if (It->GetOwnerClass() && It->GetOwnerClass()->GetName() == TEXT("Actor"))
                                    continue;
                                FString Text;
                                It->ExportTextItem(Text, It->ContainerPtrToValuePtr<void>(Actor), nullptr, Actor, PPF_None);
                                if (Text.Len() < 300)
                                    Values->SetStringField(It->GetOwnerClass()->GetName() + TEXT(".") + Name, Text);
                            }
                            Row->SetObjectField(TEXT("actor_values"), Values);
                            TArray<TSharedPtr<FJsonValue>> Parts;
                            TInlineComponentArray<UActorComponent*> All(Actor);
                            for (UActorComponent* Part : All)
                            {
                                FString Text = Part->GetName() + TEXT(" ") + Part->GetClass()->GetName();
                                if (auto* Decal = Cast<UDecalComponent>(Part))
                                    Text += FString::Printf(TEXT(" material=%s size=%s visible=%d registered=%d"),
                                        Decal->GetDecalMaterial() ? *Decal->GetDecalMaterial()->GetPathName() : TEXT("none"),
                                        *Decal->DecalSize.ToString(), int32(Decal->IsVisible()), int32(Decal->IsRegistered()));
                                Parts.Add(MakeShared<FJsonValueString>(Text));
                            }
                            Row->SetArrayField(TEXT("actor_components"), Parts);
                            Row->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
                            TArray<FMaterialParameterInfo> Infos;
                            TArray<FGuid> Ids;
                            Material->GetAllScalarParameterInfo(Infos, Ids);
                            auto Scalars = MakeShared<FJsonObject>();
                            for (const auto& Info : Infos)
                            {
                                float Value = 0;
                                if (Material->GetScalarParameterValue(Info, Value))
                                    Scalars->SetNumberField(Info.Name.ToString(), Value);
                            }
                            Row->SetObjectField(TEXT("scalars"), Scalars);
                            Material->GetAllVectorParameterInfo(Infos, Ids);
                            auto Vectors = MakeShared<FJsonObject>();
                            for (const auto& Info : Infos)
                            {
                                FLinearColor Value;
                                if (Material->GetVectorParameterValue(Info, Value))
                                    Vectors->SetStringField(Info.Name.ToString(), Value.ToString());
                            }
                            Row->SetObjectField(TEXT("vectors"), Vectors);
                            Material->GetAllTextureParameterInfo(Infos, Ids);
                            auto Textures = MakeShared<FJsonObject>();
                            for (const auto& Info : Infos)
                            {
                                UTexture* Value = nullptr;
                                if (Material->GetTextureParameterValue(Info, Value))
                                    Textures->SetStringField(Info.Name.ToString(), Value ? Value->GetPathName() : TEXT("null"));
                            }
                            Row->SetObjectField(TEXT("textures"), Textures);
                            Rows.Add(MakeShared<FJsonValueObject>(Row));
                        }
                }
    Report->SetArrayField(TEXT("instances"), Rows);
    // The map's directional lights, as the renderer chooses one of them for forward (per-pixel translucent) lighting.
    TArray<TSharedPtr<FJsonValue>> Lights;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<UDirectionalLightComponent*> Parts(Actor);
                    for (UDirectionalLightComponent* Light : Parts)
                    {
                        auto Row = MakeShared<FJsonObject>();
                        Row->SetStringField(TEXT("light"), Light->GetPathName());
                        Row->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
                        Row->SetNumberField(TEXT("intensity"), Light->Intensity);
                        Row->SetStringField(TEXT("color"), Light->GetLightColor().ToString());
                        Row->SetNumberField(TEXT("mobility"), int32(Light->Mobility));
                        Row->SetBoolField(TEXT("visible"), Light->IsVisible());
                        Row->SetBoolField(TEXT("registered"), Light->IsRegistered());
                        Row->SetBoolField(TEXT("affects_world"), Light->bAffectsWorld);
                        Row->SetBoolField(TEXT("atmosphere_sun"), Light->bUsedAsAtmosphereSunLight);
                        Row->SetBoolField(TEXT("editor_hidden"), Actor->IsHiddenEd());
                        Row->SetStringField(TEXT("rotation"), Light->GetComponentRotation().ToString());
                        Lights.Add(MakeShared<FJsonValueObject>(Row));
                    }
                }
    Report->SetArrayField(TEXT("directional_lights"), Lights);
    // What the editor reflects in: the map's sky lights and reflection captures.
    TArray<TSharedPtr<FJsonValue>> Reflections;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<USceneComponent*> Parts(Actor);
                    for (USceneComponent* Part : Parts)
                    {
                        auto Row = MakeShared<FJsonObject>();
                        if (auto* Sky = Cast<USkyLightComponent>(Part))
                        {
                            Row->SetStringField(TEXT("sky_light"), Sky->GetPathName());
                            Row->SetNumberField(TEXT("intensity"), Sky->Intensity);
                            Row->SetNumberField(TEXT("source_type"), int32(Sky->SourceType));
                            Row->SetStringField(TEXT("cubemap"), Sky->Cubemap ? Sky->Cubemap->GetPathName() : FString());
                            Row->SetBoolField(TEXT("visible"), Sky->IsVisible());
                        }
                        else if (auto* Capture = Cast<UReflectionCaptureComponent>(Part))
                        {
                            Row->SetStringField(TEXT("reflection_capture"), Capture->GetPathName());
                            Row->SetStringField(TEXT("location"), Capture->GetComponentLocation().ToString());
                            Row->SetBoolField(TEXT("visible"), Capture->IsVisible());
                        }
                        else
                            continue;
                        if (Reflections.Num() < 64)
                            Reflections.Add(MakeShared<FJsonValueObject>(Row));
                    }
                }
    Report->SetArrayField(TEXT("reflection_sources"), Reflections);
    // Each level's built lighting: its registry and identity, and how many of its meshes carry a lighting id.
    TArray<TSharedPtr<FJsonValue>> LevelLighting;
    for (ULevel* Level : World->GetLevels())
        if (Level)
        {
            int32 Meshes = 0, WithIds = 0, Found = 0, InRegistry = 0;
            TArray<TSharedPtr<FJsonValue>> Missed;
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
                    for (UStaticMeshComponent* Part : Parts)
                    {
                        ++Meshes;
                        if (Part->LODData.Num() && Part->LODData[0].MapBuildDataId.IsValid())
                        {
                            ++WithIds;
                            const bool Drawn = Part->GetMeshMapBuildData(Part->LODData[0]) != nullptr;
                            const bool Held = Level->MapBuildData && Level->MapBuildData->GetMeshBuildData(Part->LODData[0].MapBuildDataId);
                            Found += Drawn;
                            InRegistry += Held;
                            if (!Drawn && Missed.Num() < 6)
                            {
                                const UStaticMesh* Mesh = Part->GetStaticMesh();
                                Missed.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s mesh %s lods %d, component lods %d, in registry %d, static lighting %d"),
                                    *Part->GetPathName(Level), Mesh ? *Mesh->GetPathName() : TEXT("none"),
                                    Mesh && Mesh->RenderData ? Mesh->RenderData->LODResources.Num() : -1, Part->LODData.Num(), int32(Held),
                                    int32(Part->HasStaticLighting()))));
                            }
                        }
                    }
                }
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("level"), Level->GetOutermost()->GetName());
            Row->SetStringField(TEXT("map_build_data"), Level->MapBuildData ? Level->MapBuildData->GetPathName() : TEXT("none"));
            Row->SetStringField(TEXT("level_build_data_id"), Level->LevelBuildDataId.ToString());
            Row->SetNumberField(TEXT("static_mesh_components"), Meshes);
            Row->SetNumberField(TEXT("with_lighting_ids"), WithIds);
            Row->SetNumberField(TEXT("with_lighting_found"), Found);
            Row->SetNumberField(TEXT("with_lighting_in_registry"), InRegistry);
            Row->SetArrayField(TEXT("missed_examples"), Missed);
            LevelLighting.Add(MakeShared<FJsonValueObject>(Row));
        }
    Report->SetArrayField(TEXT("level_lighting"), LevelLighting);
    // The environment controllers: their components, and every section of their day scene with its values.
    TArray<TSharedPtr<FJsonValue>> Environments;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
            {
                if (!IsValid(Actor) || !Actor->GetClass()->GetName().Contains(TEXT("EnvController")))
                    continue;
                auto Row = MakeShared<FJsonObject>();
                Row->SetStringField(TEXT("actor"), Actor->GetPathName());
                TArray<TSharedPtr<FJsonValue>> Parts;
                TInlineComponentArray<UActorComponent*> All(Actor);
                for (UActorComponent* Part : All)
                    Parts.Add(MakeShared<FJsonValueString>(Part->GetName() + TEXT(" ") + Part->GetClass()->GetName()));
                Row->SetArrayField(TEXT("components"), Parts);
                auto Values = MakeShared<FJsonObject>();
                for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
                    if (It->GetOwnerClass() && It->GetOwnerClass()->GetName() == TEXT("EnvController"))
                    {
                        FString Text;
                        It->ExportTextItem(Text, It->ContainerPtrToValuePtr<void>(Actor), nullptr, Actor, PPF_None);
                        Values->SetStringField(It->GetName(), Text.Left(300));
                    }
                Row->SetObjectField(TEXT("values"), Values);
                FObjectPropertyBase* SceneProperty = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), TEXT("EnvParamSceneDay"));
                UObject* Scene = SceneProperty ? SceneProperty->GetObjectPropertyValue_InContainer(Actor) : nullptr;
                Row->SetStringField(TEXT("day_scene"), Scene ? Scene->GetPathName() : TEXT("none"));
                if (Scene)
                {
                    auto Sections = MakeShared<FJsonObject>();
                    for (TFieldIterator<FObjectPropertyBase> It(Scene->GetClass()); It; ++It)
                    {
                        UObject* Section = It->GetObjectPropertyValue_InContainer(Scene);
                        if (!Section)
                            continue;
                        auto Fields = MakeShared<FJsonObject>();
                        Fields->SetStringField(TEXT("_asset"), Section->GetPathName());
                        for (TFieldIterator<FProperty> Field(Section->GetClass()); Field; ++Field)
                        {
                            if (Field->GetOwnerClass() == UObject::StaticClass())
                                continue;
                            FString Text;
                            Field->ExportTextItem(Text, Field->ContainerPtrToValuePtr<void>(Section), nullptr, Section, PPF_None);
                            Fields->SetStringField(Field->GetName(), Text.Left(400));
                        }
                        Sections->SetObjectField(It->GetName(), Fields);
                    }
                    Row->SetObjectField(TEXT("day_sections"), Sections);
                }
                Environments.Add(MakeShared<FJsonValueObject>(Row));
            }
    Report->SetArrayField(TEXT("environments"), Environments);
    // The material instances the native construction previews put on the map's meshes, with their values.
    TArray<TSharedPtr<FJsonValue>> Previewed;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
            {
                if (!IsValid(Actor))
                    continue;
                TInlineComponentArray<UMeshComponent*> Meshes(Actor);
                for (UMeshComponent* Mesh : Meshes)
                    for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
                    {
                        auto* Instance = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot));
                        if (!Instance || Instance->GetOuter() != GetTransientPackage())
                            continue;
                        auto Row = MakeShared<FJsonObject>();
                        Row->SetStringField(TEXT("component"), Mesh->GetPathName());
                        Row->SetNumberField(TEXT("slot"), Slot);
                        Row->SetStringField(TEXT("parent"), Instance->Parent ? Instance->Parent->GetPathName() : TEXT("none"));
                        auto Values = MakeShared<FJsonObject>();
                        for (const FScalarParameterValue& Value : Instance->ScalarParameterValues)
                            Values->SetNumberField(Value.ParameterInfo.Name.ToString(), Value.ParameterValue);
                        for (const FVectorParameterValue& Value : Instance->VectorParameterValues)
                            Values->SetStringField(Value.ParameterInfo.Name.ToString(), Value.ParameterValue.ToString());
                        Row->SetObjectField(TEXT("values"), Values);
                        Previewed.Add(MakeShared<FJsonValueObject>(Row));
                    }
                TInlineComponentArray<UDirectionalLightComponent*> Suns(Actor);
                for (UDirectionalLightComponent* Sun : Suns)
                    if (Sun->LightFunctionMaterial)
                    {
                        auto Row = MakeShared<FJsonObject>();
                        Row->SetStringField(TEXT("component"), Sun->GetPathName());
                        Row->SetStringField(TEXT("light_function"), Sun->LightFunctionMaterial->GetPathName());
                        const UMaterial* Base = Sun->LightFunctionMaterial->GetMaterial();
                        Row->SetStringField(TEXT("light_function_master"), Base ? Base->GetPathName() : TEXT("none"));
                        Row->SetBoolField(TEXT("light_function_domain"), Base && Base->MaterialDomain == MD_LightFunction);
                        FString GraphError;
                        if (Base)
                            FFileHelper::SaveStringToFile(P4MaterialGraphJson(const_cast<UMaterial*>(Base), GraphError),
                                *(Folder / TEXT("light_function_graph.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                        Row->SetNumberField(TEXT("fade_distance"), Sun->LightFunctionFadeDistance);
                        Row->SetNumberField(TEXT("intensity"), Sun->Intensity);
                        Row->SetStringField(TEXT("rotation"), Sun->GetComponentRotation().ToString());
                        Previewed.Add(MakeShared<FJsonValueObject>(Row));
                    }
            }
    Report->SetArrayField(TEXT("previewed_materials"), Previewed);
    // The world's collection instances and their values.
    auto Collections = MakeShared<FJsonObject>();
    TSet<UMaterialParameterCollection*> Named;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                    if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), TEXT("EnvMPC")))
                        if (auto* Found = Cast<UMaterialParameterCollection>(Property->GetObjectPropertyValue_InContainer(Actor)))
                            Named.Add(Found);
    for (UMaterialParameterCollection* Collection : Named)
    {
        UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
        if (!Instance)
            continue;
        auto Values = MakeShared<FJsonObject>();
        for (const FCollectionScalarParameter& Parameter : Collection->ScalarParameters)
        {
            float Value = 0.f;
            Instance->GetScalarParameterValue(Parameter.ParameterName, Value);
            Values->SetNumberField(Parameter.ParameterName.ToString(), Value);
        }
        for (const FCollectionVectorParameter& Parameter : Collection->VectorParameters)
        {
            FLinearColor Value;
            Instance->GetVectorParameterValue(Parameter.ParameterName, Value);
            Values->SetStringField(Parameter.ParameterName.ToString(), Value.ToString());
        }
        Collections->SetObjectField(Collection->GetPathName(), Values);
    }
    Report->SetObjectField(TEXT("collections"), Collections);
    TestTrue(TEXT("The map draws something with the named masters"), Rows.Num() > 0);
    TestTrue(TEXT("The map as imported is captured"), P4SweepCapture::Capture(World, Folder / TEXT("base"), Report));
    struct FSaved
    {
        UMaterial* Master;
        FColorMaterialInput BaseColor;
        FColorMaterialInput Emissive;
        FScalarMaterialInput Specular;
        FScalarMaterialInput Opacity;
        TEnumAsByte<ETranslucencyLightingMode> Lighting;
        FVectorMaterialInput Normal;
        FScalarMaterialInput Roughness;
        FScalarMaterialInput Metallic;
    };
    TArray<FSaved> Saved;
    for (UMaterial* Master : Masters)
        Saved.Add({Master, Master->BaseColor, Master->EmissiveColor, Master->Specular, Master->Opacity,
            Master->TranslucencyLightingMode, Master->Normal, Master->Roughness, Master->Metallic});
    const auto Compile = [&]()
    {
        for (UMaterial* Master : Masters)
            Master->PostEditChange();
        if (GShaderCompilingManager)
            GShaderCompilingManager->FinishAllCompilation();
        FlushRenderingCommands();
    };
    const auto Restore = [&]()
    {
        for (const FSaved& Item : Saved)
        {
            Item.Master->BaseColor = Item.BaseColor;
            Item.Master->EmissiveColor = Item.Emissive;
            Item.Master->Specular = Item.Specular;
            Item.Master->Opacity = Item.Opacity;
            Item.Master->TranslucencyLightingMode = Item.Lighting;
            Item.Master->Normal = Item.Normal;
            Item.Master->Roughness = Item.Roughness;
            Item.Master->Metallic = Item.Metallic;
        }
        Compile();
    };
    ON_SCOPE_EXIT
    {
        Restore();
    };
    const auto Black = [](UMaterial* Master)
    {
        auto* Value = NewObject<UMaterialExpressionConstant3Vector>(Master);
        Value->Constant = FLinearColor::Black;
        return Value;
    };
    const auto Scalar = [](UMaterial* Master, float Number)
    {
        auto* Value = NewObject<UMaterialExpressionConstant>(Master);
        Value->R = Number;
        return Value;
    };
    struct FVariant
    {
        const TCHAR* Name;
        bool NoEmissive, NoBaseColor, NoSpecular, Red;
        int32 Lighting = -1;
        bool FlatNormal = false;
        int32 Constants = 0; // 1 base color, 2 roughness, 4 metallic, 8 specular, 16 opacity
        int32 Finite = 0;    // the same bits: that input drawn red where it is not finite, green where it is
    };
    TArray<FString> Only;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WATER_DIAGNOSTIC_VARIANTS")).ParseIntoArray(Only, TEXT(","));
    for (const FVariant& Variant : {FVariant{TEXT("lit-only"), true, false, false, false},
             FVariant{TEXT("emissive-only"), false, true, true, false},
             FVariant{TEXT("no-specular"), false, false, true, false}, FVariant{TEXT("red"), false, true, true, true},
             FVariant{TEXT("volumetric-directional"), false, false, false, false, TLM_VolumetricDirectional},
             FVariant{TEXT("surface"), false, false, false, false, TLM_Surface},
             FVariant{TEXT("flat-normal"), false, false, false, false, -1, true},
             FVariant{TEXT("constants"), true, false, false, false, -1, true, 31},
             FVariant{TEXT("const-base-color"), false, false, false, false, -1, false, 1},
             FVariant{TEXT("const-roughness"), false, false, false, false, -1, false, 2},
             FVariant{TEXT("const-metallic"), false, false, false, false, -1, false, 4},
             FVariant{TEXT("const-opacity"), false, false, false, false, -1, false, 16},
             FVariant{TEXT("finite-base-color"), false, true, true, false, -1, false, 0, 1},
             FVariant{TEXT("finite-roughness"), false, true, true, false, -1, false, 0, 2},
             FVariant{TEXT("finite-opacity"), false, true, true, false, -1, false, 0, 16}})
    {
        if (Only.Num() && !Only.Contains(TEXT("all")) && !Only.Contains(Variant.Name))
            continue;
        for (UMaterial* Master : Masters)
        {
            if (Variant.FlatNormal)
                Master->Normal.Expression = nullptr;
            if (Variant.Finite)
            {
                FExpressionInput Source = Variant.Finite == 1 ? FExpressionInput(Master->BaseColor)
                    : Variant.Finite == 2                    ? FExpressionInput(Master->Roughness)
                                                             : FExpressionInput(Master->Opacity);
                auto* Check = NewObject<UMaterialExpressionCustom>(Master);
                Check->OutputType = CMOT_Float3;
                // Red: not finite or below 0 in any channel; blue: above 1 in any channel; green: in [0, 1].
                Check->Code = Variant.Finite == 1
                    ? TEXT("float3 v = A; if (any(isnan(v)) || any(isinf(v)) || any(v < 0)) return float3(4, 0, 0); return any(v > 1) ? float3(0, 0, 4) : float3(0, 4, 0);")
                    : TEXT("float v = A; if (isnan(v) || isinf(v) || v < 0) return float3(4, 0, 0); return v > 1 ? float3(0, 0, 4) : float3(0, 4, 0);");
                Check->Inputs[0].InputName = TEXT("A");
                Check->Inputs[0].Input = Source;
                Master->EmissiveColor.Connect(0, Check);
                Master->Opacity.Connect(0, Scalar(Master, 1.f));
            }
            // Inputs replaced by plain constants: with all of them, what is left is the material's own settings.
            if (Variant.Constants & 1)
            {
                auto* Grey = NewObject<UMaterialExpressionConstant3Vector>(Master);
                Grey->Constant = FLinearColor(.5f, .5f, .5f);
                Master->BaseColor.Connect(0, Grey);
            }
            if (Variant.Constants & 2)
                Master->Roughness.Connect(0, Scalar(Master, .5f));
            if (Variant.Constants & 4)
                Master->Metallic.Connect(0, Scalar(Master, 0.f));
            if (Variant.Constants & 8)
                Master->Specular.Connect(0, Scalar(Master, .5f));
            if (Variant.Constants & 16)
                Master->Opacity.Connect(0, Scalar(Master, .9f));
            if (Variant.NoEmissive)
                Master->EmissiveColor.Expression = nullptr;
            if (Variant.Lighting >= 0)
                Master->TranslucencyLightingMode = ETranslucencyLightingMode(Variant.Lighting);
            if (Variant.Red)
            {
                // Where the surface draws: a bright, opaque emissive red.
                auto* Value = NewObject<UMaterialExpressionConstant3Vector>(Master);
                Value->Constant = FLinearColor(4.f, 0.f, 0.f);
                Master->EmissiveColor.Connect(0, Value);
                auto* One = NewObject<UMaterialExpressionConstant>(Master);
                One->R = 1.f;
                Master->Opacity.Connect(0, One);
            }
            if (Variant.NoBaseColor)
                Master->BaseColor.Connect(0, Black(Master));
            if (Variant.NoSpecular)
                Master->Specular.Connect(0, Scalar(Master, 0.f));
        }
        Compile();
        auto Views = MakeShared<FJsonObject>();
        TestTrue(FString::Printf(TEXT("The %s variant is captured"), Variant.Name),
            P4SweepCapture::Capture(World, Folder / Variant.Name, Views));
        Report->SetObjectField(Variant.Name, Views);
        Restore();
    }
    // Plain stock translucent slabs, one per lighting mode, over the first water surface of this map: whether the map's
    // own lighting draws a stock per-pixel surface.
    if (Only.Contains(TEXT("slabs")) && WaterBounds.Num())
    {
        const FBox Water = WaterBounds[0];
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        TArray<AActor*> Slabs;
        const ETranslucencyLightingMode Modes[] = {TLM_VolumetricDirectional, TLM_Surface, TLM_SurfacePerPixelLighting};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
            Material->BlendMode = BLEND_Translucent;
            Material->TranslucencyLightingMode = Modes[Index];
            auto* White = NewObject<UMaterialExpressionConstant3Vector>(Material);
            White->Constant = FLinearColor(.5f, .5f, .5f);
            Material->Expressions.Add(White);
            Material->BaseColor.Connect(0, White);
            Material->Opacity.Connect(0, Scalar(Material, .9f));
            Material->PostEditChange();
            // Along the line of sight of the entry view, which looks over open water.
            FVector At = FVector(Water.GetCenter().X - 600.f + Index * 600.f, Water.GetCenter().Y, Water.Max.Z + 30.f);
            const FString AtText = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WATER_DIAGNOSTIC_SLABS_AT"));
            FVector Base, Step;
            TArray<FString> Parts;
            if (AtText.ParseIntoArray(Parts, TEXT(";")) == 2 && Base.InitFromString(Parts[0]) && Step.InitFromString(Parts[1]))
                At = Base + Step * Index;
            AStaticMeshActor* Slab = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
            Slab->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Slab->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Slab->SetActorScale3D(FVector(1.2f, 1.2f, .1f));
            Slab->GetStaticMeshComponent()->SetMaterial(0, Material);
            Slabs.Add(Slab);
        }
        Compile();
        auto Views = MakeShared<FJsonObject>();
        Views->SetStringField(TEXT("slab_order"), TEXT("-X to +X: volumetric directional, surface, surface per-pixel"));
        TestTrue(TEXT("The stock slabs are captured"), P4SweepCapture::Capture(World, Folder / TEXT("slabs"), Views));
        Report->SetObjectField(TEXT("slabs"), Views);
        for (AActor* Slab : Slabs)
            World->DestroyActor(Slab);
    }
    // The game's scene color copy read applied to a copy that holds plain linear color, as if the game's copy were not
    // encoded: every Custom read of SceneColorCopyTexture decodes the editor's encoded copy first.
    if (Only.Contains(TEXT("raw-copy")))
    {
        TArray<TPair<FString*, FString>> Codes;
        for (UMaterial* Master : Masters)
            for (UMaterialExpression* Expression : Master->Expressions)
            {
                FString* Code = nullptr;
                if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
                    Code = &Custom->Code;
                else if (auto* Game = Cast<UMaterialExpressionKadanGameCustom>(Expression))
                    Code = &Game->Code;
                // pow(sample.rgb, 4.0) * 10.0 becomes pow(pow(sample.rgb, 4.0) * 10.0, 4.0) * 10.0.
                const FString Decode = TEXT(".rgb, 4.0) * 10.0");
                if (Code && Code->Contains(TEXT("SceneColorCopyTexture")) && Code->Contains(Decode))
                {
                    Codes.Emplace(Code, *Code);
                    Code->ReplaceInline(TEXT("pow(Texture2DSample(SceneColorCopyTexture"), TEXT("pow(pow(Texture2DSample(SceneColorCopyTexture"));
                    Code->ReplaceInline(*Decode, TEXT(".rgb, 4.0) * 10.0, 4.0) * 10.0"));
                }
            }
        Compile();
        auto Views = MakeShared<FJsonObject>();
        Views->SetNumberField(TEXT("changed_reads"), Codes.Num());
        TArray<TSharedPtr<FJsonValue>> Reads;
        for (UMaterial* Master : Masters)
            for (UMaterialExpression* Expression : Master->Expressions)
            {
                const FString* Code = Cast<UMaterialExpressionCustom>(Expression) ? &Cast<UMaterialExpressionCustom>(Expression)->Code
                    : Cast<UMaterialExpressionKadanGameCustom>(Expression) ? &Cast<UMaterialExpressionKadanGameCustom>(Expression)->Code
                                                                           : nullptr;
                if (Code && Code->Contains(TEXT("SceneColorCopyTexture")))
                    Reads.Add(MakeShared<FJsonValueString>(Expression->GetClass()->GetName() + TEXT(": ") + *Code));
            }
        Views->SetArrayField(TEXT("scene_color_reads"), Reads);
        TestTrue(TEXT("The raw-copy variant is captured"), P4SweepCapture::Capture(World, Folder / TEXT("raw-copy"), Views));
        Report->SetObjectField(TEXT("raw-copy"), Views);
        for (auto& Item : Codes)
            *Item.Key = Item.Value;
        Compile();
    }
    // The map's sky light and reflection captures captured again before the pictures.
    if (Only.Contains(TEXT("reflections")))
    {
        USkyLightComponent::UpdateSkyCaptureContents(World);
        UReflectionCaptureComponent::UpdateReflectionCaptureContents(World);
        FlushRenderingCommands();
        auto Views = MakeShared<FJsonObject>();
        TestTrue(TEXT("The reflections variant is captured"), P4SweepCapture::Capture(World, Folder / TEXT("reflections"), Views));
        Report->SetObjectField(TEXT("reflections"), Views);
    }
    // The water's absorption drawn as color: red the height fog amount, green the distance fog amount (the alphas of
    // the emissive's two fog interpolations, outer distance, inner height), opaque, nothing else lit.
    if (Only.Contains(TEXT("fog-amounts")))
    {
        int32 Found = 0;
        for (UMaterial* Master : Masters)
        {
            auto* Outer = Cast<UMaterialExpressionLinearInterpolate>(Master->EmissiveColor.Expression);
            auto* Inner = Outer ? Cast<UMaterialExpressionLinearInterpolate>(Outer->A.Expression) : nullptr;
            if (!Inner)
                continue;
            ++Found;
            auto* Show = NewObject<UMaterialExpressionCustom>(Master);
            Show->OutputType = CMOT_Float3;
            Show->Code = TEXT("return float3(saturate(H), saturate(D), 0) * 2;");
            Show->Inputs[0].InputName = TEXT("H");
            Show->Inputs[0].Input = Inner->Alpha;
            FCustomInput Distance;
            Distance.InputName = TEXT("D");
            Distance.Input = Outer->Alpha;
            Show->Inputs.Add(Distance);
            Master->EmissiveColor.Connect(0, Show);
            Master->BaseColor.Connect(0, Black(Master));
            Master->Specular.Connect(0, Scalar(Master, 0.f));
            Master->Opacity.Connect(0, Scalar(Master, 1.f));
        }
        Compile();
        auto Views = MakeShared<FJsonObject>();
        Views->SetNumberField(TEXT("masters_with_fog"), Found);
        TestTrue(TEXT("The fog-amounts variant is captured"), P4SweepCapture::Capture(World, Folder / TEXT("fog-amounts"), Views));
        Report->SetObjectField(TEXT("fog-amounts"), Views);
        Restore();
    }
    // The values the water's absorption is made of, as color bands: the thickness its distance fog measures (scene depth
    // behind minus the surface's depth: blue < 25, cyan < 50, green < 100, yellow < 200, orange < 400 cm, red beyond),
    // and the height fog amount (the same colors for < 0.1, 0.25, 0.5, 0.75, 0.95, then 1). Each node on the way is
    // checked by class; the report keeps the walk.
    if (Only.Contains(TEXT("absorption-values")))
    {
        TArray<TSharedPtr<FJsonValue>> Walks;
        const auto Follow = [&](UMaterialExpression* From, const TArray<int32>& Path, FExpressionInput& Out)
        {
            FString Walk = From ? From->GetClass()->GetName() : TEXT("null");
            UMaterialExpression* At = From;
            for (int32 Step = 0; Step < Path.Num() && At; ++Step)
            {
                TArray<FExpressionInput*> Inputs = At->GetInputs();
                if (!Inputs.IsValidIndex(Path[Step]) || !Inputs[Path[Step]]->Expression)
                {
                    At = nullptr;
                    break;
                }
                if (Step + 1 == Path.Num())
                    Out = *Inputs[Path[Step]];
                At = Inputs[Path[Step]]->Expression;
                Walk += FString::Printf(TEXT(" -%d-> %s"), Path[Step], *At->GetClass()->GetName());
            }
            Walks.Add(MakeShared<FJsonValueString>(Walk));
            return At != nullptr;
        };
        const FString Bands = TEXT("float3 c = v < B.x ? float3(0,0,4) : v < B.y ? float3(0,4,4) : v < B.z ? float3(0,4,0) : ")
                              TEXT("v < B.w ? float3(4,4,0) : v < E ? float3(4,1.5,0) : float3(4,0,0); return c;");
        for (UMaterial* Master : Masters)
        {
            auto* Outer = Cast<UMaterialExpressionLinearInterpolate>(Master->EmissiveColor.Expression);
            FExpressionInput Thickness, Height;
            // Outer lerp alpha (2) -> Multiply A (0) -> Saturate (0) -> Divide A (0) -> Subtract A (0): the thickness.
            const bool HasThickness = Follow(Outer, {2, 0, 0, 0, 0}, Thickness);
            // Outer lerp A (0) -> inner lerp alpha (2) -> Multiply B (1): the saturated height fog amount.
            const bool HasHeight = Follow(Outer, {0, 2, 1}, Height);
            for (int32 Which = 0; Which < 2; ++Which)
            {
                if (!(Which ? HasHeight : HasThickness))
                    continue;
                auto* Show = NewObject<UMaterialExpressionCustom>(Master);
                Show->OutputType = CMOT_Float3;
                Show->Code = Which ? TEXT("float4 B = float4(0.1, 0.25, 0.5, 0.75); float E = 0.95; float v = V;") + Bands
                                   : TEXT("float4 B = float4(25, 50, 100, 200); float E = 400; float v = V;") + Bands;
                Show->Inputs[0].InputName = TEXT("V");
                Show->Inputs[0].Input = Which ? Height : Thickness;
                Master->EmissiveColor.Connect(0, Show);
                Master->BaseColor.Connect(0, Black(Master));
                Master->Specular.Connect(0, Scalar(Master, 0.f));
                Master->Opacity.Connect(0, Scalar(Master, 1.f));
                Compile();
                auto Views = MakeShared<FJsonObject>();
                const FString Name = Which ? TEXT("height-fog-amount") : TEXT("thickness");
                TestTrue(TEXT("An absorption value is captured"), P4SweepCapture::Capture(World, Folder / Name, Views));
                Report->SetObjectField(Name, Views);
                Restore();
            }
        }
        Report->SetArrayField(TEXT("absorption_walks"), Walks);
    }
    // The water actors' decal components given the material the actor's class holds for them, as the game's native
    // construction does (CausticsDecal <- CausticsMaterial on the game's water box), then captured.
    if (Only.Contains(TEXT("caustics")))
    {
        TArray<TPair<UDecalComponent*, UMaterialInterface*>> Changed;
        for (ULevel* Level : World->GetLevels())
            if (Level && Level->bIsVisible)
                for (AActor* Actor : Level->Actors)
                {
                    if (!IsValid(Actor))
                        continue;
                    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), TEXT("CausticsMaterial"));
                    UMaterialInterface* Material = Property
                        ? Cast<UMaterialInterface>(Property->GetObjectPropertyValue_InContainer(Actor))
                        : nullptr;
                    TInlineComponentArray<UDecalComponent*> Decals(Actor);
                    for (UDecalComponent* Decal : Decals)
                        if (Material && Decal->GetName() == TEXT("CausticsDecal"))
                        {
                            Changed.Emplace(Decal, Decal->GetDecalMaterial());
                            Decal->SetDecalMaterial(Material);
                        }
                }
        Compile();
        auto Views = MakeShared<FJsonObject>();
        Views->SetNumberField(TEXT("decals_given_materials"), Changed.Num());
        TestTrue(TEXT("The caustics variant is captured"), P4SweepCapture::Capture(World, Folder / TEXT("caustics"), Views));
        Report->SetObjectField(TEXT("caustics"), Views);
        for (auto& Item : Changed)
            Item.Key->SetDecalMaterial(Item.Value);
    }
    // The map's own lights and shadows instead of the preview lights, with its reflections captured again.
    if (Only.Contains(TEXT("map-lights")))
    {
        USkyLightComponent::UpdateSkyCaptureContents(World);
        UReflectionCaptureComponent::UpdateReflectionCaptureContents(World);
        FPlatformMisc::SetEnvironmentVar(TEXT("P4_NATIVE_SWEEP_MAP_LIGHTS"), TEXT("1"));
        auto Views = MakeShared<FJsonObject>();
        TestTrue(TEXT("The map-lights variant is captured"), P4SweepCapture::Capture(World, Folder / TEXT("map-lights"), Views));
        FPlatformMisc::SetEnvironmentVar(TEXT("P4_NATIVE_SWEEP_MAP_LIGHTS"), TEXT(""));
        Report->SetObjectField(TEXT("map-lights"), Views);
    }
    // Each named expression class replaced by a constant wherever the masters read it (scene depth far away, pixel
    // depth near, vectors straight up), one class per capture.
    TArray<FString> Stubs;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WATER_DIAGNOSTIC_STUBS")).ParseIntoArray(Stubs, TEXT(","));
    for (const FString& Stub : Stubs)
    {
        TArray<TPair<FExpressionInput*, FExpressionInput>> Changed;
        for (UMaterial* Master : Masters)
        {
            UMaterialExpression* Value = nullptr;
            if (Stub == TEXT("MaterialExpressionSceneDepth") || Stub == TEXT("MaterialExpressionPixelDepth"))
                Value = Scalar(Master, Stub == TEXT("MaterialExpressionSceneDepth") ? 100000.f : 1000.f);
            else
            {
                auto* Up = NewObject<UMaterialExpressionConstant3Vector>(Master);
                Up->Constant = FLinearColor(0, 0, 1);
                Value = Up;
            }
            const auto Swap = [&](FExpressionInput* Input)
            {
                if (Input && Input->Expression && Input->Expression->GetClass()->GetName() == Stub)
                {
                    Changed.Emplace(Input, *Input);
                    Input->Expression = Value;
                    Input->OutputIndex = 0;
                    Input->Mask = 0;
                }
            };
            for (UMaterialExpression* Expression : Master->Expressions)
                if (Expression)
                    for (FExpressionInput* Input : Expression->GetInputs())
                        Swap(Input);
            for (int32 Property = 0; Property < MP_MAX; ++Property)
                Swap(Master->GetExpressionInputForProperty(EMaterialProperty(Property)));
        }
        Compile();
        auto Views = MakeShared<FJsonObject>();
        Views->SetNumberField(TEXT("replaced_inputs"), Changed.Num());
        TestTrue(TEXT("The stub variant is captured"), P4SweepCapture::Capture(World, Folder / Stub, Views));
        Report->SetObjectField(Stub, Views);
        for (auto& Item : Changed)
            *Item.Key = Item.Value;
        Compile();
    }
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("The diagnostic report is kept"),
        FFileHelper::SaveStringToFile(
            Json, *(Folder / TEXT("diagnostic.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}
// The stock control: a blank map with plain white translucent slabs, one for each translucent lighting mode, side by
// side over an opaque floor, captured as the sweep captures a map.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TranslucentLightingControl, "P4MapKit.Editor.TranslucentLightingControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TranslucentLightingControl::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WATER_DIAGNOSTIC")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Name a map for the water diagnostic."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Floor->SetActorScale3D(FVector(40, 40, 1));
    const ETranslucencyLightingMode Modes[] = {TLM_VolumetricNonDirectional, TLM_VolumetricDirectional,
        TLM_Surface, TLM_SurfacePerPixelLighting};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
        Material->BlendMode = BLEND_Translucent;
        Material->TranslucencyLightingMode = Modes[Index];
        auto* White = NewObject<UMaterialExpressionConstant3Vector>(Material);
        White->Constant = FLinearColor(.5f, .5f, .5f);
        auto* Opacity = NewObject<UMaterialExpressionConstant>(Material);
        Opacity->R = .9f;
        Material->Expressions.Add(White);
        Material->Expressions.Add(Opacity);
        Material->BaseColor.Connect(0, White);
        Material->Opacity.Connect(0, Opacity);
        Material->PostEditChange();
        AStaticMeshActor* Slab = World->SpawnActor<AStaticMeshActor>(FVector(-1200.f + Index * 800.f, 0, 200), FRotator::ZeroRotator);
        Slab->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Slab->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Slab->SetActorScale3D(FVector(6, 6, .2f));
        Slab->GetStaticMeshComponent()->SetMaterial(0, Material);
    }
    World->GetOutermost()->SetDirtyFlag(false);
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
    auto Report = MakeShared<FJsonObject>();
    TestTrue(TEXT("The control is captured"),
        P4SweepCapture::Capture(World, FPaths::ProjectSavedDir() / TEXT("P4MapKit/WaterDiagnostic/control"), Report));
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}
#endif

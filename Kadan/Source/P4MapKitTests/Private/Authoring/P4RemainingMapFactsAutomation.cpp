#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4KadanBlueprintPlans.h"
#include "P4LevelChanges.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace
{
TSharedRef<FJsonObject> ResourceFacts(UMaterialInterface* Material)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("path"), GetPathNameSafe(Material));
    auto* Resource = Material ? Material->GetMaterialResource(GMaxRHIFeatureLevel) : nullptr;
    Row->SetBoolField(TEXT("resource"), Resource != nullptr);
    Row->SetBoolField(TEXT("shader_map"), Resource && Resource->HasValidGameThreadShaderMap());
    if (const auto* Instance = Cast<UMaterialInstance>(Material))
    {
        Row->SetBoolField(TEXT("static_permutation"), Instance->bHasStaticPermutationResource);
        Row->SetStringField(TEXT("parent"), GetPathNameSafe(Instance->Parent));
    }
    return Row;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RemainingMapFacts, "P4MapKit.Editor.RemainingMapFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RemainingMapFacts::RunTest(const FString&)
{
    const FString Gallery = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REMAINING_MAP_FACTS"));
    if (Gallery.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Select the remaining map evidence folder."));
        return true;
    }
    auto Report = MakeShared<FJsonObject>();
    const TArray<FString> ExtraWorlds = {
        TEXT("/Game/Carrot4/Maps/Ending/Ending_Area500/Sublevels/Ending_Area500_Objects"),
        TEXT("/Game/Carrot4/Maps/Main/Area/Area500/Sublevels/Area500_Objects")};
    auto ExtraChanges = MakeShared<FJsonObject>();
    for (const FString& Package : ExtraWorlds)
    {
        UWorld* World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
        if (World)
            ExtraChanges->SetStringField(Package, P4LevelChangesJson(P4AnalyzeOriginalLevelChanges(World)));
    }
    Report->SetObjectField(TEXT("extra_world_changes"), ExtraChanges);
    UWorld* Hero = UEditorLoadingAndSavingUtils::LoadMap(
        TEXT("/Game/Carrot4/Maps/Main/Area/Area500/Sublevels/Area500_Hero_Objects"));
    if (!TestNotNull(TEXT("The saved inherited-component fixture opens cold"), Hero))
        return false;
    Report->SetStringField(TEXT("hero_changes"), P4LevelChangesJson(P4AnalyzeOriginalLevelChanges(Hero)));
    TArray<TSharedPtr<FJsonValue>> Components;
    for (AActor* Actor : Hero->PersistentLevel->Actors)
    {
        if (!IsValid(Actor) || Actor->GetName() != TEXT("GMultiBlockArea500_01A_2"))
            continue;
        auto ActorRow = MakeShared<FJsonObject>();
        ActorRow->SetStringField(TEXT("actor"), Actor->GetPathName());
        auto* Blueprint = Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy);
        TArray<TSharedPtr<FJsonValue>> Templates;
        for (UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
        {
            auto* Generated = Cast<UBlueprintGeneratedClass>(Class);
            if (!Generated)
                continue;
            auto TemplateRow = MakeShared<FJsonObject>();
            TemplateRow->SetStringField(TEXT("class"), Class->GetPathName());
            if (auto* CDO = Cast<AActor>(Class->GetDefaultObject()))
                TemplateRow->SetStringField(TEXT("cdo_root"), GetPathNameSafe(CDO->GetRootComponent()));
            TArray<TSharedPtr<FJsonValue>> Overrides;
            if (auto* Handler = Generated->GetInheritableComponentHandler(false))
                for (auto It = Handler->CreateRecordIterator(); It; ++It)
                {
                    auto Override = MakeShared<FJsonObject>();
                    Override->SetStringField(TEXT("variable"), It->ComponentKey.GetSCSVariableName().ToString());
                    Override->SetStringField(TEXT("template"), GetPathNameSafe(It->ComponentTemplate));
                    if (auto* Scene = Cast<USceneComponent>(It->ComponentTemplate))
                        Override->SetNumberField(TEXT("mobility"), int32(Scene->Mobility));
                    Overrides.Add(MakeShared<FJsonValueObject>(Override));
                }
            TemplateRow->SetArrayField(TEXT("overrides"), Overrides);
            Templates.Add(MakeShared<FJsonValueObject>(TemplateRow));
        }
        ActorRow->SetArrayField(TEXT("templates"), Templates);
        ActorRow->SetStringField(TEXT("source_templates"),
            Blueprint ? Blueprint->GetOutermost()
                            ->GetMetaData()
                            ->GetValue(Blueprint, TEXT("P4OriginalInheritedTemplates"))
                            .Left(32000)
                      : FString());
        TArray<TSharedPtr<FJsonValue>> Parts;
        TInlineComponentArray<UActorComponent*> Owned(Actor);
        for (UActorComponent* Part : Owned)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("component"), Part->GetPathName());
            Row->SetStringField(TEXT("archetype"), GetPathNameSafe(Part->GetArchetype()));
            if (auto* Scene = Cast<USceneComponent>(Part))
                Row->SetNumberField(TEXT("mobility"), int32(Scene->Mobility));
            Parts.Add(MakeShared<FJsonValueObject>(Row));
        }
        ActorRow->SetArrayField(TEXT("components"), Parts);
        Components.Add(MakeShared<FJsonValueObject>(ActorRow));
    }
    Report->SetArrayField(TEXT("hero_components"), Components);
    UWorld* Water = UEditorLoadingAndSavingUtils::LoadMap(
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave033/Cave033_F00/Cave033_F00_P"));
    if (!TestNotNull(TEXT("The saved water fixture opens cold"), Water))
        return false;
    TArray<TSharedPtr<FJsonValue>> WaterParts;
    for (ULevel* Level : Water->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
            {
                if (!IsValid(Actor) || Actor->GetName() != TEXT("BP_WaterBox2_0"))
                    continue;
                TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
                for (UStaticMeshComponent* Part : Parts)
                {
                    auto Row = MakeShared<FJsonObject>();
                    Row->SetStringField(TEXT("component"), Part->GetPathName());
                    Row->SetStringField(TEXT("scale"), Part->GetComponentScale().ToString());
                    Row->SetStringField(TEXT("world_extent"), Part->Bounds.BoxExtent.ToString());
                    Row->SetStringField(TEXT("mesh_extent"),
                        Part->GetStaticMesh() ? Part->GetStaticMesh()->GetBounds().BoxExtent.ToString() : FString());
                    TArray<TSharedPtr<FJsonValue>> Materials;
                    for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
                        Materials.Add(MakeShared<FJsonValueObject>(ResourceFacts(Part->GetMaterial(Slot))));
                    Row->SetArrayField(TEXT("materials"), Materials);
                    WaterParts.Add(MakeShared<FJsonValueObject>(Row));
                }
            }
    Report->SetArrayField(TEXT("water_components"), WaterParts);
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    FString Text;
    TSharedPtr<FJsonObject> Area;
    TArray<TSharedPtr<FJsonValue>> Resources;
    if (FFileHelper::LoadFileToString(Text, *(Gallery / TEXT("Area003_P_day.row.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Area) && Area.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
        if (Area->TryGetArrayField(TEXT("capture_materials_without_shaders"), Paths))
            for (const auto& Path : *Paths)
            {
                auto* Material = LoadObject<UMaterialInstanceConstant>(nullptr, *Path->AsString());
                auto Row = ResourceFacts(Material);
                if (Material)
                {
                    Row->SetObjectField(TEXT("parent_resource"), ResourceFacts(Material->Parent));
                    auto* Copy = DuplicateObject<UMaterialInstanceConstant>(Material, GetTransientPackage());
                    Copy->ClearFlags(RF_Public | RF_Standalone);
                    Copy->SetFlags(RF_Transient);
                    UMaterialEditingLibrary::UpdateMaterialInstance(Copy);
                    if (GShaderCompilingManager)
                        GShaderCompilingManager->FinishAllCompilation();
                    Row->SetObjectField(TEXT("updated_transient_copy"), ResourceFacts(Copy));
                    Copy->MarkPendingKill();
                }
                Resources.Add(MakeShared<FJsonValueObject>(Row));
            }
    }
    Report->SetArrayField(TEXT("material_resources"), Resources);
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTIAL_REPAK"));
    const TArray<FString> Roots = {TEXT("/Game/Carrot4/Core/Actor/GameFramework"),
        TEXT("/Game/Carrot4/Core/GameMode/BP_GameModeWithGameFramework")};
    const auto Plan = P4KadanPlanImport(Settings, TSet<FString>(Roots), false);
    FP4KadanBlueprintPlans Blueprints;
    FP4CurrentGameRegistryPtr Reader;
    FString Error;
    const bool Planned = P4KadanPlanBlueprints(Plan.Assets, Roots,
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/RemainingBlueprintPlan"), Blueprints, Error, Settings, Reader);
    Report->SetBoolField(TEXT("framework_planned"), Planned);
    Report->SetStringField(TEXT("framework_error"), Error);
    Report->SetStringField(TEXT("framework_modules"), FString::Join(Blueprints.Modules, TEXT(",")));
    Report->SetStringField(TEXT("framework_unloaded_modules"), FString::Join(Blueprints.ModulesNotLoaded, TEXT(",")));
    auto Refused = MakeShared<FJsonObject>();
    for (const auto& Pair : Blueprints.Refused)
        Refused->SetStringField(Pair.Key, Pair.Value);
    Report->SetObjectField(TEXT("framework_refused"), Refused);
    Text.Reset();
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
    return TestTrue(TEXT("Actual remaining-source diagnostics are retained"),
        FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("P4MapKit/RemainingMapFacts.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
#endif

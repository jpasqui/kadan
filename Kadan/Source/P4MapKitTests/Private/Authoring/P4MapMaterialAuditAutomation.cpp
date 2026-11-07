#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "RHI.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "P4OriginalRecord.h"

// Which materials of a saved map the editor cannot draw as themselves: a game material kept from the game's
// file (no graph, so the editor has no shaders for it), a graph whose shaders do not compile, or a material
// with no shader map. A diagnostic for people to read, not a check: run it by name with P4_NATIVE_SCREEN_MAP
// set. The map is opened as it is saved; nothing is imported or saved. Rows go to
// Saved/P4MapKit/MapMaterialAudit/<map>.json, one per material, with the components that draw it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MapMaterialAudit, "P4MapKit.Editor.MapMaterialAudit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MapMaterialAudit::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SCREEN_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Set P4_NATIVE_SCREEN_MAP to a saved map to audit its materials"));
        return true;
    }
    if (!GEditor || GUsingNullRHI)
    {
        AddError(TEXT("The material audit needs D3D: it reads the editor's compiled shaders"));
        return false;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!World)
    {
        AddError(TEXT("The saved map cannot be opened: ") + Map);
        return false;
    }
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    struct FUse
    {
        int32 Components = 0;
        TArray<FString> Samples;
        FBox Bounds = FBox(ForceInit);
    };
    TMap<UMaterialInterface*, FUse> Uses;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor) && !Actor->IsHiddenEd())
                {
                    TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
                    for (UPrimitiveComponent* Part : Parts)
                    {
                        if (!Part || !Part->IsRegistered() || !Part->IsVisible())
                            continue;
                        TArray<UMaterialInterface*> Used;
                        Part->GetUsedMaterials(Used);
                        for (UMaterialInterface* Material : Used)
                            if (Material)
                            {
                                FUse& Use = Uses.FindOrAdd(Material);
                                ++Use.Components;
                                if (Use.Samples.Num() < 4)
                                    Use.Samples.Add(Actor->GetName() + TEXT(".") + Part->GetName());
                                if (FMath::IsFinite(Part->Bounds.SphereRadius) && Part->Bounds.SphereRadius < 1e6f)
                                    Use.Bounds += Part->Bounds.GetBox();
                            }
                    }
                }
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Problems = 0;
    for (auto& Pair : Uses)
    {
        UMaterialInterface* Material = Pair.Key;
        UMaterial* Base = Material->GetMaterial();
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("material"), Material->GetPathName());
        Row->SetStringField(TEXT("base"), Base ? Base->GetPathName() : FString());
        Row->SetNumberField(TEXT("components"), Pair.Value.Components);
        TArray<TSharedPtr<FJsonValue>> Samples;
        for (const FString& Sample : Pair.Value.Samples)
            Samples.Add(MakeShared<FJsonValueString>(Sample));
        Row->SetArrayField(TEXT("samples"), Samples);
        if (Pair.Value.Bounds.IsValid)
            Row->SetStringField(TEXT("bounds_center"), Pair.Value.Bounds.GetCenter().ToString());
        FString Problem;
        if (!Base)
            Problem = TEXT("no base material");
        else
        {
            Row->SetNumberField(TEXT("base_expressions"), Base->Expressions.Num());
            FP4OriginalRecord Record;
            FString Error;
            if (P4ReadOriginalRecord(Base, Record, Error))
            {
                Row->SetStringField(TEXT("base_fidelity"), Record.Fidelity);
                Row->SetBoolField(TEXT("base_reference_only"), Record.ReferenceOnly);
            }
            if (Base->GetPathName().StartsWith(TEXT("/Engine/")))
            {
            }
            else if (Base->Expressions.Num() == 0 && Base->GetPathName().StartsWith(TEXT("/Game/Carrot4/")))
                Problem = TEXT("the game's material kept from its file: the editor has no graph to draw it");
            if (Problem.IsEmpty())
            {
                const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
                if (!Resource)
                    Problem = TEXT("no material resource");
                else if (Resource->GetCompileErrors().Num())
                    Problem = TEXT("compile error: ") + Resource->GetCompileErrors()[0];
                else if (!Resource->HasValidGameThreadShaderMap())
                    Problem = TEXT("no shader map");
            }
        }
        Row->SetStringField(TEXT("problem"), Problem);
        if (!Problem.IsEmpty())
            ++Problems;
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("map"), Map);
    Report->SetArrayField(TEXT("materials"), Rows);
    Report->SetNumberField(TEXT("materials_drawn"), Uses.Num());
    Report->SetNumberField(TEXT("materials_not_drawn_as_themselves"), Problems);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MapMaterialAudit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json, *(Folder / (FPackageName::GetShortName(Map) + TEXT(".json"))),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(
        FString::Printf(TEXT("%s draws %d materials; %d are not drawn as themselves."), *Map, Uses.Num(), Problems));
    return true;
}
#endif

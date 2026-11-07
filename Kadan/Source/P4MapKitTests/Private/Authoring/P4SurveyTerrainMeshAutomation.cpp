#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "P4AuthoringRows.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SurveyTerrainMeshes, "P4MapKit.Kadan.SurveyTerrainMeshes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SurveyTerrainMeshes::RunTest(const FString&)
{
    const FString Input = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_SURVEY_TERRAIN_MESHES"));
    if (Input.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Set P4_SURVEY_TERRAIN_MESHES to the retained terrain-source request."));
        return true;
    }
    FString Text;
    TArray<TSharedPtr<FJsonValue>> Sources, Results;
    if (!TestTrue(TEXT("Read retained mesh source request"),
            FFileHelper::LoadFileToString(Text, *Input) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Sources)))
        return false;
    for (const auto& Value : Sources)
    {
        const FString Header = Value->AsString();
        TSharedPtr<FJsonObject> Build;
        const FString Reply = UP4AuthoringTools::NativeBuildOriginalMesh(
            Header, FPaths::ChangeExtension(Header, TEXT("uexp")), FString());
        if (!TestTrue(TEXT("Native build comparison returns JSON"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Reply), Build) && Build.IsValid()))
            continue;
        Build->SetStringField(TEXT("header"), Header);
        Results.Add(MakeShared<FJsonValueObject>(Build));
        TestTrue(TEXT("Terrain reconstructs without losing source data: ") + FPaths::GetBaseFilename(Header),
            Build->GetBoolField(TEXT("valid")) &&
                (Build->GetBoolField(TEXT("equal")) || Build->GetBoolField(TEXT("equal_within_engine_weld")) ||
                    Build->GetBoolField(TEXT("equal_within_engine_tangents"))));
    }
    FJsonSerializer::Serialize(Results, TJsonWriterFactory<>::Create(&Text));
    const FString Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/TerrainMeshRegression.json");
    TestTrue(TEXT("Keep native terrain build comparisons"),
        FFileHelper::SaveStringToFile(Text, *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}
#endif

#include "P4OriginalPositiveInfinityOperation.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4MaterialGraph.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalPositiveInfinityGraphs, "P4MapKit.Material.OriginalPositiveInfinityGraphs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalPositiveInfinityGraphs::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const FString Fixture = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MATERIAL_POSITIVE_INF_GRAPHS"));
    if (Fixture.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "OriginalPositiveInfinityGraphs was not exercised. Set P4_MATERIAL_POSITIVE_INF_GRAPHS to a file of the four graphs; P4MapKit.Kadan.MaterialRecoveryHeld recovers them from the game files."));
        return true;
    }
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (Fixture.IsEmpty() || !FFileHelper::LoadFileToString(Text, *Fixture) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
    {
        AddError(
            TEXT("The four original finite graph fixtures must be supplied through P4_MATERIAL_POSITIVE_INF_GRAPHS"));
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Root->TryGetArrayField(TEXT("graphs"), Records) || Records->Num() != 4)
    {
        AddError(TEXT("The fixture must contain all four complete original graphs"));
        return false;
    }
    const TSet<FString> Wanted = {TEXT("/Game/Carrot4/MasterMaterial/UI/M_ui_CircleGauge_Translucent_00_Master"),
        TEXT("/Game/Carrot4/MasterMaterial/UI/M_ui_CircleGauge_Translucent_01_Master"),
        TEXT("/Game/Carrot4/MasterMaterial/UI/M_ui_Countdown_Translucent_00_Master"),
        TEXT("/Game/Carrot4/MasterMaterial/UI/M_ui_LifeGauge_Translucent_00_Master")};
    TSet<FString> Seen;
    const FString Prefix = TEXT("/Game/P4PositiveInfinityTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    for (int32 Index = 0; Index < Records->Num(); ++Index)
    {
        const auto Record = (*Records)[Index]->AsObject();
        FString Package;
        const TSharedPtr<FJsonObject>* Graph = nullptr;
        if (!Record.IsValid() || !Record->TryGetStringField(TEXT("package"), Package) || !Wanted.Contains(Package) ||
            Seen.Contains(Package) || !Record->TryGetObjectField(TEXT("graph"), Graph))
        {
            AddError(TEXT("Original graph fixture identity is invalid or repeated"));
            return false;
        }
        Seen.Add(Package);
        FString Json, Error;
        FJsonSerializer::Serialize(Graph->ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        UMaterial* Material = P4BuildMaterialFromGraph(Json, Prefix + FString::FromInt(Index), Error);
        if (!TestNotNull(*Package, Material))
        {
            AddError(Error);
            continue;
        }
        TestTrue(TEXT("Original UI family keeps its complete native settings"),
            Material->MaterialDomain == MD_UI && Material->BlendMode == BLEND_Translucent &&
                Material->GetShadingModels().HasShadingModel(MSM_Unlit));
        int32 Choices = 0;
        for (UMaterialExpression* Expression : Material->Expressions)
        {
            auto* Choice = Cast<UMaterialExpressionCustom>(Expression);
            if (!Choice || Choice->Description != P4OriginalPositiveInfinityOperation::Description())
                continue;
            ++Choices;
            TestTrue(TEXT("Actual complete graph has exact finite uint predicate and scalar branch contract"),
                P4OriginalPositiveInfinityOperation::Matches(Choice, Error));
            const FString OriginalCode = Choice->Code;
            Choice->Code.ReplaceInline(TEXT("0x7f800000u"), TEXT("0xff800000u"));
            TestFalse(TEXT("Negative infinity word changes the predicate"),
                P4OriginalPositiveInfinityOperation::Matches(Choice, Error));
            Choice->Code = OriginalCode;
            Choice->OutputType = CMOT_Float4;
            TestFalse(TEXT("Vector output cannot replace the scalar choice"),
                P4OriginalPositiveInfinityOperation::Matches(Choice, Error));
            Choice->OutputType = CMOT_Float1;
            const FName OriginalName = Choice->Inputs[0].InputName;
            Choice->Inputs[0].InputName = FName(TEXT("Equal"));
            TestFalse(TEXT("Value and branch input names cannot be exchanged"),
                P4OriginalPositiveInfinityOperation::Matches(Choice, Error));
            Choice->Inputs[0].InputName = OriginalName;
            const FExpressionInput OriginalBranch = Choice->Inputs[1].Input;
            auto* Vector = NewObject<UMaterialExpressionConstant3Vector>(Material);
            Choice->Inputs[1].Input.Expression = Vector;
            Choice->Inputs[1].Input.OutputIndex = 0;
            Choice->Inputs[1].Input.Mask = 0;
            TestFalse(TEXT("A vector branch cannot silently truncate into the scalar choice"),
                P4OriginalPositiveInfinityOperation::Matches(Choice, Error));
            Choice->Inputs[1].Input = OriginalBranch;
            TestTrue(TEXT("Negative controls restore the exact original graph operation"),
                P4OriginalPositiveInfinityOperation::Matches(Choice, Error));
        }
        TestEqual(TEXT("Both original strict equality choices survive native construction"), Choices, 2);
        const FString Readback = P4MaterialGraphJson(Material, Error);
        TestFalse(TEXT("Actual editor graph readback succeeds"), Readback.IsEmpty());
        TestTrue(TEXT("Independent readback retains the finite uint predicate"),
            Readback.Contains(P4OriginalPositiveInfinityOperation::Code()));
        TestTrue(TEXT("Original output pins survive construction"),
            Material->EmissiveColor.Expression && Material->Opacity.Expression);
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->MarkPendingKill();
    }
    TestEqual(TEXT("Every exact original UI package was exercised"), Seen.Num(), 4);
    return !HasAnyErrors();
}
#endif

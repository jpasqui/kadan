#include "P4OriginalClassValueNotes.h"
#include "P4KadanTestSeams.h"
#include "P4MapKitCore/property_decoder.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ActorComponent.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalClassUnheldDetailTest, "P4MapKit.Kadan.ClassUnheldDetails",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalClassUnheldDetailTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    using V = P4MapKit::FDecodedValue;
    FString Kept;
    int32 Removed = 0;
    TArray<FP4OriginalClassValueNotHeld> Observed;
    if (!TestTrue(TEXT("The actual native Unheld traversal records unsupported nested postprocess members"),
            P4LevelContent::P4UnheldClassValueDetailsForTest(UPostProcessComponent::StaticClass(),
                TEXT("{\"Settings\":{\"P4DiagnosticMissingField\":42,\"BloomIntensity\":2.25}}"), Kept, Removed,
                Observed)))
        return false;
    if (!TestTrue(
            TEXT("Exactly the missing nested field is counted and identified"), Removed == 1 && Observed.Num() == 1))
        return false;
    TestTrue(TEXT("Native traversal keeps exact object/property/schema/kind/reason/value evidence"),
        Observed[0].Object == TEXT("SourceTemplate") &&
            Observed[0].Property == TEXT("Settings.P4DiagnosticMissingField") &&
            Observed[0].Schema == TEXT("/Script/Engine.PostProcessSettings") && Observed[0].Kind == TEXT("number") &&
            Observed[0].Reason == TEXT("property-not-in-native-schema") && Observed[0].SourceValue == TEXT("42"));
    TSharedPtr<FJsonObject> KeptJson;
    if (!TestTrue(TEXT("Supported postprocess data remains readable after the same native decision"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Kept), KeptJson)))
        return false;
    TestEqual(TEXT("Known postprocess value is preserved exactly"),
        KeptJson->GetObjectField(TEXT("Settings"))->GetNumberField(TEXT("BloomIntensity")), 2.25);
    Observed.Reset();
    if (!TestTrue(TEXT("Native named enum refusal produces distinct evidence"),
            P4LevelContent::P4UnheldClassValueDetailsForTest(USceneComponent::StaticClass(),
                TEXT("{\"Mobility\":\"P4UnavailableNativeMobility\",\"bVisible\":true}"), Kept, Removed, Observed)))
        return false;
    TestTrue(TEXT("An unavailable enum choice is distinguished from a missing property"),
        Removed == 1 && Observed.Num() == 1 && Observed[0].Property == TEXT("Mobility") &&
            Observed[0].Reason == TEXT("enum-choice-not-in-native-schema"));
    Observed.Reset();
    if (!TestTrue(TEXT("The real native struct-array traversal retains element identity"),
            P4LevelContent::P4UnheldClassValueDetailsForTest(UActorComponent::StaticClass(),
                TEXT(
                    "{\"UCSModifiedProperties\":[{\"MemberName\":\"RelativeLocation\",\"P4DiagnosticMissingField\":true}]}"),
                Kept, Removed, Observed)))
        return false;
    TestTrue(TEXT("Nested array details preserve the exact source index"),
        Removed == 1 && Observed.Num() == 1 &&
            Observed[0].Property == TEXT("UCSModifiedProperties[0].P4DiagnosticMissingField"));
    FP4OriginalClassComparisonResult Result;
    Result.ValuesKnown = true;
    Result.ValuesNotHeld = 1;
    Result.Same = true;
    P4AppendOriginalClassUnheldDetail(&Result.ValuesNotHeldDetails, TEXT("Default__BP_EnvController_C.PostProcess"),
        TEXT("Settings.C4PostProcessParam0"), TEXT("/Script/Engine.PostProcessSettings"),
        V::Atom(V::EKind::Number, "0.800000011920929"), TEXT("property-not-in-native-schema"));
    TSharedPtr<FJsonObject> Json;
    if (!TestTrue(TEXT("Existing class comparison JSON retains a readable bounded detail array"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P4OriginalClassComparisonJson(Result)), Json)))
        return false;
    const auto& Row = Json->GetArrayField(TEXT("values_not_held_details"))[0]->AsObject();
    TestTrue(TEXT("Object, nested property, native schema, kind and exact preview identify the source member"),
        Row->GetStringField(TEXT("object")) == TEXT("Default__BP_EnvController_C.PostProcess") &&
            Row->GetStringField(TEXT("property")) == TEXT("Settings.C4PostProcessParam0") &&
            Row->GetStringField(TEXT("schema")) == TEXT("/Script/Engine.PostProcessSettings") &&
            Row->GetStringField(TEXT("kind")) == TEXT("number") &&
            Row->GetStringField(TEXT("source_value")) == TEXT("0.800000011920929"));
    TestTrue(TEXT("Adding evidence preserves existing numeric count/comparison behavior"),
        Json->GetIntegerField(TEXT("values_this_engine_cannot_hold")) == 1 && Json->GetBoolField(TEXT("same")) &&
            !Json->GetBoolField(TEXT("values_not_held_details_truncated")));
    V Large;
    Large.Kind = V::EKind::Array;
    Large.Items.resize(4096);
    Result.ValuesNotHeldDetails.Reset();
    Result.ValuesNotHeld = 300;
    for (int32 Index = 0; Index < 300; ++Index)
        P4AppendOriginalClassUnheldDetail(&Result.ValuesNotHeldDetails, TEXT("Template"), FString::FromInt(Index),
            TEXT("/Script/Engine.Fixture"), Large, TEXT("property-not-in-native-schema"));
    TestEqual(
        TEXT("A large diagnostic corpus cannot publish more than256 rows"), Result.ValuesNotHeldDetails.Num(), 256);
    TestTrue(TEXT("Large source compounds have bounded honest previews"),
        Result.ValuesNotHeldDetails[0].SourceValue.Len() <= 512 &&
            Result.ValuesNotHeldDetails[0].SourceValue.StartsWith(TEXT("bounded preview omitted:")));
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P4OriginalClassComparisonJson(Result)), Json);
    TestTrue(TEXT("The complete unsupported count survives bounded detail truncation"),
        Json->GetIntegerField(TEXT("values_this_engine_cannot_hold")) == 300 &&
            Json->GetBoolField(TEXT("values_not_held_details_truncated")));
    return !HasAnyErrors();
}
#endif

#include "P4ComparisonNames.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ComparisonNameTypes, "P4MapKit.Quality.ComparisonNameTypes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ComparisonNameTypes::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using P4MapKit::FDecodedValue;
    const auto String = [](const char* Text) { return FDecodedValue::Atom(FDecodedValue::EKind::String, Text); };
    FDecodedValue Names;
    Names.Kind = FDecodedValue::EKind::Array;
    Names.Items.push_back(String("HeightMap"));
    const auto Before = Names;
    P4ComparisonNames(Names, AActor::StaticClass()->FindPropertyByName(TEXT("Tags")));
    TestTrue(TEXT("Native FName array compares without case"), Names.Items[0].Text == "heightmap");
    TestTrue(TEXT("Comparison preserves the recorded source"), Before.Items[0].Text == "HeightMap");
    auto Object = String("/Game/Area.Actor.OverLapArea");
    P4ComparisonNames(Object, USceneComponent::StaticClass()->FindPropertyByName(TEXT("AttachParent")));
    TestTrue(TEXT("Native object reference compares without case"), Object.Text == "/game/area.actor.overlaparea");
    auto Label = String("HeightMap");
    const FProperty* LabelProperty = AActor::StaticClass()->FindPropertyByName(TEXT("ActorLabel"));
    if (!TestTrue(
            TEXT("Negative control is a genuine FString property"), CastField<FStrProperty>(LabelProperty) != nullptr))
        return false;
    P4ComparisonNames(Label, LabelProperty);
    TestTrue(TEXT("Native FString remains case sensitive"), Label.Text == "HeightMap");
    auto Unknown = String("HeightMap");
    P4ComparisonNames(Unknown, nullptr);
    TestTrue(TEXT("An unknown schema cannot excuse a spelling difference"), Unknown.Text == "HeightMap");
    const FProperty* Location = USceneComponent::StaticClass()->FindPropertyByName(TEXT("RelativeLocation"));
    FDecodedValue Upper, Lower;
    Upper.Kind = Lower.Kind = FDecodedValue::EKind::Object;
    Upper.Fields.emplace_back("X", FDecodedValue::Atom(FDecodedValue::EKind::Number, "130.0"));
    Lower.Fields.emplace_back("x", FDecodedValue::Atom(FDecodedValue::EKind::Number, "130.0"));
    P4ComparisonNames(Upper, Location);
    P4ComparisonNames(Lower, Location);
    TestTrue(TEXT("Known nested struct property names follow FName identity too"),
        P4MapKit::DecodedJson(Upper) == P4MapKit::DecodedJson(Lower));
    FDecodedValue Unrecognized;
    Unrecognized.Kind = FDecodedValue::EKind::Object;
    Unrecognized.Fields.emplace_back("NotAVectorMember", String("KeepCase"));
    P4ComparisonNames(Unrecognized, Location);
    TestTrue(TEXT("Unrecognized nested data is not normalized without a schema"),
        Unrecognized.Fields[0].first == "NotAVectorMember" && Unrecognized.Fields[0].second.Text == "KeepCase");
    return !HasAnyErrors();
}
#endif

#if P4_MAPKIT_TESTS
#include "P4RedundantMaterialOverrides.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RedundantMaterialOverridesTest, "P4MapKit.Levels.NullMaterialOverrideEquivalence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RedundantMaterialOverridesTest::RunTest(const FString&)
{
    using P4MapKit::FDecodedValue;
    auto* Component = NewObject<UStaticMeshComponent>(GetTransientPackage());
    FDecodedValue Empty, Source, Nulls;
    Nulls.Kind = FDecodedValue::EKind::Array;
    Nulls.Items.push_back(FDecodedValue::Atom(FDecodedValue::EKind::Null, "null"));
    Source.Fields.emplace_back("OverrideMaterials", Nulls);
    const auto Original = Source;
    P4WithoutRedundantNullMaterialOverrides(Component, Source, Empty);
    TestNull(TEXT("A source null override is equivalent to the actual empty override array"),
             Source.Field("OverrideMaterials"));
    Component->OverrideMaterials.Add(nullptr);
    Source = Original;
    P4WithoutRedundantNullMaterialOverrides(Component, Source, Empty);
    TestNull(TEXT("An identical live null slot omitted by the cook retains mesh-default materials"),
             Source.Field("OverrideMaterials"));
    Component->OverrideMaterials.Add(nullptr);
    Source = Original;
    P4WithoutRedundantNullMaterialOverrides(Component, Source, Empty);
    TestNotNull(TEXT("Different nonempty override array dimensions remain compared"),
                Source.Field("OverrideMaterials"));
    Component->OverrideMaterials.Reset();
    FDecodedValue Explicit = Original;
    Source = Original;
    P4WithoutRedundantNullMaterialOverrides(Component, Source, Explicit);
    TestNotNull(TEXT("A cooked explicit field remains a real comparison"), Source.Field("OverrideMaterials"));
    Component->OverrideMaterials.Add(UMaterial::GetDefaultMaterial(MD_Surface));
    Source = Original;
    P4WithoutRedundantNullMaterialOverrides(Component, Source, Empty);
    TestNotNull(TEXT("A creator-assigned material is not hidden"), Source.Field("OverrideMaterials"));
    Component->OverrideMaterials.Reset();
    Source = Original;
    Source.Fields[0].second.Items[0] =
        FDecodedValue::Atom(FDecodedValue::EKind::String, "/Game/Creator/Material.Material");
    P4WithoutRedundantNullMaterialOverrides(Component, Source, Empty);
    TestNotNull(TEXT("A non-null source override is not projected"), Source.Field("OverrideMaterials"));
    Source = Original;
    auto* Other = NewObject<USceneComponent>(GetTransientPackage());
    P4WithoutRedundantNullMaterialOverrides(Other, Source, Empty);
    TestNotNull(TEXT("An unrelated object's same-named field is not projected"), Source.Field("OverrideMaterials"));
    Other->MarkPendingKill();
    Component->MarkPendingKill();
    return !HasAnyErrors();
}
#endif

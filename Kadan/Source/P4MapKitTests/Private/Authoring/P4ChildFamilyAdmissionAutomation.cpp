#include "CoreMinimal.h"
#include "P4KadanTestSeams.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/PointLight.h"
#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/World.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMeshActor.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GameFramework/Volume.h"
#include "P4LevelImportWorld.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
namespace P4LevelContent
{
KADAN_API bool ProjectionConstructorChildrenSafe(UClass* Root, TFunctionRef<bool(UClass*)> Supported, FString& Error);
KADAN_API void SetOriginalLayerEditorVisibility(ULevelStreaming*, bool);
KADAN_API bool ProjectionNavigationIsGameBacked(const UClass*);
KADAN_API bool ProjectionNavigationBoundsClass(const UClass*);
} //namespace P4LevelContent
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalNavigationAdmission, "P4MapKit.NativeImport.OriginalNavigationAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalNavigationAdmission::RunTest(const FString&)
{
    TestTrue(TEXT("The engine NavigationData role is kept in the original game layer"),
        P4LevelContent::ProjectionNavigationIsGameBacked(ANavigationData::StaticClass()));
    TestTrue(TEXT("Actual Recast navigation inherits the same source preservation policy"),
        P4LevelContent::ProjectionNavigationIsGameBacked(ARecastNavMesh::StaticClass()));
    TestFalse(TEXT("Unresolved classes are not assigned a made-up navigation identity"),
        P4LevelContent::ProjectionNavigationIsGameBacked(nullptr));
    TestFalse(TEXT("Independent actual mesh actors retain editable construction admission"),
        P4LevelContent::ProjectionNavigationIsGameBacked(AStaticMeshActor::StaticClass()));
    TestFalse(TEXT("Independent actual light actors retain editable construction admission"),
        P4LevelContent::ProjectionNavigationIsGameBacked(APointLight::StaticClass()));
    TestFalse(TEXT("A native component is not a navigation actor family"),
        P4LevelContent::ProjectionNavigationIsGameBacked(UStaticMeshComponent::StaticClass()));
    TestTrue(TEXT("Actual source navigation bounds are eligible for separately qualified brush reconstruction"),
        P4LevelContent::ProjectionNavigationBoundsClass(ANavMeshBoundsVolume::StaticClass()));
    TestFalse(TEXT("An arbitrary source volume does not gain navigation-bounds admission"),
        P4LevelContent::ProjectionNavigationBoundsClass(AVolume::StaticClass()));
    TestFalse(TEXT("A mesh actor is not a source bounds volume"),
        P4LevelContent::ProjectionNavigationBoundsClass(AStaticMeshActor::StaticClass()));
    FString Error;
    TestTrue(TEXT("Actual bounds can use the bounded constructor preflight before exact source-family proof"),
        P4LevelContent::ProjectionConstructorChildrenSafe(
            ANavMeshBoundsVolume::StaticClass(), [](UClass*) { return true; }, Error));
    TestFalse(TEXT("Other generic volumes remain conservatively declined"),
        P4LevelContent::ProjectionConstructorChildrenSafe(AVolume::StaticClass(), [](UClass*) { return true; }, Error));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLayerEditorVisibility,
    "P4MapKit.NativeImport.OriginalLayerEditorVisibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalLayerEditorVisibility::RunTest(const FString&)
{
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4EditorVisibility_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UWorld> World(P4CreateOriginalImportWorld(FName(TEXT("EditorVisibility")), Package.Get()));
    if (!TestNotNull(TEXT("A real engine world supplies editor streaming state"), World.Get()))
        return false;
    World->WorldType = EWorldType::Editor;
    ULevelStreamingDynamic* Streaming = NewObject<ULevelStreamingDynamic>(World.Get());
    World->AddStreamingLevel(Streaming);
    for (const bool Loaded : {false, true})
        for (const bool Visible : {false, true})
            for (const bool Selected : {false, true})
            {
                Streaming->SetShouldBeLoaded(Loaded);
                Streaming->SetShouldBeVisible(Visible);
                P4LevelContent::SetOriginalLayerEditorVisibility(Streaming, Selected);
                TestEqual(TEXT("Source runtime load state remains exact"), Streaming->ShouldBeLoaded(), Loaded);
                TestEqual(
                    TEXT("Source runtime visibility remains exact"), Streaming->GetShouldBeVisibleFlag(), Visible);
                TestEqual(TEXT("Editor-only visibility matches the chosen original variant"),
                    Streaming->GetShouldBeVisibleInEditor(), Selected);
                TestEqual(TEXT("The actual UE editor visibility predicate uses the editor-only choice"),
                    Streaming->ShouldBeVisible(), Selected);
            }
    World->WorldType = EWorldType::Inactive;
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ChildFamilyAdmission, "P4MapKit.NativeImport.SourceChildFamilyAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ChildFamilyAdmission::RunTest(const FString&)
{
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4ChildFamily_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package.Get(),
        TEXT("Parent"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("A real source-style Blueprint fixture exists"), Blueprint))
        return false;
    USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(
        UChildActorComponent::StaticClass(), TEXT("SourceChild"));
    Blueprint->SimpleConstructionScript->AddNode(Node);
    UChildActorComponent* Template = Cast<UChildActorComponent>(Node->ComponentTemplate);
    if (!TestNotNull(TEXT("The genuine engine SCS child template exists"), Template))
        return false;
    Template->SetChildActorClass(APointLight::StaticClass());
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (!TestTrue(TEXT("The real Blueprint compiles before admission"),
            Blueprint->GeneratedClass && Blueprint->Status != BS_Error))
        return false;
    FString Error;
    const auto Supported = [](UClass* Class) { return Class && Class->IsChildOf(AActor::StaticClass()); };
    TestTrue(TEXT("A bounded source-complete child constructor graph is eligible for exact cooking"),
        P4LevelContent::ProjectionConstructorChildrenSafe(Blueprint->GeneratedClass, Supported, Error));
    TestFalse(TEXT("A declined constructor child disqualifies the whole parent before spawning"),
        P4LevelContent::ProjectionConstructorChildrenSafe(
            Blueprint->GeneratedClass, [](UClass* Class) { return Class && Class != APointLight::StaticClass(); },
            Error));
    Template = Cast<UChildActorComponent>(
        Node->GetActualComponentTemplate(Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass)));
    if (!TestNotNull(TEXT("The compiled actual component template is discoverable"), Template))
        return false;
    Template->SetChildActorClass(TSubclassOf<AActor>(Blueprint->GeneratedClass));
    TestFalse(TEXT("A recursive native/SCS child constructor graph is rejected before actor creation"),
        P4LevelContent::ProjectionConstructorChildrenSafe(Blueprint->GeneratedClass, Supported, Error));
    Blueprint->ClearFlags(RF_Public | RF_Standalone);
    return !HasAnyErrors();
}
namespace P4LevelContent
{
KADAN_API bool ProjectionClassHasProvenDefaults(const UClass*, const TSet<FName>&);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MissingClassSourceAdmission, "P4MapKit.NativeImport.MissingClassSourceAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MissingClassSourceAdmission::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FString Package = TEXT("/Game/P4MissingClassSource_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TSet<FName> Unproven;
    int32 Asked = -1;
    const auto Evidence = P4ClassSourceAdmissionForTest(Package + TEXT(".Missing_C"), FString(), Unproven, Asked);
    TestEqual(TEXT("An unavailable declared class remains in the required source-proof inventory"), Unproven.Num(), 1);
    TestTrue(TEXT("Missing source is explicitly declined, not treated as equal"), Unproven.Contains(FName(*Package)));
    TestEqual(TEXT("No class comparison is claimed for absent source"), Asked, 0);
    TestEqual(TEXT("The exact missing-class source failure is retained"), Evidence.Num(), 1);
    TestTrue(TEXT("An independent native actor retains default admission"),
        P4LevelContent::ProjectionClassHasProvenDefaults(APointLight::StaticClass(), Unproven));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeDerivedTerrainTracking, "P4MapKit.NativeImport.NativeDerivedTerrainTracking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeDerivedTerrainTracking::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FString Class = TEXT("/Script/Carrot4.StaticCollisionComponent");
    const UClass* Actual = FindObject<UClass>(nullptr, *Class);
    if (!TestNotNull(TEXT("The actual source-qualified SDK native class is loaded"), Actual))
        return false;
    const FStructProperty* Storage = FindFProperty<FStructProperty>(Actual, TEXT("C4TerrainTagContainer"));
    AddInfo(FString::Printf(
        TEXT("Terrain declaration: native %d, static mesh %d, owner %s, flags %016llx, struct %s, size %d"),
        int32(Actual->HasAnyClassFlags(CLASS_Native)), int32(Actual->IsChildOf(UStaticMeshComponent::StaticClass())),
        Storage ? *Storage->GetOwnerStruct()->GetPathName() : TEXT("missing"),
        Storage ? Storage->GetPropertyFlags() : 0ull,
        Storage && Storage->Struct ? *Storage->Struct->GetPathName() : TEXT("missing"),
        Storage && Storage->Struct ? Storage->Struct->GetStructureSize() : -1));
    const FString Member = TEXT(
        "{\"MemberParent\":\"/Script/Carrot4.StaticCollisionComponent\",\"MemberName\":\"C4TerrainTagContainer\",\"MemberGuid\":{\"A\":0,\"B\":0,\"C\":0,\"D\":0}}");
    TArray<FString> Compared, EngineMade;
    const auto Read = [&](const FString& Owner, const FString& Members)
    { return P4TerrainTrackingLinesForTest(Owner, TEXT("[") + Members + TEXT("]"), Compared, EngineMade); };
    const auto HasTracking = [&]()
    {
        return Compared.ContainsByPredicate(
            [](const FString& Line) { return Line.Contains(TEXT("ucsmodifiedproperties =")); });
    };
    if (!TestTrue(TEXT("The complete native source member identity decodes"), Read(Class, Member)))
        return false;
    TestFalse(TEXT("Only the qualified native game-derived terrain tracking is bookkeeping"), HasTracking());
    TestEqual(TEXT("The classification records its concrete engine-derived reason"), EngineMade.Num(), 1);
    for (const FString& Other : {Member.Replace(TEXT("StaticCollisionComponent"), TEXT("CarrotStaticMeshComponent")),
             Member.Replace(TEXT("C4TerrainTagContainer"), TEXT("BodyInstance")),
             Member.Replace(TEXT("\"A\":0"), TEXT("\"A\":1")), Member.Replace(TEXT("\"D\":0"), TEXT("\"D\":\"0\"")),
             Member.LeftChop(1) + TEXT(",\"Unknown\":0}"), Member.Replace(TEXT(",\"D\":0"), TEXT(""))})
    {
        if (!TestTrue(TEXT("Negative native member control decodes"), Read(Class, Other)))
            return false;
        TestTrue(
            TEXT("Different/missing/extra parent, name, GUID or GUID type remains strictly compared"), HasTracking());
        TestEqual(TEXT("A mismatched member earns no terrain classification"), EngineMade.Num(), 0);
    }
    TestTrue(TEXT("An unrelated component class decodes"), Read(TEXT("/Script/Engine.StaticMeshComponent"), Member));
    TestTrue(TEXT("Exact member spelling alone never qualifies a different component class"), HasTracking());
    const FString Authored = Member.Replace(TEXT("C4TerrainTagContainer"), TEXT("BodyInstance"));
    TestTrue(TEXT("Mixed native and authored tracking decodes"), Read(Class, Member + TEXT(",") + Authored));
    TestTrue(TEXT("An independent authored member stays in the comparison"), HasTracking());
    TestTrue(TEXT("The retained authored member is unchanged"),
        Compared.ContainsByPredicate([&](const FString& Line)
            { return Line == TEXT("source.component | ucsmodifiedproperties = [") + Authored + TEXT("]"); }));
    TestEqual(TEXT("Only the exact derived entry is classified in a mixed list"), EngineMade.Num(), 1);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ClassComponentNativeTail, "P4MapKit.NativeImport.ClassComponentNativeTail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ClassComponentNativeTail::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    TArray<uint8> Bytes;
    Bytes.AddZeroed(8);
    int32 Lods = -1;
    TestTrue(TEXT("Actual UObject identity and empty mesh LOD list are fully decoded"),
        P4ClassComponentTailForTest(UStaticMeshComponent::StaticClass(), Bytes, Lods));
    TestEqual(TEXT("Decoded empty list has exactly zero LODs"), Lods, 0);
    TestFalse(TEXT("Matching bytes cannot qualify an unrelated object class"),
        P4ClassComponentTailForTest(UObject::StaticClass(), Bytes, Lods));
    Bytes.Add(1);
    TestFalse(TEXT("Unknown extra payload is never admitted"),
        P4ClassComponentTailForTest(UStaticMeshComponent::StaticClass(), Bytes, Lods));
    Bytes.SetNum(7);
    TestFalse(TEXT("Truncated native data remains unreadable"),
        P4ClassComponentTailForTest(UStaticMeshComponent::StaticClass(), Bytes, Lods));
    Bytes.SetNumZeroed(10);
    Bytes[4] = 1;
    Bytes[8] = 2;
    Bytes[9] = 1;
    TestTrue(TEXT("A complete stripped server LOD uses the same bounded native reader"),
        P4ClassComponentTailForTest(UStaticMeshComponent::StaticClass(), Bytes, Lods));
    TestEqual(TEXT("The nonempty declared LOD list is read, not assumed empty"), Lods, 1);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ClassSkyAtmosphereNativeTail, "P4MapKit.NativeImport.ClassSkyAtmosphereNativeTail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ClassSkyAtmosphereNativeTail::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    TArray<uint8> Bytes;
    Bytes.AddZeroed(20);
    for (int32 Index = 4; Index < Bytes.Num(); ++Index)
        Bytes[Index] = uint8(Index * 11);
    TArray<FString> Compared, EngineMade;
    const auto Read = [&](UClass* Class, bool Undecoded = false)
    { return P4ClassLightingTailForTest(Class, Bytes, Undecoded, Compared, EngineMade); };
    TestTrue(TEXT("The complete native SkyAtmosphere lighting GUID is understood"),
        Read(USkyAtmosphereComponent::StaticClass()));
    TestTrue(TEXT("Authored atmosphere properties remain compared"),
        Compared.Contains(TEXT("Source.Sky | rayleighscatteringscale = 12.5")));
    TestTrue(TEXT("Only the lighting-build identifier is classified as engine data"),
        EngineMade.ContainsByPredicate(
            [](const FString& Line) { return Line.Contains(TEXT("the id of a lighting build, 20 bytes")); }));
    TestFalse(TEXT("Matching bytes cannot qualify a different component class"), Read(USceneComponent::StaticClass()));
    TestFalse(
        TEXT("Unread authored properties still refuse admission"), Read(USkyAtmosphereComponent::StaticClass(), true));
    Bytes[0] = 1;
    TestFalse(TEXT("An object GUID payload requires its own reader"), Read(USkyAtmosphereComponent::StaticClass()));
    Bytes[0] = 0;
    Bytes.SetNum(19);
    TestFalse(TEXT("A truncated lighting GUID is refused"), Read(USkyAtmosphereComponent::StaticClass()));
    Bytes.SetNumZeroed(21);
    TestFalse(TEXT("Extra native payload stays unreadable"), Read(USkyAtmosphereComponent::StaticClass()));
    return !HasAnyErrors();
}
#endif

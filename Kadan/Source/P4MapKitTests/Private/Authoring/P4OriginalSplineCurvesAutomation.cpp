#if P4_MAPKIT_TESTS
#include "P4OriginalSplineCurves.h"
#include "P4OriginalComponentReferences.h"
#include "Components/SplineComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ObjectVersion.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalSplineReplay, "P4MapKit.NativeImport.SplineCurveConstructionReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalSplineReplay::RunTest(const FString&)
{
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.RouteSplineComponent"));
    if (!TestNotNull(TEXT("The real native SDK route spline class is loaded"), Class))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4SplineCurve_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package.Get(),
        TEXT("BP_RouteCurve"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestTrue(TEXT("A real editor Blueprint exists"), World && Blueprint.IsValid()))
        return false;
    USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Class, TEXT("RouteOpaque"));
    Blueprint->SimpleConstructionScript->AddNode(Node);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    AActor* Actor = World->SpawnActor<AActor>(Blueprint->GeneratedClass);
    const auto Component = [&]() -> USplineComponent*
    {
        const FObjectProperty* Member = Actor
            ? FindFProperty<FObjectProperty>(Actor->GetClass(), Node->GetVariableName())
            : nullptr;
        return Member ? Cast<USplineComponent>(Member->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
    };
    USplineComponent* First = Component();
    if (!TestNotNull(TEXT("Actual native route spline instantiates through its SCS"), First))
        return false;
    First->SplineCurves = FSplineCurves();
    for (int32 Index = 0; Index < 4; ++Index)
    {
        First->SplineCurves.Position.Points.Emplace(float(Index), FVector(Index * 10.0f, (Index % 2) * 10.0f, 0),
            FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
        First->SplineCurves.Rotation.Points.Emplace(
            float(Index), FQuat(FVector::UpVector, Index * 0.5f), FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
        First->SplineCurves.Scale.Points.Emplace(
            float(Index), FVector::OneVector, FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
    }
    First->UpdateSpline();
    First->bSplineHasBeenEdited = true;
    // Model the game's retained auto-tangent bits differing from desktop regeneration by one ULP.
    float& Tangent = First->SplineCurves.Rotation.Points[1].ArriveTangent.W;
    uint32 Bits;
    FMemory::Memcpy(&Bits, &Tangent, sizeof(Bits));
    ++Bits;
    FMemory::Memcpy(&Tangent, &Bits, sizeof(Bits));
    First->SplineCurves.Rotation.Points[1].LeaveTangent.W = Tangent;
    const FSplineCurves Source = First->SplineCurves;
    const auto CurveBits = [](FSplineCurves Curves)
    {
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes);
        Writer.SetUE4Ver(VER_UE4_AUTOMATIC_VERSION);
        Writer << Curves.Position << Curves.Rotation << Curves.Scale << Curves.ReparamTable;
        return Bytes;
    };
    const TArray<uint8> Original = CurveBits(Source);
    for (int32 Round = 0; Round < 3; ++Round)
    {
        USplineComponent* Old = Component();
        {
            FP4OriginalComponentReferenceScope Scope(World);
            Actor->RerunConstructionScripts();
        }
        USplineComponent* Current = Component();
        if (!TestTrue(TEXT("Real construction replaces the native route spline"), Current && Current != Old))
            return false;
        TestTrue(TEXT("Every spline point, tangent, mode, loop and reparameterization bit survives"),
            CurveBits(Current->SplineCurves) == Original);
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("Exact native curve data survives old-component collection"),
            CurveBits(Current->SplineCurves) == Original);
    }
    USplineComponent* Old = NewObject<USplineComponent>(Actor, Class);
    USplineComponent* New = NewObject<USplineComponent>(Actor, Class);
    Old->bSplineHasBeenEdited = New->bSplineHasBeenEdited = true;
    Old->SplineCurves = Source;
    const auto Reset = [&]()
    {
        New->SplineCurves = Source;
        New->SplineCurves.Rotation.AutoSetTangents(0.0f, New->bStationaryEndpoints);
    };
    Reset();
    TestTrue(TEXT("A proven native-only quaternion rebuild can retain the source bits"),
        P4RestoreOriginalSplineCurves(Old, New));
    for (int32 Control = 0; Control < 4; ++Control)
    {
        Reset();
        if (Control == 0)
            New->SplineCurves.Position.Points[1].OutVal.X += 1.0f;
        if (Control == 1)
            New->SplineCurves.Rotation.Points[1].InterpMode = CIM_CurveUser;
        if (Control == 2)
            New->SplineCurves.ReparamTable.Points[1].OutVal += 1.0f;
        if (Control == 3)
            New->SplineCurves.Rotation.Points[1].ArriveTangent.W += 0.25f;
        const TArray<uint8> Authored = CurveBits(New->SplineCurves);
        TestFalse(TEXT("Authored point, mode, table and tangent changes are preserved"),
            P4RestoreOriginalSplineCurves(Old, New));
        TestTrue(TEXT("Refusal keeps every creator curve bit unchanged"), CurveBits(New->SplineCurves) == Authored);
    }
    Reset();
    New->bStationaryEndpoints = !Old->bStationaryEndpoints;
    TestFalse(TEXT("Changed native generation settings retain current construction"),
        P4RestoreOriginalSplineCurves(Old, New));
    New->bStationaryEndpoints = Old->bStationaryEndpoints;
    Reset();
    New->bInputSplinePointsToConstructionScript = true;
    TestFalse(TEXT("A construction script explicitly consuming spline points keeps ownership"),
        P4RestoreOriginalSplineCurves(Old, New));
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}
#endif

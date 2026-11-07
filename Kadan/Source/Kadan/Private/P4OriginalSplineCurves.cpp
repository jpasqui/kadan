#include "P4OriginalSplineCurves.h"
#include "P4GameNames.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ObjectVersion.h"
#include "UObject/UnrealType.h"

namespace
{
TArray<uint8> CurveBits(FSplineCurves Curves)
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    Writer.SetUE4Ver(VER_UE4_AUTOMATIC_VERSION);
    Writer << Curves.Position << Curves.Rotation << Curves.Scale << Curves.ReparamTable;
    return Bytes;
}
}

bool P4RestoreOriginalSplineCurves(UActorComponent* OldComponent, UActorComponent* NewComponent)
{
    USplineComponent* Old = Cast<USplineComponent>(OldComponent);
    USplineComponent* New = Cast<USplineComponent>(NewComponent);
    UClass* Storage = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".RouteSplineComponent"));
    if (!Old || !IsValid(New) || !Storage || !Storage->HasAnyClassFlags(CLASS_Native) ||
        Storage->GetSuperClass() != USplineComponent::StaticClass() || !New->IsA(Storage) ||
        Old->GetClass() != New->GetClass() || Old->IsTemplate() || New->IsTemplate() ||
        !Old->GetTypedOuter<AActor>() || Old->GetTypedOuter<AActor>() != New->GetTypedOuter<AActor>() ||
        !Old->bSplineHasBeenEdited || !New->bSplineHasBeenEdited ||
        Old->bInputSplinePointsToConstructionScript || New->bInputSplinePointsToConstructionScript ||
        Old->SplineCurves.Metadata_DEPRECATED || New->SplineCurves.Metadata_DEPRECATED) return false;
    for (const TCHAR* Name : {TEXT("bStationaryEndpoints"), TEXT("bClosedLoop"), TEXT("bLoopPositionOverride"),
        TEXT("LoopPosition"), TEXT("ReparamStepsPerSegment"), TEXT("DefaultUpVector")})
    {
        const FProperty* Field = USplineComponent::StaticClass()->FindPropertyByName(Name);
        if (!Field || !Field->Identical_InContainer(Old, New, 0, PPF_None)) return false;
    }
    // FSplineInstanceData restores the authored points, then unconditionally calls UpdateSpline.
    // The desktop's quaternion auto-tangent arithmetic can differ from the game's saved bits.
    // Require the ENTIRE replacement payload to be exactly the old payload with only this
    // native auto-tangent calculation applied. Point, mode, user tangent and table edits refuse.
    // FSplineCurves::operator== omits ReparamTable, so compare native serialized bits instead.
    FSplineCurves Generated = Old->SplineCurves;
    Generated.Rotation.AutoSetTangents(0.0f, New->bStationaryEndpoints);
    const TArray<uint8> Before = CurveBits(Old->SplineCurves), After = CurveBits(New->SplineCurves);
    if (Before == After || CurveBits(Generated) != After) return false;
    New->SplineCurves.Rotation = Old->SplineCurves.Rotation;
    return true;
}

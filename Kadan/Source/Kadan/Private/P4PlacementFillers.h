#pragma once
// The fields of a placement row's blobs that the game's serializers take from the actor's components (a sound
// emitter's place, a nav link component's links, a spline's points, a trigger box's extent), filled the same way.
// A field names its kind in the layout data ("fill", with "fill_class", "fill_tag" or "fill_name" for the component
// it reads). Each kind is checked against the game's rows: a blob made from one of the game's imported actors is the
// game's row, byte for byte (P4MapKit.Editor.PlacementBlobsFromActors).
#include "P4PlacementInternal.h"

namespace P4PlacementDetail
{
using FFiller = bool (*)(AActor* Actor, UObject* Owner, const P::Field& Field, P::Value& Out, FString& Error);
struct FFillerKind
{
    const TCHAR* Kind;
    FFiller Fill;
};
// Every kind: the ones of each file below, in one list.
TArrayView<const FFillerKind> FillerKinds();
// Fills Out, of Field's type, from the actor's components; false with Error when the actor does not have what the
// game's serializer reads, or the kind is not known.
bool FillFromActor(AActor* Actor, UObject* Owner, const P::Field& Field, P::Value& Out, FString& Error);

// The components of the actor the fillers look in: of the class (native or Blueprint, by name, its subclasses too),
// with the tag, or of the name, in the actor's component order.
TArray<UActorComponent*> ComponentsFor(AActor* Actor, const P::Field& Field);

// Kinds, by file. Each file lists its own in a FFillerKind array returned here.
TArrayView<const FFillerKind> NavFillers();    // P4PlacementFillNav.cpp: places and nav links
TArrayView<const FFillerKind> ShapeFillers();  // P4PlacementFillShapes.cpp: splines, boxes and the rest
} // namespace P4PlacementDetail

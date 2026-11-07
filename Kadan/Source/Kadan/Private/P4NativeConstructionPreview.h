#pragma once
#include "CoreMinimal.h"
class UWorld;
class UMaterial;
class FProperty;
class UObject;
// What the game's native classes do when they are constructed, which their editor stand-ins (declarations only)
// cannot: Kadan's record of those steps (DefaultKadan.ini, [NativeConstruction], read from the game's code), applied
// to a map's actors on transient instances, which a save leaves out as the game's own editor leaves its instances out.
// Returns how many steps were applied. Steps already applied are not applied again.
KADAN_API int32 P4ApplyNativeConstructionPreviews(UWorld*);
// The editor's drawing of a decal master in the game's water box decal mode (DBM_DBuffer_C4_WaterBox): a transient
// copy of the master whose outputs carry the game's wet look as a stock color decal. The master itself when it has
// another mode; null when the copy cannot be built.
KADAN_API UMaterial* P4WaterBoxDecalPreview(UMaterial* Master, FString& Error);
// The environment controllers' step of the same (P4NativeEnvironmentRecord.inl); part of the call above.
KADAN_API int32 P4ApplyNativeEnvironmentPreviews(UWorld*);
// Before a preview changes a saved value: the map's own value is kept, once per object and property.
KADAN_API void P4RememberNativePreviewValue(UObject* Object, FProperty* Property);
// The same for a value a property copy cannot hold: Exchange swaps the map's own value and the preview's each time it
// is called. Once per object and key.
KADAN_API void P4RememberNativePreviewExchange(UObject* Object, FName Key, TFunction<void()> Exchange);
// While one is alive, every remembered value is the map's own again, so a save or an export sees the map as it is;
// the previews come back when the last one ends.
struct KADAN_API FP4SuspendNativePreviews
{
    FP4SuspendNativePreviews();
    ~FP4SuspendNativePreviews();
};
// The same as a pair of calls, for an engine event's begin and end.
KADAN_API void P4SetNativePreviewsSuspended(bool Suspended);

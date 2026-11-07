#pragma once
#include "CoreMinimal.h"
#include "ShowFlags.h"
class UWorld;
struct FPostProcessSettings;

// Correct only the final editor preview, including Game View. Imported settings and cook output stay source-exact.
KADAN_API bool P4ApplyImportedEditorExposure(UWorld* World, const FEngineShowFlags& ViewFlags, bool SceneCapture, FPostProcessSettings& Settings);
void P4StartEditorExposure();
void P4StopEditorExposure();

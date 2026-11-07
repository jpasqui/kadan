#pragma once
#include "CoreMinimal.h"
#include "P4GameAssetFamilies.h"

class UObject;

namespace P4Kadan
{
struct FAssetPolicyIssue
{
    FString Code, Reason, Subject;
};

struct FAssetPolicy
{
    bool AllowEdit = true;
    bool AllowPreview = true;
    bool Original = false;
    bool ReferenceOnly = false;
    bool StandIn = false;
    // A material whose graph, or whose parent's graph, has Kadan game nodes: the editor draws it with
    // stand-in values where the game reads the scene. PreviewNotice says so in the read-only notice.
    bool PreviewDiffers = false;
    FString Reason, PreviewNotice;
    TArray<FAssetPolicyIssue> Issues;
};

// Editor-thread snapshot. Reads provenance and the live material preview graph; never saves,
// cooks, diagnoses content or hashes package files. Creator assets keep their ordinary editor.
KADAN_API FAssetPolicy GetAssetPolicy(UObject* Asset);
// Classification does not grant permission to stand-ins, broken imports or native classes.
KADAN_API EGameAssetFamily GameAssetFamily(const UObject* Asset);
KADAN_API bool IsRecoveredGameActorBlueprint(const UObject* Asset);
KADAN_API bool IsRecoveredGameMaterialInstance(const UObject* Asset);
// True for a material or material instance whose base material has a Kadan game node.
KADAN_API bool PreviewDiffersFromGame(UObject* Asset);
KADAN_API const TCHAR* PreviewNoticeText();
// The Content Browser's description of an asset with the line that says its preview differs.
KADAN_API FText PreviewDescription(UObject* Asset, const FText& Description);
void InitializeAssetPolicy();
void ShutdownAssetPolicy();
}

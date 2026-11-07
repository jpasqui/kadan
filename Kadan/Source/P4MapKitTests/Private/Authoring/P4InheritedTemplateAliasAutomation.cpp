#if P4_MAPKIT_TESTS
#include "P4OriginalRecord.h"
#include "P4LevelChanges.h"
#include "Components/SceneComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InheritedTemplateAliasTest, "P4MapKit.Levels.RedundantInheritedTemplateIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InheritedTemplateAliasTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_INHERITED_TEMPLATE_ALIAS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the actual cold inherited-root fixture."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(
        TEXT("/Game/Carrot4/Maps/Main/Area/Area500/Sublevels/Area500_Hero_Objects"));
    if (!TestNotNull(TEXT("The actual saved layer opens cold"), World))
        return false;
    AActor* Actor = FindObject<AActor>(World->PersistentLevel, TEXT("GMultiBlockArea500_01A_2"));
    USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
    if (!TestNotNull(TEXT("The actual source root component is present"), Root))
        return false;
    const FString Before = World->GetOutermost()->GetMetaData()->GetValue(World, TEXT("P4OriginalRecord"));
    const FString LinesBefore = World->GetOutermost()->GetMetaData()->GetValue(World, TEXT("P4NativeLevelImported"));
    const auto Original = P4DiagnoseOriginalAsset(World);
    TestTrue(TEXT("Pruning a redundant source scalar override retains cold source equality"),
             Original.Error.IsEmpty() && Original.Comparison == EP4OriginalComparison::Equal);
    const auto Delta = P4AnalyzeOriginalLevelChanges(World);
    TestTrue(TEXT("The old saved baseline reports no creator edit"), Delta.IsValid() && Delta.Unchanged);
    const auto Mobility = Root->Mobility;
    Root->Mobility = Mobility == EComponentMobility::Static ? EComponentMobility::Movable : EComponentMobility::Static;
    const auto Edited = P4DiagnoseOriginalAsset(World);
    TestTrue(TEXT("Changing the actual overridden scalar is still a creator edit"),
             Edited.Error.IsEmpty() && Edited.Comparison == EP4OriginalComparison::Different);
    Root->Mobility = Mobility;
    const FVector Location = Root->GetRelativeLocation();
    Root->SetRelativeLocation(Location + FVector(1, 0, 0));
    const auto Moved = P4DiagnoseOriginalAsset(World);
    TestTrue(TEXT("A real one-centimetre component edit remains visible"),
             Moved.Error.IsEmpty() && Moved.Comparison == EP4OriginalComparison::Different);
    Root->SetRelativeLocation(Location);
    const auto Restored = P4DiagnoseOriginalAsset(World);
    TestTrue(TEXT("Restoring the source values restores equality"),
             Restored.Error.IsEmpty() && Restored.Comparison == EP4OriginalComparison::Equal);
    TestEqual(TEXT("No original record is re-baselined"),
              FString(World->GetOutermost()->GetMetaData()->GetValue(World, TEXT("P4OriginalRecord"))), Before);
    TestEqual(TEXT("No imported property or archetype line is rewritten"),
              FString(World->GetOutermost()->GetMetaData()->GetValue(World, TEXT("P4NativeLevelImported"))),
              LinesBefore);
    return !HasAnyErrors();
}
#endif

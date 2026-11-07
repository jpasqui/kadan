#if P4_MAPKIT_TESTS
#include "P4OriginalRecord.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4HiddenLayerDiagnostic, "P4MapKit.Levels.HiddenOriginalLayerDiagnosticIsReadOnly",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4HiddenLayerDiagnostic::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_HIDDEN_LAYER_DIAGNOSTIC")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned original surface fixtures."));
        return true;
    }
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    for (const TCHAR* Area : {TEXT("Area001"), TEXT("Area002")})
    {
        const FString Prefix = TEXT("/Game/Carrot4/Maps/Main/Area/") + FString(Area) + TEXT("/") + Area;
        if (!TestNotNull(TEXT("The actual surface root opens"),
                         UEditorLoadingAndSavingUtils::LoadMap(Prefix + TEXT("_P"))))
            return false;
        UWorld* Layer = LoadObject<UWorld>(nullptr, *(FPaths::GetPath(Prefix) + TEXT("/Sublevels/") + Area +
                                                      TEXT("_Hero_Objects.") + Area + TEXT("_Hero_Objects")));
        if (!TestTrue(TEXT("The original hero layer is loaded but remains unregistered"),
                      Layer && Layer->PersistentLevel && !Layer->PersistentLevel->bAreComponentsCurrentlyRegistered))
            return false;
        TMap<UChildActorComponent*, AActor*> Children;
        for (AActor* Actor : Layer->PersistentLevel->Actors)
            if (IsValid(Actor))
            {
                TInlineComponentArray<UChildActorComponent*> Parts;
                Actor->GetComponents(Parts);
                for (auto* Part : Parts)
                    Children.Add(Part, Part->GetChildActor());
            }
        TestTrue(TEXT("The actual hidden fixture has native child actors"), Children.Num() > 0);
        const bool Dirty = Layer->GetOutermost()->IsDirty();
        for (int32 Pass = 0; Pass < 3; ++Pass)
        {
            const auto Compared = P4DiagnoseOriginalAsset(Layer);
            TestTrue(TEXT("Repeated actual native diagnostics preserve source equality"),
                     Compared.Error.IsEmpty() && Compared.Comparison == EP4OriginalComparison::Equal);
            TestFalse(TEXT("A diagnostic leaves the hidden level unregistered"),
                      Layer->PersistentLevel->bAreComponentsCurrentlyRegistered);
            TestEqual(TEXT("A diagnostic preserves the original dirty state"), Layer->GetOutermost()->IsDirty(), Dirty);
            for (const auto& Pair : Children)
                TestTrue(TEXT("A diagnostic never destroys/recreates a hidden source child"),
                         IsValid(Pair.Key) && Pair.Key->GetChildActor() == Pair.Value &&
                             (!Pair.Value || IsValid(Pair.Value)));
        }
        AActor* Edited = nullptr;
        for (AActor* Actor : Layer->PersistentLevel->Actors)
            if (IsValid(Actor) && Actor->GetName() == TEXT("Base1_Pod") && Actor->GetRootComponent())
            {
                Edited = Actor;
                break;
            }
        if (!TestNotNull(TEXT("A real source actor supplies the creator-edit control"), Edited))
            return false;
        USceneComponent* Root = Edited->GetRootComponent();
        const FVector Original = Root->GetRelativeLocation();
        Root->SetRelativeLocation_Direct(Original + FVector(1, 0, 0));
        Root->UpdateComponentToWorld();
        const auto Changed = P4DiagnoseOriginalAsset(Layer);
        TestTrue(TEXT("A genuine one-centimetre creator move still differs"),
                 Changed.Error.IsEmpty() && Changed.Comparison == EP4OriginalComparison::Different);
        Root->SetRelativeLocation_Direct(Original);
        Root->UpdateComponentToWorld();
        const auto Restored = P4DiagnoseOriginalAsset(Layer);
        TestTrue(TEXT("Restoring the real creator control restores exact source equality"),
                 Restored.Error.IsEmpty() && Restored.Comparison == EP4OriginalComparison::Equal);
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    }
    return !HasAnyErrors();
}
#endif

#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "P4LevelImportWorld.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Brush.h"
#include "Components/BrushComponent.h"
#include "Misc/ScopeExit.h"
namespace P4LevelContent
{
KADAN_API TSet<int32> ProjectionUnsafeAttachmentNodes(const TMap<int32, int32>& Parents);
KADAN_API bool ProjectionClassHasProvenDefaults(const UClass* Class, const TSet<FName>& Declined);
KADAN_API bool ProjectionFamilyEqual(
    const TArray<FString>& Before, const TArray<FString>& After, const FString& Level, const FString& Name);
KADAN_API bool ProjectionRetireUnexpectedActors(UWorld* World, const TSet<FName>& Admitted, FString& Error);
} //namespace P4LevelContent
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameBackedEditorScaffold, "P4MapKit.NativeImport.GameBackedEditorScaffold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameBackedEditorScaffold::RunTest(const FString&)
{
    TStrongObjectPtr<UWorld> Hold(P4CreateOriginalImportWorld(
        FName(*(TEXT("P4GameBackedScaffold_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        GetTransientPackage()));
    UWorld* World = Hold.Get();
    if (!TestNotNull(TEXT("The real inactive import world is initialized"), World))
        return false;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    GEditor->InitBuilderBrush(World);
    ABrush* Brush = World->GetDefaultBrush();
    if (!TestTrue(TEXT("The editor builder brush has a model and component"),
            Brush && Brush->Brush && Brush->GetBrushComponent()))
        return false;
    TestTrue(TEXT("Ordinary Map_Load will reuse the already initialized world"), World->bIsWorldInitialized != 0);
    TestFalse(TEXT("Client/server stripping alone does not mark the builder brush editor-only"), Brush->IsEditorOnly());
    FString Error;
    TestTrue(TEXT("An entirely uneditable projection retains editor scaffolding"),
        P4LevelContent::ProjectionRetireUnexpectedActors(World, {}, Error));
    TestTrue(TEXT("An empty source subset still has the engine brush at actor index one"),
        World->GetDefaultBrush() == Brush);
    AActor* Admitted = World->SpawnActor<AActor>();
    AActor* Unexpected = World->SpawnActor<AActor>();
    if (!TestTrue(TEXT("Source and unexpected constructor actors are real instances"), Admitted && Unexpected))
        return false;
    TSet<FName> Names;
    Names.Add(Admitted->GetFName());
    TestTrue(TEXT("Partial-world retirement succeeds"),
        P4LevelContent::ProjectionRetireUnexpectedActors(World, Names, Error));
    TestTrue(TEXT("An admitted source actor survives"), IsValid(Admitted));
    TestFalse(TEXT("An unadmitted constructor actor cannot become a creator addition"), IsValid(Unexpected));
    TestTrue(TEXT("Map_Load's exact builder-brush lookup remains valid"),
        World->PersistentLevel->Actors.Num() >= 2 && World->PersistentLevel->Actors[1] == Brush &&
            World->GetDefaultBrush() == Brush && Brush->Brush && Brush->GetBrushComponent()->Brush == Brush->Brush);
    // Refuse malformed scaffolding before altering any source actor or recording a baseline.
    World->PersistentLevel->Actors[1] = nullptr;
    TestFalse(TEXT("A missing brush refuses publication instead of reaching the editor's null dereference"),
        P4LevelContent::ProjectionRetireUnexpectedActors(World, {}, Error));
    TestTrue(TEXT("Scaffold refusal has not retired a supported source actor"), IsValid(Admitted));
    World->PersistentLevel->Actors[1] = Brush;
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameBackedAttachmentAdmission,
    "P4MapKit.NativeImport.GameBackedAttachmentAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameBackedAttachmentAdmission::RunTest(const FString&)
{
    TMap<int32, int32> Parents;
    Parents.Add(1, 2);
    Parents.Add(2, 3);
    Parents.Add(4, 4);
    Parents.Add(5, 6);
    Parents.Add(6, 5);
    Parents.Add(7, 5);
    const auto Unsafe = P4LevelContent::ProjectionUnsafeAttachmentNodes(Parents);
    TestFalse(TEXT("Ordinary chain is safe"), Unsafe.Contains(1));
    TestFalse(TEXT("Unserialized root terminates a safe chain"), Unsafe.Contains(2));
    TestTrue(TEXT("Self attachment is refused before component transforms"), Unsafe.Contains(4));
    TestTrue(TEXT("First cycle member is refused"), Unsafe.Contains(5));
    TestTrue(TEXT("Second cycle member is refused"), Unsafe.Contains(6));
    TestTrue(TEXT("Child leading into the cycle is refused"), Unsafe.Contains(7));
    TestEqual(TEXT("Only unsafe families are excluded"), Unsafe.Num(), 4);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameBackedSettledFamilyProof, "P4MapKit.NativeImport.GameBackedSettledFamilyProof",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameBackedSettledFamilyProof::RunTest(const FString&)
{
    const FString Level = TEXT("fixture.persistentlevel");
    const TArray<FString> Before = {
        Level + TEXT(".bad.unit | enabled = true"), Level + TEXT(".good.mesh | visible = true")};
    const TArray<FString> Changed = {
        Level + TEXT(".bad.unit | enabled = false"), Level + TEXT(".good.mesh | visible = true")};
    TestFalse(TEXT("Construction cannot erase a source-proven nested instance value"),
        P4LevelContent::ProjectionFamilyEqual(Before, Changed, Level, TEXT("bad")));
    TestTrue(TEXT("An independent settled actor retains qualification"),
        P4LevelContent::ProjectionFamilyEqual(Before, Changed, Level, TEXT("good")));
    TestFalse(TEXT("A destroyed original actor never gains an empty baseline"),
        P4LevelContent::ProjectionFamilyEqual(Before, {}, Level, TEXT("bad")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameBackedIndependentDefaults,
    "P4MapKit.NativeImport.GameBackedIndependentDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameBackedIndependentDefaults::RunTest(const FString&)
{
    const FString Root = TEXT("/Temp/P4GameBackedDefaults_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> FailedPackage(CreatePackage(*(Root + TEXT("/Failed"))));
    TStrongObjectPtr<UPackage> ChildPackage(CreatePackage(*(Root + TEXT("/Child"))));
    UClass* Failed = NewObject<UClass>(FailedPackage.Get(), TEXT("Failed_C"));
    Failed->SetSuperStruct(AActor::StaticClass());
    UClass* Child = NewObject<UClass>(ChildPackage.Get(), TEXT("Child_C"));
    Child->SetSuperStruct(Failed);
    TSet<FName> Declined;
    Declined.Add(FailedPackage->GetFName());
    TestFalse(TEXT("Failed source default class is uneditable"),
        P4LevelContent::ProjectionClassHasProvenDefaults(Failed, Declined));
    TestFalse(TEXT("Inherited failed source defaults disqualify its family"),
        P4LevelContent::ProjectionClassHasProvenDefaults(Child, Declined));
    TestTrue(TEXT("Unrelated native actor retains source-default admission"),
        P4LevelContent::ProjectionClassHasProvenDefaults(AActor::StaticClass(), Declined));
    TestFalse(TEXT("An unavailable class never passes source-default admission"),
        P4LevelContent::ProjectionClassHasProvenDefaults(nullptr, Declined));
    Child->ClearFlags(RF_Public | RF_Standalone);
    Child->MarkPendingKill();
    Failed->ClearFlags(RF_Public | RF_Standalone);
    Failed->MarkPendingKill();
    return true;
}
#endif

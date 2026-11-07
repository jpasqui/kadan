#if P4_MAPKIT_TESTS
#include "P4OriginalRecord.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InheritedRuleTable, "P4MapKit.Levels.InheritedRuleTableDefaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InheritedRuleTable::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RULE_TABLE_DEFAULTS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned actual original rule fixtures."));
        return true;
    }
    for (const TCHAR* Name : {TEXT("Cave016_F15"), TEXT("Cave018_F00")})
    {
        const FString Cave = FString(Name).Left(7), Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/") + Cave +
                                                              TEXT("/") + Name + TEXT("/") + Name + TEXT("_P");
        auto* World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + Name + TEXT("_P")));
        if (!TestNotNull(TEXT("The actual original root loads"), World))
            return false;
        auto* Actor = FindObject<AActor>(World->PersistentLevel, TEXT("ACarrotGameRuleStory_4"));
        const auto* Field = Actor ? FindFProperty<FArrayProperty>(Actor->GetClass(), TEXT("UIDataTableList")) : nullptr;
        const auto* Inner = Field ? CastField<FObjectProperty>(Field->Inner) : nullptr;
        if (!TestTrue(TEXT("The actual source actor has the native typed table array"),
                      Actor && Field && Inner && Inner->PropertyClass == UDataTable::StaticClass()))
            return false;
        FScriptArrayHelper Values(Field, Field->ContainerPtrToValuePtr<void>(Actor));
        if (!TestEqual(TEXT("The genuine table is present rather than lost on loading"), Values.Num(), 1))
            return false;
        UObject* Table = Inner->GetObjectPropertyValue(Values.GetRawPtr(0));
        TestTrue(TEXT("The repaired class supplies exactly the same original table"),
                 Field->Identical_InContainer(Actor, Actor->GetArchetype()) && Table &&
                     Table->GetName() == TEXT("DT_UIWidgetStory"));
        const auto Original = P4DiagnoseOriginalAsset(World);
        TestTrue(TEXT("Omitting a tag equal to its repaired class default preserves source equality"),
                 Original.Error.IsEmpty() && Original.Comparison == EP4OriginalComparison::Equal);
        Values.EmptyValues();
        const auto Cleared = P4DiagnoseOriginalAsset(World);
        TestTrue(TEXT("A creator-cleared table array still differs"),
                 Cleared.Error.IsEmpty() && Cleared.Comparison == EP4OriginalComparison::Different);
        Values.AddValues(1);
        Inner->SetObjectPropertyValue(Values.GetRawPtr(0), Table);
        const auto Restored = P4DiagnoseOriginalAsset(World);
        TestTrue(TEXT("Restoring the real typed table restores exact source equality"),
                 Restored.Error.IsEmpty() && Restored.Comparison == EP4OriginalComparison::Equal);
    }
    return !HasAnyErrors();
}
#endif

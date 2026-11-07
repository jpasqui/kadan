#include "P4AuthoringRows.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Editor.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingRemapAutomation, "P4MapKit.Quality.StreamingRemap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingRemapAutomation::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Game/P4StreamingAutomation/Remap"));
    UWorld* World = NewObject<UWorld>(Package, NAME_None, RF_Transactional);
    ULevelStreamingDynamic* First = NewObject<ULevelStreamingDynamic>(World);
    ULevelStreamingDynamic* Second = NewObject<ULevelStreamingDynamic>(World);
    First->SetWorldAssetByPackageName(TEXT("/Game/P4StreamingAutomation/Child"));
    Second->SetWorldAssetByPackageName(TEXT("/Game/P4StreamingAutomation/Other"));
    First->SetShouldBeLoaded(false);
    First->SetShouldBeVisible(false);
    First->SetShouldBeVisibleInEditor(false);
    Second->SetShouldBeLoaded(true);
    Second->SetShouldBeVisible(true);
    Second->SetShouldBeVisibleInEditor(true);
    First->LevelTransform = FTransform(FRotator(11, 23, 5), FVector(137, -84, 9), FVector(2, 3, 1));
    Second->LevelTransform = FTransform(FRotator(-3, 91, 1), FVector(-18, 67, 5), FVector(1, 2, 4));
    World->AddStreamingLevel(First);
    World->AddStreamingLevel(Second);
    Package->SetDirtyFlag(false);
    const FString Initial = UP4AuthoringTools::StreamingState(World);
    const FTransform FirstTransform = First->LevelTransform, SecondTransform = Second->LevelTransform;
    const FString Good = TEXT(
        "{\"/Game/P4StreamingAutomation/Child\":{\"target\":\"/Game/P4StreamingAutomation/CopiedChild\",\"loaded\":true,\"visible\":true,\"editor_visible\":true,\"package_to_load\":\"\"},\"/Game/P4StreamingAutomation/Other\":{\"target\":\"/Game/P4StreamingAutomation/CopiedOther\",\"loaded\":false,\"visible\":false,\"editor_visible\":false,\"package_to_load\":\"/Game/P4StreamingAutomation/Other\"}}");
    const auto Refuses = [&](const FString& Json, const TCHAR* Label)
    {
        TestFalse(Label, UP4AuthoringTools::RemapStreaming(World, Json, false, true));
        TestEqual(TEXT("Refusal retains every stream flag, package and override"),
            UP4AuthoringTools::StreamingState(World), Initial);
        TestFalse(TEXT("Refusal does not dirty authoring package"), Package->IsDirty());
        TestTrue(TEXT("Refusal retains transforms and actual array membership"),
            First->LevelTransform.Equals(FirstTransform, 0) && Second->LevelTransform.Equals(SecondTransform, 0) &&
                World->GetStreamingLevels().Num() == 2 && World->GetStreamingLevels()[0] == First &&
                World->GetStreamingLevels()[1] == Second);
    };
    Refuses(TEXT("broken"), TEXT("Malformed mapping refused"));
    Refuses(TEXT("[]"), TEXT("Array root refused"));
    Refuses(TEXT("{}"), TEXT("Incomplete source mapping refused"));
    Refuses(Good + TEXT("{}"), TEXT("Trailing document refused"));
    for (const TCHAR* Flag : {TEXT("loaded"), TEXT("visible"), TEXT("editor_visible")})
    {
        const FString Field = FString::Printf(TEXT("\"%s\":false"), Flag);
        Refuses(Good.Replace(*Field, *FString::Printf(TEXT("\"%s\":\"false\""), Flag)),
            TEXT("Late row string boolean refused atomically"));
        Refuses(Good.Replace(*Field, *FString::Printf(TEXT("\"%s\":0"), Flag)),
            TEXT("Late row numeric boolean refused atomically"));
        Refuses(Good.Replace(*(Field + TEXT(",")), TEXT("")), TEXT("Late row missing boolean refused atomically"));
    }
    for (const TCHAR* Target :
        {TEXT("/Game/Bad.Bad"), TEXT("/Game/"), TEXT("/Game/Bad Name"), TEXT("/Engine/Other"), TEXT("/Game/../Other")})
        Refuses(Good.Replace(TEXT("/Game/P4StreamingAutomation/CopiedOther"), Target),
            TEXT("Late invalid package target refused before first remap"));
    Refuses(Good.Replace(TEXT("\"editor_visible\":false"), TEXT("\"editor_visible\":false,\"editor_visible\":true")),
        TEXT("Duplicate row field refused"));
    Refuses(Good.Replace(TEXT("/Game/P4StreamingAutomation/CopiedOther"), TEXT("/Game/Bad\\u0000Hidden")),
        TEXT("Escaped NUL package refused"));
    Refuses(Good.Replace(
                TEXT("\"package_to_load\":\"/Game/P4StreamingAutomation/Other\""), TEXT("\"package_to_load\":null")),
        TEXT("Non-string override refused"));
    Refuses(Good.Replace(TEXT("\"package_to_load\":\"/Game/P4StreamingAutomation/Other\""),
                TEXT("\"package_to_load\":\"/Engine/Bad\"")),
        TEXT("Invalid override refused"));
    TestFalse(
        TEXT("Missing export world refused before loading flags change"), UP4AuthoringTools::LoadExportLevels(World));
    TestEqual(TEXT("Missing export retains every flag"), UP4AuthoringTools::StreamingState(World), Initial);
    TestFalse(TEXT("Missing export does not dirty package"), Package->IsDirty());
    for (const TCHAR* Reference : {TEXT("/Game/P4StreamingAutomation/Other.WrongObject"),
             TEXT("/Game/P4StreamingAutomation/Other.Other:Subobject")})
    {
        Second->SetWorldAsset(TSoftObjectPtr<UWorld>(FSoftObjectPath(Reference)));
        const FString InvalidState = UP4AuthoringTools::StreamingState(World);
        TestFalse(TEXT("Noncanonical world object reference refuses remap"),
            UP4AuthoringTools::RemapStreaming(World, Good, false, true));
        TestFalse(TEXT("Noncanonical world object reference refuses export load"),
            UP4AuthoringTools::LoadExportLevels(World));
        TestEqual(TEXT("Reference refusal retains all source settings"), UP4AuthoringTools::StreamingState(World),
            InvalidState);
        TestFalse(TEXT("Reference refusal does not dirty source"), Package->IsDirty());
        Second->SetWorldAssetByPackageName(TEXT("/Game/P4StreamingAutomation/Other"));
    }
    TestTrue(TEXT("Complete remap succeeds with surrounding whitespace"),
        UP4AuthoringTools::RemapStreaming(World, TEXT(" \n") + Good + TEXT("\r\n"), false, true));
    TestEqual(TEXT("First actual stream remapped"), First->GetWorldAssetPackageName(),
        FString(TEXT("/Game/P4StreamingAutomation/CopiedChild")));
    TestEqual(TEXT("Second target cache primed before undo"), Second->GetWorldAssetPackageName(),
        FString(TEXT("/Game/P4StreamingAutomation/CopiedOther")));
    TestTrue(TEXT("Optional empty override stays NAME_None"), First->PackageNameToLoad.IsNone());
    TestEqual(TEXT("Explicit override preserved"), Second->PackageNameToLoad,
        FName(TEXT("/Game/P4StreamingAutomation/Other")));
    TestTrue(TEXT("Native setter applies runtime flags"),
        First->ShouldBeLoaded() && First->GetShouldBeVisibleFlag() && First->GetShouldBeVisibleInEditor());
    TestTrue(TEXT("No class, transform or entry substitution"),
        World->GetStreamingLevels()[0] == First && World->GetStreamingLevels()[1] == Second &&
            First->LevelTransform.Equals(FirstTransform, 0) && Second->LevelTransform.Equals(SecondTransform, 0));
    TestTrue(TEXT("One ordinary editor undo covers both stream objects"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores all references and flags"), UP4AuthoringTools::StreamingState(World), Initial);
    TestEqual(TEXT("Undo invalidates first native package cache"), First->GetWorldAssetPackageName(),
        FString(TEXT("/Game/P4StreamingAutomation/Child")));
    TestEqual(TEXT("Undo invalidates second native package cache"), Second->GetWorldAssetPackageName(),
        FString(TEXT("/Game/P4StreamingAutomation/Other")));
    TestTrue(TEXT("Ordinary editor redo covers both objects"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores target"), First->GetWorldAssetPackageName(),
        FString(TEXT("/Game/P4StreamingAutomation/CopiedChild")));
    TestTrue(TEXT("Undo returns fixture to original"), GEditor->UndoTransaction());
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("Bake forces temporary targets and visible/load flags"),
        UP4AuthoringTools::RemapStreaming(World, Good, true, false));
    TestTrue(TEXT("Bake load override is copied target"),
        First->PackageNameToLoad == First->GetWorldAssetPackageFName() &&
            Second->PackageNameToLoad == Second->GetWorldAssetPackageFName() && Second->ShouldBeLoaded() &&
            Second->GetShouldBeVisibleFlag() && Second->GetShouldBeVisibleInEditor());
    TestFalse(TEXT("Temporary bake remap does not dirty authored package"), Package->IsDirty());
    // Delete an actual streaming entry, then pass its old row. The helper must
    // operate on today's actual list and cannot recreate the deleted child.
    World->RemoveStreamingLevel(Second);
    const FString Remaining = TEXT(
        "{\"/Game/P4StreamingAutomation/CopiedChild\":{\"target\":\"/Game/P4StreamingAutomation/FinalChild\",\"loaded\":false,\"visible\":false,\"editor_visible\":false},\"/Game/P4StreamingAutomation/CopiedOther\":{\"target\":\"/Game/P4StreamingAutomation/FinalOther\",\"loaded\":false,\"visible\":false,\"editor_visible\":false}}");
    TestTrue(TEXT("Stale deleted row does not recreate a streaming entry"),
        UP4AuthoringTools::RemapStreaming(World, Remaining, false, false));
    TestEqual(TEXT("Deleted stream remains absent"), World->GetStreamingLevels().Num(), 1);
    TestTrue(TEXT("Surviving stream retains identity"), World->GetStreamingLevels()[0] == First);
    Second->SetWorldAssetByPackageName(First->GetWorldAssetPackageFName());
    Second->PackageNameToLoad = NAME_None;
    First->SetShouldBeLoaded(false);
    First->SetShouldBeVisible(false);
    First->SetShouldBeVisibleInEditor(false);
    Second->SetShouldBeLoaded(true);
    Second->SetShouldBeVisible(true);
    Second->SetShouldBeVisibleInEditor(true);
    World->AddStreamingLevel(Second);
    Package->SetDirtyFlag(false);
    const FString RepeatedInitial = UP4AuthoringTools::StreamingState(World);
    const FString InstanceTrue = TEXT(
        "{\"loaded\":true,\"visible\":true,\"editor_visible\":true,\"package_to_load\":\"\"}");
    const FString InstanceFalse = TEXT(
        "{\"loaded\":false,\"visible\":false,\"editor_visible\":false,\"package_to_load\":\"\"}");
    const FString Instances = FString::Printf(
        TEXT(
            "{\"/Game/P4StreamingAutomation/FinalChild\":{\"target\":\"/Game/P4StreamingAutomation/RepeatedCopy\",\"instances\":{\"%s\":%s,\"%s\":%s}}}"),
        *First->GetName(), *InstanceTrue, *Second->GetName(), *InstanceFalse);
    TestTrue(TEXT("Distinct instances of one child keep independent remap flags"),
        UP4AuthoringTools::RemapStreaming(World, Instances, false, true));
    TestTrue(TEXT("First instance flags differ from second"),
        First->ShouldBeLoaded() && First->GetShouldBeVisibleFlag() && First->GetShouldBeVisibleInEditor() &&
            !Second->ShouldBeLoaded() && !Second->GetShouldBeVisibleFlag() && !Second->GetShouldBeVisibleInEditor());
    TestTrue(TEXT("Repeated child retains both actual objects and exact transforms"),
        World->GetStreamingLevels().Num() == 2 && World->GetStreamingLevels()[0] == First &&
            World->GetStreamingLevels()[1] == Second && First->LevelTransform.Equals(FirstTransform, 0) &&
            Second->LevelTransform.Equals(SecondTransform, 0));
    TestTrue(TEXT("Repeated instance remap undo"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores differing original instance states"), UP4AuthoringTools::StreamingState(World),
        RepeatedInitial);
    Package->SetDirtyFlag(false);
    const auto RefusesInstances = [&](const FString& Json, const TCHAR* Label)
    {
        TestFalse(Label, UP4AuthoringTools::RemapStreaming(World, Json, false, true));
        TestEqual(TEXT("Invalid instance map retains both complete original states"),
            UP4AuthoringTools::StreamingState(World), RepeatedInitial);
        TestFalse(TEXT("Invalid instance map does not dirty"), Package->IsDirty());
    };
    RefusesInstances(Instances.Replace(*Second->GetName(), TEXT("MissingInstance")),
        TEXT("Missing current plus extra unknown instance refused"));
    RefusesInstances(Instances.Replace(*(TEXT(",\"") + Second->GetName() + TEXT("\":") + InstanceFalse), TEXT("")),
        TEXT("Missing second instance refused"));
    RefusesInstances(
        Instances.Replace(TEXT("\"instances\":{\""),
            TEXT("\"instances\":{\"ExtraInstance\":{\"loaded\":false,\"visible\":false,\"editor_visible\":false},\"")),
        TEXT("Extra instance identity refused"));
    RefusesInstances(Instances.Replace(*InstanceFalse, TEXT("{\"loaded\":false,\"visible\":false}")),
        TEXT("Late incomplete instance row refused before first changes"));
    UWorld* ForeignWorld = NewObject<UWorld>(GetTransientPackage());
    ULevelStreamingDynamic* Foreign = NewObject<ULevelStreamingDynamic>(ForeignWorld);
    Foreign->SetWorldAssetByPackageName(First->GetWorldAssetPackageFName());
    const FString ForeignBefore = Foreign->GetWorldAssetPackageName();
    // Deliberately malformed input bypasses AddStreamingLevel's ownership
    // ensure. Production APIs must refuse it before touching either world.
    auto& ActualStreams = const_cast<TArray<ULevelStreaming*>&>(World->GetStreamingLevels());
    ActualStreams.Add(Foreign);
    TestFalse(TEXT("Foreign world-owned stream reference cannot be remapped"),
        UP4AuthoringTools::RemapStreaming(World, Remaining, false, false));
    TestEqual(TEXT("Foreign source reference remains unchanged"), Foreign->GetWorldAssetPackageName(), ForeignBefore);
    TestFalse(TEXT("Foreign stream export load refused"), UP4AuthoringTools::LoadExportLevels(World));
    ActualStreams.RemoveSingle(Foreign);
    World->RemoveStreamingLevel(Second);
    World->RemoveStreamingLevel(First);
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("Empty actual list accepts empty plan"),
        UP4AuthoringTools::RemapStreaming(World, TEXT("{}"), false, true));
    TestFalse(TEXT("Empty remap creates no dirty authoring package"), Package->IsDirty());
    TestFalse(TEXT("Null world remap refused"), UP4AuthoringTools::RemapStreaming(nullptr, Good, false));
    TestFalse(TEXT("Null export load refused"), UP4AuthoringTools::LoadExportLevels(nullptr));
    TestFalse(TEXT("Null export selection refused"), UP4AuthoringTools::SetExportLevel(nullptr, TEXT("/Game/Map")));
    TestFalse(TEXT("Invalid export selection refused"), UP4AuthoringTools::SetExportLevel(World, TEXT("/Engine/Map")));
    TestFalse(
        TEXT("Absent actual export level refused"), UP4AuthoringTools::SetExportLevel(World, TEXT("/Game/Missing")));
    TestFalse(TEXT("Null export save refused"), UP4AuthoringTools::SaveExportWorld(nullptr, TEXT("/Game/Map")));
    TestFalse(TEXT("World without actual persistent level cannot be saved"),
        UP4AuthoringTools::SaveExportWorld(World, TEXT("/Game/Map")));
    const bool ThreadGuard = Async(EAsyncExecution::Thread,
        [World, Good]()
        {
            return UP4AuthoringTools::StreamingState(World).IsEmpty() &&
                !UP4AuthoringTools::RemapStreaming(World, Good, false) && !UP4AuthoringTools::LoadExportLevels(World) &&
                !UP4AuthoringTools::SetExportLevel(World, TEXT("/Game/Map")) &&
                !UP4AuthoringTools::SaveExportWorld(World, TEXT("/Game/Map"));
        }).Get();
    TestTrue(TEXT("Every UObject streaming helper refuses worker thread calls"), ThreadGuard);
    return true;
}
#endif

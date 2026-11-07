#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Serialization/ObjectWriter.h"

namespace P4StreamingBoundaryCoverage
{
struct FFixture
{
    UPackage* Package;
    UWorld* World;
    ULevelStreamingDynamic* Stream;
    TArray<uint8> Before;
    FFixture()
    {
        Package = CreatePackage(
            *(TEXT("/Game/P4StreamingBoundary/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        World = NewObject<UWorld>(Package);
        Stream = NewObject<ULevelStreamingDynamic>(World, TEXT("Primary"));
        Stream->SetWorldAssetByPackageName(TEXT("/Game/P4StreamingBoundary/Child"));
        Stream->SetShouldBeLoaded(false);
        Stream->SetShouldBeVisible(false);
        Stream->SetShouldBeVisibleInEditor(true);
        Stream->LevelTransform = FTransform(FRotator(4, 89, 27), FVector(12, -45, 67), FVector(1, 3, 2));
        World->AddStreamingLevel(Stream);
        Package->SetDirtyFlag(false);
        Before = Snapshot();
    }
    TArray<uint8> Snapshot() const
    {
        TArray<uint8> Bytes;
        FObjectWriter Writer(Stream, Bytes, false, false, false, PPF_DuplicateVerbatim);
        return Bytes;
    }
    bool Unchanged() const
    {
        return Snapshot() == Before && World->GetStreamingLevels().Num() == 1 &&
            World->GetStreamingLevels()[0] == Stream && Stream->GetOuter() == World && !Package->IsDirty();
    }
    FString Map(const FString& Row) const { return TEXT("{\"/Game/P4StreamingBoundary/Child\":") + Row + TEXT("}"); }
};
const TCHAR* Flags = TEXT("{\"loaded\":true,\"visible\":false,\"editor_visible\":true}");
const TCHAR* GoodRow = TEXT(
    "{\"target\":\"/Game/P4StreamingBoundary/Copy\",\"loaded\":true,\"visible\":false,\"editor_visible\":true}");
} //namespace P4StreamingBoundaryCoverage

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingInstanceShapeRefusals, "P4MapKit.Quality.StreamingInstanceShapeRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingInstanceShapeRefusals::RunTest(const FString& Parameters)
{
    using namespace P4StreamingBoundaryCoverage;
    FFixture Fixture;
    TestTrue(TEXT("Native fixture starts unchanged"), Fixture.Unchanged());
    const auto Refuses = [&](const FString& Row, const TCHAR* Label)
    {
        TestFalse(Label, UP4AuthoringTools::RemapStreaming(Fixture.World, Fixture.Map(Row), false, true));
        TestTrue(
            TEXT("Refusal preserves exact native serialized stream, owner, list and dirty state"), Fixture.Unchanged());
    };
    for (const TCHAR* Value : {TEXT("null"), TEXT("false"), TEXT("7"), TEXT("[]"), TEXT("\"row\"")})
        Refuses(Value, TEXT("Non-object source row refuses"));
    for (const TCHAR* Value : {TEXT("null"), TEXT("false"), TEXT("7"), TEXT("[]"), TEXT("\"instances\"")})
        Refuses(FString(TEXT("{\"target\":\"/Game/P4StreamingBoundary/Copy\",\"instances\":")) + Value + TEXT("}"),
            TEXT("Non-object instance inventory refuses"));
    const auto Instance = [&](const FString& Values)
    { return FString(TEXT("{\"target\":\"/Game/P4StreamingBoundary/Copy\",\"instances\":")) + Values + TEXT("}"); };
    for (const TCHAR* Name : {TEXT(""), TEXT("None")})
        Refuses(Instance(FString(TEXT("{\"")) + Name + TEXT("\":") + Flags + TEXT("}")),
            TEXT("Empty or NAME_None instance identity refuses"));
    Refuses(Instance(FString(TEXT("{\"Primary\":")) + Flags + TEXT(",\"PRIMARY\":") + Flags + TEXT("}")),
        TEXT("Native FName instance collision refuses"));
    for (const TCHAR* Value : {TEXT("null"), TEXT("false"), TEXT("7"), TEXT("[]"), TEXT("\"flags\"")})
        Refuses(
            Instance(FString(TEXT("{\"Primary\":")) + Value + TEXT("}")), TEXT("Non-object instance state refuses"));
    for (const TCHAR* Value :
        {TEXT("null"), TEXT("false"), TEXT("7"), TEXT("[]"), TEXT("{}"), TEXT("\"/Game/P4StreamingBoundary/Wrong\"")})
        Refuses(Instance(FString(TEXT(
                             "{\"Primary\":{\"loaded\":true,\"visible\":false,\"editor_visible\":true,\"target\":")) +
                    Value + TEXT("}}")),
            TEXT("Instance target must match the parent target"));
    FString Nul = Fixture.Map(GoodRow) + TEXT("Xtail");
    const int32 Offset = Nul.Len() - 5;
    Nul[Offset] = 0;
    TestTrue(TEXT("Literal NUL fixture retains trailing code units"), Nul[Offset] == 0 && Nul.Len() > Offset + 1);
    TestFalse(TEXT("Literal NUL mapping refuses"), UP4AuthoringTools::RemapStreaming(Fixture.World, Nul, false, true));
    TestTrue(TEXT("Literal NUL refusal retains exact native stream"), Fixture.Unchanged());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingNativeInventoryRefusals,
    "P4MapKit.Quality.StreamingNativeInventoryRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingNativeInventoryRefusals::RunTest(const FString& Parameters)
{
    using namespace P4StreamingBoundaryCoverage;
    FFixture Fixture;
    auto& Streams = const_cast<TArray<ULevelStreaming*>&>(Fixture.World->GetStreamingLevels());
    for (ULevelStreaming* Extra :
        {static_cast<ULevelStreaming*>(nullptr), static_cast<ULevelStreaming*>(Fixture.Stream)})
    {
        Streams.Insert(Extra, 0);
        TestFalse(TEXT("Malformed actual stream list refuses remap"),
            UP4AuthoringTools::RemapStreaming(Fixture.World, Fixture.Map(GoodRow), false, true));
        TestTrue(TEXT("Refusal preserves the exact malformed list"),
            Streams.Num() == 2 && Streams[0] == Extra && Streams[1] == Fixture.Stream);
        if (!Extra)
            TestFalse(TEXT("Null native list entry refuses export loading"),
                UP4AuthoringTools::LoadExportLevels(Fixture.World));
        Streams.RemoveAt(0);
        TestTrue(TEXT("Native stream fields and source package remain untouched"), Fixture.Unchanged());
    }
    Fixture.Stream->SetIsRequestingUnloadAndRemoval(true);
    const TArray<uint8> Requested = Fixture.Snapshot();
    TestFalse(TEXT("Real pending removal refuses remap"),
        UP4AuthoringTools::RemapStreaming(Fixture.World, Fixture.Map(GoodRow), false, true));
    TestFalse(TEXT("Real pending removal refuses export loading"), UP4AuthoringTools::LoadExportLevels(Fixture.World));
    TestTrue(TEXT("Refusal retains native pending-removal state exactly"),
        Fixture.Snapshot() == Requested && Fixture.Stream->GetIsRequestingUnloadAndRemoval());
    Fixture.Stream->SetIsRequestingUnloadAndRemoval(false);
    Fixture.World->PersistentLevel = NewObject<ULevel>(Fixture.World);
    TestFalse(TEXT("External export package refuses"),
        UP4AuthoringTools::SaveExportWorld(Fixture.World, TEXT("/Engine/P4StreamingBoundary")));
    TestFalse(TEXT("Actual source package cannot be export destination"),
        UP4AuthoringTools::SaveExportWorld(Fixture.World, Fixture.Package->GetName()));
    TestFalse(TEXT("Unsaved native source package refuses export"),
        UP4AuthoringTools::SaveExportWorld(Fixture.World, TEXT("/Game/P4StreamingBoundary/UnsavedCopy")));
    TestTrue(TEXT("Save preflight failures preserve exact native stream and clean source"), Fixture.Unchanged());
    return !HasAnyErrors();
}
#endif

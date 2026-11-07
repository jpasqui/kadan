#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS)
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Async/Async.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WorldExportGraphTest, "P4MapKit.Quality.WorldExportGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WorldExportGraphTest::RunTest(const FString&)
{
    TArray<TStrongObjectPtr<UWorld>> Worlds;
    auto MakeWorld = [&Worlds](const TCHAR* Name)
    {
        const FString PackageName = TEXT("/Game/P4StreamingGraphFixture/") + FString(Name);
        UPackage* Package = CreatePackage(*PackageName);
        UWorld* World = NewObject<UWorld>(Package, FName(Name));
        World->PersistentLevel = NewObject<ULevel>(World, TEXT("PersistentLevel"));
        Worlds.Emplace(World);
        Package->SetDirtyFlag(false);
        return World;
    };
    UWorld* Root = MakeWorld(TEXT("Root"));
    UWorld* A = MakeWorld(TEXT("A"));
    UWorld* B = MakeWorld(TEXT("B"));
    UWorld* Shared = MakeWorld(TEXT("Shared"));
    auto AddStream = [](UWorld* Parent, UWorld* Child)
    {
        auto Stream = NewObject<ULevelStreamingDynamic>(Parent);
        Stream->SetWorldAssetByPackageName(Child->GetOutermost()->GetFName());
        Stream->SetShouldBeLoaded(false);
        Stream->SetShouldBeVisible(false);
        Stream->LevelTransform = FTransform(FRotator(11, 22, 33), FVector(600, 100, -25), FVector(2, 3, 4));
        Parent->AddStreamingLevel(Stream);
        return Stream;
    };
    ULevelStreaming* First = AddStream(Root, A);
    AddStream(Root, B);
    AddStream(Root, A); // distinct instances, same World
    AddStream(A, Shared);
    AddStream(B, Shared);
    const FString Target = TEXT("/Game/P4StreamingGraphFixture/Export");
    const FString Before = UP4AuthoringTools::StreamingState(Root);
    auto Decode = [this](const FString& Json)
    {
        TSharedPtr<FJsonObject> Object;
        TestTrue(TEXT("Native graph always returns JSON"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object));
        return Object;
    };
    auto Result = Decode(UP4AuthoringTools::NativeWorldStreamingGraph(Root, Target));
    if (!Result.IsValid())
        return false;
    TestTrue(TEXT("Shared descendants and repeated instances are supported"), Result->GetBoolField(TEXT("valid")));
    const auto Graph = Result->GetObjectField(TEXT("graph"));
    TestEqual(TEXT("Actual World graph has four unique Worlds"), Graph->Values.Num(), 4);
    const auto RootNode = Graph->GetObjectField(Root->GetOutermost()->GetName());
    TestEqual(TEXT("Root destination is exact"), RootNode->GetStringField(TEXT("target")), Target);
    TestEqual(TEXT("Repeated authored instances stay ordered"), RootNode->GetArrayField(TEXT("streams")).Num(), 3);
    TestEqual(
        TEXT("Planning does not change streaming flags/overrides"), UP4AuthoringTools::StreamingState(Root), Before);
    TestTrue(TEXT("Planning preserves authored transform"),
        First->LevelTransform.Equals(FTransform(FRotator(11, 22, 33), FVector(600, 100, -25), FVector(2, 3, 4))));
    for (const auto& World : Worlds)
        TestFalse(TEXT("Planning leaves authoring packages clean"), World->GetOutermost()->IsDirty());
    auto Refuse = [this, &Decode](UWorld* World, const FString& Destination, const TCHAR* Reason)
    {
        const auto Verdict = Decode(UP4AuthoringTools::NativeWorldStreamingGraph(World, Destination));
        if (!Verdict.IsValid())
            return;
        TestFalse(Reason, Verdict->GetBoolField(TEXT("valid")));
        TestFalse(TEXT("Refusal does not return a partial graph"), Verdict->HasField(TEXT("graph")));
        FString Error;
        TestTrue(FString(Reason) + TEXT(": refusal explains the unsupported input"),
            Verdict->TryGetStringField(TEXT("error"), Error) && !Error.IsEmpty());
    };
    Refuse(nullptr, Target, TEXT("Missing World refused"));
    Refuse(Root, Root->GetOutermost()->GetName(), TEXT("Source cannot be overwritten"));
    Refuse(Root, TEXT("/Engine/NoExport"), TEXT("Non-project destination refused"));
    Refuse(Root, TEXT("/Game/Bad.Object"), TEXT("Object path is not package path"));
    FString NulTarget = Target + TEXT("_tail");
    // FString::AppendChar(0) is intentionally a no-op in UE4.26.
    NulTarget.GetCharArray()[Target.Len()] = 0;
    Refuse(Root, NulTarget, TEXT("NUL destination refused"));
    Refuse(Root, A->GetOutermost()->GetName(), TEXT("Destination cannot replace an authored descendant"));
    First->PackageNameToLoad = TEXT("/Game/P4StreamingGraphFixture/UnqualifiedOverride");
    Refuse(Root, Target, TEXT("Different package override cannot be silently discarded"));
    First->PackageNameToLoad = NAME_None;
    First->SetWorldAsset(TSoftObjectPtr<UWorld>(FSoftObjectPath(A->GetOutermost()->GetName() + TEXT(".WrongObject"))));
    Refuse(Root, Target, TEXT("A package match does not admit the wrong World object"));
    First->SetWorldAsset(TSoftObjectPtr<UWorld>(FSoftObjectPath(A->GetPathName() + TEXT(":Subobject"))));
    Refuse(Root, Target, TEXT("A World subobject is not a streaming World asset"));
    First->SetWorldAssetByPackageName(TEXT("/Game/P4StreamingGraphFixture/DeletedWorld"));
    Refuse(Root, Target, TEXT("Deleted streaming World is not resurrected"));
    First->SetWorldAssetByPackageName(A->GetOutermost()->GetFName());
    auto Back = AddStream(Shared, Root);
    Refuse(Root, Target, TEXT("Cycles refused"));
    Shared->RemoveStreamingLevel(Back);
    // Deliberately corrupt the reflected array without invoking UE's streaming
    // callbacks, which require non-null entries even when setting the array.
    auto& Rows = const_cast<TArray<ULevelStreaming*>&>(Root->GetStreamingLevels());
    Rows.Add(nullptr);
    Refuse(Root, Target, TEXT("Null streaming entry is not silently dropped"));
    Rows.Pop();
    auto Worker = Async(EAsyncExecution::ThreadPool,
        [Root, Target] { return UP4AuthoringTools::NativeWorldStreamingGraph(Root, Target); });
    const auto WorkerResult = Decode(Worker.Get());
    TestFalse(TEXT("Worker refuses before touching UObjects"), WorkerResult->GetBoolField(TEXT("valid")));
    TestEqual(
        TEXT("Refusal leaves original streaming state unchanged"), UP4AuthoringTools::StreamingState(Root), Before);
    return true;
}
#endif

#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"

#include "P4StreamingSavedWorldShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingSavedGraphAdmission, "P4MapKit.Quality.StreamingSavedGraphAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingSavedGraphAdmission::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingSavedWorldCoverage;
    FP4StreamingSavedWorldFixture Fixture;
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    UWorld* Nested = Fixture.Make(TEXT("Nested"));
    UWorld* Grandchild = Fixture.Make(TEXT("Grandchild"));
    Fixture.Add(Nested, Grandchild, TEXT("NestedChild"));
    if (!TestTrue(TEXT("Real native leaf map saved"), Fixture.Save(Leaf)) ||
        !TestTrue(TEXT("Real native nested map saved"), Fixture.Save(Nested)))
        return false;
    ULevelStreamingDynamic* First = Fixture.Add(Parent, Leaf, TEXT("First"));
    ULevelStreamingDynamic* Late = Fixture.Add(Parent, Nested, TEXT("Late"));
    for (UWorld* World : Fixture.Worlds)
        World->GetOutermost()->SetDirtyFlag(false);
    const auto Refuses = [&](const TCHAR* Label)
    {
        TArray<FWorldState> Before;
        for (UWorld* World : Fixture.Worlds)
            Before.Emplace(World);
        const FDiskState Disk(Fixture.Base);
        TestFalse(Label, UP4AuthoringTools::LoadExportLevels(Parent));
        for (const FWorldState& State : Before)
            TestTrue(
                TEXT("Refusal retains whole native graph, objects, fields, metadata and dirty flags"), State.Same());
        TestTrue(TEXT("Refusal preserves exact Content file set and bytes"), Disk.Same());
        TestNull(TEXT("Earlier valid child remains unloaded after late refusal"), First->GetLoadedLevel());
    };
    Late->PackageNameToLoad = Leaf->GetOutermost()->GetFName();
    Refuses(TEXT("Late genuine override disagreement refuses before first child loads"));
    Late->PackageNameToLoad = NAME_None;
    Refuses(TEXT("Saved direct child with its own real streaming child refuses flattening"));
    // An explicit matching override is accepted by preflight, unlike the
    // disagreement above; ordinary UE load supplies LoadedLevel and ownership.
    Parent->RemoveStreamingLevel(Late);
    First->PackageNameToLoad = Leaf->GetOutermost()->GetFName();
    TestTrue(
        TEXT("Saved native direct child loads with matching override"), UP4AuthoringTools::LoadExportLevels(Parent));
    TestTrue(TEXT("Engine supplies exact leaf level and parent membership"),
        First->GetLoadedLevel() == Leaf->PersistentLevel && Parent->GetLevels().Contains(Leaf->PersistentLevel) &&
            Leaf->PersistentLevel->OwningWorld == Parent);
    TestTrue(TEXT("Export loading forces three native flags"),
        First->ShouldBeLoaded() && First->GetShouldBeVisibleFlag() && First->GetShouldBeVisibleInEditor());
    TestFalse(TEXT("Ordinary export loading does not dirty parent"), Parent->GetOutermost()->IsDirty());
    TestFalse(TEXT("Ordinary export loading does not dirty leaf"), Leaf->GetOutermost()->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingLoadedNativeRemap, "P4MapKit.Quality.StreamingLoadedNativeRemap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingLoadedNativeRemap::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingSavedWorldCoverage;
    FP4StreamingSavedWorldFixture Fixture;
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("AuthoredPose");
    Spawn.ObjectFlags = RF_Transactional;
    AActor* Actor = Leaf->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Genuine authored native actor created"), Actor))
        return false;
    USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("AuthoredRoot"), RF_Transactional);
    Actor->AddInstanceComponent(Root);
    if (!TestTrue(TEXT("Native root attaches to its actual owning actor"), Actor->SetRootComponent(Root)))
        return false;
    Root->SetRelativeLocation(FVector(73, -11, 19));
    Root->SetRelativeRotation(FRotator(21, 48, -13));
    Root->SetRelativeScale3D(FVector(2, 3, 4));
    Root->RegisterComponent();
    if (!TestTrue(TEXT("Native direct child saved"), Fixture.Save(Leaf)))
        return false;
    ULevelStreamingDynamic* Stream = Fixture.Add(Parent, Leaf, TEXT("AuthoredChild"));
    if (!TestTrue(TEXT("Ordinary native loading supplies leaf"), UP4AuthoringTools::LoadExportLevels(Parent)) ||
        !TestTrue(TEXT("Actual child level identity"), Stream->GetLoadedLevel() == Leaf->PersistentLevel))
        return false;
    const FTransform Transform = Stream->LevelTransform;
    const FString Path = Stream->GetPathName();
    const FString ActorPath = Actor->GetPathName();
    const FVector Location = Root->GetRelativeLocation(), Scale = Root->GetRelativeScale3D();
    const FRotator Rotation = Root->GetRelativeRotation();
    const TArray<uint8> ParentMetadata = Bytes(Parent->GetOutermost()->GetMetaData()),
                        LeafMetadata = Bytes(Leaf->GetOutermost()->GetMetaData());
    const FDiskState Disk(Fixture.Base);
    Parent->GetOutermost()->SetDirtyFlag(false);
    Leaf->GetOutermost()->SetDirtyFlag(false);
    TestTrue(TEXT("Same-target remap hides actual loaded level temporarily"),
        UP4AuthoringTools::RemapStreaming(Parent, SameTarget(Stream, false), false, false));
    TestTrue(TEXT("Same target preserves actual loaded-level identity while hiding"),
        Stream->GetLoadedLevel() == Leaf->PersistentLevel && !Leaf->PersistentLevel->bIsVisible &&
            !Stream->GetShouldBeVisibleFlag() && !Stream->GetShouldBeVisibleInEditor());
    TestTrue(TEXT("Native actor hides with its actual level"), Actor->bHiddenEdLevel);
    TestTrue(TEXT("Same-target remap shows same actual loaded level"),
        UP4AuthoringTools::RemapStreaming(Parent, SameTarget(Stream, true), false, false));
    TestTrue(TEXT("Native visibility restored without level substitution"),
        Stream->GetLoadedLevel() == Leaf->PersistentLevel && Leaf->PersistentLevel->bIsVisible);
    TestFalse(TEXT("Native actor is visible again in its actual level"), Actor->bHiddenEdLevel);
    const FString Target = Fixture.Base + TEXT("/CopiedLeaf");
    const FString Mapping = FString::Printf(
        TEXT("{\"%s\":{\"target\":\"%s\",\"loaded\":false,\"visible\":false,\"editor_visible\":false}}"),
        *Leaf->GetOutermost()->GetName(), *Target);
    TestTrue(TEXT("Changed target clears actual loaded level through public engine API"),
        UP4AuthoringTools::RemapStreaming(Parent, Mapping, false, false));
    TestNull(TEXT("Prior level is no longer the stream's current loaded level"), Stream->GetLoadedLevel());
    TestEqual(TEXT("Changed native world-asset cache is exact"), Stream->GetWorldAssetPackageName(), Target);
    TestEqual(TEXT("Default load-package override follows changed target"), Stream->PackageNameToLoad, FName(*Target));
    TestTrue(TEXT("Second same-target remap accepts already cleared native level"),
        UP4AuthoringTools::RemapStreaming(Parent, SameTarget(Stream, false), false, false));
    TestNull(TEXT("Second remap cannot substitute or reacquire an unloaded native level"), Stream->GetLoadedLevel());
    TestTrue(TEXT("Second remap uses independent requested runtime flags"),
        Stream->ShouldBeLoaded() && !Stream->GetShouldBeVisibleFlag() && !Stream->GetShouldBeVisibleInEditor());
    TestTrue(TEXT("Remaps retain owner, object path, array identity and authored transform"),
        Stream->GetOuter() == Parent && Stream->GetPathName() == Path && Parent->GetStreamingLevels().Num() == 1 &&
            Parent->GetStreamingLevels()[0] == Stream && Stream->LevelTransform.Equals(Transform, 0));
    TestTrue(TEXT("Remaps preserve actual authored actor, root, package and literal pose"),
        Actor->GetRootComponent() == Root && Root->GetOwner() == Actor && Actor->GetLevel() == Leaf->PersistentLevel &&
            Actor->GetPathName() == ActorPath && Root->GetRelativeLocation() == Location &&
            Root->GetRelativeRotation() == Rotation && Root->GetRelativeScale3D() == Scale);
    TestTrue(TEXT("Remaps preserve exact complete native package metadata"),
        Bytes(Parent->GetOutermost()->GetMetaData()) == ParentMetadata &&
            Bytes(Leaf->GetOutermost()->GetMetaData()) == LeafMetadata);
    TestFalse(TEXT("Temporary remap leaves parent clean"), Parent->GetOutermost()->IsDirty());
    TestFalse(TEXT("Temporary visibility leaves source leaf clean"), Leaf->GetOutermost()->IsDirty());
    TestTrue(TEXT("Remapping creates no Content publication or source-byte change"), Disk.Same());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingActualLevelSelection, "P4MapKit.Quality.StreamingActualLevelSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingActualLevelSelection::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingSavedWorldCoverage;
    FP4StreamingSavedWorldFixture Fixture;
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    if (!TestTrue(TEXT("Native leaf map saved"), Fixture.Save(Leaf)))
        return false;
    ULevelStreamingDynamic* Stream = Fixture.Add(Parent, Leaf, TEXT("Child"));
    if (!TestTrue(TEXT("Real child becomes actual world level"), UP4AuthoringTools::LoadExportLevels(Parent)))
        return false;
    const FDiskState Disk(Fixture.Base);
    const TArray<ULevel*> Levels = Parent->GetLevels();
    const TArray<ULevelStreaming*> Streams = Parent->GetStreamingLevels();
    TestTrue(TEXT("Select exact actual loaded child package"),
        UP4AuthoringTools::SetExportLevel(Parent, Leaf->GetOutermost()->GetName()));
    TestTrue(TEXT("Native current level is exact child pointer"), Parent->GetCurrentLevel() == Leaf->PersistentLevel);
    TestTrue(TEXT("Repeated current-level selection accepts same native pointer"),
        UP4AuthoringTools::SetExportLevel(Parent, Leaf->GetOutermost()->GetName()));
    TestTrue(TEXT("Select native persistent package"),
        UP4AuthoringTools::SetExportLevel(Parent, Parent->GetOutermost()->GetName()));
    TestTrue(TEXT("Native persistent pointer restored"), Parent->GetCurrentLevel() == Parent->PersistentLevel);
    const FWorldState ParentBefore(Parent), LeafBefore(Leaf);
    TestFalse(TEXT("Well formed absent package refuses after scanning actual levels"),
        UP4AuthoringTools::SetExportLevel(Parent, Fixture.Base + TEXT("/Absent")));
    TestTrue(TEXT("Selection refusal preserves whole native parent and leaf graph"),
        ParentBefore.Same() && LeafBefore.Same());
    TestTrue(TEXT("Selection preserves native levels and streaming inventories"),
        Parent->GetLevels() == Levels && Parent->GetStreamingLevels() == Streams &&
            Stream->GetLoadedLevel() == Leaf->PersistentLevel);
    TestTrue(TEXT("Selection leaves Content publication and source bytes untouched"), Disk.Same());
    return !HasAnyErrors();
}
#endif

// Include at the END of P4StreamingSavedWorldCoverageAutomation.cpp: reuses
// that already native-verified owned fixture, complete snapshots and disk oracle.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Linker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "P4StreamingWorldGraphShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WorldGraphSavedAssetNameBoundary,
    "P4MapKit.Quality.WorldGraphSavedAssetNameBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WorldGraphSavedAssetNameBoundary::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4WorldGraphNativeBranches;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* World = Fixture.Make(TEXT("Root"));
    // A public native asset rename is legal UE authoring state, although this
    // exporter only supports canonical package.asset names. No private flag or
    // internal schema is changed to reach this outcome.
    if (!TestTrue(TEXT("Public engine rename supplies genuine noncanonical authored World name"),
            World->Rename(
                TEXT("AuthoredAlias"), World->GetOuter(), REN_DontCreateRedirectors | REN_NonTransactional)) ||
        !TestTrue(TEXT("Actual renamed World saved and inventoried by native registry"),
            Registry.SaveAndRegister(Fixture, World)))
        return false;
    const FPreservedGraph Before(Fixture, Registry);
    ExactRefusal(*this, Verdict(*this, World, Fixture.Base + TEXT("/Export")),
        TEXT("Streaming reference is not its /Game World asset: ") + World->GetOutermost()->GetName());
    Before.Check(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WorldGraphSavedDepthBoundary, "P4MapKit.Quality.WorldGraphSavedDepthBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WorldGraphSavedDepthBoundary::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4WorldGraphNativeBranches;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    // Exactly 129 tiny native Worlds qualify the documented nesting boundary.
    // No large-entry/4097-World stress, hash collision search or disk copies.
    for (int32 Index = 0; Index < 129; ++Index)
        Fixture.Make(*FString::Printf(TEXT("World%03d"), Index));
    for (int32 Index = 0; Index < 127; ++Index)
        Fixture.Add(Fixture.Worlds[Index], Fixture.Worlds[Index + 1], TEXT("AuthoredChild"));
    for (UWorld* World : Fixture.Worlds)
        if (!TestTrue(TEXT("Actual bounded native map saved and registered"), Registry.SaveAndRegister(Fixture, World)))
            return false;
    const FString Target = Fixture.Base + TEXT("/Export");
    {
        const FPreservedGraph Before(Fixture, Registry);
        const auto Result = Verdict(*this, Fixture.Worlds[0], Target);
        if (!Result.IsValid())
            return false;
        bool Valid = false;
        TestTrue(TEXT("Exactly 128 native nesting levels are accepted"),
            Result->TryGetBoolField(TEXT("valid"), Valid) && Valid);
        const TSharedPtr<FJsonObject>* Graph = nullptr;
        const bool HasGraph = Result->TryGetObjectField(TEXT("graph"), Graph) && Graph && Graph->IsValid();
        TestTrue(
            TEXT("Accepted graph contains exactly 128 authored Worlds"), HasGraph && (*Graph)->Values.Num() == 128);
        if (HasGraph)
        {
            TestEqual(TEXT("Actual root's planned destination equals the requested package"),
                (*Graph)->GetObjectField(Fixture.Worlds[0]->GetOutermost()->GetName())->GetStringField(TEXT("target")),
                Target);
            for (int32 Index = 0; Index < 128; ++Index)
            {
                TestTrue(TEXT("Every actual source World identity is present"),
                    (*Graph)->HasTypedField<EJson::Object>(Fixture.Worlds[Index]->GetOutermost()->GetName()));
                const auto Node = (*Graph)->GetObjectField(Fixture.Worlds[Index]->GetOutermost()->GetName());
                const auto& Rows = Node->GetArrayField(TEXT("streams"));
                TestEqual(TEXT("Exact linear native graph edge inventory"), Rows.Num(), Index < 127 ? 1 : 0);
                if (Index < 127 && Rows.Num() == 1)
                {
                    const auto Row = Rows[0]->AsObject();
                    TestEqual(TEXT("Every linear edge names its actual next World"),
                        Row->GetStringField(TEXT("package")), Fixture.Worlds[Index + 1]->GetOutermost()->GetName());
                    TestEqual(TEXT("Every edge retains actual authored stream identity"),
                        Row->GetStringField(TEXT("stream_name")), FString(TEXT("AuthoredChild")));
                    TestTrue(TEXT("Inspection did not load a child or create a load override"),
                        Row->GetStringField(TEXT("loaded_package")).IsEmpty() &&
                            Row->GetStringField(TEXT("package_to_load")).IsEmpty());
                    TestTrue(TEXT("All three authored unloaded/hidden flags remain false in plan"),
                        !Row->GetBoolField(TEXT("loaded")) && !Row->GetBoolField(TEXT("visible")) &&
                            !Row->GetBoolField(TEXT("editor_visible")));
                }
            }
        }
        Before.Check(*this);
    }
    Fixture.Add(Fixture.Worlds[127], Fixture.Worlds[128], TEXT("AuthoredChild"));
    if (!TestTrue(
            TEXT("Actual new authored child is saved before depth inspection"), Fixture.Save(Fixture.Worlds[127])))
        return false;
    {
        const FPreservedGraph Before(Fixture, Registry);
        ExactRefusal(*this, Verdict(*this, Fixture.Worlds[0], Target),
            TEXT("Streaming graph exceeds the supported 4096 Worlds or 128 nesting levels"));
        Before.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WorldGraphLoadedReferenceTransition,
    "P4MapKit.Quality.WorldGraphLoadedReferenceTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WorldGraphLoadedReferenceTransition::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4WorldGraphNativeBranches;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Root = Fixture.Make(TEXT("Root"));
    UWorld* Old = Fixture.Make(TEXT("Old"));
    UWorld* Next = Fixture.Make(TEXT("Next"));
    ULevelStreamingDynamic* Stream = Fixture.Add(Root, Old, TEXT("AuthoredChild"));
    for (UWorld* World : Fixture.Worlds)
        if (!TestTrue(TEXT("Real native world saved and registered"), Registry.SaveAndRegister(Fixture, World)))
            return false;
    if (!TestTrue(
            TEXT("Public native export loader supplies actual old child"), UP4AuthoringTools::LoadExportLevels(Root)) ||
        !TestTrue(TEXT("Actual loaded child pointer and ownership match"),
            Stream->GetLoadedLevel() == Old->PersistentLevel && Old->PersistentLevel->OwningWorld == Root &&
                Root->GetLevels().Contains(Old->PersistentLevel)))
        return false;
    const FString Target = Fixture.Base + TEXT("/Export");
    Stream->PackageNameToLoad = Old->GetOutermost()->GetFName();
    {
        const FPreservedGraph Before(Fixture, Registry);
        const auto Result = Verdict(*this, Root, Target);
        if (!Result.IsValid())
            return false;
        bool Valid = false;
        TestTrue(TEXT("Genuine matching loaded package and explicit override are accepted"),
            Result->TryGetBoolField(TEXT("valid"), Valid) && Valid);
        const TSharedPtr<FJsonObject>* Graph = nullptr;
        const bool HasGraph = Result->TryGetObjectField(TEXT("graph"), Graph) && Graph && Graph->IsValid();
        TestTrue(
            TEXT("Loaded native graph retains exactly parent and old child"), HasGraph && (*Graph)->Values.Num() == 2);
        if (HasGraph)
        {
            const auto Node = (*Graph)->GetObjectField(Root->GetOutermost()->GetName());
            const auto& Rows = Node->GetArrayField(TEXT("streams"));
            TestEqual(TEXT("Actual native stream inventory remains one row"), Rows.Num(), 1);
            if (Rows.Num() == 1)
            {
                const auto Row = Rows[0]->AsObject();
                TestEqual(TEXT("Exact actual loaded package is reported"), Row->GetStringField(TEXT("loaded_package")),
                    Old->GetOutermost()->GetName());
                TestEqual(TEXT("Exact native authored override is reported"),
                    Row->GetStringField(TEXT("package_to_load")), Old->GetOutermost()->GetName());
                TestEqual(TEXT("Actual stream identity is retained"), Row->GetStringField(TEXT("stream_name")),
                    Stream->GetName());
            }
        }
        Before.Check(*this);
    }
    // Native public reference editing invalidates package caches but deliberately
    // leaves the old LoadedLevel until the engine processes the transition.
    Stream->PackageNameToLoad = NAME_None;
    Stream->SetWorldAssetByPackageName(Next->GetOutermost()->GetFName());
    if (!TestTrue(TEXT("Public reference edit retains actual old loaded level before next engine update"),
            Stream->GetLoadedLevel() == Old->PersistentLevel))
        return false;
    {
        const FPreservedGraph Before(Fixture, Registry);
        ExactRefusal(*this, Verdict(*this, Root, Target),
            TEXT("Streaming package override needs an exact export adapter: ") + Next->GetOutermost()->GetName());
        Before.Check(*this);
        TestTrue(TEXT("Refusal preserves actual loaded child, owning World and membership"),
            Stream->GetLoadedLevel() == Old->PersistentLevel && Old->PersistentLevel->OwningWorld == Root &&
                Root->GetLevels().Contains(Old->PersistentLevel));
    }
    return !HasAnyErrors();
}
#endif

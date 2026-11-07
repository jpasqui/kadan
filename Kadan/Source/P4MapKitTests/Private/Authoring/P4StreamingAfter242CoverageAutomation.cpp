#include "P4AuthoringRows.h"
#include "P4StreamingWorldGraphShared.h"
// PRIVATE: append after the existing WorldGraph helper/tests in
// P4StreamingSavedWorldCoverageAutomation.cpp. Reuses native-verified context,
// actual saved-world and bounded registry/file cleanup fixtures.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "UObject/PackageFileSummary.h"
#include "UObject/ObjectVersion.h"

namespace P4StreamingAfter242Native
{
using namespace P4StreamingSavedWorldCoverage;
using P4WorldGraphNativeBranches::FRegistryCustody;
bool SerializationReady(UObject* Object)
{
    TArray<uint8> Native;
    FObjectWriter Writer(Object, Native, false, false, false, PPF_DuplicateVerbatim);
    return !Writer.IsError() && Native.Num() > 0;
}
struct FPackageState
{
    UPackage* Package;
    UObject* Outer;
    FString Path;
    EObjectFlags Flags;
    uint32 PackageFlags;
    FGuid Guid, PersistentGuid;
    FName Filename;
    uint64 FileSize;
    TArray<UObject*> Inventory;
    explicit FPackageState(UWorld* World)
        : Package(World->GetOutermost()), Outer(Package->GetOuter()), Path(Package->GetPathName()),
          Flags(Package->GetFlags()), PackageFlags(Package->GetPackageFlags()), Guid(Package->GetGuid()),
          PersistentGuid(Package->GetPersistentGuid()), Filename(Package->FileName), FileSize(Package->FileSize)
    {
        GetObjectsWithOuter(Package, Inventory, true);
    }
    bool Same() const
    {
        if (Package->GetOuter() != Outer || !Package->GetPathName().Equals(Path, ESearchCase::CaseSensitive) ||
            Package->GetFlags() != Flags || Package->GetPackageFlags() != PackageFlags || Package->GetGuid() != Guid ||
            Package->GetPersistentGuid() != PersistentGuid || Package->FileName != Filename ||
            Package->FileSize != FileSize)
            return false;
        TArray<UObject*> Actual;
        GetObjectsWithOuter(Package, Actual, true);
        if (Actual.Num() != Inventory.Num())
            return false;
        for (UObject* Object : Inventory)
            if (!Actual.Contains(Object))
                return false;
        return true;
    }
};
struct FPreserved
{
    TArray<FWorldState> Worlds;
    TArray<FPackageState> Packages;
    FDiskState Disk;
    const FRegistryCustody& Registry;
    TArray<uint8> RegistryBefore;
    FPreserved(const FP4StreamingSavedWorldFixture& InFixture, const FRegistryCustody& InRegistry)
        : Disk(InFixture.Base), Registry(InRegistry), RegistryBefore(InRegistry.Snapshot())
    {
        for (UWorld* World : InFixture.Worlds)
        {
            Worlds.Emplace(World);
            Packages.Emplace(World);
        }
    }
    bool Preflight(FAutomationTestBase& Test) const
    {
        bool Ready = Disk.Same() && Registry.Snapshot() == RegistryBefore;
        for (const FPackageState& Package : Packages)
            Ready &= Package.Same();
        for (const FString& Name : Disk.Names)
            Ready &= Disk.Payloads.FindChecked(Name).Num() > 0;
        for (const FWorldState& World : Worlds)
        {
            Ready &= World.Metadata.Num() > 0 && SerializationReady(World.Package->GetMetaData()) && World.Same();
            for (const FObjectState& Object : World.Objects)
                Ready &= Object.Serialized.Num() > 0 && SerializationReady(Object.Object);
        }
        Test.TestTrue(
            TEXT("Mandatory pre-API graph, native byte, metadata, registry and disk snapshots are ready and stable"),
            Ready);
        return Ready;
    }
    void Check(FAutomationTestBase& Test, const FString& AddedFile = FString()) const
    {
        for (const FPackageState& Package : Packages)
            Test.TestTrue(
                TEXT(
                    "Whole source native package identity, flags, GUIDs, filename, filesize and UObject inventory are retained"),
                Package.Same());
        for (const FWorldState& World : Worlds)
        {
            Test.TestTrue(
                TEXT(
                    "Original whole native World graph, objects, pointers, class, flags, metadata and dirty state are preserved"),
                World.Same());
            Test.TestTrue(TEXT("Current native metadata serialization remains ready"),
                SerializationReady(World.Package->GetMetaData()));
            for (const FObjectState& Object : World.Objects)
                Test.TestTrue(
                    TEXT("Current native object serialization remains ready"), SerializationReady(Object.Object));
        }
        Test.TestTrue(
            TEXT("Original native asset registry inventory remains exact"), Registry.Snapshot() == RegistryBefore);
        if (AddedFile.IsEmpty())
            Test.TestTrue(TEXT("Whole original Content namespace and bytes are preserved"), Disk.Same());
        else
        {
            TArray<FString> Expected = Disk.Names;
            Expected.Add(AddedFile);
            Expected.Sort();
            TArray<FString> Actual;
            IFileManager::Get().FindFilesRecursive(Actual, *Disk.Directory, TEXT("*"), true, false);
            Actual.Sort();
            Test.TestTrue(TEXT("Only the independently named new owned map is published"), Actual == Expected);
            for (const FString& Name : Disk.Names)
            {
                TArray<uint8> Current;
                Test.TestTrue(TEXT("Every original owned map remains byte-exact after copy publication"),
                    FFileHelper::LoadFileToArray(Current, *Name) && Current == Disk.Payloads.FindChecked(Name));
            }
        }
    }
};
FString Row(const FString& Target)
{
    return FString::Printf(
        TEXT(
            "{\"target\":\"%s\",\"loaded\":false,\"visible\":false,\"editor_visible\":false,\"package_to_load\":\"\"}"),
        *Target);
}
bool CopySavedRootless(FAutomationTestBase& Test, FP4StreamingSavedWorldFixture& InFixture,
    FRegistryCustody& InRegistry, UWorld* Source, AActor* SourceActor, const TCHAR* CopyName)
{
    const FString Target = InFixture.Base + TEXT("/") + CopyName;
    const FString File = FPackageName::LongPackageNameToFilename(Target, FPackageName::GetMapPackageExtension());
    Test.TestFalse(
        TEXT("Exact owned export destination is absent before actual copy"), IFileManager::Get().FileExists(*File));
    Source->GetOutermost()->SetDirtyFlag(true);
    const FPreserved Before(InFixture, InRegistry);
    if (!Before.Preflight(Test))
        return false;
    const bool Saved = UP4AuthoringTools::SaveExportWorld(Source, Target);
    Test.TestTrue(TEXT("Actual native saved World export succeeds"), Saved);
    // Discover and take cleanup custody of the real SaveMap-created object
    // before any early return. Do not construct a stand-in output World.
    UWorld* Copy = FindObject<UWorld>(nullptr, *(Target + TEXT(".") + CopyName));
    if (Copy && Copy != Source && Copy->GetOutermost()->GetName() == Target)
    {
        if (!GEngine->GetWorldContextFromWorld(Copy))
            GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(Copy);
        InFixture.Worlds.AddUnique(Copy);
        InFixture.Files.AddUnique(File);
    }
    if (!Test.TestTrue(TEXT("SaveMap duplicates into a distinct actual target World/package"),
            Saved && Copy && Copy != Source && Copy->GetOutermost() != Source->GetOutermost() &&
                Copy->GetOutermost()->GetName() == Target && Copy->PersistentLevel))
        return false;
    Before.Check(Test, File); // Registry source snapshot excludes the new copy.
    InRegistry.Assets.AddUnique(Copy);
    AActor* CopiedActor = FindObject<AActor>(Copy->PersistentLevel, TEXT("RootlessAuthored"));
    Test.TestTrue(TEXT("Independent copy has actual rootless actor, class, owning level and authored values"),
        CopiedActor && CopiedActor != SourceActor && CopiedActor->GetClass() == AActor::StaticClass() &&
            !CopiedActor->GetRootComponent() && CopiedActor->GetLevel() == Copy->PersistentLevel &&
            CopiedActor->InitialLifeSpan == 143.25f && CopiedActor->Tags.Num() == 2 &&
            CopiedActor->Tags[0] == TEXT("AuthoredRootless") && CopiedActor->Tags[1] == TEXT("KeepNativeSibling"));
    Test.TestTrue(TEXT("Original actual rootless actor retains owning source level and independent authored values"),
        SourceActor->GetLevel() == Source->PersistentLevel && !SourceActor->GetRootComponent() &&
            SourceActor->InitialLifeSpan == 143.25f && SourceActor->Tags.Num() == 2 &&
            SourceActor->Tags[0] == TEXT("AuthoredRootless") && SourceActor->Tags[1] == TEXT("KeepNativeSibling"));
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File));
    if (!Test.TestTrue(TEXT("New actual map file is independently readable"), Reader.IsValid()))
        return false;
    FPackageFileSummary Summary;
    *Reader << Summary;
    Test.TestTrue(TEXT("Saved disk output has real Unreal map summary and exact target native package GUID"),
        !Reader->IsError() && uint32(Summary.Tag) == uint32(PACKAGE_FILE_TAG) &&
            (Summary.PackageFlags & PKG_ContainsMap) != 0 && Summary.Guid == Copy->GetOutermost()->GetGuid());
    Reader.Reset();
    Test.TestTrue(TEXT("Copied native asset is admitted by actual registry discovery"),
        FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FName(*Copy->GetPathName())).IsValid());
    return !Test.HasAnyErrors();
}
AActor* Rootless(UWorld* World)
{
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("RootlessAuthored");
    Spawn.ObjectFlags = RF_Transactional;
    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
    if (Actor)
    {
        Actor->InitialLifeSpan = 143.25f;
        Actor->Tags = {TEXT("AuthoredRootless"), TEXT("KeepNativeSibling")};
    }
    return Actor;
}
} //namespace P4StreamingAfter242Native

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingAfter242ReferenceTransition,
    "P4MapKit.Quality.StreamingAfter242LoadedReferenceMismatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingAfter242ReferenceTransition::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingAfter242Native;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Old = Fixture.Make(TEXT("Old"));
    UWorld* Next = Fixture.Make(TEXT("Next"));
    if (!TestTrue(TEXT("Actual old and new child maps saved and registered"),
            Registry.SaveAndRegister(Fixture, Old) && Registry.SaveAndRegister(Fixture, Next)))
        return false;
    ULevelStreamingDynamic* Stream = Fixture.Add(Parent, Old, TEXT("AuthoredChild"));
    if (!TestTrue(
            TEXT("Public export loader supplies actual old level"), UP4AuthoringTools::LoadExportLevels(Parent)) ||
        !TestTrue(TEXT("Actual native old loaded-level identity and ownership"),
            Stream->GetLoadedLevel() == Old->PersistentLevel && Old->PersistentLevel->OwningWorld == Parent))
        return false;
    Stream->SetWorldAssetByPackageName(Next->GetOutermost()->GetFName());
    if (!TestTrue(TEXT("Public reference edit retains old loaded level until next streaming update"),
            Stream->GetLoadedLevel() == Old->PersistentLevel && Stream->PackageNameToLoad.IsNone()))
        return false;
    for (UWorld* World : Fixture.Worlds)
        World->GetOutermost()->SetDirtyFlag(false);
    const FPreserved Before(Fixture, Registry);
    if (!Before.Preflight(*this))
        return false;
    TestFalse(
        TEXT("Export load refuses genuine new saved reference with actual old loaded package before forcing flags"),
        UP4AuthoringTools::LoadExportLevels(Parent));
    TestTrue(TEXT("Refusal keeps actual old level and owning-world membership"),
        Stream->GetLoadedLevel() == Old->PersistentLevel && Old->PersistentLevel->OwningWorld == Parent &&
            Parent->GetLevels().Contains(Old->PersistentLevel));
    Before.Check(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingAfter242NativeMappings,
    "P4MapKit.Quality.StreamingAfter242NativeMappingIdentities",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingAfter242NativeMappings::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingAfter242Native;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    if (!TestTrue(TEXT("Actual canonical source leaf saved and registered"), Registry.SaveAndRegister(Fixture, Leaf)))
        return false;
    ULevelStreamingDynamic* Stream = Fixture.Add(Parent, Leaf, TEXT("AuthoredInstance"));
    const FString Source = Leaf->GetOutermost()->GetName(), Alias = Fixture.Base + TEXT("/LEAF");
    if (!TestTrue(TEXT("Actual source package and case alias have the same native FName identity"),
            !Source.Equals(Alias, ESearchCase::CaseSensitive) && FName(*Source) == FName(*Alias)))
        return false;
    for (UWorld* World : Fixture.Worlds)
        World->GetOutermost()->SetDirtyFlag(false);
    {
        const FPreserved Before(Fixture, Registry);
        if (!Before.Preflight(*this))
            return false;
        const FString Collision = FString::Printf(
            TEXT("{\"%s\":%s,\"%s\":%s}"), *Source, *Row(Source), *Alias, *Row(Source));
        TestFalse(TEXT("Case-colliding native source package rows refuse before any stream mutation"),
            UP4AuthoringTools::RemapStreaming(Parent, Collision, false, true));
        Before.Check(*this);
    }
    {
        const FPreserved Before(Fixture, Registry);
        if (!Before.Preflight(*this))
            return false;
        const FString Matching = FString::Printf(TEXT("{\"%s\":{\"target\":\"%s\",\"instances\":{\"%s\":%s}}}"),
            *Source, *Source, *Stream->GetName(), *Row(Source));
        TestTrue(TEXT("Actual per-instance row accepts explicit exact parent target agreement as no-op"),
            UP4AuthoringTools::RemapStreaming(Parent, Matching, false, false));
        TestTrue(TEXT("Independent source path, load override, three flags and actual instance remain original"),
            Stream->GetWorldAssetPackageName() == Source && Stream->PackageNameToLoad.IsNone() &&
                !Stream->ShouldBeLoaded() && !Stream->GetShouldBeVisibleFlag() &&
                !Stream->GetShouldBeVisibleInEditor() && Parent->GetStreamingLevels().Num() == 1 &&
                Parent->GetStreamingLevels()[0] == Stream);
        Before.Check(*this);
    }
    if (!TestTrue(TEXT("Installed actual engine Entry map exists"),
            FPackageName::DoesPackageExist(TEXT("/Engine/Maps/Entry"))))
        return false;
    Stream->SetWorldAssetByPackageName(TEXT("/Engine/Maps/Entry"));
    {
        const FPreserved Before(Fixture, Registry);
        if (!Before.Preflight(*this))
            return false;
        const FString Mapping = FString::Printf(TEXT("{\"%s\":%s}"), *Source, *Row(Source));
        TestFalse(TEXT("Actual engine map reference is outside authored Game remap scope"),
            UP4AuthoringTools::RemapStreaming(Parent, Mapping, false, true));
        TestFalse(TEXT("Actual engine map reference refuses export loading before engine asset load"),
            UP4AuthoringTools::LoadExportLevels(Parent));
        Before.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StreamingAfter242StandaloneCopy,
    "P4MapKit.Quality.StreamingAfter242StandaloneSavedCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StreamingAfter242StandaloneCopy::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingAfter242Native;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Source = Fixture.Make(TEXT("Source"));
    AActor* Actor = Rootless(Source);
    if (!TestTrue(TEXT("Actual authored rootless native actor exists"), Actor && !Actor->GetRootComponent()) ||
        !TestTrue(TEXT("Tiny authored world has no map-build-data sidecar"), !Source->PersistentLevel->MapBuildData) ||
        !TestTrue(
            TEXT("Source actual map saved and registered before export"), Registry.SaveAndRegister(Fixture, Source)))
        return false;
    return CopySavedRootless(*this, Fixture, Registry, Source, Actor, TEXT("StandaloneCopy"));
}

#endif

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS && PLATFORM_WINDOWS
// PRIVATE append after P4StreamingAfter242Native in the existing saved-world
// automation TU. Root alone promotes/builds/runs. Uncompiled and unexecuted.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Curves/CurveFloat.h"
#include "UObject/StrongObjectPtr.h"

namespace P4StreamingWorldGraph258
{
using namespace P4StreamingSavedWorldCoverage;
using P4WorldGraphNativeBranches::FRegistryCustody;
using P4WorldGraphNativeBranches::Verdict;
using P4WorldGraphNativeBranches::ExactRefusal;
using P4StreamingAfter242Native::FPreserved;
using P4StreamingAfter242Native::SerializationReady;
bool Text(const FString& A, const FString& B)
{
    return A.Equals(B, ESearchCase::CaseSensitive);
}
bool Check(FAutomationTestBase& InTest, const FString& Label, bool Value)
{
    return InTest.TestTrue(Label, Value);
}
FString Mapping(ULevelStreaming* InStream, const FString& InRow)
{
    return TEXT("{\"") + InStream->GetWorldAssetPackageName() + TEXT("\":") + InRow + TEXT("}");
}
bool SaveAll(FAutomationTestBase& InTest, FP4StreamingSavedWorldFixture& InFixture, FRegistryCustody& InRegistry)
{
    for (UWorld* World : InFixture.Worlds)
        if (!Check(InTest, TEXT("Actual owned native map saved and authentically registered"),
                InRegistry.SaveAndRegister(InFixture, World)))
            return false;
    return true;
}
TArray<uint8> Rows(UPackage* InPackage, bool* Ready = nullptr)
{
    TArray<FAssetData> Actual;
    FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(InPackage->GetFName(), Actual);
    Actual.Sort([](const FAssetData& A, const FAssetData& B)
        { return A.ObjectPath.ToString().Compare(B.ObjectPath.ToString(), ESearchCase::CaseSensitive) < 0; });
    TArray<uint8> Out;
    FMemoryWriter Writer(Out);
    int32 Count = Actual.Num();
    Writer << Count;
    for (const FAssetData& Row : Actual)
    {
        FString Path = Row.ObjectPath.ToString(), Package = Row.PackageName.ToString(),
                Folder = Row.PackagePath.ToString();
        FString Name = Row.AssetName.ToString(), Class = Row.AssetClass.ToString();
        TArray<int32> Chunks = Row.ChunkIDs;
        uint32 PackageFlags = Row.PackageFlags;
        Writer << Path << Package << Folder << Name << Class << Chunks << PackageFlags;
        TArray<TPair<FString, FString>> Tags;
        for (const auto& Tag : Row.TagsAndValues)
            Tags.Emplace(Tag.Key.ToString(), FString(Tag.Value));
        Tags.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
            { return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0; });
        int32 TagCount = Tags.Num();
        Writer << TagCount;
        for (auto& Tag : Tags)
            Writer << Tag.Key << Tag.Value;
    }
    if (Ready)
        *Ready = !Writer.IsError() && Out.Num() > 0;
    return Out;
}
// Entire actual object, metadata, native package identity and inventory;
// neither a schema surrogate nor a rewritten expected serialized snapshot.
struct FNativeObjectPackage
{
    FObjectState Object, Metadata;
    UPackage* Package;
    UObject* Outer;
    FString Path;
    EObjectFlags Flags;
    uint32 PackageFlags;
    bool Dirty;
    FGuid Guid, PersistentGuid;
    FName Filename;
    uint64 FileSize;
    TArray<UObject*> Inventory;
    TArray<uint8> RegistryRows;
    bool RegistryReady = false;
    explicit FNativeObjectPackage(UObject* InObject)
        : Object(InObject), Metadata(InObject->GetOutermost()->GetMetaData()), Package(InObject->GetOutermost()),
          Outer(Package->GetOuter()), Path(Package->GetPathName()), Flags(Package->GetFlags()),
          PackageFlags(Package->GetPackageFlags()), Dirty(Package->IsDirty()), Guid(Package->GetGuid()),
          PersistentGuid(Package->GetPersistentGuid()), Filename(Package->FileName), FileSize(Package->FileSize)
    {
        GetObjectsWithOuter(Package, Inventory, true);
        RegistryRows = Rows(Package, &RegistryReady);
    }
    bool Same() const
    {
        bool CurrentReady = false;
        const TArray<uint8> CurrentRows = Rows(Package, &CurrentReady);
        if (!Object.Same() || !Metadata.Same() || !Text(Object.Object->GetPathName(), Object.Path) ||
            !Text(Metadata.Object->GetPathName(), Metadata.Path) || Package->GetOuter() != Outer ||
            !Text(Package->GetPathName(), Path) || Package->GetFlags() != Flags ||
            Package->GetPackageFlags() != PackageFlags || Package->IsDirty() != Dirty || Package->GetGuid() != Guid ||
            Package->GetPersistentGuid() != PersistentGuid || Package->FileName != Filename ||
            Package->FileSize != FileSize || !CurrentReady || CurrentRows != RegistryRows)
            return false;
        TArray<UObject*> Current;
        GetObjectsWithOuter(Package, Current, true);
        if (Current.Num() != Inventory.Num())
            return false;
        for (UObject* Entry : Inventory)
            if (!Current.Contains(Entry))
                return false;
        return SerializationReady(Object.Object) && SerializationReady(Metadata.Object);
    }
    bool Preflight(FAutomationTestBase& InTest) const
    {
        return Check(InTest,
            TEXT("Mandatory whole object/package/metadata/registry snapshots are successful, nonempty and stable"),
            RegistryReady && Object.Serialized.Num() > 0 && Metadata.Serialized.Num() > 0 && Same());
    }
    void AssertSame(FAutomationTestBase& InTest) const
    {
        Check(InTest,
            TEXT(
                "Whole native object, metadata, package flags/GUID/file info/inventory and genuine registry rows remain unchanged"),
            Same());
    }
};
struct FOutsideWorld
{
    FAutomationTestBase& Test;
    FString PackageName;
    TStrongObjectPtr<UWorld> World;
    explicit FOutsideWorld(FAutomationTestBase& InTest)
        : Test(InTest), PackageName(TEXT("/Engine/P4StreamingWorldGraph258/") +
                            FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/Outside")),
          World(UWorld::CreateWorld(EWorldType::Editor, false, TEXT("Outside"), CreatePackage(*PackageName)))
    {
        GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World.Get());
        World->GetOutermost()->GetMetaData();
    }
    ~FOutsideWorld()
    {
        const bool Owned = World.IsValid() && Text(World->GetOutermost()->GetName(), PackageName);
        Check(Test, TEXT("Outside-project fixture cleanup owns its exact unsaved native World"), Owned);
        if (!Owned)
            return;
        Check(
            Test, TEXT("Outside-project fixture created no Engine file"), !FPackageName::DoesPackageExist(PackageName));
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World.Get());
        World->ClearFlags(RF_Public | RF_Standalone);
        World->SetFlags(RF_Transient);
        Check(Test, TEXT("Retired outside-project World is not a live asset"), !World->IsAsset());
    }
};
struct FExtraCurve
{
    FP4StreamingSavedWorldFixture& Fixture;
    FAutomationTestBase& Test;
    TStrongObjectPtr<UCurveFloat> Curve;
    FString Filename;
    bool Registered = false;
    FExtraCurve(FP4StreamingSavedWorldFixture& InFixture, FAutomationTestBase& InTest)
        : Fixture(InFixture), Test(InTest),
          Curve(NewObject<UCurveFloat>(
              CreatePackage(*(InFixture.Base + TEXT("/ActualCurve"))), TEXT("ActualCurve"), RF_Public | RF_Standalone)),
          Filename(FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(
              InFixture.Base + TEXT("/ActualCurve"), FPackageName::GetAssetPackageExtension())))
    {
        Curve->FloatCurve.AddKey(-1.0f, 23.25f);
        Curve->FloatCurve.AddKey(2.0f, -3.5f);
        Curve->GetOutermost()->GetMetaData()->SetValue(Curve.Get(), TEXT("P4Owned258Curve"), *InFixture.Base);
    }
    bool Save()
    {
        if (!Check(Test, TEXT("Exact new native curve package has no prior file"),
                !IFileManager::Get().FileExists(*Filename)) ||
            !Check(Test, TEXT("Owned native asset directory exists"),
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true)))
            return false;
        if (!Check(Test, TEXT("Actual native CurveFloat saves through the public Unreal package API"),
                UPackage::SavePackage(Curve->GetOutermost(), Curve.Get(), RF_Public | RF_Standalone, *Filename, GError,
                    nullptr, false, false, SAVE_NoError, nullptr, FDateTime::MinValue(), false)))
            return false;
        FAssetRegistryModule::AssetCreated(Curve.Get());
        Registered = true;
        const FAssetData Actual = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(
            FName(*Curve->GetPathName()));
        return Check(Test, TEXT("Authentic registry row describes the actual saved CurveFloat"),
            Actual.IsValid() && Actual.ObjectPath == FName(*Curve->GetPathName()) &&
                Actual.AssetClass == UCurveFloat::StaticClass()->GetFName());
    }
    ~FExtraCurve()
    {
        UPackage* Package = Curve->GetOutermost();
        const FString Expected = Fixture.Base + TEXT("/ActualCurve");
        const bool Owned = Text(Package->GetName(), Expected) &&
            Text(Filename,
                FPaths::ConvertRelativePathToFull(
                    FPackageName::LongPackageNameToFilename(Expected, FPackageName::GetAssetPackageExtension())));
        Check(Test, TEXT("Extra native asset retirement binds the exact GUID package and asset filename"), Owned);
        if (!Owned)
            return;
        if (Registered)
            FAssetRegistryModule::AssetDeleted(Curve.Get());
        Curve->ClearFlags(RF_Public | RF_Standalone);
        Curve->SetFlags(RF_Transient);
        Check(Test, TEXT("Disposed extra native curve cannot reappear as a live asset"), !Curve->IsAsset());
        bool Deleted = !IFileManager::Get().FileExists(*Filename);
        uint32 Error = 0;
        int32 Attempts = 0;
#if PLATFORM_WINDOWS
        P4WorldGraphNativeBranches::FMapFileEvidence Original;
        const bool Captured = Deleted || Original.Capture(Filename);
        Check(Test, TEXT("Extra asset cleanup captures actual full file bytes and native file identity"), Captured);
        if (!Deleted && Captured)
        {
            ResetLoadersForSave(Package, *Filename);
            for (int32 Index = 0; Index < 5; ++Index)
            {
                if (Index)
                    FPlatformProcess::Sleep(0.05f);
                if (!Check(Test,
                        TEXT(
                            "Before extra-asset deletion original bytes/identity and exact package/file ownership remain unchanged"),
                        Text(Package->GetName(), Expected) && Original.Same(Filename)))
                    break;
                ++Attempts;
                Deleted = IFileManager::Get().Delete(*Filename, true, false, true);
                Error = Deleted ? 0 : FPlatformMisc::GetLastError();
                if (Deleted || (Error != ERROR_SHARING_VIOLATION && Error != ERROR_LOCK_VIOLATION))
                    break;
            }
        }
#else
        if (!Deleted)
        {
            ResetLoadersForSave(Package, *Filename);
            ++Attempts;
            Deleted = IFileManager::Get().Delete(*Filename, true, false, true);
            Error = Deleted ? 0 : FPlatformMisc::GetLastError();
        }
#endif
        const bool Absent = !IFileManager::Get().FileExists(*Filename);
        Check(Test,
            FString::Printf(
                TEXT("Exact extra native asset cleanup succeeds and leaves no file: %s; attempts=%d; error=%u"),
                *Filename, Attempts, Error),
            Deleted && Absent);
        if (Absent)
            FAssetRegistryModule::PackageDeleted(Package);
        TArray<FAssetData> Actual;
        FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(Package->GetFName(), Actual);
        Check(Test, TEXT("Extra native asset cleanup leaves no real package registry inventory"), Actual.Num() == 0);
    }
};
struct FVisibilityRefusals
{
    FAutomationTestBase& Test;
    FP4StreamingSavedWorldFixture& Fixture;
    FRegistryCustody& Registry;
    UWorld* Parent;
    UWorld* Child;
    ULevelStreaming* Stream;
    FDelegateHandle Added, Removed;
    int32 VisibleCalls = 0, InvisibleCalls = 0;
    FVisibilityRefusals(FAutomationTestBase& InTest, FP4StreamingSavedWorldFixture& InFixture,
        FRegistryCustody& InRegistry, UWorld* InParent, UWorld* InChild, ULevelStreaming* InStream)
        : Test(InTest), Fixture(InFixture), Registry(InRegistry), Parent(InParent), Child(InChild), Stream(InStream)
    {
        Added = FWorldDelegates::LevelAddedToWorld.AddLambda(
            [this](ULevel* InLevel, UWorld* InWorld) { During(InLevel, InWorld, true); });
        Removed = FWorldDelegates::LevelRemovedFromWorld.AddLambda(
            [this](ULevel* InLevel, UWorld* InWorld) { During(InLevel, InWorld, false); });
    }
    ~FVisibilityRefusals()
    {
        FWorldDelegates::LevelAddedToWorld.Remove(Added);
        FWorldDelegates::LevelRemovedFromWorld.Remove(Removed);
    }
    void During(ULevel* InLevel, UWorld* InWorld, bool BecomingVisible)
    {
        if (InWorld != Parent || InLevel != Child->PersistentLevel)
            return;
        if (BecomingVisible)
            ++VisibleCalls;
        else
            ++InvisibleCalls;
        const auto Expected = BecomingVisible ? ULevelStreaming::ECurrentState::MakingVisible
                                              : ULevelStreaming::ECurrentState::MakingInvisible;
        if (!Check(Test,
                FString::Printf(
                    TEXT(
                        "Actual UE visibility callback observes its genuine streaming transition: visible=%d; state=%d; expected=%d; loaded_matches=%d; owner_matches=%d"),
                    BecomingVisible ? 1 : 0, int32(Stream->GetCurrentState()), int32(Expected),
                    Stream->GetLoadedLevel() == InLevel ? 1 : 0, InLevel->OwningWorld == Parent ? 1 : 0),
                Stream->GetCurrentState() == Expected && Stream->GetLoadedLevel() == InLevel &&
                    InLevel->OwningWorld == Parent))
            return;
        const FPreserved Before(Fixture, Registry);
        if (!Before.Preflight(Test))
            return;
        const FTransform Pose = Stream->LevelTransform;
        const auto State = Stream->GetCurrentState();
        ULevel* Loaded = Stream->GetLoadedLevel();
        Check(Test,
            TEXT("Public remap refuses an actual native visibility transition before touching any authored state"),
            !UP4AuthoringTools::RemapStreaming(Parent,
                Mapping(Stream, P4StreamingAfter242Native::Row(Stream->GetWorldAssetPackageName())), false, true));
        Before.Check(Test);
        Check(Test,
            TEXT(
                "Transition refusal retains exact native state, loaded pointer, owner, visibility and stream transform"),
            Stream->GetCurrentState() == State && Stream->GetLoadedLevel() == Loaded &&
                Stream->GetTypedOuter<UWorld>() == Parent && Stream->LevelTransform.Equals(Pose, 0) &&
                InLevel->bIsVisible == BecomingVisible);
    }
};
} //namespace P4StreamingWorldGraph258

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Streaming258JsonRefusals, "P4MapKit.Quality.Streaming258ActualMappingInputRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Streaming258JsonRefusals::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingWorldGraph258;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    ULevelStreaming* Stream = Fixture.Add(Parent, Leaf, TEXT("ActualChild"));
    if (!SaveAll(*this, Fixture, Registry))
        return false;
    const FString Target = Leaf->GetOutermost()->GetName();
    const FString ValidRow = P4StreamingAfter242Native::Row(Target);
    const FPreserved Before(Fixture, Registry);
    if (!Before.Preflight(*this))
        return false;
    Check(*this, TEXT("Complete native same-target plan remains an independently checked no-op control"),
        UP4AuthoringTools::RemapStreaming(Parent, Mapping(Stream, ValidRow), false, false));
    Before.Check(*this);
    const TArray<FString> Inputs = {FString(TEXT("{\"/Engine/Maps/Entry\":")) + ValidRow + TEXT("}"),
        Mapping(Stream, TEXT("{\"loaded\":false,\"visible\":false,\"editor_visible\":false}")),
        Mapping(Stream, TEXT("{\"target\":\"") + Target + TEXT("\",\"instances\":[1]}"))};
    for (const FString& Input : Inputs)
    {
        if (!Before.Preflight(*this))
            return false;
        Check(*this, TEXT("Actual serialized mapping shape refuses before transaction/publication"),
            !UP4AuthoringTools::RemapStreaming(Parent, Input, false, true));
        Before.Check(*this);
    }
    Check(*this,
        TEXT("Every refusal retains the native reference, three flags, load override and exact stream identity"),
        Text(Stream->GetWorldAssetPackageName(), Target) && Stream->PackageNameToLoad.IsNone() &&
            !Stream->ShouldBeLoaded() && !Stream->GetShouldBeVisibleFlag() && !Stream->GetShouldBeVisibleInEditor() &&
            Parent->GetStreamingLevels().Num() == 1 && Parent->GetStreamingLevels()[0] == Stream);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WorldGraph258Namespaces, "P4MapKit.Quality.WorldGraph258ActualNativeNamespaces",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WorldGraph258Namespaces::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingWorldGraph258;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    ULevelStreaming* Stream = Fixture.Add(Parent, Leaf, TEXT("ActualChild"));
    if (!SaveAll(*this, Fixture, Registry))
        return false;
    const FString Target = Fixture.Base + TEXT("/Export");
    {
        const FPreserved Before(Fixture, Registry);
        if (!Before.Preflight(*this))
            return false;
        const auto Accepted = Verdict(*this, Parent, Target);
        bool Valid = false;
        const TSharedPtr<FJsonObject>* Graph = nullptr;
        if (!Check(*this, TEXT("Actual owned native graph positive control has exactly its two real Worlds"),
                Accepted.IsValid() && Accepted->TryGetBoolField(TEXT("valid"), Valid) && Valid &&
                    Accepted->TryGetObjectField(TEXT("graph"), Graph) && Graph && Graph->IsValid() &&
                    (*Graph)->Values.Num() == 2))
            return false;
        const auto RootNode = (*Graph)->GetObjectField(Parent->GetOutermost()->GetName());
        const auto& Edges = RootNode->GetArrayField(TEXT("streams"));
        Check(*this, TEXT("Independent real root/edge identities, flags and unloaded state match the graph"),
            Text(RootNode->GetStringField(TEXT("target")), Target) && Edges.Num() == 1 &&
                Text(Edges[0]->AsObject()->GetStringField(TEXT("stream_name")), Stream->GetName()) &&
                Text(Edges[0]->AsObject()->GetStringField(TEXT("package")), Leaf->GetOutermost()->GetName()) &&
                Edges[0]->AsObject()->GetStringField(TEXT("loaded_package")).IsEmpty() &&
                !Edges[0]->AsObject()->GetBoolField(TEXT("loaded")) &&
                !Edges[0]->AsObject()->GetBoolField(TEXT("visible")) &&
                !Edges[0]->AsObject()->GetBoolField(TEXT("editor_visible")));
        Before.Check(*this);
    }
    Stream->SetWorldAssetByPackageName(TEXT("/Engine/Maps/Entry"));
    {
        const FPreserved Before(Fixture, Registry);
        if (!Before.Preflight(*this))
            return false;
        ExactRefusal(*this, Verdict(*this, Parent, Target),
            TEXT("Missing or invalid project streaming reference: /Engine/Maps/Entry"));
        Before.Check(*this);
    }
    {
        FOutsideWorld Outside(*this);
        const FWorldState WorldBefore(Outside.World.Get());
        const FNativeObjectPackage Before(Outside.World.Get());
        if (!Before.Preflight(*this) ||
            !Check(*this, TEXT("Non-Game fixture is a real initialized authored UE World"),
                Outside.World->PersistentLevel && Outside.World->GetNumLevels() > 0 && WorldBefore.Same()))
            return false;
        ExactRefusal(*this, Verdict(*this, Outside.World.Get(), Target),
            TEXT("Streaming export needs distinct /Game source and destination packages"));
        Before.AssertSame(*this);
        Check(*this, TEXT("Non-Game graph refusal retains the complete real World graph"), WorldBefore.Same());
    }
    {
        UWorld* Default = UWorld::StaticClass()->GetDefaultObject<UWorld>();
        const FNativeObjectPackage Before(Default);
        if (!Check(*this, TEXT("Actual stock World CDO is complete and has its native empty persistent-level default"),
                Default && Default->HasAnyFlags(RF_ClassDefaultObject) && !Default->PersistentLevel) ||
            !Before.Preflight(*this))
            return false;
        ExactRefusal(*this, Verdict(*this, Default, Target), TEXT("Expected an actual authored World"));
        Before.AssertSame(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Streaming258NonWorldAsset,
    "P4MapKit.Quality.Streaming258SavedNativeAssetIsNotWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Streaming258NonWorldAsset::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingWorldGraph258;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    FExtraCurve Asset(Fixture, *this);
    if (!Asset.Save())
        return false;
    ULevelStreamingDynamic* Stream = NewObject<ULevelStreamingDynamic>(Parent, TEXT("ActualCurveReference"));
    Stream->SetWorldAssetByPackageName(Asset.Curve->GetOutermost()->GetFName());
    Stream->SetShouldBeLoaded(false);
    Stream->SetShouldBeVisible(false);
    Stream->SetShouldBeVisibleInEditor(false);
    Parent->AddStreamingLevel(Stream);
    const FPreserved WorldsBefore(Fixture, Registry);
    const FNativeObjectPackage AssetBefore(Asset.Curve.Get());
    if (!WorldsBefore.Preflight(*this) || !AssetBefore.Preflight(*this) ||
        !Check(*this, TEXT("Actual saved package exists and its native authored curve has independent scalar values"),
            FPackageName::DoesPackageExist(Asset.Curve->GetOutermost()->GetName()) &&
                Asset.Curve->FloatCurve.GetNumKeys() == 2 && Asset.Curve->FloatCurve.Eval(-1.0f) == 23.25f &&
                Asset.Curve->FloatCurve.Eval(2.0f) == -3.5f))
        return false;
    Check(*this,
        TEXT("Canonical existing native non-World asset refuses export loading before forced flags or streaming work"),
        !UP4AuthoringTools::LoadExportLevels(Parent));
    WorldsBefore.Check(*this);
    AssetBefore.AssertSame(*this);
    Check(*this, TEXT("Class refusal cannot fabricate a loaded level or override"),
        Stream->GetLoadedLevel() == nullptr && Stream->PackageNameToLoad.IsNone());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Streaming258DestroyedActorCopy,
    "P4MapKit.Quality.Streaming258DestroyedActorSavedCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Streaming258DestroyedActorCopy::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingWorldGraph258;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    AActor* Survivor = P4StreamingAfter242Native::Rootless(Leaf);
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("GenuinelyDeletedActor");
    Spawn.ObjectFlags = RF_Transactional;
    TStrongObjectPtr<AActor> Deleted(Leaf->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn));
    if (!Check(*this, TEXT("Real native survivor and deletable actor exist in the authored source level"),
            Survivor && Deleted.IsValid() && Deleted->GetLevel() == Leaf->PersistentLevel))
        return false;
    Deleted->Tags = {TEXT("RemovedByNativeDestroyActor")};
    Deleted->InitialLifeSpan = 91.75f;
    if (!SaveAll(*this, Fixture, Registry))
        return false;
    ULevelStreaming* Stream = Fixture.Add(Parent, Leaf, TEXT("ActualChild"));
    if (!Check(*this, TEXT("Public export loading supplies the actual saved child and native ownership"),
            UP4AuthoringTools::LoadExportLevels(Parent) && Stream->GetLoadedLevel() == Leaf->PersistentLevel &&
                Deleted->GetWorld() == Parent))
        return false;
    const int32 Slot = Leaf->PersistentLevel->Actors.Find(Deleted.Get());
    const TWeakObjectPtr<AActor> DeletedIdentity(Deleted.Get());
    TArray<UObject*> DeletedObjects;
    GetObjectsWithOuter(Deleted.Get(), DeletedObjects, true);
    DeletedObjects.Add(Deleted.Get());
    TArray<UObject*> ExpectedLiveObjects;
    GetObjectsWithOuter(Leaf, ExpectedLiveObjects, true);
    for (UObject* Object : DeletedObjects)
        ExpectedLiveObjects.Remove(Object);
    if (!Check(*this, TEXT("Independent native actor slot exists before ordinary actor deletion"), Slot >= 0) ||
        !Check(*this, TEXT("Public native DestroyActor succeeds in the actor's actual owning World"),
            Parent->DestroyActor(Deleted.Get(), false, false)) ||
        !Check(*this,
            TEXT("Real engine deletion retains its native null actor slot and original pending-kill identity"),
            Leaf->PersistentLevel->Actors.IsValidIndex(Slot) && Leaf->PersistentLevel->Actors[Slot] == nullptr &&
                Deleted->IsPendingKill() && DeletedIdentity.Get(true) == Deleted.Get()))
        return false;
    // Normal OnPreSaveWorld performs GC. Finish this fixture's genuine actor
    // deletion before taking immutable source snapshots: pending-kill actors
    // cannot be kept alive by a strong reference through that native GC.
    // No inventory filter, actor-slot edit or GC-elimination override is used.
    Deleted.Reset();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TArray<UObject*> ActualLiveObjects;
    GetObjectsWithOuter(Leaf, ActualLiveObjects, true);
    bool ExactLiveInventory = ActualLiveObjects.Num() == ExpectedLiveObjects.Num();
    for (UObject* Object : ExpectedLiveObjects)
        ExactLiveInventory &= ActualLiveObjects.Contains(Object);
    if (!Check(*this,
            TEXT(
                "Completed native deletion removes only the actual destroyed actor subtree from the source's whole UObject inventory"),
            ExactLiveInventory && DeletedIdentity.Get(true) == nullptr &&
                FindObject<AActor>(Leaf->PersistentLevel, TEXT("GenuinelyDeletedActor")) == nullptr) ||
        !Check(*this,
            TEXT(
                "Completed native deletion preserves the actual null slot, survivor authored values and loaded child ownership"),
            Leaf->PersistentLevel->Actors.IsValidIndex(Slot) && Leaf->PersistentLevel->Actors[Slot] == nullptr &&
                Leaf->PersistentLevel->Actors.Contains(Survivor) && Survivor->GetLevel() == Leaf->PersistentLevel &&
                !Survivor->GetRootComponent() && Survivor->InitialLifeSpan == 143.25f && Survivor->Tags.Num() == 2 &&
                Survivor->Tags[0] == TEXT("AuthoredRootless") && Survivor->Tags[1] == TEXT("KeepNativeSibling") &&
                Stream->GetLoadedLevel() == Leaf->PersistentLevel && Leaf->PersistentLevel->OwningWorld == Parent &&
                Parent->GetLevels().Contains(Leaf->PersistentLevel)))
        return false;
    if (!P4StreamingAfter242Native::CopySavedRootless(
            *this, Fixture, Registry, Leaf, Survivor, TEXT("DeletedActorCopy")))
        return false;
    UWorld* Copy = FindObject<UWorld>(nullptr, *(Fixture.Base + TEXT("/DeletedActorCopy.DeletedActorCopy")));
    if (!Check(*this, TEXT("Real saved copy has a distinct actual native persistent level"),
            Copy && Copy->PersistentLevel && Copy->PersistentLevel != Leaf->PersistentLevel))
        return false;
    bool Gone = true;
    for (AActor* Actor : Copy->PersistentLevel->Actors)
        if (Actor &&
            (Actor->GetFName() == TEXT("GenuinelyDeletedActor") ||
                Actor->Tags.Contains(TEXT("RemovedByNativeDestroyActor"))))
            Gone = false;
    Check(*this, TEXT("Independent saved-copy native actor inventory excludes the actually destroyed authored actor"),
        Gone);
    Check(*this,
        TEXT("Copy restores and retains the source's exact engine-generated null slot and actual loaded ownership"),
        Leaf->PersistentLevel->Actors.IsValidIndex(Slot) && Leaf->PersistentLevel->Actors[Slot] == nullptr &&
            Stream->GetLoadedLevel() == Leaf->PersistentLevel && Leaf->PersistentLevel->OwningWorld == Parent);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Streaming258VisibilityRefusals,
    "P4MapKit.Quality.Streaming258ActualVisibilityTransitionRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Streaming258VisibilityRefusals::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingWorldGraph258;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Parent = Fixture.Make(TEXT("Parent"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    if (!SaveAll(*this, Fixture, Registry))
        return false;
    ULevelStreaming* Stream = Fixture.Add(Parent, Leaf, TEXT("ActualChild"));
    const FDiskState Files(Fixture.Base);
    const TArray<uint8> RegistryBefore = Registry.Snapshot();
    const TArray<uint8> ParentMetadata = Bytes(Parent->GetOutermost()->GetMetaData()),
                        LeafMetadata = Bytes(Leaf->GetOutermost()->GetMetaData());
    // CreateWorld's actual persistent level starts visible. Its initial load
    // adopts that level as LoadedVisible, and the export loader then adds it
    // to the parent. Establish the normal loaded/hidden state through real
    // engine operations before observing subsequent visibility transitions.
    if (!Check(*this, TEXT("Native warmup adopts the actual saved child and adds it to its parent"),
            UP4AuthoringTools::LoadExportLevels(Parent) &&
                Stream->GetCurrentState() == ULevelStreaming::ECurrentState::LoadedVisible &&
                Stream->GetLoadedLevel() == Leaf->PersistentLevel && Leaf->PersistentLevel->OwningWorld == Parent &&
                Leaf->PersistentLevel->bIsVisible && Parent->GetLevels().Contains(Leaf->PersistentLevel)))
        return false;
    Stream->SetShouldBeVisible(false);
    Stream->SetShouldBeVisibleInEditor(false);
    Parent->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    if (!Check(*this, TEXT("Native warmup hiding establishes a genuine stable loaded/hidden baseline"),
            Stream->GetCurrentState() == ULevelStreaming::ECurrentState::LoadedNotVisible &&
                Stream->GetLoadedLevel() == Leaf->PersistentLevel && Leaf->PersistentLevel->OwningWorld == Parent &&
                !Leaf->PersistentLevel->bIsVisible && Stream->ShouldBeLoaded() && !Stream->GetShouldBeVisibleFlag() &&
                !Stream->GetShouldBeVisibleInEditor()))
        return false;
    const FPreserved StableHidden(Fixture, Registry);
    if (!StableHidden.Preflight(*this))
        return false;
    FVisibilityRefusals Hooks(*this, Fixture, Registry, Parent, Leaf, Stream);
    if (!Check(*this, TEXT("Ordinary native export loading completes after a reentrant transition refusal"),
            UP4AuthoringTools::LoadExportLevels(Parent)))
        return false;
    Check(*this,
        TEXT("Genuine visibility transition was observed exactly once and reaches its independent loaded result"),
        Hooks.VisibleCalls == 1 && Hooks.InvisibleCalls == 0 &&
            Stream->GetCurrentState() == ULevelStreaming::ECurrentState::LoadedVisible &&
            Stream->GetLoadedLevel() == Leaf->PersistentLevel && Leaf->PersistentLevel->bIsVisible &&
            Parent->GetLevels().Contains(Leaf->PersistentLevel));
    Stream->SetShouldBeVisible(false);
    Stream->SetShouldBeVisibleInEditor(false);
    Parent->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    Check(*this, TEXT("Ordinary native hiding completes after the actual MakingInvisible refusal"),
        Hooks.VisibleCalls == 1 && Hooks.InvisibleCalls == 1 &&
            Stream->GetCurrentState() == ULevelStreaming::ECurrentState::LoadedNotVisible &&
            Stream->GetLoadedLevel() == Leaf->PersistentLevel && !Leaf->PersistentLevel->bIsVisible &&
            Stream->ShouldBeLoaded() && !Stream->GetShouldBeVisibleFlag() && !Stream->GetShouldBeVisibleInEditor());
    Check(*this,
        TEXT("Independent lifecycle readback retains actual level ownership, stream inventory and no load override"),
        Leaf->PersistentLevel->OwningWorld == Parent && Parent->GetStreamingLevels().Num() == 1 &&
            Parent->GetStreamingLevels()[0] == Stream && Stream->PackageNameToLoad.IsNone());
    Check(*this,
        TEXT(
            "Intentional native visibility lifecycle never publishes Content, changes source bytes/metadata or dirties authoring packages"),
        Files.Same() && Registry.Snapshot() == RegistryBefore &&
            Bytes(Parent->GetOutermost()->GetMetaData()) == ParentMetadata &&
            Bytes(Leaf->GetOutermost()->GetMetaData()) == LeafMetadata && !Parent->GetOutermost()->IsDirty() &&
            !Leaf->GetOutermost()->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Streaming258RetiredTrackedWorld,
    "P4MapKit.Quality.Streaming258NativeRetirementClearsTrackedWeakStream",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Streaming258RetiredTrackedWorld::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4StreamingWorldGraph258;
    FP4StreamingSavedWorldFixture Fixture;
    FRegistryCustody Registry(Fixture, *this);
    UWorld* Old = Fixture.Make(TEXT("RetiredOwner"));
    UWorld* Parent = Fixture.Make(TEXT("LiveOwner"));
    UWorld* Leaf = Fixture.Make(TEXT("Leaf"));
    if (!Check(*this, TEXT("Actual shared child saved before owned authoring lifecycle"),
            Registry.SaveAndRegister(Fixture, Leaf)))
        return false;
    ULevelStreaming* OldStream = Fixture.Add(Old, Leaf, TEXT("OldChild"));
    ULevelStreaming* Stream = Fixture.Add(Parent, Leaf, TEXT("LiveChild"));
    OldStream->SetFlags(RF_Transactional);
    Stream->SetFlags(RF_Transactional);
    Old->GetOutermost()->SetDirtyFlag(true);
    Parent->GetOutermost()->SetDirtyFlag(true);
    {
        const FPreserved Before(Fixture, Registry);
        if (!Before.Preflight(*this))
            return false;
        Check(*this, TEXT("Actual owned no-op authoring remap registers the real native old stream"),
            UP4AuthoringTools::RemapStreaming(
                Old, Mapping(OldStream, P4StreamingAfter242Native::Row(Leaf->GetOutermost()->GetName())), false, true));
        Before.Check(*this);
    }
    const FString RetiredPath = Old->GetPathName();
    const TWeakObjectPtr<ULevelStreaming> Retired(OldStream);
    Old->DestroyWorld(false);
    GEngine->DestroyWorldContext(Old);
    Old->MarkObjectsPendingKill();
    Old->ClearFlags(RF_Public | RF_Standalone);
    Old->SetFlags(RF_Transient);
    // Remove only the truly retired World from live fixture cleanup ownership;
    // its public destruction/context teardown have already completed.
    Fixture.Worlds.Remove(Old);
    if (!Check(*this, TEXT("Genuine public world retirement invalidates its native weak stream without global GC"),
            Old->IsPendingKill() && OldStream->IsPendingKill() && !Retired.IsValid() && !Old->IsAsset() &&
                Text(Old->GetPathName(), RetiredPath) && !GEngine->GetWorldContextFromWorld(Old)))
        return false;
    const FPreserved Before(Fixture, Registry);
    if (!Before.Preflight(*this))
        return false;
    Check(*this, TEXT("Next real owned no-op authoring remap survives stale native weak custody"),
        UP4AuthoringTools::RemapStreaming(
            Parent, Mapping(Stream, P4StreamingAfter242Native::Row(Leaf->GetOutermost()->GetName())), false, true));
    Before.Check(*this);
    Check(*this, TEXT("Next actual remap retains unrelated saved child and live native stream invariants"),
        Stream->GetTypedOuter<UWorld>() == Parent && Stream->GetLoadedLevel() == nullptr &&
            Stream->PackageNameToLoad.IsNone() && !Stream->ShouldBeLoaded() && !Stream->GetShouldBeVisibleFlag() &&
            !Stream->GetShouldBeVisibleInEditor() && Parent->GetStreamingLevels().Num() == 1 &&
            Parent->GetStreamingLevels()[0] == Stream);
    return !HasAnyErrors();
}
#endif

#endif

// Private review draft: not built or executed. Uses native saved assets only.
#include "P4OriginalRecord.h"
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "AssetRegistryModule.h"
#include "Curves/CurveVector.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace P4ProjectInventoryCoverageNext
{
TArray<uint8> ObjectBytes(UObject* Object)
{
    TArray<uint8> Result;
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    return Result;
}
TArray<uint8> RegistryBytes(FAssetData Data)
{
    TArray<uint8> Result;
    FMemoryWriter Writer(Result);
    Data.SerializeForCache(Writer);
    return Result;
}

struct FInventoryFixture
{
    FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString Authored = TEXT("/Game/P4ProjectInventoryCoverage/") + Id;
    FString Original = TEXT("/Game/Carrot4/P4ProjectInventoryCoverage/") + Id;
    TArray<UObject*> Assets;
    TArray<UObject*> Published;
    TArray<UWorld*> Worlds;
    TArray<FString> Files;
    TArray<FString> ExpectedAbsentFiles;
    UClass* SceneClass = nullptr;
    FSoftObjectProperty* OpenLevel = nullptr;
    FAutomationTestBase& Test;

    explicit FInventoryFixture(FAutomationTestBase& In) : Test(In)
    {
        SceneClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.GameSceneData"));
        OpenLevel = SceneClass ? FindFProperty<FSoftObjectProperty>(SceneClass, TEXT("OpenLevel")) : nullptr;
    }
    bool Ready()
    {
        return Test.TestTrue(TEXT("Existing concrete game scene and native soft-world schema"),
            SceneClass && !SceneClass->HasAnyClassFlags(CLASS_Abstract) && OpenLevel &&
                OpenLevel->PropertyClass == UWorld::StaticClass() && GEngine);
    }
    ~FInventoryFixture()
    {
        for (UObject* Object : Published)
            FAssetRegistryModule::AssetDeleted(Object);
        for (UObject* Object : Assets)
        {
            Test.TestTrue(TEXT("Only exact GUID-owned native assets receive cleanup flags"),
                Object->GetOutermost()->GetName().StartsWith(Authored + TEXT("/"), ESearchCase::CaseSensitive) ||
                    Object->GetOutermost()->GetName().StartsWith(Original + TEXT("/"), ESearchCase::CaseSensitive));
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->SetFlags(RF_Transient);
        }
        for (int32 I = Worlds.Num() - 1; I >= 0; --I)
        {
            Worlds[I]->DestroyWorld(false);
            GEngine->DestroyWorldContext(Worlds[I]);
        }
        for (UObject* Object : Assets)
        {
            Object->RemoveFromRoot();
        }
        // Only exact native filenames owned by this GUID fixture are removed.
        const FString AuthoredDirectory = FPaths::ConvertRelativePathToFull(
            FPaths::GetPath(FPackageName::LongPackageNameToFilename(Authored / TEXT("Marker"))));
        const FString OriginalDirectory = FPaths::ConvertRelativePathToFull(
            FPaths::GetPath(FPackageName::LongPackageNameToFilename(Original / TEXT("Marker"))));
        for (const FString& File : Files)
        {
            const FString Absolute = FPaths::ConvertRelativePathToFull(File);
            const bool Owned = Absolute.StartsWith(AuthoredDirectory + TEXT("/"), ESearchCase::CaseSensitive) ||
                Absolute.StartsWith(OriginalDirectory + TEXT("/"), ESearchCase::CaseSensitive);
            if (!Test.TestTrue(TEXT("Cleanup filename lies within exact GUID-owned Content roots"), Owned))
                continue;
            Test.TestTrue(TEXT("Owned native saved fixture file deletion succeeds"),
                IFileManager::Get().Delete(*File, false, true));
            Test.TestFalse(
                TEXT("Owned native saved fixture file is absent after cleanup"), IFileManager::Get().FileExists(*File));
        }
    }
    UObject* Keep(UObject* Object)
    {
        if (Object)
        {
            Object->AddToRoot();
            Assets.Add(Object);
            Object->GetOutermost()->GetMetaData()->SetValue(Object, TEXT("P4InventorySentinel"), *Id);
        }
        return Object;
    }
    UWorld* World(const TCHAR* Name)
    {
        UPackage* Package = CreatePackage(*(Authored / Name));
        UWorld* Result = UWorld::CreateWorld(EWorldType::Editor, false, FName(Name), Package);
        if (!Test.TestNotNull(TEXT("Genuine initialized owned editor world"), Result))
            return nullptr;
        GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(Result);
        Result->SetFlags(RF_Public | RF_Standalone);
        Keep(Result);
        Worlds.Add(Result);
        return Result;
    }
    UObject* Scene(const TCHAR* Name, const FSoftObjectPath& Path, bool Game = false)
    {
        UPackage* Package = CreatePackage(*((Game ? Original : Authored) / Name));
        UObject* Result = Keep(
            NewObject<UObject>(Package, SceneClass, FName(Name), RF_Public | RF_Standalone | RF_Transactional));
        if (!Test.TestNotNull(TEXT("Actual native game-scene instance"), Result))
            return nullptr;
        OpenLevel->SetPropertyValue_InContainer(Result, FSoftObjectPtr(Path));
        if (!Save(Result, false))
            return nullptr;
        if (!Game)
            return Result;
        // A game scene is one the import published with its original record, wherever it lies.
        FP4OriginalRecord Record;
        Record.Package = Package->GetName();
        Record.Class = Result->GetClass()->GetPathName();
        Record.Kind = TEXT("inventory-fixture");
        Record.SourceDigestKind = TEXT("fixture");
        Record.ComparisonSchema = TEXT("fixture");
        Record.SourceDigest = FString::ChrN(64, TEXT('a'));
        Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
        FString Error;
        return Test.TestTrue(TEXT("The game scene has its original record"), P4MarkOriginalAsset(Result, Record, Error))
            ? Result
            : nullptr;
    }
    UCurveVector* Curve(const TCHAR* Name)
    {
        UPackage* Package = CreatePackage(*(Authored / Name));
        UCurveVector* Result = Cast<UCurveVector>(
            Keep(NewObject<UCurveVector>(Package, FName(Name), RF_Public | RF_Standalone)));
        if (!Result)
            return nullptr;
        Result->FloatCurves[1].AddKey(1.25f, 37.5f);
        return Save(Result, false) ? Result : nullptr;
    }
    bool Save(UObject* Object, bool Map)
    {
        if (!Object)
            return false;
        const FString File = FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(),
            Map ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
        Files.AddUnique(File);
        if (!Test.TestTrue(
                TEXT("Owned native save directory"), IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) ||
            !Test.TestTrue(TEXT("Ordinary native package save"),
                UPackage::SavePackage(Object->GetOutermost(), Object, RF_Public | RF_Standalone, *File, GError, nullptr,
                    false, false, SAVE_NoError, nullptr, FDateTime::MinValue(), false)))
            return false;
        if (!Published.Contains(Object))
        {
            FAssetRegistryModule::AssetCreated(Object);
            Published.Add(Object);
        }
        return Test.TestTrue(TEXT("Genuine registry entry and native disk package both exist"),
            FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FName(*Object->GetPathName())).IsValid() &&
                FPackageName::DoesPackageExist(Object->GetOutermost()->GetName()));
    }
};

struct FObjectSnapshot
{
    UObject* Object;
    UObject* Outer;
    UClass* Class;
    FString Path;
    EObjectFlags Flags;
    TArray<uint8> Bytes;
    explicit FObjectSnapshot(UObject* In)
        : Object(In), Outer(In->GetOuter()), Class(In->GetClass()), Path(In->GetPathName()), Flags(In->GetFlags()),
          Bytes(ObjectBytes(In))
    {}
    bool Same() const
    {
        return Object->GetOuter() == Outer && Object->GetClass() == Class &&
            Object->GetPathName().Equals(Path, ESearchCase::CaseSensitive) && Object->GetFlags() == Flags &&
            ObjectBytes(Object) == Bytes;
    }
};
struct FInventoryGuard
{
    struct FPackageSnapshot
    {
        UPackage* Package;
        uint32 Flags;
        bool Dirty;
        TArray<uint8> Metadata;
        TArray<UObject*> Children;
        TArray<FObjectSnapshot> Objects;
        explicit FPackageSnapshot(UPackage* In)
            : Package(In), Flags(In->GetPackageFlags()), Dirty(In->IsDirty()), Metadata(ObjectBytes(In->GetMetaData()))
        {
            GetObjectsWithOuter(In, Children, true);
            for (UObject* Object : Children)
                Objects.Emplace(Object);
        }
        bool Same() const
        {
            TArray<UObject*> Now;
            GetObjectsWithOuter(Package, Now, true);
            if (Now.Num() != Children.Num() || Package->GetPackageFlags() != Flags || Package->IsDirty() != Dirty ||
                ObjectBytes(Package->GetMetaData()) != Metadata)
                return false;
            for (UObject* Object : Children)
                if (!Now.Contains(Object))
                    return false;
            for (const FObjectSnapshot& Object : Objects)
                if (!Object.Same())
                    return false;
            return true;
        }
    };
    FInventoryFixture& Fixture;
    TArray<FPackageSnapshot> Packages;
    TArray<FString> Names;
    TMap<FString, TArray<uint8>> Payloads;
    TMap<UObject*, TArray<uint8>> Registry;
    struct FWorldGraph
    {
        UWorld* World;
        ULevel* Persistent;
        ULevel* Current;
        TArray<ULevel*> Levels;
        TArray<ULevelStreaming*> Streams;
        explicit FWorldGraph(UWorld* In)
            : World(In), Persistent(In->PersistentLevel), Current(In->GetCurrentLevel()), Levels(In->GetLevels()),
              Streams(In->GetStreamingLevels())
        {}
        bool Same() const
        {
            return World->PersistentLevel == Persistent && World->GetCurrentLevel() == Current &&
                World->GetLevels() == Levels && World->GetStreamingLevels() == Streams;
        }
    };
    TArray<FWorldGraph> Graphs;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    bool Owns(UObject* Object) const
    {
        if (!Object)
            return false;
        for (UObject* Asset : Fixture.Assets)
            if (Object->GetOutermost() == Asset->GetOutermost())
                return true;
        return false;
    }
    void CheckExpectedFiles(const TArray<FString>& Actual) const
    {
        for (const FString& File : Fixture.Files)
        {
            const bool Absent = Fixture.ExpectedAbsentFiles.Contains(File);
            Fixture.Test.TestTrue(
                *FString::Printf(TEXT("Expected saved native fixture file %s: %s"),
                    Absent ? TEXT("deliberately absent") : TEXT("included in full disk census"), *File),
                Absent ? (!Actual.Contains(File) && !IFileManager::Get().FileExists(*File))
                       : (Actual.Contains(File) && IFileManager::Get().FileExists(*File)));
        }
    }
    explicit FInventoryGuard(FInventoryFixture& In) : Fixture(In)
    {
        for (UObject* Object : Fixture.Assets)
            Packages.Emplace(Object->GetOutermost());
        for (UWorld* World : Fixture.Worlds)
            Graphs.Emplace(World);
        for (const FString& Root : {Fixture.Authored, Fixture.Original})
        {
            const FString Directory = FPaths::GetPath(FPackageName::LongPackageNameToFilename(Root / TEXT("Marker")));
            IFileManager::Get().FindFilesRecursive(Names, *Directory, TEXT("*"), true, false, false);
        }
        Names.Sort();
        CheckExpectedFiles(Names);
        for (const FString& Name : Names)
        {
            TArray<uint8> Bytes;
            Fixture.Test.TestTrue(
                TEXT("Independent whole-file snapshot succeeds"), FFileHelper::LoadFileToArray(Bytes, *Name));
            Payloads.Add(Name, MoveTemp(Bytes));
        }
        for (UObject* Object : Fixture.Published)
            Registry.Add(Object,
                RegistryBytes(FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FName(*Object->GetPathName()))));
        A = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Object, const FEditPropertyChain&)
            {
                if (Owns(Object))
                    ++Pre;
            });
        B = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Object, FPropertyChangedEvent&)
            {
                if (Owns(Object))
                    ++Post;
            });
    }
    ~FInventoryGuard()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    }
    void Check()
    {
        for (const FPackageSnapshot& Package : Packages)
            Fixture.Test.TestTrue(
                TEXT("Every owned object/descendant byte, metadata, flags, class, path and dirty state retained"),
                Package.Same());
        for (const FWorldGraph& Graph : Graphs)
            Fixture.Test.TestTrue(
                TEXT("World persistent/current levels and loaded/streaming membership retained"), Graph.Same());
        TArray<FString> Now;
        for (const FString& Root : {Fixture.Authored, Fixture.Original})
        {
            const FString Directory = FPaths::GetPath(FPackageName::LongPackageNameToFilename(Root / TEXT("Marker")));
            IFileManager::Get().FindFilesRecursive(Now, *Directory, TEXT("*"), true, false, false);
        }
        Now.Sort();
        CheckExpectedFiles(Now);
        Fixture.Test.TestTrue(TEXT("Exact owned disk file census unchanged"), Now == Names);
        for (const FString& Name : Names)
        {
            TArray<uint8> Bytes;
            Fixture.Test.TestTrue(TEXT("Whole saved native file retained byte-for-byte"),
                FFileHelper::LoadFileToArray(Bytes, *Name) && Bytes == Payloads.FindChecked(Name));
        }
        for (const auto& Pair : Registry)
        {
            const FAssetData NowAsset = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(
                FName(*Pair.Key->GetPathName()));
            Fixture.Test.TestTrue(TEXT("Exact real registry identities, tags, chunks and package flags retained"),
                NowAsset.IsValid() && RegistryBytes(NowAsset) == Pair.Value);
        }
        Fixture.Test.TestEqual(TEXT("Inventory emits no owned pre-edit callback"), Pre, 0);
        Fixture.Test.TestEqual(TEXT("Inventory emits no owned post-edit callback"), Post, 0);
    }
};

TSharedPtr<FJsonObject> Read(FAutomationTestBase& Test, const FString& Json)
{
    TSharedPtr<FJsonObject> Result;
    if (!Test.TestTrue(TEXT("Public inventory response is a JSON object"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Result) && Result.IsValid()))
        return nullptr;
    return Result;
}
TSharedPtr<FJsonObject> Binding(
    FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Root, UObject* Scene, bool Present)
{
    if (!Root)
        return nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Test.TestTrue(TEXT("Public scene inventory has array"), Root->TryGetArrayField(TEXT("scenes"), Rows)))
        return nullptr;
    TSharedPtr<FJsonObject> Found;
    int32 Count = 0;
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Path;
        if (Value.IsValid() && Value->TryGetObject(Row) && (*Row)->TryGetStringField(TEXT("scene"), Path) &&
            Path.Equals(Scene->GetPathName(), ESearchCase::CaseSensitive))
        {
            Found = *Row;
            ++Count;
        }
    }
    Test.TestEqual(TEXT("Exact owned scene inventory cardinality"), Count, Present ? 1 : 0);
    return Found;
}
void CheckPlan(FAutomationTestBase& Test, const FString& World, const FString& Route, const FString& Scene, bool Valid,
    const FString& Error, const FString& SecondError = TEXT(""))
{
    const auto Plan = Read(Test, UP4AuthoringTools::NativeWorldExportPlan(World));
    if (!Plan)
        return;
    FString ActualWorld, ActualRoute, ActualScene;
    bool Full = false, Strip = false, ActualValid = !Valid;
    Test.TestTrue(TEXT("World plan preserves exact requested package"),
        Plan->TryGetStringField(TEXT("world"), ActualWorld) && ActualWorld.Equals(World, ESearchCase::CaseSensitive));
    Test.TestTrue(TEXT("Expected public export route"),
        Plan->TryGetStringField(TEXT("route"), ActualRoute) && ActualRoute.Equals(Route, ESearchCase::CaseSensitive));
    Test.TestTrue(TEXT("Expected exact scene identity"),
        Plan->TryGetStringField(TEXT("scene"), ActualScene) && ActualScene.Equals(Scene, ESearchCase::CaseSensitive));
    Test.TestTrue(TEXT("Full scene export remains mandatory"), Plan->TryGetBoolField(TEXT("full_scene"), Full) && Full);
    Test.TestTrue(TEXT("Game asset stripping remains mandatory"),
        Plan->TryGetBoolField(TEXT("strip_game_assets"), Strip) && Strip);
    Test.TestTrue(TEXT("Exact plan admission verdict"),
        Plan->TryGetBoolField(TEXT("valid"), ActualValid) && ActualValid == Valid);
    const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
    if (!Test.TestTrue(TEXT("Public plan error array"), Plan->TryGetArrayField(TEXT("errors"), Errors)))
        return;
    const int32 ExpectedCount = Valid ? 0 : (SecondError.IsEmpty() ? 1 : 2);
    Test.TestEqual(TEXT("Exact error cardinality"), Errors->Num(), ExpectedCount);
    if (Errors->Num() == ExpectedCount && !Valid)
    {
        Test.TestTrue(
            TEXT("Exact first meaningful refusal"), (*Errors)[0]->AsString().Equals(Error, ESearchCase::CaseSensitive));
        if (ExpectedCount == 2)
            Test.TestTrue(TEXT("Exact scene-specific refusal"),
                (*Errors)[1]->AsString().Equals(SecondError, ESearchCase::CaseSensitive));
    }
}
} //namespace P4ProjectInventoryCoverageNext

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InventoryNativeSavedRoutes, "P4MapKit.Quality.InventoryNativeSavedRoutes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InventoryNativeSavedRoutes::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4ProjectInventoryCoverageNext;
    FInventoryFixture F(*this);
    if (!F.Ready())
        return false;
    UWorld* Unbound = F.World(TEXT("Unbound"));
    UWorld* Authored = F.World(TEXT("AuthoredWorld"));
    UWorld* Original = F.World(TEXT("OriginalWorld"));
    if (!F.Save(Unbound, true) || !F.Save(Authored, true) || !F.Save(Original, true))
        return false;
    UObject* A = F.Scene(TEXT("AuthoredScene"), FSoftObjectPath(Authored->GetPathName()));
    UObject* O = F.Scene(TEXT("OriginalScene"), FSoftObjectPath(Original->GetPathName()), true);
    UObject* Parent = F.Scene(TEXT("ParentScene"), FSoftObjectPath());
    if (!A || !O || !Parent)
        return false;
    FInventoryGuard Guard(F);
    const auto Inventory = Read(*this, UP4AuthoringTools::NativeProjectInventory());
    if (Inventory)
    {
        FString Source;
        double Format = 0;
        TestTrue(TEXT("Native Content Browser is the inventory source"),
            Inventory->TryGetStringField(TEXT("source"), Source) && Source == TEXT("native-editor-assets"));
        TestTrue(TEXT("Public inventory format"), Inventory->TryGetNumberField(TEXT("format"), Format) && Format == 1);
    }
    for (UObject* Scene : {A, O})
    {
        const auto Row = Binding(*this, Inventory, Scene, true);
        bool AuthoredFlag = false, Saved = false;
        FString Error, World;
        if (Row)
        {
            TestTrue(TEXT("Real saved scene world binding"),
                Row->TryGetStringField(TEXT("world"), World) &&
                    World.Equals(
                        (Scene == A ? Authored : Original)->GetOutermost()->GetName(), ESearchCase::CaseSensitive));
            TestTrue(TEXT("Authored/original route follows actual scene package"),
                Row->TryGetBoolField(TEXT("authored"), AuthoredFlag) && AuthoredFlag == (Scene == A));
            TestTrue(TEXT("Genuine saved native world recognized"),
                Row->TryGetBoolField(TEXT("saved_world"), Saved) && Saved);
            TestTrue(
                TEXT("Saved route has no diagnostic"), Row->TryGetStringField(TEXT("error"), Error) && Error.IsEmpty());
        }
    }
    Binding(*this, Inventory, Parent, false);
    CheckPlan(*this, Unbound->GetOutermost()->GetName(), TEXT("unbound"), TEXT(""), true, TEXT(""));
    CheckPlan(*this, Authored->GetOutermost()->GetName(), TEXT("independent"), A->GetPathName(), true, TEXT(""));
    CheckPlan(
        *this, Authored->GetOutermost()->GetName().ToLower(), TEXT("independent"), A->GetPathName(), true, TEXT(""));
    CheckPlan(*this, Original->GetOutermost()->GetName(), TEXT("original"), O->GetPathName(), true, TEXT(""));
    Guard.Check();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InventoryNativeDuplicateSceneBindings,
    "P4MapKit.Quality.InventoryNativeDuplicateSceneBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InventoryNativeDuplicateSceneBindings::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4ProjectInventoryCoverageNext;
    FInventoryFixture F(*this);
    if (!F.Ready())
        return false;
    UWorld* World = F.World(TEXT("SharedWorld"));
    if (!F.Save(World, true))
        return false;
    UObject* A = F.Scene(TEXT("FirstScene"), FSoftObjectPath(World->GetPathName()));
    UObject* B = F.Scene(TEXT("SecondScene"), FSoftObjectPath(World->GetPathName()));
    if (!A || !B)
        return false;
    FInventoryGuard Guard(F);
    const auto Inventory = Read(*this, UP4AuthoringTools::NativeProjectInventory());
    Binding(*this, Inventory, A, true);
    Binding(*this, Inventory, B, true);
    CheckPlan(*this, World->GetOutermost()->GetName(), TEXT("unbound"), TEXT(""), false,
        TEXT("Multiple native scene assets refer to ") + World->GetOutermost()->GetName());
    Guard.Check();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InventoryNativeWorldAvailabilityRefusals,
    "P4MapKit.Quality.InventoryNativeWorldAvailabilityRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InventoryNativeWorldAvailabilityRefusals::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4ProjectInventoryCoverageNext;
    FInventoryFixture F(*this);
    if (!F.Ready())
        return false;
    UWorld* Unsaved = F.World(TEXT("UnsavedWorld"));
    UWorld* Missing = F.World(TEXT("MissingWorld"));
    if (!Unsaved || !F.Save(Missing, true))
        return false;
    UCurveVector* Wrong = F.Curve(TEXT("NotAWorld"));
    if (!Wrong)
        return false;
    UObject* U = F.Scene(TEXT("UnsavedScene"), FSoftObjectPath(Unsaved->GetPathName()));
    UObject* M = F.Scene(TEXT("MissingScene"), FSoftObjectPath(Missing->GetPathName()));
    UObject* W = F.Scene(TEXT("WrongKindScene"), FSoftObjectPath(Wrong->GetPathName()));
    if (!U || !M || !W)
        return false;
    const FString MissingFile = FPackageName::LongPackageNameToFilename(
        Missing->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension());
    if (!TestTrue(TEXT("Owned saved map deliberately removed while real registry entry remains"),
            IFileManager::Get().Delete(*MissingFile, true, true)))
        return false;
    F.ExpectedAbsentFiles.Add(MissingFile);
    TestTrue(TEXT("Real registered world retained for missing-disk boundary"),
        FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FName(*Missing->GetPathName())).IsValid());
    FInventoryGuard Guard(F);
    const auto Inventory = Read(*this, UP4AuthoringTools::NativeProjectInventory());
    for (UObject* Scene : {U, M, W})
    {
        const FString Package =
            F.OpenLevel->GetPropertyValue_InContainer(Scene).ToSoftObjectPath().GetLongPackageName();
        const auto Row = Binding(*this, Inventory, Scene, true);
        bool Saved = true;
        FString Error;
        if (Row)
        {
            TestTrue(TEXT("Unavailable or wrong-kind native package refuses saved-world status"),
                Row->TryGetBoolField(TEXT("saved_world"), Saved) && !Saved);
            TestTrue(TEXT("Scene provides meaningful world restoration diagnostic"),
                Row->TryGetStringField(TEXT("error"), Error) &&
                    Error.Equals(
                        TEXT("Save or restore the world referenced by ") + Scene->GetPathName() + TEXT(": ") + Package,
                        ESearchCase::CaseSensitive));
        }
        // Both the world admission error and its sole scene binding error remain visible.
        CheckPlan(*this, Package, TEXT("independent"), Scene->GetPathName(), false,
            TEXT("Save or restore the level before exporting: ") + Package,
            TEXT("Save or restore the world referenced by ") + Scene->GetPathName() + TEXT(": ") + Package);
    }
    CheckPlan(*this, F.Authored / TEXT("AbsentUnbound"), TEXT("unbound"), TEXT(""), false,
        TEXT("Save or restore the level before exporting: ") + (F.Authored / TEXT("AbsentUnbound")));
    Guard.Check();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4InventoryNativePathRefusals, "P4MapKit.Quality.InventoryNativePathRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4InventoryNativePathRefusals::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4ProjectInventoryCoverageNext;
    FInventoryFixture F(*this);
    if (!F.Ready())
        return false;
    UWorld* World = F.World(TEXT("ValidWorld"));
    UWorld* Other = F.World(TEXT("OtherWorld"));
    if (!F.Save(World, true) || !F.Save(Other, true))
        return false;
    const FString Package = World->GetOutermost()->GetName();
    UObject* Subobject = F.Scene(
        TEXT("SubobjectScene"), FSoftObjectPath(FName(*World->GetPathName()), TEXT("PersistentLevel")));
    UObject* WrongName = F.Scene(
        TEXT("WrongNameScene"), FSoftObjectPath(Other->GetOutermost()->GetName() + TEXT(".DifferentAsset")));
    UObject* Outside = F.Scene(TEXT("EngineScene"), FSoftObjectPath(TEXT("/Engine/P4InventoryCoverage/World.World")));
    if (!Subobject || !WrongName || !Outside)
        return false;
    FInventoryGuard Guard(F);
    const auto Inventory = Read(*this, UP4AuthoringTools::NativeProjectInventory());
    for (UObject* Scene : {Subobject, WrongName, Outside})
    {
        const auto Row = Binding(*this, Inventory, Scene, true);
        FString Error;
        bool Saved = true;
        if (Row)
        {
            TestTrue(TEXT("Subobject, wrong asset name and non-Game references reject with exact scene identity"),
                Row->TryGetStringField(TEXT("error"), Error) &&
                    Error.Equals(TEXT("Scene OpenLevel must reference its world asset: ") + Scene->GetPathName(),
                        ESearchCase::CaseSensitive));
            TestTrue(TEXT("Malformed scene does not claim a saved world"),
                Row->TryGetBoolField(TEXT("saved_world"), Saved) && !Saved);
        }
    }
    for (const FString& Bad : {FString(TEXT("")), FString(TEXT("/Engine/P4InventoryCoverage/World")),
             F.Authored / TEXT("Bad Space"), F.Authored / TEXT("Dot.Name")})
        CheckPlan(*this, Bad, TEXT("unbound"), TEXT(""), false, TEXT("Expected a saved /Game world package"));
    CheckPlan(*this, Package, TEXT("independent"), Subobject->GetPathName(), false,
        TEXT("Scene OpenLevel must reference its world asset: ") + Subobject->GetPathName());
    CheckPlan(*this, Other->GetOutermost()->GetName(), TEXT("independent"), WrongName->GetPathName(), false,
        TEXT("Scene OpenLevel must reference its world asset: ") + WrongName->GetPathName());
    Guard.Check();
    return !HasAnyErrors();
}
#endif

// Check editing and file access on a real import, and restrictions on placed actor edits.
#include "P4KadanAssetPolicy.h"
#include "P4KadanEditor.h"
#include "P4KadanImport.h"
#include "P4KadanReadOnly.h"
#include "P4SoundReference.h"
#include "P4CreatorSource.h"
#include "P4OriginalAssetImport.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserAssetDataPayload.h"
#include "ContentBrowserAssetDataSource.h"
#include "Containers/Ticker.h"
#include "DataTableEditorUtils.h"
#include "Editor.h"
#include "EditorReimportHandler.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "IDetailsView.h"
#include "ISinglePropertyView.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "PhysicsEngine/BodySetup.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "RenderingThread.h"
#include "Serialization/ObjectWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/SimpleAssetEditor.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SWindow.h"
#include "Widgets/SNullWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace P4FixtureRetirement
{
TSet<FName> LivePackages();
bool Roots(const TArray<UObject*>& Owned, const TArray<UObject*>& Direct);
bool Package(UPackage* Made);
} //namespace P4FixtureRetirement

namespace P4KadanReadOnlyCave000
{
TArray<FString> ContentFiles()
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(
        Files, *FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), TEXT("*"), true, false);
    for (FString& File : Files)
        File = FPaths::ConvertRelativePathToFull(File);
    return Files;
}

// Takes what the test made out of the editor and off the disk.
bool Remove(const TArray<FString>& Files, const TSet<FName>& Preserve = {})
{
    TArray<UPackage*> Packages;
    for (const FString& File : Files)
    {
        FString Package;
        if (FPackageName::IsPackageFilename(File) && FPackageName::TryConvertFilenameToLongPackageName(File, Package))
            if (UPackage* Made = FindPackage(nullptr, *Package))
                Packages.AddUnique(Made);
    }
    bool Retired = true;
    TSet<FName> Kept = Preserve;
    for (UPackage* Made : Packages)
    {
        if (Preserve.Contains(Made->GetFName()))
            continue;
        if (!P4FixtureRetirement::Package(Made))
        {
            Kept.Add(Made->GetFName());
            Retired = false;
        }
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
    for (const FString& File : Files)
    {
        FString Name;
        if (FPackageName::TryConvertFilenameToLongPackageName(FPaths::ChangeExtension(File, TEXT("uasset")), Name) &&
            Kept.Contains(FName(*Name)))
            continue;
        if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true, true))
            Retired = false;
        for (FString Folder = FPaths::GetPath(File);
            FPaths::IsUnderDirectory(Folder, Content) && !FPaths::IsSamePath(Folder, Content);
            Folder = FPaths::GetPath(Folder))
            if (!IFileManager::Get().DeleteDirectory(*Folder, false, false))
                break;
    }
    return Retired;
}

// What the test made is listed in a file of its own while it runs, so that a run cut off is cleaned up
// by the next one.
struct FMade
{
    FString List;
    TSet<FString> Before;
    TSet<FName> LiveBefore;
    TArray<TWeakObjectPtr<UObject>> Copies;
    void TrackCopy(UObject* Copy)
    {
        if (!Copy)
            return;
        // DuplicateAsset does not save its package. Track only the returned asset and
        // its owned objects, leaving any unrelated objects in its package alone.
        const auto TrackOwned = [&](UObject* Owner)
        {
            if (!Owner)
                return;
            Copies.AddUnique(Owner);
            TArray<UObject*> Inside;
            GetObjectsWithOuter(Owner, Inside, true);
            for (UObject* Object : Inside)
                Copies.AddUnique(Object);
        };
        TrackOwned(Copy);
        // Generated classes have the package as their outer, rather than the Blueprint.
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Copy))
        {
            const auto TrackClass = [&](UClass* Class)
            {
                if (!Class || Class->GetOutermost() != Copy->GetOutermost() || Class->ClassGeneratedBy != Blueprint)
                    return;
                TrackOwned(Class);
                if (UObject* Default = Class->GetDefaultObject(false))
                    if (Default->GetClass() == Class && Default->GetOutermost() == Copy->GetOutermost())
                        TrackOwned(Default);
            };
            TrackClass(Blueprint->GeneratedClass);
            TrackClass(Blueprint->SkeletonGeneratedClass);
        }
    }
    bool RemoveCopies()
    {
        // Finish commands which still read the copies before retiring them. Their native
        // material-instance resources must release their parent links while game parents
        // are still alive, before Remove marks imported packages pending kill.
        FlushRenderingCommands();
        TArray<UObject*> Owned, Direct;
        for (const auto& Held : Copies)
            if (UObject* Object = Held.Get())
            {
                Owned.AddUnique(Object);
                if (Object->GetOuter() && Object->GetOuter()->IsA<UPackage>())
                    Direct.AddUnique(Object);
            }
        if (!P4FixtureRetirement::Roots(Owned, Direct))
            return false;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        FlushRenderingCommands();
        Copies.Empty();
        return true;
    }
    TArray<FString> NewSince(const TSet<FString>& Files) const
    {
        TArray<FString> Out;
        for (const FString& File : ContentFiles())
            if (!Files.Contains(File))
                Out.Add(File);
        return Out;
    }
    bool Ready = false;
    FMade() : List(FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests/KadanReadOnlyCave000/before.txt"))
    {
        if (IFileManager::Get().FileExists(*List))
        {
            TArray<FString> Lines;
            if (!FFileHelper::LoadFileToStringArray(Lines, *List))
                return;
            if (!Remove(NewSince(TSet<FString>(Lines)), P4FixtureRetirement::LivePackages()))
                return;
        }
        LiveBefore = P4FixtureRetirement::LivePackages();
        Before = TSet<FString>(ContentFiles());
        Ready = IFileManager::Get().MakeDirectory(*FPaths::GetPath(List), true) &&
            FFileHelper::SaveStringArrayToFile(
                Before.Array(), *List, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    ~FMade()
    {
        if (!RemoveCopies())
            return;
        if (Ready && Remove(NewSince(Before), LiveBefore))
            IFileManager::Get().Delete(*List, false, true, true);
    }
};

FString Shown(const TArray<FString>& Messages)
{
    return FString::Join(Messages, TEXT(" / "));
}

bool PlainText(const FString& Message)
{
    return !Message.Contains(TEXT(":")) && !Message.Contains(TEXT(";")) && !Message.Contains(TEXT("—"));
}

} //namespace P4KadanReadOnlyCave000

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyCave000Test, "P4MapKit.Kadan.ReadOnlyCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanReadOnlyCave000Test::RunTest(const FString&)
{
    using namespace P4KadanReadOnlyCave000;
    // Told in the log as well, so that a run that stops on the way still says how far it came.
    const auto Note = [this](const FString& Line)
    {
        AddInfo(Line);
        UE_LOG(LogTemp, Display, TEXT("KadanReadOnlyTest %s"), *Line);
    };
    if (!GEditor || !FSlateApplication::IsInitialized())
    {
        AddError(TEXT("The placed actor test needs the Slate editor."));
        return false;
    }
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddError(TEXT("The placed actor test needs P4_NATIVE_ROOT."));
        return false;
    }
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Root));
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    for (const FString& Needed : {Game})
        if (!TestTrue(TEXT("The test's input is there: ") + Needed, IFileManager::Get().DirectoryExists(*Needed)))
            return false;
    P4RegisterKadanEditor();
    P4Kadan::InitializePropertyGuards();
    auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    IPlatformFile& Disk = FPlatformFileManager::Get().GetPlatformFile();
    const auto Flush = [&]()
    {
        FTicker::GetCoreTicker().Tick(0.2f);
        P4Kadan::FlushReadOnlyGuards();
        // This synchronous test pumps the core ticker between UI routes, rather than an
        // entire engine frame. Match the main loop's public per-frame Modify tracker reset
        // so each simulated route invokes UObject::Modify's real notification.
        FCoreUObjectDelegates::ObjectsModifiedThisFrame.Reset();
    };

    GEditor->NewMap();
    FMade Made;
    if (!TestTrue(TEXT("the test can keep its exact import cleanup list"), Made.Ready))
        return false;
    ON_SCOPE_EXIT
    {
        GEditor->NewMap();
        Editors->CloseAllAssetEditors();
    };
    FString MapFile;
    FPackageName::TryConvertLongPackageNameToFilename(Map, MapFile, TEXT(".umap"));
    if (!TestFalse(TEXT("The project has no Cave000 before the test"), IFileManager::Get().FileExists(*MapFile)))
        return false;

    // Import the map and its dependencies.
    TestNotNull(TEXT("The project's own game WorldSettings class loads"),
        LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.MyWorldSettings")));
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Game;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    const FP4KadanImportResult Result = P4KadanImportMap(Settings, Map, true);
    TestFalse(TEXT("The import was not cancelled"), Result.Cancelled);
    Note(FString::Printf(TEXT("Map import complete %d. Refused packages %d. Imported level objects %d."), Result.Ok,
        Result.NotImported.Num(), Result.LevelObjects));
    for (const FString& Problem : Result.Problems)
        Note(TEXT("Map import problem ") + Problem);
    P4Kadan::TakeReadOnlyMessages();
    Flush();
    TestEqual(TEXT("The import is not taken for an edit"), Shown(P4Kadan::TakeReadOnlyMessages()), FString());

    // The imported assets by package. A map's own objects are released when the editor closes the map, so only the
    // others are held.
    TMap<FString, TWeakObjectPtr<UObject>> Assets;
    TArray<TStrongObjectPtr<UObject>> HeldAssets;
    for (const auto& Pair : Result.Imported)
    {
        TArray<FAssetData> Found;
        Registry.GetAssetsByPackageName(FName(*Pair.Key), Found);
        UObject* Asset = Found.Num() ? Found[0].GetAsset() : nullptr;
        if (!TestNotNull(TEXT("Imported asset resolves ") + Pair.Key, Asset))
            continue;
        Assets.Add(Pair.Key, Asset);
        UPackage* OwningPackage = Asset->GetOutermost();
        if (!Asset->IsA<UWorld>() && !OwningPackage->HasAnyPackageFlags(PKG_ContainsMap) &&
            !UWorld::FindWorldInPackage(OwningPackage))
            HeldAssets.Emplace(Asset);
        FString File;
        const bool Exists = FPackageName::DoesPackageExist(Pair.Key, nullptr, &File);
        const auto Policy = P4Kadan::GetAssetPolicy(Asset);
        TestEqual(TEXT("Editing follows recovered data ") + Pair.Key, Policy.AllowEdit, !Policy.ReferenceOnly);
        TestTrue(TEXT("Imported package file is writable ") + Pair.Key, Exists && !Disk.IsReadOnly(*File));
        TestEqual(TEXT("Content Browser follows reference protection ") + Pair.Key,
            Tools.AllPassWritableFolderFilter({Pair.Key}), Policy.AllowEdit);
    }

    // (c) A game level takes the edits Tsuchi can apply and refuses the others, naming the actor and property.
    {
        TestTrue(TEXT("The map opens"), FEditorFileUtils::LoadMap(MapFile, false, false));
        UWorld* World = GEditor->GetEditorWorldContext().World();
        TArray<AActor*> GameActors;
        if (World && World->PersistentLevel)
            for (AActor* Actor : World->PersistentLevel->Actors)
                if (Actor && P4Kadan::GameActorOf(Actor) == Actor && Actor->GetRootComponent())
                    GameActors.Add(Actor);
        Note(FString::Printf(TEXT("%d game actors with a root in the persistent level."), GameActors.Num()));
        if (TestTrue(TEXT("The level has game actors"), GameActors.Num() >= 4))
        {
            P4Kadan::TakeReadOnlyMessages();
            const auto Allowed = [&](const FString& What)
            {
                Flush();
                const TArray<FString> Told = P4Kadan::TakeReadOnlyMessages();
                TestEqual(TEXT("Level edit allowed: ") + What + TEXT(" ") + Shown(Told), Told.Num(), 0);
                Note(TEXT("Level edit ") + What +
                    (Told.Num() ? TEXT(" FAILED, refused. ") + Shown(Told) : FString(TEXT(" allowed"))));
            };
            const auto Refused = [&](const FString& What, AActor* Actor, const FString& Property)
            {
                Flush();
                const TArray<FString> Told = P4Kadan::TakeReadOnlyMessages();
                const bool Named = Told.Num() == 1 && Told[0].Contains(Actor->GetActorLabel()) &&
                    (Property.IsEmpty() || Told[0].Contains(Property));
                TestTrue(
                    TEXT("Level edit refused with its actor and property: ") + What + TEXT(" ") + Shown(Told), Named);
                if (!Named)
                    Note(TEXT("Level edit ") + What + TEXT(" FAILED to be refused by name. ") + Shown(Told));
                for (const FString& Message : Told)
                    TestTrue(TEXT("Plain sentence: ") + Message, PlainText(Message));
                Note(TEXT("Refused. ") + Shown(Told));
            };
            // A move: the root's place.
            AActor* Moved = GameActors[0];
            USceneComponent* MovedRoot = Moved->GetRootComponent();
            FProperty* Location = FindFProperty<FProperty>(USceneComponent::StaticClass(), TEXT("RelativeLocation"));
            const FVector Was = MovedRoot->GetRelativeLocation();
            FVector There = Was + FVector(100, 0, 0);
            PropertyAccessUtil::SetPropertyValue_Object(Location, MovedRoot, Location, &There, INDEX_NONE,
                PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
            Allowed(TEXT("move"));
            TestTrue(TEXT("The move stays"), MovedRoot->GetRelativeLocation().Equals(There));
            // The editor's own route: a move reported as the actor's change, with the root's property.
            // Reading that property from the actor stopped the editor in the 0.1.0 build.
            {
                FEditPropertyChain Chain;
                Chain.AddHead(Location);
                Chain.SetActivePropertyNode(Location);
                Chain.SetActiveMemberPropertyNode(Location);
                static_cast<UObject*>(Moved)->PreEditChange(Chain);
                There += FVector(0, 50, 0);
                MovedRoot->SetRelativeLocation(There);
                FPropertyChangedEvent Event(Location, EPropertyChangeType::ValueSet);
                Event.MemberProperty = Location;
                FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Moved, Event);
                Allowed(TEXT("move reported on the actor"));
                TestTrue(TEXT("The move reported on the actor stays"), MovedRoot->GetRelativeLocation().Equals(There));
            }
            // A switch on the actor and an enum on its root.
            FProperty* Hidden = FindFProperty<FProperty>(AActor::StaticClass(), TEXT("bHidden"));
            if (TestNotNull(TEXT("bHidden"), Hidden))
            {
                const bool Hide = true;
                PropertyAccessUtil::SetPropertyValue_Object(Hidden, GameActors[1], Hidden, &Hide, INDEX_NONE,
                    PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
                Allowed(TEXT("switch"));
                TestTrue(TEXT("The switch stays"), GameActors[1]->IsHidden());
            }
            // A supported scalar also stays when a native setter omits the postchange broadcast.
            if (FBoolProperty* Flag = CastField<FBoolProperty>(Hidden))
            {
                AActor* Actor = GameActors[1];
                const bool WasHidden = Flag->GetPropertyValue_InContainer(Actor);
                FEditPropertyChain Chain;
                Chain.AddHead(Flag);
                Chain.SetActivePropertyNode(Flag);
                Chain.SetActiveMemberPropertyNode(Flag);
                static_cast<UObject*>(Actor)->PreEditChange(Chain);
                Flag->SetPropertyValue_InContainer(Actor, !WasHidden);
                Allowed(TEXT("supported scalar without post-change"));
                TestEqual(TEXT("The supported scalar stays without a postchange broadcast"),
                    Flag->GetPropertyValue_InContainer(Actor), !WasHidden);
            }
            FProperty* Mobility = FindFProperty<FProperty>(USceneComponent::StaticClass(), TEXT("Mobility"));
            if (TestNotNull(TEXT("Mobility"), Mobility))
            {
                uint8 Movable = uint8(EComponentMobility::Movable);
                PropertyAccessUtil::SetPropertyValue_Object(Mobility, GameActors[1]->GetRootComponent(), Mobility,
                    &Movable, INDEX_NONE, PropertyAccessUtil::EditorReadOnlyFlags,
                    EPropertyAccessChangeNotifyMode::Default);
                Allowed(TEXT("enum"));
            }
            // An asset by its path on an actual recovered scenery component. Cave000
            // uses Blueprint scenery actors, so native AStaticMeshActor is not required.
            AActor* MeshActor = nullptr;
            UStaticMeshComponent* MeshComponent = nullptr;
            TStrongObjectPtr<UWorld> FixtureWorld; // Released before the real NewMap cleanup below.
            for (const auto& Pair : Result.Imported)
            {
                if (Pair.Value != TEXT("World"))
                    continue;
                UWorld* SourceWorld = LoadObject<UWorld>(
                    nullptr, *(Pair.Key + TEXT(".") + FPackageName::GetLongPackageAssetName(Pair.Key)));
                if (!SourceWorld || !SourceWorld->PersistentLevel)
                    continue;
                for (AActor* Actor : SourceWorld->PersistentLevel->Actors)
                {
                    if (!Actor || P4Kadan::GameActorOf(Actor) != Actor)
                        continue;
                    TArray<UStaticMeshComponent*> Components;
                    Actor->GetComponents(Components);
                    for (UStaticMeshComponent* Component : Components)
                        if (Component && Component->GetStaticMesh() &&
                            Component->GetStaticMesh()->GetOutermost()->GetName().StartsWith(TEXT("/Game/Carrot4/")))
                        {
                            MeshActor = Actor;
                            MeshComponent = Component;
                            break;
                        }
                    if (MeshComponent)
                        break;
                }
                if (MeshComponent)
                {
                    FixtureWorld.Reset(SourceWorld);
                    Note(TEXT("Asset-reference game actor source ") + MeshActor->GetPathName() + TEXT(" component ") +
                        MeshComponent->GetPathName());
                    break;
                }
            }
            UStaticMesh* Other = nullptr;
            for (const auto& Pair : Assets)
                if (UStaticMesh* One = Cast<UStaticMesh>(Pair.Value.Get()))
                    if (!MeshComponent || One != MeshComponent->GetStaticMesh())
                    {
                        Other = One;
                        break;
                    }
            if (TestTrue(TEXT("An imported game scenery mesh component and another mesh"),
                    MeshActor && MeshComponent && Other))
            {
                FProperty* MeshProperty = FindFProperty<FProperty>(
                    UStaticMeshComponent::StaticClass(), TEXT("StaticMesh"));
                PropertyAccessUtil::SetPropertyValue_Object(MeshProperty, MeshComponent, MeshProperty, &Other,
                    INDEX_NONE, PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
                Allowed(TEXT("asset reference"));
                TestTrue(TEXT("The other mesh stays"), MeshComponent->GetStaticMesh() == Other);
            }
            // A list on a game actor is refused and put back.
            AActor* Tagged = GameActors[2];
            const TArray<FName> TagsWere = Tagged->Tags;
            TArray<FName> TagsNow = TagsWere;
            TagsNow.Add(TEXT("KadanTest"));
            FProperty* TagsProperty = FindFProperty<FProperty>(AActor::StaticClass(), TEXT("Tags"));
            PropertyAccessUtil::SetPropertyValue_Object(TagsProperty, Tagged, TagsProperty, &TagsNow, INDEX_NONE,
                PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
            Refused(TEXT("list"), Tagged, TagsProperty->GetDisplayNameText().ToString());
            TestTrue(TEXT("The list is put back"), Tagged->Tags == TagsWere);
            // A component added to a game actor is refused and taken off.
            AActor* Grown = GameActors[3];
            Grown->Modify();
            USceneComponent* Part = NewObject<USceneComponent>(Grown, TEXT("KadanTestPart"), RF_Transactional);
            Part->CreationMethod = EComponentCreationMethod::Instance;
            Part->SetupAttachment(Grown->GetRootComponent());
            Grown->AddInstanceComponent(Part);
            Part->RegisterComponent();
            Refused(TEXT("added component"), Grown, TEXT(""));
            TArray<USceneComponent*> Left;
            Grown->GetComponents(Left);
            TestFalse(TEXT("The added component is taken off"), Left.Contains(Part));
            // An object inside a game actor is refused.
            UBodySetup* Inside = NewObject<UBodySetup>(Grown, TEXT("KadanTestInside"), RF_Transactional);
            FProperty* Trace = FindFProperty<FProperty>(UBodySetup::StaticClass(), TEXT("CollisionTraceFlag"));
            if (TestNotNull(TEXT("CollisionTraceFlag"), Trace))
            {
                const uint8 WasTrace = uint8(Inside->CollisionTraceFlag);
                FEditPropertyChain Chain;
                Chain.AddHead(Trace);
                Chain.SetActivePropertyNode(Trace);
                Chain.SetActiveMemberPropertyNode(Trace);
                Inside->PreEditChange(Chain);
                FPropertyChangedEvent Unchanged(Trace, EPropertyChangeType::ValueSet);
                Inside->PostEditChangeProperty(Unchanged); // BodySetup's native override does not call Super.
                Allowed(TEXT("unchanged inner property"));
                uint8 Complex = uint8(CTF_UseComplexAsSimple);
                PropertyAccessUtil::SetPropertyValue_Object(Trace, Inside, Trace, &Complex, INDEX_NONE,
                    PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
                Refused(TEXT("object inside the actor"), Grown, Trace->GetDisplayNameText().ToString());
                TestEqual(TEXT("The inner property is restored without a postchange broadcast"),
                    uint8(Inside->CollisionTraceFlag), WasTrace);
            }
            // A removal.
            World->EditorDestroyActor(GameActors[GameActors.Num() - 1], true);
            Allowed(TEXT("removal"));
            UPackage* LevelPackage = World->GetOutermost();
            const auto& CanSave = FCoreUObjectDelegates::IsPackageOKToSaveDelegate;
            TestTrue(TEXT("The level can be saved"), !CanSave.IsBound() || CanSave.Execute(LevelPackage, MapFile, GWarn));
            LevelPackage->SetDirtyFlag(false);
        }
        GEditor->NewMap();
    }

    return !HasAnyErrors();
}
#endif

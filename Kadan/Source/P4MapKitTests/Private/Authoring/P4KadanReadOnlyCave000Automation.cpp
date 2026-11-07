// Cave000 imported into a project without it, then every way of editing an imported game asset is tried:
// each locked kind reports locked, six kinds stay byte for byte as imported whatever is tried on them, and
// a game level and a game table take the edits Tsuchi can apply and refuse the others with their reason.
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

TArray<uint8> FileBytes(const FString& File)
{
    TArray<uint8> Bytes;
    FFileHelper::LoadFileToArray(Bytes, *File, FILEREAD_Silent);
    return Bytes;
}

// Authored saved properties are checked independently of byte-exact package files. Native
// editor resources are derived and are never loaded back into a live asset as an undo archive.
TArray<uint8> PackageBytes(UPackage* Package)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    Objects.RemoveAll([](UObject* Object)
        { return !Object || Object->IsPendingKill() || Object->IsA<UField>() || Object->HasAnyFlags(RF_Transient); });
    Objects.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
    FString All;
    for (UObject* Object : Objects)
    {
        All += Object->GetPathName() + TEXT("\n");
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
                continue;
            for (int32 Index = 0; Index < It->ArrayDim; ++Index)
            {
                FString Text;
                It->ExportText_InContainer(Index, Text, Object, Object, Object, PPF_None);
                All += It->GetName() + TEXT("=") + Text + TEXT("\n");
            }
        }
    }
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(*All), All.Len() * sizeof(TCHAR));
    return Bytes;
}

FString Shown(const TArray<FString>& Messages)
{
    return FString::Join(Messages, TEXT(" / "));
}

bool PlainText(const FString& Message)
{
    return !Message.Contains(TEXT(":")) && !Message.Contains(TEXT(";")) && !Message.Contains(TEXT("—"));
}

// A property of an asset's class a script could set: editable, a plain value, one of it.
FProperty* ScriptProperty(UObject* Asset)
{
    // The real Wwise event exposes an editable bank reference, while its scalar
    // duration/radius fields are VisibleAnywhere. Probe the actual supported edit route.
    if (Asset->GetClass()->GetPathName() == TEXT("/Script/AkAudio.AkAudioEvent"))
        if (FObjectPropertyBase* Bank = FindFProperty<FObjectPropertyBase>(Asset->GetClass(), TEXT("RequiredBank")))
            if (Bank->HasAnyPropertyFlags(CPF_Edit) && !Bank->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient))
                return Bank;
    for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
    {
        FProperty* P = *It;
        if (!P->HasAnyPropertyFlags(CPF_Edit) ||
            P->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_DisableEditOnTemplate) || P->ArrayDim != 1)
            continue;
        if (CastField<FBoolProperty>(P) || CastField<FIntProperty>(P) || CastField<FFloatProperty>(P))
            return P;
    }
    for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
    {
        FProperty* P = *It;
        if (!P->HasAnyPropertyFlags(CPF_Edit) ||
            P->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_DisableEditOnTemplate) || P->ArrayDim != 1)
            continue;
        if (CastField<FStrProperty>(P) || CastField<FNameProperty>(P) || CastField<FNumericProperty>(P) ||
            CastField<FBoolProperty>(P))
            return P;
    }
    return nullptr;
}

// Sets a property the way a script or an editor utility does (Python's set_editor_property).
void ScriptSet(UObject* Object, FProperty* Property)
{
    void* Value = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Value);
    Property->CopyCompleteValue(Value, Property->ContainerPtrToValuePtr<void>(Object));
    if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        Bool->SetPropertyValue(Value, !Bool->GetPropertyValue(Value));
    else if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
    {
        if (Number->IsFloatingPoint())
            Number->SetFloatingPointPropertyValue(Value, Number->GetFloatingPointPropertyValue(Value) + 1.5);
        else
            Number->SetIntPropertyValue(Value, Number->GetSignedIntPropertyValue(Value) + 1);
    }
    else if (FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Property))
        Reference->SetObjectPropertyValue(
            Value, Reference->GetObjectPropertyValue(Value) ? nullptr : Reference->PropertyClass->GetDefaultObject());
    else if (FStrProperty* Str = CastField<FStrProperty>(Property))
        Str->SetPropertyValue(Value, Str->GetPropertyValue(Value) + TEXT(" changed"));
    else if (FNameProperty* Name = CastField<FNameProperty>(Property))
        Name->SetPropertyValue(Value, FName(TEXT("Changed")));
    PropertyAccessUtil::SetPropertyValue_Object(Property, Object, Property, Value, INDEX_NONE,
        PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
    Property->DestroyValue(Value);
    FMemory::Free(Value);
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
        AddError(TEXT("The read-only test needs the Slate editor."));
        return false;
    }
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddError(TEXT("The read-only test needs P4_NATIVE_ROOT."));
        return false;
    }
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Root));
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    const FString Tables = Repo / TEXT("private/modding-research/tables/dts/Carrot4/Content/Carrot4");
    const FString SoundFile = Root /
        TEXT(
            "table-recovery/asset-references/Carrot4/Content/Carrot4/Audio/Event/Ambience/Area/AkE_Play_Amb_BaseNoiseDay.uasset");
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    for (const FString& Needed : {Game})
        if (!TestTrue(TEXT("The test's input is there: ") + Needed, IFileManager::Get().DirectoryExists(*Needed)))
            return false;
    for (const FString& Needed : {Tables / TEXT("Core/GActor/DT_OtakaraParameter.uasset"),
             Tables / TEXT("Core/PresetPikmin/DT_PresetPikmin.uasset"), SoundFile})
        if (!TestTrue(TEXT("The test's input is there: ") + Needed, IFileManager::Get().FileExists(*Needed)))
            return false;
    P4RegisterKadanEditor();
    P4Kadan::InitializePropertyGuards();
    auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    auto& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
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

    // The import: the map with everything it names, two game tables (one makes a kind of treasure) and a
    // Wwise sound event.
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
    TMap<FString, FString> Imported = Result.Imported;
    TestFalse(TEXT("The import was not cancelled"), Result.Cancelled);
    Note(FString::Printf(TEXT("Map import complete %d. Refused packages %d. Imported level objects %d."), Result.Ok,
        Result.NotImported.Num(), Result.LevelObjects));
    for (const FString& Problem : Result.Problems)
        Note(TEXT("Map import problem ") + Problem);
    {
        P4ImportPublication::FImportScope Importing;
        for (const TCHAR* Table : {TEXT("Core/GActor/DT_OtakaraParameter"), TEXT("Core/PresetPikmin/DT_PresetPikmin")})
        {
            const FP4OriginalAssetImportResult One = P4ImportOriginalAssetNative(
                Tables / Table + TEXT(".uasset"), FString(), true);
            if (TestTrue(FString(TEXT("Table imported ")) + Table + TEXT(" ") + One.ErrorDetail, One.IsValid()))
                Imported.Add(One.Package, One.Class);
        }
        FP4OriginalAssetImportResult Sound = P4ImportOriginalAssetNative(SoundFile, FString(), true);
        if (!Sound.IsValid())
            Sound = P4ImportOriginalAssetNative(SoundFile, FString(), true, true);
        if (!Sound.IsValid())
            Sound = P4KeepUnsupportedSoundReference(Sound, true);
        if (TestTrue(TEXT("Sound event or explicitly unsupported identity reference imported ") + Sound.ErrorDetail,
                Sound.IsValid()))
        {
            Imported.Add(Sound.Package, Sound.Class);
            if (Sound.ReferenceOnly)
            {
                const FP4OriginalStatus Status = P4QueryOriginalAsset(Sound.Asset);
                TestTrue(TEXT("Unsupported Wwise event retains protected game identity"),
                    Status.IsOriginal() && Status.Record.ReferenceOnly && Status.Record.ReadOnly);
                TestFalse(TEXT("Identity-only sound never claims recovered content comparison"),
                    Sound.ComparedKnown || Sound.DifferencesKnown);
                const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(Sound.Asset);
                TestTrue(TEXT("Identity-only sound is not equal to game audio"),
                    Diagnostic.Comparison != EP4OriginalComparison::Equal);
                Note(TEXT("Sound is a protected Wwise identity reference. Its event payload was not recovered."));
            }
        }
    }
    P4Kadan::TakeReadOnlyMessages();
    Flush();
    TestEqual(TEXT("The import is not taken for an edit"), Shown(P4Kadan::TakeReadOnlyMessages()), FString());

    // (a) Every imported game asset of a locked kind reports locked, and levels and tables do not.
    TMap<FString, TWeakObjectPtr<UObject>> Assets;
    TArray<TStrongObjectPtr<UObject>> HeldAssets;
    TSet<FString> MapOwnedPackages;
    TArray<FString> NotLocked, NotEditable;
    TMap<FString, int32> Kinds;
    for (const auto& Pair : Imported)
    {
        TArray<FAssetData> Found;
        Registry.GetAssetsByPackageName(FName(*Pair.Key), Found);
        UObject* Asset = Found.Num() ? Found[0].GetAsset() : nullptr;
        if (!Asset)
        {
            NotLocked.Add(Pair.Key + TEXT(" (not loaded)"));
            continue;
        }
        Assets.Add(Pair.Key, Asset);
        UPackage* OwningPackage = Asset->GetOutermost();
        if (Asset->IsA<UWorld>() || OwningPackage->HasAnyPackageFlags(PKG_ContainsMap) ||
            UWorld::FindWorldInPackage(OwningPackage))
            MapOwnedPackages.Add(Pair.Key);
        else
            HeldAssets.Emplace(Asset); // Map-owned objects must be released when the editor closes their map.
        ++Kinds.FindOrAdd(Asset->GetClass()->GetName());
        const P4Kadan::FAssetPolicy Policy = P4Kadan::GetAssetPolicy(Asset);
        FString File;
        FPackageName::DoesPackageExist(Pair.Key, nullptr, &File);
        const bool ReadOnlyFile = !File.IsEmpty() && Disk.IsReadOnly(*FPaths::ConvertRelativePathToFull(File));
        const bool Writable = Tools.AllPassWritableFolderFilter({Pair.Key});
        const TSharedPtr<IAssetTypeActions> Actions = Tools.GetAssetTypeActionsForClass(Asset->GetClass()).Pin();
        const bool Mark = P4Kadan::LockMark(FAssetData(Asset)) != SNullWidget::NullWidget;
        if (Asset->IsA<UWorld>() || Asset->IsA<UDataTable>())
        {
            if (!Policy.AllowEdit || P4Kadan::IsLockedPackage(Asset->GetOutermost()) || ReadOnlyFile || !Writable ||
                Mark)
                NotEditable.Add(Pair.Key + TEXT(" (") + Asset->GetClass()->GetName() + TEXT(")"));
        }
        else if (Policy.AllowEdit || !P4Kadan::IsLockedPackage(Asset->GetOutermost()) || !ReadOnlyFile || Writable ||
            !Mark || Policy.Reason.IsEmpty())
            NotLocked.Add(FString::Printf(TEXT("%s (%s%s%s%s%s)"), *Pair.Key, *Asset->GetClass()->GetName(),
                Policy.AllowEdit ? TEXT(", editable") : TEXT(""), ReadOnlyFile ? TEXT("") : TEXT(", writable file"),
                Writable ? TEXT(", browser can edit") : TEXT(""), Mark ? TEXT("") : TEXT(", no lock mark")));
    }
    TArray<FString> KindCounts;
    for (const auto& Pair : Kinds)
        KindCounts.Add(FString::Printf(TEXT("%s %d"), *Pair.Key, Pair.Value));
    KindCounts.Sort();
    Note(FString::Printf(TEXT("Imported %d game assets: %s."), Assets.Num(), *FString::Join(KindCounts, TEXT(", "))));
    for (const FString& One : NotLocked)
        AddError(TEXT("Not locked: ") + One);
    for (const FString& One : NotEditable)
        AddError(TEXT("Level or table not editable: ") + One);
    TestEqual(TEXT("Imported game assets of a locked kind that do not report locked"), NotLocked.Num(), 0);

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
                P4KadanReadOnlyCave000::ScriptSet(GameActors[1], Hidden);
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
            for (const auto& Pair : Imported)
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
            TestFalse(TEXT("The level is not locked"), P4Kadan::IsLockedPackage(LevelPackage));
            TestTrue(TEXT("The level can be saved"),
                FCoreUObjectDelegates::IsPackageOKToSaveDelegate.Execute(LevelPackage, MapFile, GWarn));
            LevelPackage->SetDirtyFlag(false);
        }
        GEditor->NewMap();
    }

    // (c) A game table takes field edits and new rows, and refuses deleting, renaming and moving its game rows
    // and new rows in a table of a kind.
    {
        UDataTable* Preset = LoadObject<UDataTable>(
            nullptr, TEXT("/Game/Carrot4/Core/PresetPikmin/DT_PresetPikmin.DT_PresetPikmin"));
        UDataTable* Treasures = LoadObject<UDataTable>(
            nullptr, TEXT("/Game/Carrot4/Core/GActor/DT_OtakaraParameter.DT_OtakaraParameter"));
        if (TestTrue(TEXT("Both tables are there"), Preset && Treasures))
        {
            using FUtils = FDataTableEditorUtils;
            const TArray<FName> GameRows = P4Kadan::GameRowsOf(Preset);
            TestTrue(TEXT("The table knows its game rows"), GameRows.Num() >= 2 && GameRows == Preset->GetRowNames());
            TestTrue(TEXT("A table is editable"),
                P4Kadan::GetAssetPolicy(Preset).AllowEdit && !P4Kadan::IsLockedPackage(Preset->GetOutermost()));
            TestFalse(TEXT("The preset table is not a table of a kind"), P4Kadan::IsKindTable(Preset));
            TestTrue(TEXT("The treasure table is a table of a kind"), P4Kadan::IsKindTable(Treasures));
            P4Kadan::TakeReadOnlyMessages();
            const auto Told = [&](const FString& What, bool Expected, const FString& Words)
            {
                Flush();
                const TArray<FString> Messages = P4Kadan::TakeReadOnlyMessages();
                if (Expected)
                {
                    TestTrue(TEXT("Table edit refused: ") + What + TEXT(" ") + Shown(Messages),
                        Messages.Num() >= 1 && Messages[0].Contains(Words));
                    for (const FString& Message : Messages)
                        TestTrue(TEXT("Plain sentence: ") + Message, PlainText(Message));
                    Note(TEXT("Refused. ") + Shown(Messages));
                }
                else
                    TestEqual(TEXT("Table edit allowed: ") + What + TEXT(" ") + Shown(Messages), Messages.Num(), 0);
                Note(TEXT("Table edit ") + What + TEXT(". ") +
                    (Messages.Num() ? Shown(Messages) : FString(TEXT("Nothing told."))));
            };
            // A field of a row, as the row editor changes it.
            const UScriptStruct* Type = Preset->GetRowStruct();
            FNumericProperty* Field = nullptr;
            FArrayProperty* List = nullptr;
            for (TFieldIterator<FProperty> It(Type); It; ++It)
            {
                if (!Field)
                    Field = CastField<FNumericProperty>(*It);
                if (!List)
                    List = CastField<FArrayProperty>(*It);
            }
            if (Field)
            {
                uint8* Row = Preset->FindRowUnchecked(GameRows[0]);
                FUtils::BroadcastPreChange(Preset, FUtils::EDataTableChangeInfo::RowData);
                Preset->Modify();
                void* Value = Field->ContainerPtrToValuePtr<void>(Row);
                if (Field->IsFloatingPoint())
                    Field->SetFloatingPointPropertyValue(Value, Field->GetFloatingPointPropertyValue(Value) + 2);
                else
                    Field->SetIntPropertyValue(Value, Field->GetSignedIntPropertyValue(Value) + 2);
                FUtils::BroadcastPostChange(Preset, FUtils::EDataTableChangeInfo::RowData);
                Told(TEXT("field"), false, FString());
            }
            if (List)
                TestTrue(TEXT("An array's length can be edited"), P4Kadan::CanEditTableProperty(*List));
            TestNotNull(TEXT("A new row"), FUtils::AddRow(Preset, TEXT("KadanTestRow")));
            Told(TEXT("new row"), false, FString());
            TestTrue(TEXT("The new row stays"), Preset->GetRowNames().Contains(FName(TEXT("KadanTestRow"))));
            FUtils::RenameRow(Preset, GameRows[0], TEXT("KadanRenamed"));
            Told(TEXT("rename a game row"), true, GameRows[0].ToString());
            TestTrue(TEXT("The game row is put back"),
                Preset->GetRowNames().Contains(GameRows[0]) &&
                    !Preset->GetRowNames().Contains(FName(TEXT("KadanRenamed"))));
            FUtils::RemoveRow(Preset, GameRows[1]);
            Told(TEXT("delete a game row"), true, GameRows[1].ToString());
            TestTrue(TEXT("The deleted game row is put back"), Preset->GetRowNames().Contains(GameRows[1]));
            FUtils::MoveRow(Preset, GameRows[1], FUtils::ERowMoveDirection::Up);
            Told(TEXT("move a game row"), true, TEXT("order"));
            TArray<FName> Order = Preset->GetRowNames();
            Order.Remove(FName(TEXT("KadanTestRow")));
            TestTrue(TEXT("The game rows keep the game's order"), Order == GameRows);
            FUtils::RenameRow(Preset, TEXT("KadanTestRow"), TEXT("KadanTestRow2"));
            Told(TEXT("rename a new row"), false, FString());
            FUtils::RemoveRow(Preset, TEXT("KadanTestRow2"));
            Told(TEXT("delete a new row"), false, FString());
            FUtils::AddRow(Treasures, TEXT("KadanTreasure"));
            Told(TEXT("new treasure row"), true,
                TEXT("New treasures, creatures and Pikmin types are made through content."));
            TestFalse(TEXT("The new treasure row is taken off"),
                Treasures->GetRowNames().Contains(FName(TEXT("KadanTreasure"))));
            Preset->GetOutermost()->SetDirtyFlag(false);
            Treasures->GetOutermost()->SetDirtyFlag(false);
        }
    }

    // (b) Every route, on six kinds: the package stays byte for byte as imported.
    UObject* Picks[6] = {};
    const TCHAR* Names[6] = {TEXT("Texture"), TEXT("Static mesh"), TEXT("Blueprint"), TEXT("Data asset"),
        TEXT("Material instance"), TEXT("Sound")};
    for (const auto& Pair : Assets)
    {
        UObject* Asset = Pair.Value.Get();
        // Loading and leaving the game map can explicitly destroy its objects. They were checked
        // above. Keep map-owned entries weak so NewMap can complete its real leak check.
        if (!Asset)
        {
            if (!MapOwnedPackages.Contains(Pair.Key))
                AddError(TEXT("Imported route asset was released: ") + Pair.Key);
            continue;
        }
        if (!Picks[0] && Asset->IsA<UTexture>())
            Picks[0] = Asset;
        if (!Picks[1] && Asset->IsA<UStaticMesh>())
            Picks[1] = Asset;
        if (!Picks[2] && Asset->GetClass() == UBlueprint::StaticClass())
            Picks[2] = Asset;
        if (!Picks[3] && Asset->IsA<UDataAsset>())
            Picks[3] = Asset;
        if (!Picks[4] && Asset->IsA<UMaterialInstanceConstant>())
            Picks[4] = Asset;
        if (!Picks[5] && Asset->GetClass()->GetOutermost()->GetName() == TEXT("/Script/AkAudio"))
            Picks[5] = Asset;
    }
    TStrongObjectPtr<UContentBrowserAssetDataSource> Browser(NewObject<UContentBrowserAssetDataSource>());
    Browser->Initialize(TEXT("/"), false);
    TArray<FString> Routes;
    TMap<FString, TArray<FString>> Table;
    const bool Python = GEngine->Exec(nullptr, TEXT("py print('Kadan read-only test')"));
    Note(Python ? TEXT("Python is there, and the script routes run in it too.")
                : TEXT("Python is not there. The script routes run as editor utilities do."));
    for (int32 Kind = 0; Kind < 6; ++Kind)
    {
        UObject* Asset = Picks[Kind];
        if (!TestNotNull(FString(TEXT("An imported ")) + Names[Kind], Asset))
            continue;
        UPackage* Package = Asset->GetOutermost();
        const FString Path = Asset->GetPathName(), PackageName = Package->GetName(),
                      Leaf = FPackageName::GetLongPackageAssetName(PackageName);
        FString File;
        FPackageName::DoesPackageExist(PackageName, nullptr, &File);
        File = FPaths::ConvertRelativePathToFull(File);
        Note(FString::Printf(TEXT("%s is %s (%s)."), Names[Kind], *PackageName, *Asset->GetClass()->GetName()));
        const TArray<uint8> DiskBefore = FileBytes(File), MemoryBefore = PackageBytes(Package);
        const bool CookedBefore = Package->bIsCookedForEditor;
        TestTrue(FString(Names[Kind]) + TEXT(" serializes the same twice"), PackageBytes(Package) == MemoryBefore);
        TArray<UPackage*> Referencers;
        {
            TArray<FName> Names2;
            Registry.GetReferencers(FName(*PackageName), Names2);
            for (const FName& Name : Names2)
                if (UPackage* One = FindPackage(nullptr, *Name.ToString()))
                    if (P4Kadan::IsLockedPackage(One))
                        Referencers.Add(One);
        }
        TArray<TArray<uint8>> ReferencersBefore;
        for (UPackage* One : Referencers)
            ReferencersBefore.Add(PackageBytes(One));
        P4Kadan::TakeReadOnlyMessages();
        const auto Check = [&](const FString& Route, bool RouteOk, bool ExpectMessage)
        {
            Flush();
            const TArray<FString> Told = P4Kadan::TakeReadOnlyMessages();
            const bool Same = FileBytes(File) == DiskBefore && PackageBytes(Package) == MemoryBefore &&
                IsValid(Asset) && !Package->IsDirty() && Package->bIsCookedForEditor == CookedBefore;
            bool Others = true;
            for (int32 I = 0; I < Referencers.Num(); ++I)
                Others &= PackageBytes(Referencers[I]) == ReferencersBefore[I];
            const bool Message = !ExpectMessage || (Told.Num() > 0 && Told[0].Contains(Leaf));
            bool Plain = true;
            for (const FString& One : Told)
                Plain &= PlainText(One);
            const bool Ok = RouteOk && Same && Others && Message && Plain;
            TestTrue(FString::Printf(TEXT("%s, %s: locked and unchanged %s"), Names[Kind], *Route, *Shown(Told)), Ok);
            Note(FString::Printf(TEXT("%s, %s: %s"), Names[Kind], *Route, Ok ? TEXT("locked") : TEXT("FAILED")));
            if (!Ok)
                Note(FString::Printf(TEXT("%s, %s: route %d, same %d, game assets that use it %d, told %d, plain %d"),
                    Names[Kind], *Route, RouteOk, Same, Others, Message, Plain));
            if (!Same || !Others)
            {
                const auto Difference = [&](UPackage* Changed, const TArray<uint8>& Before)
                {
                    const TArray<uint8> After = PackageBytes(Changed);
                    if (Before == After)
                        return;
                    TArray<FString> OldLines, NewLines;
                    FString(Before.Num() / sizeof(TCHAR), reinterpret_cast<const TCHAR*>(Before.GetData()))
                        .ParseIntoArrayLines(OldLines);
                    FString(After.Num() / sizeof(TCHAR), reinterpret_cast<const TCHAR*>(After.GetData()))
                        .ParseIntoArrayLines(NewLines);
                    int32 Index = 0;
                    while (Index < OldLines.Num() && Index < NewLines.Num() && OldLines[Index] == NewLines[Index])
                        ++Index;
                    Note(TEXT("Changed property in ") + Changed->GetName() + TEXT(". Before ") +
                        (OldLines.IsValidIndex(Index) ? OldLines[Index].Left(250) : TEXT("end")) + TEXT(". After ") +
                        (NewLines.IsValidIndex(Index) ? NewLines[Index].Left(250) : TEXT("end")));
                };
                Difference(Package, MemoryBefore);
                for (int32 Index = 0; Index < Referencers.Num(); ++Index)
                    Difference(Referencers[Index], ReferencersBefore[Index]);
                Note(FString::Printf(
                    TEXT("File unchanged %d. Package dirty %d."), FileBytes(File) == DiskBefore, Package->IsDirty()));
            }
            Routes.AddUnique(Route);
            Table.FindOrAdd(Route).SetNum(6);
            Table[Route][Kind] = Ok ? TEXT("locked") : TEXT("FAILED");
            Package->SetDirtyFlag(false);
        };
        const TSharedPtr<IAssetTypeActions> Actions = Tools.GetAssetTypeActionsForClass(Asset->GetClass()).Pin();

        // Asset editors and type actions.
        {
            bool Ok = !Editors->OpenEditorForAsset(Asset, EToolkitMode::Standalone, nullptr, false);
            Ok &= Actions.IsValid();
            if (Actions)
            {
                // The type's own editor, asked for directly: the reference opens (another module's editor is
                // closed for it on the next tick).
                Actions->OpenAssetEditor({Asset});
                Flush();
                IAssetEditorInstance* Open = Editors->FindEditorForAsset(Asset, false);
                Ok &= Open && Open->GetEditorName() == FName(TEXT("KadanReference"));
                Editors->CloseAllEditorsForAsset(Asset);
                // A double click: the reference opens, or no editor at all.
                if (!Actions->AssetsActivatedOverride({Asset}, EAssetTypeActivationMethod::DoubleClicked))
                    Ok &= !Editors->OpenEditorForAsset(Asset, EToolkitMode::Standalone, nullptr, false);
                Flush();
                Open = Editors->FindEditorForAsset(Asset, false);
                Ok &= !Open || Open->GetEditorName() == FName(TEXT("KadanReference"));
                Editors->CloseAllEditorsForAsset(Asset);
                Ok &= Actions->GetValidAssetsForPreviewOrEdit({FAssetData(Asset)}, false).Num() == 0 ||
                    !Tools.AllPassWritableFolderFilter({PackageName});
            }
            // An editor made directly, past the asset actions, is closed and the reference opens instead.
            FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, nullptr, Asset);
            Flush();
            IAssetEditorInstance* Open = Editors->FindEditorForAsset(Asset, false);
            Ok &= Open && Open->GetEditorName() == FName(TEXT("KadanReference"));
            Editors->CloseAllEditorsForAsset(Asset);
            Check(TEXT("Asset editors and type actions"), Ok, false);
        }
        // Bulk edit via property matrix.
        {
            bool Ok = !Tools.AllPassWritableFolderFilter({PackageName});
            auto Matrix = Properties.CreatePropertyEditorToolkit(EToolkitMode::Standalone, nullptr, Asset);
            Flush();
            const auto Manager = Matrix->GetAssociatedTabManager();
            const auto Grid = Manager
                ? Manager->FindExistingLiveTab(FTabId(TEXT("PropertyEditorToolkit_PropertyTable")))
                : nullptr;
            Ok &= Grid.IsValid() && !Grid->GetContent()->IsEnabled();
            Matrix->CloseWindow();
            Check(TEXT("Property matrix"), Ok, false);
        }
        // A details panel on the asset.
        {
            FDetailsViewArgs Args;
            Args.bUpdatesFromSelection = false;
            Args.bLockable = false;
            Args.bAllowSearch = false;
            const TSharedRef<IDetailsView> View = Properties.CreateDetailView(Args);
            View->SetObject(Asset);
            const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(400, 300))[View];
            FSlateApplication::Get().AddWindow(Window, false);
            Flush();
            const bool Ok = !View->IsPropertyEditingEnabled();
            Window->RequestDestroyWindow();
            Check(TEXT("Details panel"), Ok, false);
        }
        // An asset dropped onto one of its properties: the property is set through its handle.
        {
            FProperty* Target = nullptr;
            for (TFieldIterator<FObjectPropertyBase> It(Asset->GetClass()); It && !Target; ++It)
                if (It->HasAnyPropertyFlags(CPF_Edit) &&
                    !It->HasAnyPropertyFlags(CPF_EditConst | CPF_InstancedReference | CPF_Transient) &&
                    It->ArrayDim == 1)
                    Target = *It;
            if (!Target)
                Target = ScriptProperty(Asset);
            bool Ok = Target != nullptr;
            if (Target)
            {
                const TSharedPtr<ISinglePropertyView> Single = Properties.CreateSingleProperty(
                    Asset, Target->GetFName(), FSinglePropertyParams());
                const TSharedPtr<IPropertyHandle> Handle = Single ? Single->GetPropertyHandle() : nullptr;
                if (Handle)
                {
                    if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Target))
                    {
                        UObject* Dropped = nullptr;
                        for (const auto& Pair : Assets)
                            if (UObject* Candidate = Pair.Value.Get())
                                if (Candidate != Asset && Candidate->IsA(Object->PropertyClass))
                                {
                                    Dropped = Candidate;
                                    break;
                                }
                        if (!Dropped && Asset->GetClass()->GetPathName() == TEXT("/Script/AkAudio.AkAudioEvent"))
                            Dropped = Object->PropertyClass->GetDefaultObject();
                        if (Dropped)
                            Handle->SetValue(Dropped);
                        else
                            Handle->SetValue(static_cast<UObject*>(nullptr));
                    }
                    else
                        Handle->SetValueFromFormattedString(TEXT("1"));
                }
            }
            Note(
                FString::Printf(TEXT("%s, drop: onto %s"), Names[Kind], Target ? *Target->GetName() : TEXT("nothing")));
            Check(TEXT("Drop onto a property"), Ok, false);
        }
        // Content Browser: rename, move, delete, edit and save.
        {
            const FAssetData Data(Asset);
            const FContentBrowserItemData Item(Browser.Get(),
                EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset, Data.ObjectPath,
                Data.AssetName, FText(), MakeShared<FContentBrowserAssetFileItemDataPayload>(Data));
            const FString NewName = TEXT("Changed");
            FText Why;
            bool Ok = !Browser->CanEditItem(Item, &Why) && !Browser->CanRenameItem(Item, &NewName, &Why) &&
                !Browser->CanDeleteItem(Item, &Why) && !Browser->CanMoveItem(Item, TEXT("/Game/KadanMoved"), &Why) &&
                !Browser->CanSaveItem(Item, EContentBrowserItemSaveFlags::None, &Why) && !Browser->EditItem(Item);
            Check(TEXT("Content Browser"), Ok, false);
        }
        // Rename and move, as scripts and the editor's asset tools do them. These exact
        // engine checkout errors are the expected refusals: one rename, one move and,
        // when Python is available, its one rename of this same locked package.
        AddExpectedError(PackageName + TEXT(" - Not checked-out or writable."), EAutomationExpectedErrorFlags::Contains,
            Python ? 3 : 2);
        {
            Tools.RenameAssets(
                {FAssetRenameData(Asset, FPackageName::GetLongPackagePath(PackageName), Leaf + TEXT("_Renamed"))});
            bool Ok = Asset->GetOutermost() == Package &&
                FindPackage(nullptr, *(PackageName + TEXT("_Renamed"))) == nullptr;
            Check(TEXT("Rename"), Ok, false);
            Tools.RenameAssets({FAssetRenameData(Asset, TEXT("/Game/KadanMoved"), Leaf)});
            Ok = Asset->GetOutermost() == Package;
            Check(TEXT("Move"), Ok, false);
        }
        // Delete.
        {
            bool Ok = ObjectTools::DeleteObjects({Asset}, false) == 0;
            Ok &= ObjectTools::ForceDeleteObjects({Asset}, false) == 0;
            Check(TEXT("Delete"), Ok && IsValid(Asset), true);
        }
        // Reimport.
        {
            FReimportManager::Instance()->Reimport(Asset, false, false, TEXT(""), nullptr, INDEX_NONE, false, true);
            Check(TEXT("Reimport"), true, false);
        }
        // Duplicate must retain game source evidence; editing permission does not prove creator source.
        UObject* Copy = Tools.DuplicateAsset(Leaf + TEXT("_Mine"), TEXT("/Game/KadanMine"), Asset);
        Made.TrackCopy(Copy);
        {
            const bool Ok = Copy && !P4HasCreatorSource(Copy);
            Check(TEXT("Duplicate does not qualify game bytes as creator source"), Ok, false);
        }
        // Consolidate and replace references: what used the game asset may point at the copy, the game asset
        // stays, and game assets that used it are put back.
        if (Copy)
        {
            TArray<UObject*> Replaced = {Asset};
            ObjectTools::ConsolidateObjects(Copy, Replaced, false);
            Check(TEXT("Consolidate"), IsValid(Asset), false);
            TArray<UObject*> Again = {Asset};
            ObjectTools::ForceReplaceReferences(Copy, Again);
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            Check(TEXT("Replace references"), IsValid(Asset), false);
        }
        // Scripts and editor utilities.
        if (FProperty* Value = ScriptProperty(Asset))
        {
            ScriptSet(Asset, Value);
            Check(TEXT("Script sets a property"), true, true);
            Asset->Modify();
            if (FBoolProperty* Bool = CastField<FBoolProperty>(Value))
                Bool->SetPropertyValue_InContainer(Asset, !Bool->GetPropertyValue_InContainer(Asset));
            else if (FNumericProperty* Number = CastField<FNumericProperty>(Value))
            {
                void* At = Number->ContainerPtrToValuePtr<void>(Asset);
                if (Number->IsFloatingPoint())
                    Number->SetFloatingPointPropertyValue(At, Number->GetFloatingPointPropertyValue(At) + 3);
                else
                    Number->SetIntPropertyValue(At, Number->GetSignedIntPropertyValue(At) + 3);
            }
            else if (FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Value))
            {
                void* At = Reference->ContainerPtrToValuePtr<void>(Asset);
                Reference->SetObjectPropertyValue(
                    At, Reference->GetObjectPropertyValue(At) ? nullptr : Reference->PropertyClass->GetDefaultObject());
            }
            Asset->PostEditChange();
            Check(TEXT("Utility changes the object"), true, true);
        }
        else
            Check(TEXT("Script sets a property"), false, false);
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            if (Blueprint->GeneratedClass)
                if (FProperty* Default = ScriptProperty(Blueprint->GeneratedClass->GetDefaultObject()))
                {
                    ScriptSet(Blueprint->GeneratedClass->GetDefaultObject(), Default);
                    Check(TEXT("Script sets a class default"), true, true);
                }
        if (Python)
        {
            if (Asset->IsA<UTexture>())
            {
                GEngine->Exec(nullptr,
                    *FString::Printf(TEXT("py unreal.load_asset('%s').set_editor_property('lod_bias', 3)"), *Path));
                Check(TEXT("Python sets a property"), true, true);
            }
            GEngine->Exec(nullptr,
                *FString::Printf(
                    TEXT("py unreal.EditorAssetLibrary.rename_asset('%s', '%s_Py')"), *PackageName, *PackageName));
            Check(TEXT("Python renames"), FindPackage(nullptr, *(PackageName + TEXT("_Py"))) == nullptr, false);
            GEngine->Exec(
                nullptr, *FString::Printf(TEXT("py unreal.EditorAssetLibrary.delete_asset('%s')"), *PackageName));
            Check(TEXT("Python deletes"), IsValid(Asset), true);
            Package->SetDirtyFlag(true);
            GEngine->Exec(
                nullptr, *FString::Printf(TEXT("py unreal.EditorAssetLibrary.save_asset('%s', False)"), *PackageName));
            Check(TEXT("Python saves"), true, false);
        }
        // Save, by the editor's own save and by Kadan's.
        {
            bool Ok = Disk.IsReadOnly(*File);
            Package->SetDirtyFlag(true);
            Ok &= !FCoreUObjectDelegates::IsPackageOKToSaveDelegate.Execute(Package, File, GWarn);
            Ok &= !GEditor->SavePackage(
                Package, Asset, RF_Public | RF_Standalone, *File, GWarn, nullptr, false, false, SAVE_NoError);
            Ok &= !UEditorLoadingAndSavingUtils::SavePackages({Package}, false);
            TArray<UPackage*> Dirty;
            FEditorFileUtils::GetDirtyContentPackages(Dirty);
            Ok &= !Dirty.Contains(Package);
            Check(TEXT("Save"), Ok, false);
        }
        Editors->CloseAllEditorsForAsset(Asset);
        if (Copy)
            Editors->CloseAllEditorsForAsset(Copy);
    }
    // The table of routes by kind.
    FString Header = TEXT("Route");
    for (const TCHAR* Name : Names)
        Header += FString(TEXT(" | ")) + Name;
    Note(Header);
    for (const FString& Route : Routes)
    {
        FString Line = Route;
        for (int32 Kind = 0; Kind < 6; ++Kind)
        {
            const FString& Cell = Table[Route][Kind];
            Line += TEXT(" | ") + (Cell.IsEmpty() ? FString(TEXT("n/a")) : Cell);
        }
        Note(Line);
    }
    return !HasAnyErrors();
}
#endif

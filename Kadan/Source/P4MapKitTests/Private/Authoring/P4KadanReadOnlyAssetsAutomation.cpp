#include "P4KadanAssetPolicy.h"
#include "P4KadanEditor.h"
#include "P4KadanReadOnly.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "Editor.h"
#include "Containers/Ticker.h"
#include "ContentBrowserAssetDataSource.h"
#include "ContentBrowserAssetDataPayload.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Animation/AnimSequence.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Sound/SoundWave.h"
#include "Curves/CurveFloat.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/BlacklistNames.h"
#include "PropertyEditorModule.h"
#include "Serialization/ObjectWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SNullWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReopenWorldActionsTest, "P4MapKit.Kadan.ReopenWorldAssetActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReopenWorldActionsTest::RunTest(const FString&)
{
    if (!GEditor || IsRunningCommandlet() || !FApp::CanEverRender())
    {
        AddError(TEXT("World action routing requires an editor with the normal asset action route initialized."));
        return false;
    }
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_WARM_REOPEN_MAP"));
    FString File;
    if (!TestTrue(TEXT("An explicit retained map exists for the action regression"),
            !Package.IsEmpty() && FPackageName::IsValidLongPackageName(Package) &&
                FPackageName::DoesPackageExist(Package, nullptr, &File)))
        return false;
    TArray<uint8> Before;
    if (!TestTrue(TEXT("The saved map is readable before opening"), FFileHelper::LoadFileToArray(Before, *File)))
        return false;
    P4RegisterKadanEditor();
    const auto Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
                             .Get()
                             .GetAssetTypeActionsForClass(UWorld::StaticClass())
                             .Pin();
    if (!TestTrue(TEXT("The registered World asset actions are available"), Actions.IsValid()))
        return false;
    int32 Opened = 0, Retired = 0;
    const FDelegateHandle Handle = FEditorDelegates::OnMapOpened.AddLambda(
        [&](const FString&, bool)
        {
            UWorld* Current = GEditor->GetEditorWorldContext().World();
            if (Current && Current->GetOutermost()->GetName() == Package)
                ++Opened;
        });
    ON_SCOPE_EXIT
    {
        FEditorDelegates::OnMapOpened.Remove(Handle);
    };
    const FString Path = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
    for (int32 Pass = 0; Pass < 4; ++Pass)
    {
        UWorld* Selected = Pass == 0 ? LoadObject<UWorld>(nullptr, *Path) : GEditor->GetEditorWorldContext().World();
        if (!TestNotNull(TEXT("The current selected World exists"), Selected))
            return false;
        const TWeakObjectPtr<UWorld> BeforeOpen(Selected);
        // This is the Content Browser's actual registered asset-action dispatch, including the
        // Kadan wrapper and stock World OpenAssetEditor. Do not pin Selected across map teardown.
        Actions->OpenAssetEditor({Selected});
        UWorld* Current = GEditor->GetEditorWorldContext().World();
        if (!TestTrue(TEXT("Native asset dispatch returns the freshly opened requested map"),
                IsValid(Current) && Current->GetOutermost()->GetName() == Package))
            return false;
        if (!BeforeOpen.IsValid())
            ++Retired;
        TestEqual(TEXT("Each action dispatch reaches normal OnMapOpened"), Opened, Pass + 1);
    }
    TestTrue(TEXT("Warm reopening actually retired a selected World during stock dispatch"), Retired > 0);
    TArray<uint8> After;
    TestTrue(TEXT("Repeated opening changes no saved map bytes"),
        FFileHelper::LoadFileToArray(After, *File) && Before == After);
    return !HasAnyErrors();
}

namespace
{
TArray<uint8> ObjectBytes(UObject* Asset)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Asset, Bytes, false, false, false);
    if (const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
    {
        // UE4.26 UStaticMesh::Serialize writes the obsolete, uninitialised local
        // Deprecated_HighResSourceMeshCRC. It is not a member of the asset. For
        // this empty mesh fixture it is exactly 32 bytes before the archive end,
        // followed by LightingGuid and three zero counts/flags. Compare every
        // real mesh byte, including the GUID, and normalize only that dead word.
        if (Mesh->GetNumSourceModels() == 0 && Mesh->Sockets.Num() == 0 && Mesh->StaticMaterials.Num() == 0 &&
            Bytes.Num() >= 32)
        {
            bool EmptyTail = true;
            for (int32 I = Bytes.Num() - 12; I < Bytes.Num(); ++I)
                EmptyTail &= Bytes[I] == 0;
            if (EmptyTail)
                for (int32 I = Bytes.Num() - 32; I < Bytes.Num() - 28; ++I)
                    Bytes[I] = 0;
        }
    }
    return Bytes;
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyAssetsTest, "P4MapKit.Kadan.ReadOnlyAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReadOnlyAssetsTest::RunTest(const FString&)
{
    if (!GEditor || !FSlateApplication::IsInitialized())
    {
        AddError(TEXT("ReadOnlyAssets needs the Slate editor."));
        return false;
    }
    P4RegisterKadanEditor();
    auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    auto& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    TStrongObjectPtr<UContentBrowserAssetDataSource> Browser(NewObject<UContentBrowserAssetDataSource>());
    Browser->Initialize(TEXT("/"), false);
    const auto BrowserRoutes = [&](UObject* Asset, const FString& Prefix)
    {
        const FAssetData Data(Asset);
        const FContentBrowserItemData Item(Browser.Get(),
            EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset, Data.ObjectPath,
            Data.AssetName, FText(), MakeShared<FContentBrowserAssetFileItemDataPayload>(Data));
        const FString NewName = TEXT("Changed");
        FText Why;
        TestFalse(Prefix + TEXT("browser editor admission"), Browser->CanEditItem(Item, &Why));
        TestFalse(Prefix + TEXT("browser rename admission"), Browser->CanRenameItem(Item, &NewName, &Why));
        TestFalse(Prefix + TEXT("browser delete admission"), Browser->CanDeleteItem(Item, &Why));
        TestFalse(Prefix + TEXT("browser save admission"),
            Browser->CanSaveItem(Item, EContentBrowserItemSaveFlags::None, &Why));
        TestFalse(Prefix + TEXT("browser edit operation"), Browser->EditItem(Item));
        TestFalse(Prefix + TEXT("browser save operation"), Browser->SaveItem(Item, EContentBrowserItemSaveFlags::None));
    };
    const FString Root = TEXT("/Game/__KadanReadOnly_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const TArray<UClass*> Kinds = {UTexture2D::StaticClass(), UStaticMesh::StaticClass(), USkeletalMesh::StaticClass(),
        UMaterial::StaticClass(), UMaterialInstanceConstant::StaticClass(), UAnimSequence::StaticClass(),
        UPhysicsAsset::StaticClass(), USoundWave::StaticClass(), UCurveFloat::StaticClass()};
    for (UClass* Kind : Kinds)
    {
        const FString Name = Kind->GetName(), Prefix = Name + TEXT(": ");
        TStrongObjectPtr<UPackage> Package(CreatePackage(*(Root / Name)));
        TStrongObjectPtr<UObject> Asset(
            NewObject<UObject>(Package.Get(), Kind, FName(*Name), RF_Public | RF_Standalone));
        TestTrue(Prefix + TEXT("creator is editable"), P4Kadan::GetAssetPolicy(Asset.Get()).AllowEdit);
        FP4OriginalRecord Record;
        Record.Package = Package->GetName();
        Record.Class = Kind->GetPathName();
        Record.Kind = TEXT("test-game-asset");
        Record.SourceDigest = FString::ChrN(64, TEXT('a'));
        Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
        Record.SourceDigestKind = TEXT("fixture");
        Record.ComparisonSchema = TEXT("fixture");
        // Exercise ordinary imported records: neither old opt-in lock flag is set.
        FString Why;
        TestTrue(Prefix + TEXT("mark original"), P4MarkOriginalAsset(Asset.Get(), Record, Why));
        const auto Policy = P4Kadan::GetAssetPolicy(Asset.Get());
        TestTrue(Prefix + TEXT("source recognised"), Policy.Original);
        TestFalse(Prefix + TEXT("all imported game asset kinds are locked"), Policy.AllowEdit);
        BrowserRoutes(Asset.Get(), Prefix);
        FString Lock;
        TestTrue(Prefix + TEXT("lock survives in saved asset registry tags"),
            FAssetData(Asset.Get()).GetTagValue(TEXT("P4KadanReadOnly"), Lock) && Lock == TEXT("1"));
        TestFalse(
            Prefix + TEXT("browser write routes are locked"), Tools.AllPassWritableFolderFilter({Package->GetName()}));
        TestTrue(Prefix + TEXT("adjacent creator package remains writable"),
            Tools.AllPassWritableFolderFilter({Root / (Name + TEXT("_Creator"))}));
        FCanDeleteAssetResult CanDelete;
        FEditorDelegates::OnAssetsCanDelete.Broadcast({Asset.Get()}, CanDelete);
        TestFalse(Prefix + TEXT("delete veto"), CanDelete.Get());
        const FString File = FPackageName::LongPackageNameToFilename(
            Package->GetName(), FPackageName::GetAssetPackageExtension());
        TestFalse(Prefix + TEXT("ordinary package save veto"),
            FCoreUObjectDelegates::IsPackageOKToSaveDelegate.Execute(Package.Get(), File, GWarn));
        {
            P4ImportPublication::FSaveScope Publish(Package.Get());
            TestTrue(Prefix + TEXT("native import publication remains available"),
                FCoreUObjectDelegates::IsPackageOKToSaveDelegate.Execute(Package.Get(), File, GWarn));
        }
        TestFalse(
            Prefix + TEXT("publication exemption ends with the call"), P4ImportPublication::IsSaving(Package.Get()));
        const auto Actions = Tools.GetAssetTypeActionsForClass(Kind).Pin();
        if (Actions)
        {
            const TArray<FAssetData> Data{FAssetData(Asset.Get())};
            TestEqual(Prefix + TEXT("bulk edit/property matrix admission excludes game asset"),
                Actions->GetValidAssetsForPreviewOrEdit(Data, false).Num(), 0);
            TestTrue(Prefix + TEXT("lock mark"), P4Kadan::LockMark(Data[0]) != SNullWidget::NullWidget);
            const bool Dirty = Package->IsDirty();
            const auto Before = ObjectBytes(Asset.Get());
            const auto Stable = ObjectBytes(Asset.Get());
            TestTrue(Prefix + TEXT("serialization baseline is stable before inspection"), Before == Stable);
            TMap<FName, FString> Values;
            for (TFieldIterator<FProperty> Field(Kind); Field; ++Field)
            {
                FString Value;
                Field->ExportText_InContainer(0, Value, Asset.Get(), Asset.Get(), Asset.Get(), PPF_None);
                Values.Add(Field->GetFName(), Value);
            }
            Actions->OpenAssetEditor({Asset.Get()});
            auto* Editor = Editors->FindEditorForAsset(Asset.Get(), false);
            TestTrue(Prefix + TEXT("direct type-action edit routes to reference"),
                Editor && Editor->GetEditorName() == FName(TEXT("KadanReference")));
            const auto After = ObjectBytes(Asset.Get());
            TestTrue(Prefix + TEXT("reference open preserves serialized data"), Before == After);
            if (Before != After)
            {
                int32 At = 0;
                while (At < Before.Num() && At < After.Num() && Before[At] == After[At])
                    ++At;
                AddInfo(Prefix +
                    FString::Printf(
                        TEXT("serialization differs at byte %d, %d -> %d bytes"), At, Before.Num(), After.Num()));
                for (int32 I = At; I < Before.Num() && I < After.Num(); ++I)
                    if (Before[I] != After[I])
                        AddInfo(Prefix + FString::Printf(TEXT("byte %d: %02x -> %02x"), I, Before[I], After[I]));
                for (TFieldIterator<FProperty> Field(Kind); Field; ++Field)
                {
                    FString Value;
                    Field->ExportText_InContainer(0, Value, Asset.Get(), Asset.Get(), Asset.Get(), PPF_None);
                    if (Values.FindChecked(Field->GetFName()) != Value)
                        AddInfo(Prefix + TEXT("changed property: ") + Field->GetName());
                }
            }
            TestEqual(Prefix + TEXT("reference open preserves dirty flag"), Package->IsDirty(), Dirty);
            if (Editor)
            {
                auto* Toolkit = static_cast<FAssetEditorToolkit*>(Editor);
                for (const TCHAR* CommandName : {TEXT("SaveAsset"), TEXT("SaveAssetAs"), TEXT("ReimportAsset")})
                {
                    const auto Command = FInputBindingManager::Get().FindCommandInContext(
                        TEXT("AssetEditor"), CommandName);
                    TestTrue(Prefix + CommandName + TEXT(" command exists"), Command.IsValid());
                    if (Command)
                        TestFalse(Prefix + CommandName,
                            Toolkit->GetToolkitCommands()->CanExecuteAction(Command.ToSharedRef()));
                }
            }
            Package->SetDirtyFlag(false);
            Editors->CloseAllEditorsForAsset(Asset.Get());
        }
        // Direct property-toolkit construction is a separate route from browser admission.
        auto Matrix = Properties.CreatePropertyEditorToolkit(EToolkitMode::Standalone, nullptr, Asset.Get());
        P4Kadan::InitializePropertyGuards();
        FTicker::GetCoreTicker().Tick(0.2f);
        const auto Manager = Matrix->GetAssociatedTabManager();
        TestTrue(Prefix + TEXT("direct matrix has a tab manager"), Manager.IsValid());
        if (Manager)
        {
            const auto Tab = Manager->FindExistingLiveTab(FTabId(TEXT("PropertyEditorToolkit_PropertyTable")));
            TestTrue(Prefix + TEXT("direct matrix grid exists"), Tab.IsValid());
            if (Tab)
                TestFalse(Prefix + TEXT("direct matrix is disabled"), Tab->GetContent()->IsEnabled());
        }
        Package->SetDirtyFlag(false);
        Matrix->CloseWindow();
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->SetFlags(RF_Transient);
        Package->SetFlags(RF_Transient);
    }
    for (UClass* Kind : {UWorld::StaticClass(), UDataTable::StaticClass()})
    {
        TStrongObjectPtr<UPackage> Package(CreatePackage(*(Root / Kind->GetName())));
        TStrongObjectPtr<UObject> Asset(
            NewObject<UObject>(Package.Get(), Kind, FName(*Kind->GetName()), RF_Public | RF_Standalone));
        if (auto* Table = Cast<UDataTable>(Asset.Get()))
            Table->RowStruct = FTableRowBase::StaticStruct();
        FP4OriginalRecord Record;
        Record.Package = Package->GetName();
        Record.Class = Kind->GetPathName();
        Record.Kind = TEXT("test-editable-game-asset");
        FString Why;
        Record.SourceDigest = FString::ChrN(64, TEXT('a'));
        Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
        Record.SourceDigestKind = TEXT("fixture");
        Record.ComparisonSchema = TEXT("fixture");
        TestTrue(TEXT("editable exception marked"), P4MarkOriginalAsset(Asset.Get(), Record, Why));
        TestTrue(Kind->GetName() + TEXT(" remains editable"), P4Kadan::GetAssetPolicy(Asset.Get()).AllowEdit);
        TestTrue(Kind->GetName() + TEXT(" keeps browser edit admission"),
            Tools.AllPassWritableFolderFilter({Package->GetName()}));
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->SetFlags(RF_Transient);
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    }
    if (UClass* Game = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GameSceneData")))
    {
        TestFalse(TEXT("game class locked"), P4Kadan::GetAssetPolicy(Game).AllowEdit);
        TestFalse(TEXT("game class defaults locked"), P4Kadan::GetAssetPolicy(Game->GetDefaultObject()).AllowEdit);
    }
    else
        AddError(TEXT("The test project needs the game class declarations."));
    if (UClass* Wwise = FindObject<UClass>(nullptr, TEXT("/Script/AkAudio.AkAudioEvent")))
    {
        TStrongObjectPtr<UPackage> Package(CreatePackage(*(Root / TEXT("Wwise"))));
        TStrongObjectPtr<UObject> Asset(
            NewObject<UObject>(Package.Get(), Wwise, TEXT("Wwise"), RF_Public | RF_Standalone));
        FP4OriginalRecord Record;
        Record.Package = Package->GetName();
        Record.Class = Wwise->GetPathName();
        Record.Kind = TEXT("wwise");
        FString Why;
        Record.SourceDigest = FString::ChrN(64, TEXT('a'));
        Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
        Record.SourceDigestKind = TEXT("fixture");
        Record.ComparisonSchema = TEXT("fixture");
        TestTrue(TEXT("Wwise source marked"), P4MarkOriginalAsset(Asset.Get(), Record, Why));
        TestFalse(
            TEXT("Wwise locked irrespective of its editor module"), P4Kadan::GetAssetPolicy(Asset.Get()).AllowEdit);
        TestFalse(TEXT("Wwise browser write routes locked"), Tools.AllPassWritableFolderFilter({Package->GetName()}));
        BrowserRoutes(Asset.Get(), TEXT("Wwise: "));
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->SetFlags(RF_Transient);
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    }
    else
        AddError(TEXT("The test project needs the Wwise game class declarations."));
    // Do not leave deferred fixture collection for a later mesh test's inventory.
    FTicker::GetCoreTicker().Tick(0.2f);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyPropertiesTest, "P4MapKit.Kadan.ReadOnlyProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReadOnlyPropertiesTest::RunTest(const FString&)
{
    // What of a game table's rows can be edited: the values Tsuchi writes into a row, and an array's length.
    // What a game level allows is tried on Cave000 (P4MapKit.Kadan.ReadOnlyCave000).
    const auto* X = FindFProperty<FProperty>(TBaseStructure<FVector>::Get(), TEXT("X"));
    auto* Row = NewObject<UScriptStruct>();
    Row->SetSuperStruct(FTableRowBase::StaticStruct());
    auto* Words = new FTextProperty(Row, TEXT("Words"), RF_Public);
    Row->AddCppProperty(Words);
    auto* Number = new FFloatProperty(Row, TEXT("Number"), RF_Public);
    Row->AddCppProperty(Number);
    auto* Nested = new FStructProperty(Row, TEXT("Nested"), RF_Public);
    Nested->Struct = TBaseStructure<FVector>::Get();
    Row->AddCppProperty(Nested);
    auto* Array = new FArrayProperty(Row, TEXT("Values"), RF_Public);
    Array->Inner = new FFloatProperty(Array, TEXT("Inner"), RF_Public);
    Row->AddCppProperty(Array);
    auto* Texts = new FArrayProperty(Row, TEXT("Texts"), RF_Public);
    Texts->Inner = new FTextProperty(Texts, TEXT("Inner"), RF_Public);
    Row->AddCppProperty(Texts);
    auto* Map = new FMapProperty(Row, TEXT("Map"), RF_Public);
    Map->KeyProp = new FNameProperty(Map, TEXT("Key"), RF_Public);
    Map->ValueProp = new FFloatProperty(Map, TEXT("Value"), RF_Public);
    Row->AddCppProperty(Map);
    TestTrue(TEXT("table number editable"), P4Kadan::CanEditTableProperty(*Number));
    TestTrue(TEXT("table nested number editable"), X && P4Kadan::CanEditTableProperty(*X, {Nested}));
    TestTrue(TEXT("existing array number editable"), P4Kadan::CanEditTableProperty(*Array->Inner, {Array}));
    TestFalse(TEXT("unsupported localized table text read-only"), P4Kadan::CanEditTableProperty(*Words));
    TestTrue(TEXT("an array's length is editable"), P4Kadan::CanEditTableProperty(*Array));
    TestFalse(TEXT("an array of localized text is read-only"), P4Kadan::CanEditTableProperty(*Texts));
    TestFalse(TEXT("unsupported map leaf read-only"), P4Kadan::CanEditTableProperty(*Map->ValueProp, {Map}));
    TestFalse(TEXT("a map is read-only"), P4Kadan::CanEditTableProperty(*Map));
    TestFalse(TEXT("unsupported nested leaf read-only"), P4Kadan::CanEditTableProperty(*Words, {Nested}));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyPersistenceTest, "P4MapKit.Kadan.ReadOnlyPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReadOnlyPersistenceTest::RunTest(const FString&)
{
    const FString Name = TEXT("__KadanReadOnlySave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> Package(CreatePackage(*(TEXT("/Game/") + Name)));
    TStrongObjectPtr<UCurveFloat> Asset(NewObject<UCurveFloat>(Package.Get(), FName(*Name), RF_Public | RF_Standalone));
    const FString File = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    ON_SCOPE_EXIT
    {
        ResetLoaders(Package.Get());
        IFileManager::Get().Delete(*File, false, true);
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->SetFlags(RF_Transient);
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
        Package->GetMetaData()->RemoveValue(Asset.Get(), TEXT("P4OriginalRecord"));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
        FAssetRegistryModule::AssetDeleted(Asset.Get());
    };
    FP4OriginalRecord Record;
    Record.Package = Package->GetName();
    Record.Class = Asset->GetClass()->GetPathName();
    Record.Kind = TEXT("curve");
    FString Why;
    Record.SourceDigest = FString::ChrN(64, TEXT('a'));
    Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
    Record.SourceDigestKind = TEXT("fixture");
    Record.ComparisonSchema = TEXT("fixture");
    if (!TestTrue(TEXT("save fixture marked"), P4MarkOriginalAsset(Asset.Get(), Record, Why)))
        return false;
    {
        P4ImportPublication::FSaveScope Import(Package.Get());
        if (!TestTrue(TEXT("native publication actually saves a locked package"),
                GEditor->SavePackage(Package.Get(), Asset.Get(), RF_Public | RF_Standalone, *File, GWarn, nullptr,
                    false, false, SAVE_NoError)))
            return false;
    }
    TArray<uint8> Before, After;
    TestTrue(TEXT("saved package bytes available"), FFileHelper::LoadFileToArray(Before, *File));
    Asset->FloatCurve.AddKey(0, 123);
    Package->SetDirtyFlag(true);
    TestFalse(TEXT("ordinary engine save refuses changed game asset"),
        GEditor->SavePackage(
            Package.Get(), Asset.Get(), RF_Public | RF_Standalone, *File, GWarn, nullptr, false, false, SAVE_NoError));
    TestTrue(TEXT("veto preserves disk bytes"), FFileHelper::LoadFileToArray(After, *File) && After == Before);
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    Registry.ScanFilesSynchronous({File}, true);
    const auto Disk = Registry.GetAssetByObjectPath(FName(*Asset->GetPathName()), true);
    FString Lock;
    TestTrue(
        TEXT("saved disk registry carries lock"), Disk.GetTagValue(TEXT("P4KadanReadOnly"), Lock) && Lock == TEXT("1"));
    // Simulate the unloaded registry route independently of live object metadata.
    Package->GetMetaData()->RemoveValue(Asset.Get(), TEXT("P4OriginalRecord"));
    P4Kadan::UpdateReadOnlyAsset(Asset.Get());
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    TestTrue(TEXT("fixture live-object lock removed"), Tools.AllPassWritableFolderFilter({Package->GetName()}));
    Registry.OnAssetAdded().Broadcast(Disk);
    TestFalse(
        TEXT("disk registry alone reinstates browser lock"), Tools.AllPassWritableFolderFilter({Package->GetName()}));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlySnapshotIndexTest, "P4MapKit.Kadan.ReadOnlySnapshotIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReadOnlySnapshotIndexTest::RunTest(const FString&)
{
    P4RegisterKadanEditor();
    const FString Name = TEXT("/Game/__KadanSnapshotIndex_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> Package(CreatePackage(*Name));
    TStrongObjectPtr<UCurveFloat> Asset(
        NewObject<UCurveFloat>(Package.Get(), TEXT("Protected"), RF_Public | RF_Standalone | RF_Transactional));
    TArray<UCurveFloat*> Parts;
    for (int32 Index = 0; Index < 512; ++Index)
    {
        UCurveFloat* Part = NewObject<UCurveFloat>(
            Asset.Get(), *FString::Printf(TEXT("Part%d"), Index), RF_Transactional);
        Part->FloatCurve.AddKey(0.f, float(Index));
        Parts.Add(Part);
    }
    const auto Mark = [&]()
    {
        FP4OriginalRecord Record;
        Record.Package = Name;
        Record.Class = Asset->GetClass()->GetPathName();
        Record.Kind = TEXT("test-game-asset");
        Record.SourceDigest = FString::ChrN(64, TEXT('a'));
        Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
        Record.SourceDigestKind = TEXT("fixture");
        Record.ComparisonSchema = TEXT("fixture");
        FString Why;
        return P4MarkOriginalAsset(Asset.Get(), Record, Why);
    };
    const auto Unlock = [&]()
    {
        Package->GetMetaData()->RemoveValue(Asset.Get(), TEXT("P4OriginalRecord"));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
    };
    ON_SCOPE_EXIT
    {
        Unlock();
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package.Get(), Objects, true);
        for (UObject* Object : Objects)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->SetFlags(RF_Transient);
        }
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    };
    if (!TestTrue(TEXT("The package is protected"), Mark() && P4Kadan::IsLockedObject(Asset.Get())))
        return false;
    const double Started = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        Parts[Index]->Modify();
        Parts[Index]->FloatCurve.Reset();
        Parts[Index]->FloatCurve.AddKey(0.f, -1.f);
    }
    // Held values must retain the owners and their references through a collection.
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    P4Kadan::FlushReadOnlyGuards();
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
        TestEqual(TEXT("Every package part is restored after GC"), Parts[Index]->FloatCurve.Eval(0.f), float(Index));
    AddInfo(FString::Printf(
        TEXT("512 modified package parts captured and restored in %.3f s."), FPlatformTime::Seconds() - Started));
    // Unlocking removes the index entries too: a later lock must capture the new baseline.
    Unlock();
    TestFalse(TEXT("The package unlocks"), P4Kadan::IsLockedObject(Asset.Get()));
    Parts[0]->FloatCurve.Reset();
    Parts[0]->FloatCurve.AddKey(0.f, 77.f);
    if (!TestTrue(TEXT("The package can be protected again"), Mark()))
        return false;
    // Modify broadcasts only once per object in one engine frame. This test deliberately relocks
    // within that same frame; deliver a distinct ordinary modification notification to request the new baseline.
    TestTrue(TEXT("The relocked package guards its child again"), P4Kadan::IsLockedObject(Parts[0]));
    TestTrue(TEXT("The child is still valid after collection"), IsValid(Parts[0]));
    TestFalse(TEXT("The child has no load or transient flags"),
        Parts[0]->HasAnyFlags(
            RF_Transient | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization));
    FCoreUObjectDelegates::OnObjectModified.Broadcast(Parts[0]);
    Parts[0]->FloatCurve.Reset();
    Parts[0]->FloatCurve.AddKey(0.f, -2.f);
    P4Kadan::FlushReadOnlyGuards();
    TestEqual(TEXT("Relocking captures the new baseline instead of retaining a stale index entry"),
        Parts[0]->FloatCurve.Eval(0.f), 77.f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyBlueprintLoadTest, "P4MapKit.Kadan.ReadOnlyBlueprintInitialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReadOnlyBlueprintLoadTest::RunTest(const FString&)
{
    P4RegisterKadanEditor();
    const FString Name = TEXT("/Game/__KadanBlueprintInit_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> Package(CreatePackage(*Name));
    TStrongObjectPtr<UBlueprint> Asset(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package.Get(),
        TEXT("Protected"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestTrue(TEXT("The Blueprint has an event graph"), Asset.IsValid() && Asset->UbergraphPages.Num()))
        return false;
    UEdGraph* Graph = Asset->UbergraphPages[0];
    UEdGraphNode_Comment* Node = NewObject<UEdGraphNode_Comment>(Graph, TEXT("ProtectedComment"), RF_Transactional);
    Graph->Nodes.Add(Node);
    Node->NodeComment = TEXT("before loading");
    ON_SCOPE_EXIT
    {
        Asset->bIsRegeneratingOnLoad = false;
        Package->GetMetaData()->RemoveValue(Asset.Get(), TEXT("P4OriginalRecord"));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package.Get(), Objects, true);
        for (UObject* Object : Objects)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->SetFlags(RF_Transient);
        }
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    };
    FP4OriginalRecord Record;
    Record.Package = Name;
    Record.Class = Asset->GetClass()->GetPathName();
    Record.Kind = TEXT("test-game-asset");
    Record.SourceDigest = FString::ChrN(64, TEXT('a'));
    Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
    Record.SourceDigestKind = TEXT("fixture");
    Record.ComparisonSchema = TEXT("fixture");
    FString Why;
    if (!TestTrue(TEXT("The Blueprint is locked"),
            P4MarkOriginalAsset(Asset.Get(), Record, Why) && P4Kadan::IsLockedObject(Node)))
        return false;
    Asset->bIsRegeneratingOnLoad = true;
    FCoreUObjectDelegates::OnObjectModified.Broadcast(Node);
    Node->NodeComment = TEXT("engine initialized on load");
    Asset->bIsRegeneratingOnLoad = false;
    {
        TGuardValue<bool> LoadingPackage(GIsEditorLoadingPackage, true);
        FCoreUObjectDelegates::OnObjectModified.Broadcast(Node);
        Node->NodeComment = TEXT("map initialized on load");
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }
    FCoreUObjectDelegates::OnObjectModified.Broadcast(Node);
    Node->NodeComment = TEXT("unsupported creator edit");
    P4Kadan::FlushReadOnlyGuards();
    TestEqual(TEXT("Initialization is retained and a later creator edit is still put back"), Node->NodeComment,
        FString(TEXT("map initialized on load")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyBlueprintCompileTest, "P4MapKit.Kadan.ReadOnlyBlueprintCompilation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReadOnlyBlueprintCompileTest::RunTest(const FString&)
{
    P4RegisterKadanEditor();
    const FString Name = TEXT("/Game/__KadanBlueprintCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> Package(CreatePackage(*Name));
    TStrongObjectPtr<UBlueprint> Asset(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package.Get(),
        TEXT("Protected"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestTrue(TEXT("A disposable Blueprint exists"), Asset.IsValid()))
        return false;
    ON_SCOPE_EXIT
    {
        Package->GetMetaData()->RemoveValue(Asset.Get(), TEXT("P4OriginalRecord"));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package.Get(), Objects, true);
        for (UObject* Object : Objects)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->SetFlags(RF_Transient);
        }
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    };
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Object;
    Type.PinSubCategoryObject = UObject::StaticClass();
    Type.ContainerType = EPinContainerType::Array;
    const FName Member(TEXT("HeldReferences"));
    if (!TestTrue(TEXT("A generated object-array field is added"),
            FBlueprintEditorUtils::AddMemberVariable(Asset.Get(), Member, Type)))
        return false;
    FKismetEditorUtilities::CompileBlueprint(
        Asset.Get(), EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave);
    UObject* BeforeCDO = Asset->GeneratedClass->GetDefaultObject();
    FArrayProperty* BeforeArray = FindFProperty<FArrayProperty>(BeforeCDO->GetClass(), Member);
    if (!TestTrue(TEXT("The generated array has an object inner descriptor"),
            BeforeArray && CastField<FObjectPropertyBase>(BeforeArray->Inner)))
        return false;
    FScriptArrayHelper Initial(BeforeArray, BeforeArray->ContainerPtrToValuePtr<void>(BeforeCDO));
    UCurveFloat* Referenced = NewObject<UCurveFloat>(GetTransientPackage());
    const TWeakObjectPtr<UObject> HeldReference(Referenced);
    CastFieldChecked<FObjectPropertyBase>(BeforeArray->Inner)
        ->SetObjectPropertyValue(Initial.GetRawPtr(Initial.AddValue()), Referenced);
    Referenced = nullptr;
    FP4OriginalRecord Record;
    Record.Package = Name;
    Record.Class = Asset->GetClass()->GetPathName();
    Record.Kind = TEXT("test-game-asset");
    Record.SourceDigest = FString::ChrN(64, TEXT('a'));
    Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
    Record.SourceDigestKind = TEXT("fixture");
    Record.ComparisonSchema = TEXT("fixture");
    FString Why;
    if (!TestTrue(TEXT("The Blueprint default is protected"),
            P4MarkOriginalAsset(Asset.Get(), Record, Why) && P4Kadan::IsLockedObject(BeforeCDO)))
        return false;
    FCoreUObjectDelegates::OnObjectModified.Broadcast(BeforeCDO);
    Initial.EmptyValues();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    P4Kadan::FlushReadOnlyGuards();
    TestTrue(TEXT("Held values keep their only object reference alive"), HeldReference.IsValid());
    if (!TestEqual(TEXT("The object-array baseline is put back"), Initial.Num(), 1))
        return false;
    TestTrue(TEXT("Rollback restores the original reference"),
        CastFieldChecked<FObjectPropertyBase>(BeforeArray->Inner)->GetObjectPropertyValue(Initial.GetRawPtr(0)) ==
            HeldReference.Get());
    UStruct* BeforeOwner = BeforeArray->GetOwnerStruct();
    const int32 BeforeSerial = BeforeOwner->FieldPathSerialNumber;
    // Keep the same-frame snapshot while the ordinary compiler purges generated
    // descriptors. GC must traverse the owned copy, then retire the old layout.
    FCoreUObjectDelegates::OnObjectModified.Broadcast(BeforeCDO);
    FKismetEditorUtilities::CompileBlueprint(
        Asset.Get(), EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave);
    UObject* CurrentCDO = Asset->GeneratedClass->GetDefaultObject();
    FArrayProperty* CurrentArray = FindFProperty<FArrayProperty>(CurrentCDO->GetClass(), Member);
    if (!TestTrue(TEXT("Ordinary compilation publishes a newly linked generated field"),
            CurrentArray &&
                (CurrentArray->GetOwnerStruct() != BeforeOwner || BeforeOwner->FieldPathSerialNumber != BeforeSerial)))
        return false;
    // No use of BeforeArray/Initial is legal after the ordinary compiler above.
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    P4Kadan::FlushReadOnlyGuards();
    FScriptArrayHelper Current(CurrentArray, CurrentArray->ContainerPtrToValuePtr<void>(CurrentCDO));
    TArray<UObject*> Baseline;
    for (int32 Index = 0; Index < Current.Num(); ++Index)
        Baseline.Add(CastFieldChecked<FObjectPropertyBase>(CurrentArray->Inner)
                ->GetObjectPropertyValue(Current.GetRawPtr(Index)));
    FCoreUObjectDelegates::OnObjectModified.Broadcast(CurrentCDO);
    Current.AddValue();
    P4Kadan::FlushReadOnlyGuards();
    TestEqual(TEXT("A later edit uses the current class layout and is still refused"), Current.Num(), Baseline.Num());
    for (int32 Index = 0; Index < FMath::Min(Current.Num(), Baseline.Num()); ++Index)
        TestTrue(TEXT("Current-layout rollback preserves every object reference"),
            CastFieldChecked<FObjectPropertyBase>(CurrentArray->Inner)
                    ->GetObjectPropertyValue(Current.GetRawPtr(Index)) == Baseline[Index]);
    return !HasAnyErrors();
}
#endif

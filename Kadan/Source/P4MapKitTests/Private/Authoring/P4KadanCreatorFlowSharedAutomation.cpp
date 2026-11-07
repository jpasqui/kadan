#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

FFlowAssets::FFlowAssets(FFlow& InFlow) : Flow(InFlow)
{}

bool FFlowAssets::Fixture()
{
    if (!IFileManager::Get().MakeDirectory(*Flow.Out, true))
    {
        Flow.Fail(TEXT("Cannot create evidence directory"));
        return true;
    }
    FString Prior;
    const bool Owned = FFileHelper::LoadFileToString(Prior, *Flow.Marker) && Prior == Owner;
    if (!Owned)
        for (const FString& PackageName : {Flow.APackage, Flow.BPackage})
            if (IFileManager::Get().FileExists(
                    *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension())))
            {
                Flow.Fail(TEXT("An unmarked creator fixture already occupies ") + PackageName);
                return true;
            }
    // The runner marks this dedicated project and the previous native stage
    // marked its two identities. Recover only these four exact metadata
    // files, then recreate the identities through the normal UI below.
    const TArray<FString> Ids = {Flow.A.Id, Flow.B.Id};
    for (const FString& Id : Ids)
        for (const FString& Kind : {FString(TEXT("KadanMods")), FString(TEXT("KadanSelections"))})
        {
            const FString File = FPaths::ProjectDir() / TEXT("Config") / Kind / (Id + TEXT(".json"));
            if (IFileManager::Get().FileExists(*File))
            {
                if (!Owned)
                {
                    Flow.Fail(TEXT("Fixture identity belongs to an unmarked project: ") + File);
                    return true;
                }
                if (!IFileManager::Get().Delete(*File))
                {
                    Flow.Fail(TEXT("Cannot reset owned fixture metadata: ") + File);
                    return true;
                }
                Flow.RecoveredMetadata.Add(MakeShared<FJsonValueString>(File));
            }
        }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Flow.Marker), true);
    if (!FFileHelper::SaveStringToFile(Owner, *Flow.Marker, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Flow.Fail(TEXT("Cannot mark owned fixture"));
        return true;
    }
    for (const FString& PackageName : {Flow.APackage, Flow.BPackage})
    {
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
        UTexture2D* Texture = LoadObject<UTexture2D>(
            nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn);
        if (!Texture)
        {
            UPackage* Package = CreatePackage(*PackageName);
            Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
        }
        if (!Texture)
        {
            Flow.Fail(TEXT("Cannot construct creator texture"));
            return true;
        }
        TArray<uint8> Pixels;
        Pixels.SetNumZeroed(32 * 32 * 4);
        for (int32 I = 0; I < 32 * 32; ++I)
        {
            Pixels[I * 4 + 1] = 255;
            Pixels[I * 4 + 3] = 255;
        }
        Texture->Source.Init(32, 32, 1, 1, TSF_BGRA8, Pixels.GetData());
        Texture->MipGenSettings = TMGS_NoMipmaps;
        FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!UPackage::SavePackage(Texture->GetOutermost(), Texture, RF_Public | RF_Standalone, *File, GError, nullptr,
                false, true, SAVE_NoError))
        {
            Flow.Fail(TEXT("Cannot save native creator fixture: ") + PackageName);
            return true;
        }
        FAssetRegistryModule::AssetCreated(Texture);
        auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        Registry.ScanFilesSynchronous({File}, true);
    }
    return true;
}

bool FFlowAssets::SaveIssueFixture(UObject* Asset)
{
    const FString File = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),
        Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!UPackage::SavePackage(
            Asset->GetOutermost(), Asset, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError))
        return Flow.Expect(false, TEXT("Cannot save an owned export issue fixture"));
    FAssetRegistryModule::AssetCreated(Asset);
    return true;
}

bool FFlowAssets::IssueFixture()
{
    const FString CubePackage = Flow.BNames.PackageRoot / TEXT("IssueCube");
    auto* Cube = NewObject<UTextureCube>(CreatePackage(*CubePackage), TEXT("IssueCube"), RF_Public | RF_Standalone);
    TArray<uint8> Pixels;
    Pixels.Init(110, 4 * 4 * 4 * 6);
    Cube->Source.Init(4, 4, 6, 1, TSF_BGRA8, Pixels.GetData());
    if (!SaveIssueFixture(Cube) || Flow.Failed)
        return true;
    const FString MaterialPackage = Flow.BNames.PackageRoot / TEXT("IssueMaterial");
    auto* Material = NewObject<UMaterial>(
        CreatePackage(*MaterialPackage), TEXT("IssueMaterial"), RF_Public | RF_Standalone);
    auto* Expression = NewObject<UMaterialExpressionTextureSample>(Material);
    Expression->Texture = Cube;
    Material->Expressions.Add(Expression);
    Material->BaseColor.Expression = Expression;
    if (!SaveIssueFixture(Material) || Flow.Failed)
        return true;
    const FString LevelPackage = Flow.BNames.PackageRoot / TEXT("OrphanLevel");
    auto* World = UWorld::CreateWorld(
        EWorldType::Editor, false, FName(TEXT("OrphanLevel")), CreatePackage(*LevelPackage), false);
    if (!World)
        return Flow.Expect(false, TEXT("Cannot create the owned level without a game map"));
    World->SetFlags(RF_Public | RF_Standalone);
    return SaveIssueFixture(World);
}

bool FFlowAssets::SelectFixture(const FString& Package)
{
    UObject* Object = LoadObject<UObject>(
        nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
    if (!Object)
        return Flow.Expect(false, TEXT("The owned fixture asset is unavailable"));
    Flow.Browser().SyncBrowserToAssets({FAssetData(Object)});
    return true;
}

bool FFlowAssets::MapProgressFixture(bool ExpectLightingWarning)
{
    if (ExpectLightingWarning)
    {
        Flow.Test.AddExpectedError(TEXT("Maps need lighting rebuilt"), EAutomationExpectedErrorFlags::Contains, 0);
    }
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_MAP"));
    const FString Filename = FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension());
    if (!FEditorFileUtils::LoadMap(Filename))
    {
        return Flow.Expect(false, TEXT("The retained original map could not open"));
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    const auto Changes = P4AnalyzeOriginalLevelChanges(World);
    if (!Changes.IsValid())
    {
        return Flow.Expect(false, TEXT("The copied map's original comparison is unavailable. ") + Changes.ErrorDetail);
    }
    for (const FString& Name : Changes.Added)
    {
        if (AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name))
        {
            World->EditorDestroyActor(Actor, true);
        }
    }
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Cube)
    {
        return Flow.Expect(false, TEXT("The editor's cube source is unavailable"));
    }
    const FString Package = Flow.BNames.PackageRoot / TEXT("ProgressCube");
    UStaticMesh* Mesh = DuplicateObject<UStaticMesh>(Cube, CreatePackage(*Package), TEXT("ProgressCube"));
    UMaterialInterface* Material = nullptr;
    for (TActorIterator<AActor> It(World); It && !Material; ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Components(*It);
        for (const auto* Component : Components)
        {
            for (int32 MaterialIndex = 0; MaterialIndex < Component->GetNumMaterials(); ++MaterialIndex)
            {
                UMaterialInterface* Candidate = Component->GetMaterial(MaterialIndex);
                FP4OriginalRecord Record;
                FString Error;
                if (Candidate && P4ReadOriginalRecord(Candidate, Record, Error))
                {
                    Material = Candidate;
                    break;
                }
            }
        }
    }
    if (!Mesh || !Material)
    {
        return Flow.Expect(false, TEXT("The map has no imported material for the owned cube fixture"));
    }
    Mesh->SetMaterial(0, Material);
    Mesh->LightMapResolution = 32;
    if (!SaveIssueFixture(Mesh))
    {
        return false;
    }
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("KadanProgressCube");
    Spawn.OverrideLevel = World->PersistentLevel;
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, 180), FRotator::ZeroRotator, Spawn);
    if (!Actor)
    {
        return Flow.Expect(false, TEXT("The owned cube could not be placed"));
    }
    Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
    Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    return Flow.Expect(UEditorLoadingAndSavingUtils::SavePackages({World->GetOutermost()}, true),
        TEXT("The independent map copy could not save"));
}

FFlowModSteps::FFlowModSteps(FFlow& InFlow) : Flow(InFlow)
{}

void FFlowModSteps::AddNewMod(const FP4ModDescriptor& Mod)
{
    Flow.Add(TEXT("New mod ") + Mod.Id, [this] { return Flow.Click(TEXT("New mod")); });
    Flow.Add(TEXT("New form clears mod picker and selected roots"),
        [this]
        {
            auto Picker = StaticCastSharedPtr<FPicker>(Flow.Tagged(TEXT("KadanModPicker")));
            auto Roots = Flow.Tagged(TEXT("KadanSelectionRoots"));
            return Flow.Expect(Picker && !Picker->GetSelectedItem() && Roots &&
                    StaticCastSharedPtr<SListView<TSharedPtr<FP4ExportRoot>>>(Roots)->GetNumItemsBeingObserved() == 0 &&
                    !Flow.ButtonEnabled(TEXT("Review export")) && !Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("New mod retains previous mod selection or export actions"));
        });
    Flow.Add(TEXT("Enter ID ") + Mod.Id, [this, Mod] { return Flow.SetField(TEXT("ID"), Mod.Id); });
    Flow.Add(TEXT("Enter name ") + Mod.Id, [this, Mod] { return Flow.SetField(TEXT("Name"), Mod.Name); });
    Flow.Add(TEXT("Enter version ") + Mod.Id, [this, Mod] { return Flow.SetField(TEXT("Version"), Mod.Version); });
    Flow.Add(TEXT("Save mod ") + Mod.Id, [this] { return Flow.Click(TEXT("Save mod")); });
    Flow.Add(TEXT("Verify saved form ") + Mod.Id,
        [this, Mod]
        {
            auto Id = Flow.Field(TEXT("ID"));
            auto Name = Flow.Field(TEXT("Name"));
            FP4ModDescriptor Read;
            FString Error;
            if (!Id || !Name || Id->GetText().ToString() != Mod.Id || Name->GetText().ToString() != Mod.Name)
                return false;
            if (!P4ReadModDescriptor(FPaths::ProjectDir(), Mod.Id, Read, Error) || Read.Id != Mod.Id ||
                Read.Name != Mod.Name || Read.Version != Mod.Version)
                Flow.Fail(TEXT("Displayed and persisted mod identity differ: ") + Error);
            FWidgetPath Path;
            if (!FSlateApplication::Get().GeneratePathToWidgetUnchecked(Id.ToSharedRef(), Path))
            {
                Flow.Fail(TEXT("ID field is outside the live Slate hierarchy"));
                return true;
            }
            bool Enabled = true;
            for (int32 I = 0; I < Path.Widgets.Num(); ++I)
                Enabled &= Path.Widgets[I].Widget->IsEnabled();
            if (Enabled)
                Flow.Fail(TEXT("Saved mod ID remains editable"));
            return true;
        });
}

void FFlowModSteps::ReopenChecks()
{
    Flow.Add(TEXT("Choose first mod after reopening"), [this] { return Flow.Choose(Flow.A); });
    Flow.Add(TEXT("First mod remains empty after reopening"), [this] { return Flow.CheckRoots(Flow.A, {}); });
    Flow.Add(TEXT("Capture reopened empty mod"), [this] { return Flow.Screenshot(TEXT("first-empty")); });
    Flow.Add(TEXT("Choose second mod after reopening"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Second mod keeps its folder after reopening"),
        [this] { return Flow.CheckRoots(Flow.B, {Flow.BFolder}); });
    Flow.Add(TEXT("Capture reopened second mod"), [this] { return Flow.Screenshot(TEXT("second-retained")); });
}

bool FFlowModSteps::VerifyExport(bool CheckPanelLists)
{
    const FString Published = Flow.ExportFolder / Flow.B.Id;
    const FString Manifest = Published / TEXT("romfs/tsuchi/mods") / Flow.B.Id / TEXT("mod.json");
    FString Json;
    TSharedPtr<FJsonObject> Document;
    if (!FFileHelper::LoadFileToString(Json, *Manifest) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Document) || !Document)
    {
        Flow.Fail(TEXT("The UI export did not publish a readable manifest"));
        return true;
    }
    FString Id, Name, Version;
    double Framework = -1;
    if (!Document->TryGetStringField(TEXT("id"), Id) || Id != Flow.B.Id ||
        !Document->TryGetStringField(TEXT("name"), Name) || Name != Flow.B.Name ||
        !Document->TryGetStringField(TEXT("version"), Version) || Version != Flow.B.Version ||
        !Document->TryGetNumberField(TEXT("framework"), Framework) || Framework != 0)
    {
        Flow.Fail(TEXT("UI export manifest differs from selected mod identity"));
        return true;
    }
    const FString Base = FPaths::GetPath(Manifest) / (Flow.BNames.Container + TEXT("_P"));
    for (const TCHAR* Extension : {TEXT(".pak"), TEXT(".utoc"), TEXT(".ucas")})
        if (IFileManager::Get().FileSize(*(Base + Extension)) <= 0)
        {
            Flow.Fail(FString(TEXT("UI export is missing a complete container: ")) + Extension);
            return true;
        }
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Published, TEXT("*"), true, false);
    if (Files.Num() != 4)
    {
        Flow.Fail(TEXT("Tiny texture export must contain only manifest and three container files"));
        return true;
    }
    FP4ExportContainerTools Tools;
    Tools.Retoc = Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"));
    Tools.Repak = Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"));
    FP4ExportGameInventory Inventory;
    FP4ExportContainerCensus Census;
    FString Error;
    const FString Paks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    if (!P4ReadExportGameInventory(Tools, Paks, Flow.Out / TEXT("inspection"), Inventory, Error) ||
        !P4InspectExportContainer(
            Tools, Base + TEXT(".utoc"), Flow.Out / TEXT("inspection"), Inventory, Census, Error) ||
        !P4ValidateExportCensusSelection(Census, {Flow.BPackage}, Error))
        Flow.Fail(TEXT("Independent container selection inspection failed: ") + Error);
    if (CheckPanelLists)
    {
        const auto Shipping = Flow.Tagged(TEXT("KadanShippingList"));
        if (!Shipping ||
            StaticCastSharedPtr<SListView<TSharedPtr<FP4ExportSelectionRow>>>(Shipping)->GetNumItemsBeingObserved() !=
                1 ||
            !Flow.VisibleText(TEXT("Ships your asset · ") + Flow.BPackage))
            Flow.Fail(TEXT("The shipping list differs from the container's single creator texture."));
        const auto PublishedFiles = Flow.Tagged(TEXT("KadanPublishedFiles"));
        if (!PublishedFiles ||
            StaticCastSharedPtr<SListView<TSharedPtr<FString>>>(PublishedFiles)->GetNumItemsBeingObserved() !=
                Files.Num())
            Flow.Fail(TEXT("The panel's final file list differs from the files written by the exporter."));
    }
    if (CheckPanelLists && !Flow.VisibleText(TEXT("Kadan exported the mod to "), true))
        Flow.Fail(TEXT("The panel did not report successful publication"));
    return true;
}

bool FFlowModSteps::VerifyImportedTexture()
{
    UObject* Texture = LoadObject<UTexture2D>(
        nullptr, *(Flow.TexturePackage + TEXT(".") + FPackageName::GetLongPackageAssetName(Flow.TexturePackage)));
    if (!Texture)
        return Flow.Expect(false, TEXT("UI import did not save the selected original texture"));
    auto Diagnostic = P4DiagnoseOriginalAsset(Texture);
    return Flow.Expect(Diagnostic.Error.IsEmpty() && Diagnostic.Provenance.IsOriginal() &&
            Diagnostic.Provenance.Record.Package == Flow.TexturePackage &&
            Diagnostic.Comparison == EP4OriginalComparison::Equal,
        TEXT("Independent original texture comparison failed after UI import/reload: ") + Diagnostic.Error);
}

FExportProgressCapture::FExportProgressCapture(FFlow& InFlow) : Flow(InFlow)
{}

FExportProgressCapture::~FExportProgressCapture()
{
    StopProgressCapture();
}

void FExportProgressCapture::StopProgressCapture()
{
    if (ProgressTickHandle.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnPostTick().Remove(ProgressTickHandle);
    ProgressTickHandle.Reset();
}

bool FExportProgressCapture::ObserveExportProgress(bool CancelAtCooking, const FString& Prefix)
{
    StopProgressCapture();
    const FString CapturePrefix = Prefix.IsEmpty() ? Flow.Stage.ToLower() : Prefix;
    ProgressStages.Empty();
    ProgressCaptures.Empty();
    CancelExportAtCooking = CancelAtCooking;
    ExportCancelClicked = false;
    ProgressTickHandle = FSlateApplication::Get().OnPostTick().AddLambda(
        [this, CapturePrefix](float)
        {
            if (CapturingProgress)
                return;
            TArray<TSharedRef<SWindow>> Windows;
            FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
            if (auto Modal = FSlateApplication::Get().GetActiveModalWindow())
            {
                Windows.AddUnique(Modal.ToSharedRef());
            }
            for (auto Window : Windows)
            {
                if (!Window->IsVisible() || Window->GetTag() != FName(TEXT("KadanExportProgress")))
                    continue;
                auto StageWidget = Find(Window, [](const TSharedRef<SWidget>& Widget)
                    { return Widget->GetTag() == FName(TEXT("KadanExportStage")); });
                if (!StageWidget)
                    continue;
                const FString CurrentStage = StaticCastSharedPtr<STextBlock>(StageWidget)->GetText().ToString();
                if (ProgressStages.Contains(CurrentStage))
                    continue;
                TGuardValue<bool> CaptureGuard(CapturingProgress, true);
                TArray<FColor> Pixels;
                FIntVector Size;
                if (!FSlateApplication::Get().TakeScreenshot(Window, Pixels, Size))
                {
                    Flow.Fail(TEXT("Cannot capture the actual progress window"));
                    return;
                }
                TArray<uint8> Png;
                FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, Png);
                const FString File = CapturePrefix + TEXT("-progress-") +
                    (CancelExportAtCooking ? TEXT("cancel-") : TEXT("")) + CurrentStage.ToLower() + TEXT(".png");
                if (!FFileHelper::SaveArrayToFile(Png, *(Flow.Out / File)))
                {
                    Flow.Fail(TEXT("Cannot save a progress screenshot"));
                    return;
                }
                ProgressStages.Add(CurrentStage);
                Flow.CapturedScreens.Add(MakeShared<FJsonValueString>(File));
                auto Event = MakeShared<FJsonObject>();
                Event->SetStringField(TEXT("stage"), CurrentStage);
                Event->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());
                Event->SetNumberField(TEXT("monotonic_seconds"), FPlatformTime::Seconds());
                Event->SetStringField(TEXT("file"), File);
                ProgressCaptures.Add(MakeShared<FJsonValueObject>(Event));
                if (CancelExportAtCooking && CurrentStage == TEXT("Cooking"))
                {
                    auto Button = Find(Window,
                        [](const TSharedRef<SWidget>& Widget)
                        {
                            return Widget->GetType() == FName(TEXT("SButton")) &&
                                Text(Widget, TEXT("Cancel export")).IsValid();
                        });
                    ExportCancelClicked = Flow.MouseClick(Button);
                }
            }
        });
    return true;
}

bool FExportProgressCapture::SaveProgressTimeline(const FString& Name)
{
    StopProgressCapture();
    auto Json = MakeShared<FJsonObject>();
    Json->SetArrayField(TEXT("captures"), ProgressCaptures);
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    return Flow.Expect(FFileHelper::SaveStringToFile(Text, *(Flow.Out / (Name + TEXT("-timeline.json"))),
                           FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
        TEXT("The real stage timeline could not save"));
}

FMaterialDetails::FMaterialDetails(FFlow& InFlow) : Flow(InFlow)
{}

bool FMaterialDetails::OpenMaterialDetails(UMaterialInterface* Material)
{
    if (Flow.Phase == 0)
    {
        const auto Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
                                 .Get()
                                 .GetAssetTypeActionsForClass(Material->GetClass())
                                 .Pin();
        if (!Flow.Expect(Actions.IsValid(), TEXT("The production material asset action is unavailable")))
        {
            return true;
        }
        Actions->OpenAssetEditor({Material});
        Flow.Phase = 1;
        Flow.ReadyFrame = Flow.Frame + 6;
        return false;
    }
    auto* Instance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Material, false);
    if (!Instance)
    {
        return false;
    }
    auto* Toolkit = static_cast<FAssetEditorToolkit*>(Instance);
    const auto Manager = Toolkit->GetAssociatedTabManager();
    const FName TabName = Instance->GetEditorName() == TEXT("KadanReference") ? TEXT("Kadan_ReadOnlyReference")
        : Material->IsA<UMaterialInstance>() ? TEXT("MaterialInstanceEditor_MaterialProperties")
                                             : TEXT("MaterialEditor_MaterialProperties");
    const auto Panel = Manager ? Manager->FindExistingLiveTab(FTabId(TabName)) : nullptr;
    if (!Panel)
    {
        return false;
    }
    DetailsTab = Panel;
    Panel->ActivateInParent(ETabActivationCause::SetDirectly);
    if (const auto Window = FSlateApplication::Get().FindWidgetWindow(Panel->GetContent()))
    {
        Window->BringToFront(true);
        P4KadanResizeDetailsSplit(Window.ToSharedRef(), Panel->GetContent(), Orient_Horizontal);
        P4KadanResizeDetailsSplit(Window.ToSharedRef(), Panel->GetContent(), Orient_Vertical);
    }
    return true;
}

void FMaterialDetails::CloseMaterialDetails(UMaterialInterface* Material)
{
    GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Material);
    DetailsTab.Reset();
}

TSharedPtr<SWidget> FMaterialDetails::MaterialTagged(const FName Name)
{
    const auto Panel = DetailsTab.Pin();
    return Panel
        ? Find(Panel->GetContent(), [Name](const TSharedRef<SWidget>& Widget) { return Widget->GetTag() == Name; })
        : nullptr;
}

bool FMaterialDetails::MaterialPrepare(const TSharedPtr<SWidget>& Widget)
{
    if (!Widget || !DetailsTab.IsValid())
    {
        return false;
    }
    if (Flow.Prepared.Pin() != Widget)
    {
        const auto Panel = DetailsTab.Pin();
        if (const auto Window = FSlateApplication::Get().FindWidgetWindow(Panel->GetContent()))
        {
            Window->BringToFront(true);
        }
        Flow.ScrollParents(Panel->GetContent(), Widget);
        Flow.Prepared = Widget;
        Flow.ReadyFrame = Flow.Frame + 2;
        return false;
    }
    return Flow.Frame >= Flow.ReadyFrame;
}

bool FMaterialDetails::MaterialSearch(const FString& Value)
{
    const auto Panel = DetailsTab.Pin();
    if (!Panel)
    {
        return false;
    }
    const auto Search = Find(
        Panel->GetContent(), [](const TSharedRef<SWidget>& Widget) { return Widget->GetType() == TEXT("SSearchBox"); });
    const auto FieldWidget = Search;
    if (!MaterialPrepare(FieldWidget))
    {
        return false;
    }
    const auto Input = StaticCastSharedPtr<SEditableTextBox>(FieldWidget);
    if (Flow.Phase == 0)
    {
        if (!Flow.MouseClick(Input))
        {
            return false;
        }
        Flow.Phase = 1;
        Flow.ReadyFrame = Flow.Frame + 2;
        return false;
    }
    if (!Flow.Expect(Input->HasKeyboardFocus(), TEXT("The actual Material Details search field has no keyboard focus")))
    {
        return true;
    }
    auto& Slate = FSlateApplication::Get();
    const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
    const FKeyEvent SelectAll(EKeys::A, Control, 0, false, 0, 0);
    Slate.ProcessKeyDownEvent(SelectAll);
    Slate.ProcessKeyUpEvent(SelectAll);
    const FKeyEvent Backspace(EKeys::BackSpace, FModifierKeysState(), 0, false, 8, 0);
    Slate.ProcessKeyDownEvent(Backspace);
    Slate.ProcessKeyUpEvent(Backspace);
    for (TCHAR Character : Value)
    {
        Slate.ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0, false));
    }
    return true;
}

bool FMaterialDetails::MaterialCapture(const FString& Name, TSharedPtr<SWidget> Content)
{
    if (!Content)
    {
        const auto Panel = DetailsTab.Pin();
        if (Panel)
        {
            Content = Panel->GetContent();
        }
    }
    return Flow.CaptureWidget(Name, Content);
}

FUpgradeSnapshot::FUpgradeSnapshot(FFlow& InFlow) : Flow(InFlow)
{}

bool FUpgradeSnapshot::UpgradeContent(bool Reopen, bool AnalyzeMove)
{
    if (Reopen)
    {
        FString Json;
        TSharedPtr<FJsonObject> Record;
        if (!Flow.Expect(FFileHelper::LoadFileToString(Json, *(Flow.Out / TEXT("upgrade-content.json"))) &&
                    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Record) && Record,
                TEXT("The owned saved-content snapshot could not be read")))
        {
            return false;
        }
        UpgradeActorPath = Record->GetStringField(TEXT("actor"));
        UpgradeMeshFile = Record->GetStringField(TEXT("mesh_file"));
        UpgradeMeshHash = Record->GetStringField(TEXT("mesh_sha256"));
        if (!Flow.Expect(UpgradeActorTransform.InitFromString(Record->GetStringField(TEXT("transform"))) &&
                    FEditorFileUtils::LoadMap(FPackageName::LongPackageNameToFilename(
                        Record->GetStringField(TEXT("map")), FPackageName::GetMapPackageExtension())),
                TEXT("The saved map could not reopen")))
        {
            return false;
        }
    }
    const auto* Actor = FindObject<AActor>(nullptr, *UpgradeActorPath);
    auto* World = GEditor->GetEditorWorldContext().World();
    FP4LevelChangesResult Changes;
    if (AnalyzeMove)
    {
        Changes = P4AnalyzeOriginalLevelChanges(World);
    }
    const bool MoveKept = !AnalyzeMove ||
        (Actor && Changes.IsValid() &&
            Changes.Moved.ContainsByPredicate(
                [Actor](const FP4LevelActorMove& Move) { return Move.Actor == Actor->GetName(); }));
    int64 Bytes = 0;
    FString Hash;
    return Flow.Expect(Actor && Actor->GetActorTransform().Equals(UpgradeActorTransform),
               TEXT("The saved original actor transform changed")) &&
        Flow.Expect(MoveKept,
            TEXT("The saved original actor move is no longer recognized by the change analyzer. ") +
                Changes.ErrorDetail) &&
        Flow.Expect(World && FindObject<AActor>(World->PersistentLevel, TEXT("KadanProgressCube")),
            TEXT("The saved creator actor is missing")) &&
        Flow.Expect(P4HashFile(UpgradeMeshFile, 64 * 1024 * 1024, Bytes, Hash) && Hash == UpgradeMeshHash,
            TEXT("The saved creator mesh bytes changed"));
}
} // namespace P4KadanCreatorFlow
#endif

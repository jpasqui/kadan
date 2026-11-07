#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeTextureExportTest, "P4MapKit.Export.CreatorTextureContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeTextureExportTest::RunTest(const FString&)
{
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.texture.test"), TEXT("Native Texture Test"), Request, Names))
        return false;
    const FP4ModDescriptor& Mod = Request.Mod;
    const FString PackageName = Names.PackageRoot + TEXT("/Texture");
    if (FindObject<UPackage>(nullptr, *PackageName))
    {
        AddError(TEXT("Fixture package is already loaded; do not overwrite another test"));
        return false;
    }
    TStrongObjectPtr<UTexture2D> Texture(
        Cast<UTexture2D>(P4ExportDriverTest::ImportOwn(*this, Names.PackageRoot, TEXT("Texture"), false)));
    if (!Texture.IsValid())
        return false;
    TStrongObjectPtr<UPackage> Package(Texture->GetOutermost());
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->PostEditChange();
    TestTrue(TEXT("Cheap availability shares the driver's creator texture route"),
        P4ExportAssetAvailability(Texture.Get(), Mod).Route == EP4ExportAssetRoute::CreatorTexture);
    TestFalse(TEXT("Cheap availability refuses missing assets"), P4ExportAssetAvailability(nullptr, Mod).IsAvailable());
    TStrongObjectPtr<UTexture2D> Unqualified(NewObject<UTexture2D>(Package.Get(), TEXT("Unqualified"), RF_Transient));
    TestFalse(TEXT("an object without actual creator source is refused before cook"),
        P4ExportAssetAvailability(Unqualified.Get(), Mod).IsAvailable());
    TStrongObjectPtr<UCurveFloat> Unsupported(NewObject<UCurveFloat>(Package.Get(), TEXT("Unsupported"), RF_Transient));
    TestFalse(TEXT("Unsupported asset families have a named refusal before export"),
        P4ExportAssetAvailability(Unsupported.Get(), Mod).IsAvailable());
    FAssetRegistryModule::AssetCreated(Texture.Get());
    struct FCleanup
    {
        UTexture2D* Texture;
        ~FCleanup()
        {
            FAssetRegistryModule::AssetDeleted(Texture);
            Texture->ClearFlags(RF_Public | RF_Standalone);
        }
    } Cleanup{Texture.Get()};
    FP4ExportRoot Root;
    Root.Path = PackageName;
    Request.Roots.Add(Root);
    FP4ExportModResult Result;
    const bool Published = P4ExportMod(Request, Result, Error);
    if (!TestTrue(
            TEXT("Actual engine cook, retoc pack, independent packed bytes proof and atomic publication"), Published))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Publication is explicit in typed report"), Result.Published);
    TestEqual(TEXT("Only selected creator package shipped"), Result.Packages.Num(), 1);
    TestEqual(TEXT("Manifest and complete three-file container shipped"), Result.Files.Num(), 4);
    const FString Manifest = Result.Directory / TEXT("romfs/tsuchi/mods") / Mod.Id / TEXT("mod.json");
    FString Before, After;
    TestTrue(TEXT("Published manifest readable"), FFileHelper::LoadFileToString(Before, *Manifest));
    Request.OtherPackageClaims.Add(FName(*PackageName), TEXT("another.mod"));
    TestFalse(TEXT("Conflicting claim refuses another export"), P4ExportMod(Request, Result, Error));
    TestTrue(TEXT("Previous manifest still readable after refusal"), FFileHelper::LoadFileToString(After, *Manifest));
    TestEqual(TEXT("Failed export preserved previous manifest"), After, Before);
    Request.OtherPackageClaims.Reset();
    Request.Tools.Retoc = TEXT("missing-retoc.exe");
    TestFalse(TEXT("Missing packing/inspection tool cannot replace old export"), P4ExportMod(Request, Result, Error));
    TestTrue(TEXT("Old manifest survives unavailable tool"), FFileHelper::LoadFileToString(After, *Manifest));
    TestEqual(TEXT("Prior export retained after inspection failure"), After, Before);
    return true;
}

// A creator mesh with collision and the creator's material instance of a game material, packed with
// their texture. The material instance is made when the project has Cave000_F00's materials.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorMeshExportTest, "P4MapKit.Export.CreatorMeshContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorMeshExportTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.mesh.test"), TEXT("Native Mesh Test"), Request, Names))
        return false;
    FImported Imported(TEXT("ExportCreatorMeshContainer"));
    if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported.Ready))
        return false;
    // The material instance route is exercised even when whole-map Blueprint admission is incomplete.
    // Import only its game parent and the parent's dependencies through the same importer.
    if (!FPackageName::DoesPackageExist(GameParent))
    {
        const FP4KadanImportResult Parent = P4KadanImportMap(Request.ExtractedGameDirectory, GameParent, true);
        for (const FString& Problem : Parent.Problems)
            AddInfo(TEXT("Not imported. ") + Problem);
        if (!TestTrue(TEXT("the material instance's game parent is imported"),
                Parent.Ok && !Parent.Cancelled && FPackageName::DoesPackageExist(GameParent)))
            return false;
    }
    FOwn Own;
    if (!MakeOwn(*this, Names.PackageRoot, Own))
        return false;
    const FP4ModDescriptor& Mod = Request.Mod;
    if (!TestTrue(TEXT("the creator's material instance is made with its game parent"), Own.Instance.IsValid()))
        return false;
    TestTrue(TEXT("the mesh has the static mesh route"),
        P4ExportAssetAvailability(Own.Mesh.Get(), Mod).Route == EP4ExportAssetRoute::CreatorStaticMesh);
    TestTrue(TEXT("the texture has the texture route"),
        P4ExportAssetAvailability(Own.Texture.Get(), Mod).Route == EP4ExportAssetRoute::CreatorTexture);
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UMaterialInterface* GameInstance = LoadObject<UMaterialInterface>(nullptr, *ObjectPathOf(GameParent));
    TStrongObjectPtr<UObject> GameCopy(Tools.DuplicateAsset(TEXT("MI_GameCopy"), Names.PackageRoot, GameInstance));
    if (GameCopy.IsValid())
        Own.Made.Add(GameCopy.Get());
    if (TestNotNull(TEXT("ordinary duplicate of the actual game instance exists"), GameCopy.Get()))
        TestFalse(TEXT("copied game instance cannot enter the creator cook route"),
            P4ExportAssetAvailability(GameCopy.Get(), Mod).IsAvailable());
    TStrongObjectPtr<UMaterialInstanceConstant> Orphan(
        NewObject<UMaterialInstanceConstant>(Own.Texture->GetOutermost(), TEXT("MI_NoParent"), RF_Transient));
    TestFalse(TEXT("a material instance without a parent is refused"),
        P4ExportAssetAvailability(Orphan.Get(), Mod).IsAvailable());
    if (Own.Instance.IsValid())
    {
        TestTrue(TEXT("the material instance of a game material has its route"),
            P4ExportAssetAvailability(Own.Instance.Get(), Mod).Route == EP4ExportAssetRoute::CreatorMaterialInstance);
        Own.Instance->bHasStaticPermutationResource = true;
        TestEqual(TEXT("an instance permutation is admitted with the Switch compiler"),
            P4ExportAssetAvailability(Own.Instance.Get(), Mod).IsAvailable(), P4UseSwitchShaderRoute());
        Own.Instance->bHasStaticPermutationResource = false;
    }
    const FString MeshPackage = Own.Mesh->GetOutermost()->GetName(),
                  TexturePackage = Own.Texture->GetOutermost()->GetName();
    for (const FString& Path : {MeshPackage, TexturePackage})
    {
        FP4ExportRoot Root;
        Root.Path = Path;
        Request.Roots.Add(Root);
    }
    FP4ExportModResult Result;
    if (!TestTrue(TEXT("the mesh, its collision and what it uses are cooked, packed and published"),
            P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        for (const FString& Line : Result.Refused)
            AddError(Line);
        return false;
    }
    TArray<FString> Expected = {MeshPackage, TexturePackage};
    if (Own.Instance.IsValid())
        Expected.Add(Own.Instance->GetOutermost()->GetName());
    Expected.Sort();
    TestTrue(TEXT("the container holds the mesh, the texture and the material instance the mesh uses"),
        Result.Packages == Expected);
    TestEqual(TEXT("a manifest and one three-file container"), Result.Files.Num(), 4);
    const TSharedPtr<FJsonObject> Manifest = ReadJson(
        Result.Directory / TEXT("romfs/tsuchi/mods") / Mod.Id / TEXT("mod.json"));
    TestTrue(TEXT("the manifest names no map"), Manifest.IsValid() && !Manifest->HasField(TEXT("maps")));
    const FString CookedRoot = FPaths::ConvertRelativePathToFull(
                                   Request.WorkDirectory / (Mod.Id + TEXT(".export-work"))) /
        TEXT("cooked/Carrot4/Content");
    const FString Cooked = CookedRoot / MeshPackage.Mid(6) + TEXT(".uasset");
    std::vector<std::string> Said;
    TestTrue(TEXT("the cooked mesh names its collision as the game does"),
        CookedNames(Cooked, Said) && Has(Said, "PhysXGeneric") && !Has(Said, "PhysXPC"));
    for (const FString& Package : Expected)
    {
        const FString Header = CookedRoot / Package.Mid(6) + TEXT(".uasset");
        TestTrue(TEXT("creator content contains neither editor script package"),
            CookedNames(Header, Said) && !Has(Said, "/Script/Kadan") && !Has(Said, "/Script/P4MapKitAuthoring"));
        TestTrue(TEXT("the creator source role is stripped from the cooked asset"),
            std::none_of(Said.begin(), Said.end(), [](const std::string& Name)
                { return Name.find("P4CreatorSourceData") == 0 || Name == "P4KadanCreatorSource"; }));
    }
    FP4ExportGameInventory Inventory;
    FP4ExportContainerCensus Census;
    const FString ContainerFolder = Result.Directory / TEXT("romfs/tsuchi/mods") / Mod.Id;
    const FString Utoc = ContainerFolder / (Names.Container + TEXT("_P.utoc"));
    TestFalse(TEXT("No owned container is put in the global game Paks folder"),
        IFileManager::Get().DirectoryExists(*(Result.Directory / TEXT("romfs/Carrot4/Content/Paks"))));
    for (const TCHAR* Suffix : {TEXT(".pak"), TEXT(".ucas"), TEXT(".utoc")})
        TestTrue(TEXT("The complete owned container is beside the mod manifest"),
            IFileManager::Get().FileSize(*(ContainerFolder / (Names.Container + TEXT("_P") + Suffix))) > 0);
    if (TestTrue(TEXT("the game's inventory and the container are read"),
            P4ReadExportGameInventory(
                Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error) &&
                P4InspectExportContainer(Request.Tools, Utoc, Request.WorkDirectory, Inventory, Census, Error)))
    {
        TestEqual(TEXT("the container holds as many packages as were cooked"), Census.PackageIds.Num(), Expected.Num());
        TestEqual(TEXT("no game package is in the container"), Census.GameClaims.Num(), 0);
        for (const FString& Package : Expected)
            TestTrue(TEXT("each cooked package is in the container"),
                Census.PackageIds.Contains(P4ExportPackageId(Package)));
    }
    else
        AddError(Error);
    return !HasAnyErrors();
}

// The same creator content kept next to the game's folders, outside the mod's folder, as a creator
// keeps a new enemy beside the game's. It ships at the paths it has. The export stays in the project's
// Saved folder for a check in the game.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorOutsideModFolderTest, "P4MapKit.Export.CreatorOutsideModFolder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorOutsideModFolderTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.outside.test"), TEXT("Native Outside Test"), Request, Names))
        return false;
    FImported Imported(TEXT("ExportCreatorOutsideModFolder"));
    if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported.Ready))
        return false;
    if (!FPackageName::DoesPackageExist(GameParent))
    {
        const FP4KadanImportResult Parent = P4KadanImportMap(Request.ExtractedGameDirectory, GameParent, true);
        for (const FString& Problem : Parent.Problems)
            AddInfo(TEXT("Not imported. ") + Problem);
        if (!TestTrue(TEXT("the material instance's game parent is imported"),
                Parent.Ok && !Parent.Cancelled && FPackageName::DoesPackageExist(GameParent)))
            return false;
    }
    const FString Outside = TEXT("/Game/Carrot4/Characters/Teki/KadanOutsideTest");
    FOwn Own;
    if (!MakeOwn(*this, Outside, Own))
        return false;
    const FP4ModDescriptor& Mod = Request.Mod;
    TestTrue(TEXT("the mesh outside the mod's folder has the static mesh route"),
        P4ExportAssetAvailability(Own.Mesh.Get(), Mod).Route == EP4ExportAssetRoute::CreatorStaticMesh);
    TestTrue(TEXT("the texture outside the mod's folder has the texture route"),
        P4ExportAssetAvailability(Own.Texture.Get(), Mod).Route == EP4ExportAssetRoute::CreatorTexture);
    FP4ExportRoot Folder;
    Folder.Kind = EP4ExportRootKind::Folder;
    Folder.Path = Outside;
    Request.Roots.Add(Folder);
    FP4ExportModResult Result;
    if (!TestTrue(TEXT("the creator's content outside the mod's folder is cooked, packed and published"),
            P4ExportMod(Request, Result, Error)))
    {
        AddError(Error);
        for (const FString& Line : Result.Refused)
            AddError(Line);
        return false;
    }
    TArray<FString> Expected = {Own.Mesh->GetOutermost()->GetName(), Own.Texture->GetOutermost()->GetName()};
    if (Own.Instance.IsValid())
        Expected.Add(Own.Instance->GetOutermost()->GetName());
    Expected.Sort();
    TestTrue(TEXT("the container holds the content at the paths it has"), Result.Packages == Expected);
    for (const FString& Package : Result.Packages)
        AddInfo(TEXT("Shipped ") + Package);
    FP4ExportGameInventory Inventory;
    FP4ExportContainerCensus Census;
    const FString Utoc = Result.Directory / TEXT("romfs/tsuchi/mods") / Mod.Id / (Names.Container + TEXT("_P.utoc"));
    if (TestTrue(TEXT("the game's inventory and the container are read"),
            P4ReadExportGameInventory(
                Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error) &&
                P4InspectExportContainer(Request.Tools, Utoc, Request.WorkDirectory, Inventory, Census, Error)))
    {
        TestEqual(TEXT("no game package is in the container"), Census.GameClaims.Num(), 0);
        for (const FString& Package : Expected)
            TestTrue(TEXT("each package is in the container at its own path"),
                Census.PackageIds.Contains(P4ExportPackageId(Package)));
    }
    else
        AddError(Error);
    AddInfo(TEXT("Export kept at ") + Result.Directory);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorLevelScriptContainerTest, "P4MapKit.Export.CreatorLevelScriptContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorLevelScriptContainerTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("test.creatorlevelscript"), TEXT("Creator level script"), RequestData, Names))
        return false;
    const FString PackageName = Names.PackageRoot / TEXT("Maps/ScriptLevel");
    UPackage* Package = CreatePackage(*PackageName);
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("ScriptLevel"), Package);
    if (!TestNotNull(TEXT("Creator level is created"), World))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(World);
        World->DestroyWorld(false);
        Package->ClearFlags(RF_Public | RF_Standalone);
        Package->SetDirtyFlag(false);
        Package->MarkPendingKill();
    };
    World->SetFlags(RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(World);
    ULevelScriptBlueprint* Script = World->PersistentLevel->GetLevelScriptBlueprint(false);
    if (!TestNotNull(TEXT("Creator level script exists"), Script))
        return false;
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Script);
    if (!TestNotNull(TEXT("Level script has an event graph"), Graph))
        return false;
    UK2Node_Event* Event = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (UK2Node_Event* Existing = Cast<UK2Node_Event>(Node))
            if (Existing->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
                Event = Existing;
    if (!Event)
    {
        Event = NewObject<UK2Node_Event>(Graph);
        Event->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), ALevelScriptActor::StaticClass());
        Event->bOverrideFunction = true;
        Graph->AddNode(Event);
        Event->CreateNewGuid();
        Event->AllocateDefaultPins();
    }
    Event->SetEnabledState(ENodeEnabledState::Enabled, true);
    UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
    Call->SetFromFunction(UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("SetGlobalTimeDilation")));
    Graph->AddNode(Call);
    Call->CreateNewGuid();
    Call->AllocateDefaultPins();
    Call->FindPinChecked(TEXT("TimeDilation"))->DefaultValue = TEXT("0.75");
    const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
    UK2Node_Self* Self = NewObject<UK2Node_Self>(Graph);
    Graph->AddNode(Self);
    Self->CreateNewGuid();
    Self->AllocateDefaultPins();
    if (!TestTrue(TEXT("The level actor supplies its owning game world"),
            Schema->TryCreateConnection(
                Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Call->FindPinChecked(TEXT("WorldContextObject")))))
        return false;
    if (!TestTrue(TEXT("BeginPlay calls the creator's script"),
            Schema->TryCreateConnection(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),
                Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute))))
        return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Script);
    FKismetEditorUtilities::CompileBlueprint(Script);
    if (!TestTrue(TEXT("Creator script compiles"), Script->Status != BS_Error && Script->GeneratedClass))
        return false;
    const FP4ExportAvailability Availability = P4ExportAssetAvailability(World, RequestData.Mod);
    if (!TestTrue(TEXT("Panel supports the creator level with its script"),
            Availability.Route == EP4ExportAssetRoute::CreatorLevel))
        return false;
    FP4ExportSelectionPreview Selection;
    FP4ExportSelectionRow Row;
    Row.Package = PackageName;
    Row.Disposition = EP4ExportDisposition::CreatorPackage;
    Selection.Rows.Add(Row);
    if (!TestTrue(TEXT("Creator review permits the actual level-script route"),
            P4ReviewCreatorSelection(RequestData.Mod, Selection, nullptr).CanExport()))
        return false;
    RequestData.MapPackage = Cave000;
    FP4ExportRoot Root;
    Root.Path = PackageName;
    RequestData.Roots.Add(Root);
    FString Error;
    FP4ExportModResult Exported;
    if (!TestTrue(
            TEXT("The panel exporter writes the level script container"), P4ExportMod(RequestData, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("One creator level ships exactly once"),
        Exported.Packages == TArray<FString>{PackageName} && Exported.Levels == TArray<FString>{PackageName});
    const FString Folder = Exported.Directory / TEXT("romfs/tsuchi/mods") / RequestData.Mod.Id;
    const auto Manifest = ReadJson(Folder / TEXT("mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    if (!TestTrue(TEXT("Manifest has the game map load entry"),
            Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 1))
        return false;
    const auto Map = (*Maps)[0]->AsObject();
    TestEqual(TEXT("Script sublevel loads into Cave000"), Map->GetStringField(TEXT("map")), FString(Cave000));
    TestEqual(TEXT("Manifest names the creator level"), Map->GetArrayField(TEXT("levels"))[0]->AsString(), PackageName);
    const FString Readback = RequestData.WorkDirectory / TEXT("script-readback");
    if (!TestTrue(TEXT("Container level is extracted for bytecode readback"),
            P4ExtractExportReadback(RequestData.Tools, Folder / (Names.Container + TEXT("_P.utoc")),
                RequestData.GamePaksDirectory, RequestData.WorkDirectory, Readback, TEXT(""), Error)))
    {
        AddError(Error);
        return false;
    }
    const FString HeaderPath = Readback / TEXT("Carrot4/Content") / (PackageName.Mid(6) + TEXT(".umap"));
    TArray<uint8> Header, Body;
    if (!TestTrue(TEXT("Cooked level uses map files"),
            FFileHelper::LoadFileToArray(Header, *HeaderPath) &&
                FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(HeaderPath, TEXT("uexp")))))
        return false;
    const auto Inventory = P4MapKit::ReadPackage(std::vector<uint8>(Header.GetData(), Header.GetData() + Header.Num()),
        std::vector<uint8>(Body.GetData(), Body.GetData() + Body.Num()));
    TestTrue(TEXT("Cooked level retains the BeginPlay script function"),
        std::any_of(Inventory.Exports.begin(), Inventory.Exports.end(), [](const P4MapKit::FPackageExport& E)
            { return E.Name == "ReceiveBeginPlay" || E.Name.find("ExecuteUbergraph") == 0; }));
    TestTrue(TEXT("Cooked script retains its native time dilation call"),
        std::any_of(Inventory.Imports.begin(), Inventory.Imports.end(),
            [](const P4MapKit::FPackageImport& I) { return I.Name == "SetGlobalTimeDilation"; }));
    AddInfo(TEXT("Creator level script output is ") + Exported.Directory);
    if (!P4ReceiptPanelExport(*this, TEXT("script"), RequestData, PackageName))
        return false;
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSoundBankTest, "P4MapKit.Export.CreatorSoundBank",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSoundBankTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    using namespace P4MapKit::WwisePCM;
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("test.creatorsound"), TEXT("Creator sound bank"), RequestData, Names))
        return false;
    const FString PackageName = Names.PackageRoot / TEXT("Sounds/tone");
    UPackage* Package = CreatePackage(*PackageName);
    USoundWave* Wave = NewObject<USoundWave>(Package, TEXT("tone"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Wave);
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Wave);
        Wave->ClearFlags(RF_Public | RF_Standalone);
        Wave->MarkPendingKill();
        Package->ClearFlags(RF_Public | RF_Standalone);
        Package->SetDirtyFlag(false);
        Package->MarkPendingKill();
    };
    Bytes Format;
    U16(Format, 1);
    U16(Format, 1);
    U32(Format, 8000);
    U32(Format, 16000);
    U16(Format, 2);
    U16(Format, 16);
    Bytes PCM{0, 0, 0x20, 0, 0xe0, 0xff, 0, 0}, Riff{'W', 'A', 'V', 'E'};
    Chunk(Riff, "fmt ", Format);
    Chunk(Riff, "data", PCM);
    Bytes Source{'R', 'I', 'F', 'F'};
    U32(Source, uint32(Riff.size()));
    Append(Source, Riff);
    Wave->RawData.Lock(LOCK_READ_WRITE);
    void* Storage = Wave->RawData.Realloc(Source.size());
    FMemory::Memcpy(Storage, Source.data(), Source.size());
    Wave->RawData.Unlock();
    FString Error;
    Sample Read;
    if (!TestTrue(TEXT("Native exporter reads the editor WAV source"), P4CreatorWaveSample(Wave, Read, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("PCM bytes are exact"),
        Read.PCM == PCM && Read.Rate == 8000 && Read.Channels == 1 && Read.Label == "tone");
    TestTrue(TEXT("Panel routes source WAV into the bank"),
        P4ExportAssetAvailability(Wave, RequestData.Mod).Route == EP4ExportAssetRoute::CreatorSound);
    FP4ExportSelectionPreview Selection;
    FP4ExportSelectionRow Row;
    Row.Package = PackageName;
    Row.Disposition = EP4ExportDisposition::CreatorPackage;
    Selection.Rows.Add(Row);
    if (!TestTrue(TEXT("Creator review permits the actual source sound-bank route"),
            P4ReviewCreatorSelection(RequestData.Mod, Selection, nullptr).CanExport()))
        return false;
    FP4ExportRoot Root;
    Root.Path = PackageName;
    RequestData.Roots.Add(Root);
    FP4ExportModResult Exported;
    if (!TestTrue(TEXT("Panel writes mod-owned sound bank from actual game bank inputs"),
            P4ExportMod(RequestData, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Folder = Exported.Directory / TEXT("romfs/tsuchi/mods") / RequestData.Mod.Id;
    const auto Manifest = ReadJson(Folder / TEXT("mod.json"));
    const TSharedPtr<FJsonObject>* Content = nullptr;
    const TSharedPtr<FJsonObject>* Banks = nullptr;
    const bool Declared = Manifest && Manifest->TryGetObjectField(TEXT("content"), Content) &&
        (*Content)->TryGetObjectField(TEXT("sound_banks"), Banks);
    if (TestTrue(TEXT("Tsuchi sound bank is declared"), Declared))
        TestEqual(
            TEXT("Declaration names bank file"), (*Banks)->GetStringField(TEXT("sounds")), FString(TEXT("sounds.bnk")));
    TArray<uint8> Data;
    if (!TestTrue(TEXT("Bank file is present"), FFileHelper::LoadFileToArray(Data, *(Folder / TEXT("sounds.bnk")))))
        return false;
    Inventory Bank;
    ReadBankInventory({Data.GetData(), Data.GetData() + Data.Num()}, false, Bank);
    TestTrue(
        TEXT("Bank contains the expected creator event ID"), Bank.Ids.count(Id("test.creatorsound.sounds.tone")) != 0);
    TestEqual(TEXT("Source SoundWave is not copied into a game container"), Exported.Packages.Num(), 0);
    TestTrue(TEXT("Published shipping list contains bank"),
        Exported.Files.Contains(TEXT("romfs/tsuchi/mods/test.creatorsound/sounds.bnk")));
    return !HasAnyErrors();
}
#endif

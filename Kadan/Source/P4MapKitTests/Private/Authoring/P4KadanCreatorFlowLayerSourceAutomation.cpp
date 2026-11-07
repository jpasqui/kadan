#include "P4KadanCreatorFlowLayers.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

void FLayersStage::AddSteps()
{
    if (Flow.Stage == TEXT("MapLayersDistinct"))
    {
        Flow.Add(
            TEXT("Import a genuine source streaming graph into the owned project"),
            [this]() { return PrepareDistinctLayerFixture(); }, 600);
    }
    if (Flow.Stage != TEXT("Pages"))
    {
        Flow.Add(
            TEXT("Open an independent copy of the actual imported map"), [this]() { return LayerMapFixture(); }, 180);
    }
    if (Flow.Stage == TEXT("MapLayers"))
    {
        Flow.Add(
            TEXT("Rebind copied fixture custody to matching actual game sources"),
            [this]() { return RebindCopiedLayerSources(); }, 600);
    }
    Flow.Add(TEXT("Open the one Kadan panel through its menu"),
        [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the actual narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Remember this panel for page switching"),
        [this]()
        {
            SwitcherTab = Flow.Tab();
            return SwitcherTab.IsValid();
        });
    Flow.Add(TEXT("Switch to Import with the in-panel control"), [this]() { return Flow.Click(TEXT("Import")); });
    if (Flow.Stage == TEXT("MapLayersBefore"))
    {
        Flow.Add(TEXT("Capture the original map panel without layer controls"),
            [this]() { return Flow.Screenshot(TEXT("map")); });
        return;
    }
    if (Flow.Stage == TEXT("Pages"))
    {
        Flow.Add(TEXT("Import remains in the same panel without a menu"),
            [this]()
            {
                return SamePanelPage() &&
                    Flow.Expect(Flow.Field(TEXT("Game folder")).IsValid(),
                        TEXT("Import is not reachable from the panel's page control"));
            });
        Flow.Add(TEXT("Capture Import reached inside the panel"), [this]() { return Flow.Screenshot(TEXT("import")); });
        Flow.Add(
            TEXT("Switch to Mods and export with its page control"), [this]() { return Flow.Click(TEXT("Mods")); });
        Flow.Add(TEXT("Export remains visible in the same panel"),
            [this]() { return SamePanelPage() && Flow.StickyModsActions(); });
        Flow.Add(TEXT("Capture Mods and export reached inside the panel"),
            [this]() { return Flow.Screenshot(TEXT("mods-export")); });
        Flow.Add(TEXT("Switch to Asset with its page control"), [this]() { return Flow.Click(TEXT("Asset")); });
        Flow.Add(TEXT("Asset remains in the same panel without a menu"),
            [this]()
            {
                const auto Page = Flow.Tagged(TEXT("KadanPages"));
                return SamePanelPage() &&
                    Flow.Expect(Page && Page->GetType() == FName(TEXT("SScrollBox")),
                        TEXT("The Asset page is not displayed in the same panel"));
            });
        Flow.Add(TEXT("Capture Asset reached inside the panel"), [this]() { return Flow.Screenshot(TEXT("asset")); });
        return;
    }
    Flow.Add(TEXT("The actual open map has layer controls"),
        [this]()
        {
            return Flow.Expect(Flow.Tagged(TEXT("KadanMapLayers")).IsValid(),
                TEXT("The open imported game map has no layer selector"));
        });
    for (const FString& Mode : {FString(TEXT("Day")), FString(TEXT("Night")), FString(TEXT("Olimar"))})
    {
        Flow.Add(
            TEXT("Choose the actual ") + Mode + TEXT(" layer control"), [this, Mode]() { return Flow.Click(Mode); },
            180);
        Flow.Add(TEXT("The actual layer selection is reported for ") + Mode,
            [this, Mode]()
            {
                const bool Reported =
                    Flow.TaggedText(TEXT("KadanMapLayerStatus")).Contains(Mode, ESearchCase::IgnoreCase);
                FString Diagnostic;
                if (!Reported)
                {
                    TSet<FName> Expected;
                    P4ActivateOriginalMapEditorLayers(GEditor->GetEditorWorldContext().World(),
                        Mode == TEXT("Olimar") ? TEXT("hero") : Mode.ToLower(), Expected, Diagnostic);
                }
                return Flow.Expect(
                    Reported, TEXT("The map layer selector did not report the selected mode. ") + Diagnostic);
            });
        Flow.Add(
            TEXT("The saved map data remains unchanged for ") + Mode, [this]() { return LayerDataPreserved(); }, 180);
        if (Flow.Stage == TEXT("MapLayersDistinct"))
        {
            Flow.Add(
                TEXT("Check actual loaded-level visibility for ") + Mode,
                [this, Mode]() { return CheckDistinctLayerVisibility(Mode); }, 180);
            Flow.Add(TEXT("Expand the actual visible-level list for ") + Mode,
                [this]() { return Flow.Expand(TEXT("KadanMapLayerLevels"), TEXT("Visible levels"), true); });
        }
        Flow.Add(TEXT("Capture the actual ") + Mode + TEXT(" layer selection"),
            [this, Mode]() { return Flow.Screenshot(Mode.ToLower()); });
    }
}

bool FLayersStage::RequireLayer(bool Value, const FString& Why)
{
    if (!Value)
    {
        Flow.Fail(Why);
    }
    return Value;
}

bool FLayersStage::LinkLayerReference(const FString& Package)
{
    const FString Reference = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_REFERENCE_CONTENT"));
    if (!RequireLayer(
            !Reference.IsEmpty() && Package.StartsWith(TEXT("/Game/")) && FPackageName::IsValidLongPackageName(Package),
            TEXT("A retained reference requires an explicit source folder and game package")))
    {
        return false;
    }
    const FString Relative = Package.Mid(6);
    for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
    {
        const FString Source = FPaths::ConvertRelativePathToFull(Reference / (Relative + Extension));
        if (!FPaths::FileExists(Source))
        {
            continue;
        }
        const FString Target = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / (Relative + Extension));
        if (!RequireLayer(FPaths::IsUnderDirectory(Source, FPaths::ConvertRelativePathToFull(Reference)) &&
                    FPaths::IsUnderDirectory(Target, FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir())),
                TEXT("A retained fixture reference escaped its source or owned content folder")))
        {
            return false;
        }
        FString Digest;
        int64 Size = 0;
        if (!RequireLayer(P4HashFile(Source, 512ll * 1024 * 1024, Size, Digest),
                TEXT("The retained fixture reference could not be hashed. ") + Source))
        {
            return false;
        }
        LayerReferenceHashes.Add(Source, Digest);
        if (FPaths::FileExists(Target))
        {
            continue;
        }
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
#if PLATFORM_WINDOWS
        if (!RequireLayer(::CreateHardLinkW(*Target, *Source, nullptr) != 0,
                TEXT("The retained fixture reference could not be linked. ") + Target))
        {
            return false;
        }
#else
        return RequireLayer(false, TEXT("The retained fixture requires Windows hard links"));
#endif
    }
    return true;
}

bool FLayersStage::LayerReferencesPreserved()
{
    for (const auto& Pair : LayerReferenceHashes)
    {
        FString Digest;
        int64 Size = 0;
        if (!RequireLayer(P4HashFile(Pair.Key, 512ll * 1024 * 1024, Size, Digest) && Digest == Pair.Value,
                TEXT("A retained fixture reference changed. ") + Pair.Key))
        {
            return false;
        }
    }
    return true;
}

bool FLayersStage::RebindCopiedLayerSources()
{
    FP4KadanSourceCatalog Catalog;
    FString Error;
    if (!RequireLayer(
            P4KadanReadSourceCatalog(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")),
                Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe")), Catalog, Error),
            Error))
    {
        return false;
    }
    FP4KadanContainerIndex Containers;
    if (!RequireLayer(P4KadanReadContainerIndex(Catalog.ImportSettings, Containers, Error), Error))
    {
        return false;
    }
    const FString Sources = FPaths::ProjectSavedDir() / TEXT("Kadan/LayerVisibilitySource");
    TArray<UWorld*> Pending = {GEditor->GetEditorWorldContext().World()};
    TSet<FName> Seen;
    while (Pending.Num())
    {
        UWorld* World = Pending.Pop(false);
        if (!RequireLayer(World != nullptr, TEXT("A copied source world is unavailable")))
        {
            return false;
        }
        const FString Package = World->GetOutermost()->GetName();
        if (Seen.Contains(FName(*Package)))
        {
            continue;
        }
        Seen.Add(FName(*Package));
        if (!RequireLayer(Seen.Num() <= 64, TEXT("The copied source fixture exceeded its world bound")))
        {
            return false;
        }
        FP4OriginalRecord Record;
        if (!RequireLayer(P4ReadOriginalRecord(World, Record, Error), Error))
        {
            return false;
        }
        if (Record.ComparisonSchema == TEXT("game-backed-level-lines-v1"))
        {
            FString Mounted;
            if (!RequireLayer(Containers.Resolve(Package, Mounted, Error) &&
                        P4KadanConvertOriginal(Catalog.ImportSettings, Containers, Package, Sources, Error),
                    Error))
            {
                return false;
            }
            const FString Header = Sources / Mounted;
            const auto Bound = P4ImportGameBackedLevelNative(Header, FPaths::ChangeExtension(Header, TEXT("uexp")),
                true, {}, TEXT("Owned copied-source custody rebinding with exact source identity."), false);
            if (!RequireLayer(Bound.IsValid() && Bound.Asset == World && Bound.SourceDigest == Record.SourceDigest,
                    TEXT("A copied fixture's source could not be rebound. ") + Bound.ErrorDetail))
            {
                return false;
            }
        }
        for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
        {
            if (Streaming)
            {
                const FString Child = Streaming->GetWorldAssetPackageName();
                Pending.Add(LoadObject<UWorld>(nullptr, *(Child + TEXT(".") + FPackageName::GetShortName(Child))));
            }
        }
    }
    return LayerDataPreserved();
}

bool FLayersStage::PrepareDistinctLayerFixture()
{
    LayerReferenceHashes.Empty();
    const FString Root = TEXT("/Game/Carrot4/Maps/Main/Area/Area500/Area500_P");
    FP4KadanSourceCatalog Catalog;
    FString Error;
    if (!RequireLayer(
            P4KadanReadSourceCatalog(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")),
                Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe")), Catalog, Error),
            Error))
    {
        return false;
    }
    FP4KadanContainerIndex Containers;
    if (!RequireLayer(P4KadanReadContainerIndex(Catalog.ImportSettings, Containers, Error), Error))
    {
        return false;
    }
    const FString Sources = FPaths::ProjectSavedDir() / TEXT("Kadan/LayerVisibilitySource");
    TArray<FString> Pending = {Root};
    TSet<FName> Seen;
    TArray<TSharedPtr<FJsonValue>> Imported;
    while (Pending.Num())
    {
        const FString Package = Pending.Pop(false);
        if (Seen.Contains(FName(*Package)))
        {
            continue;
        }
        Seen.Add(FName(*Package));
        if (!RequireLayer(Seen.Num() <= 64 && Package.StartsWith(TEXT("/Game/")),
                TEXT("The layer fixture exceeded its bounded game-world graph")))
        {
            return false;
        }
        FString Mounted;
        if (!RequireLayer(Containers.Resolve(Package, Mounted, Error), Error) ||
            !RequireLayer(P4KadanConvertOriginal(Catalog.ImportSettings, Containers, Package, Sources, Error), Error))
        {
            return false;
        }
        const FString Header = Sources / Mounted;
        TArray<TSharedPtr<FP4KadanSourceAsset>> Scanned;
        TArray<FString> Problems;
        if (!RequireLayer(P4KadanScanGame(FPaths::GetPath(Header), Scanned, Problems) && Problems.Num() == 0,
                FString::Join(Problems, TEXT("\n"))))
        {
            return false;
        }
        for (const auto& Asset : Scanned)
        {
            if (FName(*Asset->Package) != FName(*Package))
            {
                continue;
            }
            for (const FString& Dependency : Asset->Dependencies)
            {
                if (!LinkLayerReference(Dependency))
                {
                    return false;
                }
            }
        }
        FP4OriginalLevelImportResult Result;
        if (!ImportLayerWorld(Catalog, Containers, Sources, Header, Result, Error))
        {
            return false;
        }
        if (!RequireLayer(Result.IsValid() && Result.Asset && Result.GameBacked,
                TEXT("The actual game-backed layer could not import. ") + Result.ErrorDetail))
        {
            return false;
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Result.Package);
        Row->SetStringField(TEXT("source_digest"), Result.SourceDigest);
        Row->SetNumberField(TEXT("original_actors"), Result.OriginalActorCount);
        Row->SetNumberField(TEXT("editable_actors"), Result.EditableOriginalActorCount);
        Imported.Add(MakeShared<FJsonValueObject>(Row));
        for (const ULevelStreaming* Streaming : Result.Asset->GetStreamingLevels())
        {
            if (Streaming)
            {
                Pending.Add(Streaming->GetWorldAssetPackageName());
            }
        }
    }
    auto Receipt = MakeShared<FJsonObject>();
    Receipt->SetArrayField(TEXT("worlds"), Imported);
    auto References = MakeShared<FJsonObject>();
    for (const auto& Pair : LayerReferenceHashes)
    {
        References->SetStringField(Pair.Key, Pair.Value);
    }
    Receipt->SetObjectField(TEXT("retained_reference_hashes"), References);
    Receipt->SetStringField(TEXT("scope"), TEXT("Actual source streaming graph and editor visibility only."));
    FString Json;
    FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<>::Create(&Json));
    if (!RequireLayer(FFileHelper::SaveStringToFile(Json, *(Flow.Out / TEXT("distinct-layer-source.json")),
                          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
            TEXT("The actual layer source receipt could not save")))
    {
        return false;
    }
    FPlatformMisc::SetEnvironmentVar(TEXT("P4_KADAN_UX_MAP"), *Root);
    return true;
}

bool FLayersStage::ImportLayerWorld(FP4KadanSourceCatalog& Catalog, FP4KadanContainerIndex& Containers,
    const FString& Sources, const FString& Header, FP4OriginalLevelImportResult& Result, FString& Error)
{
    TSet<FName> Repaired;
    for (int32 Attempt = 0; Attempt < 8; ++Attempt)
    {
        Result = P4ImportGameBackedLevelNative(Header, FPaths::ChangeExtension(Header, TEXT("uexp")), true, {},
            TEXT("Owned editor layer visibility fixture with original actors retained in the game."), false);
        if (Result.IsValid())
        {
            break;
        }
        const FString MissingMarker = TEXT("level-import-asset-not-in-the-project: ");
        const int32 At = Result.ErrorDetail.Find(MissingMarker, ESearchCase::CaseSensitive);
        if (At == INDEX_NONE)
        {
            break;
        }
        FString ObjectPath = Result.ErrorDetail.Mid(At + MissingMarker.Len());
        int32 End = INDEX_NONE;
        if (ObjectPath.FindChar(TEXT(':'), End))
        {
            ObjectPath.LeftInline(End);
        }
        ObjectPath.TrimEndInline();
        const FString Missing = FPackageName::ObjectPathToPackageName(ObjectPath);
        if (!FPackageName::IsValidLongPackageName(Missing) || Repaired.Contains(FName(*Missing)))
        {
            break;
        }
        Repaired.Add(FName(*Missing));
        FString ReferenceMounted;
        if (!RequireLayer(Containers.Resolve(Missing, ReferenceMounted, Error) &&
                    P4KadanConvertOriginal(Catalog.ImportSettings, Containers, Missing, Sources, Error),
                Error))
        {
            return false;
        }
        const FString ReferenceHeader = Sources / ReferenceMounted;
        const auto Reference = P4ImportOriginalAssetNative(
            ReferenceHeader, FPaths::ChangeExtension(ReferenceHeader, TEXT("uexp")), true, true);
        if (!RequireLayer(Reference.IsValid() && Reference.Asset && Reference.ReferenceOnly,
                TEXT("The actual required reference could not import. ") + Reference.ErrorDetail))
        {
            return false;
        }
    }
    return true;
}

bool FLayersStage::CheckDistinctLayerVisibility(const FString& Caption)
{
    const FString Mode = Caption == TEXT("Olimar") ? TEXT("hero") : Caption.ToLower();
    UWorld* Root = GEditor->GetEditorWorldContext().World();
    TArray<TPair<UWorld*, bool>> Pending;
    Pending.Emplace(Root, true);
    TMap<FName, bool> Seen;
    TMap<ULevelStreaming*, bool> Expected;
    TArray<TSharedPtr<FJsonValue>> Levels;
    int32 Matching = 0;
    int32 Opposite = 0;
    while (Pending.Num())
    {
        const auto Visit = Pending.Pop(false);
        UWorld* World = Visit.Key;
        if (!RequireLayer(World != nullptr, TEXT("A real imported layer is unavailable")))
        {
            return false;
        }
        const FName Package = World->GetOutermost()->GetFName();
        if (const bool* Prior = Seen.Find(Package))
        {
            if (*Prior || !Visit.Value)
            {
                continue;
            }
        }
        Seen.Add(Package, Visit.Value);
        for (ULevelStreaming* Streaming : World->GetStreamingLevels())
        {
            if (!Streaming)
            {
                continue;
            }
            const FString Child = Streaming->GetWorldAssetPackageName();
            const FString Leaf = FPackageName::GetShortName(Child);
            const bool Day = Leaf.Contains(TEXT("_Day"), ESearchCase::IgnoreCase);
            const bool Night = Leaf.Contains(TEXT("_Night"), ESearchCase::IgnoreCase);
            const bool Hero = Leaf.Contains(TEXT("_Hero"), ESearchCase::IgnoreCase);
            const FString ChildMode = Hero ? TEXT("hero") : Night ? TEXT("night") : Day ? TEXT("day") : TEXT("common");
            const bool Visible = Visit.Value && (ChildMode == TEXT("common") || ChildMode == Mode);
            Expected.FindOrAdd(Streaming) |= Visible;
            UWorld* ChildWorld = LoadObject<UWorld>(nullptr, *(Child + TEXT(".") + FPackageName::GetShortName(Child)));
            Pending.Emplace(ChildWorld, Visible);
        }
    }
    for (const auto& Pair : Expected)
    {
        ULevelStreaming* Streaming = Pair.Key;
        const FString Child = Streaming->GetWorldAssetPackageName();
        const ULevel* Level = Streaming->GetLoadedLevel();
        const bool LoadedVisible = Level && Level->bIsVisible;
        if (!RequireLayer(Streaming->GetShouldBeVisibleInEditor() == Pair.Value && LoadedVisible == Pair.Value,
                TEXT("The actual editor visibility differs from the chosen layer set. ") + Child))
        {
            return false;
        }
        if (Child.Contains(TEXT("_Day"), ESearchCase::IgnoreCase))
        {
            Matching += Mode == TEXT("day") && LoadedVisible ? 1 : 0;
            Opposite += Mode != TEXT("day") && !LoadedVisible ? 1 : 0;
        }
        if (Child.Contains(TEXT("_Night"), ESearchCase::IgnoreCase))
        {
            Matching += Mode == TEXT("night") && LoadedVisible ? 1 : 0;
            Opposite += Mode != TEXT("night") && !LoadedVisible ? 1 : 0;
        }
        if (Child.Contains(TEXT("_Hero"), ESearchCase::IgnoreCase))
        {
            Matching += Mode == TEXT("hero") && LoadedVisible ? 1 : 0;
            Opposite += Mode != TEXT("hero") && !LoadedVisible ? 1 : 0;
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Child);
        Row->SetBoolField(TEXT("editor_visible"), Streaming->GetShouldBeVisibleInEditor());
        Row->SetBoolField(TEXT("loaded_level_visible"), LoadedVisible);
        Levels.Add(MakeShared<FJsonValueObject>(Row));
    }
    if (!RequireLayer(
            Opposite > 0 && Matching > 0, TEXT("The fixture did not prove separate Day, Night and Olimar levels")))
    {
        return false;
    }
    auto Receipt = MakeShared<FJsonObject>();
    Receipt->SetArrayField(TEXT("levels"), Levels);
    Receipt->SetStringField(TEXT("mode"), Mode);
    FString Json;
    FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<>::Create(&Json));
    return LayerDataPreserved() && LayerReferencesPreserved() &&
        RequireLayer(FFileHelper::SaveStringToFile(Json, *(Flow.Out / (TEXT("distinct-layer-") + Mode + TEXT(".json"))),
                         FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
            TEXT("The actual visibility receipt could not save"));
}
} // namespace P4KadanCreatorFlow
#endif

// Development test seams of the level content files. Compiled only into development test builds.

#include "P4LevelContentInternal.h"
#include "P4Common.h"

#if P4_KADAN_TEST_SEAMS

namespace P4LevelContent
{
bool P4UnheldClassValuesForTest(const UStruct* Schema, const FString& Json, FString& Kept, int32& Count)
{
    FDecodedValue Value;
    if (!P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Json)), Value))
        return false;
    FString Error;
    Count = 0;
    Unheld(Schema, Value, Count, 0, nullptr, FString(), FString(), nullptr, &Error);
    Kept = Text(P4MapKit::DecodedJson(Value));
    return Error.IsEmpty();
}

bool P4UnheldClassValueDetailsForTest(const UStruct* Schema, const FString& Json, FString& Kept, int32& Count,
    TArray<FP4OriginalClassValueNotHeld>& Details)
{
    FDecodedValue Value;
    if (!P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Json)), Value))
        return false;
    Count = 0;
    Details.Reset();
    FString Error;
    Unheld(Schema, Value, Count, 0, &Details, TEXT("SourceTemplate"), FString(), nullptr, &Error);
    Kept = Text(P4MapKit::DecodedJson(Value));
    return Error.IsEmpty();
}
} // namespace P4LevelContent

// Exposes class-line folding to native controls.
FString P4LevelClassLineForTest(const FString& Class, bool Fold)
{
    P4LevelContent::FFile File;
    P4LevelContent::FNode Node;
    Node.Path = TEXT("Layer.PersistentLevel.SourceActor");
    Node.Class = Class;
    Node.PlainTail = true;
    File.Nodes.Add(Node);
    TArray<FString> Compared, Derived;
    P4LevelContent::Lines(File, Compared, Derived, nullptr, Fold);
    return Compared.Num() ? Compared[0] : FString();
}

bool P4TerrainTrackingLinesForTest(
    const FString& Class, const FString& Json, TArray<FString>& Compared, TArray<FString>& EngineMade)
{
    P4LevelContent::FFile File;
    P4LevelContent::FNode Node;
    Node.Class = Class;
    Node.Path = TEXT("source.component");
    P4MapKit::FDecodedValue Value;
    std::size_t At = 0;
    const std::string Bytes = TCHAR_TO_UTF8(*Json);
    if (!P4MapKit::ReadDecodedJson(Bytes, At, Value) || At != Bytes.size() ||
        Value.Kind != P4MapKit::FDecodedValue::EKind::Array)
        return false;
    Node.Values.Fields.emplace_back("UCSModifiedProperties", std::move(Value));
    File.Nodes.Add(MoveTemp(Node));
    Compared.Reset();
    EngineMade.Reset();
    P4LevelContent::Lines(File, Compared, EngineMade);
    return true;
}

bool P4ClassComponentTailForTest(UClass* Schema, const TArray<uint8>& Bytes, int32& Lods)
{
    P4LevelContent::FNode Node;
    Node.TailStart = 0;
    Node.TailEnd = Bytes.Num();
    const bool Good = P4LevelContent::ReadClassComponentTail(
        Node, Schema, std::vector<uint8>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()));
    Lods = Node.Lods;
    return Good;
}

bool P4ClassLightingTailForTest(
    UClass* Schema, const TArray<uint8>& Bytes, bool Undecoded, TArray<FString>& Compared, TArray<FString>& EngineMade)
{
    P4LevelContent::FFile File;
    P4LevelContent::FNode Node;
    Node.Name = TEXT("Sky");
    Node.Path = TEXT("Source.Sky");
    Node.Class = Schema ? Schema->GetPathName() : TEXT("/Script/CoreUObject.Object");
    Node.TailStart = 0;
    Node.TailEnd = Bytes.Num();
    if (Undecoded)
        Node.Undecoded = TEXT("invalid-properties");
    P4MapKit::FDecodedValue Authored;
    Authored.Kind = P4MapKit::FDecodedValue::EKind::Number;
    Authored.Text = "12.5";
    Node.Values.Fields.emplace_back("RayleighScatteringScale", std::move(Authored));
    File.Body.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    const bool Good = P4LevelContent::ReadClassComponentTail(Node, Schema, File.Body);
    File.Nodes.Add(MoveTemp(Node));
    Compared.Reset();
    EngineMade.Reset();
    P4LevelContent::Lines(File, Compared, EngineMade);
    return Good;
}

TArray<FString> P4ClassSourceAdmissionForTest(
    const FString& ClassPath, const FString& Root, TSet<FName>& Unproven, int32& Asked)
{
    P4LevelContent::FFile File;
    File.PackageName = TEXT("/Game/P4ClassSourceAdmission/World");
    P4LevelContent::FNode Node;
    Node.Class = ClassPath;
    File.Nodes.Add(Node);
    return P4LevelContent::ClassesThatDiffer(File, Root, Asked, nullptr, &Unproven);
}

FP4OriginalLevelImportResult P4RepairRetainedPlayerDefaultsForTest(const FString& HeaderFile, const FString& BodyFile,
    const FString& ExpectedSourceDigest, const FString& ExpectedSavedDigest)
{
    using namespace P4LevelContent;
    FP4OriginalLevelImportResult Result;
    auto Fail = [&](const FString& Detail)
    {
        Result.Error = EP4OriginalLevelImportError::CookDiffers;
        Result.ErrorDetail = Detail;
        return Result;
    };
    const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Sublevels/Cave000_F00_Player");
    const FString Short = FPackageName::GetShortName(Package);
    TArray<uint8> Header, Body;
    FString SourceDigest, Error, File;
    if (!IsInGameThread() || FString(FApp::GetProjectName()) != TEXT("KadanOne") || ExpectedSourceDigest.Len() != 64 ||
        ExpectedSavedDigest.Len() != 64 || IFileManager::Get().FileSize(*HeaderFile) < 0 ||
        IFileManager::Get().FileSize(*HeaderFile) > 64 * 1024 * 1024 || IFileManager::Get().FileSize(*BodyFile) < 0 ||
        IFileManager::Get().FileSize(*BodyFile) > 64 * 1024 * 1024 ||
        !FFileHelper::LoadFileToArray(Header, *HeaderFile, FILEREAD_Silent) ||
        !FFileHelper::LoadFileToArray(Body, *BodyFile, FILEREAD_Silent) ||
        !P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), SourceDigest) ||
        !P4SameText(SourceDigest, ExpectedSourceDigest))
        return Fail(TEXT("retained-player-source-digest-differs"));
    FFile Game;
    Game.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
    Game.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    if (!Read(Game, FString(), Error) || !P4SameText(Game.PackageName, Package))
        return Fail(TEXT("retained-player-source-identity-differs: ") + Error);
    if (!Refusal(Game).IsEmpty())
        return Fail(Refusal(Game));
    Result.Package = Package;
    Result.AssetPath = Package + TEXT(".") + Short;
    Result.Objects = Game.Nodes.Num();
    Result.IdentityKnown = true;
    const FString Tail = Package.Mid(6) + TEXT(".umap");
    FString Root = HeaderFile;
    FPaths::NormalizeFilename(Root);
    if (!Root.EndsWith(Tail, ESearchCase::CaseSensitive))
        return Fail(TEXT("retained-player-source-folder-unusable"));
    Root.LeftChopInline(Tail.Len());
    Game.SourceRoot = Root;
    if (!FPackageName::DoesPackageExist(Package, nullptr, &File) || !IFileManager::Get().IsReadOnly(*File))
        return Fail(TEXT("retained-player-not-a-protected-saved-layer"));
    int64 Bytes = 0;
    FString SavedDigest;
    if (!P4HashFile(File, 64 * 1024 * 1024, Bytes, SavedDigest) || !P4SameText(SavedDigest, ExpectedSavedDigest))
        return Fail(TEXT("retained-player-file-changed-before-repair"));
    TStrongObjectPtr<UWorld> Original(LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + Short)));
    FBaseline Previous;
    FP4OriginalRecord Record;
    if (!Original.IsValid() || Original->GetOutermost()->IsDirty() || P4HasCreatorSource(Original.Get()) ||
        !ReadBaseline(Original.Get(), Previous) || !P4SameText(Previous.Source, SourceDigest) ||
        !P4ReadOriginalRecord(Original.Get(), Record, Error) || Record.Kind != TEXT("level"))
        return Fail(TEXT("retained-player-original-record-or-source-differs"));
    P4ImportPublication::FImportScope Import;
    // Use the same source-class seeding as a first import. Cooked level deltas are relative to the
    // game's class defaults, not whichever defaults the retained editor Blueprint currently has.
    int32 Asked = 0;
    TArray<FString> Differing;
    FLentTemplates Lent;
    Result.DifferingClasses = ClassesThatDiffer(Game, Root, Asked, &Differing);
    Result.ClassesCompared = Asked;
    Result.ClassesKnown = true;
    const auto ClassDepth = [](const FString& Name)
    {
        int32 Depth = 0;
        const UClass* Class = LoadObject<UClass>(nullptr,
            *(Name + TEXT(".") + FPackageName::GetShortName(Name) + TEXT("_C")), nullptr, LOAD_NoWarn | LOAD_Quiet);
        for (; Class; Class = Class->GetSuperClass())
            ++Depth;
        return Depth;
    };
    Differing.StableSort([&](const FString& A, const FString& B) { return ClassDepth(A) < ClassDepth(B); });
    for (const FString& Name : Differing)
        Lent.Lend(Name, Root);
    Result.LentClasses = Lent.Classes;
    Result.ClassProblems = Lent.Problems;
    if (Lent.Problems.Num() || Lent.Classes.Num() != Differing.Num())
        return Fail(TEXT("retained-player-original-class-defaults-not-proven"));
    // Stage in memory. The original file, state record and loaded objects are untouched until a
    // complete cook of the staged correction equals the actual original game file.
    const FString StagePackage = TEXT("/Temp/P4RetainedPlayerProof_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/") + Short;
    TArray<FString> NotHeld, NotShipped;
    FString(Original->GetOutermost()->GetMetaData()->GetValue(Original.Get(), NotHeldKey))
        .ParseIntoArray(NotHeld, TEXT("\n"), true);
    const TSet<FString, FLocKeySetFuncs> PreviouslyNotHeld(NotHeld);
    FFile Staging = Game;
    Staging.PackageName = StagePackage;
    TArray<FString> StageOnly;
    TStrongObjectPtr<UWorld> Stage(Build(Staging, NotHeld, NotShipped, StageOnly, Error));
    if (!Stage.IsValid())
        return Fail(TEXT("retained-player-stage-not-made: ") + Error);
    Stage->ClearFlags(RF_Public | RF_Standalone);
    ON_SCOPE_EXIT
    {
        UWorld* Finished = Stage.Get();
        Stage.Reset();
        if (IsValid(Finished))
            Discard(Finished);
    };
    for (const FString& Value : NotHeld)
        if (!PreviouslyNotHeld.Contains(Value))
            return Fail(TEXT("retained-player-newly-unheld-value: ") + Value);
    // Only these source-absent constructor defaults are repaired in the retained layer. A complete
    // game comparison below refuses every unrelated field/object difference rather than replaying it.
    const FString Owner = Short + TEXT(".PersistentLevel.GAvatar2.CakEventHandler");
    const TSet<FString> Affected = {
        Owner, Owner + TEXT(".CakMoveSpeedControlUnit_0"), Owner + TEXT(".CakSinkRatioControlUnit_0")};
    auto Hydrate = [&](UWorld* World) -> bool
    {
        TMap<int32, UObject*> Made;
        int32 Repaired = 0;
        for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
            if (UObject* Object = StaticFindObject(
                    UObject::StaticClass(), World->GetOutermost(), *Game.Node(Index).Path))
                Made.Add(Index, Object);
        for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
        {
            if (!Affected.Contains(Game.Node(Index).Path))
                continue;
            if (Game.Node(Index).Path != Owner && !Game.Node(Index).Values.Fields.empty())
            {
                Error = TEXT("retained-player-unit-has-authored-values: ") + Game.Node(Index).Path;
                return false;
            }
            UObject* const* Found = Made.Find(Index);
            if (!Found || !P4SameText((*Found)->GetClass()->GetPathName(), Game.Node(Index).Class))
            {
                Error = TEXT("retained-player-source-object-not-present: ") + Game.Node(Index).Path;
                return false;
            }
            if (!Apply(Game, Index, *Found, Made, true, Error, &NotHeld, &NotShipped))
                return false;
            ++Repaired;
        }
        if (Repaired != Affected.Num())
        {
            Error = TEXT("retained-player-affected-source-exports-not-present");
            return false;
        }
        for (const FString& Value : NotHeld)
            if (!PreviouslyNotHeld.Contains(Value))
            {
                Error = TEXT("retained-player-newly-unheld-value: ") + Value;
                return false;
            }
        return true;
    };
    auto Compare = [&](UWorld* World, FFile& Mine, TArray<FString>& ExactLines) -> bool
    {
        if (!Cooked(World, true, Mine, Error))
            return false;
        // This proof world must live beside the real layer. Its locally generated script class has
        // that temporary package identity, while retaining the proven original class structure.
        // Normalize only exact class identities of matching original script exports; no field values.
        if (World == Stage.Get() && Game.ScriptClass)
        {
            const FString LocalClass = StagePackage + TEXT(".") + Game.Node(Game.ScriptClass).Path;
            const FString OriginalClass = Package + TEXT(".") + Game.Node(Game.ScriptClass).Path;
            for (FNode& Node : Mine.Nodes)
                if (Node.Class == LocalClass)
                {
                    bool OriginalExport = false;
                    for (const FNode& Source : Game.Nodes)
                        if (Source.Path == Node.Path && Source.Class == OriginalClass)
                        {
                            OriginalExport = true;
                            break;
                        }
                    if (!OriginalExport)
                    {
                        Error = TEXT("retained-player-staged-script-export-not-original: ") + Node.Path;
                        return false;
                    }
                    Node.Class = OriginalClass;
                }
        }
        FFile Compared = Game;
        Without(Compared, NotHeld);
        RedundantUnshippedNulls(Compared, Mine, Root, NotShipped, World);
        TArray<FString> GameSays, MineSays, GameEngine, MineEngine;
        Lines(Compared, GameSays, GameEngine, nullptr, true);
        Lines(Mine, MineSays, MineEngine, nullptr, true);
        TSet<FString, FLocKeySetFuncs> InGame(GameSays), InMine(MineSays);
        Result.OnlyGameSays.Reset();
        Result.OnlyLevelSays.Reset();
        for (const FString& Line : GameSays)
            if (!InMine.Contains(Line))
                Result.OnlyGameSays.Add(Line);
        for (const FString& Line : MineSays)
            if (!InGame.Contains(Line))
                Result.OnlyLevelSays.Add(Line);
        Result.Differences = Result.OnlyGameSays.Num() + Result.OnlyLevelSays.Num();
        Result.DifferencesKnown = true;
        Result.Compared = GameSays.Num();
        Result.ComparedKnown = true;
        Lines(Mine, ExactLines, MineEngine);
        if (Result.Differences)
        {
            Error = TEXT("retained-player-cook-differs-from-actual-game");
            return false;
        }
        return true;
    };
    FFile StagedCook;
    TArray<FString> StagedLines;
    if (!Compare(Stage.Get(), StagedCook, StagedLines))
        return Fail(Error);
    // No constructor normalization or old project fingerprint substitutes for this equality.
    if (Original->GetOutermost()->IsDirty() || !P4HashFile(File, 64 * 1024 * 1024, Bytes, SavedDigest) ||
        !P4SameText(SavedDigest, ExpectedSavedDigest))
        return Fail(TEXT("retained-player-changed-during-source-proof"));
    FFile ActualCook;
    TArray<FString> ActualLines;
    if (!Hydrate(Original.Get()) || !Compare(Original.Get(), ActualCook, ActualLines))
        return Fail(Error);
    // Preserve the baseline's ordinary editor-state format after the game-only defaults are returned.
    // Actual-game equality above is a separate proof, and never replaced by this new state snapshot.
    Lent.Return();
    if (!Cooked(Original.Get(), true, ActualCook, Error))
        return Fail(Error);
    TArray<FString> ActualEngine;
    ActualLines.Reset();
    Lines(ActualCook, ActualLines, ActualEngine);
    // Production levels are creator-editable, unlike locked reference assets. This private fixture's
    // explicit file protection is released only after both full source proofs, and restored on exit.
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (!Files.IsReadOnly(*File) || !P4HashFile(File, 64 * 1024 * 1024, Bytes, SavedDigest) ||
        !P4SameText(SavedDigest, ExpectedSavedDigest))
        return Fail(TEXT("retained-player-protected-file-changed-before-publication"));
    if (!Files.SetReadOnly(*File, false) || Files.IsReadOnly(*File))
        return Fail(TEXT("retained-player-protected-file-not-unlocked"));
    ON_SCOPE_EXIT
    {
        Files.SetReadOnly(*File, true);
    };
    FBaseline Repaired = Previous;
    if (!Digest(ActualLines, Repaired.State))
        return Fail(TEXT("retained-player-proven-state-unreadable"));
    auto Meta = Original->GetOutermost()->GetMetaData();
    Meta->SetValue(Original.Get(), BaselineKey, *BaselineText(Repaired));
    Meta->SetValue(Original.Get(), ImportedKey, *Pack(ActualLines));
    if (!P4MarkLegacyOriginalAsset(Original.Get(), Record.Values, Error))
        return Fail(Error);
    Original->MarkPackageDirty();
    if (!P4SaveAssetPackage(Original.Get(), [&]() { return P4SameText(State(Original.Get()), Repaired.State); }, Error))
        return Fail(Error);
    if (!Files.SetReadOnly(*File, true) || !Files.IsReadOnly(*File))
        return Fail(TEXT("retained-player-protected-file-not-restored"));
    Result.Asset = Original.Get();
    Result.Package = Package;
    Result.AssetPath = Package + TEXT(".") + Short;
    Result.IdentityKnown = Result.BaselineKnown = true;
    Result.SourceDigest = SourceDigest;
    Result.ImportedState = Result.State = Repaired.State;
    Result.Saved = true;
    return Result;
}

bool P4ApplyDeclaredLevelDefaultsForTest(
    UObject* Instance, UObject* Template, const FString& BoolProperty, bool BoolValue, FString& Error)
{
    if (!IsValid(Instance) || !IsValid(Template))
        return false;
    P4LevelContent::FFile File;
    File.World = 1;
    P4LevelContent::FNode Defaults;
    Defaults.Path = Template->GetPathName();
    Defaults.Class = Template->GetClass()->GetPathName();
    File.Nodes.Add(Defaults);
    P4LevelContent::FNode Node;
    Node.Path = Instance->GetPathName();
    Node.Class = Instance->GetClass()->GetPathName();
    Node.Template = 1;
    if (!BoolProperty.IsEmpty())
        Node.Values.Fields.emplace_back(std::string(TCHAR_TO_UTF8(*BoolProperty)),
            P4MapKit::FDecodedValue::Atom(P4MapKit::FDecodedValue::EKind::Boolean, BoolValue ? "true" : "false"));
    File.Nodes.Add(Node);
    TMap<int32, UObject*> Made;
    Made.Add(1, Template);
    Made.Add(2, Instance);
    return P4LevelContent::Apply(File, 2, Instance, Made, true, Error);
}

bool P4OriginalLevelStorageSubobjectMatchesForTest(
    UObject* Object, UObject* Outer, UClass* Declared, const FString& Name, uint32 SourceFlags, UObject* SourceTemplate)
{
    return P4LevelContent::ExistingStorageSubobject(Object, Outer, Declared, Name, SourceFlags, SourceTemplate);
}

FP4OriginalLevelImportResult P4ProbeOriginalGameBackedFamiliesForTest(
    const FString& HeaderFile, const FString& BodyFile)
{
    const FString Destination = TEXT("/Temp/P4OriginalFamilyProof_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) +
        TEXT("/") + FPaths::GetBaseFilename(HeaderFile);
    return P4LevelContent::ImportGameBacked(
        HeaderFile, BodyFile, false, {}, TEXT("unsaved source-family construction diagnostic"), true, Destination);
}

void P4DiscardOriginalFamilyProbeForTest(UWorld* World)
{
    if (IsValid(World))
        P4LevelContent::Discard(World);
}

#endif // P4_KADAN_TEST_SEAMS

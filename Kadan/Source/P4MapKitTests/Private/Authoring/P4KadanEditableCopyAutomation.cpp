// Copies of imported game assets, made as a creator makes them (Duplicate in the Content Browser), saved, and
// opened again by a second editor process. Run in a Kadan project that imported the game's assets:
//   P4_EDITABLE_COPY_TAG       a folder name for this run's copies (/Game/EditableCopyCheck_<tag>)
//   P4_EDITABLE_COPY_PACKAGES  for Create: game packages separated by ';', or class:<AssetClass> for the first
//                              complete (not reference-only) original of that class under /Game/Carrot4
//   P4_EDITABLE_COPY_SUFFIX    optional: each copy is named the original's name with this after it
// Create copies, checks and saves; Reopen (a separate editor process) loads the saved copies and checks again.
// A copy is exact when its cook holds the same lines as the original's (the copy's package name read as the
// original's), and, for a material instance, when Kadan's copied-content comparison says Equal.
#include "P4CreatorSource.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanReadOnly.h"
#include "P4KadanTestSeams.h"
#include "P4LevelContent.h"
#include "P4MaterialInstanceCopyContent.h"
#include "P4OriginalRecord.h"
#include "P4PackageSave.h"
#include "P4StockTags.h"
#include "P4StockTagTree.h"
#include "P4NativeHash.h"
#include "Dom/JsonObject.h"
#include "Curves/CurveFloat.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/MetaData.h"
#include "Misc/ScopeExit.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4EditableCopyCheck
{
FString Tag()
{
    return FPlatformMisc::GetEnvironmentVariable(TEXT("P4_EDITABLE_COPY_TAG"));
}
FString Folder()
{
    return TEXT("/Game/EditableCopyCheck_") + Tag();
}
FString Results()
{
    return FPaths::ProjectSavedDir() / TEXT("P4MapKit/EditableCopy") / Tag();
}

UObject* LoadAsset(const FString& Package)
{
    return LoadObject<UObject>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
}

// A complete original of the game: imported by Kadan, locked, holding all of its values.
bool CompleteOriginal(UObject* Asset, FString& Note)
{
    FP4OriginalRecord Record;
    FString Error;
    if (!Asset)
    {
        Note = TEXT("does not load");
        return false;
    }
    if (!P4ReadOriginalRecord(Asset, Record, Error))
    {
        Note = TEXT("no original record: ") + Error;
        return false;
    }
    if (Record.ReferenceOnly)
    {
        Note = TEXT("reference only");
        return false;
    }
    Note = TEXT("complete original");
    return true;
}

FString Pick(const FString& Entry, FString& Note)
{
    if (!Entry.StartsWith(TEXT("class:")))
        return Entry;
    const FName Class(*Entry.Mid(6));
    FARFilter Filter;
    Filter.ClassNames.Add(Class);
    Filter.PackagePaths.Add(TEXT("/Game/Carrot4"));
    Filter.bRecursivePaths = true;
    TArray<FAssetData> Found;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Found);
    Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
    int32 Tried = 0, ReferenceOnly = 0, NotEqual = 0;
    for (const FAssetData& Data : Found)
    {
        if (++Tried > 80)
            break;
        FString Why;
        UObject* Asset = LoadAsset(Data.PackageName.ToString());
        if (!CompleteOriginal(Asset, Why))
        {
            if (Why == TEXT("reference only"))
                ++ReferenceOnly;
            continue;
        }
        // An original Kadan can tell is still the game's.
        if (P4DiagnoseOriginalAsset(Asset).Comparison == EP4OriginalComparison::Equal)
            return Data.PackageName.ToString();
        ++NotEqual;
    }
    Note = FString::Printf(
        TEXT(
            "no complete original of class %s that compares equal to the game among %d tried of %d (%d reference only, %d not compared equal)"),
        *Class.ToString(), FMath::Min(Tried, Found.Num()), Found.Num(), ReferenceOnly, NotEqual);
    return FString();
}

// Text with every whole word From (not part of a longer name) read as To.
FString Words(const FString& Text, const FString& From, const FString& To)
{
    FString Out;
    int32 At = 0;
    for (int32 Found; (Found = Text.Find(From, ESearchCase::CaseSensitive, ESearchDir::FromStart, At)) != INDEX_NONE;)
    {
        const int32 End = Found + From.Len();
        const bool Whole = (Found == 0 || !FChar::IsIdentifier(Text[Found - 1])) &&
            (End >= Text.Len() || !FChar::IsIdentifier(Text[End]));
        Out += Text.Mid(At, Found - At) + (Whole ? To : From);
        At = End;
    }
    return Out + Text.Mid(At);
}

// The cook lines of an asset, with a copy's package and asset names read as the original's.
bool CookLines(UObject* Asset, const FString& Rename, const FString& As, TArray<FString>& Out)
{
    const FString State = P4OriginalObjectAssetStateAndLines(Asset, Out);
    if (State.IsEmpty())
        return false;
    if (!Rename.IsEmpty())
        for (FString& Line : Out)
        {
            Line.ReplaceInline(*Rename, *As, ESearchCase::CaseSensitive);
            Line = Words(
                Line, FPackageName::GetLongPackageAssetName(Rename), FPackageName::GetLongPackageAssetName(As));
        }
    Out.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    return true;
}

// Whether the copy holds exactly what the original holds. Differences says the first lines that differ.
bool Exact(UObject* Original, UObject* Copy, FString& Differences)
{
    if (Copy->IsA<UMaterialInstanceConstant>())
    {
        const auto Result = P4CompareCopiedMaterialInstanceContent(Copy);
        Differences = FString::Printf(TEXT("copied-content state %d, original %s, %s"), int32(Result.State),
            *Result.OriginalPackage, *Result.Error);
        return Result.State == EP4CopiedMICContent::Equal &&
            Result.OriginalPackage == Original->GetOutermost()->GetName();
    }
    TArray<FString> Game, Mine;
    if (!CookLines(Original, FString(), FString(), Game))
    {
        Differences = TEXT("the original does not cook");
        return false;
    }
    if (!CookLines(Copy, Copy->GetOutermost()->GetName(), Original->GetOutermost()->GetName(), Mine))
    {
        Differences = TEXT("the copy does not cook");
        return false;
    }
    TArray<FString> Missing, Extra;
    for (const FString& Line : Game)
        if (!Mine.Contains(Line))
            Missing.Add(Line);
    for (const FString& Line : Mine)
        if (!Game.Contains(Line))
            Extra.Add(Line);
    Differences = FString::Printf(
        TEXT("%d lines, %d only in the original, %d only in the copy"), Game.Num(), Missing.Num(), Extra.Num());
    for (int32 Index = 0; Index < FMath::Min(4, Missing.Num()); ++Index)
        Differences += TEXT("\n    original: ") + Missing[Index].Left(400);
    for (int32 Index = 0; Index < FMath::Min(4, Extra.Num()); ++Index)
        Differences += TEXT("\n    copy: ") + Extra[Index].Left(400);
    return Missing.Num() == 0 && Extra.Num() == 0 && Game.Num() == Mine.Num();
}

bool Editable(UObject* Copy)
{
    return P4Kadan::GetAssetPolicy(Copy).AllowEdit && !P4Kadan::IsLockedObject(Copy) && P4IsCreatorAsset(Copy);
}

// An edit made as the Details panel makes it, and whether Kadan's guards leave it in place. The value is put
// back afterwards.
FString EditSticks(UObject* Asset)
{
    FBoolProperty* Chosen = nullptr;
    for (TFieldIterator<FBoolProperty> It(Asset->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Edit) &&
            !It->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_DisableEditOnInstance) && It->ArrayDim == 1)
        {
            Chosen = *It;
            break;
        }
    if (!Chosen)
        return TEXT("no editable bool property to try");
    const auto Set = [&](bool Value)
    {
        Asset->PreEditChange(Chosen);
        Chosen->SetPropertyValue_InContainer(Asset, Value);
        FPropertyChangedEvent Event(Chosen, EPropertyChangeType::ValueSet);
        Asset->PostEditChangeProperty(Event);
    };
    const bool Before = Chosen->GetPropertyValue_InContainer(Asset);
    const bool WasDirty = Asset->GetOutermost()->IsDirty();
    Set(!Before);
    P4Kadan::FlushReadOnlyGuards();
    const bool Kept = Chosen->GetPropertyValue_InContainer(Asset) == !Before;
    Set(Before);
    P4Kadan::FlushReadOnlyGuards();
    Asset->GetOutermost()->SetDirtyFlag(WasDirty);
    return FString::Printf(TEXT("%s %s"), *Chosen->GetName(), Kept ? TEXT("kept") : TEXT("put back"));
}

void Write(const FString& Name, const TArray<FString>& Lines)
{
    IFileManager::Get().MakeDirectory(*Results(), true);
    FFileHelper::SaveStringArrayToFile(Lines, *(Results() / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
} //namespace P4EditableCopyCheck

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanEditableCopyCreate, "P4MapKit.Kadan.EditableCopy.Create",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanEditableCopyCreate::RunTest(const FString&)
{
    using namespace P4EditableCopyCheck;
    const FString Wanted = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_EDITABLE_COPY_PACKAGES"));
    if (Tag().IsEmpty() || Wanted.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Set P4_EDITABLE_COPY_TAG and P4_EDITABLE_COPY_PACKAGES."));
        return true;
    }
    TArray<FString> Entries, Report, Made;
    Wanted.ParseIntoArray(Entries, TEXT(";"));
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    for (const FString& Entry : Entries)
    {
        FString Note;
        const FString Package = Pick(Entry, Note);
        if (Package.IsEmpty())
        {
            AddError(Entry + TEXT(": ") + Note);
            Report.Add(TEXT("FAIL ") + Entry + TEXT(" | ") + Note);
            continue;
        }
        UObject* Original = LoadAsset(Package);
        const bool Complete = CompleteOriginal(Original, Note);
        if (!Original)
        {
            AddError(Package + TEXT(" does not load"));
            Report.Add(TEXT("FAIL ") + Package + TEXT(" | does not load"));
            continue;
        }
        TStrongObjectPtr<UObject> Hold(Original);
        const FP4OriginalDiagnostic Game = P4DiagnoseOriginalAsset(Original);
        const bool Locked = !P4Kadan::GetAssetPolicy(Original).AllowEdit && P4Kadan::IsLockedObject(Original);
        UObject* Copy = Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(Package) +
                FPlatformMisc::GetEnvironmentVariable(TEXT("P4_EDITABLE_COPY_SUFFIX")),
            Folder(), Original);
        if (!Copy)
        {
            AddError(Package + TEXT(": Duplicate made nothing"));
            Report.Add(TEXT("FAIL ") + Package + TEXT(" | Duplicate made nothing"));
            continue;
        }
        P4Kadan::FlushReadOnlyGuards();
        FString Differences, Why;
        const bool IsEditable = Editable(Copy);
        const bool IsExact = Exact(Original, Copy, Differences);
        const bool Saved = P4SaveAssetPackage(Copy, TFunction<bool()>(), Why);
        const bool Passed = Complete && IsEditable && IsExact && Saved;
        const FString Line = FString::Printf(
            TEXT(
                "%s %s | %s | %s | original vs game %d %s | original locked %d | copy editable %d | copy exact %d (%s) | saved %d %s"),
            Passed ? TEXT("PASS") : TEXT("FAIL"), *Package, *Original->GetClass()->GetName(), *Note,
            int32(Game.Comparison), *Game.Error.Left(200), Locked, IsEditable, IsExact, *Differences, Saved, *Why);
        Report.Add(Line);
        if (Passed)
            AddInfo(TEXT("COPYCHECK ") + Line);
        else
            AddError(TEXT("COPYCHECK ") + Line);
        if (Saved)
            Made.Add(Package + TEXT("|") + Copy->GetOutermost()->GetName());
    }
    Write(TEXT("create.txt"), Report);
    Write(TEXT("copies.txt"), Made);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanEditableCopyReopen, "P4MapKit.Kadan.EditableCopy.Reopen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanEditableCopyReopen::RunTest(const FString&)
{
    using namespace P4EditableCopyCheck;
    TArray<FString> Made, Report;
    if (Tag().IsEmpty() || !FFileHelper::LoadFileToStringArray(Made, *(Results() / TEXT("copies.txt"))))
    {
        AddWarning(TEXT("P4MapKit test not run. Run EditableCopy.Create with the same P4_EDITABLE_COPY_TAG first."));
        return true;
    }
    if (Made.Num() == 0)
        AddError(TEXT("Create saved no copies."));
    for (const FString& Row : Made)
    {
        FString Package, CopyPackage;
        if (!Row.Split(TEXT("|"), &Package, &CopyPackage))
            continue;
        UObject* Already = FindObject<UPackage>(nullptr, *CopyPackage);
        UObject* Original = LoadAsset(Package);
        UObject* Copy = LoadAsset(CopyPackage);
        if (!Original || !Copy)
        {
            AddError(Row + TEXT(": does not load"));
            Report.Add(TEXT("FAIL ") + Row + TEXT(" | does not load"));
            continue;
        }
        TStrongObjectPtr<UObject> HoldOriginal(Original), HoldCopy(Copy);
        FString Differences;
        const bool IsEditable = Editable(Copy);
        const bool IsExact = Exact(Original, Copy, Differences);
        const FString Edit = EditSticks(Copy);
        const bool EditKept = Edit.EndsWith(TEXT(" kept")) || Edit.StartsWith(TEXT("no editable"));
        // An edit, even one put back, has Unreal build the asset's derived data again (an animation's
        // compression): told, not required. The copy as saved and opened again is what must be exact.
        FString After;
        const bool StillExact = Exact(Original, Copy, After);
        const bool Passed = !Already && IsEditable && IsExact && EditKept;
        const FString Line = FString::Printf(
            TEXT(
                "%s %s | %s | loaded fresh %d | copy editable %d | copy exact %d (%s) | edit %s | exact after the edit was put back %d (%s)"),
            Passed ? TEXT("PASS") : TEXT("FAIL"), *CopyPackage, *Copy->GetClass()->GetName(), !Already, IsEditable,
            IsExact, *Differences, *Edit, StillExact, *After.Left(300));
        Report.Add(Line);
        if (Passed)
            AddInfo(TEXT("COPYCHECK ") + Line);
        else
            AddError(TEXT("COPYCHECK ") + Line);
    }
    Write(TEXT("reopen.txt"), Report);
    return !HasAnyErrors();
}

// Synthetic copy-history regression: no game data, fake equality receipt, or original-source admission.
// Run these three selectors in three editor processes with the same unique P4_EDITABLE_COPY_TAG.
namespace P4EditableCopyLineage
{
using namespace P4EditableCopyCheck;
FString Root()
{
    return TEXT("/Game/EditableCopyLineage_") + Tag();
}
FString Package(const TCHAR* Name)
{
    return Root() / Name;
}
FString Manifest()
{
    return Results() / TEXT("lineage.json");
}
const TCHAR* const StockKey = TEXT("P4GameStockTags");
const TCHAR* const HistoryKey = TEXT("P4KadanCopiedGameAsset");

bool Ready(FAutomationTestBase& Test)
{
    return Test.TestTrue(TEXT("An explicit unique copy-lineage tag is required"),
        !Tag().IsEmpty() && FPackageName::IsValidLongPackageName(Root()));
}

// A real typed stock-tag record on a concrete native curve, using the strict writer/store/readback
// of the importer. Curves have no intrinsic AssetUserData, so this covers the metadata-only route.
UCurveFloat* SyntheticOriginal(FAutomationTestBase& Test, const TCHAR* Name, bool Value)
{
    if (!Test.TestFalse(TEXT("The synthetic curve class is concrete before construction"),
            UCurveFloat::StaticClass()->HasAnyClassFlags(CLASS_Abstract)))
        return nullptr;
    UPackage* Outer = CreatePackage(*Package(Name));
    UCurveFloat* Asset = NewObject<UCurveFloat>(Outer, Name, RF_Public | RF_Standalone | RF_Transactional);
    P4StockTags::FNode Node;
    Node.Name = {"C4SyntheticCopyFlag", 0};
    Node.Type = {"BoolProperty", 0};
    Node.BoolValue = Value;
    std::vector<std::string> Names;
    P4StockTags::Bytes Bytes;
    P4StockTags::FWriter Writer{Names};
    if (!Test.TestTrue(TEXT("The synthetic tagged boolean writes"), Writer.List(Bytes, {Node})))
        return nullptr;
    P4MapKit::FPackageInventory Inventory;
    Inventory.Names = Names;
    TArray<FString> Kept, Problems;
    const TArray<FP4GameStockTagWanted> Wanted{
        {TEXT("C4SyntheticCopyFlag"), Value ? TEXT("true") : TEXT("false"), EP4GameStockTag::Member}};
    if (!Test.TestTrue(TEXT("The actual stock-tag store accepts the synthetic value"),
            P4StoreGameStockTags(
                Asset, Bytes, Inventory, 0, Bytes.size(), TEXT("CurveFloat"), Wanted, Kept, Problems)) ||
        !Test.TestEqual(TEXT("Exactly one synthetic field is held"), Kept.Num(), 1) ||
        !Test.TestEqual(TEXT("No field failed to store"), Problems.Num(), 0))
        return nullptr;
    P4SealGameStockTags(Outer);
    FP4OriginalRecord Record;
    Record.Package = Outer->GetName();
    Record.Class = Asset->GetClass()->GetPathName();
    Record.Kind = TEXT("synthetic-copy-regression");
    Record.SourceDigestKind = TEXT("synthetic-fixture-only");
    Record.ReadOnly = true;
    P4HashUtf8(TEXT("Synthetic metadata transport, not game evidence"), Record.SourceDigest);
    FString Why;
    if (!Test.TestTrue(TEXT("The synthetic source identity is recorded"), P4MarkOriginalAsset(Asset, Record, Why)))
        return nullptr;
    return Asset;
}

void Retire(UObject* Asset)
{
    if (!Asset)
        return;
    Asset->ClearFlags(RF_Public | RF_Standalone);
    Asset->GetOutermost()->SetDirtyFlag(false);
    Asset->MarkPendingKill();
}

bool Holds(FAutomationTestBase& Test, UObject* Asset, const FString& Expected, const FString& OriginalPackage)
{
    if (!Test.TestNotNull(TEXT("The copy exists"), Asset))
        return false;
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    Test.TestTrue(TEXT("The typed game-only value remains readable"), P4CountGameStockTags(Asset) == 1);
    Test.TestTrue(TEXT("Every stored byte of the game-only value remains exact"),
        P4ExactNativeString(Meta->GetValue(Asset, StockKey), Expected));
    Test.TestTrue(TEXT("The copy retains explicit game-copy history"), P4HasCopiedGameData(Asset));
    Test.TestTrue(TEXT("The copy remains editable"), Editable(Asset));
    TSharedPtr<FJsonObject> History;
    FString Origin;
    const bool Read = FJsonSerializer::Deserialize(
                          TJsonReaderFactory<>::Create(Meta->GetValue(Asset, HistoryKey)), History) &&
        History.IsValid() && History->TryGetStringField(TEXT("origin_package"), Origin) &&
        P4ExactNativeString(Origin, OriginalPackage);
    Test.TestTrue(TEXT("The history still names the initial original, not a new original baseline"), Read);
    FP4OriginalRecord Own;
    FString Why;
    Test.TestFalse(TEXT("A creator copy never becomes an original import"), P4ReadOriginalRecord(Asset, Own, Why));
    return Read && P4ExactNativeString(Meta->GetValue(Asset, StockKey), Expected) && P4CountGameStockTags(Asset) == 1;
}

bool ReadManifest(FAutomationTestBase& Test, FString& Original, FString& First, FString& Second, FString& Expected)
{
    FString Text;
    TSharedPtr<FJsonObject> Json;
    return Test.TestTrue(TEXT("Create wrote the expected lineage fixture manifest"),
        FFileHelper::LoadFileToString(Text, *Manifest()) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid() &&
            Json->TryGetStringField(TEXT("original"), Original) && Original == Package(TEXT("Original")) &&
            Json->TryGetStringField(TEXT("first"), First) && First == Package(TEXT("First")) &&
            Json->TryGetStringField(TEXT("second"), Second) && Second == Package(TEXT("Second")) &&
            Json->TryGetStringField(TEXT("stock_tags"), Expected));
}
} //namespace P4EditableCopyLineage

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanCopyLineageCreate, "P4MapKit.Kadan.EditableCopy.LineageCreate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanCopyLineageCreate::RunTest(const FString&)
{
    using namespace P4EditableCopyLineage;
    if (!Ready(*this))
        return false;
    if (!TestFalse(TEXT("A unique tag never overwrites an existing fixture"),
            FPackageName::DoesPackageExist(Package(TEXT("First"))) || IFileManager::Get().FileExists(*Manifest())))
        return false;
    UCurveFloat* Original = SyntheticOriginal(*this, TEXT("Original"), true);
    if (!Original)
        return false;
    ON_SCOPE_EXIT
    {
        Retire(Original);
    };
    const FString Expected = Original->GetOutermost()->GetMetaData()->GetValue(Original, StockKey);
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UObject* First = Tools.DuplicateAsset(TEXT("First"), Root(), Original);
    if (!Holds(*this, First, Expected, Package(TEXT("Original"))))
        return false;
    UObject* SamePayload = Tools.DuplicateAsset(TEXT("SamePayload"), Root(), First);
    Holds(*this, SamePayload, Expected, Package(TEXT("Original")));
    Retire(SamePayload);
    FString Why;
    if (!TestTrue(TEXT("The first copy saves normally"), P4SaveAssetPackage(First, TFunction<bool()>(), Why)))
        return false;
    IModularFeatures& Features = IModularFeatures::Get();
    if (!TestTrue(TEXT("The targeted completion-failure test seam exists"),
            Features.IsModularFeatureAvailable(FP4KadanCopyTestSeams::FeatureName())))
        return false;
    const auto& Seams = Features.GetModularFeature<FP4KadanCopyTestSeams>(FP4KadanCopyTestSeams::FeatureName());
    if (!TestTrue(TEXT("The failure seam has its one-package setter"), Seams.SetCompletionFailurePackage != nullptr))
        return false;
    Seams.SetCompletionFailurePackage(Package(TEXT("FailedCompletion")));
    ON_SCOPE_EXIT
    {
        Seams.SetCompletionFailurePackage(FString());
    };
    UObject* Failed = Tools.DuplicateAsset(TEXT("FailedCompletion"), Root(), First);
    if (!TestNotNull(TEXT("The controlled completion failure still has a native copy"), Failed))
        return false;
    UMetaData* FailedMeta = Failed->GetOutermost()->GetMetaData();
    TestTrue(TEXT("A failure after installation preserves the complete game-value payload"),
        P4ExactNativeString(FailedMeta->GetValue(Failed, StockKey), Expected) && P4CountGameStockTags(Failed) == 1);
    TestTrue(TEXT("Incomplete copied data is explicitly marked"),
        FailedMeta->HasValue(Failed, TEXT("P4KadanCopiedGameDataFailure")));
    FString FailureReason;
    TestFalse(TEXT("The actual export creator-asset gate refuses incomplete copied data"),
        P4IsCreatorAsset(Failed, &FailureReason));
    TestTrue(TEXT("The refusal tells the creator to make a new copy"), FailureReason.Contains(TEXT("Make a new copy")));
    TSharedPtr<FJsonObject> FailedHistory;
    TestTrue(TEXT("Failure retains history without publishing a validated donor payload seal"),
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(FailedMeta->GetValue(Failed, HistoryKey)), FailedHistory) &&
            FailedHistory.IsValid() && !FailedHistory->HasField(TEXT("carried_data_sha256")));
    TestTrue(TEXT("The failure mark and retained values can be saved for honest later inspection"),
        P4SaveAssetPackage(Failed, TFunction<bool()>(), Why));
    Retire(Failed);
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("original"), Package(TEXT("Original")));
    Json->SetStringField(TEXT("first"), Package(TEXT("First")));
    Json->SetStringField(TEXT("second"), Package(TEXT("Second")));
    Json->SetStringField(TEXT("stock_tags"), Expected);
    FString Text;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    IFileManager::Get().MakeDirectory(*Results(), true);
    TestTrue(TEXT("The bounded synthetic expectation manifest saves"),
        FJsonSerializer::Serialize(Json, Writer) &&
            FFileHelper::SaveStringToFile(Text, *Manifest(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanCopyLineageReopen, "P4MapKit.Kadan.EditableCopy.LineageReopen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanCopyLineageReopen::RunTest(const FString&)
{
    using namespace P4EditableCopyLineage;
    if (!Ready(*this))
        return false;
    FString OriginalPackage, FirstPackage, SecondPackage, Expected;
    if (!ReadManifest(*this, OriginalPackage, FirstPackage, SecondPackage, Expected))
        return false;
    if (!TestNull(TEXT("The original is absent in this new editor process"), FindPackage(nullptr, *OriginalPackage)) ||
        !TestNull(TEXT("The first copy was not already loaded"), FindPackage(nullptr, *FirstPackage)))
        return false;
    UObject* First = LoadAsset(FirstPackage);
    TStrongObjectPtr<UObject> HoldFirst(First);
    if (!Holds(*this, First, Expected, OriginalPackage))
        return false;
    TestNull(TEXT("Loading the first copy did not load its original"), FindPackage(nullptr, *OriginalPackage));
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UObject* Second = Tools.DuplicateAsset(TEXT("Second"), Root(), First);
    if (!Holds(*this, Second, Expected, OriginalPackage))
        return false;
    FString Why;
    TestTrue(TEXT("The second-generation copy saves normally"), P4SaveAssetPackage(Second, TFunction<bool()>(), Why));
    // A sealed source may have creator edits to ordinary reflected properties; what transfers is the
    // current copy's content plus its intact game-only payload, rather than reloading the game original.
    Retire(Second);
    UCurveFloat* Other = SyntheticOriginal(*this, TEXT("OtherOriginal"), false);
    if (!Other)
        return false;
    // Both source candidates have identical reflected content but different hidden boolean values.
    UObject* Ambiguous = Tools.DuplicateAsset(TEXT("Ambiguous"), Root(), First);
    if (TestNotNull(TEXT("Unreal still creates the ambiguous duplicate"), Ambiguous))
    {
        TestFalse(TEXT("Ambiguous hidden values are never selected by donor iteration order"),
            P4ObjectHasGameStockTags(Ambiguous));
        TestFalse(TEXT("Ambiguous duplication does not receive sealed game-copy authority"),
            Ambiguous->GetOutermost()->GetMetaData()->HasValue(Ambiguous, HistoryKey));
    }
    Retire(Ambiguous);
    Retire(Other);
    UMetaData* Meta = First->GetOutermost()->GetMetaData();
    const FString Before = Meta->GetValue(First, StockKey);
    Meta->SetValue(First, StockKey, *(Before + TEXT(" "))); // valid JSON, altered exact payload seal
    UObject* Changed = Tools.DuplicateAsset(TEXT("ChangedSeal"), Root(), First);
    if (TestNotNull(TEXT("Unreal still creates the changed-seal duplicate"), Changed))
    {
        TestFalse(
            TEXT("An altered metadata payload is not copied under stale lineage"), P4ObjectHasGameStockTags(Changed));
        TestFalse(TEXT("An altered payload never gains a fresh lineage seal"),
            Changed->GetOutermost()->GetMetaData()->HasValue(Changed, HistoryKey));
    }
    Meta->SetValue(First, StockKey, *Before);
    First->GetOutermost()->SetDirtyFlag(false);
    Retire(Changed);
    TestNull(
        TEXT("The original was never loaded by second-generation duplication"), FindPackage(nullptr, *OriginalPackage));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanCopyLineageSecondReopen, "P4MapKit.Kadan.EditableCopy.LineageSecondReopen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanCopyLineageSecondReopen::RunTest(const FString&)
{
    using namespace P4EditableCopyLineage;
    if (!Ready(*this))
        return false;
    FString OriginalPackage, FirstPackage, SecondPackage, Expected;
    if (!ReadManifest(*this, OriginalPackage, FirstPackage, SecondPackage, Expected))
        return false;
    TestNull(
        TEXT("The game original is absent before the second copy's cold load"), FindPackage(nullptr, *OriginalPackage));
    TestNull(TEXT("The first copy is absent before the second copy's cold load"), FindPackage(nullptr, *FirstPackage));
    if (!TestNull(TEXT("The second copy loads in a fresh process"), FindPackage(nullptr, *SecondPackage)))
        return false;
    UObject* Second = LoadAsset(SecondPackage);
    TStrongObjectPtr<UObject> HoldSecond(Second);
    Holds(*this, Second, Expected, OriginalPackage);
    TestNull(TEXT("The second copy has no dependency on its original"), FindPackage(nullptr, *OriginalPackage));
    TestNull(TEXT("The second copy has no dependency on its first copy"), FindPackage(nullptr, *FirstPackage));
    UObject* Failed = LoadAsset(Package(TEXT("FailedCompletion")));
    TStrongObjectPtr<UObject> HoldFailed(Failed);
    if (TestNotNull(TEXT("The controlled failed copy also survives a cold save/load"), Failed))
    {
        TestTrue(TEXT("Cold failure keeps the typed game-only field"), P4CountGameStockTags(Failed) == 1);
        FString Why;
        TestFalse(
            TEXT("Cold failure remains refused by the actual export creator gate"), P4IsCreatorAsset(Failed, &Why));
        TestTrue(TEXT("The cold refusal still explains recovery"), Why.Contains(TEXT("Make a new copy")));
    }
    return !HasAnyErrors();
}
#endif

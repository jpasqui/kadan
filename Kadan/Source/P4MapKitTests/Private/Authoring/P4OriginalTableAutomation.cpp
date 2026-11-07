#include "P4AuthoringRows.h"
#include "P4LevelContent.h"
#include "P4ExportDriver.h"
#include "P4KadanCreatorReview.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "Async/Async.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS)
// The game's tables through the importer every other kind goes through: made at the game's path, saved the
// way a cook saves them, compared with the game's file row by row, and known afterwards as untouched or
// edited. The files are the user's own, found beside the native work folder.
namespace P4OriginalTableTests
{
TSharedPtr<FJsonObject> Parse(const FString& Text)
{
    TSharedPtr<FJsonObject> Json;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
    return Json;
}
bool Flag(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
{
    bool Value = false;
    return Json.IsValid() && Json->TryGetBoolField(Name, Value) && Value;
}
FString Word(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
{
    FString Value;
    if (Json.IsValid())
        Json->TryGetStringField(Name, Value);
    return Value;
}
int32 Count(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
{
    double Value = -1;
    if (Json.IsValid())
        Json->TryGetNumberField(Name, Value);
    return int32(Value);
}

// One of the game's tables: where its file is, and where it goes in Content.
struct FTable
{
    FString Package, Asset, Header, File;
    explicit FTable(const TCHAR* GamePath)
    {
        Package = FString(TEXT("/Game/")) + GamePath;
        Asset = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
        Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")) /
                TEXT("../modding-research/tables/dts/Carrot4/Content") / GamePath +
            TEXT(".uasset");
        FPackageName::TryConvertLongPackageNameToFilename(Package, File, TEXT(".uasset"));
        File = FPaths::ConvertRelativePathToFull(File);
    }
    bool NothingThere() const { return !IFileManager::Get().FileExists(*File) && Find() == nullptr; }
    UDataTable* Find() const { return FindObject<UDataTable>(nullptr, *Asset); }
    TSharedPtr<FJsonObject> Import(bool Save) const
    {
        return Parse(UP4AuthoringTools::NativeImportOriginalAsset(Header, FString(), Save));
    }
    TSharedPtr<FJsonObject> Status() const { return Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(Find())); }
    void Remove() const
    {
        if (UPackage* Made = FindPackage(nullptr, *Package))
        {
            if (UDataTable* Table = Find())
            {
                FAssetRegistryModule::AssetDeleted(Table);
                Table->ClearFlags(RF_Public | RF_Standalone);
                Table->MarkPendingKill();
            }
            ResetLoaders(Made);
            Made->SetDirtyFlag(false);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        IFileManager::Get().Delete(*File, false, true, true);
        for (FString Folder = FPaths::GetPath(File);
            FPaths::IsUnderDirectory(Folder, Content) && !FPaths::IsSamePath(Folder, Content);
            Folder = FPaths::GetPath(Folder))
            if (!IFileManager::Get().DeleteDirectory(*Folder, false, false))
                break;
    }
};
uint8* Row(const UDataTable* Table, const TCHAR* Name)
{
    uint8* const* Found = Table->GetRowMap().Find(FName(Name));
    return Found ? *Found : nullptr;
}
// Takes two rows out and puts them back in each other's places. The table gives a freed place to the
// next row added, the last freed first.
void TakeOutAndPutBack(UDataTable* Table, const TCHAR* First, const TCHAR* Second)
{
    FStructOnScope A(Table->GetRowStruct()), B(Table->GetRowStruct());
    Table->GetRowStruct()->CopyScriptStruct(A.GetStructMemory(), Row(Table, First));
    Table->GetRowStruct()->CopyScriptStruct(B.GetStructMemory(), Row(Table, Second));
    Table->RemoveRow(FName(First));
    Table->RemoveRow(FName(Second));
    Table->AddRow(FName(First), *reinterpret_cast<const FTableRowBase*>(A.GetStructMemory()));
    Table->AddRow(FName(Second), *reinterpret_cast<const FTableRowBase*>(B.GetStructMemory()));
}
} //namespace P4OriginalTableTests

// The treasure table: 284 rows of numbers and names.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalTableKeptTest, "P4MapKit.Quality.OriginalTableKeptInContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalTableKeptTest::RunTest(const FString&)
{
    using namespace P4OriginalTableTests;
    const FTable Game(TEXT("Carrot4/Core/GActor/DT_OtakaraParameter"));
    if (!TestTrue(
            TEXT("the game's table file is where the test looks"), IFileManager::Get().FileExists(*Game.Header)) ||
        !TestTrue(TEXT("nothing is at the test's place before it"), Game.NothingThere()))
        return false;
    ON_SCOPE_EXIT
    {
        Game.Remove();
    };

    const auto Missing = Parse(
        UP4AuthoringTools::NativeImportOriginalAsset(Game.Header + TEXT(".none"), FString(), true));
    TestTrue(TEXT("a file that is not there is refused"),
        !Flag(Missing, TEXT("valid")) && Word(Missing, TEXT("error")) == TEXT("asset-import-source-unreadable"));

    // Something else, a virus scanner say, holds the file for a moment: the import waits for it.
    TUniquePtr<FArchive> Held(IFileManager::Get().CreateFileWriter(*Game.Header, FILEWRITE_Append));
    TArray<uint8> Unread;
    TestTrue(TEXT("while the file is held it cannot be read"),
        Held.IsValid() && !FFileHelper::LoadFileToArray(Unread, *Game.Header, FILEREAD_Silent));
    TFuture<void> Released = Async(EAsyncExecution::Thread, [&Held]()
    {
        FPlatformProcess::Sleep(0.3f);
        Held.Reset();
    });
    const auto First = Game.Import(true);
    Released.Wait();
    if (!TestTrue(TEXT("the table is imported"), Flag(First, TEXT("valid"))))
    {
        AddError(Word(First, TEXT("error")));
        return false;
    }
    TestTrue(TEXT("it was made and saved"), Flag(First, TEXT("created")) && Flag(First, TEXT("saved")));
    TestTrue(TEXT("every row was made, and every row and member was compared with the game's file"),
        Count(First, TEXT("rows")) == 284 && Count(First, TEXT("compared")) > 284 * 8);
    TestTrue(TEXT("it is not kept for reference only"),
        !Flag(First, TEXT("reference_only")) && Count(First, TEXT("native_bytes_not_held")) == 0);
    const FString State = Word(First, TEXT("state"));
    TestTrue(TEXT("its state is a digest, and is what was recorded"),
        State.Len() == 64 && Word(First, TEXT("imported_state")) == State);
    UDataTable* Table = Game.Find();
    if (!TestTrue(
            TEXT("an ordinary table is at the game's path"), Table != nullptr && Table->GetRowStruct() != nullptr))
        return false;
    TestTrue(TEXT("its rows are of the game's type"),
        Table->GetRowStruct()->GetPathName() == TEXT("/Script/Carrot4.OtakaraParameterTableRow"));
    const TArray<FName> Names = Table->GetRowNames();
    if (!TestTrue(TEXT("it has the game's rows"), Names.Num() == 284))
        return false;
    TestTrue(TEXT("the rows are in the game's order"),
        Names[0] == TEXT("OTADARTS") && Names[1] == TEXT("OTABRUSHA") && Names[2] == TEXT("OTAPUZZLEA") &&
            Names[282] == TEXT("OTAMATDOLLMUNI") && Names[283] == TEXT("OTAMATDOLLLUNI"));
    const FNumericProperty* Weight = FindFProperty<FNumericProperty>(Table->GetRowStruct(), TEXT("CarryWeightMin"));
    const FNumericProperty* Kira = FindFProperty<FNumericProperty>(Table->GetRowStruct(), TEXT("Kira"));
    uint8* Darts = Row(Table, TEXT("OTADARTS"));
    if (!TestTrue(
            TEXT("the first row and its members are there"), Weight != nullptr && Kira != nullptr && Darts != nullptr))
        return false;
    void* WeightAt = Weight->ContainerPtrToValuePtr<void>(Darts);
    TestTrue(TEXT("a row has the game's values"),
        Weight->GetSignedIntPropertyValue(WeightAt) == 7 &&
            Kira->GetSignedIntPropertyValue(Kira->ContainerPtrToValuePtr<void>(Darts)) == 60);
    TestTrue(TEXT("the file is there and the package is not left dirty"),
        IFileManager::Get().FileExists(*Game.File) && !Table->GetOutermost()->IsDirty());
    TestTrue(TEXT("its state after the save"), P4OriginalObjectAssetState(Table) == State);
    auto Status = Game.Status();
    TestTrue(TEXT("it is known as imported from the game, and as untouched"),
        Flag(Status, TEXT("valid")) && Flag(Status, TEXT("original")) && Flag(Status, TEXT("unchanged")));

    const auto Again = Game.Import(true);
    TestTrue(TEXT("a second import of the same file changes nothing"),
        Flag(Again, TEXT("valid")) && !Flag(Again, TEXT("created")) && !Flag(Again, TEXT("saved")) &&
            Game.Find() == Table);

    // What is compared is every row's every value, its name and its place.
    Weight->SetIntPropertyValue(WeightAt, int64(8));
    Table->MarkPackageDirty(); // Ordinary editor edits notify the package; the explicit diagnostic also covers low-level writes.
    TestTrue(TEXT("a changed value of a row is seen"), !Flag(Game.Status(), TEXT("unchanged")));
    const auto Refused = Game.Import(true);
    TestTrue(TEXT("an edited table is not imported over"),
        !Flag(Refused, TEXT("valid")) &&
            Word(Refused, TEXT("error")) == TEXT("asset-import-would-replace-an-edited-asset"));
    Weight->SetIntPropertyValue(WeightAt, int64(7));
    TestTrue(TEXT("the value put back makes it untouched again"), Flag(Game.Status(), TEXT("unchanged")));

    TakeOutAndPutBack(Table, TEXT("OTADARTS"), TEXT("OTABRUSHA"));
    TArray<FName> Order = Table->GetRowNames();
    if (TestTrue(TEXT("two rows changed places"),
            Order.Num() == 284 && Order[0] == TEXT("OTABRUSHA") && Order[1] == TEXT("OTADARTS")))
    {
        TestTrue(TEXT("rows in another order are seen"), !Flag(Game.Status(), TEXT("unchanged")));
        TakeOutAndPutBack(Table, TEXT("OTABRUSHA"), TEXT("OTADARTS"));
        Order = Table->GetRowNames();
        TestTrue(
            TEXT("the rows are back in their places"), Order[0] == TEXT("OTADARTS") && Order[1] == TEXT("OTABRUSHA"));
        TestTrue(TEXT("the order put back makes it untouched again"), Flag(Game.Status(), TEXT("unchanged")));
    }

    {
        FStructOnScope Copy(Table->GetRowStruct());
        Table->GetRowStruct()->CopyScriptStruct(Copy.GetStructMemory(), Row(Table, TEXT("OTAMATDOLLLUNI")));
        Table->AddRow(TEXT("P4TestRow"), *reinterpret_cast<const FTableRowBase*>(Copy.GetStructMemory()));
        TestTrue(TEXT("an added row is seen"), !Flag(Game.Status(), TEXT("unchanged")));
        Table->RemoveRow(TEXT("P4TestRow"));
        TestTrue(TEXT("the row taken out again makes it untouched again"), Flag(Game.Status(), TEXT("unchanged")));
        Table->RemoveRow(TEXT("OTAMATDOLLLUNI"));
        TestTrue(TEXT("a row taken out is seen"), !Flag(Game.Status(), TEXT("unchanged")));
    }
    Game.Remove();
    TestTrue(TEXT("the test leaves nothing"), Game.NothingThere());
    return !HasAnyErrors();
}

// A table of localized text. Text is what the game finds it by: a namespace, a key and the source.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalTableTextTest, "P4MapKit.Quality.OriginalTableLocalizedText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalTableTextTest::RunTest(const FString&)
{
    using namespace P4OriginalTableTests;
    const FTable Game(TEXT("Carrot4/Core/UI/DT_UIHowToPlayOsusowakeTextLabel"));
    if (!TestTrue(
            TEXT("the game's table file is where the test looks"), IFileManager::Get().FileExists(*Game.Header)) ||
        !TestTrue(TEXT("nothing is at the test's place before it"), Game.NothingThere()))
        return false;
    ON_SCOPE_EXIT
    {
        Game.Remove();
    };
    const auto First = Game.Import(true);
    if (!TestTrue(TEXT("the table is imported"), Flag(First, TEXT("valid"))))
    {
        AddError(Word(First, TEXT("error")));
        return false;
    }
    TestTrue(TEXT("it was made and saved, with its four rows"),
        Flag(First, TEXT("created")) && Flag(First, TEXT("saved")) && Count(First, TEXT("rows")) == 4);
    UDataTable* Table = Game.Find();
    const FTextProperty* Label = Table ? FindFProperty<FTextProperty>(Table->GetRowStruct(), TEXT("label")) : nullptr;
    uint8* One = Table ? Row(Table, TEXT("NewRow")) : nullptr;
    if (!TestTrue(TEXT("the table, its text member and its first row are there"), Label != nullptr && One != nullptr))
        return false;
    const FText Text = Label->GetPropertyValue_InContainer(One);
    const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text), Key = FTextInspector::GetKey(Text);
    const FString* Source = FTextInspector::GetSourceString(Text);
    TestTrue(TEXT("the text has a key and a source, and a namespace that is not this package's"),
        Key.IsSet() && !Key.GetValue().IsEmpty() && Source != nullptr && !Source->IsEmpty() && Namespace.IsSet() &&
            TextNamespaceUtil::ExtractPackageNamespace(Namespace.GetValue()).IsEmpty());
    TestTrue(TEXT("it is untouched after the save, which keeps the key"), Flag(Game.Status(), TEXT("unchanged")));
    Label->SetPropertyValue_InContainer(
        One, FText::ChangeKey(FTextKey(Namespace.Get(FString())), FTextKey(TEXT("P4AnotherKey")), Text));
    TestTrue(TEXT("the same words under another key are seen"), !Flag(Game.Status(), TEXT("unchanged")));
    Label->SetPropertyValue_InContainer(One,
        FText::ChangeKey(
            FTextKey(Namespace.Get(FString())), FTextKey(Key.Get(FString())), FText::FromString(*Source + TEXT("!"))));
    TestTrue(TEXT("other words under the same key are seen"), !Flag(Game.Status(), TEXT("unchanged")));
    Label->SetPropertyValue_InContainer(One, Text);
    TestTrue(TEXT("the text put back makes it untouched again"), Flag(Game.Status(), TEXT("unchanged")));
    Game.Remove();
    TestTrue(TEXT("the test leaves nothing"), Game.NothingThere());
    return !HasAnyErrors();
}

// A kind of member a row can have: a map whose values are structs with a fixed list in them. Made,
// compared and thrown away without a save.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalTableMembersTest, "P4MapKit.Quality.OriginalTableMapsAndFixedLists",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalTableMembersTest::RunTest(const FString&)
{
    using namespace P4OriginalTableTests;
    const FTable Game(TEXT("Carrot4/Core/PresetPikmin/DT_PresetPikmin"));
    if (!TestTrue(
            TEXT("the game's table file is where the test looks"), IFileManager::Get().FileExists(*Game.Header)) ||
        !TestTrue(TEXT("nothing is at the test's place before it"), Game.NothingThere()))
        return false;
    ON_SCOPE_EXIT
    {
        Game.Remove();
    };
    const auto First = Game.Import(false);
    if (!TestTrue(TEXT("the table is imported"), Flag(First, TEXT("valid"))))
    {
        AddError(Word(First, TEXT("error")));
        return false;
    }
    TestTrue(TEXT("with its 53 rows, and not saved"),
        Count(First, TEXT("rows")) == 53 && Flag(First, TEXT("created")) && !Flag(First, TEXT("saved")) &&
            !IFileManager::Get().FileExists(*Game.File));
    UDataTable* Table = Game.Find();
    const FMapProperty* Follow = Table ? FindFProperty<FMapProperty>(Table->GetRowStruct(), TEXT("FollowPikminNum1p"))
                                       : nullptr;
    uint8* One = Table ? Row(Table, TEXT("Area001_0")) : nullptr;
    if (!TestTrue(TEXT("the map member and the first row are there"), Follow != nullptr && One != nullptr))
        return false;
    FScriptMapHelper Pairs(Follow, Follow->ContainerPtrToValuePtr<void>(One));
    const FStructProperty* Leaves = CastField<FStructProperty>(Follow->ValueProp);
    const FNumericProperty* Number = Leaves ? FindFProperty<FNumericProperty>(Leaves->Struct, TEXT("Num")) : nullptr;
    if (!TestTrue(TEXT("the map has the game's entries, each a struct with a fixed list of three"),
            Pairs.Num() >= 8 && Number != nullptr && Number->ArrayDim == 3))
        return false;
    TestTrue(TEXT("it is untouched"), Flag(Game.Status(), TEXT("unchanged")));
    void* Last = Number->ContainerPtrToValuePtr<void>(Pairs.GetValuePtr(0), 2);
    const int64 Was = Number->GetSignedIntPropertyValue(Last);
    Number->SetIntPropertyValue(Last, Was + 1);
    TestTrue(
        TEXT("a changed last place of a fixed list inside a map is seen"), !Flag(Game.Status(), TEXT("unchanged")));
    Number->SetIntPropertyValue(Last, Was);
    TestTrue(TEXT("the value put back makes it untouched again"), Flag(Game.Status(), TEXT("unchanged")));
    Game.Remove();
    TestTrue(TEXT("the test leaves nothing"), Game.NothingThere());
    return !HasAnyErrors();
}
// The normal exporter writes a table-only mod without requiring an open map or cooking game data.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalTableManifestExport, "P4MapKit.Export.OriginalTableManifest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalTableManifestExport::RunTest(const FString&)
{
    using namespace P4OriginalTableTests;
    const FTable Game(TEXT("Carrot4/Core/GActor/DT_OtakaraParameter"));
    if (!TestTrue(TEXT("table fixture path is unused"), Game.NothingThere()))
        return false;
    const FTable Teki(TEXT("Carrot4/Core/GActor/DT_TekiParameter"));
    const FTable Pattern(TEXT("Carrot4/Characters/Teki/DiscoDamagumo/DT_DiscoDamagumo_StepPattern"));
    if (!TestTrue(TEXT("array and added-row fixture paths are unused"), Teki.NothingThere() && Pattern.NothingThere()))
        return false;
    ON_SCOPE_EXIT
    {
        Pattern.Remove();
        Teki.Remove();
        Game.Remove();
    };
    const auto Imported = Game.Import(false);
    if (!TestTrue(TEXT("stock table imports for the normal export path"), Flag(Imported, TEXT("valid"))))
    {
        AddError(Word(Imported, TEXT("error")));
        return false;
    }
    UDataTable* Table = Game.Find();
    FNumericProperty* Kira = Table ? FindFProperty<FNumericProperty>(Table->GetRowStruct(), TEXT("Kira")) : nullptr;
    uint8* Darts = Table ? Row(Table, TEXT("OTADARTS")) : nullptr;
    if (!Kira || !Darts)
        return false;
    Kira->SetIntPropertyValue(Kira->ContainerPtrToValuePtr<void>(Darts), int64(75));
    Table->MarkPackageDirty();
    for (const FTable* Fixture : {&Teki, &Pattern})
    {
        const auto Loaded = Fixture->Import(false);
        if (!TestTrue(TEXT("stock array and added-row tables import"), Flag(Loaded, TEXT("valid"))))
        {
            AddError(Word(Loaded, TEXT("error")));
            return false;
        }
    }
    UDataTable* TekiTable = Teki.Find();
    UDataTable* PatternTable = Pattern.Find();
    if (!TekiTable || !PatternTable || !TekiTable->GetRowNames().Num() || !PatternTable->GetRowNames().Num())
        return false;
    const FString TekiRow = TekiTable->GetRowNames()[0].ToString(),
                  PatternDonor = PatternTable->GetRowNames()[0].ToString();
    const FArrayProperty* Distances = FindFProperty<FArrayProperty>(
        TekiTable->GetRowStruct(), TEXT("URODistanceThesholds"));
    const FNumericProperty* Distance = Distances ? CastField<FNumericProperty>(Distances->Inner) : nullptr;
    if (!Distances || !Distance || !Distance->IsFloatingPoint())
        return false;
    FScriptArrayHelper Array(Distances, Distances->ContainerPtrToValuePtr<void>(Row(TekiTable, *TekiRow)));
    const int32 AddedAt = Array.AddValue();
    Distance->SetFloatingPointPropertyValue(Array.GetRawPtr(AddedAt), 1234.0);
    TekiTable->MarkPackageDirty();
    const FString NewRow = TEXT("native.table.test:walk");
    FStructOnScope Copy(PatternTable->GetRowStruct());
    PatternTable->GetRowStruct()->CopyScriptStruct(Copy.GetStructMemory(), Row(PatternTable, *PatternDonor));
    PatternTable->AddRow(FName(*NewRow), *reinterpret_cast<const FTableRowBase*>(Copy.GetStructMemory()));
    PatternTable->MarkPackageDirty();
    const FString Scratch = FPaths::ProjectSavedDir() / TEXT("P4MapKit/TableManifestExport");
    if (!IFileManager::Get().DeleteDirectory(*Scratch, false, true))
        return false;
    FP4ExportModRequest Request;
    Request.Mod.Id = TEXT("native.table.test");
    Request.Mod.Name = TEXT("Native Table Test");
    Request.Mod.Version = TEXT("1.0.0");
    Request.ProjectDirectory = Scratch / TEXT("project");
    Request.OutputDirectory = Scratch / TEXT("out");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit/TableManifestExport/work");
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(NativeRoot));
    Request.GamePaksDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Request.Tools.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Request.Tools.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Request.Tools.Retoc.IsEmpty())
        Request.Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Request.Tools.Repak.IsEmpty())
        Request.Tools.Repak = Repo / TEXT("private/repak/repak.exe");
    Request.Roots.Add({EP4ExportRootKind::Package, Game.Package});
    Request.Roots.Add({EP4ExportRootKind::Package, Teki.Package});
    Request.Roots.Add({EP4ExportRootKind::Package, Pattern.Package});
    FString Error;
    if (!P4WriteModDescriptor(Request.ProjectDirectory, Request.Mod, nullptr, Error))
    {
        AddError(Error);
        return false;
    }
    FP4ExportGameInventory Inventory;
    FP4ExportSelectionPreview Preview;
    if (!P4ReadExportGameInventory(Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error) ||
        !P4PreviewExportSelection(Request.Mod, Request.Roots, Inventory, {}, Preview, Error))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("table is kept outside level edits"), Preview.GameEditCandidates.Num(), 0);
    TestEqual(TEXT("all table manifest routes are selected"), Preview.TableEditCandidates.Num(), 3);
    TestTrue(TEXT("panel permits the table-only selection without a map"),
        P4ReviewCreatorSelection(Request.Mod, Preview, nullptr).CanExport());
    FP4ExportModResult Exported;
    if (!TestTrue(TEXT("normal exporter publishes table-only mod"), P4ExportMod(Request, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("no game package is cooked or shipped"), Exported.Packages.Num(), 0);
    TestEqual(TEXT("only the manifest is shipped"), Exported.Files.Num(), 1);
    FString Text;
    TestTrue(TEXT("published manifest can be opened"),
        FFileHelper::LoadFileToString(
            Text, *(Exported.Directory / TEXT("romfs/tsuchi/mods/native.table.test/mod.json"))));
    auto Manifest = Parse(Text);
    if (!Manifest || !Manifest->HasTypedField<EJson::Object>(TEXT("tables")))
        return false;
    auto Changes = Manifest->GetObjectField(TEXT("tables"))->GetObjectField(Game.Asset);
    TestEqual(TEXT("unchanged stock rows are absent"), Changes->Values.Num(), 1);
    TestEqual(TEXT("only creator-set value is stored"),
        Changes->GetObjectField(TEXT("OTADARTS"))->GetNumberField(TEXT("Kira")), 75.0);
    auto TekiChanges = Manifest->GetObjectField(TEXT("tables"))->GetObjectField(Teki.Asset);
    TestEqual(TEXT("only the changed creature row is stored"), TekiChanges->Values.Num(), 1);
    auto DistanceChanges = TekiChanges->GetObjectField(TekiRow);
    TestEqual(TEXT("the array length is published"),
        DistanceChanges->GetObjectField(TEXT("URODistanceThesholds"))->GetNumberField(TEXT("length")),
        double(Array.Num()));
    TestEqual(TEXT("the new element is published separately from length"),
        DistanceChanges->GetNumberField(FString::Printf(TEXT("URODistanceThesholds[%d]"), AddedAt)), 1234.0);
    auto PatternChanges = Manifest->GetObjectField(TEXT("tables"))->GetObjectField(Pattern.Asset);
    TestEqual(TEXT("only the creator-owned added row is stored"), PatternChanges->Values.Num(), 1);
    TestEqual(TEXT("the new row names its stock donor"),
        PatternChanges->GetObjectField(NewRow)->GetStringField(TEXT("from")), PatternDonor);
    TestEqual(TEXT("unchanged new-row values are not copied"), PatternChanges->GetObjectField(NewRow)->Values.Num(), 1);
    return !HasAnyErrors();
}
#endif

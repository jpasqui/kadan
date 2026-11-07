#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "P4MapKitCore/quality.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Async/Async.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"
#include "P4NativeHash.h"
#include "Misc/PackageName.h"
#include "AssetRegistryModule.h"
#include "HAL/ThreadSafeBool.h"
#include "P4OriginalDecode.h"
#include "HAL/ThreadSafeCounter.h"
#include "P4NativeValueStaging.h"
#include "HAL/PlatformFileManager.h"
#include "P4MapKitCore/bulk_data.h"
#include "Serialization/BulkData.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "P4MapKitCore/texture_payload.h"
#include "Engine/Texture2D.h"
#include "P4TextureAuthoring.h"
#include "P4TextureProperties.h"
#include "P4StaticMeshStaging.h"
#include "P4TextureContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS)
namespace
{
int32 P4CallbackRows = 0, P4PostImportCalls = 0, P4ChangedCalls = 0;
bool P4CallbackIncomplete = false, P4CallbackRefuse = false;
// Test-only native constructor/vtable, without adding a reflected test class
// to the plugin's delivery schema. UE calls these actual RowBase overrides.
struct FP4InventoryCallbackRow : FTableRowBase
{
    virtual void OnPostDataImport(const UDataTable* Table, FName, TArray<FString>& Problems) override
    {
        ++P4PostImportCalls;
        if (Table->GetRowMap().Num() != P4CallbackRows || !Table->GetRowMap().Contains(TEXT("Last")))
        {
            P4CallbackIncomplete = true;
            Problems.Add(TEXT("native callback saw incomplete row inventory"));
        }
        if (P4CallbackRefuse)
            Problems.Add(TEXT("native callback explicitly refused complete inventory"));
    }
    virtual void OnDataTableChanged(const UDataTable* Table, FName) override
    {
        ++P4ChangedCalls;
        if (Table->GetRowMap().Num() != P4CallbackRows)
            P4CallbackIncomplete = true;
    }
};
UDataTable* P4JobTestTable(const TCHAR* Package)
{
    UPackage* Outer = CreatePackage(Package);
    UDataTable* Table = NewObject<UDataTable>(Outer, NAME_None, RF_Transactional);
    Table->RowStruct = FTableRowBase::StaticStruct();
    Table->AddRow(TEXT("Before"), FTableRowBase());
    Outer->SetDirtyFlag(false);
    return Table;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeEvidenceHashTest, "P4MapKit.Quality.NativeEvidenceHash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeEvidenceHashTest::RunTest(const FString&)
{
    FString Digest;
    TestTrue(TEXT("empty UTF-8 hash"), P4HashUtf8(TEXT(""), Digest));
    TestEqual(TEXT("independent SHA-256 empty vector"), Digest,
        FString(TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")));
    TestTrue(TEXT("ASCII UTF-8 hash"), P4HashUtf8(TEXT("abc"), Digest));
    TestEqual(TEXT("independent SHA-256 abc vector"), Digest,
        FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
    const uint8 Header[] = {'a'}, Body[] = {'b', 'c'};
    TestTrue(TEXT("source digest binds both exact decoder buffers"), P4HashSourceBuffers(Header, 1, Body, 2, Digest));
    TestEqual(TEXT("header/body ordering equals abc vector"), Digest,
        FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
    TestFalse(TEXT("null nonempty source buffer refuses"), P4HashSourceBuffers(nullptr, 1, Body, 2, Digest));
    TestFalse(TEXT("negative source length refuses"), P4HashSourceBuffers(Header, -1, Body, 2, Digest));
    TestFalse(TEXT("oversized source buffer refuses before access"),
        P4HashSourceBuffers(Header, 64 * 1024 * 1024 + 1, Body, 2, Digest));
    FString Unicode;
    Unicode += TCHAR(0x65e5);
    Unicode += TCHAR(0x672c);
    Unicode += TCHAR(0x8a9e);
    Unicode += TEXT(" ");
    Unicode += TCHAR(0xd83e);
    Unicode += TCHAR(0xdd55);
    Unicode += TEXT(" \"quoted\"\nline\tend");
    TestTrue(TEXT("Unicode, surrogate pair and controls use exact UTF-8"), P4HashUtf8(Unicode, Digest));
    TestEqual(TEXT("independent Unicode UTF-8 digest"), Digest,
        FString(TEXT("cb60a28bb9eb3a286fec2e95fea61a7f738210dc24a98e794f826d8bf8a36b0f")));
    TestTrue(TEXT("chunk boundary hash"), P4HashUtf8(FString::ChrN(65537, TEXT('x')), Digest));
    TestEqual(TEXT("independent chunk boundary digest"), Digest,
        FString(TEXT("1abe08ebecf1c18cab71f6fe28aaddf20268f85bad78bb9a72f88ca47c874662")));
    FString EmbeddedNul = TEXT("ab");
    EmbeddedNul[0] = TCHAR(0);
    TestFalse(TEXT("embedded NUL cannot truncate hash input"), P4HashUtf8(EmbeddedNul, Digest));
    TestTrue(TEXT("failed NUL hash clears prior digest"), Digest.IsEmpty());
    for (const TCHAR Character : {TCHAR(0xd800), TCHAR(0xdc00)})
    {
        FString Invalid;
        Invalid.AppendChar(Character);
        TestFalse(TEXT("invalid Unicode cannot collide via replacement or truncation"), P4HashUtf8(Invalid, Digest));
        TestTrue(TEXT("failed hash clears prior digest"), Digest.IsEmpty());
    }
    FString BadPair;
    BadPair.AppendChar(TCHAR(0xd800));
    BadPair += TEXT("a");
    TestFalse(TEXT("high surrogate requires low surrogate"), P4HashUtf8(BadPair, Digest));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeAssetValuesTest, "P4MapKit.Quality.NativeAssetValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeAssetValuesTest::RunTest(const FString&)
{
    UP4AuthoringTools::EnableOriginalAssetDetails();
    auto ColorClass = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.OnyonCombineEftColorData"));
    TStrongObjectPtr<UObject> Color(UP4AuthoringTools::NewNativeAsset(ColorClass));
    if (!TestTrue(TEXT("actual recovered color asset class"), Color.IsValid()))
        return false;
    const FString Source = TEXT("{\"Red\":{\"R\":0.125,\"G\":-0.0,\"B\":0.75,\"A\":0.5}}");
    TestTrue(TEXT("typed color write"), UP4AuthoringTools::NativeApplyAssetValues(Color.Get(), Source));
    TestTrue(TEXT("independent exact color readback"),
        UP4AuthoringTools::NativeAssetReadback(Color.Get(), Source).Contains(TEXT("\"valid\":true")));
    TestTrue(TEXT("signed zero survives"),
        UP4AuthoringTools::NativeAssetValuesJson(Color.Get()).Contains(TEXT("\"G\":-0.0")));
    const FString Held = UP4AuthoringTools::NativeAssetValuesJson(Color.Get());
    const bool WasDirty = Color->GetOutermost()->IsDirty();
    for (const FString& Invalid : {FString(TEXT("{\"Red\":{\"R\":1.0,\"Unknown\":2}}")),
             FString(TEXT("{\"Red\":{\"R\":true}}")), FString(TEXT("{\"Red\":{\"R\":1.0,\"r\":2.0}}")),
             FString(TEXT("{\"Red\":{\"R\":1.0},\"Red\":{\"R\":2.0}}")), FString(TEXT("{\"Red\":{\"R\":1e100}}"))})
    {
        TestFalse(TEXT("invalid asset data refuses: ") + Invalid,
            UP4AuthoringTools::NativeApplyAssetValues(Color.Get(), Invalid));
        TestEqual(
            TEXT("refusal retains every native value"), UP4AuthoringTools::NativeAssetValuesJson(Color.Get()), Held);
        TestEqual(TEXT("refusal retains dirty state"), Color->GetOutermost()->IsDirty(), WasDirty);
    }
    TestTrue(TEXT("changed value detected without mutation"),
        UP4AuthoringTools::NativeAssetReadback(Color.Get(), TEXT("{\"Red\":{\"R\":0.25}}"))
            .Contains(TEXT("\"valid\":false")));
    auto Worker = Async(EAsyncExecution::ThreadPool,
        [Target = Color.Get(), Source]() { return UP4AuthoringTools::NativeApplyAssetValues(Target, Source); });
    TestFalse(TEXT("off-thread write refuses"), Worker.Get());
    TestFalse(TEXT("class default mutation refuses"),
        UP4AuthoringTools::NativeApplyAssetValues(ColorClass->GetDefaultObject(), Source));
    TestFalse(TEXT("null asset refuses"), UP4AuthoringTools::NativeApplyAssetValues(nullptr, Source));
    auto CountClass = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.ActorPlacedNumAsset"));
    TStrongObjectPtr<UObject> Counts(UP4AuthoringTools::NewNativeAsset(CountClass));
    if (!TestTrue(TEXT("actual recovered count asset class"), Counts.IsValid()))
        return false;
    const FString CountSource = TEXT(
        "{\"ActorPlacedNumInfo\":{\"AssetVersion\":18446744073709551615,\"ExploreRateActorNum\":[[\"Otakara\",14],[\"Survivor\",0]]}}");
    TestTrue(TEXT("nested enum map and exact uint64 write"),
        UP4AuthoringTools::NativeApplyAssetValues(Counts.Get(), CountSource));
    TestTrue(TEXT("uint64 never rounds"),
        UP4AuthoringTools::NativeAssetValuesJson(Counts.Get()).Contains(TEXT("18446744073709551615")));
    TestTrue(TEXT("map values compared by native key"),
        UP4AuthoringTools::NativeAssetReadback(Counts.Get(), CountSource).Contains(TEXT("\"valid\":true")));
    const FString CountHeld = UP4AuthoringTools::NativeAssetValuesJson(Counts.Get());
    TestFalse(TEXT("native enum aliases cannot create duplicate keys"),
        UP4AuthoringTools::NativeApplyAssetValues(Counts.Get(),
            TEXT(
                "{\"ActorPlacedNumInfo\":{\"ExploreRateActorNum\":[[\"Otakara\",14],[\"EExploreRateTargetType::Otakara\",2]]}}")));
    TestEqual(
        TEXT("failed map write retains all fields"), UP4AuthoringTools::NativeAssetValuesJson(Counts.Get()), CountHeld);
    TestTrue(TEXT("empty nested map clears explicitly"),
        UP4AuthoringTools::NativeApplyAssetValues(
            Counts.Get(), TEXT("{\"ActorPlacedNumInfo\":{\"ExploreRateActorNum\":[]}}")));
    TestTrue(TEXT("partial struct edits retain other fields"),
        UP4AuthoringTools::NativeAssetValuesJson(Counts.Get()).Contains(TEXT("18446744073709551615")));
    auto SceneClass = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GameSceneData"));
    TStrongObjectPtr<UObject> Scene(UP4AuthoringTools::NewNativeAsset(SceneClass));
    if (!TestTrue(TEXT("actual recovered scene class"), Scene.IsValid()))
        return false;
    const FString Missing = TEXT("{\"ParentScene\":\"/Game/NativeQuality/AbsentScene.AbsentScene\"}");
    TestTrue(TEXT("pending references can be inventoried without substitution"),
        UP4AuthoringTools::NativeAssetReferences(Scene.Get(), Missing).Contains(TEXT("\"valid\":true")));
    TestTrue(TEXT("publication schema rejects missing hard reference"),
        UP4AuthoringTools::NativeCanonicalAssetValues(Scene.Get(), Missing).Contains(TEXT("\"valid\":false")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StaticMeshStagingTest, "P4MapKit.Quality.StaticMeshStaging",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StaticMeshStagingTest::RunTest(const FString&)
{
    return P4TestStaticMeshStaging(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TableAdmissionTest, "P4MapKit.Quality.TableAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TableAdmissionTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
    Table->RowStruct = FTableRowBase::StaticStruct();
    TestTrue(TEXT("Valid rows admitted"), UP4AuthoringTools::ImportTableJson(Table, TEXT("[{\"Name\":\"Original\"}]")));
    const FString Original = UP4AuthoringTools::TableJson(Table);
    for (const TCHAR* Invalid : {TEXT("[{\"Name\":\"Original\",\"Unknown\":1}]"),
             TEXT("[{\"Name\":\"Original\"},{\"Name\":\"original\"}]"), TEXT("[{\"Name\":\"None\"}]"), TEXT("broken")})
    {
        TestFalse(TEXT("Invalid import refused"), UP4AuthoringTools::ImportTableJson(Table, Invalid));
        TestEqual(TEXT("Failed import retains rows"), UP4AuthoringTools::TableJson(Table), Original);
    }
    TestTrue(TEXT("Explicit delete all"), UP4AuthoringTools::ImportTableJson(Table, TEXT("[]")));
    TestEqual(TEXT("Empty table"), Table->GetRowMap().Num(), 0);
    UDataTable* UndoTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transactional);
    UndoTable->RowStruct = FTableRowBase::StaticStruct();
    UndoTable->AddRow(TEXT("Before"), FTableRowBase());
    TestTrue(TEXT("Transaction import"), UP4AuthoringTools::ImportTableJson(UndoTable, TEXT("[{\"Name\":\"After\"}]")));
    TestTrue(TEXT("Undo transaction"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores original rows"), UndoTable->GetRowMap().Contains(TEXT("Before")));
    TestFalse(TEXT("Undo removes imported rows"), UndoTable->GetRowMap().Contains(TEXT("After")));
    TestTrue(TEXT("Redo transaction"), GEditor->RedoTransaction());
    TestTrue(TEXT("Redo restores imported rows"), UndoTable->GetRowMap().Contains(TEXT("After")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TableSchemaTest, "P4MapKit.Quality.TableSchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TableSchemaTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    // Real FProperties, without adding artificial reflected classes to delivery.
    auto Struct = NewObject<UScriptStruct>(GetTransientPackage(), NAME_None, RF_Transient);
    Struct->SetSuperStruct(FTableRowBase::StaticStruct());
    auto Asset = new FObjectProperty(Struct, TEXT("Asset"), RF_Public);
    Asset->PropertyClass = UDataTable::StaticClass();
    Struct->AddCppProperty(Asset);
    auto Fixed = new FObjectProperty(Struct, TEXT("Fixed"), RF_Public);
    Fixed->PropertyClass = UDataTable::StaticClass();
    Fixed->ArrayDim = 3;
    Struct->AddCppProperty(Fixed);
    auto Class = new FClassProperty(Struct, TEXT("Class"), RF_Public);
    Class->PropertyClass = UClass::StaticClass();
    Class->MetaClass = UDataTable::StaticClass();
    Struct->AddCppProperty(Class);
    auto Array = new FArrayProperty(Struct, TEXT("Assets"), RF_Public);
    auto Inner = new FObjectProperty(Array, TEXT("Inner"), RF_Public);
    Inner->PropertyClass = UDataTable::StaticClass();
    Array->Inner = Inner;
    Struct->AddCppProperty(Array);
    auto Map = new FMapProperty(Struct, TEXT("ByName"), RF_Public);
    Map->KeyProp = new FNameProperty(Map, TEXT("Key"), RF_Public);
    auto MapValue = new FObjectProperty(Map, TEXT("Value"), RF_Public);
    MapValue->PropertyClass = UDataTable::StaticClass();
    Map->ValueProp = MapValue;
    Struct->AddCppProperty(Map);
    auto Number = new FUInt64Property(Struct, TEXT("Exact"), RF_Public);
    Struct->AddCppProperty(Number);
    auto Path = new FStructProperty(Struct, TEXT("Path"), RF_Public);
    Path->Struct = TBaseStructure<FSoftObjectPath>::Get();
    Struct->AddCppProperty(Path);
    Struct->Bind();
    Struct->StaticLink(true);
    auto Table = UP4AuthoringTools::NewNativeTable(Struct);
    if (!TestNotNull(TEXT("Native dynamically reflected row schema"), Table))
        return false;
    auto Target = NewObject<UDataTable>(
        CreatePackage(TEXT("/Game/NativeJobTests/References")), TEXT("Target"), RF_Transient);
    Target->RowStruct = FTableRowBase::StaticStruct();
    const FString TargetPath = Target->GetPathName();
    const FString Original = TEXT("[{\"Name\":\"Original\",\"asset\":\"") + TargetPath +
        TEXT(
            "\",\"class\":\"/Script/Engine.DataTable\",\"Fixed\":null,\"fixed[1]\":\"None\",\"FIXED[2]\":\"\",\"Assets\":[\"") +
        TargetPath + TEXT("\"],\"ByName\":{\"__native_map_pairs\":[[\"First\",\"") + TargetPath +
        TEXT("\"]]},\"exact\":18446744073709551615,\"path\":\"/Game/StillUnrecovered.Asset\"}]");
    const FString Verdict = UP4AuthoringTools::NativeCanonicalTableRows(Table, Original);
    TestTrue(TEXT("Native fields/slots/wrappers admitted"), Verdict.Contains(TEXT("\"valid\":true")));
    TestTrue(TEXT("Actual native field spelling returned"),
        Verdict.Contains(TEXT("\"") + Number->GetName() + TEXT("\":18446744073709551615")));
    TestTrue(TEXT("Complete fixed slots assembled and null refs normalized"),
        Verdict.Contains(TEXT("\"Fixed\":[\"\",\"\",\"\"]")));
    TestEqual(TEXT("Admission does not create rows"), Table->GetRowMap().Num(), 0);
    TestFalse(TEXT("Schema-only struct cannot construct native RowBase vtable"),
        UP4AuthoringTools::ImportTableJson(Table, Original));
    const FString Held = UP4AuthoringTools::NativeTableRowsJson(Table);
    TestTrue(TEXT("Hard asset admitted without creating rows"), Verdict.Contains(TargetPath));
    TestTrue(TEXT("Unresolved soft path retained"), Verdict.Contains(TEXT("/Game/StillUnrecovered.Asset")));
    const FString Missing = TEXT("/Game/NativeJobTests/References.Missing");
    const TArray<FString> Invalid = {Original.Replace(*TargetPath, *Missing),
        Original.Replace(TEXT("/Script/Engine.DataTable"), TEXT("/Script/Engine.Texture2D")),
        TEXT("[{\"Name\":\"Original\",\"Asset\":\"/Script/Engine.Texture2D\"}]"),
        TEXT("[{\"Name\":\"Original\",\"Asset\":null,\"asset\":null}]"),
        TEXT("[{\"Name\":\"Original\",\"Exact\":0,\"Exact\":1}]"), TEXT("[{\"Name\":\"Original\",\"Name\":\"Lost\"}]"),
        TEXT("[{\"Name\":\"Original\",\"Fixed\":null,\"Fixed[1]\":null}]"),
        TEXT("[{\"Name\":\"Original\",\"Fixed\":[null,null,null],\"Fixed[1]\":null}]"),
        TEXT("[{\"Name\":\"Original\",\"Fixed[3]\":null}]"),
        TEXT("[{\"Name\":\"Original\",\"Fixed\":null,\"Fixed[0]\":null}]"),
        TEXT("[{\"Name\":\"Original\",\"Assets\":[\"/Script/Engine.Texture2D\"]}]"),
        TEXT("[{\"Name\":\"Original\",\"ByName\":[[\"First\",\"/Script/Engine.Texture2D\"]]}]"),
        TEXT("[{\"Name\":\"Original\",\"ByName\":[[\"First\",null],[\"first\",null]]}]"),
        TEXT("[{\"Name\":\"Original\",\"Exact\":18446744073709551616}]"),
        TEXT("[{\"Name\":\"Original\",\"Exact\":1.5}]"), TEXT("[{\"Name\":\"Original\",\"Path\":{\"Unknown\":1}}]"),
        TEXT("[{\"Name\":\"Original\",\"Path\":{\"AssetPathName\":\"X\",\"assetpathname\":\"Y\"}}]"),
        TEXT("[{\"Name\":\"Original\\u0000Lost\"}]"),
        TEXT("[{\"Name\":\"Original\",\"Path\":{\"AssetPathName\":\"X\\u0000Lost\"}}]")};
    for (const auto& Input : Invalid)
    {
        TestTrue(TEXT("Invalid native admission diagnosed: ") + Input,
            UP4AuthoringTools::NativeCanonicalTableRows(Table, Input).Contains(TEXT("\"valid\":false")));
        TestFalse(
            TEXT("Direct import cannot bypass native admission"), UP4AuthoringTools::ImportTableJson(Table, Input));
        TestEqual(TEXT("Refused input retains live rows"), UP4AuthoringTools::NativeTableRowsJson(Table), Held);
    }
    for (const TCHAR* Field : {TEXT("Name"), TEXT("Unknown")})
    {
        const FString Oversized = TEXT("[{\"") + FString(Field) + TEXT("\":\"") + FString::ChrN(NAME_SIZE, TEXT('X')) +
            TEXT("\"}]");
        TestTrue(TEXT("Engine name overflow refused"),
            UP4AuthoringTools::NativeCanonicalTableRows(Table, Oversized).Contains(TEXT("\"valid\":false")));
    }
    const FString Deep = TEXT("[{\"Name\":\"Original\",\"Assets\":") + FString::ChrN(4096, TEXT('[')) + TEXT("0") +
        FString::ChrN(4096, TEXT(']')) + TEXT("}]");
    TestTrue(TEXT("Deep JSON refused before DOM parse"),
        UP4AuthoringTools::NativeCanonicalTableRows(Table, Deep).Contains(TEXT("\"valid\":false")));
    auto Worker = Async(EAsyncExecution::ThreadPool,
        [Table]() { return UP4AuthoringTools::NativeCanonicalTableRows(Table, TEXT("[]")); });
    TestTrue(TEXT("Native UObject admission refuses worker thread"), Worker.Get().Contains(TEXT("\"valid\":false")));
    bool NativeClassTested = false;
    for (TObjectIterator<UScriptStruct> Candidate; Candidate && !NativeClassTested; ++Candidate)
    {
        if (Candidate->GetOutermost()->GetName() != TEXT("/Script/Carrot4") || !Candidate->GetCppStructOps() ||
            !Candidate->IsChildOf(FTableRowBase::StaticStruct()))
            continue;
        for (TFieldIterator<FProperty> Field(*Candidate); Field; ++Field)
        {
            auto Reference = CastField<FClassProperty>(*Field);
            if (!Reference || Reference->ArrayDim != 1 || !Reference->MetaClass ||
                Reference->HasAnyPropertyFlags(CPF_Transient))
                continue;
            auto NativeTable = UP4AuthoringTools::NewNativeTable(*Candidate);
            const FString Qualified = TEXT("[{\"Name\":\"Original\",\"") + Reference->GetName() + TEXT("\":\"") +
                Reference->MetaClass->GetPathName() + TEXT("\"}]");
            TestTrue(TEXT("Actual recovered native row imports qualified class reference"),
                UP4AuthoringTools::ImportTableJson(NativeTable, Qualified));
            if (!NativeTable->GetRowMap().Contains(TEXT("Original")))
                return false;
            const auto ReadClass = [&]()
            {
                return Reference->GetObjectPropertyValue(
                    Reference->ContainerPtrToValuePtr<void>(NativeTable->GetRowMap().FindChecked(TEXT("Original"))));
            };
            TestEqual(
                TEXT("Actual native reference readback"), ReadClass(), static_cast<UObject*>(Reference->MetaClass));
            const FString Rejected = Qualified.Replace(
                *Reference->MetaClass->GetPathName(), TEXT("/Game/NativeJobTests/NoSuchClass.Missing_C"));
            TestFalse(TEXT("Direct actual native import refuses missing reference"),
                UP4AuthoringTools::ImportTableJson(NativeTable, Rejected));
            TestEqual(TEXT("Refusal preserves actual native reference"), ReadClass(),
                static_cast<UObject*>(Reference->MetaClass));
            TestEqual(TEXT("Refusal preserves actual native row count"), NativeTable->GetRowMap().Num(), 1);
            NativeClassTested = true;
            break;
        }
    }
    TestTrue(TEXT("Recovered native class-reference row exercised"), NativeClassTested);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextFidelityTest, "P4MapKit.Quality.LocalizedText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextFidelityTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    auto Struct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.HowToPlayOsusowakeTextLabel"));
    if (!TestNotNull(TEXT("Original native localized row type"), Struct))
        return false;
    auto Text = CastField<FTextProperty>(Struct->FindPropertyByName(TEXT("label")));
    if (!TestNotNull(TEXT("Actual FText property"), Text))
        return false;
    TestTrue(TEXT("Original label is editable"), Text->HasAnyPropertyFlags(CPF_Edit));
    UDataTable* Table = NewObject<UDataTable>(
        CreatePackage(TEXT("/Game/NativeJobTests/Localized")), TEXT("TextTable"), RF_Transactional);
    Table->RowStruct = Struct;
    const FString Original = TEXT(
        "[{\"Name\":\"Original\",\"label\":{\"__native_text\":{\"flags\":0,\"history\":0,\"namespace\":\"OriginalNamespace\",\"key\":\"StableOriginalKey\",\"source\":\"Original label\"}}}]");
    TestTrue(TEXT("Full native localization metadata imported"), UP4AuthoringTools::ImportTableJson(Table, Original));
    const FString Held = UP4AuthoringTools::NativeTableRowsJson(Table);
    const auto ReadText = [&]()
    {
        return Text->GetPropertyValue(
            Text->ContainerPtrToValuePtr<void>(Table->GetRowMap().FindChecked(TEXT("Original"))));
    };
    TestEqual(TEXT("Original key retained"), FTextInspector::GetKey(ReadText()).GetValue(),
        FString(TEXT("StableOriginalKey")));
    TestEqual(TEXT("Original namespace retained"), FTextInspector::GetNamespace(ReadText()).GetValue(),
        FString(TEXT("OriginalNamespace")));
    TestTrue(TEXT("Source edit imported"),
        UP4AuthoringTools::ImportTableJson(Table, Original.Replace(TEXT("Original label"), TEXT("Edited label"))));
    TestTrue(TEXT("Native text edit undo"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores complete text metadata"), UP4AuthoringTools::NativeTableRowsJson(Table), Held);
    TestTrue(TEXT("Native text edit redo"), GEditor->RedoTransaction());
    TestEqual(
        TEXT("Redo restores source"), *FTextInspector::GetSourceString(ReadText()), FString(TEXT("Edited label")));
    for (const FString& Invalid : {Original.Replace(TEXT("\"flags\":0"), TEXT("\"flags\":8")),
             Original.Replace(TEXT("\"history\":0"), TEXT("\"history\":1")),
             Original.Replace(TEXT("StableOriginalKey"), TEXT("")), Original.Replace(TEXT("Original label"), TEXT(""))})
    {
        const FString Before = UP4AuthoringTools::NativeTableRowsJson(Table);
        TestFalse(TEXT("Lossy text representation refused"), UP4AuthoringTools::ImportTableJson(Table, Invalid));
        TestEqual(
            TEXT("Text refusal leaves native rows unchanged"), UP4AuthoringTools::NativeTableRowsJson(Table), Before);
    }
    TestTrue(TEXT("Empty native text admitted"),
        UP4AuthoringTools::ImportTableJson(Table,
            TEXT(
                "[{\"Name\":\"Original\",\"label\":{\"__native_text\":{\"flags\":0,\"history\":-1,\"has_source\":false}}}]")));
    TestTrue(TEXT("Empty FText remains empty"), ReadText().IsEmpty());
    TestTrue(TEXT("Invariant native text admitted"),
        UP4AuthoringTools::ImportTableJson(Table,
            TEXT(
                "[{\"Name\":\"Original\",\"label\":{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"source\":\"Invariant label\"}}}]")));
    TestTrue(TEXT("Invariant FText retains culture flag"), ReadText().IsCultureInvariant());
    TestEqual(TEXT("Invariant source retained"), *FTextInspector::GetSourceString(ReadText()),
        FString(TEXT("Invariant label")));
    TestTrue(TEXT("Restore localized value"), UP4AuthoringTools::ImportTableJson(Table, Original));
    const FString PackageNamespace = TextNamespaceUtil::EnsurePackageNamespace(Table);
    const FText EditorText = FText::ChangeKey(
        TextNamespaceUtil::BuildFullNamespace(TEXT("OriginalNamespace"), PackageNamespace, true),
        TEXT("StableOriginalKey"), ReadText());
    Text->SetPropertyValue(
        Text->ContainerPtrToValuePtr<void>(Table->GetRowMap().FindChecked(TEXT("Original"))), EditorText);
    TestEqual(TEXT("Editor package suffix retains exact runtime localization identity"),
        UP4AuthoringTools::NativeTableRowsJson(Table), Held);
    TestTrue(TEXT("Native re-publication retains runtime identity"), UP4AuthoringTools::ImportTableJson(Table, Held));
    TestEqual(TEXT("Original key remains after editor namespace normalization"),
        FTextInspector::GetKey(ReadText()).GetValue(), FString(TEXT("StableOriginalKey")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TableReadbackTest, "P4MapKit.Quality.TableReadback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TableReadbackTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    auto Struct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.HowToPlayOsusowakeTextLabel"));
    if (!TestNotNull(TEXT("Actual native text row type"), Struct))
        return false;
    auto Table = UP4AuthoringTools::NewNativeTable(Struct);
    const FString Source = TEXT(
        "[{\"Name\":\"Original\",\"label\":{\"__native_text\":{\"flags\":0,\"history\":0,\"namespace\":\"NativeReadback.UI\",\"key\":\"ReadbackOriginalKey\",\"source\":\"Original readback source\"}}}]");
    if (!TestTrue(TEXT("Native original text import qualifies its own readback"),
            UP4AuthoringTools::ImportTableJson(Table, Source)))
        return false;
    const FString Held = UP4AuthoringTools::NativeTableRowsJson(Table);
    TestTrue(TEXT("Independent actual native readback matches"),
        UP4AuthoringTools::NativeTableReadback(Table, Source).Contains(TEXT("\"valid\":true")));
    TestTrue(TEXT("Native FName row casing is identity"),
        UP4AuthoringTools::NativeTableReadback(Table, Source.Replace(TEXT("Original\""), TEXT("original\"")))
            .Contains(TEXT("\"valid\":true")));
    TestTrue(TEXT("Serialized override projection can omit native defaults"),
        UP4AuthoringTools::NativeTableReadback(Table, TEXT("[{\"Name\":\"Original\"}]"))
            .Contains(TEXT("\"valid\":true")));
    const TArray<FString> Wrong = {Source.Replace(TEXT("Original readback source"), TEXT("Changed source")),
        Source.Replace(TEXT("NativeReadback.UI"), TEXT("ChangedNamespace")),
        Source.Replace(TEXT("ReadbackOriginalKey"), TEXT("ChangedKey")),
        Source.Replace(TEXT("Original\""), TEXT("OtherRow\"")), TEXT("[]"),
        TEXT(
            "[{\"Name\":\"Original\",\"label\":{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"source\":\"Original readback source\"}}}]")};
    for (const auto& Input : Wrong)
    {
        TestTrue(TEXT("Changed runtime metadata/identity diagnosed"),
            UP4AuthoringTools::NativeTableReadback(Table, Input).Contains(TEXT("\"valid\":false")));
        TestEqual(TEXT("Readback refusal does not mutate rows"), UP4AuthoringTools::NativeTableRowsJson(Table), Held);
    }
    bool IntegerTested = false;
    for (TObjectIterator<UScriptStruct> Candidate; Candidate && !IntegerTested; ++Candidate)
    {
        if (Candidate->GetOutermost()->GetName() != TEXT("/Script/Carrot4") ||
            !Candidate->IsChildOf(FTableRowBase::StaticStruct()))
            continue;
        auto Array = CastField<FArrayProperty>(Candidate->FindPropertyByName(TEXT("DebugUniqueIds")));
        if (!Array || !CastField<FUInt64Property>(Array->Inner))
            continue;
        auto Exact = UP4AuthoringTools::NewNativeTable(*Candidate);
        const FString Numbers = TEXT(
            "[{\"Name\":\"Exact\",\"DebugUniqueIds\":[0,9007199254740993,9223372036854775808,18446744073709551615]}]");
        if (!TestTrue(TEXT("Real uint64 row imports with native readback admission"),
                UP4AuthoringTools::ImportTableJson(Exact, Numbers)))
            return false;
        const FString Original = UP4AuthoringTools::NativeTableRowsJson(Exact);
        const FString Proof = UP4AuthoringTools::NativeTableReadback(Exact, Numbers);
        TestTrue(TEXT("All64 bits appear in both independent projections"),
            Proof.Contains(TEXT("\"valid\":true")) && Proof.Contains(TEXT("18446744073709551615")));
        TestTrue(TEXT("One-bit source difference above double precision refuses"),
            UP4AuthoringTools::NativeTableReadback(
                Exact, Numbers.Replace(TEXT("18446744073709551615"), TEXT("18446744073709551614")))
                .Contains(TEXT("\"valid\":false")));
        TestTrue(TEXT("Source array shrink refuses"),
            UP4AuthoringTools::NativeTableReadback(Exact, TEXT("[{\"Name\":\"Exact\",\"DebugUniqueIds\":[0]}]"))
                .Contains(TEXT("\"valid\":false")));
        TestEqual(TEXT("Source mismatches retain actual native rows"), UP4AuthoringTools::NativeTableRowsJson(Exact),
            Original);
        FScriptArrayHelper Values(
            Array, Array->ContainerPtrToValuePtr<void>(Exact->GetRowMap().FindChecked(TEXT("Exact"))));
        Array->Inner->InitializeValue(Values.GetRawPtr(3));
        TestTrue(TEXT("Independent changed native value refuses unchanged source"),
            UP4AuthoringTools::NativeTableReadback(Exact, Numbers).Contains(TEXT("\"valid\":false")));
        IntegerTested = true;
    }
    TestTrue(TEXT("Actual recovered uint64 schema exercised"), IntegerTested);
    TestTrue(TEXT("Missing table refuses"),
        UP4AuthoringTools::NativeTableReadback(nullptr, Source).Contains(TEXT("\"valid\":false")));
    auto Worker = Async(EAsyncExecution::ThreadPool,
        [Table, Source]() { return UP4AuthoringTools::NativeTableReadback(Table, Source); });
    TestTrue(TEXT("Native row reads refuse off the editor thread"), Worker.Get().Contains(TEXT("\"valid\":false")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TableReimportTest, "P4MapKit.Quality.TableReimport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TableReimportTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    TStrongObjectPtr<UDataTable> Current(UP4AuthoringTools::NewNativeTable(FTableRowBase::StaticStruct()));
    TStrongObjectPtr<UDataTable> Prior(UP4AuthoringTools::NewNativeTable(FTableRowBase::StaticStruct()));
    TStrongObjectPtr<UDataTable> Source(UP4AuthoringTools::NewNativeTable(FTableRowBase::StaticStruct()));
    for (const TCHAR* Name : {TEXT("Edited"), TEXT("Custom"), TEXT("Clash"), TEXT("Stock_1")})
        Current->AddRow(Name, FTableRowBase());
    for (const TCHAR* Name : {TEXT("Edited"), TEXT("Deleted"), TEXT("Stock_1")})
        Prior->AddRow(Name, FTableRowBase());
    for (const TCHAR* Name :
        {TEXT("Edited"), TEXT("Deleted"), TEXT("New"), TEXT("CLASH"), TEXT("STOCK_1"), TEXT("Stock_01")})
        Source->AddRow(Name, FTableRowBase());
    const FString Held = UP4AuthoringTools::NativeTableRowsJson(Current.Get());
    const FString Previous = UP4AuthoringTools::NativeTableRowsJson(Prior.Get());
    const FString Original = UP4AuthoringTools::NativeTableRowsJson(Source.Get());
    TSharedPtr<FJsonObject> Result;
    const FString Planned = UP4AuthoringTools::NativeMergeTableRows(Current.Get(), Prior.Get(), Source.Get());
    if (!TestTrue(TEXT("Native reimport plan has JSON result"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Planned), Result)))
        return false;
    TestFalse(TEXT("Qualified native row plan accepted"), Result->HasField(TEXT("error")));
    TestEqual(TEXT("Only genuinely new rows added"), Result->GetIntegerField(TEXT("added")), 2);
    TSet<FName> Names;
    for (const auto& Row : Result->GetArrayField(TEXT("rows")))
        Names.Add(FName(*Row->AsObject()->GetStringField(TEXT("Name"))));
    TestEqual(TEXT("Authored and added rows retained"), Names.Num(), 6);
    TestTrue(TEXT("Custom row survives"), Names.Contains(TEXT("Custom")));
    TestFalse(TEXT("Deleted original stays deleted"), Names.Contains(TEXT("Deleted")));
    TestTrue(TEXT("Numbered row identity retained"), Names.Contains(TEXT("Stock_1")));
    TestTrue(TEXT("Leading-zero row is distinct"), Names.Contains(TEXT("Stock_01")));
    TestEqual(TEXT("Full new stock baseline retained"), Result->GetArrayField(TEXT("baseline")).Num(), 6);
    TestEqual(
        TEXT("Planning leaves current rows unchanged"), UP4AuthoringTools::NativeTableRowsJson(Current.Get()), Held);
    TestEqual(TEXT("Planning leaves prior baseline unchanged"), UP4AuthoringTools::NativeTableRowsJson(Prior.Get()),
        Previous);
    TestEqual(
        TEXT("Planning leaves source rows unchanged"), UP4AuthoringTools::NativeTableRowsJson(Source.Get()), Original);
    Source->EmptyTable();
    Source->CreateTableFromOtherTable(Prior.Get());
    Current->EmptyTable();
    const FString Cleared = UP4AuthoringTools::NativeMergeTableRows(Current.Get(), Prior.Get(), Source.Get());
    TestTrue(TEXT("Delete-all reimport produces no resurrection"),
        Cleared.Contains(TEXT("\"rows\":[]")) && Cleared.Contains(TEXT("\"added\":0")));
    TestTrue(TEXT("Initial import accepts no existing table"),
        UP4AuthoringTools::NativeMergeTableRows(nullptr, Current.Get(), Source.Get()).Contains(TEXT("\"added\":3")));
    TestTrue(TEXT("Missing source refuses"),
        UP4AuthoringTools::NativeMergeTableRows(Current.Get(), Prior.Get(), nullptr).Contains(TEXT("\"error\"")));
    auto NativeStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.HowToPlayOsusowakeTextLabel"));
    if (!TestNotNull(TEXT("Recovered typed row schema"), NativeStruct))
        return false;
    TStrongObjectPtr<UDataTable> Typed(UP4AuthoringTools::NewNativeTable(NativeStruct));
    TestTrue(TEXT("Different native row schema refuses"),
        UP4AuthoringTools::NativeMergeTableRows(Typed.Get(), Prior.Get(), Source.Get()).Contains(TEXT("\"error\"")));
    auto Worker = Async(EAsyncExecution::ThreadPool,
        [Current = Current.Get(), Prior = Prior.Get(), Source = Source.Get()]()
        { return UP4AuthoringTools::NativeMergeTableRows(Current, Prior, Source).Contains(TEXT("editor thread")); });
    TestTrue(TEXT("Worker cannot create or copy editor tables"), Worker.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ArrayRowsReplacedTest, "P4MapKit.Quality.ArrayRowsReplaced",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ArrayRowsReplacedTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UP4AuthoringTools::EnableOriginalAssetDetails();
    auto Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.ActorPlacementInfoAsset"));
    TStrongObjectPtr<UObject> Asset(UP4AuthoringTools::NewNativeAsset(Class));
    if (!TestTrue(TEXT("recovered placement asset class"), Asset.IsValid()))
        return false;
    if (!TestTrue(TEXT("first row written"),
            UP4AuthoringTools::NativeApplyAssetValues(Asset.Get(),
                TEXT("{\"ActorGeneratorList\":[{\"GeneratorID\":41,\"OutlineFolderPath\":\"Objects/First\"}]}"))))
        return false;
    TestTrue(TEXT("first row holds its folder"),
        UP4AuthoringTools::NativeAssetValuesJson(Asset.Get()).Contains(TEXT("\"OutlineFolderPath\":\"Objects/First\"")));
    if (!TestTrue(TEXT("replacement row written"),
            UP4AuthoringTools::NativeApplyAssetValues(
                Asset.Get(), TEXT("{\"ActorGeneratorList\":[{\"GeneratorID\":42}]}"))))
        return false;
    const FString After = UP4AuthoringTools::NativeAssetValuesJson(Asset.Get());
    TestTrue(TEXT("replacement row holds its own value"), After.Contains(TEXT("\"GeneratorID\":42")));
    TestFalse(TEXT("replacement row does not keep the earlier row's folder"), After.Contains(TEXT("Objects/First")));
    // The folder is a name, and a name starts as None.
    TestTrue(TEXT("a member the row leaves out is the row's default"),
        After.Contains(TEXT("\"OutlineFolderPath\":\"None\"")));
    return true;
}

namespace P4TableOrderTests
{
// A row of the game's cave treasure rank table: three plain numbers.
bool AddRank(UDataTable* Table, const TCHAR* Name, int32 Gold)
{
    auto Property = CastField<FIntProperty>(Table->RowStruct->FindPropertyByName(TEXT("Gold")));
    if (!Property)
        return false;
    FStructOnScope Row(Table->RowStruct);
    Property->SetPropertyValue_InContainer(Row.GetStructMemory(), Gold);
    Table->AddRow(Name, *reinterpret_cast<const FTableRowBase*>(Row.GetStructMemory()));
    return true;
}
// Row names in order, joined by commas.
FString RowNames(const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TArray<FString> Names;
    for (const auto& Row : Rows)
        Names.Add(Row->AsObject()->GetStringField(TEXT("Name")));
    return FString::Join(Names, TEXT(","));
}
FString RowNames(const FString& RowsJson)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RowsJson), Rows) ? RowNames(Rows)
                                                                                      : FString(TEXT("?"));
}
} //namespace P4TableOrderTests

// The order of a table's rows is data: a save writes them in it and the game reads them in it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TableRowOrderTest, "P4MapKit.Quality.TableRowOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TableRowOrderTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TableOrderTests;
    auto RowStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.CaveOtakaraCollectRank"));
    if (!TestNotNull(TEXT("recovered rank row"), RowStruct))
        return false;
    TStrongObjectPtr<UDataTable> Current(UP4AuthoringTools::NewNativeTable(RowStruct));
    TStrongObjectPtr<UDataTable> Prior(UP4AuthoringTools::NewNativeTable(RowStruct));
    TStrongObjectPtr<UDataTable> Source(UP4AuthoringTools::NewNativeTable(RowStruct));
    bool Made = true;
    for (UDataTable* Table : {Current.Get(), Prior.Get(), Source.Get()})
        Made = Made && AddRank(Table, TEXT("Zeta"), 3) && AddRank(Table, TEXT("Alpha"), 1) &&
            AddRank(Table, TEXT("Mid"), 2);
    Made = Made && AddRank(Source.Get(), TEXT("Beta"), 4);
    if (!TestTrue(TEXT("tables made"), Made))
        return false;
    TestEqual(TEXT("rows are read in the table's own order"),
        RowNames(UP4AuthoringTools::NativeTableRowsJson(Current.Get())), FString(TEXT("Zeta,Alpha,Mid")));
    TSharedPtr<FJsonObject> Plan;
    if (!TestTrue(TEXT("reimport plan"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeMergeTableRows(
                                             Current.Get(), Prior.Get(), Source.Get())),
                Plan) &&
                Plan.IsValid() && !Plan->HasField(TEXT("error"))))
        return false;
    TestEqual(TEXT("a reimport keeps the authored order and puts the game's new row after it"),
        RowNames(Plan->GetArrayField(TEXT("rows"))), FString(TEXT("Zeta,Alpha,Mid,Beta")));
    // The same rows in another order are not the table that was asked for.
    TStrongObjectPtr<UDataTable> Shuffled(UP4AuthoringTools::NewNativeTable(RowStruct));
    Made = AddRank(Shuffled.Get(), TEXT("Alpha"), 1) && AddRank(Shuffled.Get(), TEXT("Mid"), 2) &&
        AddRank(Shuffled.Get(), TEXT("Zeta"), 3);
    const FString Wanted = TEXT(
        "[{\"Name\":\"Zeta\",\"Gold\":3},{\"Name\":\"Alpha\",\"Gold\":1},{\"Name\":\"Mid\",\"Gold\":2}]");
    TestTrue(TEXT("readback accepts the rows in the order given"),
        Made && UP4AuthoringTools::NativeTableReadback(Current.Get(), Wanted).Contains(TEXT("\"valid\":true")));
    TestTrue(TEXT("readback refuses the same rows in another order"),
        UP4AuthoringTools::NativeTableReadback(Shuffled.Get(), Wanted).Contains(TEXT("\"valid\":false")));
    return true;
}

// A reimport merges the game's new rows only. When the game's file changes or drops a row that
// was imported before, nothing says whose value wins, so the plan is refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TableReimportChangedRowsTest, "P4MapKit.Quality.TableReimportChangedRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TableReimportChangedRowsTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TableOrderTests;
    auto RowStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.CaveOtakaraCollectRank"));
    if (!TestNotNull(TEXT("recovered rank row"), RowStruct))
        return false;
    TStrongObjectPtr<UDataTable> Current(UP4AuthoringTools::NewNativeTable(RowStruct));
    TStrongObjectPtr<UDataTable> Prior(UP4AuthoringTools::NewNativeTable(RowStruct));
    TStrongObjectPtr<UDataTable> Changed(UP4AuthoringTools::NewNativeTable(RowStruct));
    TStrongObjectPtr<UDataTable> Dropped(UP4AuthoringTools::NewNativeTable(RowStruct));
    bool Made = true;
    for (UDataTable* Table : {Current.Get(), Prior.Get()})
        Made = Made && AddRank(Table, TEXT("Kept"), 1) && AddRank(Table, TEXT("Other"), 2);
    Made = Made && AddRank(Changed.Get(), TEXT("Kept"), 1) && AddRank(Changed.Get(), TEXT("Other"), 9);
    Made = Made && AddRank(Dropped.Get(), TEXT("Kept"), 1);
    if (!TestTrue(TEXT("tables made"), Made))
        return false;
    const FString Held = UP4AuthoringTools::NativeTableRowsJson(Current.Get());
    const FString ChangedPlan = UP4AuthoringTools::NativeMergeTableRows(Current.Get(), Prior.Get(), Changed.Get());
    TestTrue(TEXT("a changed game row refuses the plan"),
        ChangedPlan.Contains(TEXT("\"error\"")) && ChangedPlan.Contains(TEXT("Other")));
    const FString DroppedPlan = UP4AuthoringTools::NativeMergeTableRows(Current.Get(), Prior.Get(), Dropped.Get());
    TestTrue(TEXT("a dropped game row refuses the plan"),
        DroppedPlan.Contains(TEXT("\"error\"")) && DroppedPlan.Contains(TEXT("Other")));
    TestFalse(TEXT("the same game rows still plan"),
        UP4AuthoringTools::NativeMergeTableRows(Current.Get(), Prior.Get(), Prior.Get()).Contains(TEXT("\"error\"")));
    TestEqual(TEXT("planning leaves the authored table as it was"),
        UP4AuthoringTools::NativeTableRowsJson(Current.Get()), Held);
    return true;
}
// An ordinary texture made from one of the game's: Happy's color map, 512 by 512 in DXT5 with ten
// mips, the top one in its .ubulk file, in the game's Characters_ColorMap group (the stock Project01).
#endif

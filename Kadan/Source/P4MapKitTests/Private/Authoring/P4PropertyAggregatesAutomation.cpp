#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Animation/AnimClassData.h"
#include "Curves/CurveVector.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace P4PropertyAggregateTests
{
UPackage* P4AggregatePackage(const TCHAR* Suffix)
{
    return CreatePackage(*(FString(TEXT("/Game/NativePropertyAggregateTests/")) + Suffix + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}

FString P4AggregateQuote(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return TEXT("\"") + Value + TEXT("\"");
}

TArray<uint8> P4AggregateNativeSnapshot(UObject* Object)
{
    TArray<uint8> Bytes;
    // Native UObject serialization is independent of PropertyJson. Disable
    // delta omissions; DuplicateVerbatim avoids package namespace creation.
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}

void P4AggregateMapAdd(FMapProperty* Property, void* Storage, int64 Key, UObject* Value)
{
    FScriptMapHelper Map(Property, Storage);
    const int32 Index = Map.AddDefaultValue_Invalid_NeedsRehash();
    CastFieldChecked<FEnumProperty>(Property->KeyProp)
        ->GetUnderlyingProperty()
        ->SetIntPropertyValue(Map.GetKeyPtr(Index), Key);
    CastFieldChecked<FObjectProperty>(Property->ValueProp)->SetObjectPropertyValue(Map.GetValuePtr(Index), Value);
    Map.Rehash();
}

bool P4AggregateMapEquals(FMapProperty* Property, UObject* Object, const TMap<int64, UObject*>& Expected)
{
    FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Object));
    if (Map.Num() != Expected.Num())
        return false;
    auto* Key = CastFieldChecked<FEnumProperty>(Property->KeyProp)->GetUnderlyingProperty();
    auto* Value = CastFieldChecked<FObjectProperty>(Property->ValueProp);
    for (int32 Index = 0; Index < Map.GetMaxIndex(); ++Index)
    {
        if (!Map.IsValidIndex(Index))
            continue;
        UObject* const* Found = Expected.Find(Key->GetSignedIntPropertyValue(Map.GetKeyPtr(Index)));
        if (!Found || *Found != Value->GetObjectPropertyValue(Map.GetValuePtr(Index)))
            return false;
    }
    return true;
}

bool P4AggregateSoftEquals(FArrayProperty* Property, UObject* Object, const TArray<FSoftObjectPath>& Expected)
{
    FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Object));
    auto* Inner = CastFieldChecked<FSoftObjectProperty>(Property->Inner);
    if (Array.Num() != Expected.Num())
        return false;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        const FSoftObjectPtr& Value = Inner->GetPropertyValue(Array.GetRawPtr(Index));
        if (Value.ToSoftObjectPath() != Expected[Index] || Value.Get() != nullptr)
            return false;
    }
    return true;
}
} //namespace P4PropertyAggregateTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyReferenceMapAutomation, "P4MapKit.Quality.PropertyReferenceMapTransactions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyReferenceMapAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAggregateTests;
    // Captured original schema: enum EVsPokerGameMode -> hard UDataTable.
    // This qualifies storage and editor transactions, not game poker behavior.
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.VsPokerParameter"));
    if (!TestNotNull(TEXT("Captured original data asset class"), Class) ||
        !TestNotNull(TEXT("Editor transaction owner"), GEditor))
        return false;
    auto* Property = CastField<FMapProperty>(Class->FindPropertyByName(TEXT("PorkerHandTableMap")));
    auto* Sibling = CastField<FEnumProperty>(Class->FindPropertyByName(TEXT("GameMode")));
    if (!TestNotNull(TEXT("Original hard-reference map"), Property) ||
        !TestNotNull(TEXT("Independent original sibling"), Sibling))
        return false;
    auto* Key = CastField<FEnumProperty>(Property->KeyProp);
    auto* Value = CastField<FObjectProperty>(Property->ValueProp);
    if (!TestNotNull(TEXT("Native enum map key"), Key) || !TestNotNull(TEXT("Native hard object map value"), Value))
        return false;
    TestEqual(TEXT("Captured hard reference class"), Value->PropertyClass, UDataTable::StaticClass());
    if (!TestTrue(TEXT("Map is genuinely editable without changing native flags"),
            Property->HasAnyPropertyFlags(CPF_Edit) &&
                !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance)))
        return false;
    UEnum* Enum = Key->GetEnum();
    if (!TestNotNull(TEXT("Captured native enum"), Enum))
        return false;
    const int64 FirstKey = Enum->GetValueByNameString(TEXT("EVsPokerGameMode::OneCard"));
    const int64 SecondKey = Enum->GetValueByNameString(TEXT("EVsPokerGameMode::TowCard"));
    if (!TestTrue(TEXT("Captured enum spellings resolve independently"), FirstKey == 1 && SecondKey == 2))
        return false;
    UPackage* Package = P4AggregatePackage(TEXT("HardMap"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Package, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UDataTable> Original(NewObject<UDataTable>(Package, NAME_None, RF_Transient));
    Original->RowStruct = FTableRowBase::StaticStruct();
    Sibling->GetUnderlyingProperty()->SetIntPropertyValue(
        Sibling->ContainerPtrToValuePtr<void>(Object.Get()), FirstKey);
    P4AggregateMapAdd(Property, Property->ContainerPtrToValuePtr<void>(Object.Get()), FirstKey, Original.Get());
    const TMap<int64, UObject*> BeforeMap = {{FirstKey, Original.Get()}};
    UDataTable* NewFirst = NewObject<UDataTable>(Package, NAME_None, RF_Transient);
    UDataTable* NewSecond = NewObject<UDataTable>(Package, NAME_None, RF_Transient);
    NewFirst->RowStruct = FTableRowBase::StaticStruct();
    NewSecond->RowStruct = FTableRowBase::StaticStruct();
    TWeakObjectPtr<UDataTable> FirstWeak(NewFirst), SecondWeak(NewSecond);
    const FString FirstText = TEXT("DataTable'") + NewFirst->GetPathName() + TEXT("'");
    const FString SecondText = TEXT("DataTable'") + NewSecond->GetPathName() + TEXT("'");
    const FString Replacement = TEXT("((EVsPokerGameMode::OneCard,") + FirstText +
        TEXT("),(EVsPokerGameMode::TowCard,") + SecondText + TEXT("))");
    Package->SetDirtyFlag(false);
    bool CallbackRan = false, BothStagedReferencesSurvived = false;
    const FDelegateHandle Callback = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Changed, const FEditPropertyChain& Chain)
        {
            (void)Chain;
            if (Changed != Object.Get())
                return;
            CallbackRan = true;
            FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Object.Get()));
            Map.EmptyValues(32);
            CollectGarbage(RF_NoFlags);
            BothStagedReferencesSurvived = FirstWeak.IsValid() && SecondWeak.IsValid();
        });
    const bool Edited = UP4AuthoringTools::SetEditablePropertyText(
        Object.Get(), TEXT("PorkerHandTableMap"), Replacement);
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Callback);
    TestTrue(TEXT("Whole-map pre-edit callback ran"), CallbackRan);
    TestTrue(TEXT("GC traces both staged map values despite destination map removal"), BothStagedReferencesSurvived);
    TestTrue(TEXT("Whole hard-reference map edit succeeds"), Edited);
    const TMap<int64, UObject*> AfterMap = {{FirstKey, FirstWeak.Get()}, {SecondKey, SecondWeak.Get()}};
    TestTrue(TEXT("Independent native map keeps both keys and exact references"),
        P4AggregateMapEquals(Property, Object.Get(), AfterMap));
    TestTrue(TEXT("Whole-map edit dirties owning package"), Package->IsDirty());
    TestTrue(TEXT("Ordinary UE Undo restores original map"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores original count, enum key and hard object"),
        P4AggregateMapEquals(Property, Object.Get(), BeforeMap));
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Undo history retains both hard references for Redo"), FirstWeak.IsValid() && SecondWeak.IsValid());
    TestTrue(TEXT("Ordinary UE Redo reapplies map"), GEditor->RedoTransaction());
    TestTrue(TEXT("Redo restores complete native map"), P4AggregateMapEquals(Property, Object.Get(), AfterMap));
    TestEqual(TEXT("All map transactions preserve sibling enum"),
        Sibling->GetUnderlyingProperty()->GetSignedIntPropertyValue(
            Sibling->ContainerPtrToValuePtr<void>(Object.Get())),
        FirstKey);
    const FString Before = UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("PorkerHandTableMap"));
    if (!TestFalse(TEXT("Whole hard-reference map projection is supported"), Before.IsEmpty()))
        return false;
    const TArray<uint8> NativeBefore = P4AggregateNativeSnapshot(Object.Get());
    if (!TestTrue(TEXT("Independent native whole-object snapshot contains data"), NativeBefore.Num() > 0))
        return false;
    Package->SetDirtyFlag(false);
    int32 NoopCallbacks = 0;
    const FDelegateHandle NoopCallback = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Changed, const FEditPropertyChain& Chain)
        {
            (void)Chain;
            if (Changed == Object.Get())
                ++NoopCallbacks;
        });
    const bool Reordered = UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("PorkerHandTableMap"),
        TEXT("((EVsPokerGameMode::TowCard,") + SecondText + TEXT("),(EVsPokerGameMode::OneCard,") + FirstText +
            TEXT("))"));
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(NoopCallback);
    TestTrue(TEXT("Map pair ordering is a semantic no-op"), Reordered);
    TestEqual(TEXT("Reordered identical map skips edit callbacks"), NoopCallbacks, 0);
    TestFalse(TEXT("Reordered identical map keeps package clean"), Package->IsDirty());
    TestTrue(TEXT("Map ordering no-op preserves all native keys and references"),
        P4AggregateMapEquals(Property, Object.Get(), AfterMap));
    TestTrue(TEXT("Ordering no-op preserves independent native whole-object bytes"),
        P4AggregateNativeSnapshot(Object.Get()) == NativeBefore);
    TStrongObjectPtr<UDataTable> Illegal(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
    Illegal->RowStruct = FTableRowBase::StaticStruct();
    const FString IllegalText = TEXT("((EVsPokerGameMode::OneCard,") + FirstText +
        TEXT("),(EVsPokerGameMode::TowCard,DataTable'") + Illegal->GetPathName() + TEXT("'))");
    AddExpectedError(TEXT("Illegal TEXT reference to a private object in external package"),
        EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("Late invalid hard map value refuses entire replacement"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("PorkerHandTableMap"), IllegalText));
    TestTrue(TEXT("Late map refusal preserves both native references and keys"),
        P4AggregateMapEquals(Property, Object.Get(), AfterMap));
    TestTrue(TEXT("Late refusal preserves complete case-sensitive projection"),
        UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("PorkerHandTableMap"))
            .Equals(Before, ESearchCase::CaseSensitive));
    TestFalse(TEXT("Late map refusal preserves clean package"), Package->IsDirty());
    TestFalse(TEXT("Duplicate actual enum identity refuses"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("PorkerHandTableMap"),
            TEXT("((EVsPokerGameMode::OneCard,None),(EVsPokerGameMode::OneCard,None))")));
    TestTrue(TEXT("Duplicate refusal preserves native map"), P4AggregateMapEquals(Property, Object.Get(), AfterMap));
    TestFalse(TEXT("A map key is not falsely exposed as an array index"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("PorkerHandTableMap[0]"), TEXT("None")));
    TestTrue(TEXT("All map refusals preserve independent native whole-object bytes"),
        P4AggregateNativeSnapshot(Object.Get()) == NativeBefore);
    TestFalse(TEXT("All map refusals preserve clean package"), Package->IsDirty());
    TestTrue(TEXT("Refusals create no Undo entry after successful edit"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo after refusals still reaches original complete map"),
        P4AggregateMapEquals(Property, Object.Get(), BeforeMap));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAggregateTextAutomation, "P4MapKit.Quality.PropertyAggregateTextFidelity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAggregateTextAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAggregateTests;
    UPackage* Package = P4AggregatePackage(TEXT("Text"));
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Package, NAME_None, RF_Transactional));
    auto* Fixed = CastField<FStructProperty>(UCurveVector::StaticClass()->FindPropertyByName(TEXT("FloatCurves")));
    if (!TestNotNull(TEXT("Actual engine fixed curve array"), Fixed))
        return false;
    TestEqual(TEXT("Actual static array has three channels"), Fixed->ArrayDim, 3);
    TestFalse(TEXT("Fixed curve source storage is not an editor editable root"), Fixed->HasAnyPropertyFlags(CPF_Edit));
    for (int32 Index = 0; Index < 3; ++Index)
        Curve->FloatCurves[Index].AddKey(float(Index), float(10 + Index));
    const FString Replacement(
        TEXT("((Keys=((Time=1,Value=21))),(Keys=((Time=2,Value=22))),(Keys=((Time=3,Value=23))))"));
    TestTrue(TEXT("Whole native fixed array imports all three channels"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves"), Replacement));
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const TArray<FRichCurveKey>& Keys = Curve->FloatCurves[Index].GetConstRefOfKeys();
        TestEqual(TEXT("Independent native fixed channel key count"), Keys.Num(), 1);
        if (Keys.Num() == 1)
        {
            TestEqual(TEXT("Independent native fixed channel time"), Keys[0].Time, float(Index + 1));
            TestEqual(TEXT("Independent native fixed channel value"), Keys[0].Value, float(Index + 21));
        }
    }
    const FString Before = UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves"));
    if (!TestFalse(TEXT("Complete fixed array is representable before refusal tests"), Before.IsEmpty()) ||
        !TestEqual(
            TEXT("Required native enum fixture has one key"), Curve->FloatCurves[1].GetConstRefOfKeys().Num(), 1))
        return false;
    const TCHAR* Refused[] = {TEXT("((Keys=()),(Keys=()))"), TEXT("((Keys=()),(Keys=()),(Keys=()),(Keys=()))"),
        TEXT("((Keys=()),(Keys=()),(Unknown=7))"), TEXT("((Keys=()),(Keys=()),(Keys=()),)"),
        TEXT("((Keys=()),(Keys=()),(Keys=())"), TEXT("((Keys=()),(Keys=()),(Keys=((Time=1,Value=2))),))")};
    for (const TCHAR* Text : Refused)
    {
        Package->SetDirtyFlag(false);
        TestFalse(TEXT("Incomplete or malformed fixed array refuses atomically"),
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves"), Text));
        TestTrue(TEXT("Refusal keeps complete three-channel projection"),
            UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves"))
                .Equals(Before, ESearchCase::CaseSensitive));
        TestFalse(TEXT("Fixed array refusal keeps package clean"), Package->IsDirty());
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const TArray<FRichCurveKey>& Keys = Curve->FloatCurves[Index].GetConstRefOfKeys();
            TestTrue(TEXT("Independent native fixed array stays intact after refusal"),
                Keys.Num() == 1 && Keys[0].Time == float(Index + 1) && Keys[0].Value == float(Index + 21));
        }
    }
    // Real engine enum storage can contain an unrepresentable imported byte.
    // A partial struct edit must refuse rather than publish only a valid leaf.
    FRichCurveKey& Corrupt = Curve->FloatCurves[1].GetKey(Curve->FloatCurves[1].GetFirstKeyHandle());
    const TEnumAsByte<ERichCurveInterpMode> PreviousMode = Corrupt.InterpMode;
    Corrupt.InterpMode = static_cast<ERichCurveInterpMode>(255);
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("Invalid native enum refuses complete property projection"),
        UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves")).IsEmpty());
    TestFalse(TEXT("Partial struct import refuses retained unrepresentable enum sibling"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys[0]"), TEXT("(Value=99)")));
    TestEqual(TEXT("Retained native enum refusal leaves value untouched"),
        Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value, 22.f);
    TestEqual(TEXT("Retained invalid enum itself is not silently repaired"),
        uint8(Curve->FloatCurves[1].GetConstRefOfKeys()[0].InterpMode), uint8(255));
    TestFalse(TEXT("Unrepresentable native enum refusal leaves package clean"), Package->IsDirty());
    Corrupt.InterpMode = PreviousMode;
    TestTrue(TEXT("Independent enum restoration returns original full projection"),
        UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves")).Equals(Before, ESearchCase::CaseSensitive));

    TStrongObjectPtr<UAnimClassData> Animation(NewObject<UAnimClassData>(Package, NAME_None, RF_Transactional));
    const FString FirstName(TEXT("Menu, (left)")), SecondName(TEXT("Menu \"right\" \\path"));
    const FString MapText = TEXT("((") + P4AggregateQuote(FirstName) + TEXT(",(BlendInTime=12,BlendOutTime=13)),(") +
        P4AggregateQuote(SecondName) + TEXT(",(BlendInTime=14,BlendOutTime=15)))");
    TestTrue(TEXT("Quoted aggregate map names preserve delimiters and escapes"),
        UP4AuthoringTools::SetPropertyText(Animation.Get(), TEXT("GraphBlendOptions"), MapText));
    const auto& Map = Animation->GetGraphBlendOptions();
    const FAnimGraphBlendOptions* First = Map.Find(FName(*FirstName));
    const FAnimGraphBlendOptions* Second = Map.Find(FName(*SecondName));
    TestTrue(TEXT("Independent native FName lookup finds exact delimited name"),
        First && First->BlendInTime == 12.f && First->BlendOutTime == 13.f);
    TestTrue(TEXT("Independent native FName lookup finds escaped quoted name"),
        Second && Second->BlendInTime == 14.f && Second->BlendOutTime == 15.f);
    TestEqual(TEXT("Names remain separate native map identities"), Map.Num(), 2);
    const FString MapBefore = UP4AuthoringTools::PropertyJson(Animation.Get(), TEXT("GraphBlendOptions"));
    if (!TestFalse(TEXT("Quoted-name map projection is supported"), MapBefore.IsEmpty()))
        return false;
    const TCHAR* BadNames[] = {TEXT("((\"unterminated,(BlendInTime=1)))"), TEXT("((\"trailing\\\",(BlendInTime=1)))"),
        TEXT("((First,(BlendInTime=1)),)"), TEXT("((First,(BlendInTime=1),Extra))")};
    for (const TCHAR* Text : BadNames)
    {
        Package->SetDirtyFlag(false);
        TestFalse(TEXT("Malformed aggregate names or pairs refuse"),
            UP4AuthoringTools::SetPropertyText(Animation.Get(), TEXT("GraphBlendOptions"), Text));
        TestTrue(TEXT("Malformed aggregate preserves whole map projection"),
            UP4AuthoringTools::PropertyJson(Animation.Get(), TEXT("GraphBlendOptions"))
                .Equals(MapBefore, ESearchCase::CaseSensitive));
        TestFalse(TEXT("Malformed aggregate keeps clean package"), Package->IsDirty());
    }
    // NAME_SIZE is the installed engine's native FName bound. Refuse before
    // attempting to construct an overlong name in the global engine name pool.
    const FString OverlongName = FString::ChrN(NAME_SIZE, TEXT('x'));
    Package->SetDirtyFlag(false);
    TestFalse(TEXT("Overlong native FName map key refuses safely"),
        UP4AuthoringTools::SetPropertyText(Animation.Get(), TEXT("GraphBlendOptions"),
            TEXT("((") + P4AggregateQuote(OverlongName) + TEXT(",(BlendInTime=1)))")));
    TestTrue(TEXT("Overlong name refusal preserves both native named entries"),
        Animation->GetGraphBlendOptions().Num() == 2 && Animation->GetGraphBlendOptions().Find(FName(*FirstName)) &&
            Animation->GetGraphBlendOptions().Find(FName(*SecondName)));
    TestTrue(TEXT("Overlong name refusal preserves complete map projection"),
        UP4AuthoringTools::PropertyJson(Animation.Get(), TEXT("GraphBlendOptions"))
            .Equals(MapBefore, ESearchCase::CaseSensitive));
    TestFalse(TEXT("Overlong name refusal preserves clean package"), Package->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertySoftArrayAutomation, "P4MapKit.Quality.PropertySoftReferenceArrays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertySoftArrayAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAggregateTests;
    UP4AuthoringTools::EnableOriginalAssetDetails();
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GameSceneData"));
    if (!TestNotNull(TEXT("Captured original scene asset class"), Class) ||
        !TestNotNull(TEXT("Native editor transaction owner"), GEditor))
        return false;
    auto* Array = CastField<FArrayProperty>(Class->FindPropertyByName(TEXT("LoadLevels")));
    auto* Sibling = CastField<FBoolProperty>(Class->FindPropertyByName(TEXT("bVariableOpenLevel")));
    if (!TestNotNull(TEXT("Actual scene streaming soft-reference array"), Array) ||
        !TestNotNull(TEXT("Independent scene bool sibling"), Sibling))
        return false;
    auto* Soft = CastField<FSoftObjectProperty>(Array->Inner);
    if (!TestNotNull(TEXT("Actual native soft-reference element"), Soft) ||
        !TestTrue(TEXT("Supported scene array is editable"),
            Array->HasAnyPropertyFlags(CPF_Edit) &&
                !Array->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance)))
        return false;
    UPackage* Package = P4AggregatePackage(TEXT("SoftArray"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Package, Class, NAME_None, RF_Transactional));
    Sibling->SetPropertyValue_InContainer(Object.Get(), true);
    const FString PathRoot = Package->GetName() + TEXT("_Absent/");
    const FSoftObjectPath Old(PathRoot + TEXT("Old.Old")), First(PathRoot + TEXT("First.First")),
        Second(PathRoot + TEXT("Second.Second"));
    {
        FScriptArrayHelper Values(Array, Array->ContainerPtrToValuePtr<void>(Object.Get()));
        Values.AddValue();
        Soft->SetPropertyValue(Values.GetRawPtr(0), FSoftObjectPtr(Old));
    }
    const FString OtherArraysBefore = UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("LoadAssets")) + TEXT("|") +
        UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("LoadBlueprints"));
    const FString Text = TEXT("(World'") + First.ToString() + TEXT("',World'") + Second.ToString() + TEXT("')");
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("Absent soft paths import without requiring world assets"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("LoadLevels"), Text));
    TestTrue(TEXT("Independent native soft array preserves order and exact unloaded paths"),
        P4AggregateSoftEquals(Array, Object.Get(), {First, Second}));
    TestNull(
        TEXT("First missing soft world was not created or loaded"), FindObject<UObject>(nullptr, *First.ToString()));
    TestNull(
        TEXT("Second missing soft world was not created or loaded"), FindObject<UObject>(nullptr, *Second.ToString()));
    TestTrue(TEXT("Native Undo restores previous unloaded path"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores actual soft reference array"), P4AggregateSoftEquals(Array, Object.Get(), {Old}));
    TestTrue(TEXT("Native Redo restores two-path array"), GEditor->RedoTransaction());
    TestTrue(
        TEXT("Redo restores ordered native references"), P4AggregateSoftEquals(Array, Object.Get(), {First, Second}));
    TestTrue(TEXT("Native sibling bool survives whole-array transaction"),
        Sibling->GetPropertyValue_InContainer(Object.Get()));
    TestTrue(TEXT("Unrelated scene arrays remain unchanged"),
        (UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("LoadAssets")) + TEXT("|") +
            UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("LoadBlueprints")))
            .Equals(OtherArraysBefore, ESearchCase::CaseSensitive));
    const TArray<uint8> SceneNativeBefore = P4AggregateNativeSnapshot(Object.Get());
    if (!TestTrue(TEXT("Independent complete native scene snapshot contains data"), SceneNativeBefore.Num() > 0))
        return false;
    Package->SetDirtyFlag(false);
    TestFalse(TEXT("Late malformed soft-reference array refuses atomically"),
        UP4AuthoringTools::SetEditablePropertyText(
            Object.Get(), TEXT("LoadLevels"), TEXT("(World'") + First.ToString() + TEXT("',\"unterminated)")));
    TestTrue(TEXT("Soft aggregate refusal preserves both ordered native paths"),
        P4AggregateSoftEquals(Array, Object.Get(), {First, Second}));
    TestTrue(TEXT("Soft aggregate refusal preserves independent complete native scene bytes"),
        P4AggregateNativeSnapshot(Object.Get()) == SceneNativeBefore);
    TestFalse(TEXT("Soft aggregate refusal preserves clean package"), Package->IsDirty());
    TestTrue(TEXT("Author can deliberately clear the complete soft-reference array"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("LoadLevels"), TEXT("()")));
    TestTrue(TEXT("Native soft-reference array is actually empty"), P4AggregateSoftEquals(Array, Object.Get(), {}));
    TestTrue(TEXT("Undo of deliberate removal restores ordered references"), GEditor->UndoTransaction());
    TestTrue(TEXT("Deletion Undo restores both exact unloaded paths"),
        P4AggregateSoftEquals(Array, Object.Get(), {First, Second}));
    TestTrue(TEXT("Deletion Undo restores independent complete native scene bytes"),
        P4AggregateNativeSnapshot(Object.Get()) == SceneNativeBefore);

    // Installed UE's real nested material import-name field is readonly.
    // Refuse before native mesh callbacks; this fixture never builds or renders.
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, NAME_None, RF_Transient));
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(Package, NAME_None, RF_Transactional));
    Mesh->StaticMaterials.Add(FStaticMaterial(Material.Get(), TEXT("Slot"), TEXT("Imported")));
    auto* Materials = CastField<FArrayProperty>(
        UStaticMesh::StaticClass()->FindPropertyByName(TEXT("StaticMaterials")));
    auto* Slot = Materials ? CastField<FStructProperty>(Materials->Inner) : nullptr;
    FProperty* Readonly = Slot ? Slot->Struct->FindPropertyByName(TEXT("ImportedMaterialSlotName")) : nullptr;
    if (!TestNotNull(TEXT("Actual nested readonly import name"), Readonly) ||
        !TestTrue(
            TEXT("Installed engine marks nested import name readonly"), Readonly->HasAnyPropertyFlags(CPF_EditConst)))
        return false;
    const FString MeshBefore = UP4AuthoringTools::PropertyJson(Mesh.Get(), TEXT("StaticMaterials"));
    if (!TestFalse(TEXT("Whole native material slot projection is supported"), MeshBefore.IsEmpty()))
        return false;
    Package->SetDirtyFlag(false);
    int32 MeshCallbacks = 0;
    const FDelegateHandle Callback = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Changed, const FEditPropertyChain& Chain)
        {
            (void)Chain;
            if (Changed == Mesh.Get())
                ++MeshCallbacks;
        });
    const bool LeafEdited = UP4AuthoringTools::SetEditablePropertyText(
        Mesh.Get(), TEXT("StaticMaterials[0].ImportedMaterialSlotName"), TEXT("Changed"));
    const bool WholeEdited = UP4AuthoringTools::SetEditablePropertyText(
        Mesh.Get(), TEXT("StaticMaterials[0]"), TEXT("(MaterialSlotName=Changed,ImportedMaterialSlotName=Changed)"));
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Callback);
    TestFalse(TEXT("Nested readonly leaf refuses"), LeafEdited);
    TestFalse(TEXT("Whole struct cannot bypass readonly child"), WholeEdited);
    TestEqual(TEXT("Readonly validation does not invoke mesh pre-edit callbacks"), MeshCallbacks, 0);
    TestTrue(TEXT("Whole native material slot remains unchanged"),
        Mesh->StaticMaterials.Num() == 1 && Mesh->StaticMaterials[0].MaterialInterface == Material.Get() &&
            Mesh->StaticMaterials[0].MaterialSlotName == FName(TEXT("Slot")) &&
            Mesh->StaticMaterials[0].ImportedMaterialSlotName == FName(TEXT("Imported")));
    TestTrue(TEXT("Readonly aggregate preserves complete case-sensitive projection"),
        UP4AuthoringTools::PropertyJson(Mesh.Get(), TEXT("StaticMaterials"))
            .Equals(MeshBefore, ESearchCase::CaseSensitive));
    TestFalse(TEXT("Readonly aggregate refusal preserves clean package"), Package->IsDirty());
    return !HasAnyErrors();
}
#endif

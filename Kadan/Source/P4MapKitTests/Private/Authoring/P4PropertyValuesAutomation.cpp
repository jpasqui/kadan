#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Curves/CurveVector.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Animation/AnimClassData.h"
#include "Serialization/JsonSerializer.h"
#include "P4NativeValueStaging.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNumericLimitsAutomation, "P4MapKit.Quality.PropertyNumericLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNumericLimitsAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    // These are captured original property types, not fabricated parser fixtures.
    // Their use here qualifies editor storage only, not original save defaults.
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Story"));
    if (!TestNotNull(TEXT("Recovered original save storage class"), Class))
        return false;
    auto* Signed = CastField<FInt64Property>(Class->FindPropertyByName(TEXT("CreatedTimeStamp")));
    auto* Unsigned = CastField<FUInt64Property>(Class->FindPropertyByName(TEXT("SavedVersion")));
    auto* SignedSibling = CastField<FInt64Property>(Class->FindPropertyByName(TEXT("SavedTimeStamp")));
    auto* UnsignedSibling = CastField<FUInt64Property>(Class->FindPropertyByName(TEXT("PatchVersion")));
    auto* Double = CastField<FDoubleProperty>(Class->FindPropertyByName(TEXT("PlayTime")));
    auto* DoubleSibling = CastField<FDoubleProperty>(Class->FindPropertyByName(TEXT("MultiPlayTime")));
    auto* Count = CastField<FIntProperty>(Class->FindPropertyByName(TEXT("SavedNum")));
    if (!TestNotNull(TEXT("Actual signed64 field"), Signed) ||
        !TestNotNull(TEXT("Actual unsigned64 field"), Unsigned) ||
        !TestNotNull(TEXT("Actual signed sibling"), SignedSibling) ||
        !TestNotNull(TEXT("Actual unsigned sibling"), UnsignedSibling) ||
        !TestNotNull(TEXT("Actual double field"), Double) ||
        !TestNotNull(TEXT("Actual double sibling"), DoubleSibling) ||
        !TestNotNull(TEXT("Actual signed32 count"), Count))
        return false;
    UPackage* Package = CreatePackage(TEXT("/Game/NativePropertyEditTests/NumericLimits"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Package, Class, NAME_None, RF_Transactional));
    const auto SignedValue = [&]() { return Signed->GetPropertyValue_InContainer(Object.Get()); };
    const auto UnsignedValue = [&]() { return Unsigned->GetPropertyValue_InContainer(Object.Get()); };
    SignedSibling->SetPropertyValue_InContainer(Object.Get(), int64(31));
    UnsignedSibling->SetPropertyValue_InContainer(Object.Get(), uint64(9007199254740993ULL));
    DoubleSibling->SetPropertyValue_InContainer(Object.Get(), 37.25);
    struct FSignedCase
    {
        const TCHAR* Text;
        int64 Value;
    };
    const FSignedCase SignedCases[] = {{TEXT("9223372036854775807"), MAX_int64},
        {TEXT("-9223372036854775808"), MIN_int64}, {TEXT("+0x7FFFFFFFFFFFFFFF"), MAX_int64},
        {TEXT("-0X8000000000000000"), MIN_int64}, {TEXT("-0x2A"), -42}, {TEXT("+42"), 42}, {TEXT("-0"), 0},
        {TEXT(" 0 "), 0}};
    for (const FSignedCase& Case : SignedCases)
    {
        TestTrue(FString(TEXT("Signed original property accepts ")) + Case.Text,
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("CreatedTimeStamp"), Case.Text));
        TestEqual(TEXT("Independent native signed storage is exact"), SignedValue(), Case.Value);
        TestEqual(TEXT("Exact signed JSON decimal readback"),
            UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("CreatedTimeStamp")),
            FString::Printf(TEXT("%lld"), Case.Value));
        TestEqual(TEXT("Signed edit preserves sibling"), SignedSibling->GetPropertyValue_InContainer(Object.Get()),
            int64(31));
    }
    struct FUnsignedCase
    {
        const TCHAR* Text;
        uint64 Value;
    };
    const FUnsignedCase UnsignedCases[] = {{TEXT("18446744073709551615"), MAX_uint64},
        {TEXT("0xffffffffffffffff"), MAX_uint64}, {TEXT("0X8000000000000000"), uint64(1) << 63},
        {TEXT("9007199254740993"), uint64(9007199254740993ULL)}, {TEXT("+0x2A"), 42}, {TEXT("+0"), 0}};
    for (const FUnsignedCase& Case : UnsignedCases)
    {
        TestTrue(FString(TEXT("Unsigned original property accepts ")) + Case.Text,
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SavedVersion"), Case.Text));
        TestEqual(TEXT("Independent native unsigned storage is exact"), UnsignedValue(), Case.Value);
        TestEqual(TEXT("Exact unsigned JSON decimal readback"),
            UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("SavedVersion")),
            FString::Printf(TEXT("%llu"), Case.Value));
        TestEqual(TEXT("Unsigned edit preserves sibling"), UnsignedSibling->GetPropertyValue_InContainer(Object.Get()),
            uint64(9007199254740993ULL));
    }
    Signed->SetPropertyValue_InContainer(Object.Get(), int64(-19));
    Unsigned->SetPropertyValue_InContainer(Object.Get(), uint64(23));
    const TCHAR* BadSigned[] = {TEXT("9223372036854775808"), TEXT("-9223372036854775809"), TEXT("0x8000000000000000"),
        TEXT("-0x8000000000000001"), TEXT("18446744073709551616"), TEXT("0x10000000000000000"), TEXT("0x"), TEXT("0xG"),
        TEXT("0x1p2"), TEXT("1e2"), TEXT("1.0"), TEXT("12a"), TEXT(""), TEXT(" "), TEXT("-"), TEXT("++1")};
    const TCHAR* BadUnsigned[] = {TEXT("-1"), TEXT("-0"), TEXT("-0x1"), TEXT("18446744073709551616"),
        TEXT("0x10000000000000000"), TEXT("0X"), TEXT("0x-1"), TEXT("0x1G"), TEXT("1.5"), TEXT("1e0"), TEXT("NaN")};
    const auto RefuseInteger = [&](FName Field, const TCHAR* Input)
    {
        Package->SetDirtyFlag(false);
        TestFalse(FString(TEXT("Out-of-range/malformed integer refuses: ")) + Input,
            UP4AuthoringTools::SetPropertyText(Object.Get(), Field, Input));
        TestEqual(TEXT("Integer refusal preserves same signed property"), SignedValue(), int64(-19));
        TestEqual(TEXT("Integer refusal preserves same unsigned property"), UnsignedValue(), uint64(23));
        TestEqual(TEXT("Integer refusal preserves signed sibling"),
            SignedSibling->GetPropertyValue_InContainer(Object.Get()), int64(31));
        TestEqual(TEXT("Integer refusal preserves unsigned sibling"),
            UnsignedSibling->GetPropertyValue_InContainer(Object.Get()), uint64(9007199254740993ULL));
        TestFalse(TEXT("Integer refusal preserves clean package"), Package->IsDirty());
    };
    for (const TCHAR* Input : BadSigned)
        RefuseInteger(TEXT("CreatedTimeStamp"), Input);
    for (const TCHAR* Input : BadUnsigned)
        RefuseInteger(TEXT("SavedVersion"), Input);
    TestTrue(TEXT("Narrow signed original field accepts minimum hex"),
        UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SavedNum"), TEXT("-0x80000000")));
    TestEqual(TEXT("Native signed32 minimum is exact"), Count->GetPropertyValue_InContainer(Object.Get()), MIN_int32);
    TestTrue(TEXT("Narrow signed original field accepts maximum"),
        UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SavedNum"), TEXT("2147483647")));
    TestEqual(TEXT("Native signed32 maximum is exact"), Count->GetPropertyValue_InContainer(Object.Get()), MAX_int32);
    for (const TCHAR* Input : {TEXT("2147483648"), TEXT("-2147483649"), TEXT("0x80000000"), TEXT("-0x80000001")})
    {
        Package->SetDirtyFlag(false);
        TestFalse(TEXT("Narrow original integer overflow refuses"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SavedNum"), Input));
        TestEqual(TEXT("Narrow overflow preserves native count"), Count->GetPropertyValue_InContainer(Object.Get()),
            MAX_int32);
        TestEqual(TEXT("Narrow overflow preserves signed64 sibling"), SignedValue(), int64(-19));
        TestFalse(TEXT("Narrow overflow preserves clean package"), Package->IsDirty());
    }

    struct FDoubleCase
    {
        const TCHAR* Text;
        double Value;
    };
    const FDoubleCase DoubleCases[] = {{TEXT("+.125f"), .125}, {TEXT("-2.5E+2"), -250.0}, {TEXT("1."), 1.0},
        {TEXT("1.7976931348623157e308"), DBL_MAX}, {TEXT("2.2250738585072014e-308"), DBL_MIN}, {TEXT("0e-999"), 0.0}};
    for (const FDoubleCase& Case : DoubleCases)
    {
        TestTrue(FString(TEXT("Finite native double accepts ")) + Case.Text,
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("PlayTime"), Case.Text));
        TestEqual(TEXT("Independent double storage is exact"), Double->GetPropertyValue_InContainer(Object.Get()),
            Case.Value);
        TestEqual(
            TEXT("Double edit preserves sibling"), DoubleSibling->GetPropertyValue_InContainer(Object.Get()), 37.25);
    }
    Double->SetPropertyValue_InContainer(Object.Get(), 5.25);
    const TCHAR* BadDouble[] = {TEXT("1.8e308"), TEXT("-1.8e308"), TEXT("1e-999"), TEXT("-1e-999"), TEXT("NaN"),
        TEXT("nan"), TEXT("Inf"), TEXT("-Infinity"), TEXT("1e+"), TEXT("1e-"), TEXT(".f"), TEXT("1ff"), TEXT("0x1p0"),
        TEXT("1 2")};
    for (const TCHAR* Input : BadDouble)
    {
        Package->SetDirtyFlag(false);
        TestFalse(FString(TEXT("Invalid native double refuses: ")) + Input,
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("PlayTime"), Input));
        TestEqual(TEXT("Double refusal preserves exact destination"),
            Double->GetPropertyValue_InContainer(Object.Get()), 5.25);
        TestEqual(
            TEXT("Double refusal preserves sibling"), DoubleSibling->GetPropertyValue_InContainer(Object.Get()), 37.25);
        TestFalse(TEXT("Double refusal preserves clean package"), Package->IsDirty());
    }
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Package, NAME_None, RF_Transactional));
    Curve->FloatCurves[0].AddKey(2.f, 7.f);
    Curve->FloatCurves[1].AddKey(3.f, 11.f);
    const TCHAR* BadFloat[] = {
        TEXT("3.5e38"), TEXT("-3.5e38"), TEXT("1e-46"), TEXT("-1e-46"), TEXT("1e999"), TEXT("Infinity")};
    for (const TCHAR* Input : BadFloat)
    {
        Package->SetDirtyFlag(false);
        TestFalse(FString(TEXT("Unrepresentable native float refuses: ")) + Input,
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[0].Keys[0].Value"), Input));
        TestEqual(TEXT("Float refusal preserves destination"), Curve->FloatCurves[0].GetConstRefOfKeys()[0].Value, 7.f);
        TestEqual(
            TEXT("Float refusal preserves same-key time"), Curve->FloatCurves[0].GetConstRefOfKeys()[0].Time, 2.f);
        TestEqual(
            TEXT("Float refusal preserves other fixed slot"), Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value, 11.f);
        TestFalse(TEXT("Float refusal preserves clean package"), Package->IsDirty());
    }
    TestTrue(TEXT("Finite float at maximum accepts"),
        UP4AuthoringTools::SetPropertyText(
            Curve.Get(), TEXT("FloatCurves[0].Keys[0].Value"), TEXT("3.4028234663852886e38")));
    TestEqual(
        TEXT("Actual float stores its native maximum"), Curve->FloatCurves[0].GetConstRefOfKeys()[0].Value, FLT_MAX);

    // The exact JSON adapter used by private native preparation must retain
    // integer tokens above double precision through an independently read field.
    TStrongObjectPtr<UObject> Scratch(NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient));
    FString Canonical, Error;
    const FString Exact = TEXT(
        "{\"CreatedTimeStamp\":-9223372036854775808,\"SavedTimeStamp\":9223372036854775807,\"SavedVersion\":18446744073709551615,\"PatchVersion\":9007199254740993,\"SavedNum\":-2147483648}");
    TestTrue(TEXT("Exact signed/unsigned JSON boundaries prepare"),
        P4PrepareExactPrivateProperties(Scratch.Get(), Exact, Canonical, Error));
    TestEqual(
        TEXT("JSON preserves native signed minimum"), Signed->GetPropertyValue_InContainer(Scratch.Get()), MIN_int64);
    TestEqual(TEXT("JSON preserves native signed maximum sibling"),
        SignedSibling->GetPropertyValue_InContainer(Scratch.Get()), MAX_int64);
    TestEqual(TEXT("JSON preserves native unsigned maximum"), Unsigned->GetPropertyValue_InContainer(Scratch.Get()),
        MAX_uint64);
    TestEqual(TEXT("JSON preserves native above-double integer sibling"),
        UnsignedSibling->GetPropertyValue_InContainer(Scratch.Get()), uint64(9007199254740993ULL));
    TestEqual(
        TEXT("JSON preserves native signed32 minimum"), Count->GetPropertyValue_InContainer(Scratch.Get()), MIN_int32);
    TestTrue(TEXT("Canonical source retains unsigned token"), Canonical.Contains(TEXT("18446744073709551615")));
    const TCHAR* BadJson[] = {TEXT("{\"CreatedTimeStamp\":9223372036854775808}"),
        TEXT("{\"CreatedTimeStamp\":-9223372036854775809}"), TEXT("{\"SavedVersion\":18446744073709551616}"),
        TEXT("{\"SavedVersion\":-1}"), TEXT("{\"SavedVersion\":1.0}"), TEXT("{\"CreatedTimeStamp\":1e2}"),
        TEXT("{\"SavedVersion\":\"18446744073709551615\"}"), TEXT("{\"CreatedTimeStamp\":false}"),
        TEXT("{\"SavedVersion\":null}"), TEXT("{\"SavedVersion\":0xFF}"), TEXT("{\"SavedNum\":2147483648}"),
        TEXT("{\"SavedNum\":-2147483649}"), TEXT("{\"SavedNum\":1.5}"), TEXT("{\"PlayTime\":1e999}"),
        TEXT("{\"PlayTime\":NaN}")};
    for (const TCHAR* Input : BadJson)
    {
        TestFalse(FString(TEXT("Malformed/range exact JSON refuses: ")) + Input,
            P4PrepareExactPrivateProperties(Scratch.Get(), Input, Canonical, Error));
        TestTrue(TEXT("Refusal has no prepared canonical document"), Canonical.IsEmpty());
        TestFalse(TEXT("Refusal gives a diagnostic"), Error.IsEmpty());
        TestEqual(TEXT("Preflight refusal preserves signed field"), Signed->GetPropertyValue_InContainer(Scratch.Get()),
            MIN_int64);
        TestEqual(TEXT("Preflight refusal preserves unsigned field"),
            Unsigned->GetPropertyValue_InContainer(Scratch.Get()), MAX_uint64);
        TestEqual(TEXT("Preflight refusal preserves exact sibling"),
            UnsignedSibling->GetPropertyValue_InContainer(Scratch.Get()), uint64(9007199254740993ULL));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNumericTransactionsAutomation,
    "P4MapKit.Quality.PropertyNumericTransactions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNumericTransactionsAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Story"));
    if (!TestNotNull(TEXT("Recovered numeric storage fixture"), Class) ||
        !TestNotNull(TEXT("Native editor transaction service"), GEditor))
        return false;
    auto* Signed = CastField<FInt64Property>(Class->FindPropertyByName(TEXT("CreatedTimeStamp")));
    auto* Sibling = CastField<FInt64Property>(Class->FindPropertyByName(TEXT("SavedTimeStamp")));
    auto* Unsigned = CastField<FUInt64Property>(Class->FindPropertyByName(TEXT("SavedVersion")));
    if (!TestNotNull(TEXT("Actual signed field"), Signed) || !TestNotNull(TEXT("Actual sibling field"), Sibling) ||
        !TestNotNull(TEXT("Actual unsigned field"), Unsigned))
        return false;
    // Captured save scalar flags already include CPF_Edit. The public details
    // enabler targets only game data assets, not this save-storage class. Verify
    // the native metadata explicitly so this test also runs alone.
    const auto Writable = [](FProperty* Property)
    {
        return Property->HasAnyPropertyFlags(CPF_Edit) &&
            !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
    };
    if (!TestTrue(TEXT("Original signed field is independently editable"), Writable(Signed)) ||
        !TestTrue(TEXT("Original unsigned field is independently editable"), Writable(Unsigned)))
        return false;
    UPackage* Package = CreatePackage(TEXT("/Game/NativePropertyEditTests/NumericTransactions"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Package, Class, NAME_None, RF_Transactional));
    Signed->SetPropertyValue_InContainer(Object.Get(), int64(17));
    Sibling->SetPropertyValue_InContainer(Object.Get(), int64(29));
    Unsigned->SetPropertyValue_InContainer(Object.Get(), uint64(9007199254740993ULL));
    Package->SetDirtyFlag(false);
    if (!TestTrue(TEXT("Native signed minimum editor edit"),
            UP4AuthoringTools::SetEditablePropertyText(
                Object.Get(), TEXT("CreatedTimeStamp"), TEXT("-0x8000000000000000"))))
        return false;
    TestEqual(
        TEXT("Editor stores signed minimum exactly"), Signed->GetPropertyValue_InContainer(Object.Get()), MIN_int64);
    TestTrue(TEXT("Numeric editor edit dirties package"), Package->IsDirty());
    TestTrue(TEXT("Native numeric Undo succeeds"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores native signed value"), Signed->GetPropertyValue_InContainer(Object.Get()), int64(17));
    TestEqual(
        TEXT("Undo preserves independent sibling"), Sibling->GetPropertyValue_InContainer(Object.Get()), int64(29));
    TestTrue(TEXT("Native numeric Redo succeeds"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo retains all signed64 bits"), Signed->GetPropertyValue_InContainer(Object.Get()), MIN_int64);
    if (!TestTrue(TEXT("Unsigned64 maximum editor edit"),
            UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("SavedVersion"), TEXT("0xFFFFFFFFFFFFFFFF"))))
        return false;
    TestEqual(
        TEXT("Editor retains unsigned maximum"), Unsigned->GetPropertyValue_InContainer(Object.Get()), MAX_uint64);
    Package->SetDirtyFlag(false);
    bool NoOpCallback = false;
    const FDelegateHandle Observe = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Target, const FEditPropertyChain& Chain)
        {
            (void)Chain;
            if (Target == Object.Get())
                NoOpCallback = true;
        });
    TestTrue(TEXT("Exact equivalent decimal no-op succeeds"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("SavedVersion"), TEXT("18446744073709551615")));
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Observe);
    TestFalse(TEXT("Numeric no-op does not issue edit callback"), NoOpCallback);
    TestFalse(TEXT("Numeric no-op preserves clean package"), Package->IsDirty());
    TestFalse(TEXT("Rejected unsigned overflow creates no editor mutation"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("SavedVersion"), TEXT("18446744073709551616")));
    TestFalse(TEXT("Rejected numeric edit preserves clean package"), Package->IsDirty());
    TestTrue(TEXT("Undo skips no-op and rejected edit"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores above-double unsigned baseline exactly"),
        Unsigned->GetPropertyValue_InContainer(Object.Get()), uint64(9007199254740993ULL));
    TestEqual(TEXT("Unsigned Undo preserves signed sibling edit"), Signed->GetPropertyValue_InContainer(Object.Get()),
        MIN_int64);
    TestTrue(TEXT("Unsigned Redo succeeds"), GEditor->RedoTransaction());
    TestEqual(
        TEXT("Unsigned Redo restores exact maximum"), Unsigned->GetPropertyValue_InContainer(Object.Get()), MAX_uint64);

    UClass* CountsClass = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.ActorPlacedNumAsset"));
    if (!TestNotNull(TEXT("Actual nested original count asset"), CountsClass))
        return false;
    UP4AuthoringTools::EnableOriginalAssetDetails();
    TStrongObjectPtr<UObject> Counts(NewObject<UObject>(Package, CountsClass, NAME_None, RF_Transactional));
    const FString Baseline = TEXT(
        "{\"ActorPlacedNumInfo\":{\"AssetVersion\":9007199254740993,\"ExploreRateActorNum\":[[\"Otakara\",14],[\"Survivor\",3]]}}");
    if (!TestTrue(TEXT("Actual native struct and sibling map baseline"),
            UP4AuthoringTools::NativeApplyAssetValues(Counts.Get(), Baseline)))
        return false;
    auto* Root = CastField<FStructProperty>(CountsClass->FindPropertyByName(TEXT("ActorPlacedNumInfo")));
    if (!TestNotNull(TEXT("Actual count root struct"), Root))
        return false;
    auto* Version = CastField<FUInt64Property>(Root->Struct->FindPropertyByName(TEXT("AssetVersion")));
    auto* Map = CastField<FMapProperty>(Root->Struct->FindPropertyByName(TEXT("ExploreRateActorNum")));
    if (!TestNotNull(TEXT("Actual nested version"), Version) || !TestNotNull(TEXT("Actual sibling map"), Map))
        return false;
    const FString Before = UP4AuthoringTools::PropertyJson(Counts.Get(), TEXT("ActorPlacedNumInfo"));
    Package->SetDirtyFlag(false);
    bool Called = false;
    const FDelegateHandle Normalize = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [&](UObject* Target, FPropertyChangedEvent& Event)
        {
            (void)Event;
            if (Target != Counts.Get())
                return;
            Called = true;
            void* Data = Root->ContainerPtrToValuePtr<void>(Counts.Get());
            Version->SetPropertyValue_InContainer(Data, uint64(7));
            FScriptMapHelper(Map, Map->ContainerPtrToValuePtr<void>(Data)).EmptyValues();
        });
    const bool Edited = UP4AuthoringTools::SetEditablePropertyText(
        Counts.Get(), TEXT("ActorPlacedNumInfo.AssetVersion"), TEXT("18446744073709551615"));
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Normalize);
    TestTrue(TEXT("Actual native post-edit notification invoked normalization"), Called);
    TestFalse(TEXT("Non-exact native numeric post-edit refuses"), Edited);
    void* Data = Root->ContainerPtrToValuePtr<void>(Counts.Get());
    TestEqual(TEXT("Refusal restores same nested numeric property"), Version->GetPropertyValue_InContainer(Data),
        uint64(9007199254740993ULL));
    TestEqual(TEXT("Refusal restores sibling native map entries"),
        FScriptMapHelper(Map, Map->ContainerPtrToValuePtr<void>(Data)).Num(), 2);
    TestEqual(TEXT("Refusal restores complete root including sibling values"),
        UP4AuthoringTools::PropertyJson(Counts.Get(), TEXT("ActorPlacedNumInfo")), Before);
    TestFalse(TEXT("Post-edit refusal restores clean package"), Package->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyPathsAutomation, "P4MapKit.Quality.PropertyPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyPathsAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UPackage* Package = CreatePackage(TEXT("/Game/NativePropertyEditTests/Paths"));
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Package, NAME_None, RF_Transient | RF_Transactional));
    Curve->FloatCurves[0].AddKey(1.f, 10.f);
    Curve->FloatCurves[1].AddKey(2.f, 20.f);
    Curve->FloatCurves[2].AddKey(3.f, 30.f);
    Package->SetDirtyFlag(false);
    TestEqual(TEXT("Shared resolver reads actual static slot/array leaf"),
        UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves[1].Keys[0].Value")), FString(TEXT("20.0")));
    TestTrue(TEXT("Static+dynamic indexed numeric edit"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys[0].Value"), TEXT("1.25e2")));
    TestEqual(TEXT("Independent native key stores scientific value"),
        Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value, 125.f);
    TestEqual(TEXT("Other static slot is preserved"), Curve->FloatCurves[0].GetConstRefOfKeys()[0].Value, 10.f);
    TestEqual(TEXT("Indexed readback agrees"),
        UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves[1].Keys[0].Value")), FString(TEXT("125.0")));
    const FString Before = UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves"));
    TestFalse(TEXT("Complete actual fixed-array readback is supported"), Before.IsEmpty());
    const TCHAR* InvalidPaths[] = {TEXT("FloatCurves..Keys"), TEXT(".FloatCurves"), TEXT("FloatCurves."),
        TEXT("FloatCurves[]"), TEXT("FloatCurves[-1]"), TEXT("FloatCurves[3]"), TEXT("FloatCurves[21474836480]"),
        TEXT("FloatCurves[1]junk"), TEXT("FloatCurves[1].Keys[2].Value"), TEXT("FloatCurves[1].Keys[0].Value.X"),
        TEXT("FloatCurves.Keys[0].Value"), TEXT("Missing")};
    for (const TCHAR* Path : InvalidPaths)
    {
        Package->SetDirtyFlag(false);
        TestFalse(FString(TEXT("Malformed path refuses: ")) + Path,
            UP4AuthoringTools::SetPropertyText(Curve.Get(), FName(Path), TEXT("7")));
        TestTrue(TEXT("Malformed path readback refuses"),
            UP4AuthoringTools::PropertyJson(Curve.Get(), FName(Path)).IsEmpty());
        TestFalse(TEXT("Malformed path does not dirty target"), Package->IsDirty());
        TestEqual(TEXT("Malformed path does not alter fixed/array data"),
            UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves")), Before);
    }
    const TCHAR* InvalidNumbers[] = {TEXT("+"), TEXT("--3"), TEXT("1.2.3"), TEXT("1e"), TEXT("1e999"), TEXT("1e-999"),
        TEXT("1e-46"), TEXT("NaN"), TEXT("125 trailing")};
    for (const TCHAR* Number : InvalidNumbers)
    {
        Package->SetDirtyFlag(false);
        TestFalse(FString(TEXT("Malformed/range numeric refuses: ")) + Number,
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys[0].Value"), Number));
        TestFalse(TEXT("Rejected numeric keeps dirty state"), Package->IsDirty());
        TestEqual(TEXT("Rejected numeric leaves source unchanged"), Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value,
            125.f);
    }
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("No-op valid edit succeeds"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys[0].Value"), TEXT("125.0")));
    TestFalse(TEXT("No-op leaves clean package"), Package->IsDirty());
    TestFalse(TEXT("Unknown struct field refuses before array publication"),
        UP4AuthoringTools::SetPropertyText(
            Curve.Get(), TEXT("FloatCurves[1].Keys"), TEXT("((Time=2,Value=7,Unknown=99))")));
    TestFalse(TEXT("Duplicate struct field refuses"),
        UP4AuthoringTools::SetPropertyText(
            Curve.Get(), TEXT("FloatCurves[1].Keys"), TEXT("((Time=2,Value=7,Value=8))")));
    TestEqual(TEXT("Rejected nested array maintains complete previous value"),
        UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves")), Before);
    TestTrue(TEXT("Nested array replacement keeps scientific float"),
        UP4AuthoringTools::SetPropertyText(
            Curve.Get(), TEXT("FloatCurves[1].Keys"), TEXT("((Time=2,Value=7e1),(Time=4,Value=8e1))")));
    TestEqual(TEXT("Engine struct array count"), Curve->FloatCurves[1].GetConstRefOfKeys().Num(), 2);
    TestEqual(TEXT("Engine second struct array value"), Curve->FloatCurves[1].GetConstRefOfKeys()[1].Value, 80.f);
    TestTrue(TEXT("Empty array edit"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys"), TEXT("()")));
    TestEqual(TEXT("Empty indexed array readback"),
        UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves[1].Keys")), FString(TEXT("[]")));
    TStrongObjectPtr<UAnimClassData> Animation(NewObject<UAnimClassData>(Package, NAME_None, RF_Transient));
    TestTrue(TEXT("Actual native map supports recursive exact numeric values"),
        UP4AuthoringTools::SetPropertyText(Animation.Get(), TEXT("GraphBlendOptions"),
            TEXT("((First,(BlendInTime=1.25e2,BlendOutTime=2e1)),(Second,(BlendInTime=3e1,BlendOutTime=4e1)))")));
    const auto& Options = Animation->GetGraphBlendOptions();
    TestEqual(TEXT("Independent native map entry count"), Options.Num(), 2);
    const FAnimGraphBlendOptions* First = Options.Find(TEXT("First"));
    const FAnimGraphBlendOptions* Second = Options.Find(TEXT("Second"));
    TestTrue(TEXT("Rehashed native map lookup retains first scientific value"),
        First && First->BlendInTime == 125.f && First->BlendOutTime == 20.f);
    TestTrue(TEXT("Rehashed native map lookup retains second scientific value"),
        Second && Second->BlendInTime == 30.f && Second->BlendOutTime == 40.f);
    const FString BeforeMap = UP4AuthoringTools::PropertyJson(Animation.Get(), TEXT("GraphBlendOptions"));
    Package->SetDirtyFlag(false);
    TestFalse(TEXT("Typed duplicate native FName map keys refuse"),
        UP4AuthoringTools::SetPropertyText(
            Animation.Get(), TEXT("GraphBlendOptions"), TEXT("((First,(BlendInTime=1)),(FIRST,(BlendInTime=2)))")));
    TestEqual(TEXT("Duplicate keys leave complete native map unchanged"),
        UP4AuthoringTools::PropertyJson(Animation.Get(), TEXT("GraphBlendOptions")), BeforeMap);
    TestFalse(TEXT("Duplicate keys leave package clean"), Package->IsDirty());
    TestFalse(TEXT("Invalid nested map numeric refuses"),
        UP4AuthoringTools::SetPropertyText(
            Animation.Get(), TEXT("GraphBlendOptions"), TEXT("((First,(BlendInTime=1e999)))")));
    TestEqual(TEXT("Rejected nested map numeric leaves native map unchanged"),
        UP4AuthoringTools::PropertyJson(Animation.Get(), TEXT("GraphBlendOptions")), BeforeMap);
    TestTrue(TEXT("Native map empty replacement succeeds"),
        UP4AuthoringTools::SetPropertyText(Animation.Get(), TEXT("GraphBlendOptions"), TEXT("()")));
    TestEqual(TEXT("Native map is actually empty"), Animation->GetGraphBlendOptions().Num(), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4EditablePropertyAutomation, "P4MapKit.Quality.EditableProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4EditablePropertyAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UPackage* Package = CreatePackage(TEXT("/Game/NativePropertyEditTests/Editable"));
    TStrongObjectPtr<UP4MaterialExportData> Asset(
        NewObject<UP4MaterialExportData>(Package, NAME_None, RF_Transactional));
    Asset->ShaderFile.FilePath = TEXT("Before.shader");
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("Editor fixture exists"), GEditor != nullptr);
    TestTrue(TEXT("Nested standard editor property transaction"),
        UP4AuthoringTools::SetEditablePropertyText(Asset.Get(), TEXT("ShaderFile.FilePath"), TEXT("\"After.shader\"")));
    TestEqual(TEXT("Independent property value"), Asset->ShaderFile.FilePath, FString(TEXT("After.shader")));
    TestTrue(TEXT("Editor edit marks package dirty"), Package->IsDirty());
    TestTrue(TEXT("Ordinary UE Undo"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores actual nested field"), Asset->ShaderFile.FilePath, FString(TEXT("Before.shader")));
    TestTrue(TEXT("Ordinary UE Redo"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo reapplies actual nested field"), Asset->ShaderFile.FilePath, FString(TEXT("After.shader")));
    Package->SetDirtyFlag(false);
    TestFalse(TEXT("Readonly original provenance cannot be user edited"),
        UP4AuthoringTools::SetEditablePropertyText(Asset.Get(), TEXT("OriginalMaterial"), TEXT("\"discarded\"")));
    TestTrue(TEXT("Readonly refusal preserves original value"), Asset->OriginalMaterial.IsEmpty());
    TestFalse(TEXT("Readonly refusal preserves clean package"), Package->IsDirty());
    TestFalse(TEXT("Trailing quoted string garbage refuses"),
        UP4AuthoringTools::SetEditablePropertyText(
            Asset.Get(), TEXT("ShaderFile.FilePath"), TEXT("\"Wrong\" trailing")));
    TestEqual(
        TEXT("Bad string does not replace valid field"), Asset->ShaderFile.FilePath, FString(TEXT("After.shader")));
    FString Embedded(TEXT("bad"));
    const TCHAR Zero = 0;
    Embedded.AppendChars(&Zero, 1);
    Embedded += TEXT("tail");
    TestFalse(TEXT("Embedded NUL string refuses"),
        UP4AuthoringTools::SetPropertyText(Asset.Get(), TEXT("ShaderFile.FilePath"), Embedded));
    TestEqual(
        TEXT("Embedded NUL refusal keeps native value"), Asset->ShaderFile.FilePath, FString(TEXT("After.shader")));
    TestTrue(TEXT("Privileged source import may populate readonly provenance"),
        UP4AuthoringTools::SetPropertyText(Asset.Get(), TEXT("OriginalMaterial"), TEXT("\"/Game/Original.Material\"")));
    TestEqual(TEXT("Quoted source string semantics preserved"), Asset->OriginalMaterial,
        FString(TEXT("/Game/Original.Material")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalPlacementMappingAutomation, "P4MapKit.Quality.OriginalPlacementMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalPlacementMappingAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UClass* ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GActor"));
    UScriptStruct* Generator = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.ActorGenerator"));
    if (!TestNotNull(TEXT("Actual recovered GActor"), ActorClass) ||
        !TestNotNull(TEXT("Actual original ActorGenerator row"), Generator))
        return false;
    FProperty* GenerateInfo = ActorClass->FindPropertyByName(TEXT("GenerateInfo"));
    FProperty* RowGenerateInfo = Generator->FindPropertyByName(TEXT("GenerateInfo"));
    TestTrue(TEXT("Independent original GenerateInfo property type matches actor field"),
        GenerateInfo && RowGenerateInfo && GenerateInfo->SameType(RowGenerateInfo));
    TestNotNull(TEXT("Original row AssetVersion exists"), Generator->FindPropertyByName(TEXT("AssetVersion")));
    TestNull(TEXT("Actor lacks row format version; it cannot supply qualified constructor defaults"),
        ActorClass->FindPropertyByName(TEXT("AssetVersion")));
    TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(EWorldType::Editor, false));
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags = RF_Transient | RF_Transactional;
    AActor* Actor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Actual original actor instantiated"), Actor))
    {
        World->DestroyWorld(false);
        return false;
    }
    const FString Verdict = UP4AuthoringTools::NativePlacementInfo(Actor);
    TSharedPtr<FJsonObject> Result;
    TestTrue(TEXT("Mapping verdict parses as native JSON"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Verdict), Result) && Result.IsValid());
    if (Result.IsValid())
    {
        TestTrue(TEXT("Mapping refuses unqualified original fields"), Result->HasField(TEXT("error")));
        TestFalse(TEXT("Refusal never publishes default-filled row"), Result->HasField(TEXT("row")));
        TestEqual(TEXT("Verified guid remains available for source-backed preset identity"),
            Result->GetStringField(TEXT("guid")), Actor->GetActorGuid().ToString(EGuidFormats::DigitsWithHyphens));
        TestTrue(TEXT("Verified original ancestors retained"), Result->GetArrayField(TEXT("ancestors")).Num() > 0);
    }
    World->DestroyWorld(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyCallbackAutomation, "P4MapKit.Quality.PropertyCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyCallbackAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UPackage* Package = CreatePackage(TEXT("/Game/NativePropertyEditTests/Callbacks"));
    // This component has no registered render state: the test changes native
    // editable storage without touching the rendering thread.
    TStrongObjectPtr<UStaticMeshComponent> Component(
        NewObject<UStaticMeshComponent>(Package, NAME_None, RF_Transactional));
    TStrongObjectPtr<UMaterial> Original(NewObject<UMaterial>(Package, NAME_None, RF_Transient));
    // The native post-edit handler removes overrides beyond the mesh's real
    // material slot count. Supply one slot so index zero is a valid editor edit.
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(Package, NAME_None, RF_Transient));
    Mesh->StaticMaterials.Add(FStaticMaterial(Original.Get()));
    TestTrue(TEXT("Native component receives actual one-slot mesh"), Component->SetStaticMesh(Mesh.Get()));
    TestEqual(TEXT("Actual native material slot count"), Component->GetNumMaterials(), 1);
    Component->OverrideMaterials.Add(Original.Get());
    UMaterial* Imported = NewObject<UMaterial>(Package, NAME_None, RF_Transient);
    TWeakObjectPtr<UMaterial> ImportedWeak(Imported);
    const FString ImportText = FString(TEXT("Material'")) + Imported->GetPathName() + TEXT("'");
    bool Called = false, StagedReferenceSurvived = false;
    const FDelegateHandle Reallocate = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Object, const FEditPropertyChain& Chain)
        {
            (void)Chain;
            if (Object != Component.Get())
                return;
            Called = true;
            Component->OverrideMaterials.Empty(256);
            Component->OverrideMaterials.Add(Original.Get());
            CollectGarbage(RF_NoFlags);
            StagedReferenceSurvived = ImportedWeak.IsValid();
        });
    const bool Edited = UP4AuthoringTools::SetEditablePropertyText(
        Component.Get(), TEXT("OverrideMaterials[0]"), ImportText);
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Reallocate);
    TestTrue(TEXT("Native pre-edit callback ran"), Called);
    TestTrue(TEXT("Staged imported UObject reference survives callback GC"), StagedReferenceSurvived);
    TestTrue(TEXT("Edit succeeds after callback reallocates destination array"), Edited);
    TestTrue(TEXT("Fresh native array receives exact imported material"),
        Component->OverrideMaterials.Num() == 1 && Component->OverrideMaterials[0] == ImportedWeak.Get());
    const FString Before = UP4AuthoringTools::PropertyJson(Component.Get(), TEXT("OverrideMaterials"));
    Package->SetDirtyFlag(false);
    TStrongObjectPtr<UMaterial> Illegal(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
    AddExpectedError(TEXT("Illegal TEXT reference to a private object in external package"),
        EAutomationExpectedErrorFlags::Contains, 1);
    const FString IllegalText = FString(TEXT("Material'")) + Illegal->GetPathName() + TEXT("'");
    TestFalse(TEXT("Rejected private hard object import refuses instead of replacing with null"),
        UP4AuthoringTools::SetEditablePropertyText(Component.Get(), TEXT("OverrideMaterials[0]"), IllegalText));
    TestEqual(TEXT("Rejected hard reference preserves actual native array"),
        UP4AuthoringTools::PropertyJson(Component.Get(), TEXT("OverrideMaterials")), Before);
    TestFalse(TEXT("Rejected hard reference preserves clean package state"), Package->IsDirty());
    const FDelegateHandle RemovePath = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Object, const FEditPropertyChain& Chain)
        {
            (void)Chain;
            if (Object == Component.Get())
            {
                Component->OverrideMaterials.Empty();
                CollectGarbage(RF_NoFlags);
            }
        });
    const bool Refused = UP4AuthoringTools::SetEditablePropertyText(
        Component.Get(), TEXT("OverrideMaterials[0]"), TEXT("None"));
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(RemovePath);
    TestFalse(TEXT("Callback removing selected array slot refuses safely"), Refused);
    TestEqual(TEXT("Refusal restores complete edited root including hard reference"),
        UP4AuthoringTools::PropertyJson(Component.Get(), TEXT("OverrideMaterials")), Before);
    TestFalse(TEXT("Callback refusal restores previous clean package state"), Package->IsDirty());
    TestTrue(TEXT("Native fixture removes mesh for invalid material-slot case"), Component->SetStaticMesh(nullptr));
    TestEqual(TEXT("No native material slots remain"), Component->GetNumMaterials(), 0);
    Component->OverrideMaterials.Empty();
    Component->OverrideMaterials.Add(ImportedWeak.Get());
    Package->SetDirtyFlag(false);
    const FString BeforeInvalidSlot = UP4AuthoringTools::PropertyJson(Component.Get(), TEXT("OverrideMaterials"));
    TestFalse(TEXT("Native post-edit removal refuses instead of claiming exact success"),
        UP4AuthoringTools::SetEditablePropertyText(Component.Get(), TEXT("OverrideMaterials[0]"), TEXT("None")));
    TestEqual(TEXT("Native post-edit removal restores whole edited root"),
        UP4AuthoringTools::PropertyJson(Component.Get(), TEXT("OverrideMaterials")), BeforeInvalidSlot);
    TestFalse(TEXT("Native post-edit refusal restores previous clean package state"), Package->IsDirty());
    return !HasAnyErrors();
}
#endif

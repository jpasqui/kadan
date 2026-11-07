// PRIVATE batch71-coordinate draft. Not compiled or executed by this worker.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Animation/AnimClassData.h"
#include "Camera/CameraShakeSourceComponent.h"
#include "Curves/CurveVector.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace P4PropertyRefusalCoverage
{
UPackage* Package()
{
    return CreatePackage(
        *(FString(TEXT("/Game/NativePropertyRefusalCoverage/")) + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}
TArray<uint8> Bytes(UObject* Object)
{
    TArray<uint8> Result;
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    return Result;
}
struct Guard
{
    UObject* Object;
    TArray<uint8> Before;
    EObjectFlags Flags;
    bool Dirty;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    explicit Guard(UObject* In)
        : Object(In), Before(Bytes(In)), Flags(In->GetFlags()), Dirty(In->GetOutermost()->IsDirty())
    {
        A = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Target, const FEditPropertyChain&)
            {
                if (Target == Object)
                    ++Pre;
            });
        B = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Target, FPropertyChangedEvent&)
            {
                if (Target == Object)
                    ++Post;
            });
    }
    ~Guard()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    }
    void Check(FAutomationTestBase& Test)
    {
        Test.TestTrue(TEXT("Complete native serialization unchanged"), Bytes(Object) == Before);
        Test.TestTrue(TEXT("Original object flags unchanged"), Object->GetFlags() == Flags);
        Test.TestEqual(TEXT("Prior dirty state retained"), Object->GetOutermost()->IsDirty(), Dirty);
        Test.TestEqual(TEXT("No pre-edit notification on refusal"), Pre, 0);
        Test.TestEqual(TEXT("No post-edit notification on refusal"), Post, 0);
    }
};
FString MapText(int32 Count)
{
    FString Out(TEXT("("));
    Out.Reserve(Count * 64 + 2);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        if (Index)
            Out += TEXT(",");
        Out += FString::Printf(TEXT("(P4RefusalEntry%d,(BlendInTime=1,BlendOutTime=2))"), Index);
    }
    Out += TEXT(")");
    return Out;
}
} //namespace P4PropertyRefusalCoverage

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyResolverProjectionRefusals,
    "P4MapKit.Quality.PropertyResolverProjectionRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyResolverProjectionRefusals::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRefusalCoverage;
    UPackage* Owner = Package();
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Owner, NAME_None, RF_Transactional));
    Curve->FloatCurves[0].AddKey(1.0f, 7.0f);
    Owner->SetDirtyFlag(false);
    {
        Guard State(Curve.Get());
        TestFalse(TEXT("Null target refuses before property access"),
            UP4AuthoringTools::SetPropertyText(nullptr, TEXT("FloatCurves"), TEXT("()")));
        TestTrue(
            TEXT("Null readback refuses"), UP4AuthoringTools::PropertyJson(nullptr, TEXT("FloatCurves")).IsEmpty());
        const bool WorkerRefused = Async(EAsyncExecution::Thread,
            [Object = Curve.Get()]()
            {
                // Both calls short-circuit at IsInGameThread; no worker property access.
                return !UP4AuthoringTools::SetPropertyText(Object, TEXT("FloatCurves[0].Keys[0].Value"), TEXT("9")) &&
                    UP4AuthoringTools::PropertyJson(Object, TEXT("FloatCurves")).IsEmpty();
            }).Get();
        TestTrue(TEXT("Wrong thread refuses before touching native object"), WorkerRefused);
        for (const TCHAR* Path : {TEXT("FloatCurves]"), TEXT("FloatCurves[1"), TEXT("FloatCurves[1a]")})
            TestFalse(TEXT("Unmatched/malformed bracket refuses"),
                UP4AuthoringTools::SetPropertyText(Curve.Get(), FName(Path), TEXT("9")));
        FString TooDeep(TEXT("FloatCurves"));
        for (int32 Index = 0; Index < 16; ++Index)
            TooDeep += TEXT(".Keys");
        TestFalse(TEXT("Seventeen source path segments refuse"),
            UP4AuthoringTools::SetPropertyText(Curve.Get(), FName(*TooDeep), TEXT("9")));
        State.Check(*this);
    }
    // A native invalid enum and a retained nonfinite struct member are actual
    // representability failures, distinct from invalid textual number inputs.
    TStrongObjectPtr<UCameraShakeSourceComponent> Camera(
        NewObject<UCameraShakeSourceComponent>(Owner, NAME_None, RF_Transactional));
    Camera->Attenuation = static_cast<ECameraShakeAttenuation>(255);
    Owner->SetDirtyFlag(false);
    {
        Guard State(Camera.Get());
        TestTrue(TEXT("Unrepresented native enumclass value refuses JSON"),
            UP4AuthoringTools::PropertyJson(Camera.Get(), TEXT("Attenuation")).IsEmpty());
        State.Check(*this);
    }
    uint32 NanBits = 0x7fc01234u;
    FMemory::Memcpy(&Curve->FloatCurves[0].Keys[0].Time, &NanBits, sizeof(NanBits));
    Owner->SetDirtyFlag(true);
    {
        Guard State(Curve.Get());
        TestTrue(TEXT("Native nonfinite leaf refuses independent projection"),
            UP4AuthoringTools::PropertyJson(Curve.Get(), TEXT("FloatCurves[0].Keys[0].Time")).IsEmpty());
        TestFalse(TEXT("Partial struct edit cannot publish while retaining nonfinite member"),
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[0].Keys[0]"), TEXT("(Value=19)")));
        uint32 Actual = 0;
        FMemory::Memcpy(&Actual, &Curve->FloatCurves[0].Keys[0].Time, sizeof(Actual));
        TestEqual(TEXT("Original nonfinite payload bits preserved, not normalized"), Actual, NanBits);
        TestEqual(TEXT("Valid sibling not partially published"), Curve->FloatCurves[0].Keys[0].Value, 7.0f);
        State.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyTextLimitRefusals, "P4MapKit.Quality.PropertyTextLimitRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyTextLimitRefusals::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRefusalCoverage;
    UPackage* Owner = Package();
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Owner, NAME_None, RF_Transactional));
    Curve->FloatCurves[0].AddKey(1.0f, 7.0f);
    Owner->SetDirtyFlag(false);
    Guard State(Curve.Get());
    for (const TCHAR* Text : {TEXT(""), TEXT("x"), TEXT("Time=1)"), TEXT("(Time=1"), TEXT("(Time)"),
             TEXT("(Time=1,,Value=2)"), TEXT("(Time=(1)")})
        TestFalse(TEXT("Malformed actual native struct text refuses"),
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[0].Keys[0]"), Text));
    FString Deep;
    for (int32 Index = 0; Index < 34; ++Index)
        Deep += TEXT("(");
    Deep += TEXT("Time=1");
    for (int32 Index = 0; Index < 34; ++Index)
        Deep += TEXT(")");
    TestFalse(TEXT("Excessive tuple nesting refuses before staged publication"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[0].Keys[0]"), Deep));
    FString TooMany(TEXT("("));
    TooMany.Reserve(200000);
    for (int32 Index = 0; Index < 65537; ++Index)
        TooMany += TEXT("(),");
    TooMany += TEXT("())");
    TestFalse(TEXT("Actual native struct array tuple-count budget refuses"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[0].Keys"), TooMany));
    TestFalse(TEXT("One-MiB text budget refuses before numeric parsing"),
        UP4AuthoringTools::SetPropertyText(
            Curve.Get(), TEXT("FloatCurves[0].Keys[0].Value"), FString::ChrN(1024 * 1024 + 1, TEXT('9'))));
    State.Check(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyMapBudgetRefusals, "P4MapKit.Quality.PropertyMapBudgetRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyMapBudgetRefusals::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRefusalCoverage;
    UPackage* Owner = Package();
    TStrongObjectPtr<UAnimClassData> Object(NewObject<UAnimClassData>(Owner, NAME_None, RF_Transactional));
    auto* Property = CastField<FMapProperty>(Object->GetClass()->FindPropertyByName(TEXT("GraphBlendOptions")));
    if (!TestNotNull(TEXT("Installed genuine FName-to-blend-struct map"), Property))
        return false;
    const EPropertyFlags Flags = Property->GetPropertyFlags();
    Object->GraphBlendOptions.FindOrAdd(FName(TEXT("Sentinel"))).BlendInTime = 17.25f;
    Object->GraphBlendOptions.FindOrAdd(FName(TEXT("Sentinel"))).BlendOutTime = 31.5f;
    Owner->SetDirtyFlag(false);
    {
        Guard State(Object.Get());
        TestFalse(TEXT("Map element without tuple wrapper refuses"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("GraphBlendOptions"), TEXT("(First)")));
        TestFalse(TEXT("Real native map entry-count limit refuses before live replacement"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("GraphBlendOptions"), MapText(4097)));
        TestFalse(TEXT("Unique native key comparison budget refuses before publication"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("GraphBlendOptions"), MapText(2897)));
        State.Check(*this);
    }
    // 2896 entries pass strict import's n*(n-1)/2 comparisons; proving identical
    // maps needs n*(n+1)/2 unmatched-key scans and exceeds the identity budget.
    // Current storage is populated through the genuine installed TMap member,
    // not a manufactured property. Unavailable must never initiate a mutation.
    Object->GraphBlendOptions.Empty();
    for (int32 Index = 0; Index < 2896; ++Index)
    {
        auto& Value = Object->GraphBlendOptions.FindOrAdd(FName(*FString::Printf(TEXT("P4RefusalEntry%d"), Index)));
        Value.BlendInTime = 1.0f;
        Value.BlendOutTime = 2.0f;
    }
    TestEqual(TEXT("Independent native baseline count"), Object->GraphBlendOptions.Num(), 2896);
    Owner->SetDirtyFlag(false);
    {
        Guard State(Object.Get());
        TestFalse(TEXT("Identity verification budget unavailable refuses unchanged native map"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("GraphBlendOptions"), MapText(2896)));
        TestEqual(TEXT("No partial publication changes native count"), Object->GraphBlendOptions.Num(), 2896);
        State.Check(*this);
    }
    TestTrue(TEXT("Native property flags remain untouched"), Property->GetPropertyFlags() == Flags);
    return !HasAnyErrors();
}
#endif

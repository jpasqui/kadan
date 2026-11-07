#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Animation/AnimClassData.h"
#include "Curves/CurveVector.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4SignedZeroAggregateTests
{
template <class Scalar, class Bits>
Bits BitsOf(Scalar Value)
{
    static_assert(sizeof(Scalar) == sizeof(Bits), "Native floating width");
    Bits Result = 0;
    FMemory::Memcpy(&Result, &Value, sizeof(Result));
    return Result;
}
template <class Scalar, class Bits>
Scalar FromBits(Bits Value)
{
    static_assert(sizeof(Scalar) == sizeof(Bits), "Native floating width");
    Scalar Result = Scalar(0);
    FMemory::Memcpy(&Result, &Value, sizeof(Result));
    return Result;
}
UPackage* Package(const TCHAR* Suffix)
{
    return CreatePackage(*(FString(TEXT("/Game/NativeSignedZeroAggregates/")) + Suffix + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}
TArray<uint8> Snapshot(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
bool Editable(FProperty* Property)
{
    return Property && Property->HasAnyPropertyFlags(CPF_Edit) &&
        !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
}
// Scoped delegates always detach, including prerequisite/refusal exits.
struct Callbacks
{
    UObject* Object;
    int32 Pre = 0;
    int32 Post = 0;
    FDelegateHandle PreHandle;
    FDelegateHandle PostHandle;
    explicit Callbacks(UObject* InObject) : Object(InObject)
    {
        PreHandle = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Target, const FEditPropertyChain&)
            {
                if (Target == Object)
                    ++Pre;
            });
        PostHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Target, FPropertyChangedEvent&)
            {
                if (Target == Object)
                    ++Post;
            });
    }
    ~Callbacks()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreHandle);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PostHandle);
    }
};
template <class Scalar, class Bits, class Field>
bool ArrayEquals(FArrayProperty* Property, UObject* Object, const TArray<Bits>& Expected)
{
    FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Object));
    Field* Inner = CastField<Field>(Property->Inner);
    if (!Inner || Array.Num() != Expected.Num())
        return false;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
        if (BitsOf<Scalar, Bits>(Inner->GetPropertyValue(Array.GetRawPtr(Index))) != Expected[Index])
            return false;
    return true;
}
template <class Scalar, class Bits, class Field>
bool CheckArray(FAutomationTestBase& Test, UClass* Class, const TCHAR* Name, const TCHAR* SiblingName, Bits Sign)
{
    auto* Property = CastField<FArrayProperty>(Class->FindPropertyByName(Name));
    auto* Sibling = CastField<Field>(Class->FindPropertyByName(SiblingName));
    if (!Test.TestNotNull(TEXT("Captured native floating array"), Property) ||
        !Test.TestNotNull(TEXT("Independent native scalar sibling"), Sibling))
        return false;
    auto* Inner = CastField<Field>(Property->Inner);
    if (!Test.TestNotNull(TEXT("Actual floating array element width"), Inner) ||
        !Test.TestTrue(TEXT("Actual array flags admit native editor edits"), Editable(Property)))
        return false;
    const EPropertyFlags Flags = Property->GetPropertyFlags();
    UPackage* Owner = Package(Name);
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    if (!Test.TestNotNull(TEXT("Native floating array owner"), Object.Get()))
        return false;
    FScriptArrayHelper Initial(Property, Property->ContainerPtrToValuePtr<void>(Object.Get()));
    Initial.EmptyValues();
    Initial.AddValues(3);
    Inner->SetPropertyValue(Initial.GetRawPtr(0), Scalar(0));
    Inner->SetPropertyValue(Initial.GetRawPtr(1), FromBits<Scalar, Bits>(Sign));
    Inner->SetPropertyValue(Initial.GetRawPtr(2), Scalar(17.5));
    Sibling->SetPropertyValue_InContainer(Object.Get(), Scalar(37.25));
    const TArray<Bits> Before{Bits(0), Sign, BitsOf<Scalar, Bits>(Scalar(17.5))};
    const TArray<Bits> Negative{Sign, Sign, BitsOf<Scalar, Bits>(Scalar(17.5))};
    const TArray<uint8> Original = Snapshot(Object.Get());
    Owner->SetDirtyFlag(false);
    Callbacks Events(Object.Get());
    const bool Changed = UP4AuthoringTools::SetEditablePropertyText(
        Object.Get(), FName(Name), TEXT("(-0.0,-0.0,17.5)"));
    Test.TestTrue(TEXT("Whole array signed-zero edit succeeds"), Changed);
    Test.TestTrue(TEXT("All whole-array native element bits are exact"),
        ArrayEquals<Scalar, Bits, Field>(Property, Object.Get(), Negative));
    Test.TestEqual(TEXT("Whole array edit has one pre callback"), Events.Pre, 1);
    Test.TestEqual(TEXT("Whole array edit has one post callback"), Events.Post, 1);
    Test.TestTrue(TEXT("Whole array sign edit dirties package"), Owner->IsDirty());
    Test.TestTrue(TEXT("Floating scalar sibling remains bit-exact"),
        BitsOf<Scalar, Bits>(Sibling->GetPropertyValue_InContainer(Object.Get())) ==
            BitsOf<Scalar, Bits>(Scalar(37.25)));
    // Only undo a transaction this test actually observed committing.
    if (!Changed || !ArrayEquals<Scalar, Bits, Field>(Property, Object.Get(), Negative) || Events.Pre != 1 ||
        Events.Post != 1 || !Owner->IsDirty())
        return false;
    Test.TestTrue(TEXT("Whole array sign edit Undo"), GEditor->UndoTransaction());
    Test.TestTrue(TEXT("Undo restores complete native object"), Snapshot(Object.Get()) == Original);
    Test.TestTrue(TEXT("Whole array sign edit Redo"), GEditor->RedoTransaction());
    Test.TestTrue(TEXT("Redo restores all native element bits"),
        ArrayEquals<Scalar, Bits, Field>(Property, Object.Get(), Negative));

    Events.Pre = 0;
    Events.Post = 0;
    Owner->SetDirtyFlag(false);
    const FString Indexed = FString(Name) + TEXT("[0]");
    const bool IndexedChanged = UP4AuthoringTools::SetEditablePropertyText(Object.Get(), FName(*Indexed), TEXT("0.0"));
    Test.TestTrue(TEXT("Indexed sign-bit edit succeeds"), IndexedChanged);
    Test.TestTrue(TEXT("Indexed sign edit preserves every sibling bit"),
        ArrayEquals<Scalar, Bits, Field>(Property, Object.Get(), Before));
    Test.TestEqual(TEXT("Indexed sign edit has one pre callback"), Events.Pre, 1);
    Test.TestEqual(TEXT("Indexed sign edit has one post callback"), Events.Post, 1);
    if (!IndexedChanged || !ArrayEquals<Scalar, Bits, Field>(Property, Object.Get(), Before) || Events.Pre != 1 ||
        Events.Post != 1 || !Owner->IsDirty())
        return false;
    const TArray<uint8> NoOpState = Snapshot(Object.Get());
    Events.Pre = 0;
    Events.Post = 0;
    Owner->SetDirtyFlag(false);
    Test.TestTrue(TEXT("Exact whole-array request succeeds as no-op"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), FName(Name), TEXT("(0.0,-0.0,17.5)")));
    Test.TestTrue(TEXT("No-op preserves whole native object"), Snapshot(Object.Get()) == NoOpState);
    Test.TestTrue(TEXT("No-op keeps package clean and sends no callbacks"),
        !Owner->IsDirty() && Events.Pre == 0 && Events.Post == 0);
    Test.TestTrue(TEXT("Undo bypasses exact array no-op"), GEditor->UndoTransaction());
    Test.TestTrue(
        TEXT("No-op did not insert an Undo entry"), ArrayEquals<Scalar, Bits, Field>(Property, Object.Get(), Negative));
    Test.TestTrue(TEXT("No native array metadata changed"), Property->GetPropertyFlags() == Flags);
    return !Test.HasAnyErrors();
}
void MapAdd(FMapProperty* Property, UObject* Object, int64 Key, uint64 Value)
{
    FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Object));
    const int32 Index = Map.AddDefaultValue_Invalid_NeedsRehash();
    CastFieldChecked<FEnumProperty>(Property->KeyProp)
        ->GetUnderlyingProperty()
        ->SetIntPropertyValue(Map.GetKeyPtr(Index), Key);
    CastFieldChecked<FDoubleProperty>(Property->ValueProp)
        ->SetPropertyValue(Map.GetValuePtr(Index), FromBits<double, uint64>(Value));
    Map.Rehash();
}
bool MapEquals(FMapProperty* Property, UObject* Object, const TMap<int64, uint64>& Expected)
{
    FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Object));
    auto* Key = CastField<FEnumProperty>(Property->KeyProp);
    auto* Value = CastField<FDoubleProperty>(Property->ValueProp);
    if (!Key || !Value || Map.Num() != Expected.Num())
        return false;
    for (int32 Index = 0; Index < Map.GetMaxIndex(); ++Index)
    {
        if (!Map.IsValidIndex(Index))
            continue;
        const uint64* Found = Expected.Find(
            Key->GetUnderlyingProperty()->GetSignedIntPropertyValue(Map.GetKeyPtr(Index)));
        if (!Found || *Found != BitsOf<double, uint64>(Value->GetPropertyValue(Map.GetValuePtr(Index))))
            return false;
    }
    return true;
}
} //namespace P4SignedZeroAggregateTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertySignedZeroArrays, "P4MapKit.Quality.PropertySignedZeroArrays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertySignedZeroArrays::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Story"));
    if (!TestNotNull(TEXT("Captured original array owner"), Class) || !TestNotNull(TEXT("Native Undo owner"), GEditor))
        return false;
    const bool Float = P4SignedZeroAggregateTests::CheckArray<float, uint32, FFloatProperty>(
        *this, Class, TEXT("LevelTimers"), TEXT("StartCaveExploreRate"), uint32(0x80000000u));
    const bool Double = P4SignedZeroAggregateTests::CheckArray<double, uint64, FDoubleProperty>(
        *this, Class, TEXT("DifficultyPlayTime"), TEXT("MultiPlayTime"), uint64(0x8000000000000000ULL));
    return Float && Double && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertySignedZeroFixedStructs, "P4MapKit.Quality.PropertySignedZeroFixedStructs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertySignedZeroFixedStructs::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4SignedZeroAggregateTests;
    const uint32 Sign = uint32(0x80000000u);
    UPackage* Owner = Package(TEXT("FixedStructs"));
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Owner));
    auto* Fixed = CastField<FStructProperty>(UCurveVector::StaticClass()->FindPropertyByName(TEXT("FloatCurves")));
    if (!TestNotNull(TEXT("Actual fixed native curve struct property"), Fixed) ||
        !TestEqual(TEXT("Actual native fixed array extent"), Fixed->ArrayDim, 3))
        return false;
    TestFalse(TEXT("Installed curve root is readonly to editor service"), Fixed->HasAnyPropertyFlags(CPF_Edit));
    const FString Whole(
        TEXT("((Keys=((Time=1,Value=-0.0))),(Keys=((Time=2,Value=0.0))),(Keys=((Time=3,Value=-0.0))))"));
    if (!TestTrue(TEXT("Privileged whole fixed array preserves zero signs"),
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves"), Whole)))
        return false;
    for (int32 Channel = 0; Channel < 3; ++Channel)
    {
        const TArray<FRichCurveKey>& Keys = Curve->FloatCurves[Channel].GetConstRefOfKeys();
        if (!TestEqual(TEXT("Every fixed native channel has its source key"), Keys.Num(), 1))
            return false;
        TestTrue(TEXT("Whole fixed-array struct carries exact zero sign"),
            BitsOf<float, uint32>(Keys[0].Value) == (Channel == 1 ? uint32(0) : Sign));
    }
    TestTrue(TEXT("Indexed fixed struct imports nested zero sign"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1]"), TEXT("(Keys=((Time=2,Value=-0.0)))")));
    if (!TestEqual(
            TEXT("Selected fixed channel still has one key"), Curve->FloatCurves[1].GetConstRefOfKeys().Num(), 1))
        return false;
    TestTrue(TEXT("Selected native channel now has negative-zero bits"),
        BitsOf<float, uint32>(Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value) == Sign);
    TestTrue(TEXT("Nested dynamic keys array changes selected sign"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys"), TEXT("((Time=2,Value=0.0))")));
    if (!TestEqual(TEXT("Nested array replacement keeps one key"), Curve->FloatCurves[1].GetConstRefOfKeys().Num(), 1))
        return false;
    TestTrue(TEXT("Nested array stores exact positive-zero bits"),
        BitsOf<float, uint32>(Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value) == uint32(0));
    TestTrue(TEXT("Indexed key struct changes sign without other fields"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys[0]"), TEXT("(Value=-0.0)")));
    TestTrue(TEXT("Nested struct sign and time remain exact"),
        BitsOf<float, uint32>(Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value) == Sign &&
            BitsOf<float, uint32>(Curve->FloatCurves[1].GetConstRefOfKeys()[0].Time) == BitsOf<float, uint32>(2.f));
    for (const int32 Channel : {0, 2})
        TestTrue(TEXT("Other fixed-array channels retain exact negative signs"),
            BitsOf<float, uint32>(Curve->FloatCurves[Channel].GetConstRefOfKeys()[0].Value) == Sign);
    const TArray<uint8> FixedBefore = Snapshot(Curve.Get());
    Owner->SetDirtyFlag(false);
    TestTrue(TEXT("Exact nested scalar request is a no-op"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys[0].Value"), TEXT("-0.0")));
    TestTrue(TEXT("Nested native no-op preserves complete fixed curves and clean package"),
        !Owner->IsDirty() && Snapshot(Curve.Get()) == FixedBefore);
    TestFalse(TEXT("Readonly fixed root still refuses editor mutation"),
        UP4AuthoringTools::SetEditablePropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys[0].Value"), TEXT("0.0")));
    TestTrue(TEXT("Readonly refusal preserves whole fixed native state"),
        Snapshot(Curve.Get()) == FixedBefore && !Owner->IsDirty());

    TStrongObjectPtr<UAnimClassData> Animation(NewObject<UAnimClassData>(Owner));
    auto* MapProperty = CastField<FMapProperty>(
        UAnimClassData::StaticClass()->FindPropertyByName(TEXT("GraphBlendOptions")));
    if (!TestNotNull(TEXT("Actual native name-to-blend-struct map"), MapProperty))
        return false;
    TestFalse(TEXT("Native blend root remains readonly to editor service"), MapProperty->HasAnyPropertyFlags(CPF_Edit));
    const FString MapText(
        TEXT("((First,(BlendInTime=-0.0,BlendOutTime=7)),(Second,(BlendInTime=0.0,BlendOutTime=4)))"));
    if (!TestTrue(TEXT("Nested map struct imports zero signs"),
            UP4AuthoringTools::SetPropertyText(Animation.Get(), TEXT("GraphBlendOptions"), MapText)))
        return false;
    const FAnimGraphBlendOptions* First = Animation->GetGraphBlendOptions().Find(FName(TEXT("First")));
    const FAnimGraphBlendOptions* Second = Animation->GetGraphBlendOptions().Find(FName(TEXT("Second")));
    if (!TestNotNull(TEXT("First actual blend struct"), First) ||
        !TestNotNull(TEXT("Second actual blend struct"), Second))
        return false;
    TestTrue(TEXT("Native map struct zero signs are exact"),
        BitsOf<float, uint32>(First->BlendInTime) == Sign && BitsOf<float, uint32>(Second->BlendInTime) == uint32(0));
    const TArray<uint8> MapBefore = Snapshot(Animation.Get());
    Owner->SetDirtyFlag(false);
    Callbacks Events(Animation.Get());
    TestTrue(TEXT("Native map permutation remains no-op"),
        UP4AuthoringTools::SetPropertyText(Animation.Get(), TEXT("GraphBlendOptions"),
            TEXT("((Second,(BlendInTime=0.0,BlendOutTime=4)),(First,(BlendInTime=-0.0,BlendOutTime=7)))")));
    TestTrue(TEXT("Permutation preserves whole native map byte state"), Snapshot(Animation.Get()) == MapBefore);
    TestTrue(TEXT("Permutation of signed nested values causes no callback or dirty state"),
        Events.Pre == 0 && Events.Post == 0 && !Owner->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertySignedZeroMapTransactions,
    "P4MapKit.Quality.PropertySignedZeroMapTransactions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertySignedZeroMapTransactions::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4SignedZeroAggregateTests;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Config"));
    if (!TestNotNull(TEXT("Captured original double-map owner"), Class) ||
        !TestNotNull(TEXT("Native transaction owner"), GEditor))
        return false;
    auto* Property = CastField<FMapProperty>(Class->FindPropertyByName(TEXT("ConfigDoubles")));
    if (!TestNotNull(TEXT("Captured native double map"), Property))
        return false;
    auto* Key = CastField<FEnumProperty>(Property->KeyProp);
    auto* Value = CastField<FDoubleProperty>(Property->ValueProp);
    if (!TestNotNull(TEXT("Captured native enum key"), Key) ||
        !TestNotNull(TEXT("Captured native double value"), Value) ||
        !TestTrue(TEXT("Captured map genuinely admits editor edits"), Editable(Property)))
        return false;
    UEnum* Enum = Key->GetEnum();
    if (!TestNotNull(TEXT("Captured key enum"), Enum))
        return false;
    const int64 FirstKey = Enum->GetValueByNameString(TEXT("EConfigDoubleType::PlayTime"));
    const int64 SecondKey = Enum->GetValueByNameString(TEXT("EConfigDoubleType::MultiPlayTime"));
    if (!TestTrue(TEXT("Original enum fixture identities exist and differ"),
            FirstKey != INDEX_NONE && SecondKey != INDEX_NONE && FirstKey != SecondKey))
        return false;
    const EPropertyFlags Flags = Property->GetPropertyFlags();
    const uint64 Sign = uint64(0x8000000000000000ULL);
    const uint64 Sentinel = BitsOf<double, uint64>(17.5);
    UPackage* Owner = Package(TEXT("MapTransactions"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    if (!TestNotNull(TEXT("Native transactional double-map instance"), Object.Get()))
        return false;
    FScriptMapHelper Initial(Property, Property->ContainerPtrToValuePtr<void>(Object.Get()));
    Initial.EmptyValues();
    MapAdd(Property, Object.Get(), FirstKey, uint64(0));
    MapAdd(Property, Object.Get(), SecondKey, Sentinel);
    const TMap<int64, uint64> Positive{{FirstKey, uint64(0)}, {SecondKey, Sentinel}};
    const TMap<int64, uint64> Negative{{FirstKey, Sign}, {SecondKey, Sentinel}};
    const TArray<uint8> Original = Snapshot(Object.Get());
    const FString NegativeText(TEXT("((EConfigDoubleType::PlayTime,-0.0),(EConfigDoubleType::MultiPlayTime,17.5))"));
    const FString PositiveText(TEXT("((EConfigDoubleType::PlayTime,0.0),(EConfigDoubleType::MultiPlayTime,17.5))"));
    Owner->SetDirtyFlag(false);
    Callbacks Events(Object.Get());
    const bool Changed = UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("ConfigDoubles"), NegativeText);
    TestTrue(TEXT("Native map sign-bit edit commits"), Changed);
    TestTrue(TEXT("Native map signs and sibling bits are exact"), MapEquals(Property, Object.Get(), Negative));
    TestTrue(TEXT("Sign-bit map commit sends callbacks and dirties"),
        Events.Pre == 1 && Events.Post == 1 && Owner->IsDirty());
    if (!Changed || !MapEquals(Property, Object.Get(), Negative) || Events.Pre != 1 || Events.Post != 1 ||
        !Owner->IsDirty())
        return false;
    TestTrue(TEXT("Native map signed-zero Undo"), GEditor->UndoTransaction());
    TestTrue(TEXT("Map Undo restores complete native object"),
        Snapshot(Object.Get()) == Original && MapEquals(Property, Object.Get(), Positive));
    TestTrue(TEXT("Native map signed-zero Redo"), GEditor->RedoTransaction());
    TestTrue(TEXT("Map Redo retains key-associated signed bits"), MapEquals(Property, Object.Get(), Negative));
    const TArray<uint8> NegativeState = Snapshot(Object.Get());
    Events.Pre = 0;
    Events.Post = 0;
    Owner->SetDirtyFlag(false);
    TestTrue(TEXT("Equivalent signed map permutation succeeds"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("ConfigDoubles"),
            TEXT("((EConfigDoubleType::MultiPlayTime,17.5),(EConfigDoubleType::PlayTime,-0.0))")));
    TestTrue(TEXT("Permutation is whole-state no-op with native key association"),
        Snapshot(Object.Get()) == NegativeState && MapEquals(Property, Object.Get(), Negative));
    TestTrue(TEXT("Permutation sends no callbacks and keeps package clean"),
        Events.Pre == 0 && Events.Post == 0 && !Owner->IsDirty());

    Events.Pre = 0;
    Events.Post = 0;
    const bool PositiveChanged = UP4AuthoringTools::SetEditablePropertyText(
        Object.Get(), TEXT("ConfigDoubles"), PositiveText);
    TestTrue(TEXT("Native opposite sign edit commits before callback refusals"), PositiveChanged);
    if (!PositiveChanged || !MapEquals(Property, Object.Get(), Positive) || Events.Pre != 1 || Events.Post != 1 ||
        !Owner->IsDirty())
        return false;
    const TArray<uint8> PositiveState = Snapshot(Object.Get());
    // First change ONLY the requested sign, isolating the final comparison bug.
    // Then also corrupt a sibling in the selected root to check full rollback.
    for (const bool CorruptSibling : {false, true})
    {
        bool SawRequestedNegative = false;
        const FDelegateHandle Normalize = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [&](UObject* Target, FPropertyChangedEvent&)
            {
                if (Target != Object.Get())
                    return;
                SawRequestedNegative = MapEquals(Property, Object.Get(), Negative);
                FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Target));
                for (int32 Index = 0; Index < Map.GetMaxIndex(); ++Index)
                {
                    if (!Map.IsValidIndex(Index))
                        continue;
                    const int64 NativeKey = Key->GetUnderlyingProperty()->GetSignedIntPropertyValue(
                        Map.GetKeyPtr(Index));
                    if (NativeKey == FirstKey)
                        Value->SetPropertyValue(Map.GetValuePtr(Index), 0.0);
                    else if (CorruptSibling)
                        Value->SetPropertyValue(Map.GetValuePtr(Index), 99.0);
                }
            });
        Events.Pre = 0;
        Events.Post = 0;
        Owner->SetDirtyFlag(false);
        const bool Accepted = UP4AuthoringTools::SetEditablePropertyText(
            Object.Get(), TEXT("ConfigDoubles"), NegativeText);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Normalize);
        TestFalse(TEXT("Callback negative-to-positive normalization refuses publication"), Accepted);
        TestTrue(TEXT("Callback observed requested exact negative sign before normalization"), SawRequestedNegative);
        TestTrue(TEXT("Refusal restores complete selected root and every other native field"),
            Snapshot(Object.Get()) == PositiveState && MapEquals(Property, Object.Get(), Positive));
        TestTrue(TEXT("Refusal restores prior clean package state"), !Owner->IsDirty());
        TestTrue(TEXT("Callback refusal ran actual native edit callbacks once"), Events.Pre == 1 && Events.Post == 1);
        if (Accepted || !SawRequestedNegative || Snapshot(Object.Get()) != PositiveState || Owner->IsDirty())
            return false;
    }
    TestTrue(TEXT("Undo bypasses refused callback edit and permutation no-op"), GEditor->UndoTransaction());
    TestTrue(TEXT("Failed callback transaction has no Undo entry"),
        Snapshot(Object.Get()) == NegativeState && MapEquals(Property, Object.Get(), Negative));
    TestTrue(TEXT("Captured native map flags never changed"), Property->GetPropertyFlags() == Flags);
    return !HasAnyErrors();
}
#endif

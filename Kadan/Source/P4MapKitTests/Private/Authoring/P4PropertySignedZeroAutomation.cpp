// Native signed-zero regression: the first execution exposed a false no-op
// for both floating widths. Keep the independent raw-bit and Undo oracles.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4PropertySignedZeroDraft
{
template <class Scalar, class Bits>
Bits RawBits(Scalar Value)
{
    static_assert(sizeof(Scalar) == sizeof(Bits), "Exact native width required");
    Bits Result = 0;
    FMemory::Memcpy(&Result, &Value, sizeof(Result));
    return Result;
}

template <class Scalar, class Bits, class Field>
bool Check(FAutomationTestBase& Test, UClass* Class, const TCHAR* FieldName, const TCHAR* SiblingName, Bits SignMask)
{
    auto* Property = CastField<Field>(Class->FindPropertyByName(FieldName));
    auto* Sibling = CastField<Field>(Class->FindPropertyByName(SiblingName));
    if (!Test.TestNotNull(TEXT("Captured floating property"), Property) ||
        !Test.TestNotNull(TEXT("Captured independent floating sibling"), Sibling))
        return false;
    if (!Test.TestTrue(TEXT("Captured scalar genuinely editable without changing metadata"),
            Property->HasAnyPropertyFlags(CPF_Edit) &&
                !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance)))
        return false;
    const EPropertyFlags Flags = Property->GetPropertyFlags();
    UPackage* Package = CreatePackage(*(FString(TEXT("/Game/NativePropertySignedZeroDraft/")) + FieldName + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Package, Class, NAME_None, RF_Transactional));
    Property->SetPropertyValue_InContainer(Object.Get(), Scalar(0));
    Sibling->SetPropertyValue_InContainer(Object.Get(), Scalar(37.25));
    Test.TestTrue(TEXT("Literal starting positive-zero bits"),
        RawBits<Scalar, Bits>(Property->GetPropertyValue_InContainer(Object.Get())) == Bits(0));

    Scalar NativeControl = Scalar(1);
    const TCHAR* End = Property->ImportText(TEXT("-0.0"), &NativeControl, PPF_None, Object.Get(), nullptr);
    if (!Test.TestTrue(TEXT("Installed native lexical negative-zero control"),
            End && !*End && RawBits<Scalar, Bits>(NativeControl) == SignMask))
        return false;

    int32 Pre = 0, Post = 0;
    const FDelegateHandle PreHandle = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Target, const FEditPropertyChain&)
        {
            if (Target == Object.Get())
                ++Pre;
        });
    const FDelegateHandle PostHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [&](UObject* Target, FPropertyChangedEvent&)
        {
            if (Target == Object.Get())
                ++Post;
        });
    Package->SetDirtyFlag(false);
    const bool Edited = UP4AuthoringTools::SetEditablePropertyText(Object.Get(), FName(FieldName), TEXT("-0.0"));
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreHandle);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PostHandle);
    const Bits Actual = RawBits<Scalar, Bits>(Property->GetPropertyValue_InContainer(Object.Get()));
    Test.TestTrue(TEXT("Finite native negative-zero edit succeeds"), Edited);
    Test.TestTrue(FString::Printf(TEXT("%s signed-zero exact bits: expected %llu, observed %llu"), FieldName,
                      static_cast<unsigned long long>(SignMask), static_cast<unsigned long long>(Actual)),
        Actual == SignMask);
    Test.TestEqual(TEXT("A sign-bit change issues one pre callback"), Pre, 1);
    Test.TestEqual(TEXT("A sign-bit change issues one post callback"), Post, 1);
    Test.TestTrue(TEXT("A sign-bit change dirties package"), Package->IsDirty());
    Test.TestTrue(TEXT("Signed-zero edit keeps sibling bits"),
        RawBits<Scalar, Bits>(Sibling->GetPropertyValue_InContainer(Object.Get())) ==
            RawBits<Scalar, Bits>(Scalar(37.25)));
    Test.TestTrue(TEXT("Signed-zero edit keeps native field flags"), Property->GetPropertyFlags() == Flags);
    // Never Undo a previous test's transaction if this candidate falsely
    // reported a no-op or refused. The failing observations above remain.
    if (!Edited || Actual != SignMask || Pre != 1 || Post != 1 || !Package->IsDirty())
        return false;
    Test.TestTrue(TEXT("Signed-zero Undo succeeds"), GEditor->UndoTransaction());
    Test.TestTrue(TEXT("Undo restores positive-zero bits"),
        RawBits<Scalar, Bits>(Property->GetPropertyValue_InContainer(Object.Get())) == Bits(0));
    Test.TestTrue(TEXT("Signed-zero Redo succeeds"), GEditor->RedoTransaction());
    Test.TestTrue(TEXT("Redo restores negative-zero bits"),
        RawBits<Scalar, Bits>(Property->GetPropertyValue_InContainer(Object.Get())) == SignMask);

    Pre = 0;
    Post = 0;
    const FDelegateHandle NoOpPre = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Target, const FEditPropertyChain&)
        {
            if (Target == Object.Get())
                ++Pre;
        });
    const FDelegateHandle NoOpPost = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [&](UObject* Target, FPropertyChangedEvent&)
        {
            if (Target == Object.Get())
                ++Post;
        });
    Package->SetDirtyFlag(false);
    Test.TestTrue(TEXT("Same negative-zero request succeeds"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), FName(FieldName), TEXT("-0.0")));
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(NoOpPre);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(NoOpPost);
    Test.TestEqual(TEXT("Exact signed-zero no-op has no pre callback"), Pre, 0);
    Test.TestEqual(TEXT("Exact signed-zero no-op has no post callback"), Post, 0);
    Test.TestFalse(TEXT("Exact signed-zero no-op keeps clean package"), Package->IsDirty());
    Test.TestTrue(TEXT("Exact signed-zero no-op keeps sign bit"),
        RawBits<Scalar, Bits>(Property->GetPropertyValue_InContainer(Object.Get())) == SignMask);
    Test.TestTrue(TEXT("Undo bypasses exact signed-zero no-op"), GEditor->UndoTransaction());
    Test.TestTrue(TEXT("No-op has no spurious Undo entry"),
        RawBits<Scalar, Bits>(Property->GetPropertyValue_InContainer(Object.Get())) == Bits(0));
    return !Test.HasAnyErrors();
}
} //namespace P4PropertySignedZeroDraft

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertySignedZeroDraft, "P4MapKit.Quality.PropertySignedZeroDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertySignedZeroDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Story"));
    if (!TestNotNull(TEXT("Captured original floating owner"), Class) ||
        !TestNotNull(TEXT("Native Undo owner"), GEditor))
        return false;
    // Run both storage widths even if the first uncovers the anticipated no-op
    // bug; each uses its own object/package and never changes captured defaults.
    const bool Float = P4PropertySignedZeroDraft::Check<float, uint32, FFloatProperty>(
        *this, Class, TEXT("DayStartExploreRate"), TEXT("StartCaveExploreRate"), uint32(0x80000000u));
    const bool Double = P4PropertySignedZeroDraft::Check<double, uint64, FDoubleProperty>(
        *this, Class, TEXT("PlayTime"), TEXT("MultiPlayTime"), uint64(0x8000000000000000ULL));
    return Float && Double && !HasAnyErrors();
}
#endif

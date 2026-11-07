#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimClassData.h"
#include "Curves/CurveVector.h"
#include "Engine/DataTable.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4PropertyContainerBoundaryTests
{
UPackage* Package(const TCHAR* Case)
{
    return CreatePackage(*(FString(TEXT("/Game/NativePropertyContainerBoundary/")) + Case + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}
TArray<uint8> Bytes(UObject* Object)
{
    TArray<uint8> Result;
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    return Result;
}
struct FGuard
{
    UObject* Object;
    TArray<uint8> Before;
    EObjectFlags Flags;
    bool Dirty;
    int32 Pre = 0, Post = 0;
    FDelegateHandle PreHandle, PostHandle;
    explicit FGuard(UObject* In)
        : Object(In), Before(Bytes(In)), Flags(In->GetFlags()), Dirty(In->GetOutermost()->IsDirty())
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
    ~FGuard()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreHandle);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PostHandle);
    }
    void Check(FAutomationTestBase& Test) const
    {
        Test.TestTrue(TEXT("Complete native serialization stays unchanged"), Bytes(Object) == Before);
        Test.TestTrue(TEXT("Original object flags stay unchanged"), Object->GetFlags() == Flags);
        Test.TestEqual(
            TEXT("Prior owning-package dirty state stays unchanged"), Object->GetOutermost()->IsDirty(), Dirty);
        Test.TestEqual(TEXT("No pre-edit callback for no-op or refusal"), Pre, 0);
        Test.TestEqual(TEXT("No post-edit callback for no-op or refusal"), Post, 0);
    }
};
void FillSparse(UAnimClassData* Object, int32 Holes)
{
    auto& Map = Object->GraphBlendOptions;
    // Engine-authored native numbered FNames avoid adding a new plain name
    // per sparse slot. Every inserted key is distinct in actual TMap storage.
    for (int32 Index = 0; Index < Holes; ++Index)
        Map.FindOrAdd(FName(TEXT("P4RemovedSlot"), Index + 1)).BlendInTime = float(Index);
    Map.FindOrAdd(TEXT("AFirst")).BlendInTime = 11.0f;
    Map.FindChecked(TEXT("AFirst")).BlendOutTime = 12.0f;
    Map.FindOrAdd(TEXT("ZLast")).BlendInTime = 21.0f;
    Map.FindChecked(TEXT("ZLast")).BlendOutTime = 22.0f;
    for (int32 Index = 0; Index < Holes; ++Index)
        Map.Remove(FName(TEXT("P4RemovedSlot"), Index + 1));
}
bool SparseValues(UAnimClassData* Object)
{
    const auto& Map = Object->GraphBlendOptions;
    const auto* A = Map.Find(TEXT("AFirst"));
    const auto* Z = Map.Find(TEXT("ZLast"));
    return Map.Num() == 2 && A && Z && A->BlendInTime == 11.0f && A->BlendOutTime == 12.0f && Z->BlendInTime == 21.0f &&
        Z->BlendOutTime == 22.0f;
}
} //namespace P4PropertyContainerBoundaryTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertySparseIdentityBoundary, "P4MapKit.Quality.PropertySparseIdentityBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertySparseIdentityBoundary::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyContainerBoundaryTests;
    UPackage* Owner = Package(TEXT("SparseIdentity"));
    auto* Property = CastField<FMapProperty>(
        UAnimClassData::StaticClass()->FindPropertyByName(TEXT("GraphBlendOptions")));
    if (!TestNotNull(TEXT("Installed native blend-map property"), Property))
        return false;
    const EPropertyFlags Flags = Property->GetPropertyFlags();
    const FString Permutation(
        TEXT("((ZLast,(BlendInTime=21,BlendOutTime=22)),(AFirst,(BlendInTime=11,BlendOutTime=12)))"));
    for (const int32 Holes : {32, 65537})
    {
        TStrongObjectPtr<UAnimClassData> Object(NewObject<UAnimClassData>(Owner, NAME_None, RF_Transactional));
        FillSparse(Object.Get(), Holes);
        FScriptMapHelper Storage(Property, Property->ContainerPtrToValuePtr<void>(Object.Get()));
        if (!TestTrue(TEXT("Real native removals retain sparse slots without fabricated metadata"),
                SparseValues(Object.Get()) && Storage.GetMaxIndex() == Holes + 2 &&
                    Storage.GetMaxIndex() > Storage.Num()))
            return false;
        if (!TestFalse(TEXT("Finite surviving map remains fully representable before edit"),
                UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("GraphBlendOptions")).IsEmpty()))
            return false;
        Owner->SetDirtyFlag(false);
        FGuard State(Object.Get());
        const bool Accepted = UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("GraphBlendOptions"), Permutation);
        TestEqual(
            TEXT("Equivalent native sparse map admits bounded scan and refuses excessive scan"), Accepted, Holes == 32);
        TestTrue(TEXT("Every surviving key retains its independently read native value"), SparseValues(Object.Get()));
        TestEqual(TEXT("No-op or refusal preserves native sparse storage maximum"), Storage.GetMaxIndex(), Holes + 2);
        State.Check(*this);
    }
    TestTrue(TEXT("Native map metadata and flags were never changed"), Property->GetPropertyFlags() == Flags);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyRetainedArrayIdentityBoundary,
    "P4MapKit.Quality.PropertyRetainedArrayIdentityBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyRetainedArrayIdentityBoundary::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyContainerBoundaryTests;
    UPackage* Owner = Package(TEXT("RetainedArray"));
    auto* Root = CastField<FStructProperty>(UCurveVector::StaticClass()->FindPropertyByName(TEXT("FloatCurves")));
    auto* Keys = Root ? CastField<FArrayProperty>(Root->Struct->FindPropertyByName(TEXT("Keys"))) : nullptr;
    if (!TestNotNull(TEXT("Installed native fixed curve property"), Root) ||
        !TestNotNull(TEXT("Installed native curve-key array"), Keys))
        return false;
    const EPropertyFlags RootFlags = Root->GetPropertyFlags(), KeyFlags = Keys->GetPropertyFlags();
    for (const int32 Count : {4, 65536})
    {
        TStrongObjectPtr<UCurveVector> Object(NewObject<UCurveVector>(Owner, NAME_None, RF_Transactional));
        auto& NativeKeys = Object->FloatCurves[0].Keys;
        NativeKeys.Reserve(Count);
        // Real engine constructor initializes every enum, tangent and weight;
        // all numbers stay finite. No reflected schema or property flag edits.
        for (int32 Index = 0; Index < Count; ++Index)
            NativeKeys.Emplace(float(Index), float(Index + 10));
        Object->FloatCurves[1].AddKey(1.0f, 31.0f);
        Object->FloatCurves[2].AddKey(2.0f, 41.0f);
        if (!TestFalse(TEXT("Large retained source array is representable before identity budgeting"),
                UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("FloatCurves[0]")).IsEmpty()))
            return false;
        Owner->SetDirtyFlag(true);
        FGuard State(Object.Get());
        // A legitimate partial native struct edit retains every key. This tiny
        // request is below all text/tuple limits; refusal must protect complete
        // retained data when exact identity traversal cannot stay bounded.
        const bool Accepted = UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("FloatCurves[0]"), TEXT("()"));
        TestEqual(TEXT("Retained-array no-op admits bounded source and refuses excessive identity traversal"), Accepted,
            Count == 4);
        TestEqual(TEXT("Every retained key remains present"), NativeKeys.Num(), Count);
        bool Equal = NativeKeys.Num() == Count;
        for (int32 Index = 0; Index < NativeKeys.Num(); ++Index)
            Equal = Equal && NativeKeys[Index].Time == float(Index) && NativeKeys[Index].Value == float(Index + 10);
        TestTrue(TEXT("Every native key preserves exact independently authored time and value"), Equal);
        TestTrue(TEXT("Other fixed channels retain their native keys"),
            Object->FloatCurves[1].GetConstRefOfKeys().Num() == 1 &&
                Object->FloatCurves[1].GetConstRefOfKeys()[0].Value == 31.0f &&
                Object->FloatCurves[2].GetConstRefOfKeys().Num() == 1 &&
                Object->FloatCurves[2].GetConstRefOfKeys()[0].Value == 41.0f);
        State.Check(*this);
    }
    TestTrue(TEXT("Both native property flag sets stay unchanged"),
        Root->GetPropertyFlags() == RootFlags && Keys->GetPropertyFlags() == KeyFlags);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNestedNativeImportBoundary,
    "P4MapKit.Quality.PropertyNestedNativeImportRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNestedNativeImportBoundary::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyContainerBoundaryTests;
    UPackage* Owner = Package(TEXT("NestedNativeImport"));
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Owner, NAME_None, RF_Transactional));
    for (int32 Index = 0; Index < 3; ++Index)
        Curve->FloatCurves[Index].AddKey(float(Index), float(Index + 10));
    auto* Root = CastField<FStructProperty>(Curve->GetClass()->FindPropertyByName(TEXT("FloatCurves")));
    auto* Array = Root ? CastField<FArrayProperty>(Root->Struct->FindPropertyByName(TEXT("Keys"))) : nullptr;
    auto* Entry = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    auto* Mode = Entry ? CastField<FByteProperty>(Entry->Struct->FindPropertyByName(TEXT("InterpMode"))) : nullptr;
    if (!TestNotNull(TEXT("Actual native curve entry interpolation property"), Mode) ||
        !TestNotNull(TEXT("Installed interpolation enum"), Mode->Enum))
        return false;
    if (!TestEqual(TEXT("Refusal fixture is independently absent from installed enum"),
            Mode->Enum->GetValueByNameString(TEXT("P4DefinitelyMissingNativeInterp")), int64(INDEX_NONE)))
        return false;
    Owner->SetDirtyFlag(false);
    {
        FGuard State(Curve.Get());
        TestFalse(TEXT("Late fixed-array native enum import refuses all staged earlier slots"),
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves"),
                TEXT(
                    "((Keys=((Time=1,Value=91))),(Keys=((Time=2,Value=92))),(Keys=((Time=3,Value=93,InterpMode=P4DefinitelyMissingNativeInterp))))")));
        TestFalse(TEXT("Late dynamic-array native enum import refuses already staged earlier element"),
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[1].Keys"),
                TEXT("((Time=4,Value=94),(Time=5,Value=95,InterpMode=P4DefinitelyMissingNativeInterp))")));
        for (int32 Index = 0; Index < 3; ++Index)
            TestTrue(TEXT("All original native channels independently retain time/value"),
                Curve->FloatCurves[Index].GetConstRefOfKeys().Num() == 1 &&
                    Curve->FloatCurves[Index].GetConstRefOfKeys()[0].Time == float(Index) &&
                    Curve->FloatCurves[Index].GetConstRefOfKeys()[0].Value == float(Index + 10));
        State.Check(*this);
    }
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.VsPokerParameter"));
    auto* Map = Class ? CastField<FMapProperty>(Class->FindPropertyByName(TEXT("PorkerHandTableMap"))) : nullptr;
    auto* EnumKey = Map ? CastField<FEnumProperty>(Map->KeyProp) : nullptr;
    auto* ObjectValue = Map ? CastField<FObjectProperty>(Map->ValueProp) : nullptr;
    auto* Sibling = Class ? CastField<FEnumProperty>(Class->FindPropertyByName(TEXT("GameMode"))) : nullptr;
    if (!TestNotNull(TEXT("Established original native map owner"), Class) ||
        !TestNotNull(TEXT("Original enum key"), EnumKey) ||
        !TestNotNull(TEXT("Original hard-object value"), ObjectValue) ||
        !TestNotNull(TEXT("Original enum sibling"), Sibling))
        return false;
    UEnum* Enum = EnumKey->GetEnum();
    if (!TestNotNull(TEXT("Original native map key enum"), Enum))
        return false;
    const int64 First = Enum->GetValueByNameString(TEXT("EVsPokerGameMode::OneCard"));
    if (!TestTrue(TEXT("Actual first enum identity exists and invalid key is absent"),
            First == 1 && Enum->GetValueByNameString(TEXT("P4DefinitelyMissingNativePoker")) == INDEX_NONE))
        return false;
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(Owner, NAME_None, RF_Transient));
    Table->RowStruct = FTableRowBase::StaticStruct();
    Sibling->GetUnderlyingProperty()->SetIntPropertyValue(Sibling->ContainerPtrToValuePtr<void>(Object.Get()), First);
    FScriptMapHelper Storage(Map, Map->ContainerPtrToValuePtr<void>(Object.Get()));
    const int32 Slot = Storage.AddDefaultValue_Invalid_NeedsRehash();
    EnumKey->GetUnderlyingProperty()->SetIntPropertyValue(Storage.GetKeyPtr(Slot), First);
    ObjectValue->SetObjectPropertyValue(Storage.GetValuePtr(Slot), Table.Get());
    Storage.Rehash();
    Owner->SetDirtyFlag(false);
    {
        FGuard State(Object.Get()), TableState(Table.Get());
        TestFalse(TEXT("Late invalid native enum map key cannot publish earlier imported pair"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("PorkerHandTableMap"),
                TEXT("((EVsPokerGameMode::OneCard,None),(P4DefinitelyMissingNativePoker,None))")));
        TestTrue(TEXT("Original native enum-key hard-reference pair remains exact"),
            Storage.Num() == 1 && Storage.IsValidIndex(Slot) &&
                EnumKey->GetUnderlyingProperty()->GetSignedIntPropertyValue(Storage.GetKeyPtr(Slot)) == First &&
                ObjectValue->GetObjectPropertyValue(Storage.GetValuePtr(Slot)) == Table.Get());
        TestEqual(TEXT("Original enum sibling remains exact"),
            Sibling->GetUnderlyingProperty()->GetSignedIntPropertyValue(
                Sibling->ContainerPtrToValuePtr<void>(Object.Get())),
            First);
        State.Check(*this);
        TableState.Check(*this);
    }
    return !HasAnyErrors();
}
#endif

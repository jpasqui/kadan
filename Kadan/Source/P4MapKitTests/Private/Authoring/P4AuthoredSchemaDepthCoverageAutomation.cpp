// PRIVATE, uncompiled/unexecuted. Genuine editor-authored schemas only.
// Full201 zero true directions: Rows210:46; PropertyValues427:26,
// 176:26 and543:38. No original-game recovery or measured-gain claim.
// Rows161:45 is dominated by the value-depth guard for these public inputs.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataAsset.h"
#include "Engine/UserDefinedStruct.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#include "P4AuthoredSchemaDepthShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredRowsDepthBoundaries, "P4MapKit.Quality.AuthoredRowsDepthBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredRowsDepthBoundaries::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredSchemaDepthAfter242;
    FFixture F(*this);
    if (!F.BuildChain(15))
        return false;
    UBlueprint* BP = F.Blueprint(TEXT("AuthoredRowsBP"), 15, true);
    UObject* Object = F.Instance(BP, TEXT("AuthoredRowsInstance"));
    if (!TestNotNull(TEXT("Actual authored DataAsset instance"), Object))
        return false;
    UBlueprint* BoundaryBP = F.Blueprint(TEXT("BoundaryRowsBP"), 14, true);
    UObject* Boundary = F.Instance(BoundaryBP, TEXT("BoundaryRowsInstance"));
    if (!TestNotNull(TEXT("Actual authored boundary DataAsset instance"), Boundary))
        return false;
    // Count actual compiled property edges independently of the plugin walk:
    // class->field=1, each struct member adds1, array element adds1.
    TestEqual(
        TEXT("Actual authored boundary scalar is at accepted depth16"), F.ScalarDepth(Boundary, TEXT("Deep")), 16);
    TestEqual(TEXT("Actual authored next-depth struct scalar is at refused depth17"),
        F.ScalarDepth(Object, TEXT("Deep")), 17);
    TestEqual(TEXT("Actual authored boundary array scalar is at refused depth17"),
        F.ScalarDepth(Boundary, TEXT("DeepArray")), 17);
    const FString Control = TEXT("{\"Shallow\":") + F.Json(0, TEXT("17.5")) + TEXT(",\"ShallowArray\":[") +
        F.Json(0, TEXT("23.25")) + TEXT("]}");
    {
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        TestTrue(TEXT("Genuine shallow authored struct import works"),
            UP4AuthoringTools::NativeApplyAssetValues(Object, Control));
        TestTrue(TEXT("Independent native shallow float readback is exact"), F.ShallowValue(Object, 17.5f));
        TestTrue(TEXT("Independent genuine shallow authored array element/control is exact"),
            F.ShallowArrayValue(Object, 23.25f));
        Guard.Check(Object, 0, 1, TEXT("ShallowArray"));
    }
    {
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        const FString BoundaryControl = TEXT("{\"Deep\":") + F.Json(14, TEXT("37.25")) + TEXT("}");
        TestTrue(TEXT("Genuine authored scalar at exact depth16 imports completely"),
            UP4AuthoringTools::NativeApplyAssetValues(Boundary, BoundaryControl));
        TestTrue(TEXT("Independent deepest native float at depth16 is exact"), F.DeepValue(Boundary, 37.25f));
        Guard.Check(Boundary, 0, 1, NAME_None, TEXT("Deep"));
    }
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    const FString StructRefusal = TEXT("{\"Deep\":") + F.Json(15, TEXT("23.25")) + TEXT("}");
    Refuses(*this, Object, StructRefusal);
    TestFalse(TEXT("Depth17 authored struct refuses before mutation"),
        UP4AuthoringTools::NativeApplyAssetValues(Object, StructRefusal));
    Guard.Check();
    const FString ArrayRefusal = TEXT("{\"DeepArray\":[") + F.Json(14, TEXT("23.25")) + TEXT("]}");
    Refuses(*this, Boundary, ArrayRefusal);
    TestFalse(TEXT("Depth17 authored array element refuses before mutation"),
        UP4AuthoringTools::NativeApplyAssetValues(Boundary, ArrayRefusal));
    Guard.Check();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredSchemaInspectionDepth, "P4MapKit.Quality.AuthoredSchemaInspectionDepth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredSchemaInspectionDepth::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredSchemaDepthAfter242;
    FFixture F(*this);
    if (!F.BuildChain(17))
        return false;
    UBlueprint* Shallow = F.Blueprint(TEXT("ShallowSchemaBP"), 0);
    UBlueprint* Deep = F.Blueprint(TEXT("DeepSchemaBP"), 17);
    UObject* Control = F.Instance(Shallow, TEXT("ShallowSchemaInstance"));
    UObject* Object = F.Instance(Deep, TEXT("DeepSchemaInstance"));
    if (!TestTrue(TEXT("Both real authored schemas have initialized instances"), Control && Object))
        return false;
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    const FString Positive = UP4AuthoringTools::NativeAssetSchema(Control);
    TSharedPtr<FJsonObject> Parsed;
    TestTrue(TEXT("Shallow actual Blueprint schema is inspectable"),
        !Positive.IsEmpty() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Positive), Parsed));
    if (Parsed)
    {
        FString ClassPath;
        TestTrue(TEXT("Public schema retains exact actual generated class identity"),
            Parsed->TryGetStringField(TEXT("class"), ClassPath) &&
                ClassPath.Equals(Control->GetClass()->GetPathName(), ESearchCase::CaseSensitive));
    }
    TestTrue(TEXT("Unrepresentably deep authored schema refuses whole projection instead of dropping fields"),
        UP4AuthoringTools::NativeAssetSchema(Object).IsEmpty());
    Guard.Check();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredPropertyDepthBoundaries, "P4MapKit.Quality.AuthoredPropertyDepthBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredPropertyDepthBoundaries::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredSchemaDepthAfter242;
    FFixture F(*this);
    if (!F.BuildChain(32))
        return false;
    UBlueprint* BP = F.Blueprint(TEXT("AuthoredPropertyBP"), 32);
    UObject* Object = F.Instance(BP, TEXT("AuthoredPropertyInstance"));
    if (!TestNotNull(TEXT("Actual initialized authored property owner"), Object))
        return false;
    {
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        TestTrue(TEXT("Ordinary shallow authored property text edit works"),
            UP4AuthoringTools::SetEditablePropertyText(Object, TEXT("Shallow"), F.Text(0, TEXT("17.5"))));
        TestTrue(TEXT("Independent native shallow float exact value"), F.ShallowValue(Object, 17.5f));
        Guard.Check(Object, 1, 1);
    }
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    TestFalse(TEXT("Actual authored native text nesting past32 refuses before edits"),
        UP4AuthoringTools::SetEditablePropertyText(Object, TEXT("Deep"), F.Text(32, TEXT("23.25"))));
    Guard.Check();
    // This shallow text is valid and changes no field. The real native value
    // still contains33 compiled structs: comparison must respect its own bound.
    TestFalse(TEXT("Native identity depth refusal cannot turn into an accepted incomplete no-op"),
        UP4AuthoringTools::SetEditablePropertyText(Object, TEXT("Deep"), TEXT("()")));
    Guard.Check();
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
// PRIVATE append inside the existing authored-depth TU's automation guard.
// Reuses its reviewed GUID ownership, complete native serialization, schema,
// property, dirty/notification, registry and no-publication preservation oracles.
// Real editor-authored schemas; uncompiled/unexecuted, no game-schema claim.
// Zero in full201 and focused242: Rows530:22T;535:21T/F;535:50T;
// 535:79T/F. TryGetNumber failure is not a reachable claim.
namespace P4AuthoredMapKeysAfter250
{
using namespace P4AuthoredSchemaDepthAfter242;
UBlueprint* Build(FFixture& Fixture)
{
    UBlueprint* BP = Fixture.Hold(FKismetEditorUtilities::CreateBlueprint(UPrimaryDataAsset::StaticClass(),
        Fixture.Package(TEXT("AuthoredMapBP")), TEXT("AuthoredMapBP"), BPTYPE_Normal, UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(), TEXT("P4AuthoredMapCoverage")));
    if (!Fixture.Test.TestNotNull(TEXT("Supported editor creates real authored map DataAsset Blueprint"), BP))
        return nullptr;
    for (const FName Name : {FName(TEXT("FloatKeys")), FName(TEXT("IntKeys"))})
    {
        FEdGraphPinType Pin;
        Pin.PinCategory = Name == FName(TEXT("FloatKeys")) ? UEdGraphSchema_K2::PC_Float : UEdGraphSchema_K2::PC_Int;
        if (!Fixture.Test.TestTrue(TEXT("Declared primitive key has genuine editor-supported hashing"),
                FBlueprintEditorUtils::HasGetTypeHash(Pin)))
            return nullptr;
        Pin.ContainerType = EPinContainerType::Map;
        Pin.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Float;
        if (!Fixture.Test.TestTrue(TEXT("Supported editor declares actual map member"),
                FBlueprintEditorUtils::AddMemberVariable(BP, Name, Pin)))
            return nullptr;
        FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, Name, false);
    }
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Log);
    if (!Fixture.Test.TestTrue(TEXT("Real authored map Blueprint compiles as concrete initialized DataAsset"),
            Log.NumErrors == 0 && BP->IsUpToDate() && BP->GeneratedClass &&
                BP->GeneratedClass->IsChildOf(UPrimaryDataAsset::StaticClass()) &&
                !BP->GeneratedClass->HasAnyClassFlags(CLASS_Abstract) && BP->GeneratedClass->GetDefaultObject()))
        return nullptr;
    auto* Floating = FindFProperty<FMapProperty>(BP->GeneratedClass, TEXT("FloatKeys"));
    auto* Integer = FindFProperty<FMapProperty>(BP->GeneratedClass, TEXT("IntKeys"));
    if (!Fixture.Test.TestTrue(TEXT("Actual generated metadata has exact declared native key/value types"),
            Floating && CastField<FFloatProperty>(Floating->KeyProp) &&
                CastField<FFloatProperty>(Floating->ValueProp) && Integer &&
                CastField<FIntProperty>(Integer->KeyProp) && CastField<FFloatProperty>(Integer->ValueProp)))
        return nullptr;
    for (const FName Name : {FName(TEXT("FloatKeys")), FName(TEXT("IntKeys"))})
    {
        FProperty* Property = BP->GeneratedClass->FindPropertyByName(Name);
        if (!Fixture.Test.TestTrue(TEXT("Ordinary editor instance-editability retained in actual compiled fields"),
                Property && Property->HasAnyPropertyFlags(CPF_Edit) &&
                    !Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnInstance | CPF_EditConst)))
            return nullptr;
    }
    return BP;
}
bool FloatMap(UObject* Object)
{
    auto* Property = FindFProperty<FMapProperty>(Object->GetClass(), TEXT("FloatKeys"));
    auto* Key = Property ? CastField<FFloatProperty>(Property->KeyProp) : nullptr;
    auto* Value = Property ? CastField<FFloatProperty>(Property->ValueProp) : nullptr;
    if (!Key || !Value)
        return false;
    FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Object));
    if (Map.Num() != 2)
        return false;
    bool Nonzero = false, Zero = false;
    for (int32 I = 0; I < Map.GetMaxIndex(); ++I)
        if (Map.IsValidIndex(I))
        {
            const float K = Key->GetPropertyValue(Map.GetKeyPtr(I)), V = Value->GetPropertyValue(Map.GetValuePtr(I));
            uint32 Bits = 0;
            FMemory::Memcpy(&Bits, &K, sizeof(Bits));
            if (K == 1.5f && V == 17.5f && !Nonzero)
                Nonzero = true;
            else if (Bits == 0 && V == 23.25f && !Zero)
                Zero = true;
            else
                return false;
        }
    return Nonzero && Zero;
}
bool IntMap(UObject* Object)
{
    auto* Property = FindFProperty<FMapProperty>(Object->GetClass(), TEXT("IntKeys"));
    auto* Key = Property ? CastField<FIntProperty>(Property->KeyProp) : nullptr;
    auto* Value = Property ? CastField<FFloatProperty>(Property->ValueProp) : nullptr;
    if (!Key || !Value)
        return false;
    FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Object));
    if (Map.Num() != 2)
        return false;
    bool Negative = false, Positive = false;
    for (int32 I = 0; I < Map.GetMaxIndex(); ++I)
        if (Map.IsValidIndex(I))
        {
            const int32 K = Key->GetPropertyValue(Map.GetKeyPtr(I));
            const float V = Value->GetPropertyValue(Map.GetValuePtr(I));
            if (K == -3 && V == 7.5f && !Negative)
                Negative = true;
            else if (K == 17 && V == 19.25f && !Positive)
                Positive = true;
            else
                return false;
        }
    return Negative && Positive;
}
bool ValidReadback(FAutomationTestBase& Test, UObject* Object, const FString& Input)
{
    TSharedPtr<FJsonObject> Json;
    bool Valid = false;
    return Test.TestTrue(TEXT("Public exact authored map readback admits complete matching input"),
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeAssetReadback(Object, Input)), Json) &&
            Json.IsValid() && Json->TryGetBoolField(TEXT("valid"), Valid) && Valid &&
            Json->HasTypedField<EJson::Object>(TEXT("source")) && Json->HasTypedField<EJson::Object>(TEXT("readback")));
}
} //namespace P4AuthoredMapKeysAfter250

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredFloatMapKeyIdentity, "P4MapKit.Quality.AuthoredFloatMapKeyIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredFloatMapKeyIdentity::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredMapKeysAfter250;
    FFixture Fixture(*this);
    UObject* Object = Fixture.Instance(Build(Fixture), TEXT("AuthoredFloatMapInstance"));
    if (!TestNotNull(TEXT("Actual authored floating-key instance"), Object))
        return false;
    const FString Input = TEXT("{\"FloatKeys\":[[1.5,17.5],[0.0,23.25]]}");
    {
        FSnapshot Guard(Fixture);
        if (!Guard.Ready())
            return false;
        TestTrue(TEXT("Real float-key map imports both exact primitive entries"),
            UP4AuthoringTools::NativeApplyAssetValues(Object, Input));
        TestTrue(TEXT("Independent physical native map retains positive zero and nonzero keys with exact values"),
            FloatMap(Object));
        ValidReadback(*this, Object, Input);
        Guard.Check(Object, 0, 1, NAME_None, TEXT("FloatKeys"));
    }
    FSnapshot Guard(Fixture);
    if (!Guard.Ready())
        return false;
    ValidReadback(*this, Object, TEXT("{\"FloatKeys\":[[0.0,23.25],[1.5,17.5]]}"));
    Guard.Check();
    const FString DuplicateZero = TEXT("{\"FloatKeys\":[[0.0,11.5],[-0.0,29.25]]}");
    TSharedPtr<FJsonObject> Verdict;
    bool Valid = true;
    FString Error;
    TestTrue(TEXT("Signed-zero key collision refuses with exact typed-map diagnostic"),
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeAssetReadback(Object, DuplicateZero)), Verdict) &&
            Verdict.IsValid() && Verdict->TryGetBoolField(TEXT("valid"), Valid) && !Valid &&
            Verdict->TryGetStringField(TEXT("error"), Error) &&
            Error.Contains(TEXT("duplicate typed native map key"), ESearchCase::CaseSensitive));
    TestFalse(TEXT("Equal positive/negative zero keys cannot replace existing map entries"),
        UP4AuthoringTools::NativeApplyAssetValues(Object, DuplicateZero));
    TestTrue(TEXT("Independent physical native map unchanged after signed-zero collision"), FloatMap(Object));
    Guard.Check();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredIntegerMapKeyIdentity, "P4MapKit.Quality.AuthoredIntegerMapKeyIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredIntegerMapKeyIdentity::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredMapKeysAfter250;
    FFixture Fixture(*this);
    UObject* Object = Fixture.Instance(Build(Fixture), TEXT("AuthoredIntegerMapInstance"));
    if (!TestNotNull(TEXT("Actual authored integer-key instance"), Object))
        return false;
    const FString Input = TEXT("{\"IntKeys\":[[-3,7.5],[17,19.25]]}");
    {
        FSnapshot Guard(Fixture);
        if (!Guard.Ready())
            return false;
        TestTrue(
            TEXT("Real integer key map imports completely"), UP4AuthoringTools::NativeApplyAssetValues(Object, Input));
        TestTrue(TEXT("Independent physical integer key/value map exactly matches input"), IntMap(Object));
        ValidReadback(*this, Object, Input);
        Guard.Check(Object, 0, 1, NAME_None, TEXT("IntKeys"));
    }
    FSnapshot Guard(Fixture);
    if (!Guard.Ready())
        return false;
    ValidReadback(*this, Object, TEXT("{\"IntKeys\":[[17,19.25],[-3,7.5]]}"));
    Guard.Check();
    return !HasAnyErrors();
}

#endif

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
// PRIVATE append inside P4AuthoredSchemaDepthCoverageAutomation.cpp before its
// final #endif. Reuses the genuine editor producer and complete FSnapshot above.
// No canonical edits/build/editor launch. Root owns integration and execution.
#include "Animation/AnimClassData.h"
#include "DataTableEditorUtils.h"
#include "Curves/CurveVector.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

namespace P4AuthoredPropertyAfter258
{
using namespace P4AuthoredSchemaDepthAfter242;
bool Exact(const FString& A, const FString& B)
{
    return A.Equals(B, ESearchCase::CaseSensitive);
}
struct FQuietRegistry
{
    FAutomationTestBase& Test;
    FString Root;
    int32 Added = 0, Removed = 0, Renamed = 0, Created = 0, Deleted = 0;
    FDelegateHandle A, R, N, C, D;
    bool Owned(const FString& Path) const { return Path.StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive); }
    explicit FQuietRegistry(FFixture& F) : Test(F.Test), Root(F.Root)
    {
        auto& Registry = FAssetRegistryModule::GetRegistry();
        A = Registry.OnAssetAdded().AddLambda(
            [this](const FAssetData& Asset)
            {
                if (Owned(Asset.PackageName.ToString()))
                    ++Added;
            });
        R = Registry.OnAssetRemoved().AddLambda(
            [this](const FAssetData& Asset)
            {
                if (Owned(Asset.PackageName.ToString()))
                    ++Removed;
            });
        N = Registry.OnAssetRenamed().AddLambda(
            [this](const FAssetData& Asset, const FString& Old)
            {
                if (Owned(Asset.PackageName.ToString()) || Owned(Old))
                    ++Renamed;
            });
        C = Registry.OnInMemoryAssetCreated().AddLambda(
            [this](UObject* Object)
            {
                if (Object && Owned(Object->GetOutermost()->GetName()))
                    ++Created;
            });
        D = Registry.OnInMemoryAssetDeleted().AddLambda(
            [this](UObject* Object)
            {
                if (Object && Owned(Object->GetOutermost()->GetName()))
                    ++Deleted;
            });
    }
    ~FQuietRegistry()
    {
        auto& Registry = FAssetRegistryModule::GetRegistry();
        Registry.OnAssetAdded().Remove(A);
        Registry.OnAssetRemoved().Remove(R);
        Registry.OnAssetRenamed().Remove(N);
        Registry.OnInMemoryAssetCreated().Remove(C);
        Registry.OnInMemoryAssetDeleted().Remove(D);
    }
    bool Same() const
    {
        return Test.TestTrue(
            TEXT("All five GUID-owned registry callbacks remain silent during public inspection/no-op/refusal"),
            Added == 0 && Removed == 0 && Renamed == 0 && Created == 0 && Deleted == 0);
    }
};
FEdGraphPinType Pin(const FName& Category, UObject* Type = nullptr)
{
    FEdGraphPinType Result;
    Result.PinCategory = Category;
    Result.PinSubCategoryObject = Type;
    return Result;
}
UBlueprint* Begin(FFixture& F, const FString& Name, UClass* Parent = UPrimaryDataAsset::StaticClass())
{
    // Keep the engine's actual short-class cache separate for every fixture.
    const FString FixtureName = Name + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UBlueprint* BP = F.Hold(
        FKismetEditorUtilities::CreateBlueprint(Parent, F.Package(FixtureName), FName(*FixtureName), BPTYPE_Normal,
            UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("P4AuthoredPropertyAfter258")));
    return F.Test.TestNotNull(TEXT("Supported editor creates actual Blueprint schema owner"), BP) ? BP : nullptr;
}
bool Declare(FFixture& F, UBlueprint* BP, FName Name, const FEdGraphPinType& Type)
{
    if (!BP ||
        !F.Test.TestTrue(TEXT("Supported editor accepts the actual authored property declaration"),
            FBlueprintEditorUtils::AddMemberVariable(BP, Name, Type)))
        return false;
    FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, Name, false);
    return true;
}
bool Compile(FFixture& F, UBlueprint* BP)
{
    if (!BP)
        return false;
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Log);
    return F.Test.TestTrue(TEXT("Real compiler creates initialized concrete authored native class"),
        Log.NumErrors == 0 && BP->IsUpToDate() && BP->GeneratedClass &&
            !BP->GeneratedClass->HasAnyClassFlags(CLASS_Abstract) && BP->GeneratedClass->GetDefaultObject());
}
TSharedPtr<FJsonObject> Parse(const FString& Text)
{
    TSharedPtr<FJsonObject> Result;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
    return Result;
}
TSharedPtr<FJsonObject> Field(const TSharedPtr<FJsonObject>& Schema, const FString& Name)
{
    const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
    if (!Schema.IsValid() || !Schema->TryGetArrayField(TEXT("fields"), Fields) || !Fields)
        return nullptr;
    for (const auto& Value : *Fields)
        if (Value.IsValid() && Value->Type == EJson::Object)
        {
            auto Object = Value->AsObject();
            FString Actual;
            if (Object.IsValid() && Object->TryGetStringField(TEXT("name"), Actual) && Exact(Actual, Name))
                return Object;
        }
    return nullptr;
}
TSharedPtr<FJsonObject> Child(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    const TSharedPtr<FJsonObject>* Result = nullptr;
    return Object.IsValid() && Object->TryGetObjectField(Name, Result) && Result && Result->IsValid() ? *Result
                                                                                                      : nullptr;
}
bool Kind(const TSharedPtr<FJsonObject>& Object, const TCHAR* Expected)
{
    FString Actual;
    return Object.IsValid() && Object->TryGetStringField(TEXT("kind"), Actual) && Exact(Actual, Expected);
}
enum class EShape
{
    Array,
    MapKey,
    MapValue
};
UBlueprint* Shape(FFixture& F, const FString& Name, EShape ShapeKind, int32 Depth)
{
    UBlueprint* BP = Begin(F, Name);
    if (!BP)
        return nullptr;
    FEdGraphPinType Type = Pin(UEdGraphSchema_K2::PC_Struct, F.Chain[Depth]);
    if (ShapeKind == EShape::Array)
        Type.ContainerType = EPinContainerType::Array;
    else
    {
        if (ShapeKind == EShape::MapKey)
        {
            if (!F.Test.TestTrue(TEXT("Genuine nested UDS key type is supported by the editor hash contract"),
                    FBlueprintEditorUtils::HasGetTypeHash(Type)))
                return nullptr;
            Type.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Float;
        }
        else
        {
            Type = Pin(UEdGraphSchema_K2::PC_String);
            Type.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Struct;
            Type.PinValueType.TerminalSubCategoryObject = F.Chain[Depth];
        }
        Type.ContainerType = EPinContainerType::Map;
    }
    return Declare(F, BP, TEXT("Payload"), Type) && Compile(F, BP) ? BP : nullptr;
}
bool LeafCensus(FFixture& F, FStructProperty* Element, FScriptArrayHelper& Array, int32 Count)
{
    if (!Element || Array.Num() != Count)
        return false;
    for (int32 I = 0; I < Count; ++I)
    {
        FProperty* Property = Element;
        void* Value = Array.GetRawPtr(I);
        for (int32 Depth = 6; Depth >= 0; --Depth)
        {
            auto* Struct = CastField<FStructProperty>(Property);
            if (!Struct || Struct->Struct != F.Chain[Depth])
                return false;
            Property = Struct->Struct->FindPropertyByName(F.Members[Depth]);
            if (!Property)
                return false;
            Value = Property->ContainerPtrToValuePtr<void>(Value);
        }
        auto* Number = CastField<FFloatProperty>(Property);
        if (!Number || Number->GetPropertyValue(Value) != 31.25f)
            return false;
    }
    return true;
}
} //namespace P4AuthoredPropertyAfter258

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredSchemaContainers258,
    "P4MapKit.Quality.AuthoredSchemaContainerDepthBoundaries258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredSchemaContainers258::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredPropertyAfter258;
    FFixture F(*this);
    if (!F.BuildChain(17))
        return false;
    struct FCase
    {
        UObject* Object;
        EShape Shape;
        int32 Depth;
    };
    TArray<FCase> Cases;
    for (const EShape ShapeKind : {EShape::Array, EShape::MapKey, EShape::MapValue})
        for (const int32 Depth : {14, 15})
        {
            const FString Name = FString::Printf(TEXT("SchemaShape_%d_%d"), int32(ShapeKind), Depth);
            UBlueprint* BP = Shape(F, Name, ShapeKind, Depth);
            UObject* Object = F.Instance(BP, *(Name + TEXT("Instance")));
            if (!TestNotNull(TEXT("Genuine authored container instance exists"), Object))
                return false;
            FProperty* Property = Object->GetClass()->FindPropertyByName(TEXT("Payload"));
            if (!TestTrue(TEXT("Compiled shape comes from real UDS/array/map descriptors"),
                    Property && Property->ArrayDim == 1 &&
                        (ShapeKind == EShape::Array ? CastField<FArrayProperty>(Property) != nullptr
                                                    : CastField<FMapProperty>(Property) != nullptr)))
                return false;
            Cases.Add({Object, ShapeKind, Depth});
        }
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    FQuietRegistry Quiet(F);
    Guard.Check();
    if (!Quiet.Same())
        return false;
    for (const auto& Case : Cases)
    {
        const FString Text = UP4AuthoringTools::NativeAssetSchema(Case.Object);
        if (Case.Depth == 15)
            TestTrue(TEXT("First extra genuine container/UDS schema edge refuses whole schema"), Text.IsEmpty());
        else
        {
            const auto Schema = Parse(Text), Payload = Field(Schema, TEXT("Payload"));
            FString ClassPath;
            TestTrue(TEXT("Exact boundary schema retains actual generated class identity"),
                Schema.IsValid() && Schema->TryGetStringField(TEXT("class"), ClassPath) &&
                    Exact(ClassPath, Case.Object->GetClass()->GetPathName()));
            TestTrue(TEXT("Complete boundary schema exposes declared container and independently expected child kinds"),
                Case.Shape == EShape::Array ? Kind(Payload, TEXT("ArrayProperty")) &&
                        Kind(Child(Payload, TEXT("inner_schema")), TEXT("StructProperty"))
                                            : Kind(Payload, TEXT("MapProperty")) &&
                        Kind(Child(Payload, TEXT("key_schema")),
                            Case.Shape == EShape::MapKey ? TEXT("StructProperty") : TEXT("StrProperty")) &&
                        Kind(Child(Payload, TEXT("value_schema")),
                            Case.Shape == EShape::MapKey ? TEXT("FloatProperty") : TEXT("StructProperty")));
        }
        Guard.Check();
        Quiet.Same();
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredTableSchemaDepth258,
    "P4MapKit.Quality.AuthoredTableSchemaDepthBoundaries258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredTableSchemaDepth258::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredPropertyAfter258;
    FFixture F(*this);
    if (!F.BuildChain(17))
        return false;
    TArray<UDataTable*> Tables;
    for (const int32 Depth : {16, 17})
    {
        if (!TestTrue(TEXT("Installed editor explicitly admits genuine UDS as DataTable row schema"),
                FDataTableEditorUtils::IsValidTableStruct(F.Chain[Depth])))
            return false;
        UDataTable* Table = F.Hold(NewObject<UDataTable>(
            F.Package(FString::Printf(TEXT("AuthoredTable_%d"), Depth)), NAME_None, RF_Transactional));
        Table->RowStruct = F.Chain[Depth];
        Tables.Add(Table);
    }
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    FQuietRegistry Quiet(F);
    Guard.Check();
    if (!Quiet.Same())
        return false;
    const auto Positive = Parse(UP4AuthoringTools::NativeTableSchema(Tables[0]));
    FString StructPath;
    TestTrue(TEXT("Exact supported row depth preserves complete real schema identity and compiled member"),
        Positive.IsValid() && Positive->TryGetStringField(TEXT("row_struct"), StructPath) &&
            Exact(StructPath, F.Chain[16]->GetPathName()) && Field(Positive, F.Members[16].ToString()).IsValid());
    Guard.Check();
    Quiet.Same();
    TestTrue(TEXT("Deeper real UDS row refuses complete schema instead of returning partial fields"),
        UP4AuthoringTools::NativeTableSchema(Tables[1]).IsEmpty());
    Guard.Check();
    Quiet.Same();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredSoftSchema258,
    "P4MapKit.Quality.AuthoredSoftReferenceSchemaAndEmptyDependencies258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredSoftSchema258::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredPropertyAfter258;
    FFixture F(*this);
    UBlueprint* BP = Begin(F, TEXT("AuthoredSoftSchema"));
    if (!Declare(F, BP, TEXT("SoftCurve"), Pin(UEdGraphSchema_K2::PC_SoftObject, UCurveVector::StaticClass())) ||
        !Declare(
            F, BP, TEXT("SoftAssetClass"), Pin(UEdGraphSchema_K2::PC_SoftClass, UPrimaryDataAsset::StaticClass())) ||
        !Compile(F, BP))
        return false;
    UObject* Object = F.Instance(BP, TEXT("AuthoredSoftInstance"));
    if (!TestNotNull(TEXT("Genuine authored soft-reference instance"), Object))
        return false;
    auto* Curve = FindFProperty<FSoftObjectProperty>(Object->GetClass(), TEXT("SoftCurve"));
    auto* Class = FindFProperty<FSoftClassProperty>(Object->GetClass(), TEXT("SoftAssetClass"));
    if (!TestTrue(TEXT("Compiler supplies exact soft object class and soft class metaclass with real empty defaults"),
            Curve && Curve->PropertyClass == UCurveVector::StaticClass() && Class &&
                Class->MetaClass == UPrimaryDataAsset::StaticClass() &&
                Curve->GetPropertyValue_InContainer(Object).IsNull() &&
                Class->GetPropertyValue_InContainer(Object).IsNull()))
        return false;
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    FQuietRegistry Quiet(F);
    Guard.Check();
    if (!Quiet.Same())
        return false;
    const auto Schema = Parse(UP4AuthoringTools::NativeAssetSchema(Object));
    const auto CurveField = Field(Schema, TEXT("SoftCurve")), ClassField = Field(Schema, TEXT("SoftAssetClass"));
    FString CurveClass, ClassClass;
    TestTrue(TEXT("Schema preserves exact genuine soft kinds and reference class identities"),
        Kind(CurveField, TEXT("SoftObjectProperty")) && Kind(ClassField, TEXT("SoftClassProperty")) &&
            CurveField->TryGetStringField(TEXT("class"), CurveClass) &&
            Exact(CurveClass, UCurveVector::StaticClass()->GetPathName()) &&
            ClassField->TryGetStringField(TEXT("class"), ClassClass) &&
            Exact(ClassClass, UClass::StaticClass()->GetPathName()));
    // The collector excludes UObject's special class/archetype serialization.
    // Its real hard reference here is inherited UDataAsset.NativeClass; read
    // the installed native field instead of inferring a dependency from class.
    auto* NativeClass = FindFProperty<FClassProperty>(Object->GetClass(), TEXT("NativeClass"));
    UObject* NativeReference = NativeClass ? NativeClass->GetObjectPropertyValue_InContainer(Object) : nullptr;
    auto* Bundle = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("AssetBundleData"));
    auto* Bundles = Bundle ? FindFProperty<FArrayProperty>(Bundle->Struct, TEXT("Bundles")) : nullptr;
    int32 HardFields = 0;
    for (TFieldIterator<FObjectPropertyBase> It(Object->GetClass()); It; ++It)
        if (!CastField<FSoftObjectProperty>(*It))
        {
            ++HardFields;
            if (!TestTrue(TEXT("Actual hard-reference field census contains only NativeClass"), *It == NativeClass))
                return false;
        }
    if (!TestTrue(
            TEXT(
                "Actual native dependency closure has one eligible hard class reference, empty bundle payload and two null soft fields"),
            HardFields == 1 && NativeReference && !NativeReference->IsEditorOnly() &&
                !NativeReference->HasAnyFlags(RF_Transient) &&
                NativeReference->GetOutermost() != Object->GetOutermost() && Bundles &&
                FScriptArrayHelper(
                    Bundles, Bundles->ContainerPtrToValuePtr<void>(Bundle->ContainerPtrToValuePtr<void>(Object)))
                        .Num() == 0))
        return false;
    const FString ExpectedPackage = NativeReference->GetOutermost()->GetName();
    const auto Dependencies = UP4AuthoringTools::NativeAssetDependencies(Object);
    TestTrue(
        TEXT("Whole dependency set is exactly the actual native hard-reference package; empty soft fields add none"),
        Dependencies.Num() == 1 && Exact(Dependencies[0], ExpectedPackage));
    TestTrue(TEXT("Genuine empty soft-reference property edit is an exact no-op"),
        UP4AuthoringTools::SetEditablePropertyText(Object, TEXT("SoftCurve"), TEXT("None")));
    TestTrue(TEXT("Genuine empty soft class edit is an exact no-op"),
        UP4AuthoringTools::SetEditablePropertyText(Object, TEXT("SoftAssetClass"), TEXT("None")));
    const auto AfterDependencies = UP4AuthoringTools::NativeAssetDependencies(Object);
    TestTrue(TEXT("Exact full dependency set survives both genuine empty soft no-ops"),
        AfterDependencies.Num() == 1 && Exact(AfterDependencies[0], ExpectedPackage));
    Guard.Check();
    Quiet.Same();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredRetainedNodes258, "P4MapKit.Quality.AuthoredRetainedNativeNodeBudget258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredRetainedNodes258::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredPropertyAfter258;
    FFixture F(*this);
    if (!F.BuildChain(6))
        return false;
    UUserDefinedStruct* Holder = F.Hold(FStructureEditorUtils::CreateUserDefinedStruct(
        F.Package(TEXT("RetainedHolder")), TEXT("RetainedHolder"), RF_Public | RF_Standalone | RF_Transactional));
    if (!TestNotNull(TEXT("Editor creates real retained-container UDS"), Holder))
        return false;
    const auto& Descriptions = FStructureEditorUtils::GetVarDesc(Holder);
    if (!TestEqual(TEXT("Holder has real initialized editor member"), Descriptions.Num(), 1))
        return false;
    const FGuid Id = Descriptions[0].VarGuid;
    auto ArrayPin = Pin(UEdGraphSchema_K2::PC_Struct, F.Chain[6]);
    ArrayPin.ContainerType = EPinContainerType::Array;
    if (!TestTrue(TEXT("Supported authoring creates actual UDS array member"),
            FStructureEditorUtils::ChangeVariableType(Holder, Id, ArrayPin)) ||
        !TestTrue(TEXT("Supported authoring labels retained array"),
            FStructureEditorUtils::RenameVariable(Holder, Id, TEXT("Items"))))
        return false;
    FStructureEditorUtils::CompileStructure(Holder);
    const auto& Compiled = FStructureEditorUtils::GetVarDesc(Holder);
    if (!TestTrue(TEXT("Holder UDS compilation is complete"), Holder->Status == UDSS_UpToDate && Compiled.Num() == 1))
        return false;
    const FName Items = Compiled[0].VarName;
    UBlueprint* BP = Begin(F, TEXT("RetainedNodeBP"));
    if (!Declare(F, BP, TEXT("Retention"), Pin(UEdGraphSchema_K2::PC_Struct, Holder)) || !Compile(F, BP))
        return false;
    for (const int32 Count : {8191, 8192})
    {
        UObject* Object = F.Instance(BP, *FString::Printf(TEXT("RetainedNode_%d"), Count));
        if (!TestNotNull(TEXT("Actual retained native fixture exists"), Object))
            return false;
        auto* Root = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("Retention"));
        auto* Array = Holder->FindPropertyByName(Items);
        auto* ArrayProperty = CastField<FArrayProperty>(Array);
        auto* Element = ArrayProperty ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
        if (!TestTrue(TEXT("Actual compiled holder and array storage types are unchanged native schemas"),
                Root && Root->Struct == Holder && Element && Element->Struct == F.Chain[6]))
            return false;
        FScriptArrayHelper Storage(
            ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Root->ContainerPtrToValuePtr<void>(Object)));
        Storage.Resize(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            void* Value = Storage.GetRawPtr(I);
            for (int32 Depth = 6; Depth >= 0; --Depth)
                Value = F.Chain[Depth]->FindPropertyByName(F.Members[Depth])->ContainerPtrToValuePtr<void>(Value);
            CastFieldChecked<FFloatProperty>(F.Chain[0]->FindPropertyByName(F.Members[0]))
                ->SetPropertyValue(Value, 31.25f);
        }
        if (!TestTrue(TEXT("Every independent authored native leaf is initialized exactly before public call"),
                LeafCensus(F, Element, Storage, Count)))
            return false;
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        FQuietRegistry Quiet(F);
        Guard.Check();
        if (!Quiet.Same())
            return false;
        const bool Accepted = UP4AuthoringTools::SetEditablePropertyText(Object, TEXT("Retention"), TEXT("()"));
        TestEqual(
            TEXT("Tiny partial no-op admits bounded retained native traversal and refuses first excessive node group"),
            Accepted, Count == 8191);
        TestTrue(TEXT("Every independent retained native leaf and array census survives no-op/refusal"),
            LeafCensus(F, Element, Storage, Count));
        Guard.Check();
        Quiet.Same();
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeSparseSlotTotal258, "P4MapKit.Quality.NativeSparseMapWholeSlotBudget258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeSparseSlotTotal258::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredPropertyAfter258;
    FFixture F(*this);
    for (const int32 Holes : {64, 65533})
    {
        UAnimClassData* Object = F.Hold(NewObject<UAnimClassData>(
            F.Package(FString::Printf(TEXT("SparseSlots_%d"), Holes)), NAME_None, RF_Transactional));
        auto& Map = Object->GraphBlendOptions;
        for (int32 I = 0; I < Holes; ++I)
            Map.FindOrAdd(FName(TEXT("OwnedRemoved258"), I + 1)).BlendInTime = float(I);
        Map.FindOrAdd(TEXT("AFirst")).BlendInTime = 11.0f;
        Map.FindChecked(TEXT("AFirst")).BlendOutTime = 12.0f;
        Map.FindOrAdd(TEXT("ZLast")).BlendInTime = 21.0f;
        Map.FindChecked(TEXT("ZLast")).BlendOutTime = 22.0f;
        for (int32 I = 0; I < Holes; ++I)
            Map.Remove(FName(TEXT("OwnedRemoved258"), I + 1));
        auto* Property = FindFProperty<FMapProperty>(Object->GetClass(), TEXT("GraphBlendOptions"));
        if (!TestNotNull(TEXT("Installed genuine native map descriptor"), Property))
            return false;
        FScriptMapHelper Storage(Property, Property->ContainerPtrToValuePtr<void>(Object));
        if (!TestTrue(TEXT("Ordinary native removals retain bounded sparse slots and two exact entries"),
                Map.Num() == 2 && Storage.GetMaxIndex() == Holes + 2 && Storage.GetMaxIndex() <= 65536))
            return false;
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        FQuietRegistry Quiet(F);
        Guard.Check();
        if (!Quiet.Same())
            return false;
        const bool Accepted = UP4AuthoringTools::SetPropertyText(Object, TEXT("GraphBlendOptions"),
            TEXT("((ZLast,(BlendInTime=21,BlendOutTime=22)),(AFirst,(BlendInTime=11,BlendOutTime=12)))"));
        TestEqual(TEXT("Both individual slot bounds pass; only excessive combined slot work refuses no-op"), Accepted,
            Holes == 64);
        const auto* A = Map.Find(TEXT("AFirst"));
        const auto* Z = Map.Find(TEXT("ZLast"));
        TestTrue(TEXT("Native key lookups retain every value and sparse maximum exactly"),
            Map.Num() == 2 && Storage.GetMaxIndex() == Holes + 2 && A && Z && A->BlendInTime == 11.0f &&
                A->BlendOutTime == 12.0f && Z->BlendInTime == 21.0f && Z->BlendOutTime == 22.0f);
        Guard.Check();
        Quiet.Same();
    }
    return !HasAnyErrors();
}

namespace P4PlacementCollisionDiagnostics
{
void Stage(FAutomationTestBase& Test, const P4AuthoredSchemaDepthAfter242::FSnapshot::FSchema& Before, const TCHAR* At)
{
    UClass* Class = Cast<UClass>(Before.Struct);
    Test.AddInfo(FString::Printf(
        TEXT("Placement schema stage=%s struct=%s size=%d/%d class_flags=0x%08x/0x%08x xor=0x%08x same=%d"), At,
        *Before.Struct->GetPathName(), Before.Size, Before.Struct->GetStructureSize(), uint32(Before.ClassFlags),
        Class ? uint32(Class->GetClassFlags()) : 0u,
        Class ? uint32(Before.ClassFlags) ^ uint32(Class->GetClassFlags()) : 0u, Before.Same()));
    TArray<FProperty*> Now;
    for (TFieldIterator<FProperty> It(Before.Struct); It; ++It)
        Now.Add(*It);
    if (Now != Before.Fields)
        Test.AddInfo(FString::Printf(TEXT("Placement schema field-order/identity changed stage=%s old=%d now=%d"), At,
            Before.Fields.Num(), Now.Num()));
    for (int32 I = 0; I < Before.Fields.Num(); ++I)
    {
        FProperty* Property = Before.Fields[I];
        if (Property->GetPropertyFlags() != Before.Flags[I] || Property->ArrayDim != Before.Dimensions[I] ||
            Property->ElementSize != Before.ElementSizes[I])
            Test.AddInfo(FString::Printf(
                TEXT(
                    "Placement schema field changed stage=%s field=%s flags=0x%016llx/0x%016llx dimensions=%d/%d element_size=%d/%d"),
                At, *Property->GetName(), uint64(Before.Flags[I]), uint64(Property->GetPropertyFlags()),
                Before.Dimensions[I], Property->ArrayDim, Before.ElementSizes[I], Property->ElementSize));
    }
}
} //namespace P4PlacementCollisionDiagnostics

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AuthoredPlacementTypeCollision258,
    "P4MapKit.Quality.AuthoredPlacementFieldTypeCollision258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AuthoredPlacementTypeCollision258::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4AuthoredPropertyAfter258;
    FFixture F(*this);
    UClass* Native = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GActor"));
    UScriptStruct* Generator = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.ActorGenerator"));
    auto* Required = Generator ? FindFProperty<FUInt64Property>(Generator, TEXT("AssetVersion")) : nullptr;
    if (!TestTrue(TEXT("Actual original placement contract requires uint64 absent from original GActor"),
            Native && Native->IsChildOf(AActor::StaticClass()) && Required && Required->ArrayDim == 1 &&
                !Native->FindPropertyByName(TEXT("AssetVersion"))))
        return false;
    // PRIVATE replacement for the single original FSchema declaration in the
    // complete 71d6489f diagnosed fragment. Six registrations and all assertions
    // stay unchanged. Initialize the actual engine cache before the pre-authoring
    // baseline: the supported compiler calls this same public initializer and
    // recursively visits GActor. No descriptor edits, class-flag masks or rebaseline
    // after compilation; the complete pre/API snapshots still compare exact flags.
    Native->SetUpRuntimeReplicationData();
    if (!TestTrue(
            TEXT("Genuine native parent replication cache is initialized before the pre-authoring schema baseline"),
            Native->HasAnyClassFlags(CLASS_ReplicationDataIsSetUp)))
        return false;
    const FSnapshot::FSchema OriginalActorSchema(Native), OriginalRowSchema(Generator);
    // This is a real user-authored Blueprint member name/type collision. The
    // original ActorGenerator and GActor schema are never changed or recovered.
    UBlueprint* BP = Begin(F, TEXT("AuthoredPlacementCollision"), Native);
    if (!Declare(F, BP, TEXT("AssetVersion"), Pin(UEdGraphSchema_K2::PC_Int64)) || !Compile(F, BP))
        return false;
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalActorSchema, TEXT("after-compile"));
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalRowSchema, TEXT("after-compile"));
    auto* Authored = FindFProperty<FInt64Property>(BP->GeneratedClass, TEXT("AssetVersion"));
    if (!TestTrue(TEXT("Real compiler supplied signed64 user field while genuine placement field remains unsigned64"),
            Authored && Authored->ArrayDim == 1 && !Authored->SameType(Required) &&
                Generator->FindPropertyByName(TEXT("AssetVersion")) == Required &&
                !Native->FindPropertyByName(TEXT("AssetVersion"))))
        return false;
    UWorld* World = F.Hold(
        UWorld::CreateWorld(EWorldType::Editor, false, NAME_None, F.Package(TEXT("CollisionWorld")), false));
    if (!TestNotNull(TEXT("Owned editor world exists"), World))
        return false;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags = RF_Transient | RF_Transactional;
    AActor* Actor = World->SpawnActor<AActor>(BP->GeneratedClass, FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Real authored subclass of the genuine game actor is spawned"), Actor))
        return false;
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalActorSchema, TEXT("after-spawn"));
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalRowSchema, TEXT("after-spawn"));
    const FGuid Guid = Actor->GetActorGuid();
    const int64 Version = Authored->GetPropertyValue_InContainer(Actor);
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    FQuietRegistry Quiet(F);
    Guard.Check();
    if (!Quiet.Same())
        return false;
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalActorSchema, TEXT("pre-public-api"));
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalRowSchema, TEXT("pre-public-api"));
    TestTrue(TEXT("Complete original native actor schema equals the pre-authoring baseline immediately before API"),
        OriginalActorSchema.Same());
    TestTrue(TEXT("Complete original native row schema equals the pre-authoring baseline immediately before API"),
        OriginalRowSchema.Same());
    const auto Verdict = Parse(UP4AuthoringTools::NativePlacementInfo(Actor));
    FString Error, ActualGuid;
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalActorSchema, TEXT("post-public-api"));
    P4PlacementCollisionDiagnostics::Stage(*this, OriginalRowSchema, TEXT("post-public-api"));
    TestTrue(TEXT("Actual user field collision refuses exact native type mismatch without synthesizing a game row"),
        Verdict.IsValid() && Verdict->TryGetStringField(TEXT("error"), Error) &&
            Exact(Error, TEXT("Original generator field has a different native actor type: AssetVersion")) &&
            Verdict->TryGetStringField(TEXT("guid"), ActualGuid) &&
            Exact(ActualGuid, Guid.ToString(EGuidFormats::DigitsWithHyphens)) && !Verdict->HasField(TEXT("row")));
    TestTrue(TEXT("Collision refusal retains genuine actor generated-class identity"),
        Actor->GetClass() == BP->GeneratedClass);
    TestTrue(TEXT("Collision refusal retains independently captured actor GUID"), Actor->GetActorGuid() == Guid);
    TestTrue(TEXT("Collision refusal retains independent signed64 authored value"),
        Authored->GetPropertyValue_InContainer(Actor) == Version);
    TestTrue(TEXT("Collision refusal retains exact original generator uint64 descriptor"),
        Generator->FindPropertyByName(TEXT("AssetVersion")) == Required);
    TestNull(TEXT("Collision refusal does not add AssetVersion to original native GActor"),
        Native->FindPropertyByName(TEXT("AssetVersion")));
    TestTrue(TEXT("Complete original native actor schema remains exactly unchanged from pre-authoring baseline"),
        OriginalActorSchema.Same());
    TestTrue(TEXT("Complete original native row schema remains exactly unchanged from pre-authoring baseline"),
        OriginalRowSchema.Same());
    Guard.Check();
    Quiet.Same();
    return !HasAnyErrors();
}

#endif

// PRIVATE, uncompiled/unexecuted. Genuine editor-authored schemas only.
// Full201 zero true directions: Rows210:46; PropertyValues427:26,
// 176:26 and543:38. No original-game recovery or measured-gain claim.
// Rows161:45 is dominated by the value-depth guard for these public inputs.
#include "P4AuthoringRows.h"
#include "P4AuthoredSchemaDepthShared.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS && PLATFORM_WINDOWS
// PRIVATE append inside P4AuthoredSchemaDepthCoverageAutomation.cpp guard.
// Requires existing reviewed P4AuthoredSchemaDepthAfter242 fixture/snapshot
// helpers. Genuine supported Blueprint/UDS authoring; no original-game claims.
#include "Materials/Material.h"
#include "Editor.h"
namespace P4RowsRemainingAuthoredReferences272
{
using namespace P4AuthoredSchemaDepthAfter242;
struct FPackageBindings
{
    struct FBinding
    {
        UPackage* Package;
        UObject* Outer;
        UClass* Class;
        FString Path;
        EObjectFlags Flags;
        FName Filename;
        FGuid Guid, PersistentGuid;
        uint64 Size;
        explicit FBinding(UPackage* In)
            : Package(In), Outer(In->GetOuter()), Class(In->GetClass()), Path(In->GetPathName()), Flags(In->GetFlags()),
              Filename(In->FileName), Guid(In->GetGuid()), PersistentGuid(In->GetPersistentGuid()), Size(In->FileSize)
        {}
        void Check(FAutomationTestBase& Test, UPackage* SuccessfullyEdited, const FString& Stage) const
        {
            const FString Label = Stage + TEXT(" GUID-owned package ") + Path;
            Test.TestTrue(Label + TEXT(" retains outer/class/exact path"),
                Package->GetOuter() == Outer && Package->GetClass() == Class &&
                    Package->GetPathName().Equals(Path, ESearchCase::CaseSensitive));
            // UPackage::SetDirtyFlag adds RF_Transactional when the successful
            // real edit records this owned package in GUndo. Preserve every
            // baseline bit; only this independently prescribed bit may grow,
            // only on the selected package, and only after the positive edit.
            const EObjectFlags Expected = Package == SuccessfullyEdited ? EObjectFlags(Flags | RF_Transactional)
                                                                        : Flags;
            Test.TestEqual(Label + TEXT(" exact flags before edit or after documented UE undo transition"),
                uint32(Package->GetFlags()), uint32(Expected));
            Test.TestTrue(Label + TEXT(" retains exact filename"), Package->FileName == Filename);
            Test.TestTrue(Label + TEXT(" retains native GUID"), Package->GetGuid() == Guid);
            Test.TestTrue(Label + TEXT(" retains persistent GUID"), Package->GetPersistentGuid() == PersistentGuid);
            Test.TestEqual(Label + TEXT(" retains native file size"), uint64(Package->FileSize), Size);
        }
    };
    TArray<FBinding> Saved;
    explicit FPackageBindings(FFixture& F)
    {
        for (UPackage* Package : F.Packages)
            Saved.Emplace(Package);
    }
    void Check(FAutomationTestBase& Test, UPackage* SuccessfullyEdited = nullptr,
        const FString& Stage = TEXT("Before edited API")) const
    {
        for (const FBinding& Item : Saved)
            Item.Check(Test, SuccessfullyEdited, Stage);
    }
};
bool Verdict(FAutomationTestBase& Test, const FString& Actual, bool Expected, const TCHAR* Fragment = TEXT(""))
{
    TSharedPtr<FJsonObject> Object;
    bool Valid = !Expected;
    FString Error;
    if (!Test.TestTrue(TEXT("Public authored Rows response has an explicit native boolean verdict"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Actual), Object) && Object.IsValid() &&
                Object->TryGetBoolField(TEXT("valid"), Valid) && Valid == Expected))
        return false;
    return Expected ||
        Test.TestTrue(TEXT("Whole public refusal retains literal native boundary reason"),
            Object->TryGetStringField(TEXT("error"), Error) && Error.Contains(Fragment, ESearchCase::CaseSensitive));
}
UBlueprint* Build(FFixture& F)
{
    if (!F.BuildChain(0))
        return nullptr;
    UBlueprint* BP = F.Hold(FKismetEditorUtilities::CreateBlueprint(UPrimaryDataAsset::StaticClass(),
        F.Package(TEXT("NativeReferenceSchema")), TEXT("NativeReferenceSchema"), BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("P4RowsRemainingReferences")));
    if (!F.Test.TestNotNull(TEXT("Supported editor creates genuine authored reference DataAsset Blueprint"), BP))
        return nullptr;
    const auto Member = [&](const TCHAR* Name, const FName Category, UObject* Type, bool Map)
    {
        FEdGraphPinType Pin;
        Pin.PinCategory = Category;
        Pin.PinSubCategoryObject = Type;
        if (Map)
        {
            if (!F.Test.TestTrue(TEXT("Actual editor schema key has supported native hashing"),
                    FBlueprintEditorUtils::HasGetTypeHash(Pin)))
                return false;
            Pin.ContainerType = EPinContainerType::Map;
            Pin.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Float;
        }
        if (!F.Test.TestTrue(TEXT("Supported editor declares actual authored native reference/map field"),
                FBlueprintEditorUtils::AddMemberVariable(BP, FName(Name), Pin)))
            return false;
        FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, FName(Name), false);
        return true;
    };
    if (!Member(TEXT("ObjectReference"), UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), false) ||
        !Member(TEXT("ObjectKeys"), UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), true) ||
        !Member(TEXT("StructKeys"), UEdGraphSchema_K2::PC_Struct, F.Chain[0], true) ||
        !Member(TEXT("FloatKeys"), UEdGraphSchema_K2::PC_Float, nullptr, true) ||
        !Member(TEXT("NativePath"), UEdGraphSchema_K2::PC_Struct, TBaseStructure<FSoftObjectPath>::Get(), false))
        return nullptr;
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Log);
    if (!F.Test.TestTrue(TEXT("Actual compiler produces concrete ready DataAsset class/CDO"),
            Log.NumErrors == 0 && BP->IsUpToDate() && BP->GeneratedClass &&
                BP->GeneratedClass->IsChildOf(UPrimaryDataAsset::StaticClass()) &&
                !BP->GeneratedClass->HasAnyClassFlags(CLASS_Abstract) && BP->GeneratedClass->GetDefaultObject()))
        return nullptr;
    auto Reference = FindFProperty<FObjectProperty>(BP->GeneratedClass, TEXT("ObjectReference"));
    auto ObjectMap = FindFProperty<FMapProperty>(BP->GeneratedClass, TEXT("ObjectKeys"));
    auto StructMap = FindFProperty<FMapProperty>(BP->GeneratedClass, TEXT("StructKeys"));
    auto FloatMap = FindFProperty<FMapProperty>(BP->GeneratedClass, TEXT("FloatKeys"));
    auto Path = FindFProperty<FStructProperty>(BP->GeneratedClass, TEXT("NativePath"));
    auto ObjectKey = ObjectMap ? CastField<FObjectProperty>(ObjectMap->KeyProp) : nullptr;
    auto StructKey = StructMap ? CastField<FStructProperty>(StructMap->KeyProp) : nullptr;
    if (!F.Test.TestTrue(TEXT("Every field is resolved from literal actual generated native metadata"),
            Reference && Reference->PropertyClass == UObject::StaticClass() && ObjectKey &&
                ObjectKey->PropertyClass == UObject::StaticClass() && CastField<FFloatProperty>(ObjectMap->ValueProp) &&
                StructKey && StructKey->Struct == F.Chain[0] && CastField<FFloatProperty>(StructMap->ValueProp) &&
                FloatMap && CastField<FFloatProperty>(FloatMap->KeyProp) &&
                CastField<FFloatProperty>(FloatMap->ValueProp) && Path &&
                Path->Struct == TBaseStructure<FSoftObjectPath>::Get() &&
                Path->Struct->GetPathName().Equals(
                    TEXT("/Script/CoreUObject.SoftObjectPath"), ESearchCase::CaseSensitive)))
        return nullptr;
    for (const FName Name : {FName(TEXT("ObjectReference")), FName(TEXT("ObjectKeys")), FName(TEXT("StructKeys")),
             FName(TEXT("FloatKeys")), FName(TEXT("NativePath"))})
    {
        FProperty* Field = BP->GeneratedClass->FindPropertyByName(Name);
        if (!F.Test.TestTrue(TEXT("Genuine editor instance-editable nontransient fields retain native descriptors"),
                Field && Field->HasAnyPropertyFlags(CPF_Edit) &&
                    !Field->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst | CPF_DisableEditOnInstance)))
            return nullptr;
    }
    return BP;
}
bool PhysicalObjectMap(UObject* Object, UObject* Expected)
{
    auto Map = FindFProperty<FMapProperty>(Object->GetClass(), TEXT("ObjectKeys"));
    auto Key = Map ? CastField<FObjectProperty>(Map->KeyProp) : nullptr;
    auto Value = Map ? CastField<FFloatProperty>(Map->ValueProp) : nullptr;
    auto Reference = FindFProperty<FObjectProperty>(Object->GetClass(), TEXT("ObjectReference"));
    if (!Key || !Value || !Reference || Reference->GetObjectPropertyValue_InContainer(Object) != Expected)
        return false;
    FScriptMapHelper Pairs(Map, Map->ContainerPtrToValuePtr<void>(Object));
    if (Pairs.Num() != 1)
        return false;
    for (int32 I = 0; I < Pairs.GetMaxIndex(); ++I)
        if (Pairs.IsValidIndex(I))
            return Key->GetObjectPropertyValue(Pairs.GetKeyPtr(I)) == Expected &&
                Value->GetPropertyValue(Pairs.GetValuePtr(I)) == 17.5f;
    return false;
}
} //namespace P4RowsRemainingAuthoredReferences272

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsRemainingAuthoredReferences272,
    "P4MapKit.Quality.RowsRemainingAuthoredReferences272",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsRemainingAuthoredReferences272::RunTest(const FString&)
{
    using namespace P4RowsRemainingAuthoredReferences272;
    FFixture F(*this);
    UBlueprint* BP = Build(F);
    UObject* Object = F.Instance(BP, TEXT("NativeReferenceInstance"));
    if (!TestNotNull(TEXT("Actual initialized authored reference/map instance"), Object))
        return false;
    TStrongObjectPtr<UMaterial> Engine(
        LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));
    if (!TestTrue(TEXT("Actual borrowed Engine asset is loaded with literal durable identity"),
            Engine.IsValid() &&
                Engine->GetPathName().Equals(
                    TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"), ESearchCase::CaseSensitive)))
        return false;
    FSnapshot::FObject EngineBefore(Engine.Get(), *this);
    const bool EngineDirty = Engine->GetOutermost()->IsDirty();
    const uint32 EnginePackageFlags = Engine->GetOutermost()->GetPackageFlags();
    if (!TestTrue(
            TEXT("Borrowed Engine asset has complete successful native serialization"), EngineBefore.SerializationGood))
        return false;
    if (!TestTrue(
            TEXT(
                "Ordinary clean authored package and active editor transactor qualify the independent undo transition"),
            GEditor && GEditor->Trans && !GIsTransacting && !Object->HasAnyFlags(RF_Transient) &&
                !Object->GetOutermost()->IsDirty() &&
                !Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsScript | PKG_CompiledIn)))
        return false;
    const FPackageBindings Packages(F);
    UPackage* SuccessfullyEdited = nullptr;
    Packages.Check(*this);
    const auto EngineSame = [&](const FString& Stage)
    {
        TestTrue(TEXT("Borrowed Engine asset complete bytes/identity/flags and dirty/package state retained"),
            EngineBefore.Same(*this) && Engine->GetOutermost()->IsDirty() == EngineDirty &&
                Engine->GetOutermost()->GetPackageFlags() == EnginePackageFlags);
        Packages.Check(*this, SuccessfullyEdited, Stage);
    };
    if (!TestTrue(TEXT("Every real authored field has complete native initial value readback"),
            !UP4AuthoringTools::NativeAssetValuesJson(Object).IsEmpty()))
        return false;
    const FString Input = TEXT("{\"ObjectReference\":") + Quote(Engine->GetPathName()) + TEXT(",\"ObjectKeys\":[[") +
        Quote(Engine->GetPathName()) + TEXT(",17.5]]}");
    {
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        Packages.Check(*this); // Prove snapshot/readback setup changed no package bindings before the API.
        if (HasAnyErrors())
            return false;
        if (!TestTrue(TEXT("Genuine Engine reference and hard-object map key import completely"),
                UP4AuthoringTools::NativeApplyAssetValues(Object, Input)))
            return false;
        SuccessfullyEdited = Object->GetOutermost();
        TestTrue(TEXT("Independent physical object reference/map key and exact float value match"),
            PhysicalObjectMap(Object, Engine.Get()));
        Verdict(*this, UP4AuthoringTools::NativeAssetReadback(Object, Input), true);
        Guard.Check(Object, 0, 1, TEXT("ObjectReference"), TEXT("ObjectKeys"));
        EngineSame(TEXT("After accepted reference/map edit"));
    }
    auto PathProperty = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("NativePath"));
    const FString PathInput = TEXT("{\"NativePath\":") + Quote(Engine->GetPathName()) + TEXT("}");
    {
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        Packages.Check(*this, SuccessfullyEdited);
        if (HasAnyErrors())
            return false;
        if (!TestTrue(TEXT("Actual native SoftObjectPath accepts its supported string representation"),
                UP4AuthoringTools::NativeApplyAssetValues(Object, PathInput)))
            return false;
        const auto Native = PathProperty->ContainerPtrToValuePtr<FSoftObjectPath>(Object);
        TestTrue(TEXT("Independent native path value retains complete exact package/object identity and empty subpath"),
            Native && Native->ToString().Equals(Engine->GetPathName(), ESearchCase::CaseSensitive) &&
                Native->GetSubPathString().IsEmpty());
        Verdict(*this, UP4AuthoringTools::NativeAssetReadback(Object, PathInput), true);
        Guard.Check(Object, 0, 1, NAME_None, TEXT("NativePath"));
        EngineSame(TEXT("After accepted native path edit"));
    }
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    Packages.Check(*this, SuccessfullyEdited);
    if (HasAnyErrors())
        return false;
    // Fresh read-only refusal snapshots require exact current flags, including
    // the earlier prescribed successful edit; no allowance for any new bit.
    const FPackageBindings RefusalBefore(F);
    struct FCase
    {
        FString Input;
        const TCHAR* Error;
    };
    const TArray<FCase> Cases = {
        {TEXT("{\"ObjectReference\":\"/Game/Invalid:Segment.Asset\"}"), TEXT("invalid hard reference path")},
        {TEXT("{\"FloatKeys\":{\"wrong-native-wrapper\":[]}}"), TEXT("typed native map pairs required")},
        {TEXT("{\"StructKeys\":[[") + F.Json(0, TEXT("17.5")) + TEXT(",23.25]]}"),
            TEXT("unsupported or invalid exact native value")}};
    for (const FCase& Case : Cases)
    {
        const FString Exact = Case.Input;
        RefusalBefore.Check(*this, nullptr, TEXT("Before canonical refusal") + Case.Input);
        Verdict(*this, UP4AuthoringTools::NativeCanonicalAssetValues(Object, Case.Input), false, Case.Error);
        Guard.Check();
        EngineSame(TEXT("After canonical refusal") + Case.Input);
        RefusalBefore.Check(*this, nullptr, TEXT("After canonical refusal") + Case.Input);
        TestFalse(TEXT("Malformed reference/map transport cannot replace the genuine existing instance"),
            UP4AuthoringTools::NativeApplyAssetValues(Object, Case.Input));
        TestTrue(TEXT("Exact existing physical hard object map survives each refusal"),
            PhysicalObjectMap(Object, Engine.Get()));
        TestTrue(
            TEXT("Every caller input code unit remains exact"), Case.Input.Equals(Exact, ESearchCase::CaseSensitive));
        Guard.Check();
        EngineSame(TEXT("After apply refusal") + Case.Input);
        RefusalBefore.Check(*this, nullptr, TEXT("After apply refusal") + Case.Input);
    }
    return !HasAnyErrors();
}

#endif

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
// PRIVATE append inside P4AuthoredSchemaDepthCoverageAutomation.cpp guard.
// Reuses real editor fixtures and corrected285 package-binding oracle.
// Four exact258/full201 zero directions predicted; execution still required.
// Native embedded-NUL case is a discovered no-loss regression, not an old-gain claim.
#include "P4NativeValueStaging.h"
namespace P4RowsAuthoredNativeBoundary289
{
using namespace P4AuthoredSchemaDepthAfter242;
using P4RowsRemainingAuthoredReferences272::FPackageBindings;
using P4RowsRemainingAuthoredReferences272::Verdict;
UBlueprint* Build(FFixture& F, bool WithSet)
{
    UBlueprint* BP = F.Hold(FKismetEditorUtilities::CreateBlueprint(UPrimaryDataAsset::StaticClass(),
        F.Package(TEXT("BoundarySchema")), TEXT("BoundarySchema"), BPTYPE_Normal, UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(), TEXT("P4RowsNativeBoundary")));
    if (!F.Test.TestNotNull(TEXT("Supported editor authors genuine boundary DataAsset Blueprint"), BP))
        return nullptr;
    FEdGraphPinType String;
    String.PinCategory = UEdGraphSchema_K2::PC_String;
    FEdGraphPinType Map;
    Map.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
    Map.PinSubCategoryObject = UObject::StaticClass();
    if (!F.Test.TestTrue(
            TEXT("Actual soft-object key supports editor native hashing"), FBlueprintEditorUtils::HasGetTypeHash(Map)))
        return nullptr;
    Map.ContainerType = EPinContainerType::Map;
    Map.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Float;
    if (!F.Test.TestTrue(TEXT("Editor declares genuine string and soft-object map fields"),
            FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Payload"), String) &&
                FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("SoftKeys"), Map)))
        return nullptr;
    FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, TEXT("Payload"), false);
    FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, TEXT("SoftKeys"), false);
    if (WithSet)
    {
        FEdGraphPinType Set;
        Set.PinCategory = UEdGraphSchema_K2::PC_Int;
        if (!F.Test.TestTrue(TEXT("Actual integer set has editor-supported hashable elements"),
                FBlueprintEditorUtils::HasGetTypeHash(Set)))
            return nullptr;
        Set.ContainerType = EPinContainerType::Set;
        if (!F.Test.TestTrue(TEXT("Editor declares an actual authored unsupported set field"),
                FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("NativeSet"), Set)))
            return nullptr;
        FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, TEXT("NativeSet"), false);
    }
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Log);
    if (!F.Test.TestTrue(TEXT("Real editor compiler produces concrete initialized DataAsset class"),
            Log.NumErrors == 0 && BP->IsUpToDate() && BP->GeneratedClass &&
                !BP->GeneratedClass->HasAnyClassFlags(CLASS_Abstract) &&
                BP->GeneratedClass->IsChildOf(UPrimaryDataAsset::StaticClass()) &&
                BP->GeneratedClass->GetDefaultObject()))
        return nullptr;
    auto Payload = FindFProperty<FStrProperty>(BP->GeneratedClass, TEXT("Payload"));
    auto SoftKeys = FindFProperty<FMapProperty>(BP->GeneratedClass, TEXT("SoftKeys"));
    auto Key = SoftKeys ? CastField<FSoftObjectProperty>(SoftKeys->KeyProp) : nullptr;
    auto NativeSet = FindFProperty<FSetProperty>(BP->GeneratedClass, TEXT("NativeSet"));
    if (!F.Test.TestTrue(TEXT("Literal declared names resolve actual generated native string/soft-map/set metadata"),
            Payload && Key && Key->PropertyClass == UObject::StaticClass() &&
                CastField<FFloatProperty>(SoftKeys->ValueProp) &&
                (WithSet ? NativeSet && CastField<FIntProperty>(NativeSet->ElementProp) : NativeSet == nullptr)))
        return nullptr;
    for (const TCHAR* Name : {TEXT("Payload"), TEXT("SoftKeys")})
    {
        FProperty* Field = BP->GeneratedClass->FindPropertyByName(Name);
        if (!F.Test.TestTrue(TEXT("Compiled ordinary authored field is editable and retains native flags"),
                Field && Field->HasAnyPropertyFlags(CPF_Edit) &&
                    !Field->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst | CPF_DisableEditOnInstance)))
            return nullptr;
    }
    if (NativeSet &&
        !F.Test.TestTrue(TEXT("Actual authored set retains ordinary instance editability"),
            NativeSet->HasAnyPropertyFlags(CPF_Edit) &&
                !NativeSet->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst | CPF_DisableEditOnInstance)))
        return nullptr;
    return BP;
}
bool ExactString(const FString& A, const FString& B)
{
    return A.Len() == B.Len() &&
        (A.IsEmpty() ||
            !FMemory::Memcmp(
                A.GetCharArray().GetData(), B.GetCharArray().GetData(), SIZE_T(A.Len() + 1) * sizeof(TCHAR)));
}
bool SoftMap(UObject* Object, const TArray<FString>& Paths, const TArray<float>& Values)
{
    auto Map = FindFProperty<FMapProperty>(Object->GetClass(), TEXT("SoftKeys"));
    auto Key = Map ? CastField<FSoftObjectProperty>(Map->KeyProp) : nullptr;
    auto Value = Map ? CastField<FFloatProperty>(Map->ValueProp) : nullptr;
    if (!Key || !Value || Paths.Num() != Values.Num())
        return false;
    FScriptMapHelper Pairs(Map, Map->ContainerPtrToValuePtr<void>(Object));
    if (Pairs.Num() != Paths.Num())
        return false;
    TArray<bool> Seen;
    Seen.Init(false, Paths.Num());
    for (int32 I = 0; I < Pairs.GetMaxIndex(); ++I)
        if (Pairs.IsValidIndex(I))
        {
            const FString Path = Key->GetPropertyValue(Pairs.GetKeyPtr(I)).ToSoftObjectPath().ToString();
            const float Number = Value->GetPropertyValue(Pairs.GetValuePtr(I));
            int32 Found = INDEX_NONE;
            for (int32 J = 0; J < Paths.Num(); ++J)
                if (!Seen[J] && ExactString(Path, Paths[J]) && Number == Values[J])
                {
                    Found = J;
                    break;
                }
            if (Found == INDEX_NONE)
                return false;
            Seen[Found] = true;
        }
    return true;
}
} //namespace P4RowsAuthoredNativeBoundary289

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsAuthoredSoftMapBoundary289, "P4MapKit.Quality.RowsAuthoredSoftMapBoundary289",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsAuthoredSoftMapBoundary289::RunTest(const FString&)
{
    using namespace P4RowsAuthoredNativeBoundary289;
    FFixture F(*this);
    UObject* Object = F.Instance(Build(F, false), TEXT("SoftMapInstance"));
    if (!TestNotNull(TEXT("Real initialized authored soft-map instance"), Object))
        return false;
    const FString Missing = F.Root / TEXT("NeverPublished.NeverPublished");
    const FString Engine = TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");
    if (!TestTrue(TEXT("Literal missing soft key is genuinely absent before import"),
            !FindObject<UObject>(nullptr, *Missing) &&
                !FPackageName::DoesPackageExist(F.Root / TEXT("NeverPublished"))))
        return false;
    if (!TestTrue(TEXT("Supported set-free authored object has mandatory complete initial readback"),
            !UP4AuthoringTools::NativeAssetValuesJson(Object).IsEmpty()))
        return false;
    if (!TestTrue(TEXT("Real editor undo transaction prerequisites are present before accepted edit"),
            GEditor && GEditor->Trans && !GIsTransacting && !Object->HasAnyFlags(RF_Transient) &&
                !Object->GetOutermost()->IsDirty() &&
                !Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsScript | PKG_CompiledIn)))
        return false;
    const FPackageBindings Original(F);
    FSnapshot Before(F);
    if (!Before.Ready())
        return false;
    Original.Check(*this);
    if (HasAnyErrors())
        return false;
    const FString Input = TEXT("{\"SoftKeys\":[[") + Quote(Missing) + TEXT(",17.5],[") + Quote(Engine) +
        TEXT(",23.25]]}");
    if (!TestTrue(
            TEXT(
                "Soft key import accepts actual missing and Engine identities without requiring hard-object resolution"),
            UP4AuthoringTools::NativeApplyAssetValues(Object, Input)))
        return false;
    TestTrue(TEXT("Independent physical native soft-pointer keys and values match completely"),
        SoftMap(Object, {Missing, Engine}, {17.5f, 23.25f}));
    Verdict(*this, UP4AuthoringTools::NativeAssetReadback(Object, Input), true);
    Before.Check(Object, 0, 1, NAME_None, TEXT("SoftKeys"));
    Original.Check(*this, Object->GetOutermost(), TEXT("After accepted soft-map edit"));
    if (HasAnyErrors())
        return false;
    const FPackageBindings RefusalState(F);
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    const FString Reverse = TEXT("{\"SoftKeys\":{\"__native_map_pairs\":[[") + Quote(Engine) + TEXT(",23.25],[") +
        Quote(Missing) + TEXT(",17.5]]}}");
    Verdict(*this, UP4AuthoringTools::NativeAssetReadback(Object, Reverse), true);
    Guard.Check();
    RefusalState.Check(*this);
    const TArray<FString> Refusals = {
        TEXT("{\"SoftKeys\":[[") + Quote(Missing) + TEXT(",7.5],[") + Quote(Missing) + TEXT(",9.25]]}"),
        TEXT(
            "{\"SoftKeys\":[[17.5,23.25]]}"), // Soft-reference transport requires a string; hard-path grammar is deliberately not borrowed.
        TEXT("{\"SoftKeys\":{\"unexpected-pairs-wrapper\":[]}}")};
    for (const FString& InputCase : Refusals)
    {
        const FString InputBefore = InputCase;
        Verdict(*this, UP4AuthoringTools::NativeCanonicalAssetValues(Object, InputCase), false);
        Guard.Check();
        RefusalState.Check(*this);
        TestFalse(TEXT("Malformed soft-map input cannot replace authored native keys"),
            UP4AuthoringTools::NativeApplyAssetValues(Object, InputCase));
        TestTrue(TEXT("Every physical native soft key/value remains exact after refusal"),
            SoftMap(Object, {Missing, Engine}, {17.5f, 23.25f}));
        TestTrue(TEXT("Caller transport bytes remain exact"), ExactString(InputCase, InputBefore));
        Guard.Check();
        RefusalState.Check(*this);
    }
    TestTrue(TEXT("Missing soft key is still neither loaded nor published after all imports/readback/refusals"),
        !FindObject<UObject>(nullptr, *Missing) && !FPackageName::DoesPackageExist(F.Root / TEXT("NeverPublished")));
    return !HasAnyErrors();
}

// PRIVATE replacement of this single existing registration only.
// ExactIntegerJson unsupported-type refusal precedes P4ReadbackValue.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsAuthoredUnsupportedSetBoundary289,
    "P4MapKit.Quality.RowsAuthoredUnsupportedSetBoundary289",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsAuthoredUnsupportedSetBoundary289::RunTest(const FString&)
{
    using namespace P4RowsAuthoredNativeBoundary289;
    FFixture F(*this);
    UBlueprint* BP = Build(F, true);
    if (!BP)
        return false;
    TStrongObjectPtr<UObject> Scratch(
        NewObject<UObject>(GetTransientPackage(), BP->GeneratedClass, NAME_None, RF_Transient));
    if (!TestNotNull(TEXT("Actual concrete editor-authored disposable native instance"), Scratch.Get()))
        return false;
    auto Payload = FindFProperty<FStrProperty>(BP->GeneratedClass, TEXT("Payload"));
    auto Set = FindFProperty<FSetProperty>(BP->GeneratedClass, TEXT("NativeSet"));
    auto Element = CastField<FIntProperty>(Set->ElementProp);
    Payload->SetPropertyValue_InContainer(Scratch.Get(), TEXT("native control"));
    FScriptSetHelper NativeSet(Set, Set->ContainerPtrToValuePtr<void>(Scratch.Get()));
    int32 Removed = INDEX_NONE;
    for (int32 Value : {7, 17, 23})
    {
        const int32 Slot = NativeSet.AddDefaultValue_Invalid_NeedsRehash();
        Element->SetPropertyValue(NativeSet.GetElementPtr(Slot), Value);
        if (Value == 17)
            Removed = Slot;
    }
    NativeSet.Rehash();
    NativeSet.RemoveAt(Removed); // Supported public container API leaves genuine sparse native set storage.
    const auto SparseSet = [&]()
    {
        if (NativeSet.Num() != 2 || NativeSet.GetMaxIndex() < 3 || NativeSet.IsValidIndex(Removed))
            return false;
        bool Seven = false, TwentyThree = false;
        for (int32 I = 0; I < NativeSet.GetMaxIndex(); ++I)
            if (NativeSet.IsValidIndex(I))
            {
                const int32 Value = Element->GetPropertyValue(NativeSet.GetElementPtr(I));
                if (Value == 7 && !Seven)
                    Seven = true;
                else if (Value == 23 && !TwentyThree)
                    TwentyThree = true;
                else
                    return false;
            }
        return Seven && TwentyThree;
    };
    if (!TestTrue(
            TEXT("Public native set helper supplies two exact distinct values and a real removed slot"), SparseSet()))
        return false;
    FSnapshot Guard(F);
    if (!Guard.Ready())
        return false;
    FSnapshot::FObject Native(Scratch.Get(), *this);
    if (!Native.SerializationGood)
        return false;
    const FPackageBindings Packages(F);
    const EObjectFlags TransientFlags = GetTransientPackage()->GetFlags();
    const uint32 TransientPackageFlags = GetTransientPackage()->GetPackageFlags();
    const bool TransientDirty = GetTransientPackage()->IsDirty();
    FString Canonical, Error;
    if (!TestTrue(
            TEXT(
                "Independent selected supported native string field prepares/readbacks completely before set refusals"),
            P4PrepareExactPrivateProperties(
                Scratch.Get(), TEXT("{\"Payload\":\"native control\"}"), Canonical, Error) &&
                !Canonical.IsEmpty() && Error.IsEmpty() &&
                UP4AuthoringTools::PropertyJson(Scratch.Get(), TEXT("Payload"))
                    .Equals(TEXT("\"native control\""), ESearchCase::CaseSensitive)))
        return false;
    for (const FString& Input : {FString(TEXT("{\"NativeSet\":[]}")), FString(TEXT("{\"NativeSet\":[7,23]}")),
             FString(TEXT("{\"NativeSet\":{\"items\":[7,23]}}"))})
    {
        const FString InputBefore = Input;
        Canonical = TEXT("stale");
        Error.Empty();
        const bool Prepared = P4PrepareExactPrivateProperties(Scratch.Get(), Input, Canonical, Error);
        TestFalse(TEXT("Whole unsupported authored set refuses before any native scratch write"), Prepared);
        TestTrue(TEXT("Unsupported set publishes no canonical private transport"), Canonical.IsEmpty());
        TestTrue(TEXT("Exact type validation refuses unsupported set before scalar readback"),
            Error.Equals(TEXT("NativeSet: unsupported or invalid exact private value"), ESearchCase::CaseSensitive));
        if (Prepared || !Canonical.IsEmpty() ||
            !Error.Equals(TEXT("NativeSet: unsupported or invalid exact private value"), ESearchCase::CaseSensitive))
            AddInfo(FString::Printf(TEXT("Actual unsupported-set contract: prepared=%d canonical-length=%d error=%s"),
                Prepared ? 1 : 0, Canonical.Len(), *Error));
        TestTrue(TEXT("Complete independent native set/string bytes and identity remain exact"), Native.Same(*this));
        TestTrue(TEXT("Physical native set values and exact sparse slot survive unsupported refusal"), SparseSet());
        TestTrue(TEXT("Caller complete transport remains exact"), ExactString(Input, InputBefore));
        TestTrue(TEXT("Disposable transient package retains exact dirty/object flags/package flags"),
            GetTransientPackage()->GetFlags() == TransientFlags &&
                GetTransientPackage()->GetPackageFlags() == TransientPackageFlags &&
                GetTransientPackage()->IsDirty() == TransientDirty);
        Guard.Check();
        Packages.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsAuthoredNativeStringBoundary289,
    "P4MapKit.Quality.RowsAuthoredNativeStringBoundary289",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsAuthoredNativeStringBoundary289::RunTest(const FString&)
{
    using namespace P4RowsAuthoredNativeBoundary289;
    FFixture F(*this);
    UObject* Object = F.Instance(Build(F, false), TEXT("NativeStringInstance"));
    if (!Object)
        return false;
    auto Payload = FindFProperty<FStrProperty>(Object->GetClass(), TEXT("Payload"));
    FString Nul = TEXT("prefixXtail");
    Nul[6] = TCHAR(0); // Public indexed FString storage preserves full extent; AppendChar(0) deliberately omits zero.
    if (!TestTrue(TEXT("Actual native FString producer retains indexed NUL and suffix with complete nonempty length"),
            Nul.Len() == 11 && Nul[6] == 0 && Nul.Mid(7).Equals(TEXT("tail"), ESearchCase::CaseSensitive)))
        return false;
    if (!TestTrue(TEXT("Windows fixture has exact independently bound two-byte TCHAR layout"), sizeof(TCHAR) == 2))
        return false;
    const FString Large = FString::ChrN(32 * 1024 * 1024 + 1, TEXT(' '));
    const FString* Inputs[] = {&Nul, &Large};
    for (const FString* Text : Inputs)
    {
        // Supported native property setter is fixture setup, before all oracles;
        // no plugin API, property flags, metadata or private memory is rewritten.
        Payload->SetPropertyValue_InContainer(Object, *Text);
        if (!TestTrue(TEXT("Native string setter and getter preserve every code unit, including terminal zero"),
                ExactString(Payload->GetPropertyValue_InContainer(Object), *Text)))
            return false;
        FSnapshot Guard(F);
        if (!Guard.Ready())
            return false;
        const FPackageBindings Packages(F);
        const FString NativeText = UP4AuthoringTools::NativeAssetValuesJson(Object);
        if (Text == &Large &&
            !TestTrue(
                TEXT(
                    "Native whole object successfully serializes the genuine large authored string beyond readback budget"),
                !NativeText.IsEmpty() && int64(NativeText.Len()) * sizeof(TCHAR) > 64 * 1024 * 1024))
            return false;
        if (Text == &Nul)
            TestTrue(
                TEXT(
                    "Unsupported embedded-NUL representation refuses whole native JSON rather than emitting a truncated prefix"),
                NativeText.IsEmpty());
        // UE4's general JSON writer truncates at NUL. Reporting the truncated
        // prefix as matching would hide original data. This unsupported native
        // representation must refuse; readback must never accept the prefix.
        const FString Input = Text == &Nul ? TEXT("{\"Payload\":\"prefix\"}")
                                           : TEXT("{\"Payload\":\"different-safe-source\"}");
        Verdict(*this, UP4AuthoringTools::NativeAssetReadback(Object, Input), false,
            TEXT("exact native asset readback unavailable or over budget"));
        TestTrue(TEXT("Every native string code unit survives readback refusal"),
            ExactString(Payload->GetPropertyValue_InContainer(Object), *Text));
        Guard.Check();
        Packages.Check(*this);
    }
    return !HasAnyErrors();
}

#endif

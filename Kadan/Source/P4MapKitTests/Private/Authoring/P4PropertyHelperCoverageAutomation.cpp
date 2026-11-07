// PRIVATE exact-batch89 draft: uncompiled and unexecuted.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Async/Async.h"
#include "Animation/AnimClassData.h"
#include "Camera/CameraShakeSourceComponent.h"
#include "Curves/CurveVector.h"
#include "Engine/DataAsset.h"
#include "Engine/Level.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4PropertyHelperCoverage
{
UPackage* Package(const TCHAR* Case)
{
    return CreatePackage(*(FString(TEXT("/Game/NativePropertyHelperCoverage/")) + Case + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}
TArray<uint8> Snapshot(UObject* Object)
{
    TArray<uint8> Result;
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    return Result;
}
struct Guard
{
    UObject* Object;
    TArray<uint8> Bytes;
    EObjectFlags Flags;
    bool Dirty;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    explicit Guard(UObject* In)
        : Object(In), Bytes(Snapshot(In)), Flags(In->GetFlags()), Dirty(In->GetOutermost()->IsDirty())
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
        Test.TestTrue(TEXT("Native nondelta snapshot unchanged"), Snapshot(Object) == Bytes);
        Test.TestTrue(TEXT("Actual object flags unchanged"), Object->GetFlags() == Flags);
        Test.TestEqual(TEXT("Prior dirty state retained"), Object->GetOutermost()->IsDirty(), Dirty);
        Test.TestEqual(TEXT("No pre-edit callback for read/refusal/no-op"), Pre, 0);
        Test.TestEqual(TEXT("No post-edit callback for read/refusal/no-op"), Post, 0);
    }
};
float* RedR(UObject* Object)
{
    auto* Red = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(TEXT("Red")));
    if (!Red)
        return nullptr;
    auto* R = CastField<FFloatProperty>(Red->Struct->FindPropertyByName(TEXT("R")));
    return R ? R->ContainerPtrToValuePtr<float>(Red->ContainerPtrToValuePtr<void>(Object)) : nullptr;
}
} //namespace P4PropertyHelperCoverage

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyMaterialProvenanceCoverage,
    "P4MapKit.Quality.PropertyMaterialProvenanceCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyMaterialProvenanceCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyHelperCoverage;
    UPackage* Owner = Package(TEXT("Material"));
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Owner, NAME_None, RF_Transactional));
    Owner->SetDirtyFlag(false);
    {
        Guard State(Material.Get());
        TestNull(TEXT("Null material has no settings"), UP4AuthoringTools::MaterialExportSettings(nullptr, false));
        TestNull(
            TEXT("Null material cannot create settings"), UP4AuthoringTools::MaterialExportSettings(nullptr, true));
        TestFalse(TEXT("Valid representation cannot edit null material"),
            UP4AuthoringTools::SetMaterialProvenance(nullptr, TEXT("Authored"), TEXT(""), TEXT("")));
        TestNull(TEXT("Read-only query leaves absent user data absent"),
            UP4AuthoringTools::MaterialExportSettings(Material.Get(), false));
        TestFalse(TEXT("Unknown representation refuses without creating user data"),
            UP4AuthoringTools::SetMaterialProvenance(
                Material.Get(), TEXT("Invented"), TEXT("Original"), TEXT("Notes")));
        TestNull(TEXT("Invalid representation creates no user data"),
            Material->GetAssetUserDataOfClass(UP4MaterialExportData::StaticClass()));
        State.Check(*this);
    }
    UP4MaterialExportData* Data = UP4AuthoringTools::MaterialExportSettings(Material.Get(), true);
    if (!TestNotNull(TEXT("Real material user-data creation"), Data))
        return false;
    TestTrue(TEXT("User data is owned by exact material"), Data->GetOuter() == Material.Get());
    TestTrue(TEXT("User data is transactional"), Data->HasAnyFlags(RF_Transactional));
    TestTrue(TEXT("Material creation marks owning package dirty"), Owner->IsDirty());
    TestTrue(TEXT("Existing query returns same native user-data pointer"),
        UP4AuthoringTools::MaterialExportSettings(Material.Get(), false) == Data);
    TestTrue(TEXT("Repeated create returns same native user-data pointer"),
        UP4AuthoringTools::MaterialExportSettings(Material.Get(), true) == Data);
    TestTrue(TEXT("Material lookup independently returns created data"),
        Material->GetAssetUserDataOfClass(UP4MaterialExportData::StaticClass()) == Data);
    TestTrue(TEXT("Reconstructed provenance is accepted"),
        UP4AuthoringTools::SetMaterialProvenance(
            Material.Get(), TEXT("Reconstructed"), TEXT("OriginalA"), TEXT("NotesA")));
    TestTrue(TEXT("Native representation and source fields read independently"),
        Data->Representation == TEXT("Reconstructed") && Data->OriginalMaterial == TEXT("OriginalA") &&
            Data->ReconstructionNotes == TEXT("NotesA"));
    Owner->SetDirtyFlag(false);
    {
        Guard MaterialState(Material.Get()), DataState(Data);
        TestTrue(TEXT("Identical provenance is a native no-op"),
            UP4AuthoringTools::SetMaterialProvenance(
                Material.Get(), TEXT("Reconstructed"), TEXT("OriginalA"), TEXT("NotesA")));
        MaterialState.Check(*this);
        DataState.Check(*this);
    }
    TestTrue(TEXT("Only source identity change is accepted"),
        UP4AuthoringTools::SetMaterialProvenance(
            Material.Get(), TEXT("Reconstructed"), TEXT("OriginalB"), TEXT("NotesA")));
    TestTrue(TEXT("Only source identity actually changes"),
        Data->Representation == TEXT("Reconstructed") && Data->OriginalMaterial == TEXT("OriginalB") &&
            Data->ReconstructionNotes == TEXT("NotesA"));
    Owner->SetDirtyFlag(false);
    TestTrue(TEXT("Only reconstruction notes change is accepted"),
        UP4AuthoringTools::SetMaterialProvenance(
            Material.Get(), TEXT("Reconstructed"), TEXT("OriginalB"), TEXT("NotesB")));
    TestTrue(TEXT("Only notes actually change and dirty package"),
        Data->ReconstructionNotes == TEXT("NotesB") && Data->OriginalMaterial == TEXT("OriginalB") && Owner->IsDirty());
    TestTrue(TEXT("Fallback representation accepted"),
        UP4AuthoringTools::SetMaterialProvenance(Material.Get(), TEXT("Fallback"), TEXT("OriginalB"), TEXT("NotesB")));
    TestTrue(TEXT("Authored representation accepted"),
        UP4AuthoringTools::SetMaterialProvenance(Material.Get(), TEXT("Authored"), TEXT("OriginalB"), TEXT("NotesB")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyCountsFactoryCoverage, "P4MapKit.Quality.PropertyCountsFactoryCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyCountsFactoryCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyHelperCoverage;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.ActorPlacedNumAsset"));
    if (!TestNotNull(TEXT("Existing recovered native count-asset schema"), Class))
        return false;
    UPackage* Owner = Package(TEXT("Factory"));
    TStrongObjectPtr<UP4ActorCountsFactory> Factory(NewObject<UP4ActorCountsFactory>());
    TestTrue(TEXT("Factory resolves actual captured class"), Factory->ResolveSupportedClass() == Class);
    TestEqual(TEXT("Native factory display name"), Factory->GetDisplayName().ToString(),
        FString(TEXT("Pikmin 4 Actor Counts")));
    const FName Name(TEXT("CountFixture"));
    Owner->SetDirtyFlag(false);
    TestNull(TEXT("Wrong native class refuses before creating object"),
        Factory->FactoryCreateNew(UDataAsset::StaticClass(), Owner, Name, RF_Transactional, nullptr, nullptr));
    TestNull(TEXT("Null requested class refuses"),
        Factory->FactoryCreateNew(nullptr, Owner, Name, RF_Transactional, nullptr, nullptr));
    TestNull(TEXT("Exact class with missing owner refuses"),
        Factory->FactoryCreateNew(Class, nullptr, Name, RF_Transactional, nullptr, nullptr));
    TestNull(TEXT("Refused factory calls leave target name absent"), FindObject<UObject>(Owner, *Name.ToString()));
    TestFalse(TEXT("Refused factory calls leave package clean"), Owner->IsDirty());
    TStrongObjectPtr<UObject> Asset(Factory->FactoryCreateNew(Class, Owner, Name, RF_Transactional, nullptr, nullptr));
    if (!TestTrue(TEXT("Exact native schema creates real count asset"), Asset.IsValid()))
        return false;
    TestTrue(TEXT("Exact class, owner, name and requested flags retained"),
        Asset->GetClass() == Class && Asset->GetOuter() == Owner && Asset->GetFName() == Name &&
            Asset->HasAnyFlags(RF_Transactional));
    TestTrue(TEXT("Factory has not saved or dirtied a package itself"), !Owner->IsDirty());
    TestTrue(
        TEXT("Native lookup independently finds result"), FindObject<UObject>(Owner, *Name.ToString()) == Asset.Get());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAssetGuardCoverage, "P4MapKit.Quality.PropertyAssetGuardCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAssetGuardCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyHelperCoverage;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.OnyonCombineEftColorData"));
    UClass* OtherClass = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.ActorPlacedNumAsset"));
    if (!TestNotNull(TEXT("Existing recovered color asset"), Class) ||
        !TestNotNull(TEXT("Existing distinct supported native asset"), OtherClass))
        return false;
    UPackage* Owner = Package(TEXT("Asset"));
    TStrongObjectPtr<UObject> Target(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UObject> Source(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UObject> Other(NewObject<UObject>(Owner, OtherClass, NAME_None, RF_Transactional));
    TStrongObjectPtr<UCurveVector> Unsupported(NewObject<UCurveVector>(Owner, NAME_None, RF_Transactional));
    TStrongObjectPtr<UCameraShakeSourceComponent> Component(
        NewObject<UCameraShakeSourceComponent>(Owner, NAME_None, RF_Transactional));
    UObject* Default = Class->GetDefaultObject();
    float* NativeR = RedR(Source.Get());
    if (!TestNotNull(TEXT("Real captured color R is native float"), NativeR))
        return false;
    const uint32 Nan = 0x7fc05678u;
    FMemory::Memcpy(NativeR, &Nan, sizeof(Nan));
    Owner->SetDirtyFlag(false);
    Guard TargetState(Target.Get()), SourceState(Source.Get()), OtherState(Other.Get()),
        UnsupportedState(Unsupported.Get()), DefaultState(Default);
    TestTrue(TEXT("Null asset schema refuses"), UP4AuthoringTools::NativeAssetSchema(nullptr).IsEmpty());
    TestTrue(TEXT("Unsupported real asset schema refuses"),
        UP4AuthoringTools::NativeAssetSchema(Unsupported.Get()).IsEmpty());
    TestFalse(TEXT("Real actor component schema is inspectable"),
        UP4AuthoringTools::NativeAssetSchema(Component.Get()).IsEmpty());
    TestTrue(TEXT("Component schema inspection does not admit asset values"),
        UP4AuthoringTools::NativeAssetValuesJson(Component.Get()).IsEmpty());
    TestTrue(TEXT("Null asset values refuse"), UP4AuthoringTools::NativeAssetValuesJson(nullptr).IsEmpty());
    TestTrue(TEXT("Unsupported real asset values refuse"),
        UP4AuthoringTools::NativeAssetValuesJson(Unsupported.Get()).IsEmpty());
    TestTrue(TEXT("Nonfinite supported native asset refuses complete projection"),
        UP4AuthoringTools::NativeAssetValuesJson(Source.Get()).IsEmpty());
    TestNull(TEXT("Null class asset construction refuses"), UP4AuthoringTools::NewNativeAsset(nullptr));
    TestNull(TEXT("Unsupported native class construction refuses"),
        UP4AuthoringTools::NewNativeAsset(UCurveVector::StaticClass()));
    TestNull(TEXT("Genuine abstract DataAsset refuses construction"),
        UP4AuthoringTools::NewNativeAsset(UDataAsset::StaticClass()));
    TestNull(TEXT("Unsupported class refuses package creation"),
        UP4AuthoringTools::CreateNativeAsset(UCurveVector::StaticClass(), Owner->GetName() / TEXT("InvalidClass")));
    TestNull(TEXT("Abstract real class refuses package creation"),
        UP4AuthoringTools::CreateNativeAsset(UDataAsset::StaticClass(), Owner->GetName() / TEXT("Abstract")));
    TestNull(TEXT("Supported class requires Game package root"),
        UP4AuthoringTools::CreateNativeAsset(Class, TEXT("/Engine/P4PropertyFixture")));
    TestNull(TEXT("Supported class requires valid package name"),
        UP4AuthoringTools::CreateNativeAsset(Class, TEXT("/Game/P4PropertyFixture?")));
    TestFalse(TEXT("Null copy target refuses"), UP4AuthoringTools::CopyNativeAssetValues(nullptr, Source.Get()));
    TestFalse(TEXT("Null copy source refuses"), UP4AuthoringTools::CopyNativeAssetValues(Target.Get(), nullptr));
    TestFalse(TEXT("Self-copy refuses"), UP4AuthoringTools::CopyNativeAssetValues(Target.Get(), Target.Get()));
    TestFalse(TEXT("Unsupported copy target refuses"),
        UP4AuthoringTools::CopyNativeAssetValues(Unsupported.Get(), Target.Get()));
    TestFalse(TEXT("Distinct actual supported classes cannot be copied"),
        UP4AuthoringTools::CopyNativeAssetValues(Target.Get(), Other.Get()));
    TestFalse(TEXT("Actual CDO is never a copy destination"),
        UP4AuthoringTools::CopyNativeAssetValues(Default, Target.Get()));
    TestFalse(TEXT("Nonfinite complete source refuses before copying any fields"),
        UP4AuthoringTools::CopyNativeAssetValues(Target.Get(), Source.Get()));
    TestEqual(TEXT("Null dependency query is empty"), UP4AuthoringTools::NativeAssetDependencies(nullptr).Num(), 0);
    TestTrue(TEXT("Null table rows refuse"), UP4AuthoringTools::NativeTableRowsJson(nullptr).IsEmpty());
    TestTrue(TEXT("Null table schema refuses"), UP4AuthoringTools::NativeTableSchema(nullptr).IsEmpty());
    TStrongObjectPtr<UDataTable> NoSchema(NewObject<UDataTable>(Owner));
    TestTrue(TEXT("Real table without RowStruct cannot project rows"),
        UP4AuthoringTools::NativeTableRowsJson(NoSchema.Get()).IsEmpty());
    TestTrue(TEXT("Real table without RowStruct cannot project schema"),
        UP4AuthoringTools::NativeTableSchema(NoSchema.Get()).IsEmpty());
    TestEqual(TEXT("Missing-schema fixture never acquired any native rows"), NoSchema->GetRowMap().Num(), 0);
    // Normal UE garbage collection calls EmptyTable during destruction. Restore
    // a real empty row schema after testing the deliberately missing schema.
    NoSchema->RowStruct = FTableRowBase::StaticStruct();
    uint32 After = 0;
    FMemory::Memcpy(&After, NativeR, sizeof(After));
    TestEqual(TEXT("Source native NaN payload bits retained"), After, Nan);
    TargetState.Check(*this);
    SourceState.Check(*this);
    OtherState.Check(*this);
    UnsupportedState.Check(*this);
    DefaultState.Check(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyActorHelperGuards, "P4MapKit.Quality.PropertyActorHelperGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyActorHelperGuards::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyHelperCoverage;
    ATargetPoint* Default = GetMutableDefault<ATargetPoint>();
    Guard DefaultState(Default);
    TestFalse(TEXT("Null actor cannot gain a root"), UP4AuthoringTools::EnsureActorRoot(nullptr));
    TestFalse(TEXT("Actual actor CDO cannot gain a root"), UP4AuthoringTools::EnsureActorRoot(Default));
    TestFalse(TEXT("Null actor defaults cannot refresh"), UP4AuthoringTools::RefreshActorDefaults(nullptr));
    TestFalse(TEXT("Native actor CDO is not a Blueprint default"), UP4AuthoringTools::RefreshActorDefaults(Default));
    TestTrue(TEXT("Null actor placement refuses"), UP4AuthoringTools::NativePlacementInfo(nullptr).IsEmpty());
    TestTrue(TEXT("Native actor CDO placement refuses"), UP4AuthoringTools::NativePlacementInfo(Default).IsEmpty());
    DefaultState.Check(*this);
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Existing isolated editor world"), World))
        return false;
    FActorSpawnParameters Spawn;
    Spawn.Name = MakeUniqueObjectName(
        World->PersistentLevel, ATargetPoint::StaticClass(), TEXT("P4HelperCoverageTarget"));
    ATargetPoint* Actor = World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Real installed target-point actor"), Actor))
        return false;
    {
        Guard ActorState(Actor);
        USceneComponent* Root = Actor->GetRootComponent();
        TestNotNull(TEXT("Actual constructor provides transform root"), Root);
        TestTrue(TEXT("Existing native root is a no-op"), UP4AuthoringTools::EnsureActorRoot(Actor));
        TestTrue(TEXT("Exact native root pointer retained"), Actor->GetRootComponent() == Root);
        TestFalse(
            TEXT("Actor instance cannot refresh Blueprint defaults"), UP4AuthoringTools::RefreshActorDefaults(Actor));
        TestTrue(TEXT("Non-game actor placement refuses"), UP4AuthoringTools::NativePlacementInfo(Actor).IsEmpty());
        const bool Refused = Async(EAsyncExecution::Thread,
            [Actor]()
            {
                return UP4AuthoringTools::NativePlacementInfo(Actor).IsEmpty();
            }).Get();
        TestTrue(TEXT("Worker placement refuses before native actor access"), Refused);
        ActorState.Check(*this);
    }
    World->DestroyActor(Actor);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertySparseMapProjection, "P4MapKit.Quality.PropertySparseMapProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertySparseMapProjection::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyHelperCoverage;
    UPackage* Owner = Package(TEXT("Sparse"));
    TStrongObjectPtr<UAnimClassData> Object(NewObject<UAnimClassData>(Owner, NAME_None, RF_Transactional));
    auto& Map = Object->GraphBlendOptions;
    Map.FindOrAdd(TEXT("ZLast")).BlendInTime = 3.0f;
    Map.FindOrAdd(TEXT("Hole")).BlendInTime = 91.0f;
    Map.FindOrAdd(TEXT("AFirst")).BlendInTime = 5.0f;
    Map.Remove(TEXT("Hole"));
    auto* Property = CastField<FMapProperty>(Object->GetClass()->FindPropertyByName(TEXT("GraphBlendOptions")));
    if (!TestNotNull(TEXT("Real reflected native blend map"), Property))
        return false;
    FScriptMapHelper Storage(Property, Property->ContainerPtrToValuePtr<void>(Object.Get()));
    if (!TestTrue(TEXT("Native removal leaves a genuine sparse hole"), Storage.GetMaxIndex() > Storage.Num()))
        return false;
    Owner->SetDirtyFlag(false);
    {
        Guard State(Object.Get());
        const FString Json = UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("GraphBlendOptions"));
        TestFalse(TEXT("Sparse native map can project all surviving entries"), Json.IsEmpty());
        TestFalse(TEXT("Removed entry is not serialized as a survivor"), Json.Contains(TEXT("Hole")));
        TestTrue(TEXT("Projected surviving names are stable lexical order"),
            Json.Find(TEXT("AFirst")) >= 0 && Json.Find(TEXT("AFirst")) < Json.Find(TEXT("ZLast")));
        TestEqual(TEXT("Native storage still contains exactly two entries"), Map.Num(), 2);
        TestEqual(TEXT("Native first value independently retained"), Map.FindChecked(TEXT("AFirst")).BlendInTime, 5.0f);
        TestEqual(TEXT("Native last value independently retained"), Map.FindChecked(TEXT("ZLast")).BlendInTime, 3.0f);
        State.Check(*this);
    }
    const uint32 Nan = 0x7fc08765u;
    FMemory::Memcpy(&Map.FindChecked(TEXT("ZLast")).BlendOutTime, &Nan, sizeof(Nan));
    {
        Guard State(Object.Get());
        TestTrue(TEXT("Sparse map with unprojectable native value refuses whole projection"),
            UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("GraphBlendOptions")).IsEmpty());
        uint32 Actual = 0;
        FMemory::Memcpy(&Actual, &Map.FindChecked(TEXT("ZLast")).BlendOutTime, sizeof(Actual));
        TestEqual(TEXT("Failed projection preserves original native NaN payload"), Actual, Nan);
        State.Check(*this);
    }
    return !HasAnyErrors();
}
#endif

#include "P4RecoveredPostProcessAuthoring.h"
#include "P4KadanTestSeams.h"
#include "P4RecoveredPostProcessStorage.h"
#include "P4ActorAssetValuePlanning.h"
#include "P4BlueprintSubobjectStorage.h"
#include "P4KadanReadOnly.h"
#include "P4LevelImportWorld.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Misc/ScopeExit.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredPostProcessAuthoringBoundary,
    "P4MapKit.Kadan.RecoveredPostProcessAuthoringBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredPostProcessAuthoringBoundary::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    UClass* Class = LoadClass<UPostProcessComponent>(
        nullptr, TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent"));
    FP4RecoveredPostProcessStorage View;
    FString Why;
    if (!TestNotNull(TEXT("Actual native PP storage class"), Class) ||
        !TestTrue(TEXT("Actual storage contract"), P4GetRecoveredPostProcessStorage(Class, View, Why)))
        return false;
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4PostProcessAuthoring_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UWorld> World(P4CreateOriginalImportWorld(TEXT("GuardWorld"), Package.Get()));
    if (!TestNotNull(TEXT("Owned isolated native world"), World.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        P4Kadan::ForgetGameActorNamesForTest(World.Get());
        World->DestroyWorld(false);
        World->MarkPendingKill();
        Package->SetDirtyFlag(false);
    };
    AActor* Actor = World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Synthetic game-actor classifier fixture"), Actor))
        return false;
    UPostProcessComponent* Component = NewObject<UPostProcessComponent>(Actor, Class, TEXT("PostProcess"));
    Actor->AddInstanceComponent(Component);
    Actor->SetRootComponent(Component);
    // This is only an owned synthetic actor-name classifier fixture, not a source
    // record or export baseline. Exercise the real packed metadata reader/UI path.
    const FString Line = World->GetName() + TEXT(".") + World->PersistentLevel->GetName() + TEXT(".") +
        Actor->GetName() + TEXT(" | bHidden = false");
    const FTCHARToUTF8 Text(*Line);
    int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, Text.Length());
    TArray<uint8> Compressed;
    Compressed.SetNumUninitialized(CompressedSize);
    if (!TestTrue(TEXT("Synthetic classifier metadata packs with the actual format"),
            FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Text.Get(), Text.Length())))
        return false;
    Compressed.SetNum(CompressedSize);
    Package->GetMetaData()->SetValue(World.Get(), TEXT("P4NativeLevelImported"),
        *(FString::FromInt(Text.Length()) + TEXT(":") + FBase64::Encode(Compressed)));
    P4Kadan::ForgetGameActorNamesForTest(World.Get());
    if (!TestTrue(TEXT("Actual UI classifier recognizes the owned actor and component"),
            P4Kadan::GameActorOf(Actor) == Actor && P4Kadan::GameActorOf(Component) == Actor))
        return false;
    FP4RecoveredPostProcessMember Scalar, Vector;
    if (!P4ResolveRecoveredPostProcessMember(View, TEXT("C4IndirectLightingSpecularIntensity"), Scalar) ||
        !P4ResolveRecoveredPostProcessMember(View, TEXT("C4PostProcessParam0"), Vector))
        return false;
    const FProperty* Priority = FindFProperty<FProperty>(UPostProcessComponent::StaticClass(), TEXT("Priority"));
    if (!TestNotNull(TEXT("Unrelated common stock scalar"), Priority))
        return false;
    TestFalse(TEXT("Actual UI refuses the genuine host struct"),
        P4Kadan::CanEditGameProperty(Component, *View.ForkSettings, {}, &Why));
    TestTrue(TEXT("Actual UI gives the missing game edit/export capability reason"),
        Why.Contains(TEXT("do not yet have a game export route")));
    TestFalse(TEXT("Actual nested details property is refused by its real parent region"),
        P4Kadan::CanEditGameProperty(Component, *Scalar.Property, {View.ForkSettings}, &Why));
    TestTrue(TEXT("Common stock scalar UI route stays available"),
        P4Kadan::CanEditGameProperty(Component, *Priority, {}, &Why));
    FP4ActorValues Values;
    TArray<FString> Packages;
    TArray<FP4ActorAssetReference> References;
    const auto Plan = [&](const FProperty* Property, const void* Data, const FString& Path, const UObject* Owner)
    {
        return P4PlanActorValue(
            Property, Data, Path, TEXT("SyntheticActor"), World.Get(), Owner, Values, Packages, References, Why);
    };
    TestFalse(TEXT("Actual actor producer refuses host root instead of emitting ForkSettings paths"),
        Plan(View.ForkSettings, View.ForkSettings->ContainerPtrToValuePtr<void>(Component),
            TEXT("PostProcess.ForkSettings"), Component));
    TestTrue(TEXT("Refused host route emits no partial actor values"), Values.empty());
    TestFalse(TEXT("Direct immediate member cannot bypass planner through a renamed path"),
        Plan(Scalar.Property, P4RecoveredPostProcessMemberData(Component, View, Scalar), TEXT("Anything"), Component));
    const FStructProperty* VectorProperty = CastFieldChecked<FStructProperty>(Vector.Property);
    const FProperty* X = VectorProperty->Struct->FindPropertyByName(TEXT("X"));
    if (!TestNotNull(TEXT("Actual nested shared vector leaf"), X))
        return false;
    TestFalse(TEXT("Direct shared Vector4.X is refused by its actual native host address region"),
        Plan(X, X->ContainerPtrToValuePtr<void>(P4RecoveredPostProcessMemberData(Component, View, Vector)),
            TEXT("NotTheHostName"), Component));
    TestTrue(TEXT("All private refusals preserve empty plans"),
        Values.empty() && Packages.Num() == 0 && References.Num() == 0);
    TestTrue(TEXT("Common stock scalar planner still emits the actual game path"),
        Plan(Priority, Priority->ContainerPtrToValuePtr<void>(Component), TEXT("PostProcess.Priority"), Component));
    TestTrue(
        TEXT("Common scalar emitted exactly once"), Values.size() == 1 && Values.count("PostProcess.Priority") == 1);
    Values.clear();
    TStrongObjectPtr<UPostProcessComponent> Stock(NewObject<UPostProcessComponent>());
    // DECLARE_FIELD's operator new initializes the actual FFieldClass identity;
    // a stack property bypasses that required public allocation path.
    TUniquePtr<FFloatProperty> NameOnly(
        new FFloatProperty(FFieldVariant(UPostProcessComponent::StaticClass()), TEXT("ForkSettings"), RF_Transient));
    float Number = .25f;
    if (!TestTrue(TEXT("Unrelated same-name control has genuine float field identity"),
            NameOnly->GetClass() == FFloatProperty::StaticClass() && CastField<FFloatProperty>(NameOnly.Get()) &&
                !CastField<FBoolProperty>(NameOnly.Get())))
        return false;
    TestTrue(TEXT("An unrelated field is never refused by the host field's name alone"),
        Plan(NameOnly.Get(), &Number, TEXT("ForkSettings"), Stock.Get()));
    Values.clear();
    TStrongObjectPtr<UClass> Descendant(NewObject<UClass>(GetTransientPackage(), NAME_None, RF_Transient));
    Descendant->SetSuperStruct(Class);
    // The negative control needs genuine hierarchy metadata, not PropertyLink
    // runtime initialization on this deliberately uninstantiated test class.
    const FProperty* Inherited = FindFProperty<FProperty>(Descendant.Get(), TEXT("ForkSettings"));
    TestTrue(TEXT("Source subclass inherits the actual declaring host metadata"), Inherited == View.ForkSettings);
    FP4RecoveredPostProcessStorage Unsupported;
    FString NotSupported;
    TestFalse(TEXT("Authoring refusal grants no import storage compatibility to the subclass"),
        P4GetRecoveredPostProcessStorage(Descendant.Get(), Unsupported, NotSupported));
    TestFalse(TEXT("Source-base subobject bridge still refuses arbitrary subclasses"),
        P4BlueprintSourceSubobjectClassMatches(Descendant.Get(), UPostProcessComponent::StaticClass()));
    TestFalse(TEXT("Inherited genuine metadata cannot be emitted even without an exact owner class"),
        Plan(Inherited, View.ForkSettings->ContainerPtrToValuePtr<void>(Component), TEXT("InheritedHost"), nullptr));
    TestTrue(TEXT("Inherited refusal remains atomic"), Values.empty());
    // Exercise real pre/post editor notifications and actual restoration, not only
    // the policy query. No property flags or source objects are rewritten.
    P4Kadan::InitializePropertyGuards();
    P4Kadan::TakeReadOnlyMessages();
    const FFloatProperty* Float = CastFieldChecked<FFloatProperty>(Scalar.Property);
    void* Address = P4RecoveredPostProcessMemberData(Component, View, Scalar);
    const float Before = Float->GetPropertyValue(Address);
    FEditPropertyChain Chain;
    Chain.AddHead(const_cast<FStructProperty*>(View.ForkSettings));
    Chain.AddTail(const_cast<FProperty*>(Scalar.Property));
    Chain.SetActiveMemberPropertyNode(const_cast<FStructProperty*>(View.ForkSettings));
    Chain.SetActivePropertyNode(const_cast<FProperty*>(Scalar.Property));
    static_cast<UObject*>(Component)->PreEditChange(Chain);
    Float->SetPropertyValue(Address, Before + 3.f);
    FPropertyChangedEvent Event(const_cast<FProperty*>(Scalar.Property), EPropertyChangeType::ValueSet);
    Event.MemberProperty = const_cast<FStructProperty*>(View.ForkSettings);
    Component->PostEditChangeProperty(Event);
    P4Kadan::FlushReadOnlyGuards();
    TestEqual(TEXT("Actual unsupported editor edit is restored to its native preimage"),
        Float->GetPropertyValue(Address), Before);
    const auto Messages = P4Kadan::TakeReadOnlyMessages();
    bool Explained = false;
    for (const auto& Message : Messages)
        Explained |= Message.Contains(TEXT("do not yet have a game export route"));
    TestTrue(TEXT("Actual editor refusal explains the missing edit/export capability"), Explained);
    return !HasAnyErrors();
}
#endif
// PRIVATE DRAFT: uncompiled/unexecuted. Root owns registration and the next UE batch.
// Real captured/installed types only; no new reflected types, flags or defaults.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Camera/CameraShakeSourceComponent.h"
#include "Components/AudioComponent.h"
#include "Curves/CurveVector.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundConcurrency.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4PropertyNextDraft
{
UPackage* Package(const TCHAR* Case)
{
    return CreatePackage(*(FString(TEXT("/Game/NativePropertyNextDraft/")) + Case + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}
TArray<uint8> Bytes(UObject* Object)
{
    TArray<uint8> Result;
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    return Result;
}
bool Writable(FProperty* Property)
{
    return Property && Property->HasAnyPropertyFlags(CPF_Edit) &&
        !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
}
struct Observer
{
    UObject* Object;
    int32 Pre = 0, Post = 0;
    FDelegateHandle PreHandle, PostHandle;
    explicit Observer(UObject* InObject) : Object(InObject)
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
    ~Observer()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreHandle);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PostHandle);
    }
};
} //namespace P4PropertyNextDraft

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNextUInt32Draft, "P4MapKit.Quality.PropertyNextUInt32Draft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNextUInt32Draft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyNextDraft;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Story"));
    if (!TestNotNull(TEXT("Captured original save class"), Class) || !TestNotNull(TEXT("Undo owner"), GEditor))
        return false;
    auto* Field = CastField<FUInt32Property>(Class->FindPropertyByName(TEXT("SongUnlockFlags")));
    auto* Sibling = CastField<FDoubleProperty>(Class->FindPropertyByName(TEXT("PlayTime")));
    if (!TestNotNull(TEXT("Actual unsigned32 field"), Field) || !TestNotNull(TEXT("Actual double sibling"), Sibling) ||
        !TestTrue(TEXT("Original flags already permit instance edits"), Writable(Field)))
        return false;
    const EPropertyFlags Flags = Field->GetPropertyFlags();
    UPackage* Pkg = Package(TEXT("UInt32"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Pkg, Class, NAME_None, RF_Transactional));
    Field->SetPropertyValue_InContainer(Object.Get(), uint32(0x80000001u));
    Sibling->SetPropertyValue_InContainer(Object.Get(), 37.25);
    Pkg->SetDirtyFlag(false);
    if (!TestTrue(TEXT("Unsigned32 hexadecimal maximum edits"),
            UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("SongUnlockFlags"), TEXT("0xffffffff"))))
        return false;
    TestEqual(TEXT("All32 bits retained independently"), Field->GetPropertyValue_InContainer(Object.Get()), MAX_uint32);
    TestEqual(TEXT("Double sibling untouched"), Sibling->GetPropertyValue_InContainer(Object.Get()), 37.25);
    TestTrue(TEXT("Actual edit dirties package"), Pkg->IsDirty());
    Pkg->SetDirtyFlag(false);
    const TArray<uint8> Before = Bytes(Object.Get());
    const EObjectFlags ObjectFlags = Object->GetFlags();
    {
        Observer Observe(Object.Get());
        // Width32 overflows differ from already tested uint64 accumulation overflow.
        for (const TCHAR* Text : {TEXT("4294967296"), TEXT("0x100000000"), TEXT("-0")})
        {
            TestFalse(FString(TEXT("Unsigned32 boundary refuses: ")) + Text,
                UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("SongUnlockFlags"), Text));
            TestEqual(
                TEXT("Refusal retains native scalar"), Field->GetPropertyValue_InContainer(Object.Get()), MAX_uint32);
            TestTrue(TEXT("Refusal retains complete serialized storage"), Bytes(Object.Get()) == Before);
            TestFalse(TEXT("Refusal leaves package clean"), Pkg->IsDirty());
        }
        TestEqual(TEXT("Refusal has no pre callback"), Observe.Pre, 0);
        TestEqual(TEXT("Refusal has no post callback"), Observe.Post, 0);
    }
    TestTrue(TEXT("Native field flags unchanged"), Field->GetPropertyFlags() == Flags);
    TestTrue(TEXT("Object flags unchanged"), Object->GetFlags() == ObjectFlags);
    TestTrue(TEXT("Undo skips all refused width32 edits"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores exact highbit baseline"), Field->GetPropertyValue_InContainer(Object.Get()),
        uint32(0x80000001u));
    TestTrue(TEXT("Redo restores32 bits"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo exact maximum"), Field->GetPropertyValue_InContainer(Object.Get()), MAX_uint32);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNextClassDraft, "P4MapKit.Quality.PropertyNextClassDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNextClassDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyNextDraft;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GameSceneData"));
    UClass* Meta = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GameSceneProc"));
    UClass* Child = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.SPDefault"));
    if (!TestNotNull(TEXT("Captured original scene class"), Class) ||
        !TestNotNull(TEXT("Captured class constraint"), Meta) ||
        !TestNotNull(TEXT("Captured concrete child class"), Child) || !TestNotNull(TEXT("Undo owner"), GEditor))
        return false;
    auto* Field = CastField<FClassProperty>(Class->FindPropertyByName(TEXT("SceneProc")));
    auto* Sibling = CastField<FBoolProperty>(Class->FindPropertyByName(TEXT("bVariableOpenLevel")));
    if (!TestNotNull(TEXT("Actual class property"), Field) || !TestNotNull(TEXT("Actual bool sibling"), Sibling) ||
        !TestTrue(TEXT("Actual class field is editable"), Writable(Field)))
        return false;
    if (!TestTrue(
            TEXT("Observed class relationship exactly retained"), Field->MetaClass == Meta && Child->IsChildOf(Meta)))
        return false;
    const EPropertyFlags Flags = Field->GetPropertyFlags();
    UPackage* Pkg = Package(TEXT("Class"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Pkg, Class, NAME_None, RF_Transactional));
    Field->SetObjectPropertyValue_InContainer(Object.Get(), Meta);
    Sibling->SetPropertyValue_InContainer(Object.Get(), true);
    Pkg->SetDirtyFlag(false);
    if (!TestTrue(TEXT("Captured valid child class edits"),
            UP4AuthoringTools::SetEditablePropertyText(
                Object.Get(), TEXT("SceneProc"), TEXT("Class'/Script/Carrot4.SPDefault'"))))
        return false;
    TestEqual(TEXT("Actual UClass pointer retained"), Field->GetObjectPropertyValue_InContainer(Object.Get()),
        static_cast<UObject*>(Child));
    TestTrue(TEXT("Class edit preserves bool sibling"), Sibling->GetPropertyValue_InContainer(Object.Get()));
    TestTrue(TEXT("Class edit dirties package"), Pkg->IsDirty());
    Pkg->SetDirtyFlag(false);
    const TArray<uint8> Before = Bytes(Object.Get());
    const EObjectFlags ObjectFlags = Object->GetFlags();
    {
        Observer Observe(Object.Get());
        for (const TCHAR* Text :
            {TEXT("Class'/Script/Engine.StaticMesh'"), TEXT("Class'/Script/Carrot4.P4DefinitelyMissingClass'"),
                TEXT("Class'/Script/Carrot4.SPDefault' trailing")})
        {
            TestFalse(FString(TEXT("Constrained class refusal: ")) + Text,
                UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("SceneProc"), Text));
            TestEqual(TEXT("Refusal preserves actual class pointer"),
                Field->GetObjectPropertyValue_InContainer(Object.Get()), static_cast<UObject*>(Child));
            TestTrue(TEXT("Class refusal leaves serialized state unchanged"), Bytes(Object.Get()) == Before);
            TestFalse(TEXT("Class refusal preserves clean package"), Pkg->IsDirty());
        }
        TestEqual(TEXT("Class refusals have no pre callbacks"), Observe.Pre, 0);
        TestEqual(TEXT("Class refusals have no post callbacks"), Observe.Post, 0);
    }
    TestTrue(TEXT("Class field flags unchanged"), Field->GetPropertyFlags() == Flags);
    TestTrue(TEXT("Object flags unchanged"), Object->GetFlags() == ObjectFlags);
    TestTrue(TEXT("Undo skips class refusals"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores native meta-class pointer"), Field->GetObjectPropertyValue_InContainer(Object.Get()),
        static_cast<UObject*>(Meta));
    TestTrue(TEXT("Class Redo succeeds"), GEditor->RedoTransaction());
    TestEqual(TEXT("Class Redo restores captured child pointer"),
        Field->GetObjectPropertyValue_InContainer(Object.Get()), static_cast<UObject*>(Child));
    if (!TestTrue(TEXT("Explicit None may clear nullable class field"),
            UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("SceneProc"), TEXT("None"))))
        return false;
    TestNull(TEXT("Explicit None has native null pointer"), Field->GetObjectPropertyValue_InContainer(Object.Get()));
    TestTrue(TEXT("Undo restores cleared class"), GEditor->UndoTransaction());
    TestEqual(TEXT("Class clear Undo retains child identity"), Field->GetObjectPropertyValue_InContainer(Object.Get()),
        static_cast<UObject*>(Child));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNextEnumsDraft, "P4MapKit.Quality.PropertyNextEnumsDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNextEnumsDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyNextDraft;
    if (!TestNotNull(TEXT("Undo owner"), GEditor))
        return false;
    UPackage* Pkg = Package(TEXT("Enums"));
    TStrongObjectPtr<UCameraShakeSourceComponent> Object(
        NewObject<UCameraShakeSourceComponent>(Pkg, NAME_None, RF_Transactional));
    auto* Enum = CastField<FEnumProperty>(Object->GetClass()->FindPropertyByName(TEXT("Attenuation")));
    if (!TestNotNull(TEXT("Installed genuine enumclass field"), Enum) ||
        !TestTrue(TEXT("Enum genuinely editable"), Writable(Enum)))
        return false;
    const EPropertyFlags Flags = Enum->GetPropertyFlags();
    Object->Attenuation = ECameraShakeAttenuation::Linear;
    Object->InnerAttenuationRadius = 123.25f;
    Pkg->SetDirtyFlag(false);
    if (!TestTrue(TEXT("Named enumclass edit"),
            UP4AuthoringTools::SetEditablePropertyText(
                Object.Get(), TEXT("Attenuation"), TEXT("ECameraShakeAttenuation::Quadratic"))))
        return false;
    TestTrue(TEXT("Native enum member is Quadratic"), Object->Attenuation == ECameraShakeAttenuation::Quadratic);
    TestEqual(TEXT("Enum edit preserves float sibling"), Object->InnerAttenuationRadius, 123.25f);
    TestTrue(TEXT("Enum edit dirties package"), Pkg->IsDirty());
    Pkg->SetDirtyFlag(false);
    const TArray<uint8> Before = Bytes(Object.Get());
    {
        Observer Observe(Object.Get());
        for (const TCHAR* Text : {TEXT("P4DefinitelyMissingEnumerator"), TEXT("256"), TEXT("1.5"), TEXT("1 trailing")})
        {
            TestFalse(FString(TEXT("Enumclass invalid input refuses: ")) + Text,
                UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("Attenuation"), Text));
            TestTrue(TEXT("Enum refusal retains native Quadratic"),
                Object->Attenuation == ECameraShakeAttenuation::Quadratic);
            TestTrue(TEXT("Enum refusal preserves native bytes"), Bytes(Object.Get()) == Before);
            TestFalse(TEXT("Enum refusal remains clean"), Pkg->IsDirty());
        }
        TestEqual(TEXT("Enum refusal no pre callbacks"), Observe.Pre, 0);
        TestEqual(TEXT("Enum refusal no post callbacks"), Observe.Post, 0);
    }
    TestTrue(TEXT("Enum flags preserved"), Enum->GetPropertyFlags() == Flags);
    TestTrue(TEXT("Undo bypasses failed enum imports"), GEditor->UndoTransaction());
    TestTrue(TEXT("Enum Undo native Linear"), Object->Attenuation == ECameraShakeAttenuation::Linear);
    TestTrue(TEXT("Enum Redo succeeds"), GEditor->RedoTransaction());
    TestTrue(TEXT("Enum Redo native Quadratic"), Object->Attenuation == ECameraShakeAttenuation::Quadratic);
    TestTrue(TEXT("Numeric named-value import is supported"),
        UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("Attenuation"), TEXT("0")));
    TestTrue(TEXT("Numeric enum stores native Linear"), Object->Attenuation == ECameraShakeAttenuation::Linear);

    // Separate installed TEnumAsByte route. Existing aggregate tests only use an
    // already invalid sibling; this directly imports valid and invalid byte enums.
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(Pkg, NAME_None, RF_Transactional));
    Curve->FloatCurves[0].AddKey(2.0f, 7.0f);
    Curve->FloatCurves[0].Keys[0].InterpMode = RCIM_Constant;
    auto* Curves = CastField<FStructProperty>(Curve->GetClass()->FindPropertyByName(TEXT("FloatCurves")));
    auto* Keys = Curves ? CastField<FArrayProperty>(Curves->Struct->FindPropertyByName(TEXT("Keys"))) : nullptr;
    auto* KeyStruct = Keys ? CastField<FStructProperty>(Keys->Inner) : nullptr;
    auto* Byte = KeyStruct ? CastField<FByteProperty>(KeyStruct->Struct->FindPropertyByName(TEXT("InterpMode")))
                           : nullptr;
    if (!TestNotNull(TEXT("Installed real enum-backed byte"), Byte) ||
        !TestNotNull(TEXT("Byte enum exists"), Byte->Enum))
        return false;
    TestTrue(TEXT("Byte-enum name imports"),
        UP4AuthoringTools::SetPropertyText(
            Curve.Get(), TEXT("FloatCurves[0].Keys[0].InterpMode"), TEXT("RCIM_Linear")));
    TestTrue(TEXT("Native byteenum Linear"), Curve->FloatCurves[0].Keys[0].InterpMode == RCIM_Linear);
    TestTrue(TEXT("Byte-enum numeric imports"),
        UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[0].Keys[0].InterpMode"), TEXT("1")));
    TestTrue(TEXT("Native byteenum Constant"), Curve->FloatCurves[0].Keys[0].InterpMode == RCIM_Constant);
    const TArray<uint8> ByteBefore = Bytes(Curve.Get());
    Pkg->SetDirtyFlag(false);
    for (const TCHAR* Text :
        {TEXT("P4DefinitelyMissingByteEnumerator"), TEXT("256"), TEXT("-1"), TEXT("RCIM_Linear trailing")})
    {
        TestFalse(FString(TEXT("Byte enum refuses: ")) + Text,
            UP4AuthoringTools::SetPropertyText(Curve.Get(), TEXT("FloatCurves[0].Keys[0].InterpMode"), Text));
        TestTrue(TEXT("Byteenum failure preserves native bytes"), Bytes(Curve.Get()) == ByteBefore);
        TestFalse(TEXT("Transport refusal does not dirty package"), Pkg->IsDirty());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNextSetDraft, "P4MapKit.Quality.PropertyNextSetRefusalDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNextSetDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyNextDraft;
    if (!TestNotNull(TEXT("Undo owner"), GEditor))
        return false;
    UPackage* Pkg = Package(TEXT("Set"));
    TStrongObjectPtr<UAudioComponent> Object(NewObject<UAudioComponent>(Pkg, NAME_None, RF_Transactional));
    TStrongObjectPtr<USoundConcurrency> First(NewObject<USoundConcurrency>(Pkg, NAME_None, RF_Transient));
    TStrongObjectPtr<USoundConcurrency> Second(NewObject<USoundConcurrency>(Pkg, NAME_None, RF_Transient));
    auto* Set = CastField<FSetProperty>(Object->GetClass()->FindPropertyByName(TEXT("ConcurrencySet")));
    if (!TestNotNull(TEXT("Installed reflected editable set"), Set) ||
        !TestTrue(TEXT("Real set flags editable"), Writable(Set)))
        return false;
    const EPropertyFlags Flags = Set->GetPropertyFlags();
    Object->ConcurrencySet = {First.Get(), Second.Get()};
    Object->VolumeMultiplier = 1.25f;
    if (!TestTrue(TEXT("Successful scalar Undo anchor"),
            UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("VolumeMultiplier"), TEXT("2.5"))))
        return false;
    Pkg->SetDirtyFlag(false);
    const TArray<uint8> Before = Bytes(Object.Get());
    const EObjectFlags ObjectFlags = Object->GetFlags();
    TestTrue(TEXT("Unsupported set projection explicitly refuses"),
        UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("ConcurrencySet")).IsEmpty());
    // ExportText is used only for a native import acceptance control. Oracle is
    // the direct populated TSet, never the plugin JSON or another parser.
    FString NativeText;
    Set->ExportTextItem(
        NativeText, Set->ContainerPtrToValuePtr<void>(Object.Get()), nullptr, Object.Get(), PPF_None, nullptr);
    TSet<USoundConcurrency*> Control;
    const TCHAR* End = Set->ImportText(*NativeText, &Control, PPF_None, Object.Get(), nullptr);
    if (!TestTrue(TEXT("Native engine can read the exact real set text"),
            End && !*End && Control.Num() == 2 && Control.Contains(First.Get()) && Control.Contains(Second.Get())))
        return false;
    {
        Observer Observe(Object.Get());
        for (const FString& Text : TArray<FString>{NativeText, TEXT("()"), TEXT("(None)")})
        {
            TestFalse(TEXT("Editable unsupported set refuses before publication"),
                UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("ConcurrencySet"), Text));
            TestFalse(TEXT("Transport unsupported set also refuses"),
                UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("ConcurrencySet"), Text));
            TestTrue(TEXT("Native set contents exactly retained"),
                Object->ConcurrencySet.Num() == 2 && Object->ConcurrencySet.Contains(First.Get()) &&
                    Object->ConcurrencySet.Contains(Second.Get()));
            TestTrue(TEXT("Set refusal preserves complete native serialization"), Bytes(Object.Get()) == Before);
            TestFalse(TEXT("Set refusal preserves clean package"), Pkg->IsDirty());
        }
        TestFalse(TEXT("Set is not exposed as an indexed array"),
            UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("ConcurrencySet[0]"), TEXT("None")));
        TestEqual(TEXT("Set refusal has no pre callback"), Observe.Pre, 0);
        TestEqual(TEXT("Set refusal has no post callback"), Observe.Post, 0);
    }
    TestTrue(TEXT("Set field flags unchanged"), Set->GetPropertyFlags() == Flags);
    TestTrue(TEXT("Set owner flags unchanged"), Object->GetFlags() == ObjectFlags);
    TestTrue(TEXT("Undo skips every set refusal"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores native scalar anchor"), Object->VolumeMultiplier, 1.25f);
    TestTrue(TEXT("Undo retains actual two-element set"),
        Object->ConcurrencySet.Num() == 2 && Object->ConcurrencySet.Contains(First.Get()) &&
            Object->ConcurrencySet.Contains(Second.Get()));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyNextInterfaceDraft, "P4MapKit.Quality.PropertyNextInterfaceRefusalDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyNextInterfaceDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyNextDraft;
    // Reflection-only dependency: these actual installed classes must already be
    // loaded by the editor host. No module load, fake owner or silent skip.
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/InteractiveToolsFramework.GizmoScaledTransformSource"));
    UClass* ChildClass = FindObject<UClass>(
        nullptr, TEXT("/Script/InteractiveToolsFramework.GizmoTransformProxyTransformSource"));
    if (!TestNotNull(TEXT("Required installed interface owner already loaded"), Class) ||
        !TestNotNull(TEXT("Required installed interface implementer already loaded"), ChildClass) ||
        !TestNotNull(TEXT("Undo owner"), GEditor))
        return false;
    auto* Field = CastField<FInterfaceProperty>(Class->FindPropertyByName(TEXT("ChildTransformSource")));
    if (!TestNotNull(TEXT("Actual native interface property"), Field) ||
        !TestNotNull(TEXT("Actual interface class"), Field->InterfaceClass))
        return false;
    const EPropertyFlags Flags = Field->GetPropertyFlags();
    UPackage* Pkg = Package(TEXT("Interface"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Pkg, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UObject> Child(NewObject<UObject>(Pkg, ChildClass, NAME_None, RF_Transient));
    void* Interface = Child->GetInterfaceAddress(Field->InterfaceClass);
    if (!TestNotNull(TEXT("Independent native interface address exists"), Interface))
        return false;
    auto* Stored = Field->ContainerPtrToValuePtr<FScriptInterface>(Object.Get());
    Stored->SetObject(Child.Get());
    Stored->SetInterface(Interface);
    TStrongObjectPtr<UCameraShakeSourceComponent> Anchor(
        NewObject<UCameraShakeSourceComponent>(Pkg, NAME_None, RF_Transactional));
    Anchor->Attenuation = ECameraShakeAttenuation::Linear;
    if (!TestTrue(TEXT("Successful separate Undo anchor"),
            UP4AuthoringTools::SetEditablePropertyText(
                Anchor.Get(), TEXT("Attenuation"), TEXT("ECameraShakeAttenuation::Quadratic"))))
        return false;
    Pkg->SetDirtyFlag(false);
    const TArray<uint8> Before = Bytes(Object.Get());
    const EObjectFlags ObjectFlags = Object->GetFlags();
    TestTrue(TEXT("Unsupported interface projection refuses"),
        UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("ChildTransformSource")).IsEmpty());
    // Literal None avoids relying on engine interface export spelling. Direct
    // ImportText first proves the transport reaches supported native syntax.
    FScriptInterface Control;
    Control.SetObject(Child.Get());
    Control.SetInterface(Interface);
    const TCHAR* End = Field->ImportText(TEXT("None"), &Control, PPF_None, Object.Get(), nullptr);
    if (!TestTrue(TEXT("Engine interface None control accepts and clears both pointers"),
            End && !*End && !Control.GetObject() && !Control.GetInterface()))
        return false;
    {
        Observer Observe(Object.Get());
        TestFalse(TEXT("Transport refuses unsupported interface JSON even for native None"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("ChildTransformSource"), TEXT("None")));
        TestFalse(TEXT("No forged interface editable flags"),
            UP4AuthoringTools::SetEditablePropertyText(Object.Get(), TEXT("ChildTransformSource"), TEXT("None")));
        TestEqual(TEXT("Interface UObject pointer unchanged"), Stored->GetObject(), Child.Get());
        TestTrue(TEXT("Native interface address unchanged"), Stored->GetInterface() == Interface);
        TestTrue(TEXT("Interface refusal retains serialized object"), Bytes(Object.Get()) == Before);
        TestEqual(TEXT("Interface refusal has no pre callback"), Observe.Pre, 0);
        TestEqual(TEXT("Interface refusal has no post callback"), Observe.Post, 0);
    }
    TestFalse(TEXT("Interface refusal preserves clean package"), Pkg->IsDirty());
    TestTrue(TEXT("Interface field flags unchanged"), Field->GetPropertyFlags() == Flags);
    TestTrue(TEXT("Interface owner flags unchanged"), Object->GetFlags() == ObjectFlags);
    TestTrue(TEXT("Undo skips interface refusals"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo reaches separate native anchor"), Anchor->Attenuation == ECameraShakeAttenuation::Linear);
    TestEqual(TEXT("Undo never discards interface UObject pointer"), Stored->GetObject(), Child.Get());
    TestTrue(TEXT("Undo never discards interface address"), Stored->GetInterface() == Interface);
    return !HasAnyErrors();
}
#endif

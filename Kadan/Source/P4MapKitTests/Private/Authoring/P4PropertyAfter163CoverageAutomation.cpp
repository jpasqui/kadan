// PRIVATE after163 proposal. Uncompiled/unexecuted; no canonical integration.
// Rebind to root's complete163 measurement before promotion. Genuine outcomes:
// plain byte/uint16 exact import and leading-zero decimal (128/134), original transient table projection
// (783/812), complete nonfinite row refusal (787), persistent asset copy retaining
// actual transient caches (845/892), nested transient schema (450), nested fixed
// arrays and zero-sign identity (545/548/553), absent row-schema read (1012).
// No metadata edits, invented types/defaults, private API access, or cache bytes
// fabricated for coverage. Original constructor/runtime equivalence not claimed.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/IntSerialization.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace P4PropertyAfter163
{
UPackage* MakePackage(const TCHAR* Case)
{
    return CreatePackage(*(FString(TEXT("/Game/NativePropertyAfter163/")) + Case + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}
TArray<uint8> NativeBytes(UObject* Object)
{
    TArray<uint8> Result;
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    return Result;
}
struct ReadGuard
{
    UObject* Object;
    UClass* Class;
    TArray<uint8> Bytes;
    EObjectFlags Flags;
    bool Dirty;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    explicit ReadGuard(UObject* In)
        : Object(In), Class(In->GetClass()), Bytes(NativeBytes(In)), Flags(In->GetFlags()),
          Dirty(In->GetOutermost()->IsDirty())
    {
        A = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Changed, const FEditPropertyChain&)
            {
                if (Changed == Object)
                    ++Pre;
            });
        B = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Changed, FPropertyChangedEvent&)
            {
                if (Changed == Object)
                    ++Post;
            });
    }
    ~ReadGuard()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    }
    void Check(FAutomationTestBase& Test)
    {
        Test.TestTrue(TEXT("Independent native bytes unchanged"), NativeBytes(Object) == Bytes);
        Test.TestTrue(
            TEXT("Actual class and flags unchanged"), Object->GetClass() == Class && Object->GetFlags() == Flags);
        Test.TestEqual(TEXT("Prior package dirty state retained"), Object->GetOutermost()->IsDirty(), Dirty);
        Test.TestEqual(TEXT("No pre-edit callback"), Pre, 0);
        Test.TestEqual(TEXT("No post-edit callback"), Post, 0);
    }
};
uint32 FloatBits(float Value)
{
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}
TArray<uint8> FieldStorage(FProperty* Field, void* Container)
{
    TArray<uint8> Bytes;
    Bytes.Append(static_cast<const uint8*>(Field->ContainerPtrToValuePtr<void>(Container)),
        Field->ElementSize * Field->ArrayDim);
    return Bytes;
}
} //namespace P4PropertyAfter163

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter163NarrowIntegers, "P4MapKit.Quality.PropertyAfter163NarrowIntegers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter163NarrowIntegers::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter163;
    UPackage* Owner = MakePackage(TEXT("NativeIntegers"));
    TStrongObjectPtr<UIntSerialization> Object(NewObject<UIntSerialization>(Owner, NAME_None, RF_Transactional));
    auto* Byte = CastField<FByteProperty>(Object->GetClass()->FindPropertyByName(TEXT("UnsignedInt8Variable")));
    auto* Word = CastField<FUInt16Property>(Object->GetClass()->FindPropertyByName(TEXT("UnsignedInt16Variable")));
    if (!TestTrue(TEXT("Installed native plain byte and uint16, without enum/metadata invention"),
            Byte && !Byte->Enum && Word))
        return false;
    const EPropertyFlags ByteFlags = Byte->GetPropertyFlags(), WordFlags = Word->GetPropertyFlags();
    // These actual engine fields have UPROPERTY() with no editor permission.
    // Use existing privileged source-import API, not invented editable flags.
    Object->UnsignedInt8Variable = 17;
    Object->UnsignedInt16Variable = 1234;
    Object->SignedInt8Variable = -19;
    Object->SignedInt16Variable = -123;
    Object->SignedInt64Variable = int64(9007199254740993LL);
    TestTrue(TEXT("Plain byte retains all eight bits"),
        UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("UnsignedInt8Variable"), TEXT("0255")));
    TestEqual(TEXT("Leading zero is decimal, not discarded or octal"), Object->UnsignedInt8Variable, uint8(255));
    TestTrue(TEXT("Native uint16 retains all sixteen bits"),
        UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("UnsignedInt16Variable"), TEXT("065535")));
    TestEqual(TEXT("Independent word getter is exact"), Object->UnsignedInt16Variable, uint16(65535));
    TestTrue(TEXT("Native signed8 minimum accepts"),
        UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SignedInt8Variable"), TEXT("-0x80")));
    TestTrue(TEXT("Native signed16 minimum accepts"),
        UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SignedInt16Variable"), TEXT("-0x8000")));
    TestEqual(TEXT("Signed eight bits exact"), Object->SignedInt8Variable, int8(-128));
    TestEqual(TEXT("Signed sixteen bits exact"), Object->SignedInt16Variable, int16(-32768));
    Owner->SetDirtyFlag(false);
    {
        ReadGuard State(Object.Get());
        for (const TCHAR* Text : {TEXT("256"), TEXT("-0"), TEXT("0x100"), TEXT("1.5")})
            TestFalse(TEXT("Plain byte refuses truncation/sign/fraction"),
                UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("UnsignedInt8Variable"), Text));
        for (const TCHAR* Text : {TEXT("65536"), TEXT("-1"), TEXT("0x10000"), TEXT("1e2")})
            TestFalse(TEXT("Native uint16 refuses overflow/sign/exponent"),
                UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("UnsignedInt16Variable"), Text));
        for (const TCHAR* Text : {TEXT("128"), TEXT("-129")})
            TestFalse(TEXT("Native signed8 overflow refuses"),
                UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SignedInt8Variable"), Text));
        for (const TCHAR* Text : {TEXT("32768"), TEXT("-32769")})
            TestFalse(TEXT("Native signed16 overflow refuses"),
                UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("SignedInt16Variable"), Text));
        TestEqual(
            TEXT("Above-double sibling remains native-exact"), Object->SignedInt64Variable, int64(9007199254740993LL));
        State.Check(*this);
    }
    TestTrue(TEXT("Original native property flags unchanged"),
        Byte->GetPropertyFlags() == ByteFlags && Word->GetPropertyFlags() == WordFlags);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter163RumbleRows, "P4MapKit.Quality.PropertyAfter163RumbleRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter163RumbleRows::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter163;
    UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.HDRumbleData"));
    if (!TestTrue(TEXT("Original rumble TableRowBase schema exists"),
            Struct && Struct->IsChildOf(FTableRowBase::StaticStruct())))
        return false;
    auto* Gain = CastField<FFloatProperty>(Struct->FindPropertyByName(TEXT("Gain")));
    auto* Layer = CastField<FEnumProperty>(Struct->FindPropertyByName(TEXT("Layer")));
    auto* CacheA = CastField<FStructProperty>(Struct->FindPropertyByName(TEXT("ResourceFileList")));
    auto* CacheB = CastField<FStructProperty>(Struct->FindPropertyByName(TEXT("AttenuationList")));
    if (!TestTrue(TEXT("Actual original cache flags and persistent native fields"),
            Gain && Layer && CacheA && CacheB && CacheA->HasAnyPropertyFlags(CPF_Transient) &&
                CacheB->HasAnyPropertyFlags(CPF_Transient)))
        return false;
    UPackage* Owner = MakePackage(TEXT("RumbleRows"));
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(Owner, NAME_None, RF_Transactional));
    Table->RowStruct = Struct;
    FStructOnScope Row(Struct);
    int32 Persistent = 0;
    for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        ++Persistent;
        void* Value = Field->ContainerPtrToValuePtr<void>(Row.GetStructMemory());
        if (auto* Float = CastField<FFloatProperty>(*Field))
            Float->SetPropertyValue(Value,
                Field->GetFName() == TEXT("Gain")        ? 1.5f
                    : Field->GetFName() == TEXT("Pitch") ? .75f
                                                         : 2.25f);
        else if (auto* Name = CastField<FNameProperty>(*Field))
            Name->SetPropertyValue(Value, FName(TEXT("NativeRumbleFixture")));
        else if (auto* String = CastField<FStrProperty>(*Field))
            String->SetPropertyValue(Value, TEXT("Authored native comment"));
        else if (auto* Bool = CastField<FBoolProperty>(*Field))
            Bool->SetPropertyValue(Value, true);
        else if (auto* Enum = CastField<FEnumProperty>(*Field))
        {
            if (!TestTrue(TEXT("Native enum has a valid declared ordinal"),
                    Enum->GetEnum() && Enum->GetEnum()->NumEnums() > 0 &&
                        Enum->GetEnum()->IsValidEnumValue(Enum->GetEnum()->GetValueByIndex(0))))
                return false;
            Enum->GetUnderlyingProperty()->SetIntPropertyValue(Value, Enum->GetEnum()->GetValueByIndex(0));
        }
        else
        {
            AddError(TEXT("Unexpected persistent original field requires an explicit fixture"));
            return false;
        }
    }
    const FName Key(TEXT("AuthoredRumbleRow"));
    Table->AddRow(Key, *reinterpret_cast<const FTableRowBase*>(Row.GetStructMemory()));
    uint8* const* Stored = Table->GetRowMap().Find(Key);
    if (!TestNotNull(TEXT("Copied actual native row"), Stored))
        return false;
    const auto CacheABytes = FieldStorage(CacheA, *Stored), CacheBBytes = FieldStorage(CacheB, *Stored);
    Owner->SetDirtyFlag(false);
    {
        ReadGuard State(Table.Get());
        const FString Json = UP4AuthoringTools::NativeTableRowsJson(Table.Get());
        TArray<TSharedPtr<FJsonValue>> Rows;
        if (!TestTrue(TEXT("Complete original row projection is valid JSON"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Rows) && Rows.Num() == 1))
            return false;
        auto Values = Rows[0]->AsObject();
        if (!TestTrue(TEXT("Projected original row has complete persistent field count"),
                Values.IsValid() && Values->Values.Num() == Persistent + 1))
            return false;
        TestEqual(TEXT("Outer row identity preserved"), Values->GetStringField(TEXT("Name")), Key.ToString());
        TestEqual(TEXT("Authored native finite gain retained"), Values->GetNumberField(TEXT("Gain")), 1.5);
        TestEqual(TEXT("Authored native pitch retained"), Values->GetNumberField(TEXT("Pitch")), .75);
        TestEqual(TEXT("Authored native speed retained"), Values->GetNumberField(TEXT("Speed")), 2.25);
        TestEqual(TEXT("Authored native comment retained"), Values->GetStringField(TEXT("Comment")),
            FString(TEXT("Authored native comment")));
        TestFalse(TEXT("Runtime resource cache is omitted"), Values->HasField(TEXT("ResourceFileList")));
        TestFalse(TEXT("Runtime attenuation cache is omitted"), Values->HasField(TEXT("AttenuationList")));
        const FString Schema = UP4AuthoringTools::NativeTableSchema(Table.Get());
        TestFalse(TEXT("Native row schema remains inspectable"), Schema.IsEmpty());
        TestFalse(TEXT("Resource cache is not exposed as persistent schema"),
            Schema.Contains(TEXT("\"name\":\"ResourceFileList\"")));
        TestFalse(TEXT("Attenuation cache is not exposed as persistent schema"),
            Schema.Contains(TEXT("\"name\":\"AttenuationList\"")));
        TestTrue(TEXT("Opaque initialized cache storage is preserved, not fabricated"),
            FieldStorage(CacheA, *Stored) == CacheABytes && FieldStorage(CacheB, *Stored) == CacheBBytes);
        State.Check(*this);
    }
    const uint32 Nan = 0x7fc04567u;
    FMemory::Memcpy(Gain->ContainerPtrToValuePtr<void>(*Stored), &Nan, sizeof(Nan));
    {
        ReadGuard State(Table.Get());
        TestTrue(TEXT("Unrepresentable original row refuses complete JSON, never a partial row"),
            UP4AuthoringTools::NativeTableRowsJson(Table.Get()).IsEmpty());
        TestEqual(TEXT("Native NaN payload remains exact after refusal"),
            FloatBits(Gain->GetPropertyValue_InContainer(*Stored)), Nan);
        TestTrue(TEXT("Row pointer and both native caches survive refusal"),
            Table->GetRowMap().FindChecked(Key) == *Stored && FieldStorage(CacheA, *Stored) == CacheABytes &&
                FieldStorage(CacheB, *Stored) == CacheBBytes);
        State.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter163GraphicsCaches, "P4MapKit.Quality.PropertyAfter163GraphicsCaches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter163GraphicsCaches::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter163;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GraphicsMgr"));
    auto* Persistent = Class ? CastField<FClassProperty>(Class->FindPropertyByName(TEXT("BP_DebugShapeDrawer")))
                             : nullptr;
    auto* Cache = Class ? CastField<FObjectProperty>(Class->FindPropertyByName(TEXT("DTDitherMPA"))) : nullptr;
    if (!TestTrue(TEXT("Actual original data asset with persistent class and transient table cache"),
            Class && Class->IsChildOf(UDataAsset::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract) &&
                Persistent && Persistent->MetaClass && !Persistent->HasAnyPropertyFlags(CPF_Transient) && Cache &&
                Cache->PropertyClass == UDataTable::StaticClass() && Cache->HasAnyPropertyFlags(CPF_Transient)))
        return false;
    UPackage* SourceOwner = MakePackage(TEXT("GraphicsSource"));
    UPackage* Owner = MakePackage(TEXT("GraphicsTarget"));
    TStrongObjectPtr<UObject> Source(NewObject<UObject>(SourceOwner, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UObject> Target(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UDataTable> SourceCache(NewObject<UDataTable>(SourceOwner));
    TStrongObjectPtr<UDataTable> TargetCache(NewObject<UDataTable>(Owner));
    SourceCache->RowStruct = TargetCache->RowStruct = FTableRowBase::StaticStruct();
    Persistent->SetObjectPropertyValue_InContainer(Source.Get(), Persistent->MetaClass);
    Persistent->SetObjectPropertyValue_InContainer(Target.Get(), nullptr);
    Cache->SetObjectPropertyValue_InContainer(Source.Get(), SourceCache.Get());
    Cache->SetObjectPropertyValue_InContainer(Target.Get(), TargetCache.Get());
    Owner->SetDirtyFlag(false);
    SourceOwner->SetDirtyFlag(false);
    {
        ReadGuard SourceState(Source.Get()), TargetState(Target.Get());
        const FString Values = UP4AuthoringTools::NativeAssetValuesJson(Source.Get());
        TestFalse(TEXT("Original manager persistent state has complete projection"), Values.IsEmpty());
        TestFalse(TEXT("Nonempty transient table cache is excluded from persistent values"),
            Values.Contains(TEXT("\"DTDitherMPA\":")));
        TestFalse(TEXT("Other original runtime cache fields are excluded"),
            Values.Contains(TEXT("\"SharedMaterialsLoad\":")) || Values.Contains(TEXT("\"PlayerCameraArray\":")));
        SourceState.Check(*this);
        TargetState.Check(*this);
    }
    const auto SourceBytes = NativeBytes(Source.Get());
    TestTrue(TEXT("Original persistent manager fields copy successfully"),
        UP4AuthoringTools::CopyNativeAssetValues(Target.Get(), Source.Get()));
    TestTrue(TEXT("Actual persistent constrained class pointer copied"),
        Persistent->GetObjectPropertyValue_InContainer(Target.Get()) == Persistent->MetaClass);
    TestTrue(TEXT("Target retains its own native cache identity"),
        Cache->GetObjectPropertyValue_InContainer(Target.Get()) == TargetCache.Get());
    TestTrue(TEXT("Source retains its own native cache identity and bytes"),
        Cache->GetObjectPropertyValue_InContainer(Source.Get()) == SourceCache.Get() &&
            NativeBytes(Source.Get()) == SourceBytes);
    TestFalse(TEXT("Source package remains clean after target copy"), SourceOwner->IsDirty());
    TestTrue(TEXT("Native successful copy dirties package"), Owner->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter163ComponentSchema, "P4MapKit.Quality.PropertyAfter163ComponentSchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter163ComponentSchema::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter163;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CharacterEditComponent"));
    auto* Blink = Class ? CastField<FStructProperty>(Class->FindPropertyByName(TEXT("BlinkControl"))) : nullptr;
    auto* Cache = Blink ? CastField<FObjectProperty>(Blink->Struct->FindPropertyByName(TEXT("ChrEditComp"))) : nullptr;
    auto* Time = Blink ? CastField<FFloatProperty>(Blink->Struct->FindPropertyByName(TEXT("OpenTime"))) : nullptr;
    if (!TestTrue(TEXT("Actual original nontransient actor-control struct with transient owner cache"),
            Class && !Class->HasAnyClassFlags(CLASS_Abstract) && Blink && !Blink->HasAnyPropertyFlags(CPF_Transient) &&
                Cache && Cache->HasAnyPropertyFlags(CPF_Transient) && Time && Class->IsChildOf(Cache->PropertyClass)))
        return false;
    UPackage* Owner = MakePackage(TEXT("ComponentSchema"));
    TStrongObjectPtr<UObject> Component(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    void* Value = Blink->ContainerPtrToValuePtr<void>(Component.Get());
    Cache->SetObjectPropertyValue_InContainer(Value, Component.Get());
    Time->SetPropertyValue_InContainer(Value, 7.25f);
    Owner->SetDirtyFlag(false);
    ReadGuard State(Component.Get());
    const FString Schema = UP4AuthoringTools::NativeAssetSchema(Component.Get());
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("Actual component schema projects valid JSON"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Schema), Root) && Root.IsValid()))
        return false;
    TSharedPtr<FJsonObject> BlinkSchema;
    for (const auto& Field : Root->GetArrayField(TEXT("fields")))
        if (Field->AsObject()->GetStringField(TEXT("name")) == TEXT("BlinkControl"))
            BlinkSchema = Field->AsObject();
    if (!TestTrue(TEXT("Original native BlinkControl is exposed"), BlinkSchema.IsValid()))
        return false;
    bool SawTime = false, SawCache = false;
    for (const auto& Field : BlinkSchema->GetArrayField(TEXT("fields")))
    {
        const FString Name = Field->AsObject()->GetStringField(TEXT("name"));
        SawTime |= Name == TEXT("OpenTime");
        SawCache |= Name == TEXT("ChrEditComp");
    }
    TestTrue(TEXT("Original persistent parameter is exposed"), SawTime);
    TestFalse(TEXT("Original runtime owner cache is omitted from persistent schema"), SawCache);
    TestEqual(
        TEXT("Independent native actor-control parameter retained"), Time->GetPropertyValue_InContainer(Value), 7.25f);
    TestTrue(TEXT("Independent native owner cache retained"),
        Cache->GetObjectPropertyValue_InContainer(Value) == Component.Get());
    State.Check(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter163NestedFixedArray,
    "P4MapKit.Quality.PropertyAfter163NestedFixedArray",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter163NestedFixedArray::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter163;
    if (!TestNotNull(TEXT("Native transaction owner"), GEditor))
        return false;
    UPackage* Owner = MakePackage(TEXT("PostProcess"));
    TStrongObjectPtr<UCameraComponent> Camera(NewObject<UCameraComponent>(Owner, NAME_None, RF_Transactional));
    auto* Settings = CastField<FStructProperty>(Camera->GetClass()->FindPropertyByName(TEXT("PostProcessSettings")));
    auto* Tints = Settings ? CastField<FStructProperty>(Settings->Struct->FindPropertyByName(TEXT("LensFlareTints")))
                           : nullptr;
    if (!TestTrue(TEXT("Actual engine editable root and nested fixed eight-color array"),
            Settings && Settings->HasAnyPropertyFlags(CPF_Edit) && Tints && Tints->ArrayDim == 8 &&
                Tints->HasAnyPropertyFlags(CPF_Edit)))
        return false;
    // CameraComponent.h exposes the root with Interp. Installed UHT parser
    // HeaderParser.cpp:3945 adds CPF_Edit for Interp; verify actual installed
    // runtime flags above rather than changing them. Component stays unregistered.
    TestFalse(TEXT("Native camera fixture stays unregistered"), Camera->IsRegistered());
    Camera->PostProcessSettings.bOverride_LensFlareTints = true;
    for (int32 Index = 0; Index < 8; ++Index)
        Camera->PostProcessSettings.LensFlareTints[Index] = FLinearColor(float(Index), .25f, .5f, 1.0f);
    const auto Before = NativeBytes(Camera.Get());
    Owner->SetDirtyFlag(false);
    {
        ReadGuard State(Camera.Get());
        TestTrue(TEXT("Partial whole-root request preserves real nested static slots as no-op"),
            UP4AuthoringTools::SetEditablePropertyText(Camera.Get(), TEXT("PostProcessSettings"), TEXT("()")));
        State.Check(*this);
    }
    const FString Text = TEXT("(LensFlareTints=((R=-0.0),(),(),(),(),(),(),()))");
    int32 PreCount = 0, PostCount = 0;
    const FDelegateHandle Pre = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Object, const FEditPropertyChain&)
        {
            if (Object == Camera.Get())
                ++PreCount;
        });
    const FDelegateHandle Post = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [&](UObject* Object, FPropertyChangedEvent&)
        {
            if (Object == Camera.Get())
                ++PostCount;
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Pre);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Post);
    };
    const bool Changed = UP4AuthoringTools::SetEditablePropertyText(Camera.Get(), TEXT("PostProcessSettings"), Text);
    TestTrue(TEXT("Real nested fixed-array sign change succeeds"), Changed);
    TestTrue(TEXT("Native sign edit emits one pre/post callback pair"), PreCount == 1 && PostCount == 1);
    TestEqual(TEXT("Exact native first-slot sign bit changed"),
        FloatBits(Camera->PostProcessSettings.LensFlareTints[0].R), uint32(0x80000000u));
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const FLinearColor& Color = Camera->PostProcessSettings.LensFlareTints[Index];
        if (Index > 0)
            TestEqual(
                TEXT("All other native fixed-slot R values retained"), FloatBits(Color.R), FloatBits(float(Index)));
        TestTrue(TEXT("Each slot retains its other native color channels"),
            FloatBits(Color.G) == FloatBits(.25f) && FloatBits(Color.B) == FloatBits(.5f) &&
                FloatBits(Color.A) == FloatBits(1.0f));
    }
    TestTrue(TEXT("Real nested sign edit dirties package"), Owner->IsDirty());
    if (!Changed || FloatBits(Camera->PostProcessSettings.LensFlareTints[0].R) != 0x80000000u || !Owner->IsDirty() ||
        PreCount != 1 || PostCount != 1)
        return false;
    TestTrue(TEXT("Native Undo restores complete post-process object"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo independently restores full native bytes"), NativeBytes(Camera.Get()) == Before);
    TestTrue(TEXT("Native Redo restores authored sign"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo independently restores negative zero bits"),
        FloatBits(Camera->PostProcessSettings.LensFlareTints[0].R), uint32(0x80000000u));
    Owner->SetDirtyFlag(false);
    {
        ReadGuard State(Camera.Get());
        TestTrue(TEXT("Exact nested fixed-array request is a no-op"),
            UP4AuthoringTools::SetEditablePropertyText(Camera.Get(), TEXT("PostProcessSettings"), Text));
        State.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter163MissingRowSchema,
    "P4MapKit.Quality.PropertyAfter163MissingRowSchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter163MissingRowSchema::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter163;
    UPackage* Owner = MakePackage(TEXT("MissingRowSchema"));
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(Owner, NAME_None, RF_Transactional));
    ON_SCOPE_EXIT
    {
        Table->RowStruct = FTableRowBase::StaticStruct();
    }; // Normal UE GC cleanup requires a real empty row schema.
    if (!TestNull(TEXT("Native new table naturally has no selected row schema"), Table->RowStruct))
        return false;
    const EObjectFlags Flags = Table->GetFlags();
    const FName Name = Table->GetFName();
    Owner->SetDirtyFlag(false);
    int32 PreCount = 0, PostCount = 0;
    const FDelegateHandle Pre = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Object, const FEditPropertyChain&)
        {
            if (Object == Table.Get())
                ++PreCount;
        });
    const FDelegateHandle Post = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [&](UObject* Object, FPropertyChangedEvent&)
        {
            if (Object == Table.Get())
                ++PostCount;
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Pre);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Post);
    };
    // Native dependency collector is neither loading nor saving. Do not use
    // FObjectWriter here: native DataTable saving logs Missing RowStruct and is
    // not a valid full-serialization oracle for this actual incomplete asset.
    const auto First = UP4AuthoringTools::NativeAssetDependencies(Table.Get());
    TestTrue(TEXT("Incomplete native table dependencies are deterministic"),
        UP4AuthoringTools::NativeAssetDependencies(Table.Get()) == First);
    TestNull(TEXT("Read-only dependency query did not invent a row schema"), Table->RowStruct);
    TestEqual(TEXT("Read-only dependency query did not invent rows"), Table->GetRowMap().Num(), 0);
    TestTrue(TEXT("Native object name, flags, outer and class retained"),
        Table->GetFName() == Name && Table->GetFlags() == Flags && Table->GetOuter() == Owner &&
            Table->GetClass() == UDataTable::StaticClass());
    TestFalse(TEXT("Incomplete native query remains clean"), Owner->IsDirty());
    TestTrue(TEXT("Incomplete native query dispatches no editor mutation callbacks"), PreCount == 0 && PostCount == 0);
    return !HasAnyErrors();
}
#endif

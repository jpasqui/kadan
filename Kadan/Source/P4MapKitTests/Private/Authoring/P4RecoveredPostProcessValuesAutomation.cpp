#include "P4RecoveredPostProcessValues.h"
#include "P4KadanTestSeams.h"
#include "P4RecoveredPostProcessStorage.h"
#include "P4DecodedPropertyWriter.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredPostProcessLogicalValues,
    "P4MapKit.EngineFields.RecoveredPostProcessLogicalValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredPostProcessLogicalValues::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    using V = P4MapKit::FDecodedValue;
    UClass* Class = LoadClass<UPostProcessComponent>(
        nullptr, TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent"));
    if (!TestNotNull(TEXT("The actual UHT post-process storage child is installed"), Class))
        return false;
    FP4RecoveredPostProcessStorage Storage;
    FString Error;
    if (!TestTrue(TEXT("The full actual native storage contract qualifies"),
            P4GetRecoveredPostProcessStorage(Class, Storage, Error)))
    {
        AddError(Error);
        return false;
    }
    TStrongObjectPtr<UObject> Target(NewObject<UObject>(GetTransientPackage(), Class));
    TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>());
    const auto Resolve = [&](const TCHAR* Name, FP4RecoveredPostProcessMember& Member)
    { return TestTrue(Name, P4ResolveRecoveredPostProcessMember(Storage, FName(Name), Member)); };
    FP4RecoveredPostProcessMember Bias, Flag, Param, Method, Mask;
    if (!Resolve(TEXT("AutoExposureBias"), Bias) || !Resolve(TEXT("bOverride_C4DepthOfFieldParam2"), Flag) ||
        !Resolve(TEXT("C4DepthOfFieldParam2"), Param) || !Resolve(TEXT("DepthOfFieldMethod"), Method) ||
        !Resolve(TEXT("C4DepthOfFieldMask"), Mask))
        return false;
    TestTrue(TEXT("Common settings retain the inherited region"), Bias.Container == Storage.StockSettings);
    TestTrue(TEXT("Original fork members use their genuine native extension region"),
        Flag.Container == Storage.ForkSettings && Param.Container == Storage.ForkSettings &&
            Method.Container == Storage.ForkSettings);
    const auto Write = [&](const char* Json, bool Lenient, FP4DecodedPropertyWriter& Writer)
    {
        V Value;
        if (!TestTrue(TEXT("The bounded source value decodes"), P4MapKit::ReadDecodedJson(Json, Value)))
            return EP4RecoveredPostProcessWrite::Refused;
        const std::string Before = P4MapKit::DecodedJson(Value);
        Writer.Lenient = Lenient;
        const auto Result = P4WriteRecoveredPostProcessSettings(
            Writer, Target.Get(), Storage.StockSettings, 0, Value, TEXT("Source.PostProcess Settings"));
        TestTrue(TEXT("Native writing leaves original decoded source values unchanged"),
            Before == P4MapKit::DecodedJson(Value));
        return Result;
    };
    FP4DecodedPropertyWriter Writer({}, true);
    TestTrue(TEXT("One logical Settings value writes both genuine native regions"),
        Write(
            "{\"AutoExposureBias\":1.25,\"bOverride_C4DepthOfFieldParam2\":true,"
            "\"C4DepthOfFieldParam2\":{\"X\":1,\"Y\":0.25,\"Z\":-0.5,\"W\":2},\"DepthOfFieldMethod\":\"DOFM_GatherDOF\"}",
            false, Writer) == EP4RecoveredPostProcessWrite::Applied);
    const auto Data = [&](const FP4RecoveredPostProcessMember& Member)
    { return P4RecoveredPostProcessMemberData(Target.Get(), Storage, Member); };
    TestEqual(TEXT("Common exposure is stored in the stock Settings object"),
        CastFieldChecked<FFloatProperty>(Bias.Property)->GetPropertyValue(Data(Bias)), 1.25f);
    TestTrue(TEXT("Packed native override flag receives the actual source value"),
        CastFieldChecked<FBoolProperty>(Flag.Property)->GetPropertyValue(Data(Flag)));
    const FVector4* Vector = static_cast<const FVector4*>(Data(Param));
    TestTrue(TEXT("All four original vector components are stored"),
        Vector && Vector->X == 1.f && Vector->Y == .25f && Vector->Z == -.5f && Vector->W == 2.f);
    FString NativeGather;
    TestTrue(TEXT("Original Gather has a distinct qualified native enum name"),
        P4RecoveredPostProcessSourceEnum(Method, TEXT("DOFM_GatherDOF"), NativeGather));
    const FByteProperty* Byte = CastFieldChecked<FByteProperty>(Method.Property);
    TestEqual(TEXT("The original Gather meaning is stored instead of stock Gaussian"),
        int64(Byte->GetPropertyValue(Data(Method))), Byte->Enum->GetValueByNameString(NativeGather));

    // Own-package references retain the normal two-phase writer protocol.
    FP4DecodedPropertyWriter Deferred([](const FString&) -> UObject* { return nullptr; }, false);
    TestTrue(TEXT("An unresolved local texture defers through the caller's writer"),
        Write("{\"C4DepthOfFieldMask\":\"SourceTexture\"}", false, Deferred) == EP4RecoveredPostProcessWrite::Applied &&
            Deferred.Later);
    int32 Resolved = 0;
    FP4DecodedPropertyWriter Final(
        [&](const FString& Path) -> UObject*
        {
            if (Path == TEXT("SourceTexture"))
            {
                ++Resolved;
                return Texture.Get();
            }
            return nullptr;
        },
        true);
    TestTrue(TEXT("The final source pass uses the original resolver"),
        Write("{\"C4DepthOfFieldMask\":\"SourceTexture\"}", false, Final) == EP4RecoveredPostProcessWrite::Applied &&
            !Final.Later && Resolved == 1);
    TestTrue(TEXT("The nested real UTexture property contains that exact object"),
        CastFieldChecked<FObjectPropertyBase>(Mask.Property)->GetObjectPropertyValue(Data(Mask)) == Texture.Get());

    FP4DecodedPropertyWriter Unknown({}, true);
    TestTrue(TEXT("Unknown logical source members are explicit omissions only in lenient mode"),
        Write("{\"NotAnOriginalMember\":2}", true, Unknown) == EP4RecoveredPostProcessWrite::Applied &&
            Unknown.NotHeld.Num() == 1);
    FP4DecodedPropertyWriter UnknownStrict({}, true);
    TestTrue(TEXT("An unknown source member cannot silently qualify"),
        Write("{\"NotAnOriginalMember\":2}", false, UnknownStrict) == EP4RecoveredPostProcessWrite::Refused &&
            !UnknownStrict.Error.IsEmpty());
    FP4DecodedPropertyWriter BadEnum({}, true);
    TestTrue(TEXT("A stock-only enum choice cannot alias an original meaning"),
        Write("{\"DepthOfFieldMethod\":\"DOFM_Gaussian\"}", true, BadEnum) == EP4RecoveredPostProcessWrite::Applied &&
            BadEnum.NotHeld.Num() == 1);
    TestEqual(TEXT("Rejected enum choice leaves the native original Gather value intact"),
        int64(Byte->GetPropertyValue(Data(Method))), Byte->Enum->GetValueByNameString(NativeGather));
    for (const char* Bad : {"{\"AutoExposureBias\":7,\"AutoExposureBias[0]\":8}", "{\"AutoExposureBias[0.5]\":8}",
             "{\"AutoExposureBias[4294967296]\":8}"})
    {
        FP4DecodedPropertyWriter Invalid({}, true);
        TestTrue(TEXT("Duplicate or malformed fixed member refuses before any native write"),
            Write(Bad, false, Invalid) == EP4RecoveredPostProcessWrite::Refused);
        TestEqual(TEXT("Preflight refusal preserves the previous common native value"),
            CastFieldChecked<FFloatProperty>(Bias.Property)->GetPropertyValue(Data(Bias)), 1.25f);
    }
    V Empty;
    P4MapKit::ReadDecodedJson("{}", Empty);
    FP4DecodedPropertyWriter PrivatePath({}, true);
    TestTrue(TEXT("Host ForkSettings cannot be supplied as a game property path"),
        P4WriteRecoveredPostProcessSettings(PrivatePath, Target.Get(), Storage.ForkSettings, 0, Empty,
            TEXT("Source ForkSettings")) == EP4RecoveredPostProcessWrite::Refused);
    TStrongObjectPtr<UPostProcessComponent> Stock(NewObject<UPostProcessComponent>());
    FP4DecodedPropertyWriter Unrelated({}, true);
    TestTrue(TEXT("The adapter grants no storage alias to the ordinary engine class"),
        P4WriteRecoveredPostProcessSettings(Unrelated, Stock.Get(), Storage.StockSettings, 0, Empty,
            TEXT("Stock Settings")) == EP4RecoveredPostProcessWrite::NotApplicable);

    const FString Logical = TEXT(
        "{\"Settings\":{\"bOverride_DepthOfFieldTiltAngles\":true,\"C4DepthOfFieldParam2\":{\"X\":1,\"Y\":0,\"Z\":0,\"W\":0},\"DepthOfFieldMethod\":\"DOFM_GatherDOF\"}}");
    FString Kept;
    int32 Count = -1;
    TestTrue(TEXT("Strict schema comparison resolves actual typed logical Settings members"),
        P4LevelContent::P4UnheldClassValuesForTest(Class, Logical, Kept, Count) && Count == 0);
    TestTrue(TEXT("The same source remains unsupported on an unrelated stock component"),
        P4LevelContent::P4UnheldClassValuesForTest(UPostProcessComponent::StaticClass(), Logical, Kept, Count) &&
            Count == 3);
    TestTrue(TEXT("Unknown original enum choices remain Unheld on the recovered storage class"),
        P4LevelContent::P4UnheldClassValuesForTest(
            Class, TEXT("{\"Settings\":{\"DepthOfFieldMethod\":\"DOFM_Gaussian\"}}"), Kept, Count) &&
            Count == 1);
    TestTrue(TEXT("Host storage cannot masquerade as an original source property"),
        P4LevelContent::P4UnheldClassValuesForTest(
            Class, TEXT("{\"ForkSettings\":{\"DepthOfFieldMethod\":\"DOFM_GatherDOF\"}}"), Kept, Count) &&
            Count == 1);
    FP4DecodedPropertyWriter OutsideArray({}, true);
    TestTrue(TEXT("Out-of-range native fixed members are explicitly omitted by lenient writing"),
        Write("{\"C4DepthOfFieldParam2[1]\":{\"X\":9}}", true, OutsideArray) == EP4RecoveredPostProcessWrite::Applied &&
            OutsideArray.NotHeld.Num() == 1);
    TestTrue(TEXT("Strict comparison reports the same fixed-member omission"),
        P4LevelContent::P4UnheldClassValuesForTest(
            Class, TEXT("{\"Settings\":{\"C4DepthOfFieldParam2[1]\":{\"X\":9}}}"), Kept, Count) &&
            Count == 1);
    for (const FString& Bad : {FString(TEXT("{\"Settings\":{\"C4DepthOfFieldParam2[0.5]\":{\"X\":9}}}")),
             FString(TEXT("{\"Settings\":{\"C4DepthOfFieldParam2\":{\"X\":9},\"C4DepthOfFieldParam2[0]\":{\"X\":9}}}")),
             FString(TEXT("{\"Settings\":7}"))})
        TestFalse(TEXT("Malformed or duplicate logical storage cannot produce a successful strict schema proof"),
            P4LevelContent::P4UnheldClassValuesForTest(Class, Bad, Kept, Count));
    return !HasAnyErrors();
}
#endif

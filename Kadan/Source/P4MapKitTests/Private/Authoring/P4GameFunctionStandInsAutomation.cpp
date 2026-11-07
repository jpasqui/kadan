#if P4_MAPKIT_TESTS
#include "P4KadanTestSeams.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

// The editor's answer for the game functions that have a stand-in, called the way an animation Blueprint
// calls them (through the function's native code), and put back as declared when the stand-ins stop.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameFunctionStandInsTest, "P4MapKit.Kadan.GameFunctionStandIns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameFunctionStandInsTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FP4KadanTestSeams* Seams = P4KadanTestSeams();
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.NiseBaseAnimInstance"));
    if (!Class)
    {
        AddWarning(TEXT("P4MapKit test not run. This project has no game classes (P4Stubs)."));
        return true;
    }
    UFunction* Function = Class->FindFunctionByName(TEXT("GetWeakPointBoneScale"));
    if (!TestNotNull(TEXT("the game declares GetWeakPointBoneScale"), Function))
        return false;
    TestTrue(TEXT("the module started the stand-in"), Seams->GameFunctionStandIns().Contains(Function->GetPathName()));
    const auto Call = [&](int32 Id)
    {
        uint8* Parameters = (uint8*)FMemory_Alloca(Function->ParmsSize);
        FMemory::Memzero(Parameters, Function->ParmsSize);
        CastFieldChecked<FIntProperty>(Function->FindPropertyByName(TEXT("WeakPointId")))
            ->SetPropertyValue_InContainer(Parameters, Id);
        Class->GetDefaultObject()->ProcessEvent(Function, Parameters);
        return *Function->GetReturnProperty()->ContainerPtrToValuePtr<FVector>(Parameters);
    };
    TestEqual(TEXT("an untouched weak point has its modelled size"), Call(0), FVector::OneVector);
    TestEqual(TEXT("every weak point has"), Call(2), FVector::OneVector);
    const FNativeFuncPtr Installed = Function->GetNativeFunc();
    Seams->StopGameFunctionStandIns();
    // Do not call the stub here: FVector's default constructor leaves its return slot uninitialized.
    const bool Stopped = !Seams->GameFunctionStandIns().Contains(Function->GetPathName()) &&
        Function->GetNativeFunc() && Function->GetNativeFunc() != Installed;
    Seams->StartGameFunctionStandIns();
    TestTrue(TEXT("stopping puts back the declared native function"), Stopped);
    TestTrue(TEXT("starting reinstalls the same native function"), Function->GetNativeFunc() == Installed);
    TestEqual(TEXT("starting again gives the stand-in"), Call(1), FVector::OneVector);
    TestEqual(TEXT("one stand-in per function"), Seams->GameFunctionStandIns().Num(), 1);
    return true;
}
#endif

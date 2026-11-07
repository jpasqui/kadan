#if P4_MAPKIT_TESTS
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectIterator.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ColdAnimationClients, "P4MapKit.Animation.ColdOriginalAnimationClients",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ColdAnimationClients::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ANIMATION_CLIENTS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned saved animation clients."));
        return true;
    }
    for (const TCHAR* Path : {TEXT("/Game/Carrot4/Objects/Pellet/Pelplant/ABP_PelplantBase.ABP_PelplantBase"),
                              TEXT("/Game/Carrot4/Characters/Teki/AmeBozu/Animation/ABP_AmeBozu.ABP_AmeBozu")})
    {
        auto* Blueprint = LoadObject<UAnimBlueprint>(nullptr, Path);
        if (!TestNotNull(TEXT("The actual saved animation Blueprint loads"), Blueprint))
            return false;
        TestTrue(TEXT("The saved animation client has no unresolved compiler error"),
                 Blueprint->Status != BS_Error && Blueprint->GeneratedClass && Blueprint->TargetSkeleton);
        const auto* Class =
            Blueprint->GeneratedClass ? IAnimClassInterface::GetFromClass(Blueprint->GeneratedClass) : nullptr;
        TestTrue(TEXT("The saved client retains a real compiled animation graph"),
                 Class && Class->GetAnimNodeProperties().Num() > 0);
    }
    UClass* Pelplant = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
        if (It->GetFName() == FName(TEXT("GPelplantBase_C")) && It->GetPathName().StartsWith(TEXT("/Game/Carrot4/")))
        {
            Pelplant = *It;
            break;
        }
    TestTrue(TEXT("The actual loaded parent supplies the animation client's real function"),
             Pelplant && Pelplant->FindFunctionByName(TEXT("GetPelplantType")));
    return !HasAnyErrors();
}
#endif

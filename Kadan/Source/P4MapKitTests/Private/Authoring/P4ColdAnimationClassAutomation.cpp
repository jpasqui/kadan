#if P4_MAPKIT_TESTS
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/Skeleton.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ColdAnimationClassTest, "P4MapKit.Animation.ColdOriginalClassSkeleton",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ColdAnimationClassTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIMATION_BINDING_PROOF")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned imported Kaburi/Shijimi fixture."));
        return true;
    }
    for (const TCHAR* Name : {TEXT("Kaburi"), TEXT("Shijimi")})
    {
        const FString Package = FString(TEXT("/Game/Carrot4/Characters/Teki/")) + Name + TEXT("/ABP_") + Name;
        UClass* Class = LoadObject<UClass>(nullptr, *(Package + TEXT(".ABP_") + Name + TEXT("_C")));
        IAnimClassInterface* Interface = Class ? IAnimClassInterface::GetFromClass(Class) : nullptr;
        const USkeleton* Before = Interface ? Interface->GetTargetSkeleton() : nullptr;
        const FString BeforeName = GetPathNameSafe(Before);
        auto* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *(Package + TEXT(".ABP_") + Name));
        IAnimClassInterface* AfterInterface = Blueprint && Blueprint->GeneratedClass
                                                  ? IAnimClassInterface::GetFromClass(Blueprint->GeneratedClass)
                                                  : nullptr;
        const USkeleton* After = AfterInterface ? AfterInterface->GetTargetSkeleton() : nullptr;
        AddInfo(FString::Printf(
            TEXT("%s class-first target %s; Blueprint asset target %s; asset-first class target %s; class replaced %d"),
            Name, *BeforeName, *GetPathNameSafe(Blueprint ? Blueprint->TargetSkeleton : nullptr),
            *GetPathNameSafe(After), Blueprint && Blueprint->GeneratedClass != Class));
        TestTrue(TEXT("Cold generated-class loading retains the actual source target skeleton"),
                 Before && Blueprint && Before == Blueprint->TargetSkeleton && After == Blueprint->TargetSkeleton);
    }
    return !HasAnyErrors();
}
#endif

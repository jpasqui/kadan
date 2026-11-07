#if P4_MAPKIT_TESTS
#include "P4LevelImportWorld.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ImportedPhysicsCacheTest, "P4MapKit.Kadan.ImportedPhysicsCacheCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ImportedPhysicsCacheTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GRAPH_MAP")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned saved Cave027 streaming fixture."));
        return true;
    }
    const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave027/Cave027_F00/Sublevels/Cave027_F00_GameArea");
    UWorld* Child = LoadObject<UWorld>(nullptr, *(Package + TEXT(".Cave027_F00_GameArea")));
    FObjectProperty* Cache = FindFProperty<FObjectProperty>(UWorld::StaticClass(), TEXT("DefaultPhysicsVolume"));
    if (!TestTrue(TEXT("The actual saved sublevel and native transient physics cache exist"),
                  Child && Child->PersistentLevel && Cache && Cache->HasAnyPropertyFlags(CPF_Transient)))
        return false;
    TStrongObjectPtr<UObject> PreviousCache(Cache->GetObjectPropertyValue_InContainer(Child));
    UWorld* PreviousOwner = Child->PersistentLevel->OwningWorld;
    const bool Dirty = Child->GetOutermost()->IsDirty();
    const int32 Actors = Child->PersistentLevel->Actors.Num();
    auto* Volume = NewObject<ADefaultPhysicsVolume>(Child->PersistentLevel, NAME_None, RF_Transient);
    TArray<TStrongObjectPtr<UWorld>> Worlds;
    const auto MakeWorld = [&]() -> UWorld*
    {
        const FString Name = TEXT("P4PhysicsCache_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UWorld* Made = P4CreateOriginalImportWorld(FName(*Name), CreatePackage(*(TEXT("/Temp/") + Name)));
        Worlds.Emplace(Made);
        return Made;
    };
    ON_SCOPE_EXIT
    {
        Cache->SetObjectPropertyValue_InContainer(Child, PreviousCache.Get());
        Child->PersistentLevel->OwningWorld = PreviousOwner;
        Child->GetOutermost()->SetDirtyFlag(Dirty);
        Volume->MarkPendingKill();
        for (auto& World : Worlds)
            if (World.IsValid())
            {
                World->MarkPendingKill();
                World->GetOutermost()->SetDirtyFlag(false);
            }
    };
    UWorld* Live = MakeWorld();
    UWorld* Other = MakeWorld();
    if (!TestTrue(TEXT("Independent inactive worlds initialize through the real importer path"), Live && Other))
        return false;
    Child->PersistentLevel->OwningWorld = Live;
    Cache->SetObjectPropertyValue_InContainer(Child, Volume);
    AddInfo(FString::Printf(
        TEXT(
            "Actual cache property type %s, flags %llx; child flags %x, context %d; volume transient %d, package matches %d, actor level matches %d"),
        *GetPathNameSafe(Cache->PropertyClass), uint64(Cache->GetPropertyFlags()), uint32(Child->GetFlags()),
        GEngine && GEngine->GetWorldContextFromWorld(Child), Volume->HasAnyFlags(RF_Transient),
        Volume->GetOutermost() == Child->GetOutermost(), Volume->GetLevel() == Child->PersistentLevel));
    const FDelegateHandle Cleanup = FWorldDelegates::OnWorldCleanup.AddLambda(
        [this, Live](UWorld* W, bool Session, bool Resources)
        {
            if (W == Live)
                AddInfo(FString::Printf(TEXT("Actual native cleanup callback: session %d resources %d"), Session,
                                        Resources));
        });
    ON_SCOPE_EXIT
    {
        FWorldDelegates::OnWorldCleanup.Remove(Cleanup);
    };
    Other->DestroyWorld(false);
    TestEqual(TEXT("Cleanup of another world preserves a live world's cached volume"),
              Cache->GetObjectPropertyValue_InContainer(Child), static_cast<UObject*>(Volume));
    Live->DestroyWorld(false);
    TestNull(TEXT("An imported sublevel releases its transient cache into the dying owning world"),
             Cache->GetObjectPropertyValue_InContainer(Child));
    UWorld* NonTransientControl = MakeWorld();
    if (!TestNotNull(TEXT("The nontransient negative control initializes"), NonTransientControl))
        return false;
    Volume->ClearFlags(RF_Transient);
    Child->PersistentLevel->OwningWorld = NonTransientControl;
    Cache->SetObjectPropertyValue_InContainer(Child, Volume);
    NonTransientControl->DestroyWorld(false);
    TestEqual(TEXT("A nontransient reference is never treated as the engine's disposable cache"),
              Cache->GetObjectPropertyValue_InContainer(Child), static_cast<UObject*>(Volume));
    TestEqual(TEXT("Cleanup does not remove source actors"), Child->PersistentLevel->Actors.Num(), Actors);
    return !HasAnyErrors();
}
#endif

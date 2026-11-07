// The fixture retirement helpers the Kadan import, export and read-only tests call (declared where they are used).
#include "P4KadanImport.h"
#include "P4KadanAssetPolicy.h"
#include "P4OriginalAssetImport.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4RecoveredFoliageType.h"
#include "P4ExportDriver.h"
#include "P4KadanContainerIndex.h"
#include "P4ExportProcess.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/blueprint_deep.h"
THIRD_PARTY_INCLUDES_END
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectIterator.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "UObject/StructOnScope.h"

namespace P4FixtureRetirement
{
TSet<FName> LivePackages()
{
    TSet<FName> Out;
    for (TObjectIterator<UObject> It; It; ++It)
        if (IsValid(*It) && !It->IsA<UMetaData>() && It->GetOuter() && It->GetOuter()->IsA<UPackage>() &&
            It->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
            Out.Add(It->GetOutermost()->GetFName());
    return Out;
}

FName VacantName(UObject* Object)
{
    // Blueprint rename also moves its classes/CDOs. None of those target names may
    // collide with unrelated transient objects (including another test's objects).
    for (;;)
    {
        const FString Name = TEXT("P4RetiredFixture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        TArray<FString> Names = {Name};
        if (Object->IsA<UBlueprint>())
        {
            Names.Add(Name + TEXT("_C"));
            Names.Add(TEXT("SKEL_") + Name + TEXT("_C"));
            Names.Add(TEXT("Default__") + Name + TEXT("_C"));
            Names.Add(TEXT("Default__SKEL_") + Name + TEXT("_C"));
        }
        else if (Object->IsA<UClass>())
            Names.Add(TEXT("Default__") + Name);
        bool Occupied = false;
        for (const FString& Candidate : Names)
            Occupied = Occupied || StaticFindObjectFast(nullptr, GetTransientPackage(), FName(*Candidate)) != nullptr;
        if (!Occupied)
            return FName(*Name);
    }
}

bool Roots(const TArray<UObject*>& Owned, const TArray<UObject*>& Direct)
{
    // Native Blueprint rename also moves both class pointers and their CDOs.
    // Refuse before registry changes unless every implicitly moved object is owned.
    for (UObject* Root : Direct)
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Root))
            for (UClass* Class : {Blueprint->GeneratedClass, Blueprint->SkeletonGeneratedClass})
                if (Class)
                {
                    if (!Owned.Contains(Class) || Class->GetOutermost() != Blueprint->GetOutermost() ||
                        Class->ClassGeneratedBy != Blueprint)
                    {
                        UE_LOG(LogTemp, Error, TEXT("Fixture retirement does not own Blueprint class: %s"),
                            *Class->GetPathName());
                        return false;
                    }
                    if (UObject* Default = Class->GetDefaultObject(false))
                        if (!Owned.Contains(Default) || Default->GetClass() != Class ||
                            Default->GetOutermost() != Blueprint->GetOutermost())
                        {
                            UE_LOG(LogTemp, Error, TEXT("Fixture retirement does not own Blueprint CDO: %s"),
                                *Default->GetPathName());
                            return false;
                        }
                }
    TMap<UObject*, UObject*> OriginalOuters;
    TMap<UObject*, FString> OriginalPaths;
    for (UObject* Root : Direct)
        OriginalOuters.Add(Root, Root->GetOuter());
    for (UObject* Object : Owned)
        if (!Object->IsA<UMetaData>())
            OriginalPaths.Add(Object, Object->GetPathName());
    for (UObject* Object : Owned)
        if (!Object->IsPendingKill() && Object->IsAsset())
            FAssetRegistryModule::AssetDeleted(Object);

    // A Blueprint moves its generated/skeleton classes and CDOs itself. Process it
    // first, then skip roots which that native rename already moved out of the package.
    for (int32 Pass = 0; Pass < 2; ++Pass)
        for (UObject* Root : Direct)
        {
            if (Root->IsA<UMetaData>() || Root->GetOuter() != OriginalOuters[Root] ||
                (Root->IsA<UBlueprint>() ? 0 : 1) != Pass)
                continue;
            const FString Before = Root->GetPathName();
            if (Root->GetName() == TEXT("ABP_Happy"))
            {
                UBlueprint* Blueprint = Cast<UBlueprint>(Root);
                UE_LOG(LogTemp, Display,
                    TEXT("Fixture retirement %s valid=%d asset=%d flags=%08x generated=%s skeleton=%s"), *Before,
                    IsValid(Root), Root->IsAsset(), uint32(Root->GetFlags()),
                    Blueprint && Blueprint->GeneratedClass ? *Blueprint->GeneratedClass->GetPathName() : TEXT("none"),
                    Blueprint && Blueprint->SkeletonGeneratedClass ? *Blueprint->SkeletonGeneratedClass->GetPathName()
                                                                   : TEXT("none"));
            }
            const FName Name = VacantName(Root);
            if (!Root->Rename(*Name.ToString(), GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
            {
                UE_LOG(LogTemp, Error, TEXT("Fixture retirement could not vacate %s"), *Before);
                return false;
            }
        }
    for (const auto& Before : OriginalPaths)
        if (FindObject<UObject>(nullptr, *Before.Value) != nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("Fixture retirement left an owned path occupied: %s"), *Before.Value);
            return false;
        }
    for (UObject* Object : Owned)
        if (!Object->IsA<UMetaData>())
        {
            if (UWorld* World = Cast<UWorld>(Object))
                if (!World->IsPendingKill())
                    World->DestroyWorld(false);
            Object->ClearFlags(RF_Public | RF_Standalone);
            // Let reachability retire this graph. Force-killing a generated class makes
            // GC null its references in dependent classes, including SuperStruct. Their
            // instances still need that ancestry in DestroyPersistentUberGraphFrame.
            // Referenced objects may survive this collection, under their vacated names.
        }
    return true;
}

bool Package(UPackage* Made)
{
    // Snapshot before moving roots: classes/CDOs/graphs move transitively. Metadata
    // stays with its package; never leave UPackage::MetaData pointing at retired data.
    TArray<UObject*> Owned, Direct;
    GetObjectsWithOuter(Made, Owned, true);
    GetObjectsWithOuter(Made, Direct, false);
    if (!Roots(Owned, Direct))
        return false;
    ResetLoaders(Made);
    Made->SetDirtyFlag(false);
    return true;
}
} //namespace P4FixtureRetirement
#endif

#pragma once
#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

// Development test seams of the Kadan module.
//
// Only a development build with tests (dev_build.py --tests) defines P4_KADAN_TEST_SEAMS for the
// Kadan module, and only then does the module register these seams when it starts. A release build
// holds none of them. The separate test module finds them at run time, never by linking, so it also
// loads beside the shipping DLLs; a test that needs a seam then reports that it did not run.
#ifndef P4_KADAN_TEST_SEAMS
#define P4_KADAN_TEST_SEAMS 0
#endif

class UClass;
class UObject;
class UStruct;
class UWorld;
class UMaterialParameterCollection;
class FShaderParametersMetadata;
struct FP4OriginalClassValueNotHeld;
struct FP4OriginalLevelImportResult;
struct FP4MaterialUsedResourceRequest;
struct FP4KadanSourceBinding;

struct FP4KadanTestSeams : public IModularFeature
{
    static FName FeatureName() { return FName(TEXT("P4KadanTestSeams")); }

    bool (*UnheldClassValues)(const UStruct*, const FString&, FString&, int32&) = nullptr;
    bool (*UnheldClassValueDetails)(
        const UStruct*, const FString&, FString&, int32&, TArray<FP4OriginalClassValueNotHeld>&) = nullptr;
    bool (*TerrainTrackingLines)(const FString&, const FString&, TArray<FString>&, TArray<FString>&) = nullptr;
    bool (*ClassComponentTail)(UClass*, const TArray<uint8>&, int32&) = nullptr;
    bool (*ClassLightingTail)(UClass*, const TArray<uint8>&, bool, TArray<FString>&, TArray<FString>&) = nullptr;
    TArray<FString> (*ClassSourceAdmission)(const FString&, const FString&, TSet<FName>&, int32&) = nullptr;
    FP4OriginalLevelImportResult (*RepairRetainedPlayerDefaults)(
        const FString&, const FString&, const FString&, const FString&) = nullptr;
    bool (*ApplyDeclaredLevelDefaults)(UObject*, UObject*, const FString&, bool, FString&) = nullptr;
    bool (*LevelStorageSubobjectMatches)(UObject*, UObject*, UClass*, const FString&, uint32, UObject*) = nullptr;
    FP4OriginalLevelImportResult (*ProbeGameBackedFamilies)(const FString&, const FString&) = nullptr;
    void (*DiscardFamilyProbe)(UWorld*) = nullptr;
    bool (*MaterialFamilyStageControls)(TArray<FString>&) = nullptr;
    void (*ForgetGameActorNames)(UWorld*) = nullptr;
    bool (*MaterialCollectionValues)(UMaterialParameterCollection*, const FP4MaterialUsedResourceRequest&,
        const FP4KadanSourceBinding&, FString&) = nullptr;
    bool (*MaterialCollectionLayout)(const FShaderParametersMetadata&, int32, int32, FString&) = nullptr;
    bool (*MaterialCollectionLifecycle)(UMaterialParameterCollection*, FString&) = nullptr;
    // A scope that denies source acquisition in the owner project while it lives.
    bool (*SourceAcquisitionAllowed)(const TArray<FString>&, FString&) = nullptr;
    void* (*BeginAcquisitionVeto)(const FString& OwnerProject) = nullptr;
    void (*EndAcquisitionVeto)(void*) = nullptr;
    bool (*AcquisitionVetoActive)(const void*) = nullptr;
    int32 (*AcquisitionVetoDenied)(const void*) = nullptr;
    const TSet<FString>* (*AcquisitionVetoPackages)(const void*) = nullptr;
    // Forces one selected dependency of a root through the real Blueprint plan loader.
    void* (*BeginWrappedBlueprintSource)(const FString& Root, const FString& Dependency) = nullptr;
    void (*EndWrappedBlueprintSource)(void*) = nullptr;
    int32 (*WrappedBlueprintSourceFailures)(const void*) = nullptr;
    void (*StartGameFunctionStandIns)() = nullptr;
    void (*StopGameFunctionStandIns)() = nullptr;
    TArray<FString> (*GameFunctionStandIns)() = nullptr;
    FString (*LevelClassLine)(const FString&, bool) = nullptr;
};

// Separate feature: a narrow one-package completion failure after copied metadata is installed.
// No production setter, no success override, and no dependency on the older seam struct's layout.
struct FP4KadanCopyTestSeams : public IModularFeature
{
    static FName FeatureName() { return FName(TEXT("P4KadanCopyTestSeams")); }
    void (*SetCompletionFailurePackage)(const FString&) = nullptr;
};

// Separate feature keeps this opt-in scope seam recognizable beside older DLLs.
struct FP4KadanSavedLevelTestSeams : public IModularFeature
{
    static FName FeatureName() { return FName(TEXT("P4KadanSavedLevelTestSeams")); }
    void* (*Begin)(const FString& Package, FString& Error) = nullptr;
    void (*End)(void*) = nullptr;
};

// Separate feature: the scene teardown guard (P4SceneTeardown.h), stopped and started again for the
// control run of the race it prevents.
struct FP4KadanSceneTeardownTestSeams : public IModularFeature
{
    static FName FeatureName() { return FName(TEXT("P4KadanSceneTeardownTestSeams")); }
    bool (*Active)() = nullptr;
    void (*Start)() = nullptr;
    void (*Stop)() = nullptr;
};

#if P4_KADAN_TEST_SEAMS
void P4RegisterKadanTestSeams();
void P4UnregisterKadanTestSeams();
#endif

#if defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "P4OriginalLevelImport.h"

inline const FP4KadanTestSeams* P4KadanTestSeams()
{
    IModularFeatures& Features = IModularFeatures::Get();
    return Features.IsModularFeatureAvailable(FP4KadanTestSeams::FeatureName())
        ? &Features.GetModularFeature<FP4KadanTestSeams>(FP4KadanTestSeams::FeatureName())
        : nullptr;
}

inline const FP4KadanSceneTeardownTestSeams* P4KadanSceneTeardownSeams()
{
    IModularFeatures& Features = IModularFeatures::Get();
    return Features.IsModularFeatureAvailable(FP4KadanSceneTeardownTestSeams::FeatureName())
        ? &Features.GetModularFeature<FP4KadanSceneTeardownTestSeams>(FP4KadanSceneTeardownTestSeams::FeatureName())
        : nullptr;
}

// The words dev_tests.py counts as a test that did not run.
#define P4_MAPKIT_NOT_RUN_TEXT TEXT("P4MapKit test not run. ")
// Inside RunTest. Reports the test as not run when the Kadan module holds no development seams.
#define P4_REQUIRE_KADAN_TEST_SEAMS()                                                                          \
    if (!P4KadanTestSeams())                                                                                   \
    {                                                                                                          \
        AddWarning(FString(P4_MAPKIT_NOT_RUN_TEXT) + TEXT("This Kadan build has no development test seams.")); \
        return true;                                                                                           \
    }

namespace P4LevelContent
{
inline bool P4UnheldClassValuesForTest(const UStruct* Schema, const FString& Json, FString& Kept, int32& Count)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->UnheldClassValues(Schema, Json, Kept, Count);
}
inline bool P4UnheldClassValueDetailsForTest(const UStruct* Schema, const FString& Json, FString& Kept, int32& Count,
    TArray<FP4OriginalClassValueNotHeld>& Details)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->UnheldClassValueDetails(Schema, Json, Kept, Count, Details);
}
} //namespace P4LevelContent
namespace P4Kadan
{
inline void ForgetGameActorNamesForTest(UWorld* World)
{
    if (const FP4KadanTestSeams* S = P4KadanTestSeams())
        S->ForgetGameActorNames(World);
}
} //namespace P4Kadan
inline bool P4TerrainTrackingLinesForTest(
    const FString& Class, const FString& Json, TArray<FString>& Compared, TArray<FString>& EngineMade)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->TerrainTrackingLines(Class, Json, Compared, EngineMade);
}
inline bool P4ClassComponentTailForTest(UClass* Schema, const TArray<uint8>& Bytes, int32& Lods)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->ClassComponentTail(Schema, Bytes, Lods);
}
inline bool P4ClassLightingTailForTest(
    UClass* Schema, const TArray<uint8>& Bytes, bool Undecoded, TArray<FString>& Compared, TArray<FString>& EngineMade)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->ClassLightingTail(Schema, Bytes, Undecoded, Compared, EngineMade);
}
inline TArray<FString> P4ClassSourceAdmissionForTest(
    const FString& ClassPath, const FString& Root, TSet<FName>& Unproven, int32& Asked)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S ? S->ClassSourceAdmission(ClassPath, Root, Unproven, Asked) : TArray<FString>();
}
inline FP4OriginalLevelImportResult P4RepairRetainedPlayerDefaultsForTest(
    const FString& Header, const FString& Body, const FString& ExpectedSourceDigest, const FString& ExpectedSavedDigest)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S ? S->RepairRetainedPlayerDefaults(Header, Body, ExpectedSourceDigest, ExpectedSavedDigest)
             : FP4OriginalLevelImportResult();
}
inline bool P4ApplyDeclaredLevelDefaultsForTest(
    UObject* Instance, UObject* Template, const FString& BoolProperty, bool BoolValue, FString& Error)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->ApplyDeclaredLevelDefaults(Instance, Template, BoolProperty, BoolValue, Error);
}
inline bool P4OriginalLevelStorageSubobjectMatchesForTest(
    UObject* Object, UObject* Outer, UClass* Declared, const FString& Name, uint32 SourceFlags, UObject* SourceTemplate)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->LevelStorageSubobjectMatches(Object, Outer, Declared, Name, SourceFlags, SourceTemplate);
}
inline FP4OriginalLevelImportResult P4ProbeOriginalGameBackedFamiliesForTest(
    const FString& HeaderFile, const FString& BodyFile)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S ? S->ProbeGameBackedFamilies(HeaderFile, BodyFile) : FP4OriginalLevelImportResult();
}
inline void P4DiscardOriginalFamilyProbeForTest(UWorld* World)
{
    if (const FP4KadanTestSeams* S = P4KadanTestSeams())
        S->DiscardFamilyProbe(World);
}
inline bool P4MaterialFamilyStageControlsForTest(TArray<FString>& Errors)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    if (!S)
    {
        Errors.Add(TEXT("This Kadan build has no development test seams"));
        return false;
    }
    return S->MaterialFamilyStageControls(Errors);
}
inline bool P4MaterialCollectionValueControl(UMaterialParameterCollection* Asset,
    const FP4MaterialUsedResourceRequest& Request, const FP4KadanSourceBinding& Binding, FString& Error)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->MaterialCollectionValues(Asset, Request, Binding, Error);
}
inline bool P4MaterialCollectionLayoutControl(
    const FShaderParametersMetadata& Metadata, int32 Scalars, int32 Vectors, FString& Error)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->MaterialCollectionLayout(Metadata, Scalars, Vectors, Error);
}
inline bool P4MaterialCollectionLifecycleControl(UMaterialParameterCollection* Asset, FString& Error)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return S && S->MaterialCollectionLifecycle(Asset, Error);
}
inline bool P4KadanSourceAcquisitionAllowed(const TArray<FString>& Packages, FString& Error)
{
    const FP4KadanTestSeams* S = P4KadanTestSeams();
    return !S || S->SourceAcquisitionAllowed(Packages, Error);
}

// Test-fixture custody only. Never changes the normal importer policy, selected tools or
// cancellation. A foreign project or thread cannot own a scope.
class FP4KadanScopedAcquisitionVeto
{
public:
    explicit FP4KadanScopedAcquisitionVeto(const FString& OwnerProject)
    {
        if ((Seams = P4KadanTestSeams()) != nullptr)
            Scope = Seams->BeginAcquisitionVeto(OwnerProject);
    }
    ~FP4KadanScopedAcquisitionVeto()
    {
        if (Seams && Scope)
            Seams->EndAcquisitionVeto(Scope);
    }
    bool IsActive() const { return Seams && Scope && Seams->AcquisitionVetoActive(Scope); }
    int32 DeniedBatches() const { return Seams && Scope ? Seams->AcquisitionVetoDenied(Scope) : 0; }
    const TSet<FString>& DeniedPackages() const
    {
        static const TSet<FString> None;
        const TSet<FString>* Packages = Seams && Scope ? Seams->AcquisitionVetoPackages(Scope) : nullptr;
        return Packages ? *Packages : None;
    }
    FP4KadanScopedAcquisitionVeto(const FP4KadanScopedAcquisitionVeto&) = delete;
    FP4KadanScopedAcquisitionVeto& operator=(const FP4KadanScopedAcquisitionVeto&) = delete;

private:
    const FP4KadanTestSeams* Seams = nullptr;
    void* Scope = nullptr;
};

// Forces one explicitly selected dependency through the real loader and wraps only a genuine
// source-read exception. It cannot fabricate a successful read.
class FP4KadanBlueprintWrappedSourceControl
{
public:
    FP4KadanBlueprintWrappedSourceControl(const FString& Root, const FString& Dependency)
    {
        if ((Seams = P4KadanTestSeams()) != nullptr)
            Scope = Seams->BeginWrappedBlueprintSource(Root, Dependency);
    }
    ~FP4KadanBlueprintWrappedSourceControl()
    {
        if (Seams && Scope)
            Seams->EndWrappedBlueprintSource(Scope);
    }
    int32 WrappedFailures() const { return Seams && Scope ? Seams->WrappedBlueprintSourceFailures(Scope) : 0; }
    FP4KadanBlueprintWrappedSourceControl(const FP4KadanBlueprintWrappedSourceControl&) = delete;
    FP4KadanBlueprintWrappedSourceControl& operator=(const FP4KadanBlueprintWrappedSourceControl&) = delete;

private:
    const FP4KadanTestSeams* Seams = nullptr;
    void* Scope = nullptr;
};
#endif

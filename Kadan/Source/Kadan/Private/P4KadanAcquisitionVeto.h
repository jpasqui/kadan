#pragma once
#include "CoreMinimal.h"
#include "P4KadanTestSeams.h"

// The development test module reaches the veto through the Kadan test seams (P4KadanTestSeams.h).
#if !(defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS)
// Called before reading a container index, creating staging folders or invoking a converter.
// Without a matching development scope, and always in a build without test seams, it allows.
bool P4KadanSourceAcquisitionAllowed(const TArray<FString>& Packages, FString& Error);

#if P4_KADAN_TEST_SEAMS
// Test-fixture custody only. Never changes the normal importer policy, selected
// tools, or cancellation behavior. A foreign project/thread cannot own a scope.
class FP4KadanScopedAcquisitionVeto
{
public:
    explicit FP4KadanScopedAcquisitionVeto(const FString& OwnerProject);
    ~FP4KadanScopedAcquisitionVeto();
    bool IsActive() const { return Active; }
    int32 DeniedBatches() const { return Denied; }
    const TSet<FString>& DeniedPackages() const { return Packages; }
    FP4KadanScopedAcquisitionVeto(const FP4KadanScopedAcquisitionVeto&) = delete;
    FP4KadanScopedAcquisitionVeto& operator=(const FP4KadanScopedAcquisitionVeto&) = delete;
private:
    friend bool P4KadanSourceAcquisitionAllowed(const TArray<FString>&, FString&);
    FP4KadanScopedAcquisitionVeto* Previous = nullptr;
    FString Project;
    TSet<FString> Packages;
    int32 Denied = 0;
    bool Active = false;
};
#endif
#endif

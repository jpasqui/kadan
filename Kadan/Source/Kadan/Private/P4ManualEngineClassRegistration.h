#pragma once
#include "CoreMinimal.h"

class UClass;

// Use only on a newly allocated manual class, before Bind/StaticLink and before
// any GetDefaultObject call. Existing-class lookup paths must not register again.
// UClass::CreateDefaultObject owns the CDO Started/Finished notifications.
class KADAN_API FP4ManualEngineClassRegistration
{
    UClass* Class;
    FString PackageName, ClassName;
    bool Valid = false, Notified = false, Finished = false;
public:
    explicit FP4ManualEngineClassRegistration(UClass* InClass, UObject* (*Register)() = nullptr);
    bool IsValid() const { return Valid; }
    bool NotificationsRequired() const { return Notified; }
    bool IsClassFinished() const { return Finished; }
    // Call after successful Bind/StaticLink and storage validation, before CDO creation.
    // Also assembles the stock GC reference stream before publishing Finished.
    // Repeated calls are harmless. Failure/destruction never publishes an invalid class.
    bool FinishClass();
    FP4ManualEngineClassRegistration(const FP4ManualEngineClassRegistration&) = delete;
    FP4ManualEngineClassRegistration& operator=(const FP4ManualEngineClassRegistration&) = delete;
};

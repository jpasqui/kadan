#pragma once
#include "CoreMinimal.h"
#include "P4ExportContainer.h"
namespace P4MapKit
{
namespace Material
{
struct FMaterialRegistryFamily;
}
} //namespace P4MapKit
class FP4CurrentGameRegistry;
using FP4CurrentGameRegistryPtr = TSharedPtr<const FP4CurrentGameRegistry, ESPMode::ThreadSafe>;
KADAN_API bool P4AcquireCurrentGameRegistry(const FP4ExportContainerTools&, const FString& Paks, const FString& Work,
    FP4CurrentGameRegistryPtr& Out, FString& Error);

// Read-only retained lookup: live lease or valid existing decoded cache only.
// Missing/invalid cache refuses before deletion, scratch creation, extraction or tools.
KADAN_API bool P4ReadRetainedCurrentGameRegistry(
    const FP4ExportContainerTools&, const FString& Paks, FP4CurrentGameRegistryPtr& Out, FString& Error);

// Valid only while explicitly owned. No persistent cache or mtime-only authority.
class KADAN_API FP4CurrentGameRegistry
{
public:
    ~FP4CurrentGameRegistry();
    bool IsCurrent(const FP4ExportContainerTools&, const FString& Paks, FString& Error) const;
    bool PackageNames(TArray<FString>& Out, FString& Error) const;
    bool Family(const FString& Selection, P4MapKit::Material::FMaterialRegistryFamily& Out, FString& Error) const;
    // Every Material and MaterialInstanceConstant package the registry's material rows name, sorted.
    bool MaterialPackages(TArray<FString>& Masters, TArray<FString>& Instances, FString& Error) const;
    const FString& RegistrySha256() const;
    const FString& RetocSha256() const;
    const FString& RepakSha256() const;
    const FString& MaterialRowsSha256() const;
    const FString& MaterialError() const;
    const FString& LeaseEpoch() const;
    static const TCHAR* MaterialSchema();

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
    explicit FP4CurrentGameRegistry(TUniquePtr<FImpl>&& In);
    friend bool P4ReadCurrentGameRegistryImpl(
        const FP4ExportContainerTools&, const FString&, const FString&, FP4CurrentGameRegistryPtr&, FString&, bool);
    friend KADAN_API bool P4AcquireCurrentGameRegistry(
        const FP4ExportContainerTools&, const FString&, const FString&, FP4CurrentGameRegistryPtr&, FString&);
};

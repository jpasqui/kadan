#pragma once
#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "P4CreatorMaterialFactories.generated.h"

class UTexture;

// Actual fresh-asset constructors. They refuse existing targets and never qualify a copied
// game graph or infer creator ownership from absent metadata or an asset-registry event.
UCLASS(HideCategories=Object, CollapseCategories, meta=(DisplayName="Material"))
class KADAN_API UP4CreatorMaterialFactory : public UFactory
{
    GENERATED_BODY()
public:
    UP4CreatorMaterialFactory();
    UPROPERTY() UTexture* InitialTexture = nullptr;
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual bool ShouldShowInNewMenu() const override { return true; }
    virtual FText GetDisplayName() const override;
    virtual uint32 GetMenuCategories() const override;
};

UCLASS(HideCategories=Object, CollapseCategories, meta=(DisplayName="Material Instance"))
class KADAN_API UP4CreatorMaterialInstanceFactory : public UMaterialInstanceConstantFactoryNew
{
    GENERATED_BODY()
public:
    UP4CreatorMaterialInstanceFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual bool ShouldShowInNewMenu() const override { return true; }
    virtual FText GetDisplayName() const override;
    virtual uint32 GetMenuCategories() const override;
};

// Same ordinary Browser creation action, with a factory whose completed constructor records
// its own source operation. Parents remain references; no game material graphs are copied.
KADAN_API void P4CreateCreatorMaterialInstances(const TArray<UObject*>& Parents);
KADAN_API void P4CreateCreatorMaterials(const TArray<UObject*>& Textures);

// Suppress only the two exact stock menu factories while the owned equivalents are active.
// Their prior CDO flags are retained and restored; no config or asset files are changed.
KADAN_API void P4InitializeCreatorMaterialFactories();
KADAN_API void P4ShutdownCreatorMaterialFactories();

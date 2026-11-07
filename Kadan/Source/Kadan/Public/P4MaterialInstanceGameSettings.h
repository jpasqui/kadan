#pragma once

#include "P4MaterialGameSettings.h"
#include "P4MaterialInstanceGameSettings.generated.h"

class UMaterialInstanceConstant;
class UMaterialInterface;
class FJsonObject;

// Local C4 base-property values. Unchecked fields resolve through the actual parent chain.
// The editor object is stripped; the cook writes these as the game's native base overrides.
UCLASS(HideCategories = (C4))
class KADAN_API UP4MaterialInstanceGameSettings : public UP4MaterialGameSettings
{
    GENERATED_BODY()
public:
    UPROPERTY() TSet<FName> Overrides;
    // Retains exact omitted/default and typed source records until a field is authored.
    UPROPERTY() FString SourceRecords;
    UPROPERTY() TSet<FName> Edited;

    static UP4MaterialInstanceGameSettings* Find(const UMaterialInstanceConstant* Instance);
    static UP4MaterialInstanceGameSettings* FindOrAdd(UMaterialInstanceConstant* Instance);
    bool Import(const TSharedPtr<FJsonObject>& Records, FString& Error);
    TSharedPtr<FJsonObject> Records() const;
    void Resolve(UP4MaterialGameSettings* Out) const;
    void RefreshInherited();
    void SetOverride(FName Property, bool Enabled);
    virtual bool CanEditChange(const FProperty* Property) const override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
};

// Only Nintendo's actual instance base fields, excluding master-only renderer modes.
KADAN_API bool P4IsC4InstanceProperty(FName Name);
KADAN_API FString P4MaterialInstanceComparisonTransport(const UMaterialInstanceConstant* Instance,
                                                        const FString& Original);
KADAN_API void P4ResolveMaterialC4(const UMaterialInterface* Material, UP4MaterialGameSettings* Out);
KADAN_API bool P4MaterialInstanceHasC4Overrides(const UMaterialInstanceConstant* Instance);
KADAN_API void P4ApplyMaterialInstanceC4ForCompile(const UMaterialInstanceConstant* Source, UMaterial* Copy);
void P4RegisterMaterialInstanceGameSettings();
void P4UnregisterMaterialInstanceGameSettings();

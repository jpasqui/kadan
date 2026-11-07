// The game's own material settings (P4MaterialGameSettings.h): kept with the material, shown on its Details panel
// and given to Kadan's shader format (KadanShaderFormat) for each of the material's shader jobs.
#include "P4MaterialGameSettings.h"
#include "P4KadanMaterialDetails.h"
#include "P4MaterialInstanceGameSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Features/IModularFeatures.h"
#include "IDetailCustomization.h"
#include "KadanSwitchShaderCompiler.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UnrealType.h"

namespace
{

// The settings of the loaded materials, by material name, as the format asks for them. A shader job names its
// material by the object's name only; two loaded materials of one name with different settings are reported.
struct FEntry
{
    const UP4MaterialGameSettings* Owner = nullptr;
    FString Path;
    TArray<TPair<FString, FString>> Defines;
};

class FRegistry final : public IKadanMaterialSettings
{
public:
    void Publish(const UP4MaterialGameSettings* Settings)
    {
        const UMaterialInterface* Material = Cast<UMaterialInterface>(Settings->GetOuter());
        FEntry Entry;
        Entry.Owner = Settings;
        if (Material)
            Entry.Path = Material->GetPathName();
        if (const auto* InstanceSettings = Cast<UP4MaterialInstanceGameSettings>(Settings))
        {
            auto* Effective = NewObject<UP4MaterialGameSettings>();
            InstanceSettings->Resolve(Effective);
            Effective->Defines(Entry.Defines);
        }
        else
        {
            Settings->Defines(Entry.Defines);
        }
        FScopeLock Locked(&Lock);
        RemoveLocked(Settings);
        if (!Material)
            return;
        // The newest settings of a material stand for it: the material editor's Apply rebuilds the material in place,
        // and its old settings object waits for the garbage collector.
        TArray<FEntry>& Entries = ByMaterial.FindOrAdd(Material->GetName());
        Entries.RemoveAll([Material](const FEntry& Other) { return Other.Owner->GetOuter() == Material; });
        Entries.Add(MoveTemp(Entry));
    }

    void Withdraw(const UP4MaterialGameSettings* Settings)
    {
        FScopeLock Locked(&Lock);
        RemoveLocked(Settings);
    }

    virtual bool Find(const FString& Material, TArray<TPair<FString, FString>>& Out) const override
    {
        FScopeLock Locked(&Lock);
        const TArray<FEntry>* Entries = ByMaterial.Find(Material);
        if (!Entries || Entries->Num() == 0)
            return false;
        for (const FEntry& Other : *Entries)
            if (Other.Defines != (*Entries)[0].Defines && !Reported.Contains(Material))
            {
                Reported.Add(Material);
                UE_LOG(LogTemp, Warning,
                    TEXT(
                        "Kadan: %s and %s have the same name and different C4 settings. Their shaders are compiled with the first one's."),
                    *(*Entries)[0].Path, *Other.Path);
            }
        Out = (*Entries)[0].Defines;
        return true;
    }

private:
    void RemoveLocked(const UP4MaterialGameSettings* Settings)
    {
        for (auto It = ByMaterial.CreateIterator(); It; ++It)
        {
            It.Value().RemoveAll([Settings](const FEntry& Entry) { return Entry.Owner == Settings; });
            if (It.Value().Num() == 0)
                It.RemoveCurrent();
        }
    }

    mutable FCriticalSection Lock;
    TMap<FString, TArray<FEntry>> ByMaterial;
    mutable TSet<FString> Reported;
};

FRegistry Registry;
bool bRegistered = false;

// After a change of the settings: the format learns them, and the material is changed as the editor changes it,
// which gives it a new id, so its shaders are compiled again with them.
void Changed(const UP4MaterialGameSettings* Settings)
{
    Settings->Publish();
    if (UMaterial* Material = Cast<UMaterial>(Settings->GetOuter()))
        if (UP4MaterialGameSettings::Find(Material) == Settings)
            Material->PostEditChange();
}

const FName LayoutClass = TEXT("MaterialInterface");

} // namespace

void UP4MaterialGameSettings::Defines(TArray<TPair<FString, FString>>& Out) const
{
    Out.Reset();
    for (TFieldIterator<FProperty> It(StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const void* At = It->ContainerPtrToValuePtr<void>(this);
        FString Value;
        if (const FFloatProperty* Float = CastField<FFloatProperty>(*It))
        {
            // Nine significant digits round-trip a float32 into the shader source.
            Value = FString::Printf(TEXT("%.9g"), double(Float->GetPropertyValue(At)));
            if (!Value.Contains(TEXT(".")) && !Value.Contains(TEXT("e"), ESearchCase::IgnoreCase))
            {
                Value += TEXT(".0");
            }
        }
        else if (const FBoolProperty* Bool = CastField<FBoolProperty>(*It))
            Value = Bool->GetPropertyValue(At) ? TEXT("1") : TEXT("0");
        else if (const FByteProperty* Byte = CastField<FByteProperty>(*It))
            Value = FString::FromInt(Byte->GetPropertyValue(At));
        else
            continue;
        Out.Emplace(It->GetName(), Value);
    }
}

void UP4MaterialGameSettings::Publish() const
{
    if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
        Registry.Publish(this);
}

UP4MaterialGameSettings* UP4MaterialGameSettings::Find(const UMaterial* Material)
{
    // UMaterialInterface keeps its user data without giving out the array.
    return Material
        ? Cast<UP4MaterialGameSettings>(const_cast<UMaterial*>(Material)->GetAssetUserDataOfClass(StaticClass()))
        : nullptr;
}

UP4MaterialGameSettings* UP4MaterialGameSettings::FindOrAdd(UMaterial* Material)
{
    if (UP4MaterialGameSettings* Found = Find(Material))
        return Found;
    UP4MaterialGameSettings* Settings = NewObject<UP4MaterialGameSettings>(Material, NAME_None, RF_Transactional);
    Material->AddAssetUserData(Settings);
    Settings->Publish();
    return Settings;
}

void UP4MaterialGameSettings::PostLoad()
{
    Super::PostLoad();
    Publish();
}

void UP4MaterialGameSettings::PostDuplicate(bool bDuplicateForPIE)
{
    Super::PostDuplicate(bDuplicateForPIE);
    Publish();
}

void UP4MaterialGameSettings::BeginDestroy()
{
    Registry.Withdraw(this);
    Super::BeginDestroy();
}

#if WITH_EDITOR
void UP4MaterialGameSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    if (Event.ChangeType != EPropertyChangeType::Interactive)
        Changed(this);
}

void UP4MaterialGameSettings::PostEditUndo()
{
    Super::PostEditUndo();
    Changed(this);
}
#endif

void P4RegisterMaterialGameSettings()
{
    if (bRegistered)
        return;
    IModularFeatures::Get().RegisterModularFeature(IKadanMaterialSettings::FeatureName(), &Registry);
    FPropertyEditorModule& Editor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    Editor.RegisterCustomClassLayout(
        LayoutClass, FOnGetDetailCustomizationInstance::CreateStatic(&P4KadanMaterialDetails));
    P4RegisterMaterialInstanceGameSettings();
    Editor.NotifyCustomizationModuleChanged();
    bRegistered = true;
}

void P4UnregisterMaterialGameSettings()
{
    if (!bRegistered)
        return;
    P4UnregisterMaterialInstanceGameSettings();
    IModularFeatures::Get().UnregisterModularFeature(IKadanMaterialSettings::FeatureName(), &Registry);
    if (FPropertyEditorModule* Editor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
        Editor->UnregisterCustomClassLayout(LayoutClass);
    bRegistered = false;
}

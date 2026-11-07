#include "P4MaterialInstanceGameSettings.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanMaterialDetails.h"
#include "P4OriginalRecord.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "PropertyEditorModule.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
FString Compact(const TSharedPtr<FJsonObject>& Json)
{
    FString Text = P4CondensedJson(Json.ToSharedRef());
    return Text;
}
FName Field(const FString& Key)
{
    const FName Plain(*Key), Boolean(*(TEXT("b") + Key));
    return FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), Plain) ? Plain : Boolean;
}
void Changed(UP4MaterialInstanceGameSettings* Settings)
{
    if (auto* Instance = Cast<UMaterialInstanceConstant>(Settings->GetOuter()))
    {
        Settings->RefreshInherited();
        Instance->Modify();
        Instance->PostEditChange();
    }
}
FOnGetDetailCustomizationInstance PreviousObjectLayout;
bool Registered = false;

// The material-instance editor customizes a UObject proxy rather than MaterialInterface.
// Its per-view layout wins over a global layout on that same proxy class. An additive
// UObject ancestor layout is queried as well; it changes only this exact proxy and
// preserves an existing ancestor customization.
class FInstanceDetails final : public IDetailCustomization
{
    TSharedPtr<IDetailCustomization> Previous;

public:
    static TSharedRef<IDetailCustomization> Make() { return MakeShared<FInstanceDetails>(); }
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        if (PreviousObjectLayout.IsBound())
        {
            Previous = PreviousObjectLayout.Execute();
            Previous->CustomizeDetails(Builder);
        }
        TArray<TWeakObjectPtr<UObject>> Objects;
        Builder.GetObjectsBeingCustomized(Objects);
        if (Objects.Num() != 1)
        {
            return;
        }
        auto* Proxy = Cast<UMaterialEditorInstanceConstant>(Objects[0].Get());
        // The layer panel's property-row generator assumes its first category
        // contains ParameterGroups. Extend only the actual Details view; adding
        // a category to that internal generator breaks stock Unreal's tree.
        if (Proxy && !Builder.GetDetailsView())
        {
            return;
        }
        auto* Instance = Proxy ? Proxy->SourceInstance : Cast<UMaterialInstanceConstant>(Objects[0].Get());
        if (!Instance || Instance->HasAnyFlags(RF_ClassDefaultObject))
        {
            return;
        }
        // Share the creator guidance and parent link, while this layout owns
        // the typed instance fields and preserves Unreal's proxy controls.
        auto Guidance = P4KadanMaterialDetails();
        Guidance->CustomizeDetails(Builder);
        auto* Settings = UP4MaterialInstanceGameSettings::Find(Instance);
        if (!Settings && P4QueryOriginalAsset(Instance).IsOriginal())
        {
            // Older imports carry transport metadata, not typed local settings.
            // A Details refresh must not replace those records with empty defaults.
            return;
        }
        // Engine and plugin content is not the project's: showing it in Details adds no Kadan data to it.
        const UPackage* Package = Instance->GetOutermost();
        if (!Settings && Package != GetTransientPackage() && !P4IsContentPackage(Package->GetName()))
        {
            return;
        }
        if (!Settings)
        {
            Settings = UP4MaterialInstanceGameSettings::FindOrAdd(Instance);
        }
        Settings->RefreshInherited();
        // Edit the native instance fields through their override rows. Exposing
        // the backing user-data object would also expose master-only fields.
        Builder.HideProperty(TEXT("AssetUserData"));
        auto& Category = Builder.EditCategory(
            TEXT("C4"), FText::FromString(TEXT("Game settings")), ECategoryPriority::Important);
        const TWeakObjectPtr<UMaterialInstanceConstant> WeakInstance(Instance);
        const TWeakObjectPtr<UP4MaterialInstanceGameSettings> WeakSettings(Settings);
        for (TFieldIterator<FProperty> It(UP4MaterialGameSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper);
            It; ++It)
        {
            const FName Name = It->GetFName();
            if (!P4IsC4InstanceProperty(Name))
            {
                continue;
            }
            const bool Advanced = It->HasAnyPropertyFlags(CPF_AdvancedDisplay);
            IDetailPropertyRow* Row = Category.AddExternalObjectProperty(
                {Settings}, Name, Advanced ? EPropertyLocation::Advanced : EPropertyLocation::Default);
            if (!Row)
            {
                continue;
            }
            TSharedPtr<SWidget> NameWidget, ValueWidget;
            Row->GetDefaultWidgets(NameWidget, ValueWidget);
            const auto Editable = [WeakInstance]()
            { return WeakInstance.IsValid() && P4Kadan::GetAssetPolicy(WeakInstance.Get()).AllowEdit; };
            Row->CustomWidget()
                .NameContent()[SNew(SHorizontalBox) +
                    SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SCheckBox)
                            .Tag(FName(*(TEXT("KadanMaterialOverride.") + Name.ToString())))
                            .IsEnabled_Lambda(Editable)
                            .IsChecked_Lambda(
                                [WeakSettings, Name]()
                                {
                                    return WeakSettings.IsValid() && WeakSettings->Overrides.Contains(Name)
                                        ? ECheckBoxState::Checked
                                        : ECheckBoxState::Unchecked;
                                })
                            .OnCheckStateChanged_Lambda(
                                [WeakSettings, Name](ECheckBoxState State)
                                {
                                    if (WeakSettings.IsValid())
                                    {
                                        WeakSettings->SetOverride(Name, State == ECheckBoxState::Checked);
                                    }
                                })] +
                    SHorizontalBox::Slot().FillWidth(1)[NameWidget.ToSharedRef()]]
                .ValueContent()[SNew(SHorizontalBox)
                                    .IsEnabled_Lambda(
                                        [Editable, WeakSettings, Name]()
                                        {
                                            return Editable() && WeakSettings.IsValid() &&
                                                WeakSettings->Overrides.Contains(Name);
                                        }) +
                    SHorizontalBox::Slot()[SNew(SBox).Tag(
                        FName(*(TEXT("KadanMaterial.") + Name.ToString())))[ValueWidget.ToSharedRef()]]];
        }
    }
};
} // namespace

bool P4IsC4InstanceProperty(FName Name)
{
    // The game's reflected MaterialInstanceBasePropertyOverrides. Param0/5/7,
    // mesh/draw modes and the other material flags exist only on the master.
    static const TSet<FName> Fields = {TEXT("C4ShaderParam1"), TEXT("C4ShaderParam2"), TEXT("C4ShaderParam3"),
        TEXT("C4ShaderParam4"), TEXT("C4ShaderParam6"), TEXT("C4ShaderParam8"), TEXT("C4ShaderParam9"),
        TEXT("C4ShaderParam10"), TEXT("bC4MaterialParam6"), TEXT("bC4MaterialParam7"), TEXT("C4MaterialParam8"),
        TEXT("bC4MaterialParam9"), TEXT("bC4MaterialParam10"), TEXT("bC4MaterialParam11")};
    return Fields.Contains(Name);
}

UP4MaterialInstanceGameSettings* UP4MaterialInstanceGameSettings::Find(const UMaterialInstanceConstant* Instance)
{
    return Instance ? Cast<UP4MaterialInstanceGameSettings>(
                          const_cast<UMaterialInstanceConstant*>(Instance)->GetAssetUserDataOfClass(StaticClass()))
                    : nullptr;
}
UP4MaterialInstanceGameSettings* UP4MaterialInstanceGameSettings::FindOrAdd(UMaterialInstanceConstant* Instance)
{
    if (auto* Found = Find(Instance))
    {
        return Found;
    }
    auto* Settings = NewObject<UP4MaterialInstanceGameSettings>(Instance, NAME_None, RF_Transactional);
    Instance->AddAssetUserData(Settings);
    Settings->RefreshInherited();
    return Settings;
}

void P4ResolveMaterialC4(const UMaterialInterface* Material, UP4MaterialGameSettings* Out)
{
    TSet<const UMaterialInterface*> Seen;
    TArray<const UP4MaterialInstanceGameSettings*> Chain;
    while (const auto* Instance = Cast<UMaterialInstanceConstant>(Material))
    {
        if (Seen.Contains(Material) || Seen.Num() >= 64)
        {
            break;
        }
        Seen.Add(Material);
        if (const auto* Local = UP4MaterialInstanceGameSettings::Find(Instance))
        {
            Chain.Add(Local);
        }
        Material = Instance->Parent;
    }
    const auto* Master = Cast<UMaterial>(Material);
    const auto* Base = UP4MaterialGameSettings::Find(Master);
    if (!Base)
    {
        Base = GetDefault<UP4MaterialGameSettings>();
    }
    for (TFieldIterator<FProperty> It(UP4MaterialGameSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It;
        ++It)
    {
        It->CopyCompleteValue_InContainer(Out, Base);
    }
    for (int32 Index = Chain.Num(); Index-- > 0;)
    {
        for (FName Name : Chain[Index]->Overrides)
        {
            if (const FProperty* P = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), Name))
            {
                P->CopyCompleteValue_InContainer(Out, Chain[Index]);
            }
        }
    }
}
void UP4MaterialInstanceGameSettings::Resolve(UP4MaterialGameSettings* Out) const
{
    P4ResolveMaterialC4(Cast<UMaterialInstanceConstant>(GetOuter()), Out);
}
void UP4MaterialInstanceGameSettings::RefreshInherited()
{
    const auto* Instance = Cast<UMaterialInstanceConstant>(GetOuter());
    auto* Resolved = NewObject<UP4MaterialGameSettings>();
    P4ResolveMaterialC4(Instance ? Instance->Parent : nullptr, Resolved);
    for (TFieldIterator<FProperty> It(UP4MaterialGameSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It;
        ++It)
    {
        if (!Overrides.Contains(It->GetFName()))
        {
            It->CopyCompleteValue_InContainer(this, Resolved);
        }
    }
}

bool UP4MaterialInstanceGameSettings::Import(const TSharedPtr<FJsonObject>& Records, FString& Error)
{
    if (!Records.IsValid())
    {
        return true;
    }
    Overrides.Reset();
    Edited.Reset();
    SourceRecords = Compact(Records);
    for (const auto& Pair : Records->Values)
    {
        const auto Record = Pair.Value->AsObject();
        const FName Name = Field(Pair.Key);
        FProperty* P = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), Name);
        if (!P || !P4IsC4InstanceProperty(Name))
        {
            Error = TEXT("material-instance-C4-field-unknown: ") + Pair.Key;
            return false;
        }
        if (Record->GetBoolField(TEXT("enabled")))
        {
            Overrides.Add(Name);
        }
        const auto Value = Record->GetObjectField(TEXT("value"));
        const FString Type = Value->GetStringField(TEXT("type"));
        if (Type == TEXT("omitted-cooked-value"))
        {
            // An enabled flag with an omitted value means the native struct's
            // default, even when the parent differs. Unchecked fields inherit
            // later, in RefreshInherited.
            P->CopyCompleteValue_InContainer(this, GetDefault<UP4MaterialGameSettings>());
            continue;
        }
        void* At = P->ContainerPtrToValuePtr<void>(this);
        if (auto* Float = CastField<FFloatProperty>(P))
        {
            if (Type != TEXT("float32"))
            {
                Error = TEXT("material-instance-C4-float-type");
                return false;
            }
            const FString Bits = Value->GetStringField(TEXT("bits"));
            uint32 Raw = 0;
            for (int32 Byte = 0; Byte < 4; ++Byte)
            {
                Raw |= FParse::HexNumber(*Bits.Mid(Byte * 2, 2)) << (Byte * 8);
            }
            float Number;
            FMemory::Memcpy(&Number, &Raw, sizeof(Number));
            Float->SetPropertyValue(At, Number);
        }
        else if (auto* Bool = CastField<FBoolProperty>(P))
        {
            if (Type != TEXT("bool"))
            {
                Error = TEXT("material-instance-C4-bool-type");
                return false;
            }
            Bool->SetPropertyValue(At, Value->GetBoolField(TEXT("value")));
        }
        else if (auto* Byte = CastField<FByteProperty>(P))
        {
            const int64 Number = Type == TEXT("name") && Byte->Enum
                ? Byte->Enum->GetValueByNameString(Value->GetStringField(TEXT("value")))
                : Type == TEXT("integer") ? int64(Value->GetNumberField(TEXT("value")))
                                          : INDEX_NONE;
            if (Number < 0 || Number > 255)
            {
                Error = TEXT("material-instance-C4-enum-value");
                return false;
            }
            Byte->SetPropertyValue(At, uint8(Number));
        }
    }
    RefreshInherited();
    return true;
}

TSharedPtr<FJsonObject> UP4MaterialInstanceGameSettings::Records() const
{
    TSharedPtr<FJsonObject> Out;
    if (!SourceRecords.IsEmpty())
    {
        P4ParseJson(SourceRecords, Out);
    }
    if (!Out.IsValid())
    {
        Out = MakeShared<FJsonObject>();
    }
    for (FName Name : Edited)
    {
        const FProperty* P = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), Name);
        if (!P || !P4IsC4InstanceProperty(Name))
        {
            continue;
        }
        const FString Alias = Name.ToString(), Key = Alias.StartsWith(TEXT("bC4")) ? Alias.Mid(1) : Alias;
        auto Record = MakeShared<FJsonObject>(), Value = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("flag"), TEXT("bOverride_") + Key);
        Record->SetBoolField(TEXT("enabled"), Overrides.Contains(Name));
        Record->SetBoolField(TEXT("value_present"), true);
        Record->SetStringField(TEXT("value_property"), Alias);
        const void* At = P->ContainerPtrToValuePtr<void>(this);
        if (const auto* Float = CastField<FFloatProperty>(P))
        {
            const float Number = Float->GetPropertyValue(At);
            const auto* Bytes = reinterpret_cast<const uint8*>(&Number);
            FString Bits;
            for (int32 Byte = 0; Byte < 4; ++Byte)
            {
                Bits += FString::Printf(TEXT("%02x"), Bytes[Byte]);
            }
            Value->SetStringField(TEXT("type"), TEXT("float32"));
            Value->SetNumberField(TEXT("value"), double(Number));
            Value->SetStringField(TEXT("bits"), Bits);
        }
        else if (const auto* Bool = CastField<FBoolProperty>(P))
        {
            Value->SetStringField(TEXT("type"), TEXT("bool"));
            Value->SetBoolField(TEXT("value"), Bool->GetPropertyValue(At));
        }
        else if (const auto* Byte = CastField<FByteProperty>(P))
        {
            Value->SetStringField(TEXT("type"), TEXT("name"));
            Value->SetStringField(TEXT("value"), Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(At)));
        }
        Record->SetObjectField(TEXT("value"), Value);
        Out->SetObjectField(Key, Record);
    }
    return Out;
}
FString P4MaterialInstanceComparisonTransport(const UMaterialInstanceConstant* Instance, const FString& Original)
{
    const auto* Settings = UP4MaterialInstanceGameSettings::Find(Instance);
    if (!Settings || Settings->Edited.Num() == 0)
    {
        // Preserve the original comparison frame, including absent transport.
        return Original;
    }
    TSharedPtr<FJsonObject> Transport;
    if (!P4ParseJson(Original, Transport))
    {
        Transport = MakeShared<FJsonObject>();
    }
    Transport->SetObjectField(TEXT("game_overrides"), Settings->Records());
    return Compact(Transport);
}
void UP4MaterialInstanceGameSettings::SetOverride(FName Property, bool Enabled)
{
    auto* Instance = Cast<UMaterialInstanceConstant>(GetOuter());
    if (!P4IsC4InstanceProperty(Property) || !Instance || !P4Kadan::GetAssetPolicy(Instance).AllowEdit)
    {
        return;
    }
    const FScopedTransaction Transaction(FText::FromString(TEXT("Edit C4 instance override")));
    Instance->Modify();
    Modify();
    if (Enabled)
    {
        Overrides.Add(Property);
    }
    else
    {
        Overrides.Remove(Property);
    }
    Edited.Add(Property);
    Changed(this);
}
bool UP4MaterialInstanceGameSettings::CanEditChange(const FProperty* Property) const
{
    const auto* Instance = Cast<UMaterialInstanceConstant>(GetOuter());
    return Property && P4IsC4InstanceProperty(Property->GetFName()) && Overrides.Contains(Property->GetFName()) &&
        Instance && P4Kadan::GetAssetPolicy(const_cast<UMaterialInstanceConstant*>(Instance)).AllowEdit &&
        Super::CanEditChange(Property);
}
void UP4MaterialInstanceGameSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    // The master callback only recompiles an actual UMaterial owner.
    Super::PostEditChangeProperty(Event);
    if (Event.Property && P4IsC4InstanceProperty(Event.Property->GetFName()))
    {
        Edited.Add(Event.Property->GetFName());
    }
    if (Event.ChangeType != EPropertyChangeType::Interactive)
    {
        Changed(this);
    }
}
void UP4MaterialInstanceGameSettings::PostEditUndo()
{
    Super::PostEditUndo();
    Changed(this);
}

bool P4MaterialInstanceHasC4Overrides(const UMaterialInstanceConstant* Instance)
{
    TSet<const UMaterialInstanceConstant*> Seen;
    while (Instance && !Seen.Contains(Instance) && Seen.Num() < 64)
    {
        Seen.Add(Instance);
        const auto* Settings = UP4MaterialInstanceGameSettings::Find(Instance);
        if (Settings && Settings->Overrides.Num())
        {
            return true;
        }
        Instance = Cast<UMaterialInstanceConstant>(Instance->Parent);
    }
    return false;
}
void P4ApplyMaterialInstanceC4ForCompile(const UMaterialInstanceConstant* Source, UMaterial* Copy)
{
    if (!Source || !Copy)
    {
        return;
    }
    auto* Effective = UP4MaterialGameSettings::FindOrAdd(Copy);
    P4ResolveMaterialC4(Source, Effective);
    Effective->Publish();
    // C4 is outside stock UE's shader-map ID. Include the complete resolved settings
    // in the private master identity, preserving DDC reuse for unchanged cold recooks.
    const UMaterial* AuthoredMaster = const_cast<UMaterialInstanceConstant*>(Source)->GetMaterial();
    FString Frame = AuthoredMaster->StateId.ToString(EGuidFormats::Digits);
    TArray<TPair<FString, FString>> Defines;
    Effective->Defines(Defines);
    for (const auto& Define : Defines)
    {
        Frame += TEXT("\n") + Define.Key + TEXT("=") + Define.Value;
    }
    FGuid::ParseExact(FMD5::HashAnsiString(*Frame), EGuidFormats::Digits, Copy->StateId);
}
void P4RegisterMaterialInstanceGameSettings()
{
    if (Registered)
    {
        return;
    }
    // Unreal's standard Object layout is registered by this module. Load it
    // first, then extend it; a later engine startup must not replace our layout.
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("DetailCustomizations"));
    auto& Editor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    if (const auto* Prior = Editor.GetClassNameToDetailLayoutNameMap().Find(TEXT("Object")))
    {
        PreviousObjectLayout = Prior->DetailLayoutDelegate;
    }
    Editor.RegisterCustomClassLayout(
        TEXT("Object"), FOnGetDetailCustomizationInstance::CreateStatic(&FInstanceDetails::Make));
    Registered = true;
}
void P4UnregisterMaterialInstanceGameSettings()
{
    if (!Registered)
    {
        return;
    }
    if (auto* Editor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
    {
        Editor->UnregisterCustomClassLayout(TEXT("Object"));
        if (PreviousObjectLayout.IsBound() && FModuleManager::Get().IsModuleLoaded(TEXT("DetailCustomizations")))
        {
            Editor->RegisterCustomClassLayout(TEXT("Object"), PreviousObjectLayout);
        }
    }
    PreviousObjectLayout.Unbind();
    Registered = false;
}

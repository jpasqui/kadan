#include "P4KadanReimportAsset.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4MaterialInstance.h"
#include "P4MaterialInstanceGameSettings.h"
#include "MaterialEditingLibrary.h"
#include "P4MaterialInstanceComparison.h"
#include "P4PackageSave.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "StaticParameterSet.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/MetaData.h"

// Defined in the creator-source implementation, deliberately not an arbitrary qualification API.
void P4RecordBuiltCreatorInstance(UMaterialInstanceConstant* Instance);

namespace P4MaterialInstance
{
bool IsStandIn(UObject* Asset)
{
    return IsValid(Asset) &&
        Asset->GetOutermost()->GetMetaData()->GetValue(Asset, TEXT("P4BlueprintStandIn")) == TEXT("1");
}
// A JSON object's keys in a fixed order.
TArray<FString> Keys(const TSharedPtr<FJsonObject>& Object)
{
    TArray<FString> Out;
    if (Object.IsValid())
        Object->Values.GetKeys(Out);
    Out.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    return Out;
}
const TSharedPtr<FJsonObject>* Part(const TSharedPtr<FJsonObject>& Root, const TCHAR* Name, bool& Malformed)
{
    const TSharedPtr<FJsonObject>* Out = nullptr;
    if (Root->HasField(Name) && !Root->TryGetObjectField(Name, Out))
        Malformed = true;
    return Out;
}

UMaterialInstanceConstant* Build(const FString& InstanceJson, const FString& Package, TArray<FString>& NotInParent,
    FString& Error, const FP4RecoveredMaterialInstanceProof* Proof = nullptr)
{
    Error.Reset();
    NotInParent.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("material-instance-requires-editor-thread");
        return nullptr;
    }
    if (Proof && !Proof->Current(InstanceJson, Package, Error))
        return nullptr;
    TSharedPtr<FJsonObject> Root;
    FString ParentPath;
    bool Malformed = false;
    if (!P4ParseJson(InstanceJson, Root) || !Root->TryGetStringField(TEXT("parent"), ParentPath))
    {
        Error = TEXT("material-instance-not-an-instance");
        return nullptr;
    }
    const TSharedPtr<FJsonObject>* Scalars = Part(Root, TEXT("scalars"), Malformed);
    const TSharedPtr<FJsonObject>* Vectors = Part(Root, TEXT("vectors"), Malformed);
    const TSharedPtr<FJsonObject>* Textures = Part(Root, TEXT("textures"), Malformed);
    const TSharedPtr<FJsonObject>* Switches = Part(Root, TEXT("switches"), Malformed);
    const TSharedPtr<FJsonObject>* Masks = Part(Root, TEXT("masks"), Malformed);
    const TSharedPtr<FJsonObject>* Overrides = Part(Root, TEXT("overrides"), Malformed);
    const TSharedPtr<FJsonObject>* GameOverrides = Part(Root, TEXT("game_overrides"), Malformed);
    if (Malformed)
    {
        Error = TEXT("material-instance-not-an-instance");
        return nullptr;
    }
    if (!P4IsValidContentPackage(Package))
    {
        Error = TEXT("material-instance-destination-not-in-content");
        return nullptr;
    }
    // A game package may spell its exported object's FName with different case
    // (Ground33_SoftOfF / Ground33_SoftOff). Preserve the authenticated export
    // spelling; public creator JSON cannot select an independent object name.
    const FString Name = Proof ? Proof->SourceObjectName() : FPackageName::GetLongPackageAssetName(Package);
    if ((!P4KadanOwnsReimportSlot(Package) && FPackageName::DoesPackageExist(Package)) ||
        IsValid(FindObject<UObject>(nullptr, *(Package + TEXT(".") + Name))))
    {
        UObject* Existing = StaticLoadObject(
            UObject::StaticClass(), nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
        Error = IsStandIn(Existing)
            ? (P4IsVerifiedStandIn(Existing) ? TEXT("material-instance-stand-in-fill-unavailable")
                                             : TEXT("material-instance-edited-stand-in-refused"))
            : TEXT("material-instance-destination-holds-another-asset");
        return nullptr;
    }
    UMaterialInterface* Parent = Cast<UMaterialInterface>(
        StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ParentPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
    if (!Parent)
    {
        Error = TEXT("material-instance-parent-not-in-the-project: ") + ParentPath;
        return nullptr;
    }
    if (IsStandIn(Parent))
    {
        Error = TEXT("material-instance-parent-is-unrecovered-stand-in: ") + ParentPath;
        return nullptr;
    }

    // Everything is read and checked before the instance is made.
    TArray<TPair<FName, float>> ScalarValues;
    TArray<TPair<FName, FLinearColor>> VectorValues;
    TArray<TPair<FName, UTexture*>> TextureValues;
    TArray<TPair<FName, bool>> SwitchValues;
    TArray<TPair<FName, TArray<bool>>> MaskValues;
    for (const FString& Key : Scalars ? Keys(*Scalars) : TArray<FString>())
    {
        double Number = 0;
        if (!(*Scalars)->Values[Key]->TryGetNumber(Number))
        {
            Error = TEXT("material-instance-scalar-needs-a-number: ") + Key;
            return nullptr;
        }
        ScalarValues.Emplace(FName(*Key), float(Number));
    }
    for (const FString& Key : Vectors ? Keys(*Vectors) : TArray<FString>())
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        double Parts[4] = {0, 0, 0, 0};
        bool Good = (*Vectors)->Values[Key]->TryGetArray(Items) && Items->Num() == 4;
        for (int32 Index = 0; Good && Index < 4; ++Index)
            Good = (*Items)[Index]->TryGetNumber(Parts[Index]);
        if (!Good)
        {
            Error = TEXT("material-instance-vector-needs-four-numbers: ") + Key;
            return nullptr;
        }
        VectorValues.Emplace(
            FName(*Key), FLinearColor(float(Parts[0]), float(Parts[1]), float(Parts[2]), float(Parts[3])));
    }
    for (const FString& Key : Textures ? Keys(*Textures) : TArray<FString>())
    {
        // A native MIC can explicitly hold a null texture. Preserve that local value, including its
        // distinction from an omitted override; no source asset is invented or requested for it.
        if ((*Textures)->Values[Key]->Type == EJson::Null)
        {
            TextureValues.Emplace(FName(*Key), nullptr);
            continue;
        }
        FString Path;
        if (!(*Textures)->Values[Key]->TryGetString(Path))
        {
            Error = TEXT("material-instance-texture-needs-an-asset-path: ") + Key;
            return nullptr;
        }
        UTexture* Texture = Cast<UTexture>(
            StaticLoadObject(UTexture::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
        if (!Texture)
        {
            Error = TEXT("material-instance-texture-not-in-the-project: ") + Path;
            return nullptr;
        }
        if (IsStandIn(Texture))
        {
            Error = TEXT("material-instance-texture-is-unrecovered-stand-in: ") + Path;
            return nullptr;
        }
        TextureValues.Emplace(FName(*Key), Texture);
    }
    for (const FString& Key : Switches ? Keys(*Switches) : TArray<FString>())
    {
        const TSharedPtr<FJsonValue>& Value = (*Switches)->Values[Key];
        if (!Value.IsValid() || Value->Type != EJson::Boolean)
        {
            Error = TEXT("material-instance-switch-needs-true-or-false: ") + Key;
            return nullptr;
        }
        SwitchValues.Emplace(FName(*Key), Value->AsBool());
    }
    FMaterialInstanceBasePropertyOverrides Base;
    FString Word;
    double Number = 0;
    for (const FString& Key : Masks ? Keys(*Masks) : TArray<FString>())
    {
        const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
        if (!(*Masks)->Values[Key]->TryGetArray(Channels) || Channels->Num() != 4)
        {
            Error = TEXT("material-instance-mask-needs-four-booleans: ") + Key;
            return nullptr;
        }
        TArray<bool> Values;
        for (const auto& Channel : *Channels)
        {
            if (!Channel.IsValid() || Channel->Type != EJson::Boolean)
            {
                Error = TEXT("material-instance-mask-needs-four-booleans: ") + Key;
                return nullptr;
            }
            Values.Add(Channel->AsBool());
        }
        MaskValues.Emplace(FName(*Key), MoveTemp(Values));
    }
    if (Overrides)
    {
        for (const FString& Key : Keys(*Overrides))
            if (Key != TEXT("two_sided") && Key != TEXT("blend_mode") && Key != TEXT("shading_model") &&
                Key != TEXT("clip") && Key != TEXT("render_settings") && Key != TEXT("dithered_lod") &&
                Key != TEXT("shadow_as_masked"))
            {
                Error = TEXT("material-instance-override-unknown: ") + Key;
                return nullptr;
            }
        if ((*Overrides)->HasField(TEXT("two_sided")))
        {
            const TSharedPtr<FJsonValue> Value = (*Overrides)->TryGetField(TEXT("two_sided"));
            if (!Value.IsValid() || Value->Type != EJson::Boolean)
            {
                Error = TEXT("material-instance-override-malformed: two_sided");
                return nullptr;
            }
            Base.bOverride_TwoSided = true;
            Base.TwoSided = Value->AsBool();
        }
        if ((*Overrides)->HasField(TEXT("blend_mode")))
        {
            const int64 Value = (*Overrides)->TryGetStringField(TEXT("blend_mode"), Word)
                ? StaticEnum<EBlendMode>()->GetValueByNameString(Word)
                : INDEX_NONE;
            if (Value == INDEX_NONE)
            {
                Error = TEXT("material-instance-override-malformed: blend_mode");
                return nullptr;
            }
            Base.bOverride_BlendMode = true;
            Base.BlendMode = EBlendMode(Value);
        }
        if ((*Overrides)->HasField(TEXT("shading_model")))
        {
            const int64 Value = (*Overrides)->TryGetStringField(TEXT("shading_model"), Word)
                ? StaticEnum<EMaterialShadingModel>()->GetValueByNameString(Word)
                : INDEX_NONE;
            if (Value == INDEX_NONE || Value >= int64(MSM_NUM))
            {
                Error = TEXT("material-instance-override-malformed: shading_model");
                return nullptr;
            }
            Base.bOverride_ShadingModel = true;
            Base.ShadingModel = EMaterialShadingModel(Value);
        }
        if ((*Overrides)->HasField(TEXT("clip")))
        {
            if (!(*Overrides)->TryGetNumberField(TEXT("clip"), Number))
            {
                Error = TEXT("material-instance-override-malformed: clip");
                return nullptr;
            }
            Base.bOverride_OpacityMaskClipValue = true;
            Base.OpacityMaskClipValue = float(Number);
        }
        for (const TCHAR* Key : {TEXT("dithered_lod"), TEXT("shadow_as_masked")})
            if ((*Overrides)->HasField(Key))
            {
                const auto Value = (*Overrides)->TryGetField(Key);
                if (!Value.IsValid() || Value->Type != EJson::Boolean)
                {
                    Error = TEXT("material-instance-override-malformed: ") + FString(Key);
                    return nullptr;
                }
                if (FString(Key) == TEXT("dithered_lod"))
                {
                    Base.bOverride_DitheredLODTransition = true;
                    Base.DitheredLODTransition = Value->AsBool();
                }
                else
                {
                    Base.bOverride_CastDynamicShadowAsMasked = true;
                    Base.bCastDynamicShadowAsMasked = Value->AsBool();
                }
            }
        // Stock MICs have no override fields for the master's translucency/renderer settings.
        // Null is the family merger's explicit inherited-dictionary removal, not false or zero.
        if ((*Overrides)->HasField(TEXT("render_settings")) &&
            (*Overrides)->TryGetField(TEXT("render_settings"))->Type != EJson::Null)
        {
            Error = TEXT("material-instance-render-settings-overlay-needs-game-engine");
            return nullptr;
        }
    }
    if (GameOverrides)
        for (const FString& Key : Keys(*GameOverrides))
        {
            const TSharedPtr<FJsonObject>* Record = nullptr;
            FString Flag;
            bool Enabled = false, Present = false;
            const TSharedPtr<FJsonObject>* Value = nullptr;
            FString Type;
            if (!(*GameOverrides)->Values[Key]->TryGetObject(Record) ||
                !(*Record)->TryGetStringField(TEXT("flag"), Flag) || !Flag.StartsWith(TEXT("bOverride_")) ||
                !(*Record)->TryGetBoolField(TEXT("enabled"), Enabled) ||
                !(*Record)->TryGetBoolField(TEXT("value_present"), Present) ||
                !(*Record)->HasField(TEXT("value_property")) || !(*Record)->TryGetObjectField(TEXT("value"), Value) ||
                !(*Value)->TryGetStringField(TEXT("type"), Type) ||
                (Type != TEXT("bool") && Type != TEXT("integer") && Type != TEXT("float32") && Type != TEXT("name") &&
                    Type != TEXT("omitted-cooked-value")))
            {
                Error = TEXT("material-instance-game-override-record-malformed: ") + Key;
                return nullptr;
            }
            if (!Present && Type != TEXT("omitted-cooked-value"))
            {
                Error = TEXT("material-instance-game-override-omitted-value-malformed: ") + Key;
                return nullptr;
            }
            FString Alias, Bits, TypedName;
            double Numeric = 0;
            bool Boolean = false;
            const TSharedPtr<FJsonValue> AliasValue = (*Record)->TryGetField(TEXT("value_property"));
            bool Good = Key.StartsWith(TEXT("C4")) &&
                (Flag == TEXT("bOverride_") + Key || Flag == TEXT("bOverride_b") + Key);
            Good = Good && (*Record)->HasTypedField<EJson::Boolean>(TEXT("enabled")) &&
                (*Record)->HasTypedField<EJson::Boolean>(TEXT("value_present"));
            Good = Good &&
                (Present ? AliasValue->TryGetString(Alias) && (Alias == Key || Alias == TEXT("b") + Key)
                         : AliasValue->Type == EJson::Null);
            if (Type == TEXT("omitted-cooked-value"))
                Good = Good && !Present && (*Value)->Values.Num() == 1;
            else if (Type == TEXT("bool"))
                Good = Good && Present && (*Value)->HasTypedField<EJson::Boolean>(TEXT("value")) &&
                    (*Value)->TryGetBoolField(TEXT("value"), Boolean);
            else if (Type == TEXT("integer"))
                Good = Good && Present && (*Value)->HasTypedField<EJson::Number>(TEXT("value")) &&
                    (*Value)->TryGetNumberField(TEXT("value"), Numeric) && FMath::IsFinite(Numeric) &&
                    Numeric == FMath::FloorToDouble(Numeric);
            else if (Type == TEXT("name"))
                Good = Good && Present && (*Value)->TryGetStringField(TEXT("value"), TypedName);
            else if (Type == TEXT("float32"))
            {
                Good = Good && Present && (*Value)->HasTypedField<EJson::Number>(TEXT("value")) &&
                    (*Value)->TryGetNumberField(TEXT("value"), Numeric) && FMath::IsFinite(Numeric) &&
                    (*Value)->TryGetStringField(TEXT("bits"), Bits) && Bits.Len() == 8;
                if (Good)
                {
                    const float Float = float(Numeric);
                    const uint8* Bytes = reinterpret_cast<const uint8*>(&Float);
                    FString Actual;
                    for (int32 Index = 0; Index < 4; ++Index)
                        Actual += FString::Printf(TEXT("%02x"), Bytes[Index]);
                    // JSON writers may spell negative zero as 0. The retained IEEE bits remain
                    // authoritative; accept either zero encoding without replacing those bits.
                    const bool EncodedZero = Numeric == 0.0 && (Bits == TEXT("00000000") || Bits == TEXT("00000080"));
                    Good = (Bits.Equals(Actual, ESearchCase::IgnoreCase) || EncodedZero) && double(Float) == Numeric;
                }
            }
            if (!Good)
            {
                Error = TEXT("material-instance-game-override-typed-value-malformed: ") + Key;
                return nullptr;
            }
        }

    UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(
        CreatePackage(*Package), FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
    Instance->SetParentEditorOnly(Parent);
    UP4MaterialInstanceGameSettings* C4 = UP4MaterialInstanceGameSettings::FindOrAdd(Instance);
    if (GameOverrides && !C4->Import(*GameOverrides, Error))
    {
        Instance->ClearFlags(RF_Public | RF_Standalone);
        Instance->MarkPendingKill();
        return nullptr;
    }
    // What the parent has, to say which of the game's values it has no place for.
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    TSet<FName> Known;
    Parent->GetAllScalarParameterInfo(Infos, Ids);
    for (const auto& Info : Infos)
        Known.Add(Info.Name);
    for (const auto& Value : ScalarValues)
    {
        if (!Known.Contains(Value.Key))
            NotInParent.Add(TEXT("scalar ") + Value.Key.ToString());
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Value.Key), Value.Value);
    }
    Infos.Reset();
    Ids.Reset();
    Known.Reset();
    Parent->GetAllVectorParameterInfo(Infos, Ids);
    for (const auto& Info : Infos)
        Known.Add(Info.Name);
    for (const auto& Value : VectorValues)
    {
        if (!Known.Contains(Value.Key))
            NotInParent.Add(TEXT("vector ") + Value.Key.ToString());
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(Value.Key), Value.Value);
    }
    Infos.Reset();
    Ids.Reset();
    Known.Reset();
    Parent->GetAllTextureParameterInfo(Infos, Ids);
    for (const auto& Info : Infos)
        Known.Add(Info.Name);
    for (const auto& Value : TextureValues)
    {
        if (!Known.Contains(Value.Key))
            NotInParent.Add(TEXT("texture ") + Value.Key.ToString());
        Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Value.Key), Value.Value);
    }
    FStaticParameterSet Static;
    Instance->GetStaticParameterValues(Static);
    for (const auto& Value : SwitchValues)
    {
        FStaticSwitchParameter* Found = Static.StaticSwitchParameters.FindByPredicate(
            [&](const FStaticSwitchParameter& Item) { return Item.ParameterInfo.Name == Value.Key; });
        if (Found)
        {
            Found->Value = Value.Value;
            Found->bOverride = true;
            continue;
        }
        NotInParent.Add(TEXT("switch ") + Value.Key.ToString());
        Static.StaticSwitchParameters.Add(
            FStaticSwitchParameter(FMaterialParameterInfo(Value.Key), Value.Value, true, FGuid()));
    }
    for (const auto& Value : MaskValues)
    {
        auto* Found = Static.StaticComponentMaskParameters.FindByPredicate(
            [&](const FStaticComponentMaskParameter& Item) { return Item.ParameterInfo.Name == Value.Key; });
        const auto& Mask = Value.Value;
        if (Found)
        {
            Found->R = Mask[0];
            Found->G = Mask[1];
            Found->B = Mask[2];
            Found->A = Mask[3];
            Found->bOverride = true;
        }
        else
        {
            NotInParent.Add(TEXT("mask ") + Value.Key.ToString());
            Static.StaticComponentMaskParameters.Add(FStaticComponentMaskParameter(
                FMaterialParameterInfo(Value.Key), Mask[0], Mask[1], Mask[2], Mask[3], true, FGuid()));
        }
    }
    Instance->UpdateStaticPermutation(Static, Base);
    // The ordinary editor update also resolves parameter expression bindings.
    // Do this only on an actually recovered fresh build, before its first native
    // baseline; PostLoad performs the same binding initialization on cold load.
    if (Proof)
        UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
    else
    {
        Instance->PreEditChange(nullptr);
        Instance->PostEditChange();
    }
    // What the instance does not override is the parent's. The engine copies it when asked.
    Instance->UpdateOverridableBaseProperties();
    FAssetRegistryModule::AssetCreated(Instance);
    Instance->MarkPackageDirty();
    if (GameOverrides || (Overrides && (*Overrides)->HasField(TEXT("render_settings"))))
    {
        TSharedRef<FJsonObject> Transport = MakeShared<FJsonObject>();
        if (GameOverrides)
            Transport->SetObjectField(TEXT("game_overrides"), *GameOverrides);
        if (Overrides && (*Overrides)->HasField(TEXT("render_settings")))
            Transport->SetField(TEXT("render_settings"), (*Overrides)->TryGetField(TEXT("render_settings")));
        FString Text = P4CondensedJson(Transport);
        Instance->GetOutermost()->GetMetaData()->SetValue(Instance, TEXT("P4MaterialInstanceTransportV2"), *Text);
    }
    // A game instance is rebuilt only inside a game import. Outside one, an instance is the creator's
    // wherever it is, also next to the game's folders.
    if (P4ImportPublication::IsImporting() &&
        Package.StartsWith(TEXT(P4_GAME_ASSET_ROOT "/"), ESearchCase::CaseSensitive))
    {
        FP4OriginalRecord Record;
        Record.Package = Package;
        Record.Class = Instance->GetClass()->GetPathName();
        Record.Kind = TEXT("material-instance");
        Record.SourceDigest = FMD5::HashAnsiString(*InstanceJson);
        Record.SourceDigestKind = TEXT("material-instance-v2-md5");
        if (GameOverrides)
            for (const FString& Key : Keys(*GameOverrides))
            {
                FP4OriginalValueNote Note;
                Note.Kind = TEXT("game-override");
                Note.Object = Package;
                Note.Property = Key;
                FJsonSerializer::Serialize((*GameOverrides)->Values[Key]->AsObject().ToSharedRef(),
                    TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Note.GameValue));
                Note.Action = TEXT("preserved-transport-only-engine-behavior-unverified");
                Record.Values.Add(MoveTemp(Note));
            }
        const bool HasRenderOverlay = Overrides && (*Overrides)->HasField(TEXT("render_settings"));
        if (HasRenderOverlay)
        {
            // Only the null removal marker is admitted above. Transport does not make stock MIC
            // renderer inheritance equivalent to the game's owner overlay semantics.
            FP4OriginalValueNote Note;
            Note.Kind = TEXT("render-settings-overlay");
            Note.Object = Package;
            Note.Property = TEXT("render_settings");
            Note.GameValue = TEXT("null");
            Note.Action = TEXT("null-removal-marker-preserved-transport-only-engine-policy-unverified");
            Record.Values.Add(MoveTemp(Note));
        }
        Record.ComparisonSchema = TEXT("material-instance-v2");
        Record.Fidelity = GameOverrides ? TEXT("game-overrides-preserved-engine-policy-unverified")
            : HasRenderOverlay          ? TEXT("render-overlay-transport-engine-policy-unverified")
                                        : TEXT("instance-parameters");
        if (Proof && !Proof->CaptureFresh(Instance, InstanceJson, Record, Error))
        {
            // This exact object was just made above; never leave a failed new
            // source build occupying the canonical path or retire old content.
            FAssetRegistryModule::AssetDeleted(Instance);
            Instance->ClearFlags(RF_Public | RF_Standalone);
            Instance->Rename(nullptr, GetTransientPackage(),
                REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
            Instance->MarkPendingKill();
            return nullptr;
        }
        if (!P4MarkOriginalAsset(Instance, Record, Error))
            return nullptr;
    }
    else if (!P4ImportPublication::IsImporting())
    {
        P4RecordBuiltCreatorInstance(Instance);
    }
    return Instance;
}

FString Json(UMaterialInstanceConstant* Instance, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Instance))
    {
        Error = TEXT("material-instance-needs-an-instance-on-the-editor-thread");
        return FString();
    }
    // A layered parameter is not one name, which is all this form can say.
    const auto Plain = [](const FMaterialParameterInfo& Info)
    { return Info.Association == EMaterialParameterAssociation::GlobalParameter; };
    TMap<FString, TSharedPtr<FJsonValue>> Scalars, Vectors, Textures, Switches, Masks;
    for (const FScalarParameterValue& Value : Instance->ScalarParameterValues)
    {
        if (!Plain(Value.ParameterInfo))
        {
            Error = TEXT("material-instance-cannot-say: a layer parameter");
            return FString();
        }
        Scalars.Add(Value.ParameterInfo.Name.ToString(), MakeShared<FJsonValueNumber>(double(Value.ParameterValue)));
    }
    for (const FVectorParameterValue& Value : Instance->VectorParameterValues)
    {
        if (!Plain(Value.ParameterInfo))
        {
            Error = TEXT("material-instance-cannot-say: a layer parameter");
            return FString();
        }
        const FLinearColor& Color = Value.ParameterValue;
        Vectors.Add(Value.ParameterInfo.Name.ToString(),
            MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(double(Color.R)),
                MakeShared<FJsonValueNumber>(double(Color.G)), MakeShared<FJsonValueNumber>(double(Color.B)),
                MakeShared<FJsonValueNumber>(double(Color.A))}));
    }
    for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
    {
        if (!Plain(Value.ParameterInfo))
        {
            Error = TEXT("material-instance-cannot-say: a layer parameter");
            return FString();
        }
        Textures.Add(Value.ParameterInfo.Name.ToString(),
            Value.ParameterValue
                ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueString>(Value.ParameterValue->GetPathName()))
                : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()));
    }
    for (const FStaticSwitchParameter& Value : Instance->GetStaticParameters().StaticSwitchParameters)
    {
        if (!Value.bOverride)
            continue;
        if (!Plain(Value.ParameterInfo))
        {
            Error = TEXT("material-instance-cannot-say: a layer parameter");
            return FString();
        }
        Switches.Add(Value.ParameterInfo.Name.ToString(), MakeShared<FJsonValueBoolean>(Value.Value));
    }
    for (const auto& Value : Instance->GetStaticParameters().StaticComponentMaskParameters)
    {
        if (!Value.bOverride)
            continue;
        if (!Plain(Value.ParameterInfo))
        {
            Error = TEXT("material-instance-cannot-say: a layer parameter");
            return FString();
        }
        TArray<TSharedPtr<FJsonValue>> Channels;
        for (bool Channel : {bool(Value.R), bool(Value.G), bool(Value.B), bool(Value.A)})
            Channels.Add(MakeShared<FJsonValueBoolean>(Channel));
        Masks.Add(Value.ParameterInfo.Name.ToString(), MakeShared<FJsonValueArray>(Channels));
    }
    const auto Sorted = [](const TMap<FString, TSharedPtr<FJsonValue>>& Values)
    {
        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        TArray<FString> Names;
        Values.GetKeys(Names);
        Names.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
        for (const FString& Name : Names)
            Out->SetField(Name, Values[Name]);
        return Out;
    };
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>(), Overrides = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("parent"), Instance->Parent ? Instance->Parent->GetPathName() : FString());
    Root->SetObjectField(TEXT("scalars"), Sorted(Scalars));
    Root->SetObjectField(TEXT("vectors"), Sorted(Vectors));
    Root->SetObjectField(TEXT("textures"), Sorted(Textures));
    Root->SetObjectField(TEXT("switches"), Sorted(Switches));
    if (Masks.Num())
        Root->SetObjectField(TEXT("masks"), Sorted(Masks));
    const FMaterialInstanceBasePropertyOverrides& Base = Instance->BasePropertyOverrides;
    if (Base.bOverride_BlendMode)
        Overrides->SetStringField(
            TEXT("blend_mode"), StaticEnum<EBlendMode>()->GetNameStringByValue(int64(Base.BlendMode.GetValue())));
    if (Base.bOverride_OpacityMaskClipValue)
        Overrides->SetNumberField(TEXT("clip"), double(Base.OpacityMaskClipValue));
    if (Base.bOverride_ShadingModel)
        Overrides->SetStringField(TEXT("shading_model"),
            StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(int64(Base.ShadingModel.GetValue())));
    if (Base.bOverride_TwoSided)
        Overrides->SetBoolField(TEXT("two_sided"), Base.TwoSided != 0);
    if (Base.bOverride_DitheredLODTransition)
        Overrides->SetBoolField(TEXT("dithered_lod"), Base.DitheredLODTransition != 0);
    if (Base.bOverride_CastDynamicShadowAsMasked)
        Overrides->SetBoolField(TEXT("shadow_as_masked"), Base.bCastDynamicShadowAsMasked != 0);
    Root->SetObjectField(TEXT("overrides"), Overrides);
    const FString Transport = Instance->GetOutermost()->GetMetaData()->GetValue(
        Instance, TEXT("P4MaterialInstanceTransportV2"));
    if (!Transport.IsEmpty())
    {
        TSharedPtr<FJsonObject> Stored;
        if (!P4ParseJson(Transport, Stored))
        {
            Error = TEXT("material-instance-transport-record-unreadable");
            return FString();
        }
        if (Stored->HasField(TEXT("game_overrides")))
            Root->SetField(TEXT("game_overrides"), Stored->TryGetField(TEXT("game_overrides")));
        if (Stored->HasField(TEXT("render_settings")))
            Overrides->SetField(TEXT("render_settings"), Stored->TryGetField(TEXT("render_settings")));
    }
    if (const auto* Settings = UP4MaterialInstanceGameSettings::Find(Instance))
    {
        const auto Records = Settings->Records();
        if (Records->Values.Num() || Root->HasField(TEXT("game_overrides")))
        {
            Root->SetObjectField(TEXT("game_overrides"), Records);
        }
    }
    FString Out = P4CondensedJson(Root);
    return Out;
}
} //namespace P4MaterialInstance

UMaterialInstanceConstant* P4BuildMaterialInstance(
    const FString& InstanceJson, const FString& Package, TArray<FString>& NotInParent, FString& Error)
{
    return P4MaterialInstance::Build(InstanceJson, Package, NotInParent, Error);
}
FString P4MaterialInstanceJson(UMaterialInstanceConstant* Instance, FString& Error)
{
    return P4MaterialInstance::Json(Instance, Error);
}

void P4MaterialInstanceSaveReferences(UMaterialInstanceConstant* Instance, TArray<UObject*>& Out)
{
    if (Instance->Parent)
        Out.AddUnique(Instance->Parent);
    for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
        if (Value.ParameterValue)
            Out.AddUnique(Value.ParameterValue);
}

static FString BuildMaterialInstanceReport(const FString& InstanceJson, const FString& Package, bool SaveToDisk,
    const TFunction<bool(FString&)>& SourceCurrent, const FP4RecoveredMaterialInstanceProof* Proof)
{
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    FString Error, Read;
    TArray<FString> NotInParent;
    bool SourceRefused = false;
    const auto CheckSource = [&]()
    {
        FString Why;
        // A recovered proof already includes SourceCurrent through its raw-read
        // guard. Check it once at each build/readback/save boundary: separately
        // checking the same whole family here repeated its filesystem census.
        if (Proof ? !Proof->Current(InstanceJson, Package, Why) : (SourceCurrent && !SourceCurrent(Why)))
        {
            SourceRefused = true;
            Error = TEXT("material-source-current-refused: ") + Why;
            return false;
        }
        return true;
    };
    Report->SetBoolField(TEXT("saved"), false);
    TSharedPtr<FJsonObject> Source;
    if (P4ParseJson(InstanceJson, Source))
    {
        if (Source->HasField(TEXT("game_overrides")))
        {
            Report->SetField(TEXT("game_overrides"), Source->TryGetField(TEXT("game_overrides")));
            Report->SetStringField(
                TEXT("game_override_policy"), TEXT("transport-only-game-engine-behavior-unverified"));
        }
        const TSharedPtr<FJsonObject>* Overrides = nullptr;
        if (Source->TryGetObjectField(TEXT("overrides"), Overrides) && (*Overrides)->HasField(TEXT("render_settings")))
        {
            Report->SetField(TEXT("render_settings_overlay"), (*Overrides)->TryGetField(TEXT("render_settings")));
            Report->SetStringField(
                TEXT("render_settings_policy"), TEXT("transport-only-game-engine-behavior-unverified"));
        }
    }
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(
        CheckSource() ? P4MaterialInstance::Build(InstanceJson, Package, NotInParent, Error, Proof) : nullptr);
    if (Instance.IsValid())
    {
        Report->SetStringField(TEXT("asset"), Instance->GetPathName());
        TArray<TSharedPtr<FJsonValue>> Missing;
        for (const FString& Item : NotInParent)
            Missing.Add(MakeShared<FJsonValueString>(Item));
        Report->SetArrayField(TEXT("not_in_parent"), Missing);
        Read = P4MaterialInstanceJson(Instance.Get(), Error);
        TSharedPtr<FJsonObject> Parsed;
        if (!Read.IsEmpty() && P4ParseJson(Read, Parsed))
            Report->SetObjectField(TEXT("instance"), Parsed);
    }
    if (Instance.IsValid() && Error.IsEmpty())
        CheckSource();
    if (Instance.IsValid() && Error.IsEmpty() && SaveToDisk)
    {
        FString Unused, Why;
        if (P4SaveAssetPackage(
                Instance.Get(),
                [&]()
                {
                    return CheckSource() &&
                        P4MaterialInstanceJson(Instance.Get(), Unused).Equals(Read, ESearchCase::CaseSensitive);
                },
                Why))
            Report->SetBoolField(TEXT("saved"), true);
        else if (Error.IsEmpty())
            Error = TEXT("material-instance-save-refused: ") + Why;
    }
    if (Instance.IsValid() && SourceRefused && !Report->GetBoolField(TEXT("saved")))
    {
        // Build refused all existing destinations before creating this instance.
        FAssetRegistryModule::AssetDeleted(Instance.Get());
        Instance->ClearFlags(RF_Public | RF_Standalone);
        Instance->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Instance->MarkPendingKill();
    }
    Report->SetBoolField(TEXT("valid"), Instance.IsValid() && Error.IsEmpty());
    Report->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Report);
    return Out;
}

FString P4BuildMaterialInstanceReport(const FString& InstanceJson, const FString& Package, bool SaveToDisk,
    const TFunction<bool(FString&)>& SourceCurrent)
{
    return BuildMaterialInstanceReport(InstanceJson, Package, SaveToDisk, SourceCurrent, nullptr);
}
FString P4BuildRecoveredMaterialInstanceReport(const FString& InstanceJson, const FString& Package, bool SaveToDisk,
    const FP4RecoveredMaterialInstanceProof& Proof, const TFunction<bool(FString&)>& SourceCurrent)
{
    return BuildMaterialInstanceReport(InstanceJson, Package, SaveToDisk, SourceCurrent, &Proof);
}

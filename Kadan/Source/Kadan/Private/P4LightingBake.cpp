#include "P4LightingBakeResult.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/Light.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "EngineUtils.h"
#include "Editor.h"
#include <cmath>

// The bake setup of a map put into a level for a lighting build, and taken out again. This is the
// Python kit's bake setup (custom_build_editor._bake_setup) in the plugin: the same lights with the
// same values under the same labels, so a level lit through either is lit the same.
namespace P4LightingBake
{
// What marks an actor as the bake's own: its label begins with one of these.
const TCHAR* const Labels[] = {TEXT("P4BakeSun"), TEXT("P4BakeSky"), TEXT("P4BakeVolume"), TEXT("P4BakeLight")};
const TCHAR* const SkyMaterial = TEXT("/Game/P4MapKit/Bake/MI_P4BakeSky");

bool IsBakeActor(const AActor* Actor)
{
    if (!Actor || Actor->IsPendingKill())
        return false;
    const FString Label = Actor->GetActorLabel();
    for (const TCHAR* Prefix : Labels)
        if (Label.StartsWith(Prefix, ESearchCase::CaseSensitive))
            return true;
    return false;
}

// A linear value as the byte of a color the way the kit writes it: the sRGB curve, rounded.
uint8 Byte(double Linear)
{
    double C = FMath::Clamp(Linear, 0.0, 1.0);
    C = C <= 0.0031308 ? C * 12.92 : 1.055 * std::pow(C, 1.0 / 2.4) - 0.055;
    return uint8(FMath::RoundHalfToEven(C * 255.0));
}

// A color of a setup: linear {R, G, B}, or the four bytes [B, G, R, A] a level of the game holds.
bool Color(const TSharedPtr<FJsonValue>& Value, FColor& Out)
{
    if (!Value.IsValid())
        return false;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (Value->TryGetObject(Object) && Object && (*Object)->HasField(TEXT("R")) && (*Object)->HasField(TEXT("G")) &&
        (*Object)->HasField(TEXT("B")))
    {
        Out = FColor(Byte((*Object)->GetNumberField(TEXT("R"))), Byte((*Object)->GetNumberField(TEXT("G"))),
            Byte((*Object)->GetNumberField(TEXT("B"))), 255);
        return true;
    }
    if (Value->TryGetArray(List) && List && List->Num() == 4)
    {
        Out = FColor(uint8((*List)[2]->AsNumber()), uint8((*List)[1]->AsNumber()), uint8((*List)[0]->AsNumber()),
            uint8((*List)[3]->AsNumber()));
        return true;
    }
    return false;
}

bool Three(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name, double Out[3])
{
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!Json.IsValid() || !Json->TryGetArrayField(Name, List) || !List || List->Num() != 3)
        return false;
    for (int32 At = 0; At < 3; ++At)
        if (!(*List)[At]->TryGetNumber(Out[At]))
            return false;
    return true;
}

// A rotation of a setup is [pitch, yaw, roll].
bool Turn(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name, FRotator& Out)
{
    double V[3];
    if (!Three(Json, Name, V))
        return false;
    Out = FRotator(float(V[0]), float(V[1]), float(V[2]));
    return true;
}

template <class T>
T* Place(UWorld* World, const FVector& At, const FRotator& Turned, const TCHAR* Label)
{
    FActorSpawnParameters How;
    How.OverrideLevel = World->PersistentLevel;
    How.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    T* Made = World->SpawnActor<T>(At, Turned, How);
    if (!Made)
        return nullptr;
    // A new actor is turned by what its class says on top of what it is given (a directional light's
    // class tilts it down). The setup's place and turn are the actor's, as the editor's own call for
    // placing an actor makes them.
    Made->SetActorLocationAndRotation(At, Turned);
    Made->SetActorLabel(Label);
    return Made;
}

// A name of the kit's Python as the property it stands for: light_source_angle is LightSourceAngle and
// cast_shadows is bCastShadows or CastShadows.
FProperty* Named(UStruct* Type, const FString& Key)
{
    const FString Plain = Key.Replace(TEXT("_"), TEXT(""));
    for (TFieldIterator<FProperty> It(Type); It; ++It)
    {
        const FString Name = It->GetName();
        if (Name.Equals(Plain, ESearchCase::IgnoreCase))
            return *It;
        if (Name.Len() > 1 && Name[0] == TEXT('b') && FChar::IsUpper(Name[1]) &&
            Name.Mid(1).Equals(Plain, ESearchCase::IgnoreCase))
            return *It;
    }
    return nullptr;
}

// One value of a setup put into a property: a number, true or false, a color, or the name of one of an
// enum's values (with or without the enum's own name before it).
bool Put(FProperty* Property, void* Container, const TSharedPtr<FJsonValue>& Value)
{
    if (!Property || !Value.IsValid())
        return false;
    void* At = Property->ContainerPtrToValuePtr<void>(Container);
    if (FBoolProperty* Flag = CastField<FBoolProperty>(Property))
    {
        bool Said = false;
        if (!Value->TryGetBool(Said))
        {
            double Number = 0.0;
            if (!Value->TryGetNumber(Number))
                return false;
            Said = Number != 0.0;
        }
        Flag->SetPropertyValue(At, Said);
        return true;
    }
    FString Word;
    const bool Text = Value->Type == EJson::String && Value->TryGetString(Word);
    UEnum* Enum = nullptr;
    FNumericProperty* Under = nullptr;
    if (FEnumProperty* Typed = CastField<FEnumProperty>(Property))
    {
        Enum = Typed->GetEnum();
        Under = Typed->GetUnderlyingProperty();
    }
    else if (FByteProperty* Small = CastField<FByteProperty>(Property))
    {
        Enum = Small->Enum;
        Under = Small;
    }
    if (Enum && Text)
    {
        int32 Sep = INDEX_NONE;
        if (Word.FindLastChar(TEXT(':'), Sep))
            Word = Word.Mid(Sep + 1);
        const int64 Number = Enum->GetValueByNameString(Word);
        if (Number == INDEX_NONE)
            return false;
        Under->SetIntPropertyValue(At, Number);
        return true;
    }
    if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
    {
        double Said = 0.0;
        if (Value->Type == EJson::Boolean || !Value->TryGetNumber(Said))
            return false;
        if (Number->IsFloatingPoint())
            Number->SetFloatingPointPropertyValue(At, Said);
        else
            Number->SetIntPropertyValue(At, int64(Said));
        return true;
    }
    if (FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        FColor Said;
        if (Struct->Struct == TBaseStructure<FColor>::Get() && Color(Value, Said))
        {
            *static_cast<FColor*>(At) = Said;
            return true;
        }
    }
    return false;
}

void Changed(ULightComponentBase* Light)
{
    Light->MarkRenderStateDirty();
    Light->MarkPackageDirty();
}

// The map's sun for the bake: all baked (static) when the map bakes its direct light, and otherwise a
// sun that only its bounce is baked of.
bool Sun(UWorld* World, const TSharedPtr<FJsonObject>& S, TArray<FString>& Placed, FString& Error)
{
    FRotator Turned;
    double Intensity = 0.0, Indirect = 0.0;
    FColor Tint;
    bool Direct = false;
    if (!S.IsValid() || !Turn(S, TEXT("rotation"), Turned) || !S->TryGetNumberField(TEXT("intensity"), Intensity) ||
        !Color(S->TryGetField(TEXT("color")), Tint) || !S->TryGetNumberField(TEXT("indirect_intensity"), Indirect))
    {
        Error = TEXT("lighting-bake-setup-has-no-whole-sun");
        return false;
    }
    S->TryGetBoolField(TEXT("direct"), Direct);
    ADirectionalLight* Made = Place<ADirectionalLight>(World, FVector(0.0f, 0.0f, 2000.0f), Turned, TEXT("P4BakeSun"));
    ULightComponent* Light = Made ? Made->GetLightComponent() : nullptr;
    if (!Light)
    {
        Error = TEXT("lighting-bake-sun-not-made");
        return false;
    }
    Light->SetMobility(Direct ? EComponentMobility::Static : EComponentMobility::Stationary);
    Light->Intensity = float(Intensity);
    Light->LightColor = Tint;
    Light->IndirectLightingIntensity = float(Indirect);
    Changed(Light);
    Placed.Add(Made->GetActorLabel());
    return true;
}

// The map's own sun as the game's level has it, where it is not all baked: its bounce goes into the
// lightmaps and its shadows into shadow maps, which the export hands to the game's sun. Its label
// sorts after the bake sun's, and of two such suns the later is given the shadow channel the game's reads.
bool GameSun(UWorld* World, const TSharedPtr<FJsonObject>& G, TArray<FString>& Placed, FString& Error)
{
    FRotator Turned;
    double Intensity = 0.0, Indirect = 1.0;
    if (!Turn(G, TEXT("rotation"), Turned) || !G->TryGetNumberField(TEXT("intensity"), Intensity))
    {
        Error = TEXT("lighting-bake-setup-has-no-whole-sun-of-the-game's");
        return false;
    }
    ADirectionalLight* Made = Place<ADirectionalLight>(
        World, FVector(0.0f, 0.0f, 2000.0f), Turned, TEXT("P4BakeSunOfTheGame"));
    ULightComponent* Light = Made ? Made->GetLightComponent() : nullptr;
    if (!Light)
    {
        Error = TEXT("lighting-bake-sun-not-made");
        return false;
    }
    Light->SetMobility(EComponentMobility::Stationary);
    Light->Intensity = float(Intensity);
    FColor Tint;
    const TSharedPtr<FJsonValue> Said = G->TryGetField(TEXT("color"));
    if (Said.IsValid() && !Said->IsNull() && Color(Said, Tint))
        Light->LightColor = Tint;
    G->TryGetNumberField(TEXT("indirect_intensity"), Indirect);
    Light->IndirectLightingIntensity = float(Indirect);
    Light->CastShadows = true;
    Light->CastStaticShadows = true;
    const TCHAR* const More[][2] = {{TEXT("source_angle"), TEXT("light_source_angle")},
        {TEXT("use_temperature"), TEXT("use_temperature")}, {TEXT("temperature"), TEXT("temperature")}};
    for (const auto& Pair : More)
    {
        const TSharedPtr<FJsonValue> Value = G->TryGetField(Pair[0]);
        if (Value.IsValid() && !Value->IsNull() && !Put(Named(Light->GetClass(), Pair[1]), Light, Value))
        {
            Error = FString(TEXT("lighting-bake-sun-value-not-set: ")) + Pair[0];
            return false;
        }
    }
    Changed(Light);
    Placed.Add(Made->GetActorLabel());
    return true;
}

// Copies of the level's static lights: baked into the lightmaps, then taken out with the bake's others.
bool StaticLights(UWorld* World, const TArray<TSharedPtr<FJsonValue>>& Lights, TArray<FString>& Placed,
    TArray<FString>& NotSet, FString& Error)
{
    for (const TSharedPtr<FJsonValue>& Entry : Lights)
    {
        const TSharedPtr<FJsonObject>* Item = nullptr;
        if (!Entry.IsValid() || !Entry->TryGetObject(Item) || !Item)
        {
            Error = TEXT("lighting-bake-light-malformed");
            return false;
        }
        const FString Kind = (*Item)->GetStringField(TEXT("kind"));
        UClass* Class = Kind == TEXT("PointLight") ? APointLight::StaticClass()
            : Kind == TEXT("SpotLight")            ? ASpotLight::StaticClass()
            : Kind == TEXT("RectLight")            ? ARectLight::StaticClass()
            : Kind == TEXT("DirectionalLight")     ? ADirectionalLight::StaticClass()
                                                   : nullptr;
        double Where[3];
        FRotator Turned;
        if (!Class || !Three(*Item, TEXT("location"), Where) || !Turn(*Item, TEXT("rotation"), Turned))
        {
            Error = TEXT("lighting-bake-light-malformed: ") + Kind;
            return false;
        }
        FActorSpawnParameters How;
        How.OverrideLevel = World->PersistentLevel;
        How.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ALight* Made = Cast<ALight>(World->SpawnActor(Class, nullptr, nullptr, How));
        ULightComponent* Light = Made ? Made->GetLightComponent() : nullptr;
        if (!Light)
        {
            Error = TEXT("lighting-bake-light-not-made: ") + Kind;
            return false;
        }
        Made->SetActorLocationAndRotation(FVector(float(Where[0]), float(Where[1]), float(Where[2])), Turned);
        Made->SetActorLabel(TEXT("P4BakeLight"));
        Light->SetMobility(EComponentMobility::Static);
        const TArray<TSharedPtr<FJsonValue>>* Bytes = nullptr;
        if ((*Item)->TryGetArrayField(TEXT("color"), Bytes) && Bytes && Bytes->Num() == 4)
            Light->LightColor = FColor(uint8((*Bytes)[0]->AsNumber()), uint8((*Bytes)[1]->AsNumber()),
                uint8((*Bytes)[2]->AsNumber()), uint8((*Bytes)[3]->AsNumber()));
        FString Units;
        if ((*Item)->TryGetStringField(TEXT("units"), Units))
        {
            ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light);
            const UEnum* Enum = StaticEnum<ELightUnits>();
            const int64 Value = Enum
                ? Enum->GetValueByNameString(Units.Equals(TEXT("UNITLESS"), ESearchCase::IgnoreCase) ? TEXT("Unitless")
                          : Units.Equals(TEXT("CANDELAS"), ESearchCase::IgnoreCase)                  ? TEXT("Candelas")
                          : Units.Equals(TEXT("LUMENS"), ESearchCase::IgnoreCase)                    ? TEXT("Lumens")
                                                                                                     : *Units)
                : INDEX_NONE;
            if (!Local || Value == INDEX_NONE)
            {
                Error = TEXT("lighting-bake-light-units-not-set: ") + Units;
                return false;
            }
            Local->IntensityUnits = ELightUnits(Value);
        }
        const TSharedPtr<FJsonObject>* Values = nullptr;
        if ((*Item)->TryGetObjectField(TEXT("values"), Values) && Values)
            for (const auto& Pair : (*Values)->Values)
                if (!Put(Named(Light->GetClass(), Pair.Key), Light, Pair.Value))
                    NotSet.Add(Kind + TEXT(".") + Pair.Key);
        Changed(Light);
        Placed.Add(Made->GetActorLabel());
    }
    return true;
}

// A copy of the map's sky sphere for the sky light to capture, with the sky actor's values on an
// instance of its material, as the game sets them while it runs. The instance is an asset of the
// project, because the build opens the level in another editor. Returns the instance's package.
bool SkySphere(
    UWorld* World, const TSharedPtr<FJsonObject>& Capture, TArray<FString>& Placed, FString& Material, FString& Error)
{
    const FString MeshPath = Capture->GetStringField(TEXT("mesh"));
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(
        nullptr, *(MeshPath.Contains(TEXT(".")) ? MeshPath : P4ObjectPathOf(MeshPath)));
    double Where[3], Size[3];
    FRotator Turned = FRotator::ZeroRotator;
    if (!Mesh)
    {
        Error = TEXT("lighting-bake-sky-mesh-not-in-the-project: ") + MeshPath;
        return false;
    }
    if (!Three(Capture, TEXT("location"), Where) || !Three(Capture, TEXT("scale"), Size))
    {
        Error = TEXT("lighting-bake-sky-capture-malformed");
        return false;
    }
    Turn(Capture, TEXT("rotation"), Turned);
    AStaticMeshActor* Sphere = Place<AStaticMeshActor>(
        World, FVector(float(Where[0]), float(Where[1]), float(Where[2])), Turned, TEXT("P4BakeSkySphere"));
    UStaticMeshComponent* Part = Sphere ? Sphere->GetStaticMeshComponent() : nullptr;
    if (!Part)
    {
        Error = TEXT("lighting-bake-sky-sphere-not-made");
        return false;
    }
    Part->SetStaticMesh(Mesh);
    Sphere->SetActorScale3D(FVector(float(Size[0]), float(Size[1]), float(Size[2])));
    Part->SetMobility(EComponentMobility::Static);
    Part->SetCastShadow(false);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UMaterialInterface* Parent = Part->GetMaterial(0);
    if (!Parent)
    {
        Error = TEXT("lighting-bake-sky-mesh-has-no-material: ") + MeshPath;
        return false;
    }
    const FString Short = FPackageName::GetLongPackageAssetName(SkyMaterial);
    UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(
        nullptr, *(FString(SkyMaterial) + TEXT(".") + Short), nullptr, LOAD_NoWarn | LOAD_Quiet);
    const bool New = !Instance;
    if (New)
        Instance = NewObject<UMaterialInstanceConstant>(
            CreatePackage(SkyMaterial), FName(*Short), RF_Public | RF_Standalone);
    if (!Instance)
    {
        Error = TEXT("lighting-bake-sky-material-not-made");
        return false;
    }
    Instance->ClearParameterValuesEditorOnly();
    Instance->SetParentEditorOnly(Parent);
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (Capture->TryGetObjectField(TEXT("params"), Params) && Params)
        for (const auto& Pair : (*Params)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
            double Number = 0.0;
            if (Pair.Value->TryGetArray(List) && List && (List->Num() == 3 || List->Num() == 4))
                Instance->SetVectorParameterValueEditorOnly(FName(*Pair.Key),
                    FLinearColor(float((*List)[0]->AsNumber()), float((*List)[1]->AsNumber()),
                        float((*List)[2]->AsNumber()), List->Num() == 4 ? float((*List)[3]->AsNumber()) : 1.0f));
            else if (Pair.Value->TryGetNumber(Number))
                Instance->SetScalarParameterValueEditorOnly(FName(*Pair.Key), float(Number));
            else
            {
                Error = TEXT("lighting-bake-sky-value-not-a-number-or-a-color: ") + Pair.Key;
                return false;
            }
        }
    Instance->PostEditChange();
    Instance->MarkPackageDirty();
    if (New)
        FAssetRegistryModule::AssetCreated(Instance);
    const FString File = FPackageName::LongPackageNameToFilename(SkyMaterial, FPackageName::GetAssetPackageExtension());
    if (!UPackage::SavePackage(Instance->GetOutermost(), Instance, RF_Public | RF_Standalone, *File, GError, nullptr,
            false, true, SAVE_NoError))
    {
        Error = TEXT("lighting-bake-sky-material-not-saved");
        return false;
    }
    Part->SetMaterial(0, Instance);
    Material = SkyMaterial;
    Placed.Add(Sphere->GetActorLabel());
    return true;
}

// The map's sky light for the bake: a capture of the map's sky sphere where the map captures its sky,
// and otherwise a cubemap, the first of the game's own, the setup's and the engine's daylight that the
// project has. SkySource says which, so that a caller can tell a stand-in.
bool Sky(UWorld* World, const TSharedPtr<FJsonObject>& K, TArray<FString>& Placed, FString& SkySource,
    FString& Material, FString& Error)
{
    double Where[3] = {0.0, 0.0, 2000.0}, Intensity = 0.0, Indirect = 0.0;
    if (!K.IsValid() || !K->TryGetNumberField(TEXT("intensity"), Intensity) ||
        !K->TryGetNumberField(TEXT("indirect_intensity"), Indirect))
    {
        Error = TEXT("lighting-bake-setup-has-no-whole-sky-light");
        return false;
    }
    Three(K, TEXT("location"), Where);
    ASkyLight* Made = Place<ASkyLight>(
        World, FVector(float(Where[0]), float(Where[1]), float(Where[2])), FRotator::ZeroRotator, TEXT("P4BakeSky"));
    USkyLightComponent* Light = Made ? Made->GetLightComponent() : nullptr;
    if (!Light)
    {
        Error = TEXT("lighting-bake-sky-light-not-made");
        return false;
    }
    Light->SetMobility(EComponentMobility::Stationary);
    const TSharedPtr<FJsonObject>* Capture = nullptr;
    if (K->TryGetObjectField(TEXT("capture"), Capture) && Capture && (*Capture)->Values.Num())
    {
        if (!SkySphere(World, *Capture, Placed, Material, Error))
            return false;
        Light->SourceType = SLS_CapturedScene;
        SkySource = TEXT("captured");
    }
    else
    {
        UTextureCube* Cube = nullptr;
        for (const FString& Path :
            {K->HasTypedField<EJson::String>(TEXT("game_cubemap")) ? K->GetStringField(TEXT("game_cubemap"))
                                                                   : FString(),
                K->HasTypedField<EJson::String>(TEXT("cubemap")) ? K->GetStringField(TEXT("cubemap")) : FString(),
                FString(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap"))})
        {
            if (Path.IsEmpty())
                continue;
            Cube = LoadObject<UTextureCube>(
                nullptr, *(Path.Contains(TEXT(".")) ? Path : P4ObjectPathOf(Path)), nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (Cube)
                break;
        }
        if (!Cube)
        {
            Error = TEXT("lighting-bake-sky-light-has-no-cubemap");
            return false;
        }
        Light->SourceType = SLS_SpecifiedCubemap;
        Light->Cubemap = Cube;
        SkySource = Cube->GetOutermost()->GetName();
    }
    Light->Intensity = float(Intensity);
    Light->IndirectLightingIntensity = float(Indirect);
    bool Flag = false;
    double Far = 0.0;
    if (K->TryGetBoolField(TEXT("lower_hemisphere_black"), Flag))
        Light->bLowerHemisphereIsBlack = Flag;
    if (K->TryGetNumberField(TEXT("distance_threshold"), Far))
        Light->SkyDistanceThreshold = float(Far);
    if (K->TryGetBoolField(TEXT("capture_emissive_only"), Flag))
        Light->bCaptureEmissiveOnly = Flag;
    Changed(Light);
    Placed.Add(Made->GetActorLabel());
    return true;
}

FP4LightingBakeResult Setup(UWorld* World, const FString& SetupJson)
{
    FP4LightingBakeResult Result;
    const auto Refuse = [&](EP4LightingBakeError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread() || !GEditor)
        return Refuse(EP4LightingBakeError::EditorThreadRequired, TEXT("lighting-bake-requires-editor-thread"));
    if (!IsValid(World) || !World->PersistentLevel)
        return Refuse(EP4LightingBakeError::LevelRequired, TEXT("lighting-bake-needs-a-level"));
    TSharedPtr<FJsonObject> Bake;
    if (!P4ParseJson(SetupJson, Bake))
        return Refuse(EP4LightingBakeError::SetupUnreadable, TEXT("lighting-bake-setup-unreadable"));
    for (TActorIterator<AActor> It(World); It; ++It)
        if (IsBakeActor(*It))
            return Refuse(EP4LightingBakeError::SetupAlreadyPresent,
                TEXT("lighting-bake-level-has-a-setup-already: ") + It->GetActorLabel());
    TArray<FString> Placed, NotSet;
    FString Error, SkySource, Material;
    const auto Fail = [&](EP4LightingBakeError Code, const FString& Why)
    {
        // A setup that was put in half is taken out again.
        for (TActorIterator<AActor> It(World); It; ++It)
            if (IsBakeActor(*It))
                World->EditorDestroyActor(*It, false);
        return Refuse(Code, Why);
    };
    const TSharedPtr<FJsonObject>* Part = nullptr;
    if (!Bake->TryGetObjectField(TEXT("sun"), Part) || !Part || !Sun(World, *Part, Placed, Error))
        return Fail(EP4LightingBakeError::SunSetupFailed,
            Error.IsEmpty() ? FString(TEXT("lighting-bake-setup-has-no-whole-sun")) : Error);
    if (Bake->TryGetObjectField(TEXT("game_sun"), Part) && Part && (*Part)->Values.Num() &&
        !GameSun(World, *Part, Placed, Error))
        return Fail(EP4LightingBakeError::GameSunSetupFailed, Error);
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (Bake->TryGetArrayField(TEXT("lights"), List) && List && !StaticLights(World, *List, Placed, NotSet, Error))
        return Fail(EP4LightingBakeError::StaticLightSetupFailed, Error);
    if (Bake->TryGetArrayField(TEXT("importance_volumes"), List) && List)
        for (const TSharedPtr<FJsonValue>& Entry : *List)
        {
            const TSharedPtr<FJsonObject>* Volume = nullptr;
            double Centre[3], Size[3];
            if (!Entry->TryGetObject(Volume) || !Volume || !Three(*Volume, TEXT("centre"), Centre) ||
                !Three(*Volume, TEXT("size"), Size))
                return Fail(
                    EP4LightingBakeError::ImportanceVolumeMalformed, TEXT("lighting-bake-importance-volume-malformed"));
            ALightmassImportanceVolume* Made = Place<ALightmassImportanceVolume>(World,
                FVector(float(Centre[0]), float(Centre[1]), float(Centre[2])), FRotator::ZeroRotator,
                TEXT("P4BakeVolume"));
            if (!Made)
                return Fail(EP4LightingBakeError::ImportanceVolumeCreationFailed,
                    TEXT("lighting-bake-importance-volume-not-made"));
            // The brush the engine gives a new volume is a cube of 200.
            Made->SetActorScale3D(FVector(float(Size[0] / 200.0), float(Size[1] / 200.0), float(Size[2] / 200.0)));
            Placed.Add(Made->GetActorLabel());
        }
    if (!Bake->TryGetObjectField(TEXT("sky"), Part) || !Part || !Sky(World, *Part, Placed, SkySource, Material, Error))
        return Fail(EP4LightingBakeError::SkySetupFailed,
            Error.IsEmpty() ? FString(TEXT("lighting-bake-setup-has-no-whole-sky-light")) : Error);
    AWorldSettings* Settings = World->GetWorldSettings();
    if (!Settings)
        return Fail(EP4LightingBakeError::WorldSettingsMissing, TEXT("lighting-bake-level-has-no-world-settings"));
    Settings->Modify();
    Settings->bForceNoPrecomputedLighting = false;
    // From the engine's own values each time: a value an earlier setup gave does not stay when this one does not give it.
    FLightmassWorldInfoSettings Lightmass;
    if (Bake->TryGetObjectField(TEXT("lightmass"), Part) && Part)
        for (const auto& Pair : (*Part)->Values)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            FColor Unused;
            // A value that is an object and not a color is not one of these settings.
            if (Pair.Value->TryGetObject(Inner) && !Color(Pair.Value, Unused))
                continue;
            if (!Put(Named(FLightmassWorldInfoSettings::StaticStruct(), Pair.Key), &Lightmass, Pair.Value))
                NotSet.Add(TEXT("lightmass.") + Pair.Key);
        }
    Settings->LightmassSettings = Lightmass;
    Settings->MarkPackageDirty();
    Placed.Sort();
    Result.Placed = MoveTemp(Placed);
    Result.ValuesNotSet = MoveTemp(NotSet);
    Result.SkySource = SkySource;
    Result.SkyMaterial = Material;
    Result.SetupCompleted = true;
    return Result;
}

// After the build: the setup's lights are taken out of the level, and so is what the build made for the
// level as a whole (its volumetric lightmap and its light volume). A level that is loaded into a map
// with a volumetric lightmap of its own gives the whole scene that one, in place of the map's: everything
// that moves in the map would be lit by a volume built without the map. What the build made for each
// mesh and each light stays, by the engine's own call for keeping part of a level's lighting data.
FP4LightingBakeResult Clear(UWorld* World)
{
    FP4LightingBakeResult Result;
    const auto Refuse = [&](EP4LightingBakeError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread() || !GEditor)
        return Refuse(EP4LightingBakeError::EditorThreadRequired, TEXT("lighting-bake-requires-editor-thread"));
    if (!IsValid(World) || !World->PersistentLevel)
        return Refuse(EP4LightingBakeError::LevelRequired, TEXT("lighting-bake-needs-a-level"));
    ULevel* Level = World->PersistentLevel;
    TSet<FGuid> Keep;
    TArray<AActor*> Found;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (IsBakeActor(*It))
            Found.Add(*It);
        TInlineComponentArray<UActorComponent*> Parts(*It);
        for (UActorComponent* Part : Parts)
        {
            if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Part))
                for (const FStaticMeshComponentLODInfo& Lod : Mesh->LODData)
                    Keep.Add(Lod.MapBuildDataId);
            if (const ULightComponentBase* Light = Cast<ULightComponentBase>(Part))
                Keep.Add(Light->LightGuid);
        }
    }
    TArray<FString> Taken, Left;
    for (AActor* Actor : Found)
    {
        const FString Label = Actor->GetActorLabel();
        (World->EditorDestroyActor(Actor, false) ? Taken : Left).Add(Label);
    }
    Result.TakenOut = Taken;
    Result.Left = Left;
    Result.RemovalAttempted = true;
    if (Left.Num())
        return Refuse(EP4LightingBakeError::ActorRemovalFailed,
            TEXT("lighting-bake-a-light-of-the-setup-could-not-be-taken-out"));
    UMapBuildDataRegistry* Registry = Level->MapBuildData;
    Result.LightingDataKnown = true;
    Result.LightingData = Registry != nullptr;
    if (Registry)
    {
        const bool Had = Registry->GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId) ||
            Registry->GetLevelPrecomputedLightVolumeBuildData(Level->LevelBuildDataId);
        // The level's own id is not among those kept, so what is held under it goes.
        Keep.Remove(Level->LevelBuildDataId);
        // The registry's own recreation context ends before its caller can rebuild retained
        // lightmap clusters. Keep components detached until both operations have completed.
        // Otherwise their new render proxies can observe the reset cluster pointers.
        {
            FGlobalComponentRecreateRenderStateContext Recreate;
            Registry->InvalidateStaticLighting(World, false, &Keep);
            Registry->SetupLightmapResourceClusters();
            for (ULevel* Loaded : World->GetLevels())
                if (Loaded)
                    Loaded->InitializeRenderingResources();
        }
        Registry->MarkPackageDirty();
        int32 Meshes = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            TInlineComponentArray<UStaticMeshComponent*> Parts(*It);
            for (const UStaticMeshComponent* Mesh : Parts)
                if (Mesh->LODData.Num() && Registry->GetMeshBuildData(Mesh->LODData[0].MapBuildDataId))
                    ++Meshes;
        }
        if (Registry->GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId) ||
            Registry->GetLevelPrecomputedLightVolumeBuildData(Level->LevelBuildDataId))
            return Refuse(EP4LightingBakeError::VolumeLightingRemovalFailed,
                TEXT("lighting-bake-volume-lighting-could-not-be-taken-out"));
        Result.BuiltLightingKnown = true;
        Result.VolumeLightingTakenOut = Had;
        Result.MeshesWithBuiltLighting = Meshes;
    }
    return Result;
}
} //namespace P4LightingBake

FP4LightingBakeResult P4SetupLightingBakeFromJsonNative(UWorld* World, const FString& Setup)
{
    return P4LightingBake::Setup(World, Setup);
}
FP4LightingBakeResult P4ClearLightingBakeNative(UWorld* World)
{
    return P4LightingBake::Clear(World);
}

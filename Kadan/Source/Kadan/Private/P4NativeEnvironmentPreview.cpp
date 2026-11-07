// The game's environment controller in the editor: what it applies to its own components from its scene when the game
// arrives in a level (P4NativeEnvironmentRecord.inl, read from the game's code), applied to an opened map's controllers.
// Every saved value it changes is remembered first (P4RememberNativePreviewValue), so a save or an export sees the
// map's own values.
#include "P4NativeConstructionPreview.h"
#include "P4Common.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/UnrealType.h"

namespace
{
const TSharedPtr<FJsonObject>& Record()
{
    static TSharedPtr<FJsonObject> Read;
    static bool bRead = false;
    if (bRead)
        return Read;
    bRead = true;
    static const char* const Source =
#include "P4NativeEnvironmentRecord.inl"
        ;
    if (!P4ParseJson(UTF8_TO_TCHAR(Source), Read))
    {
        UE_LOG(LogTemp, Error, TEXT("Kadan's native environment record is unreadable"));
        Read.Reset();
    }
    return Read;
}

// A property at a dotted path from an object: object properties are followed, struct properties stepped into.
// Owner and Top are the object the last step belongs to and that object's own property holding it.
struct FFound
{
    UObject* Owner = nullptr;
    FProperty* Top = nullptr;
    FProperty* Property = nullptr;
    void* Container = nullptr;
    void* At = nullptr;
};

bool Find(UObject* Root, const FString& Path, FFound& Out)
{
    TArray<FString> Parts;
    Path.ParseIntoArray(Parts, TEXT("."));
    UObject* Object = Root;
    UStruct* Struct = Root ? Root->GetClass() : nullptr;
    void* Container = Root;
    for (int32 Index = 0; Struct && Index < Parts.Num(); ++Index)
    {
        FProperty* Property = FindFProperty<FProperty>(Struct, *Parts[Index]);
        if (!Property)
            return false;
        void* At = Property->ContainerPtrToValuePtr<void>(Container);
        if (Container == Object)
        {
            Out.Owner = Object;
            Out.Top = Property;
        }
        if (Index + 1 == Parts.Num())
        {
            Out.Property = Property;
            Out.Container = Container;
            Out.At = At;
            return true;
        }
        if (auto* Reference = CastField<FObjectPropertyBase>(Property))
        {
            Object = Reference->GetObjectPropertyValue(At);
            Struct = Object ? Object->GetClass() : nullptr;
            Container = Object;
        }
        else if (auto* Inner = CastField<FStructProperty>(Property))
        {
            Struct = Inner->Struct;
            Container = At;
        }
        else
            return false;
    }
    return false;
}

struct FValue
{
    enum class EKind : uint8 { None, Number, Color, Flag, Object, Whole };
    EKind Kind = EKind::None;
    double Number = 0;
    FLinearColor Color = FLinearColor::Black;
    bool Flag = false;
    UObject* Object = nullptr;
    int64 Whole = 0;
};

// The scene's value at the time: an FEnvParamFloat or FEnvParamLinearColor is its curve when the curve is on.
FValue Read(const FFound& Found, float Time)
{
    FValue Out;
    if (auto* Struct = CastField<FStructProperty>(Found.Property))
    {
        const FName Name = Struct->Struct->GetFName();
        if (Name == TEXT("EnvParamFloat") || Name == TEXT("EnvParamLinearColor"))
        {
            FProperty* Value = FindFProperty<FProperty>(Struct->Struct, TEXT("Value"));
            FBoolProperty* On = FindFProperty<FBoolProperty>(Struct->Struct, TEXT("bEnableCurve"));
            FObjectPropertyBase* Curve = FindFProperty<FObjectPropertyBase>(Struct->Struct, TEXT("Curve"));
            UObject* CurveObject = Curve ? Curve->GetObjectPropertyValue_InContainer(Found.At) : nullptr;
            const bool UseCurve = On && On->GetPropertyValue_InContainer(Found.At) && CurveObject;
            if (Name == TEXT("EnvParamFloat"))
            {
                Out.Kind = FValue::EKind::Number;
                if (UseCurve && Cast<UCurveFloat>(CurveObject))
                    Out.Number = Cast<UCurveFloat>(CurveObject)->GetFloatValue(Time);
                else if (auto* Number = CastField<FFloatProperty>(Value))
                    Out.Number = Number->GetPropertyValue_InContainer(Found.At);
            }
            else
            {
                Out.Kind = FValue::EKind::Color;
                if (UseCurve && Cast<UCurveLinearColor>(CurveObject))
                    Out.Color = Cast<UCurveLinearColor>(CurveObject)->GetLinearColorValue(Time);
                else if (Value)
                    Out.Color = *Value->ContainerPtrToValuePtr<FLinearColor>(Found.At);
            }
            return Out;
        }
        if (Struct->Struct == TBaseStructure<FLinearColor>::Get())
        {
            Out.Kind = FValue::EKind::Color;
            Out.Color = *static_cast<const FLinearColor*>(Found.At);
        }
        return Out;
    }
    if (auto* Number = CastField<FNumericProperty>(Found.Property))
    {
        if (Number->IsFloatingPoint())
        {
            Out.Kind = FValue::EKind::Number;
            Out.Number = Number->GetFloatingPointPropertyValue(Found.At);
        }
        else
        {
            Out.Kind = FValue::EKind::Whole;
            Out.Whole = Number->GetSignedIntPropertyValue(Found.At);
        }
    }
    else if (auto* Flag = CastField<FBoolProperty>(Found.Property))
    {
        Out.Kind = FValue::EKind::Flag;
        Out.Flag = Flag->GetPropertyValue(Found.At);
    }
    else if (auto* Enum = CastField<FEnumProperty>(Found.Property))
    {
        Out.Kind = FValue::EKind::Whole;
        Out.Whole = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Found.At);
    }
    else if (auto* Reference = CastField<FObjectPropertyBase>(Found.Property))
    {
        Out.Kind = FValue::EKind::Object;
        Out.Object = Reference->GetObjectPropertyValue(Found.At);
    }
    return Out;
}

bool Write(const FFound& Found, const FValue& Value, bool SRGB)
{
    P4RememberNativePreviewValue(Found.Owner, Found.Top);
    FProperty* Property = Found.Property;
    if (auto* Struct = CastField<FStructProperty>(Property))
    {
        if (Value.Kind != FValue::EKind::Color)
            return false;
        if (Struct->Struct == TBaseStructure<FLinearColor>::Get())
            *static_cast<FLinearColor*>(Found.At) = Value.Color;
        else if (Struct->Struct == TBaseStructure<FColor>::Get())
            *static_cast<FColor*>(Found.At) = Value.Color.ToFColor(SRGB);
        else
            return false;
    }
    else if (auto* Number = CastField<FNumericProperty>(Property))
    {
        if (Number->IsFloatingPoint() && Value.Kind == FValue::EKind::Number)
            Number->SetFloatingPointPropertyValue(Found.At, Value.Number);
        else if (Number->IsInteger() && Value.Kind == FValue::EKind::Whole)
            Number->SetIntPropertyValue(Found.At, Value.Whole);
        else
            return false;
    }
    else if (auto* Flag = CastField<FBoolProperty>(Property))
    {
        if (Value.Kind != FValue::EKind::Flag)
            return false;
        Flag->SetPropertyValue(Found.At, Value.Flag);
    }
    else if (auto* Enum = CastField<FEnumProperty>(Property))
    {
        if (Value.Kind != FValue::EKind::Whole)
            return false;
        Enum->GetUnderlyingProperty()->SetIntPropertyValue(Found.At, Value.Whole);
    }
    else if (auto* Reference = CastField<FObjectPropertyBase>(Property))
    {
        if (Value.Kind != FValue::EKind::Object || (Value.Object && !Value.Object->IsA(Reference->PropertyClass)))
            return false;
        Reference->SetObjectPropertyValue(Found.At, Value.Object);
    }
    else
        return false;
    // A post-process setting takes effect with its override flag.
    if (Found.Container != Found.Owner)
        if (FBoolProperty* Override = FindFProperty<FBoolProperty>(
                Found.Top && CastField<FStructProperty>(Found.Top) ? CastField<FStructProperty>(Found.Top)->Struct : nullptr,
                *(TEXT("bOverride_") + Property->GetName())))
            Override->SetPropertyValue_InContainer(Found.Container, true);
    if (auto* Component = Cast<UActorComponent>(Found.Owner))
        Component->MarkRenderStateDirty();
    return true;
}

FString Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    FString Out;
    if (Object.IsValid())
        Object->TryGetStringField(Name, Out);
    return Out;
}

int32 ApplyOne(AActor* Actor, const TSharedPtr<FJsonObject>& Json)
{
    UWorld* World = Actor->GetWorld();
    FFound SceneProperty;
    if (!Find(Actor, Field(Json, TEXT("scene")), SceneProperty) || !CastField<FObjectPropertyBase>(SceneProperty.Property))
        return 0;
    UObject* Scene = CastField<FObjectPropertyBase>(SceneProperty.Property)->GetObjectPropertyValue(SceneProperty.At);
    if (!Scene)
        return 0;
    double Time = 0.5;
    Json->TryGetNumberField(TEXT("time"), Time);
    const TSharedPtr<FJsonObject>* Override = nullptr;
    FFound When, OverrideTime;
    if (Json->TryGetObjectField(TEXT("time_override"), Override) && Find(Actor, Field(*Override, TEXT("when")), When) &&
        Read(When, 0).Flag && Find(Actor, Field(*Override, TEXT("time")), OverrideTime))
        Time = Read(OverrideTime, 0).Number;
    const auto SceneValue = [&](const FString& Path, FValue& Out)
    {
        FFound Found;
        if (!Find(Scene, Path, Found))
            return false;
        Out = Read(Found, float(Time));
        return Out.Kind != FValue::EKind::None;
    };
    const auto ActorValue = [&](const FString& Path, FValue& Out)
    {
        FFound Found;
        if (!Find(Actor, Path, Found))
            return false;
        Out = Read(Found, float(Time));
        return Out.Kind != FValue::EKind::None;
    };
    int32 Applied = 0;
    const TArray<TSharedPtr<FJsonValue>>* Statics = nullptr;
    if (Json->TryGetArrayField(TEXT("static"), Statics))
        for (const auto& Name : *Statics)
        {
            FFound Found;
            if (Find(Actor, Name->AsString() + TEXT(".Mobility"), Found))
            {
                FValue Static;
                Static.Kind = FValue::EKind::Whole;
                Static.Whole = int64(EComponentMobility::Static);
                Applied += Write(Found, Static, false);
            }
        }
    const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
    FFound CollectionProperty;
    UMaterialParameterCollectionInstance* Collection = nullptr;
    if (Find(Actor, Field(Json, TEXT("collection")), CollectionProperty) &&
        CastField<FObjectPropertyBase>(CollectionProperty.Property))
        Collection = World->GetParameterCollectionInstance(Cast<UMaterialParameterCollection>(
            CastField<FObjectPropertyBase>(CollectionProperty.Property)->GetObjectPropertyValue(CollectionProperty.At)));
    if (Json->TryGetArrayField(TEXT("steps"), Steps))
        for (const auto& Item : *Steps)
        {
            const TSharedPtr<FJsonObject> Step = Item->AsObject();
            FValue Value;
            bool KeepNone = false;
            if (!Step.IsValid() || !SceneValue(Field(Step, TEXT("value")), Value) ||
                (Step->TryGetBoolField(TEXT("keep_none"), KeepNone) && KeepNone && Value.Kind == FValue::EKind::Object &&
                    !Value.Object))
                continue;
            // A value the scene turns off with a flag of its own is the step's fixed one.
            FValue Enabled;
            double Otherwise = 0;
            if (Value.Kind == FValue::EKind::Number && SceneValue(Field(Step, TEXT("when")), Enabled) && !Enabled.Flag &&
                Step->TryGetNumberField(TEXT("otherwise"), Otherwise))
                Value.Number = Otherwise;
            double Min = 0;
            if (Value.Kind == FValue::EKind::Number && Step->TryGetNumberField(TEXT("min"), Min))
                Value.Number = FMath::Max(Value.Number, Min);
            FValue Scale;
            if (Value.Kind == FValue::EKind::Number && ActorValue(Field(Step, TEXT("scale")), Scale))
                Value.Number *= Scale.Number;
            bool SRGB = true;
            FValue SRGBValue;
            if (!Step->TryGetBoolField(TEXT("srgb"), SRGB) && SceneValue(Field(Step, TEXT("srgb")), SRGBValue))
                SRGB = SRGBValue.Flag;
            const FString Scalar = Field(Step, TEXT("scalar"));
            if (!Scalar.IsEmpty())
            {
                if (Collection && Value.Kind == FValue::EKind::Number)
                    Applied += Collection->SetScalarParameterValue(FName(*Scalar), float(Value.Number));
                continue;
            }
            FFound Target;
            if (Find(Actor, Field(Step, TEXT("set")), Target))
                Applied += Write(Target, Value, SRGB);
        }
    // The sun's direction: (pitch, yaw) = (altitude + offset + pitch_add, azimuth + offset + yaw_add).
    const TSharedPtr<FJsonObject>* Sun = nullptr;
    FFound LightProperty;
    FValue Altitude, Azimuth;
    if (Json->TryGetObjectField(TEXT("sun"), Sun) && Find(Actor, Field(*Sun, TEXT("light")), LightProperty) &&
        CastField<FObjectPropertyBase>(LightProperty.Property) && SceneValue(Field(*Sun, TEXT("altitude")), Altitude) &&
        SceneValue(Field(*Sun, TEXT("azimuth")), Azimuth))
        if (auto* Light = Cast<USceneComponent>(
                CastField<FObjectPropertyBase>(LightProperty.Property)->GetObjectPropertyValue(LightProperty.At)))
        {
            double Pitch = Altitude.Number, Yaw = Azimuth.Number;
            FValue Ignore, Offset, Overridden, Value;
            if (!(SceneValue(Field(*Sun, TEXT("ignore_offsets")), Ignore) && Ignore.Flag))
            {
                if (ActorValue(Field(*Sun, TEXT("altitude_offset")), Offset))
                    Pitch += Offset.Number;
                if (ActorValue(Field(*Sun, TEXT("azimuth_offset")), Offset))
                    Yaw += Offset.Number;
            }
            if (ActorValue(Field(*Sun, TEXT("override")), Overridden) && Overridden.Flag)
            {
                if (ActorValue(Field(*Sun, TEXT("override_altitude")), Value))
                    Pitch = Value.Number;
                if (ActorValue(Field(*Sun, TEXT("override_azimuth")), Value))
                    Yaw = Value.Number;
            }
            double PitchAdd = 0, YawAdd = 0;
            (*Sun)->TryGetNumberField(TEXT("pitch_add"), PitchAdd);
            (*Sun)->TryGetNumberField(TEXT("yaw_add"), YawAdd);
            const FRotator Rotation(float(Pitch + PitchAdd), float(Yaw + YawAdd), 0.f);
            FFound Also;
            FValue Angle;
            Angle.Kind = FValue::EKind::Number;
            Angle.Number = Rotation.Pitch;
            if (Find(Actor, Field(*Sun, TEXT("also_pitch")), Also))
                Applied += Write(Also, Angle, false);
            Angle.Number = Rotation.Yaw;
            if (Find(Actor, Field(*Sun, TEXT("also_yaw")), Also))
                Applied += Write(Also, Angle, false);
            // A world rotation is stored relative to the parent, which works the relative location and scale out
            // again too (Cave022_F00's sun moved 0.01 cm), so all three are swapped back.
            for (const TCHAR* Name : {TEXT("RelativeRotation"), TEXT("RelativeLocation"), TEXT("RelativeScale3D")})
                P4RememberNativePreviewValue(Light, FindFProperty<FProperty>(USceneComponent::StaticClass(), Name));
            if (Light->GetAttachParent())
                Light->SetWorldRotation(Rotation);
            else
                Light->SetRelativeRotation(Rotation);
            ++Applied;
        }
    // Four of the scene's numbers as one collection vector.
    const TArray<TSharedPtr<FJsonValue>>* Vectors = nullptr;
    if (Collection && Json->TryGetArrayField(TEXT("collection_vectors"), Vectors))
        for (const auto& Item : *Vectors)
        {
            const TSharedPtr<FJsonObject> Entry = Item->AsObject();
            const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
            if (!Entry.IsValid() || !Entry->TryGetArrayField(TEXT("parts"), Parts) || Parts->Num() != 4)
                continue;
            float Values[4] = {0, 0, 0, 0};
            bool Read4 = true;
            for (int32 Index = 0; Read4 && Index < 4; ++Index)
            {
                FValue Part;
                Read4 = SceneValue((*Parts)[Index]->AsString(), Part) && Part.Kind == FValue::EKind::Number;
                Values[Index] = float(Part.Number);
            }
            if (Read4)
                Applied += Collection->SetVectorParameterValue(
                    FName(*Field(Entry, TEXT("vector"))), FLinearColor(Values[0], Values[1], Values[2], Values[3]));
        }
    // A light's color into the collection: its linear color times its intensity.
    const TArray<TSharedPtr<FJsonValue>>* Colors = nullptr;
    if (Collection && Json->TryGetArrayField(TEXT("light_colors"), Colors))
        for (const auto& Item : *Colors)
        {
            const TSharedPtr<FJsonObject> Entry = Item->AsObject();
            FFound Found;
            if (!Entry.IsValid() || !Find(Actor, Field(Entry, TEXT("light")), Found) ||
                !CastField<FObjectPropertyBase>(Found.Property))
                continue;
            UObject* Light = CastField<FObjectPropertyBase>(Found.Property)->GetObjectPropertyValue(Found.At);
            FLinearColor Color;
            if (auto* Direct = Cast<ULightComponent>(Light))
                Color = FLinearColor(Direct->LightColor) * Direct->Intensity;
            else if (auto* Sky = Cast<USkyLightComponent>(Light))
                Color = FLinearColor(Sky->LightColor) * Sky->Intensity;
            else
                continue;
            Applied += Collection->SetVectorParameterValue(FName(*Field(Entry, TEXT("vector"))), Color);
        }
    FFound Recapture;
    if (Find(Actor, Field(Json, TEXT("recapture")), Recapture) && CastField<FObjectPropertyBase>(Recapture.Property))
        if (auto* Sky = Cast<USkyLightComponent>(
                CastField<FObjectPropertyBase>(Recapture.Property)->GetObjectPropertyValue(Recapture.At)))
            Sky->RecaptureSky();
    return Applied;
}
// A number property, whole or not, times scale and times another number property when the term names one.
bool Term(AActor* Actor, const TSharedPtr<FJsonObject>& Json, double& Out)
{
    FFound Found;
    if (!Json.IsValid() || !Find(Actor, Field(Json, TEXT("value")), Found))
        return false;
    const auto Number = [](const FValue& Value, double& Into)
    {
        if (Value.Kind == FValue::EKind::Number)
            Into = Value.Number;
        else if (Value.Kind == FValue::EKind::Whole)
            Into = double(Value.Whole);
        else
            return false;
        return true;
    };
    if (!Number(Read(Found, 0), Out))
        return false;
    double Scale = 1, Times = 1;
    Json->TryGetNumberField(TEXT("scale"), Scale);
    FFound By;
    if (Find(Actor, Field(Json, TEXT("times")), By) && !Number(Read(By, 0), Times))
        return false;
    Out *= Scale * Times;
    return true;
}

// A class's material instance on one of its meshes, its parameters from the actor's own properties.
int32 ApplyMaterialInstance(AActor* Actor, const TSharedPtr<FJsonObject>& Entry)
{
    const FString Name = Field(Entry, TEXT("component"));
    TInlineComponentArray<UMeshComponent*> Meshes(Actor);
    UMeshComponent* const* Mesh = Meshes.FindByPredicate([&](UMeshComponent* Part) { return Part->GetName() == Name; });
    FFound MaterialProperty;
    if (!Mesh || !Find(Actor, Field(Entry, TEXT("material")), MaterialProperty))
        return 0;
    auto* Parent = Cast<UMaterialInterface>(Read(MaterialProperty, 0).Object);
    if (!Parent)
        return 0;
    double SlotNumber = 0;
    Entry->TryGetNumberField(TEXT("slot"), SlotNumber);
    const int32 Slot = int32(SlotNumber);
    auto* Instance = Cast<UMaterialInstanceDynamic>((*Mesh)->GetMaterial(Slot));
    // An instance this step made before is kept and given the actor's values again.
    if (!Instance || Instance->Parent != Parent || Instance->GetOuter() != GetTransientPackage())
    {
        Instance = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
        Instance->SetFlags(RF_Transient);
        P4RememberNativePreviewValue(*Mesh, FindFProperty<FProperty>(UMeshComponent::StaticClass(), TEXT("OverrideMaterials")));
        (*Mesh)->SetMaterial(Slot, Instance);
    }
    const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
    if (Entry->TryGetArrayField(TEXT("parameters"), Parameters))
        for (const auto& Item : *Parameters)
        {
            const TSharedPtr<FJsonObject> Parameter = Item->AsObject();
            if (!Parameter.IsValid())
                continue;
            const FName Named(*Field(Parameter, TEXT("name")));
            // A render target the class makes for the parameter, as its construction leaves it: cleared, never drawn.
            const TSharedPtr<FJsonObject>* Target = nullptr;
            if (Parameter->TryGetObjectField(TEXT("render_target"), Target))
            {
                const int64 Format = StaticEnum<ETextureRenderTargetFormat>()->GetValueByNameString(
                    TEXT("RTF_") + Field(*Target, TEXT("format")));
                const TArray<TSharedPtr<FJsonValue>>* Clear = nullptr;
                double Width = 0, Height = 0;
                if (Format == INDEX_NONE || !(*Target)->TryGetArrayField(TEXT("clear"), Clear) || Clear->Num() != 4 ||
                    !Term(Actor, (*Target)->GetObjectField(TEXT("width")), Width) ||
                    !Term(Actor, (*Target)->GetObjectField(TEXT("height")), Height))
                    continue;
                const FLinearColor Color(float((*Clear)[0]->AsNumber()), float((*Clear)[1]->AsNumber()),
                    float((*Clear)[2]->AsNumber()), float((*Clear)[3]->AsNumber()));
                const int32 X = FMath::Max(1, int32(Width)), Y = FMath::Max(1, int32(Height));
                UTexture* Held = nullptr;
                auto* Made = Instance->GetTextureParameterValue(Named, Held)
                    ? Cast<UTextureRenderTarget2D>(Held) : nullptr;
                if (!Made || Made->GetOuter() != GetTransientPackage() || Made->SizeX != X || Made->SizeY != Y ||
                    Made->RenderTargetFormat != Format || !Made->ClearColor.Equals(Color))
                {
                    Made = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
                    Made->RenderTargetFormat = ETextureRenderTargetFormat(Format);
                    Made->ClearColor = Color;
                    Made->InitAutoFormat(X, Y);
                    Made->UpdateResourceImmediate(true);
                    Instance->SetTextureParameterValue(Named, Made);
                }
                continue;
            }
            const TArray<TSharedPtr<FJsonValue>>* Direction = nullptr;
            FFound Pitch, Yaw, Found;
            if (Parameter->TryGetArrayField(TEXT("direction"), Direction) && Direction->Num() == 2 &&
                Find(Actor, (*Direction)[0]->AsString(), Pitch) && Find(Actor, (*Direction)[1]->AsString(), Yaw))
            {
                const FVector Vector = FRotator(float(Read(Pitch, 0).Number), float(Read(Yaw, 0).Number), 0.f).Vector();
                Instance->SetVectorParameterValue(Named, FLinearColor(Vector.X, Vector.Y, Vector.Z, 1.f));
                continue;
            }
            const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
            if (Parameter->TryGetArrayField(TEXT("vector"), Parts) && Parts->Num() == 4)
            {
                double Values[4] = {0, 0, 0, 0};
                bool Read4 = true;
                for (int32 Index = 0; Index < 4; ++Index)
                    Read4 = Read4 && Term(Actor, (*Parts)[Index]->AsObject(), Values[Index]);
                if (Read4)
                    Instance->SetVectorParameterValue(Named,
                        FLinearColor(float(Values[0]), float(Values[1]), float(Values[2]), float(Values[3])));
                continue;
            }
            if (!Find(Actor, Field(Parameter, TEXT("value")), Found))
                continue;
            const FValue Value = Read(Found, 0);
            double Scale = 1;
            Parameter->TryGetNumberField(TEXT("scale"), Scale);
            if (Value.Kind == FValue::EKind::Number)
                Instance->SetScalarParameterValue(Named, float(Value.Number * Scale));
            else if (Value.Kind == FValue::EKind::Color)
                Instance->SetVectorParameterValue(Named, Value.Color);
        }
    return 1;
}
} // namespace

int32 P4ApplyNativeEnvironmentPreviews(UWorld* World)
{
    const TSharedPtr<FJsonObject>& Json = Record();
    UClass* Class = Json.IsValid() ? FindObject<UClass>(nullptr, *Field(Json, TEXT("class"))) : nullptr;
    if (!World || !Class)
        return 0;
    int32 Applied = 0;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor) && Actor->IsA(Class))
                    Applied += ApplyOne(Actor, Json);
    const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
    if (Json->TryGetArrayField(TEXT("material_instances"), Instances))
        for (const auto& Item : *Instances)
        {
            const TSharedPtr<FJsonObject> Entry = Item->AsObject();
            UClass* Owner = Entry.IsValid() ? FindObject<UClass>(nullptr, *Field(Entry, TEXT("class"))) : nullptr;
            if (!Owner)
                continue;
            for (ULevel* Level : World->GetLevels())
                if (Level)
                    for (AActor* Actor : Level->Actors)
                        if (IsValid(Actor) && Actor->IsA(Owner))
                            Applied += ApplyMaterialInstance(Actor, Entry);
        }
    return Applied;
}

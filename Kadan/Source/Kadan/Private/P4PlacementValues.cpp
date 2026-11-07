// Placement export: a property's value as the placement stream holds it, and back.
#include "P4PlacementInternal.h"

using namespace P4PlacementDetail;

namespace
{
bool TextOf(const FProperty* Field, const void* Data, FString& Out)
{
    if (const FNameProperty* Name = CastField<FNameProperty>(Field))
        Out = Name->GetPropertyValue(Data).ToString();
    else if (const FStrProperty* String = CastField<FStrProperty>(Field))
        Out = String->GetPropertyValue(Data);
    else if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Field))
    {
        const FSoftObjectPath Path = Soft->GetPropertyValue(Data).ToSoftObjectPath();
        Out = Path.IsNull() ? TEXT("None") : Path.ToString();
    }
    else if (const FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Field))
    {
        const UObject* Target = Reference->GetObjectPropertyValue(Data);
        Out = Target ? Target->GetPathName() : TEXT("None");
    }
    else
        return false;
    return true;
}

// A member of a component transform field: the component's world transform for Rotation, Translation and
// Scale3D, and the component's own property of that name for the rest.
bool ComponentMember(
    USceneComponent* Component, const std::string& Name, const P::Type& Type, P::Value& Out, FString& Error)
{
    const FTransform Transform = Component->GetComponentTransform();
    if (Name == "Rotation" && Type.Is == P::Kind::Quat)
    {
        const FQuat Q = Transform.GetRotation();
        const float Words[] = {Q.X, Q.Y, Q.Z, Q.W};
        for (int32 I = 0; I < 4; ++I)
            Out.Words[I] = P::Value::FloatBits(Words[I]);
        return true;
    }
    if ((Name == "Translation" || Name == "Scale3D") && Type.Is == P::Kind::Vec)
    {
        const FVector V = Name == "Translation" ? Transform.GetTranslation() : Transform.GetScale3D();
        for (int32 I = 0; I < 3; ++I)
            Out.Words[I] = P::Value::FloatBits(V[I]);
        return true;
    }
    FProperty* Leaf = nullptr;
    void* Data = nullptr;
    return Resolve(Component, Wide(Name), Leaf, Data, Error) && ToValue(Leaf, Data, Type, Out, Error);
}
} // namespace

namespace P4PlacementDetail
{
const FNumericProperty* Numeric(const FProperty* Field)
{
    if (const FEnumProperty* Enum = CastField<FEnumProperty>(Field))
        return Enum->GetUnderlyingProperty();
    return CastField<FNumericProperty>(Field);
}

// A property's value as the stream holds it.
bool ToValue(const FProperty* Field, const void* Data, const P::Type& Type, P::Value& Out, FString& Error)
{
    Out = P::Value();
    Out.Is = Type.Is;
    const FStructProperty* Struct = CastField<FStructProperty>(Field);
    switch (Type.Is)
    {
        case P::Kind::Bool:
            if (const FBoolProperty* Bool = CastField<FBoolProperty>(Field))
            {
                Out.Bits = Bool->GetPropertyValue(Data) ? 1 : 0;
                return true;
            }
            if (const FNumericProperty* Number = Numeric(Field))
            {
                Out.Bits = Number->GetUnsignedIntPropertyValue(Data) ? 1 : 0;
                return true;
            }
            break;
        case P::Kind::I32:
            if (const FNumericProperty* Number = Numeric(Field))
                if (Number->IsInteger())
                {
                    Out.Bits = uint32(int32(Number->GetSignedIntPropertyValue(Data)));
                    return true;
                }
            break;
        case P::Kind::U32:
        case P::Kind::U8:
        case P::Kind::U16:
        case P::Kind::U64:
            if (const FNumericProperty* Number = Numeric(Field))
                if (Number->IsInteger())
                {
                    const uint64 Value = Number->GetUnsignedIntPropertyValue(Data);
                    const uint64 Mask = Type.Is == P::Kind::U8 ? 0xFF
                        : Type.Is == P::Kind::U16              ? 0xFFFF
                        : Type.Is == P::Kind::U32              ? 0xFFFFFFFFull
                                                               : ~0ull;
                    if (Value & ~Mask)
                    {
                        Error = TEXT("a value too large for the stream");
                        return false;
                    }
                    Out.Bits = Value;
                    return true;
                }
            break;
        case P::Kind::F32:
            if (const FFloatProperty* Float = CastField<FFloatProperty>(Field))
            {
                Out.SetFloat(Float->GetPropertyValue(Data));
                return true;
            }
            if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Field))
            {
                Out.SetFloat(float(Double->GetPropertyValue(Data)));
                return true;
            }
            break;
        case P::Kind::Name:
        {
            if (const FEnumProperty* Enum = CastField<FEnumProperty>(Field))
            {
                Out = P::NameFromUtf8(Utf8(Enum->GetEnum()->GetNameStringByValue(
                    Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Data))));
                return true;
            }
            if (const FByteProperty* Byte = CastField<FByteProperty>(Field))
                if (Byte->Enum)
                {
                    Out = P::NameFromUtf8(Utf8(Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(Data))));
                    return true;
                }
            FString Text;
            if (TextOf(Field, Data, Text))
            {
                Out = P::NameFromUtf8(Utf8(Text));
                return true;
            }
            break;
        }
        case P::Kind::SoftPath:
            if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Field))
            {
                const FSoftObjectPath Path = Soft->GetPropertyValue(Data).ToSoftObjectPath();
                Out = P::EmptyOf(Type, P::RowVersion);
                *Out.Find("AssetPathName") = P::NameFromUtf8(
                    Utf8(Path.IsNull() ? FString() : Path.GetAssetPathName().ToString()));
                *Out.Find("SubPathString") = P::NameFromUtf8(Utf8(Path.GetSubPathString()));
                return true;
            }
            break;
        case P::Kind::Vec:
            if (Struct && Struct->Struct == TBaseStructure<FVector>::Get())
            {
                const FVector& V = *static_cast<const FVector*>(Data);
                Out.Words[0] = P::Value::FloatBits(V.X);
                Out.Words[1] = P::Value::FloatBits(V.Y);
                Out.Words[2] = P::Value::FloatBits(V.Z);
                return true;
            }
            break;
        case P::Kind::Rot:
            if (Struct && Struct->Struct == TBaseStructure<FRotator>::Get())
            {
                const FRotator& R = *static_cast<const FRotator*>(Data);
                Out.Words[0] = P::Value::FloatBits(R.Pitch);
                Out.Words[1] = P::Value::FloatBits(R.Yaw);
                Out.Words[2] = P::Value::FloatBits(R.Roll);
                return true;
            }
            break;
        case P::Kind::Quat:
            if (Struct && Struct->Struct == TBaseStructure<FQuat>::Get())
            {
                const FQuat& Q = *static_cast<const FQuat*>(Data);
                Out.Words[0] = P::Value::FloatBits(Q.X);
                Out.Words[1] = P::Value::FloatBits(Q.Y);
                Out.Words[2] = P::Value::FloatBits(Q.Z);
                Out.Words[3] = P::Value::FloatBits(Q.W);
                return true;
            }
            break;
        case P::Kind::Array:
            if (const FArrayProperty* Array = CastField<FArrayProperty>(Field))
            {
                FScriptArrayHelper Items(Array, Data);
                for (int32 I = 0; I < Items.Num(); ++I)
                {
                    P::Value Item;
                    if (!ToValue(Array->Inner, Items.GetRawPtr(I), *Type.Item, Item, Error))
                        return false;
                    Out.Items.push_back(std::move(Item));
                }
                return true;
            }
            break;
        case P::Kind::Struct:
            if (Struct)
            {
                Out = P::EmptyOf(Type, P::RowVersion);
                if (Struct->Struct == TBaseStructure<FTransform>::Get())
                {
                    P::Value* Rotation = Out.Find("Rotation");
                    P::Value* Translation = Out.Find("Translation");
                    P::Value* Scale = Out.Find("Scale3D");
                    if (!Rotation || !Translation || !Scale)
                        break;
                    const FTransform& Transform = *static_cast<const FTransform*>(Data);
                    const FQuat Q = Transform.GetRotation();
                    const FVector T = Transform.GetTranslation(), S = Transform.GetScale3D();
                    const float Quat[] = {Q.X, Q.Y, Q.Z, Q.W};
                    for (int32 I = 0; I < 4; ++I)
                        Rotation->Words[I] = P::Value::FloatBits(Quat[I]);
                    for (int32 I = 0; I < 3; ++I)
                    {
                        Translation->Words[I] = P::Value::FloatBits(T[I]);
                        Scale->Words[I] = P::Value::FloatBits(S[I]);
                    }
                    return true;
                }
                for (auto& Member : Out.Members)
                {
                    const P::Type* Of = nullptr;
                    for (const auto& F : Type.Members)
                        if (F.Name == Member.first)
                            Of = F.Of;
                    const FProperty* Inner = Struct->Struct->FindPropertyByName(FName(*Wide(Member.first)));
                    if (!Inner || !Of)
                    {
                        Error = FString::Printf(TEXT("%s has no %s"), *Struct->Struct->GetName(), *Wide(Member.first));
                        return false;
                    }
                    if (!ToValue(Inner, Inner->ContainerPtrToValuePtr<void>(Data), *Of, Member.second, Error))
                    {
                        Error = Wide(Member.first) + TEXT(": ") + Error;
                        return false;
                    }
                }
                return true;
            }
            break;
        case P::Kind::Repeat:
        case P::Kind::Bools:
            break;
    }
    if (Error.IsEmpty())
        Error = FString::Printf(TEXT("%s is not of the kind the stream has"), *Field->GetName());
    return false;
}

bool ComponentValue(AActor* Actor, const P::Field& Field, P::Value& Out, FString& Error)
{
    USceneComponent* Component = SceneComponentNamed(Actor, FName(*Wide(Field.Name)));
    // A class's defaults need not instantiate the named construction-script components, while the
    // placed actor does: the placed actor's component is read for both.
    if (!Component)
        return true;
    for (auto& Member : Out.Members)
    {
        const P::Type* Of = nullptr;
        for (const auto& One : Field.Of->Members)
            if (One.Name == Member.first)
                Of = One.Of;
        if (!Of || !ComponentMember(Component, Member.first, *Of, Member.second, Error))
        {
            Error = Wide(Field.Name + "." + Member.first) + TEXT(": ") +
                (Error.IsEmpty() ? FString(TEXT("not a member")) : Error);
            return false;
        }
    }
    return true;
}

// A value of the stream given to a property: what ToValue reads, written. A name the stream gives an
// object property has to name an object the project has.
bool FromValue(const FProperty* Field, void* Data, const P::Type& Type, const P::Value& In, FString& Error)
{
    const FStructProperty* Struct = CastField<FStructProperty>(Field);
    switch (Type.Is)
    {
        case P::Kind::Bool:
            if (const FBoolProperty* Bool = CastField<FBoolProperty>(Field))
            {
                Bool->SetPropertyValue(Data, In.Bits != 0);
                return true;
            }
            if (const FNumericProperty* Number = Numeric(Field))
                if (Number->IsInteger())
                {
                    Number->SetIntPropertyValue(Data, uint64(In.Bits != 0));
                    return true;
                }
            break;
        case P::Kind::I32:
            if (const FNumericProperty* Number = Numeric(Field))
                if (Number->IsInteger())
                {
                    Number->SetIntPropertyValue(Data, int64(int32(uint32(In.Bits))));
                    return true;
                }
            break;
        case P::Kind::U32:
        case P::Kind::U8:
        case P::Kind::U16:
        case P::Kind::U64:
            if (const FNumericProperty* Number = Numeric(Field))
                if (Number->IsInteger())
                {
                    Number->SetIntPropertyValue(Data, uint64(In.Bits));
                    return true;
                }
            break;
        case P::Kind::F32:
            if (const FFloatProperty* Float = CastField<FFloatProperty>(Field))
            {
                Float->SetPropertyValue(Data, In.Float());
                return true;
            }
            if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Field))
            {
                Double->SetPropertyValue(Data, double(In.Float()));
                return true;
            }
            break;
        case P::Kind::Name:
        {
            const FString Text = Wide(P::NameUtf8(In));
            const bool None = Text.IsEmpty() || Text == TEXT("None");
            if (const FEnumProperty* Enum = CastField<FEnumProperty>(Field))
            {
                const int64 Value = Enum->GetEnum()->GetValueByNameString(Text);
                if (Value == INDEX_NONE)
                {
                    Error = Text + TEXT(" is not an enum option");
                    return false;
                }
                Enum->GetUnderlyingProperty()->SetIntPropertyValue(Data, Value);
                return true;
            }
            if (const FByteProperty* Byte = CastField<FByteProperty>(Field))
                if (Byte->Enum)
                {
                    const int64 Value = Byte->Enum->GetValueByNameString(Text);
                    if (Value < 0 || Value > 255)
                    {
                        Error = Text + TEXT(" is not an enum option");
                        return false;
                    }
                    Byte->SetPropertyValue(Data, uint8(Value));
                    return true;
                }
            if (const FNameProperty* Name = CastField<FNameProperty>(Field))
            {
                Name->SetPropertyValue(Data, None ? FName() : FName(*Text));
                return true;
            }
            if (const FStrProperty* String = CastField<FStrProperty>(Field))
            {
                String->SetPropertyValue(Data, Text);
                return true;
            }
            if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Field))
            {
                Soft->SetPropertyValue(Data, None ? FSoftObjectPtr() : FSoftObjectPtr(FSoftObjectPath(Text)));
                return true;
            }
            if (const FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Field))
            {
                if (None)
                {
                    Reference->SetObjectPropertyValue(Data, nullptr);
                    return true;
                }
                UObject* Target = StaticLoadObject(
                    UObject::StaticClass(), nullptr, *Text, nullptr, LOAD_NoWarn | LOAD_Quiet);
                if (!Target)
                {
                    Error = Text + TEXT(" is not in the project");
                    return false;
                }
                const FClassProperty* ClassField = CastField<FClassProperty>(Field);
                if (!Target->IsA(Reference->PropertyClass) ||
                    (ClassField && !(Cast<UClass>(Target) && Cast<UClass>(Target)->IsChildOf(ClassField->MetaClass))))
                {
                    Error = Text + TEXT(" is not of the kind the property holds");
                    return false;
                }
                Reference->SetObjectPropertyValue(Data, Target);
                return true;
            }
            break;
        }
        case P::Kind::SoftPath:
            if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Field))
            {
                const P::Value *Asset = In.Find("AssetPathName"), *Sub = In.Find("SubPathString");
                if (!Asset || !Sub)
                {
                    Error = TEXT("The soft reference is missing one of its strings");
                    return false;
                }
                const FString Path = Wide(P::NameUtf8(*Asset)), SubPath = Wide(P::NameUtf8(*Sub));
                Soft->SetPropertyValue(Data,
                    Path.IsEmpty() || Path == TEXT("None") ? FSoftObjectPtr()
                                                           : FSoftObjectPtr(FSoftObjectPath(FName(*Path), SubPath)));
                return true;
            }
            break;
        case P::Kind::Vec:
            if (Struct && Struct->Struct == TBaseStructure<FVector>::Get())
            {
                P::Value Word;
                Word.Is = P::Kind::F32;
                FVector& V = *static_cast<FVector*>(Data);
                Word.Bits = In.Words[0];
                V.X = Word.Float();
                Word.Bits = In.Words[1];
                V.Y = Word.Float();
                Word.Bits = In.Words[2];
                V.Z = Word.Float();
                return true;
            }
            break;
        case P::Kind::Rot:
            if (Struct && Struct->Struct == TBaseStructure<FRotator>::Get())
            {
                P::Value Word;
                Word.Is = P::Kind::F32;
                FRotator& R = *static_cast<FRotator*>(Data);
                Word.Bits = In.Words[0];
                R.Pitch = Word.Float();
                Word.Bits = In.Words[1];
                R.Yaw = Word.Float();
                Word.Bits = In.Words[2];
                R.Roll = Word.Float();
                return true;
            }
            break;
        case P::Kind::Quat:
            if (Struct && Struct->Struct == TBaseStructure<FQuat>::Get())
            {
                P::Value Word;
                Word.Is = P::Kind::F32;
                FQuat& Q = *static_cast<FQuat*>(Data);
                Word.Bits = In.Words[0];
                Q.X = Word.Float();
                Word.Bits = In.Words[1];
                Q.Y = Word.Float();
                Word.Bits = In.Words[2];
                Q.Z = Word.Float();
                Word.Bits = In.Words[3];
                Q.W = Word.Float();
                return true;
            }
            break;
        case P::Kind::Array:
            if (const FArrayProperty* Array = CastField<FArrayProperty>(Field))
            {
                FScriptArrayHelper Items(Array, Data);
                // A placement event does not serialize every member of its reflected struct. Keep
                // existing items' unstreamed values, such as bEnable, when importing its options.
                const int32 Count = int32(In.Items.size());
                if (Items.Num() > Count)
                    Items.RemoveValues(Count, Items.Num() - Count);
                else if (Items.Num() < Count)
                    Items.AddValues(Count - Items.Num());
                for (int32 I = 0; I < Items.Num(); ++I)
                    if (!FromValue(Array->Inner, Items.GetRawPtr(I), *Type.Item, In.Items[size_t(I)], Error))
                    {
                        Error = FString::Printf(TEXT("[%d]: "), I) + Error;
                        return false;
                    }
                return true;
            }
            break;
        case P::Kind::Struct:
            if (Struct)
            {
                if (Struct->Struct == TBaseStructure<FTransform>::Get())
                {
                    const P::Value* Rotation = In.Find("Rotation");
                    const P::Value* Translation = In.Find("Translation");
                    const P::Value* Scale = In.Find("Scale3D");
                    if (!Rotation || !Translation || !Scale)
                        break;
                    auto Float = [](uint32 Bits)
                    {
                        P::Value V;
                        V.Bits = Bits;
                        return V.Float();
                    };
                    const FQuat Q(Float(Rotation->Words[0]), Float(Rotation->Words[1]), Float(Rotation->Words[2]),
                        Float(Rotation->Words[3]));
                    const FVector T(
                        Float(Translation->Words[0]), Float(Translation->Words[1]), Float(Translation->Words[2]));
                    const FVector S(Float(Scale->Words[0]), Float(Scale->Words[1]), Float(Scale->Words[2]));
                    *static_cast<FTransform*>(Data) = FTransform(Q, T, S);
                    return true;
                }
                // Only what the stream holds: a member the version leaves out keeps its value.
                for (const auto& Member : In.Members)
                {
                    const P::Type* Of = nullptr;
                    for (const auto& F : Type.Members)
                        if (F.Name == Member.first)
                            Of = F.Of;
                    const FProperty* Inner = Struct->Struct->FindPropertyByName(FName(*Wide(Member.first)));
                    if (!Inner || !Of)
                    {
                        Error = FString::Printf(TEXT("%s has no %s"), *Struct->Struct->GetName(), *Wide(Member.first));
                        return false;
                    }
                    if (!FromValue(Inner, Inner->ContainerPtrToValuePtr<void>(Data), *Of, Member.second, Error))
                    {
                        Error = Wide(Member.first) + TEXT(": ") + Error;
                        return false;
                    }
                }
                return true;
            }
            break;
        case P::Kind::Repeat:
        case P::Kind::Bools:
            break;
    }
    if (Error.IsEmpty())
        Error = FString::Printf(TEXT("%s is not of the kind the stream has"), *Field->GetName());
    return false;
}

// A value's lines for comparing: a name that is empty and one that is None are the same to a property.
std::vector<std::string> Canonical(const P::Value& Value, const std::string& Path)
{
    std::vector<std::string> Out;
    P::Flatten(Value, Path, Out);
    static const std::string Hex = P::Hex("None"), None = "=n:" + Hex;
    for (std::string& Line : Out)
        if (Line.find(".SubPathString=") == std::string::npos && Line.size() >= None.size() &&
            Line.compare(Line.size() - None.size(), None.size(), None) == 0)
            Line.resize(Line.size() - Hex.size());
    return Out;
}
} // namespace P4PlacementDetail

#include "P4ParticleDistributionDefaults.h"
#include "Distributions/DistributionFloat.h"
#include "Distributions/DistributionVector.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace P4ParticleDistributionDefaults
{
const FObjectProperty* Reference(const FProperty* Property)
{
    const auto* Struct = CastField<FStructProperty>(Property);
    if (!Struct || Struct->ArrayDim != 1)
        return nullptr;
    UClass* Expected = nullptr;
    if (Struct->Struct == FRawDistributionFloat::StaticStruct())
        Expected = UDistributionFloat::StaticClass();
    else if (Struct->Struct == FRawDistributionVector::StaticStruct())
        Expected = UDistributionVector::StaticClass();
    else
        return nullptr;
    const auto* Ref = FindFProperty<FObjectProperty>(Struct->Struct, TEXT("Distribution"));
    if (!Ref || Ref->ArrayDim != 1 || Ref->GetOwnerStruct() != Struct->Struct || Ref->PropertyClass != Expected)
        return nullptr;
    FStructOnScope Default(Struct->Struct);
    return Ref->GetObjectPropertyValue_InContainer(Default.GetStructMemory()) ? nullptr : Ref;
}
bool ExplicitNull(const P4MapKit::FDecodedValue& Value, std::size_t& Index)
{
    using V = P4MapKit::FDecodedValue;
    if (Value.Kind != V::EKind::Object)
        return false;
    int32 Found = 0;
    for (std::size_t Slot = 0; Slot < Value.Fields.size(); ++Slot)
        if (Value.Fields[Slot].first == "Distribution")
        {
            ++Found;
            Index = Slot;
        }
    return Found == 1 && Value.Fields[Index].second.Kind == V::EKind::Null;
}
} //namespace P4ParticleDistributionDefaults

bool P4CanonicalOriginalParticleDistribution(P4MapKit::FDecodedValue& Value, const FProperty* Property)
{
    std::size_t Index = 0;
    if (!P4ParticleDistributionDefaults::Reference(Property) ||
        !P4ParticleDistributionDefaults::ExplicitNull(Value, Index))
        return false;
    Value.Fields.erase(Value.Fields.begin() + Index);
    return true;
}

bool P4OriginalParticleDistributionNullsHeld(
    UObject* Object, const P4MapKit::FDecodedValue& Source, int32& Checked, FString& Error)
{
    Checked = 0;
    if (!Object || Source.Kind != P4MapKit::FDecodedValue::EKind::Object)
    {
        Error = TEXT("particle-distribution-source-object-unreadable");
        return false;
    }
    for (const auto& Field : Source.Fields)
    {
        const FProperty* Property = Object->GetClass()->FindPropertyByName(FName(UTF8_TO_TCHAR(Field.first.c_str())));
        const auto* Reference = P4ParticleDistributionDefaults::Reference(Property);
        std::size_t Index = 0;
        if (!Reference || !P4ParticleDistributionDefaults::ExplicitNull(Field.second, Index))
            continue;
        ++Checked;
        if (Reference->GetObjectPropertyValue_InContainer(Property->ContainerPtrToValuePtr<void>(Object)))
        {
            Error = Object->GetPathName() + TEXT(".") + UTF8_TO_TCHAR(Field.first.c_str()) +
                TEXT(".Distribution: original-null-reference-not-held");
            return false;
        }
    }
    Error.Reset();
    return true;
}

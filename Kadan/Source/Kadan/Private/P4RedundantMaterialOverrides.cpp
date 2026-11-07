#include "P4RedundantMaterialOverrides.h"
#include "Components/MeshComponent.h"

void P4WithoutRedundantNullMaterialOverrides(
    UObject* Live, P4MapKit::FDecodedValue& Source, const P4MapKit::FDecodedValue& Cooked)
{
    using P4MapKit::FDecodedValue;
    const auto* Component = Cast<UMeshComponent>(Live);
    if (!Component || Cooked.Field("OverrideMaterials"))
        return;
    for (const auto* Material : Component->OverrideMaterials)
        if (Material)
            return;
    for (std::size_t Index = 0; Index < Source.Fields.size(); ++Index)
    {
        const auto& Field = Source.Fields[Index];
        if (Field.first != "OverrideMaterials" || Field.second.Kind != FDecodedValue::EKind::Array ||
            Field.second.Items.empty() || Field.second.Items.size() > 256)
            continue;
        if (Component->OverrideMaterials.Num() != 0 &&
            Component->OverrideMaterials.Num() != int32(Field.second.Items.size()))
            return;
        for (const auto& Value : Field.second.Items)
            if (Value.Kind != FDecodedValue::EKind::Null)
                return;
        // GetMaterial falls through for each null entry, exactly as it does for
        // the verified empty or equally sized all-null live array. Non-null creator materials,
        // explicit cooked tags and arbitrary source fields are never projected.
        Source.Fields.erase(Source.Fields.begin() + Index);
        return;
    }
}

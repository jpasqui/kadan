#pragma once
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "P4MapKitCore/property_decoder.h"

inline void P4PersistentTextFlags(P4MapKit::FDecodedValue& Value)
{
    using P4MapKit::FDecodedValue;
    if (Value.Fields.size() != 1 || Value.Fields[0].first != "__native_text")
        return;
    auto& Text = Value.Fields[0].second;
    const auto* History = Text.Field("history");
    if (!History || History->Kind != FDecodedValue::EKind::Number || (History->Text != "0" && History->Text != "-1"))
        return;
    for (auto& Field : Text.Fields)
    {
        if (Field.first != "flags" || Field.second.Kind != FDecodedValue::EKind::Number)
            continue;
        uint64 Flags = 0;
        if (!P4MapKit::ParseInteger(Field.second.Text, false, Flags) || Flags > MAX_uint32)
            return;
        // FText serialization removes precisely these load-time bookkeeping
        // flags when reading or writing a persistent archive. Localization
        // namespace, key, source and every other flag remain compared.
        Flags &= ~uint64(ETextFlag::ConvertedProperty | ETextFlag::InitializedFromString);
        Field.second.Text = std::to_string(Flags);
    }
}

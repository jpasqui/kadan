// Comparison-only projection of independently decoded native table values.
// Importer values and literal string/text contents retain their original bytes.
#pragma once
#include "property_decoder.h"

namespace P4MapKit
{
namespace TableRowComparison
{
inline bool FoldSoftAssetNames(FDecodedValue& Value)
{
    if (Value.Kind == FDecodedValue::EKind::HardReference)
    {
        // UObject's package/outer/object names are FNames too. The name pool
        // can keep a different display spelling after another class loads:
        // Caveinfo and CaveInfo still identify the same generated class.
        // Only the independently decoded reference leaf grants this rule;
        // class-looking FString/Text values remain byte-exact below.
        if (!Value.ReferenceIndex) return Value.Text.empty();
        if (Value.Text.empty()) return false;
        for (char& Letter : Value.Text)
            if (Letter >= 'A' && Letter <= 'Z') Letter += 'a' - 'A';
        return true;
    }
    if (Value.Kind == FDecodedValue::EKind::SoftPath)
    {
        const std::string Suffix = Value.SubPath.empty() ? std::string() : ":" + Value.SubPath;
        if (Value.Text != Value.AssetPath + Suffix) return false;
        // The asset path is an FName. A soft subobject path is an FString and
        // remains exact, even when it contains another /Game/ substring.
        for (char& Letter : Value.AssetPath)
            if (Letter >= 'A' && Letter <= 'Z') Letter += 'a' - 'A';
        Value.Text = Value.AssetPath + Suffix;
        return true;
    }
    // Localized text carries its source, key and namespace as exact strings.
    if (Value.Kind == FDecodedValue::EKind::NativeText) return true;
    for (auto& Field : Value.Fields) if (!FoldSoftAssetNames(Field.second)) return false;
    for (auto& Item : Value.Items) if (!FoldSoftAssetNames(Item)) return false;
    return true;
}

inline FDecodedValue Value(const FDecodedValue& Legacy, const FDecodedValue* Typed, bool FoldNames)
{
    // A typed decoder may support more archives than the established importer.
    // It contributes comparison identity only for the exact legacy value that
    // already passed that importer, never a replacement or fallback import.
    if (!FoldNames || !Typed || DecodedJson(*Typed) != DecodedJson(Legacy)) return Legacy;
    FDecodedValue Compared = *Typed;
    if (!FoldSoftAssetNames(Compared)) return Legacy;
    return LegacyDecodedValue(Compared);
}
}
}

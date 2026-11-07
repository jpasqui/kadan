#pragma once
#include "P4PersistentTextFlags.h"

inline void P4CanonicalUnkeyedTextLiteral(P4MapKit::FDecodedValue& Value)
{
    using V = P4MapKit::FDecodedValue;
    if (Value.Fields.size() != 1 || Value.Fields[0].first != "__native_text")
        return;
    const auto& Text = Value.Fields[0].second;
    const auto* Flags = Text.Field("flags");
    const auto* History = Text.Field("history");
    const auto* Namespace = Text.Field("namespace");
    const auto* Key = Text.Field("key");
    const auto* Source = Text.Field("source");
    if (Text.Fields.size() != 5 || !Flags || Flags->Kind != V::EKind::Number ||
        (Flags->Text != "0" && Flags->Text != std::to_string(uint32(ETextFlag::Immutable))) || !History ||
        History->Kind != V::EKind::Number || History->Text != "0" || !Namespace ||
        Namespace->Kind != V::EKind::String || !Namespace->Text.empty() || !Key || Key->Kind != V::EKind::String ||
        !Key->Text.empty() || !Source || Source->Kind != V::EKind::String)
        return;
    // The localization manager returns an unmanaged source string for exactly
    // this legacy no-identity case. Persist it as an invariant literal instead
    // of allowing the editor to fabricate a localization GUID. Keep immutable
    // semantics and the original wording; keyed/localized texts stay exact.
    V Literal;
    const uint32 NativeFlags = ETextFlag::CultureInvariant | (Flags->Text == "0" ? 0 : ETextFlag::Immutable);
    Literal.Fields.emplace_back("flags", V::Atom(V::EKind::Number, std::to_string(NativeFlags)));
    Literal.Fields.emplace_back("history", V::Atom(V::EKind::Number, "-1"));
    Literal.Fields.emplace_back("has_source", V::Atom(V::EKind::Boolean, Source->Text.empty() ? "false" : "true"));
    if (!Source->Text.empty())
        Literal.Fields.emplace_back("source", *Source);
    Value.Fields[0].second = std::move(Literal);
}

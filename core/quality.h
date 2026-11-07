// Portable helpers of the kit's readers. No engine objects or filesystem side effects.
#pragma once
// Preserve parser nonfinite checks when embedded in UE4's /fp:fast MSVC build.
#if defined(_MSC_VER)
#pragma float_control(precise, on)
#endif
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <unordered_set>

namespace P4MapKit
{
inline bool ParseInteger(const std::string& Text, bool Unsigned, std::uint64_t& Bits)
{
    if (Text.empty()) return false;
    const bool Negative = Text[0] == '-';
    if (Unsigned && Negative) return false;
    const std::uint64_t Limit = Unsigned ? std::numeric_limits<std::uint64_t>::max() :
        (std::uint64_t(1) << 63) - (Negative ? 0 : 1);
    const std::size_t Start = Negative ? 1 : 0;
    if (Start == Text.size()) return false;
    std::uint64_t Number = 0;
    for (std::size_t At = Start; At < Text.size(); ++At)
    {
        if (Text[At] < '0' || Text[At] > '9') return false;
        const std::uint64_t Digit = static_cast<std::uint64_t>(Text[At] - '0');
        if (Number > (Limit - Digit) / 10) return false;
        Number = Number * 10 + Digit;
    }
    Bits = Negative ? std::uint64_t(0) - Number : Number;
    return true;
}

inline bool ValidDigest(const std::string& Digest)
{
    if (Digest.size() != 64) return false;
    for (char Character : Digest)
        if (!((Character >= '0' && Character <= '9') || (Character >= 'a' && Character <= 'f'))) return false;
    return true;
}

// Coverage spans account for every byte, including opaque data. This checks
// arithmetic and coverage, not whether a decoder has understood those bytes.
struct FSourceSpan { std::size_t Offset; std::size_t Size; };
inline bool CompleteSpans(std::size_t SourceSize, const std::vector<FSourceSpan>& Spans)
{
    std::size_t End = 0;
    for (const FSourceSpan& Span : Spans)
    {
        if (Span.Size == 0 || Span.Offset != End || Span.Offset > SourceSize ||
            Span.Size > SourceSize - Span.Offset) return false;
        End = Span.Offset + Span.Size;
    }
    return End == SourceSize;
}
}

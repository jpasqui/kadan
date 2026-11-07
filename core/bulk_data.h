// UE4.26 legacy FByteBulkData headers and uncompressed byte ranges.
// Source custody and a resolved range do not qualify an editable asset.
#pragma once
#include "package_reader.h"
#include <functional>

namespace P4MapKit
{
enum ELegacyBulkFlag : std::uint32_t
{
    BulkAtEnd = 0x1, BulkZlib = 0x2, BulkSingleElement = 0x4,
    BulkSingleUse = 0x8, BulkUnused = 0x20, BulkInline = 0x40,
    BulkStream = 0x80, BulkSeparate = 0x100, BulkBitWindow = 0x200,
    BulkNotInline = 0x400, BulkOptional = 0x800, BulkMapped = 0x1000,
    BulkSize64 = 0x2000, BulkDuplicate = 0x4000, BulkOldVersion = 0x8000,
    BulkNoFixup = 0x10000
};
enum class EBulkSource { None, Body, Regular, Optional, Mapped };
struct FByteBulkHeader
{
    std::uint32_t Flags = 0;
    std::uint64_t Count = 0, Size = 0;
    std::int64_t Offset = 0;
    std::uint16_t OldVersion = 0;
    std::size_t Start = 0, End = 0;
};
struct FByteBulkContext
{
    // Body, .ubulk, .uptnl, .m.ubulk. -1 means absent, including optional data.
    std::array<std::int64_t, 4> FileSizes{{-1, -1, -1, -1}};
    std::uint64_t HeaderSize = 0;
    std::int64_t BulkStart = 0;
    bool HasBulkStart = false;
    // Converted sidecar formats need independently established file-relative
    // offsets. Never infer this from a missing summary or a plausible range.
    bool RebasedSidecarOffsets = false;
};
struct FByteBulkRange
{
    FByteBulkHeader Header;
    EBulkSource Source = EBulkSource::None;
    std::uint64_t Offset = 0, Size = 0;
};
inline bool ReadByteBulkHeader(FByteReader& Reader, FByteBulkHeader& Out)
{
    FByteBulkHeader Header;
    Header.Start = Reader.Tell();
    if (!Reader.U32(Header.Flags)) return false;
    if (Header.Flags & ~std::uint32_t(0x1ffef)) return Reader.Reject("unknown-bulk-flags");
    // Duplicate optional records carry another header. Until both copies and
    // their equivalence are qualified, do not consume only the primary copy.
    if (Header.Flags & BulkDuplicate) return Reader.Reject("duplicate-bulk-unqualified");
    const std::size_t Width = Header.Flags & BulkSize64 ? 8 : 4;
    if (!Reader.Unsigned(Width, Header.Count) || !Reader.Unsigned(Width, Header.Size)) return false;
    const std::uint64_t Maximum = Width == 8 ? std::uint64_t(INT64_MAX) : std::uint64_t(INT32_MAX);
    if (Header.Count > Maximum || Header.Size > Maximum) return Reader.Reject("negative-bulk-size");
    std::uint64_t Bits = 0;
    if (!Reader.Unsigned(8, Bits)) return false;
    Header.Offset = Bits <= std::uint64_t(INT64_MAX) ? std::int64_t(Bits) :
        -1 - std::int64_t(UINT64_MAX - Bits);
    if (Header.Flags & BulkOldVersion)
    {
        if (!Reader.Unsigned(2, Bits)) return false;
        Header.OldVersion = std::uint16_t(Bits);
    }
    Header.End = Reader.Tell();
    Out = Header;
    return true;
}
inline bool ResolveByteBulk(const FByteBulkHeader& Header, std::uint64_t InlineAt,
    const FByteBulkContext& Context, FByteBulkRange& Out, FReadError& Error)
{
    const auto Reject = [&](const char* Code) { Error = {Header.Start, Code}; return false; };
    if (!Error.Code.empty()) return false;
    if (Header.Flags & ~std::uint32_t(0x1ffef)) return Reject("unknown-bulk-flags");
    if (Header.Flags & BulkDuplicate) return Reject("duplicate-bulk-unqualified");
    if (Header.Flags & (BulkZlib | BulkBitWindow | BulkSingleElement))
        return Reject("bulk-codec-unqualified");
    const bool AtEnd = (Header.Flags & BulkAtEnd) != 0;
    const bool Separate = (Header.Flags & BulkSeparate) != 0;
    if ((!AtEnd && (Separate || (Header.Flags & (BulkOptional | BulkMapped)))) ||
        (AtEnd && (Header.Flags & BulkInline)) ||
        ((Header.Flags & BulkOptional) && (Header.Flags & BulkMapped)) ||
        ((Header.Flags & (BulkOptional | BulkMapped)) && !Separate))
        return Reject("inconsistent-bulk-storage-flags");
    FByteBulkRange Range;
    Range.Header = Header;
    if ((Header.Flags & BulkUnused) || (Header.Count == 0 && Header.Size == 0))
    { Out = Range; return true; }
    if (Header.Count != Header.Size) return Reject("byte-bulk-count-size-mismatch");
    Range.Size = Header.Size;
    if (!AtEnd)
    {
        // UE loads inline when PayloadAtEndOfFile is absent; ForceInlinePayload
        // is a save policy, not the complete definition of the archive layout.
        Range.Source = EBulkSource::Body;
        Range.Offset = InlineAt;
    }
    else
    {
        Range.Source = !Separate ? EBulkSource::Body : Header.Flags & BulkOptional ?
            EBulkSource::Optional : Header.Flags & BulkMapped ? EBulkSource::Mapped : EBulkSource::Regular;
        std::int64_t Offset = Header.Offset;
        if (!(Header.Flags & BulkNoFixup) && !(Separate && Context.RebasedSidecarOffsets))
        {
            if (!Context.HasBulkStart) return Reject("bulk-offset-base-unqualified");
            if (Context.BulkStart < 0) return Reject("invalid-bulk-offset-base");
            if (Offset > INT64_MAX - Context.BulkStart) return Reject("bulk-offset-overflow");
            Offset += Context.BulkStart;
        }
        if (Offset < 0) return Reject("negative-bulk-offset");
        Range.Offset = std::uint64_t(Offset);
        if (!Separate)
        {
            // UE linker offsets address the combined header/export archive.
            if (Range.Offset < Context.HeaderSize) return Reject("bulk-offset-before-body");
            Range.Offset -= Context.HeaderSize;
        }
    }
    const std::size_t Slot = std::size_t(Range.Source) - 1;
    const auto FileSize = Context.FileSizes[Slot];
    if (FileSize < 0) return Reject("missing-bulk-source");
    const auto Limit = std::uint64_t(FileSize);
    if (Range.Offset > Limit || Range.Size > Limit - Range.Offset) return Reject("bulk-range-out-of-bounds");
    Out = Range;
    return true;
}
inline bool ReadByteBulk(FByteReader& Reader, const FByteBulkContext& Context, FByteBulkRange& Out)
{
    FByteBulkHeader Header;
    FByteBulkRange Range;
    if (!ReadByteBulkHeader(Reader, Header) ||
        !ResolveByteBulk(Header, Reader.Tell(), Context, Range, Reader.Error)) return false;
    if (Range.Source == EBulkSource::Body && !(Header.Flags & BulkAtEnd))
    {
        if (Range.Size > Reader.Limit() - Reader.Tell()) return Reader.Reject("inline-bulk-outside-export");
        if (!Reader.Skip(std::size_t(Range.Size))) return false;
    }
    Out = Range;
    return true;
}
// ReadAt must fill the requested byte count or return false. Sink writes only to
// caller-owned private staging; a cancelled/failed stream must not be published.
inline bool StreamByteBulk(const FByteBulkRange& Range,
    const std::function<bool(EBulkSource, std::uint64_t, std::uint8_t*, std::size_t)>& ReadAt,
    const std::function<bool(const std::uint8_t*, std::size_t)>& Sink,
    const std::function<bool()>& Cancel, FReadError& Error)
{
    const auto Reject = [&](const char* Code) { Error = {Range.Header.Start, Code}; return false; };
    if (!Error.Code.empty()) return false;
    if (Range.Source == EBulkSource::None) return Range.Size == 0 ? true : Reject("invalid-empty-bulk-range");
    if (Range.Source < EBulkSource::Body || Range.Source > EBulkSource::Mapped ||
        Range.Size > std::uint64_t(INT64_MAX) || Range.Offset > std::uint64_t(INT64_MAX) - Range.Size)
        return Reject("invalid-bulk-stream-range");
    if (!ReadAt || !Sink) return Reject("missing-bulk-stream-callback");
    std::array<std::uint8_t, 64 * 1024> Buffer;
    std::uint64_t Done = 0;
    while (Done < Range.Size)
    {
        if (Cancel && Cancel()) return Reject("bulk-stream-cancelled");
        const auto Count = std::size_t(std::min<std::uint64_t>(Buffer.size(), Range.Size - Done));
        if (!ReadAt(Range.Source, Range.Offset + Done, Buffer.data(), Count)) return Reject("bulk-stream-read-failed");
        if (Cancel && Cancel()) return Reject("bulk-stream-cancelled");
        if (!Sink(Buffer.data(), Count)) return Reject("bulk-stream-write-failed");
        Done += Count;
    }
    if (Cancel && Cancel()) return Reject("bulk-stream-cancelled");
    return true;
}
}

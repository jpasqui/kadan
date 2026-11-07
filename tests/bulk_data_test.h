// Native archive-layout and range-boundary cases; no game bytes in this fixture.
#pragma once
#include "core/bulk_data.h"
static std::vector<std::uint8_t> P4BulkBytes(std::uint32_t Flags, std::uint64_t Count = 3,
    std::uint64_t Size = 3, std::uint64_t Offset = 0)
{
    std::vector<std::uint8_t> Bytes;
    P4Put(Bytes, Flags); const std::size_t Width = Flags & P4MapKit::BulkSize64 ? 8 : 4;
    P4Put(Bytes, Count, Width); P4Put(Bytes, Size, Width); P4Put(Bytes, Offset, 8);
    if (Flags & P4MapKit::BulkOldVersion) P4Put(Bytes, 12345, 2);
    return Bytes;
}
static void CheckByteBulkReaders()
{
    using namespace P4MapKit;
    FByteBulkContext Context;
    Context.FileSizes = {{100, 100, 100, 100}};
    const auto Decode = [&](const std::vector<std::uint8_t>& Bytes, const char* Expected) {
        FByteReader Reader(Bytes); FByteBulkRange Range;
        Range.Offset = 999;
        const bool Good = ReadByteBulk(Reader, Context, Range);
        Check(Good == (std::string(Expected).empty())); Check(Reader.Error.Code == Expected);
        if (!Good) Check(Range.Offset == 999);
    };
    for (const std::uint32_t Flags : {0u, std::uint32_t(BulkInline), std::uint32_t(BulkSingleUse),
        std::uint32_t(BulkSize64 | BulkOldVersion)})
    {
        auto Bytes = P4BulkBytes(Flags);
        Bytes.insert(Bytes.end(), {0x18, 0xa7, 0xff});
        FByteReader Reader(Bytes); FByteBulkRange Range;
        Check(ReadByteBulk(Reader, Context, Range)); Check(Range.Source == EBulkSource::Body);
        Check(Range.Size == 3); Check(Range.Offset == (Flags & BulkOldVersion ? 30 : 20));
        Check(Reader.Tell() == Bytes.size());
        Check(Range.Header.OldVersion == (Flags & BulkOldVersion ? 12345 : 0));
        for (std::size_t Size = 0; Size < Bytes.size(); ++Size)
        {
            auto Cut = Bytes; Cut.resize(Size);
            FByteReader Short(Cut); FByteBulkRange Rejected;
            Check(!ReadByteBulk(Short, Context, Rejected)); Check(!Short.Error.Code.empty());
        }
    }
    Decode(P4BulkBytes(0x10), "unknown-bulk-flags");
    Decode(P4BulkBytes(0x80000000), "unknown-bulk-flags");
    Decode(P4BulkBytes(BulkDuplicate), "duplicate-bulk-unqualified");
    Decode(P4BulkBytes(0, 0xffffffff), "negative-bulk-size");
    Decode(P4BulkBytes(BulkSize64, 1, UINT64_MAX), "negative-bulk-size");
    for (const auto Flag : {BulkZlib, BulkSingleElement, BulkBitWindow})
        Decode(P4BulkBytes(Flag), "bulk-codec-unqualified");
    for (const std::uint32_t Flags : {std::uint32_t(BulkSeparate), std::uint32_t(BulkOptional),
        std::uint32_t(BulkMapped), std::uint32_t(BulkAtEnd | BulkInline),
        std::uint32_t(BulkAtEnd | BulkSeparate | BulkMapped | BulkOptional),
        std::uint32_t(BulkAtEnd | BulkMapped), std::uint32_t(BulkAtEnd | BulkOptional)})
        Decode(P4BulkBytes(Flags), "inconsistent-bulk-storage-flags");
    Decode(P4BulkBytes(0, 2, 3), "byte-bulk-count-size-mismatch");
    Decode(P4BulkBytes(BulkAtEnd), "bulk-offset-base-unqualified");
    Context.HasBulkStart = true; Context.BulkStart = -1;
    Decode(P4BulkBytes(BulkAtEnd), "invalid-bulk-offset-base");
    Context.BulkStart = 1;
    Decode(P4BulkBytes(BulkAtEnd, 3, 3, INT64_MAX), "bulk-offset-overflow");
    Decode(P4BulkBytes(BulkAtEnd, 3, 3, std::uint64_t(INT64_MIN)), "negative-bulk-offset");
    Context.HeaderSize = 10;
    Decode(P4BulkBytes(BulkAtEnd | BulkNoFixup, 3, 3, 9), "bulk-offset-before-body");
    // Signed relative offsets can be negative before the package fixup.
    Context.BulkStart = 100; Context.HeaderSize = 10;
    auto Bytes = P4BulkBytes(BulkAtEnd, 3, 3, UINT64_MAX - 79);
    FByteReader Reader(Bytes); FByteBulkRange Range;
    Check(ReadByteBulk(Reader, Context, Range)); Check(Range.Offset == 10); Check(Reader.Tell() == 20);
    for (const auto Pair : {std::make_pair(std::uint32_t(0), EBulkSource::Regular),
        std::make_pair(std::uint32_t(BulkOptional), EBulkSource::Optional),
        std::make_pair(std::uint32_t(BulkMapped), EBulkSource::Mapped)})
    {
        Context.RebasedSidecarOffsets = true; Context.HasBulkStart = false;
        const auto Header = P4BulkBytes(BulkAtEnd | BulkSeparate | Pair.first, 3, 3, 97);
        FByteReader Sidecar(Header); FByteBulkRange Resolved;
        Check(ReadByteBulk(Sidecar, Context, Resolved)); Check(Resolved.Source == Pair.second);
        Check(Resolved.Offset == 97); Check(Resolved.Size == 3); Check(Sidecar.Tell() == 20);
        const auto Slot = std::size_t(Pair.second) - 1;
        Context.FileSizes[Slot] = -1; Decode(Header, "missing-bulk-source");
        Context.FileSizes[Slot] = 99; Decode(Header, "bulk-range-out-of-bounds");
        Context.FileSizes[Slot] = 100;
    }
    Decode(P4BulkBytes(BulkAtEnd | BulkSeparate | BulkNoFixup, 3, 3, 101), "bulk-range-out-of-bounds");
    // A valid range beyond 4 GiB is only a descriptor: no giant fixture or
    // payload allocation is required to prove 64-bit count/offset preservation.
    const std::uint64_t Large = (std::uint64_t(1) << 32) + 7;
    Context.FileSizes[1] = std::int64_t(Large + 17);
    const auto Wide = P4BulkBytes(BulkAtEnd | BulkSeparate | BulkSize64 | BulkNoFixup, Large, Large, 17);
    FByteReader WideReader(Wide); FByteBulkRange WideRange;
    Check(ReadByteBulk(WideReader, Context, WideRange));
    Check(WideRange.Header.Count == Large); Check(WideRange.Size == Large);
    Check(WideRange.Offset == 17); Check(WideReader.Tell() == 28);
    Context.FileSizes[1] = 100;
    Context.FileSizes[0] = 100;
    Decode(P4BulkBytes(0), "inline-bulk-outside-export");
    Context.FileSizes[0] = 20;
    Decode(P4BulkBytes(0), "bulk-range-out-of-bounds");
    for (const auto Flags : {std::uint32_t(BulkUnused), std::uint32_t(0)})
    {
        auto Empty = P4BulkBytes(Flags, Flags ? 42 : 0, 0, UINT64_MAX);
        FByteReader NoData(Empty); FByteBulkRange Resolved;
        Check(ReadByteBulk(NoData, Context, Resolved)); Check(Resolved.Source == EBulkSource::None);
        Check(Resolved.Size == 0); Check(Resolved.Header.Count == (Flags ? 42 : 0));
    }
    FReadError Error{0, "prior-error"};
    FByteBulkRange Invalid;
    Check(!ResolveByteBulk(FByteBulkHeader{}, 0, Context, Invalid, Error)); Check(Error.Code == "prior-error");
    FByteBulkHeader Forged; Forged.Flags = 0x10; Error = {};
    Check(!ResolveByteBulk(Forged, 0, Context, Invalid, Error));
    Forged.Flags = BulkDuplicate; Error = {};
    Check(!ResolveByteBulk(Forged, 0, Context, Invalid, Error));

    // More than one chunk, exact absolute offsets and bytes. The source is
    // computed independently by its absolute index, never copied into a fixture.
    Range.Source = EBulkSource::Mapped; Range.Offset = 19; Range.Size = 2 * 65536 + 7;
    std::size_t Reads = 0, Writes = 0; std::uint64_t Seen = 0;
    const auto Read = [&](EBulkSource Source, std::uint64_t At, std::uint8_t* Data, std::size_t Count) {
        Check(Source == EBulkSource::Mapped); Check(At == Range.Offset + Seen);
        Check(Count <= 65536); ++Reads;
        for (std::size_t Index = 0; Index < Count; ++Index) Data[Index] = std::uint8_t((At + Index) % 251);
        return true;
    };
    const auto Write = [&](const std::uint8_t* Data, std::size_t Count) {
        for (std::size_t Index = 0; Index < Count; ++Index) Check(Data[Index] == (Range.Offset + Seen + Index) % 251);
        Seen += Count; ++Writes; return true;
    };
    Error = {}; Check(StreamByteBulk(Range, Read, Write, {}, Error));
    Check(Reads == 3); Check(Writes == 3); Check(Seen == Range.Size);
    const auto FailRead = [](EBulkSource, std::uint64_t, std::uint8_t*, std::size_t) { return false; };
    const auto PassRead = [](EBulkSource, std::uint64_t, std::uint8_t*, std::size_t) { return true; };
    const auto FailWrite = [](const std::uint8_t*, std::size_t) { return false; };
    const auto PassWrite = [](const std::uint8_t*, std::size_t) { return true; };
    Error = {}; Check(!StreamByteBulk(Range, FailRead, PassWrite, {}, Error)); Check(Error.Code == "bulk-stream-read-failed");
    Error = {}; Check(!StreamByteBulk(Range, PassRead, FailWrite, {}, Error)); Check(Error.Code == "bulk-stream-write-failed");
    for (int When : {1, 2, 7})
    {
        int Calls = 0; Error = {};
        Check(!StreamByteBulk(Range, PassRead, PassWrite, [&]() { return ++Calls == When; }, Error));
        Check(Error.Code == "bulk-stream-cancelled");
    }
    Error = {}; Check(!StreamByteBulk(Range, {}, PassWrite, {}, Error));
    Error = {}; Check(!StreamByteBulk(Range, PassRead, {}, {}, Error));
    Error = {0, "prior-error"}; Check(!StreamByteBulk(Range, PassRead, PassWrite, {}, Error));
    Check(Error.Code == "prior-error");
    Range.Source = EBulkSource::None; Range.Size = 0; Error = {};
    Check(StreamByteBulk(Range, {}, {}, {}, Error));
    Range.Size = 1; Error = {}; Check(!StreamByteBulk(Range, {}, {}, {}, Error));
    Range.Source = EBulkSource(99); Error = {}; Check(!StreamByteBulk(Range, {}, {}, {}, Error));
    Range.Source = EBulkSource::Mapped; Range.Size = UINT64_MAX; Error = {};
    Check(!StreamByteBulk(Range, {}, {}, {}, Error));
    Range.Size = 1; Range.Offset = INT64_MAX; Error = {};
    Check(!StreamByteBulk(Range, {}, {}, {}, Error));
}

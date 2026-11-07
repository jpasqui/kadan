// Independent binary fixtures and reader adversarial cases. Included by the native runner.
#pragma once
#include "core/package_reader.h"
static void P4Put(std::vector<std::uint8_t>& Bytes, std::uint64_t Value, std::size_t Width = 4)
{ for (std::size_t Index = 0; Index < Width; ++Index) Bytes.push_back(Index < 8 ? std::uint8_t(Value >> (Index * 8)) : 0); }
static void P4Patch(std::vector<std::uint8_t>& Bytes, std::size_t At, std::uint64_t Value, std::size_t Width = 4)
{ for (std::size_t Index = 0; Index < Width; ++Index) Bytes[At + Index] = std::uint8_t(Value >> (Index * 8)); }
static void P4String(std::vector<std::uint8_t>& Bytes, const std::string& Text)
{ P4Put(Bytes, Text.size() + 1); Bytes.insert(Bytes.end(), Text.begin(), Text.end()); Bytes.push_back(0); }
static void CheckPackageBulkSummaries()
{
    using namespace P4MapKit;
    // Independent cooked summary fixture: variable generations, engine branch
    // strings, additional packages and chunk IDs all precede/follow the base.
    std::vector<std::uint8_t> Header;
    P4Put(Header, 0x9e2a83c1); P4Put(Header, UINT32_MAX - 6);
    for (int Index = 0; Index < 5; ++Index) P4Put(Header, 0);
    P4String(Header, "None");
    const std::size_t FlagsAt = Header.size(); P4Put(Header, 0x80000000);
    for (int Index = 0; Index < 17; ++Index) P4Put(Header, 0);
    const std::size_t TailAt = Header.size();
    P4Put(Header, 2); for (int Index = 0; Index < 4; ++Index) P4Put(Header, Index);
    for (const std::string Branch : {"++UE4+Release-4.26", "compatible"})
    { P4Put(Header, 4, 2); P4Put(Header, 26, 2); P4Put(Header, 2, 2); P4Put(Header, 123456); P4String(Header, Branch); }
    const std::size_t CompressionAt = Header.size(); P4Put(Header, 0); P4Put(Header, 0); P4Put(Header, 17);
    P4Put(Header, 1); P4String(Header, "/Game/Additional"); P4Put(Header, 0);
    const std::size_t BulkAt = Header.size(); P4Put(Header, 0, 8);
    P4Put(Header, 0); P4Put(Header, 2); P4Put(Header, 3); P4Put(Header, 9); P4Put(Header, 0); P4Put(Header, 0);
    const std::size_t NamesAt = Header.size(); P4String(Header, "None"); P4Put(Header, 0);
    P4Patch(Header, 24, Header.size()); P4Patch(Header, FlagsAt + 4, 1); P4Patch(Header, FlagsAt + 8, NamesAt);
    const std::uint64_t Base = Header.size() + 128;
    P4Patch(Header, BulkAt, Base, 8);
    const auto Good = ReadPackageHeader(Header, 256);
    Check(Good.Error.Code.empty()); Check(Good.HasBulkDataStartOffset);
    Check(Good.BulkDataStartOffset == std::int64_t(Base)); Check(Good.BulkDataSummaryError.Code.empty());
    auto Changed = Header; P4Patch(Changed, BulkAt, 0, 8);
    const auto Zero = ReadPackageHeader(Changed, 256);
    Check(Zero.Error.Code.empty()); Check(Zero.HasBulkDataStartOffset); Check(Zero.BulkDataStartOffset == 0);
    // Missing or invalid summary bases do not guess, and do not change census.
    for (const auto Value : {UINT64_MAX, std::uint64_t(Header.size() + 257)})
    {
        Changed = Header; P4Patch(Changed, BulkAt, Value, 8);
        const auto Bad = ReadPackageHeader(Changed, 256);
        Check(Bad.Error.Code.empty()); Check(!Bad.HasBulkDataStartOffset);
        Check(Bad.BulkDataSummaryError.Code == "invalid-package-bulk-offset-base");
    }
    for (const auto At : {TailAt, CompressionAt, CompressionAt + 4})
    {
        Changed = Header; P4Patch(Changed, At, UINT32_MAX);
        const auto Bad = ReadPackageHeader(Changed, 256);
        Check(Bad.Error.Code.empty()); Check(!Bad.HasBulkDataStartOffset); Check(!Bad.BulkDataSummaryError.Code.empty());
    }
    for (std::size_t End = TailAt; End < NamesAt; ++End)
    {
        Changed.assign(Header.begin(), Header.begin() + End);
        Changed.insert(Changed.end(), Header.begin() + NamesAt, Header.end());
        P4Patch(Changed, 24, Changed.size()); P4Patch(Changed, FlagsAt + 8, End);
        const auto Bad = ReadPackageHeader(Changed, 256);
        Check(Bad.Error.Code.empty()); Check(!Bad.HasBulkDataStartOffset); Check(!Bad.BulkDataSummaryError.Code.empty());
    }
}
static void CheckPackageReaders()
{
    CheckPackageBulkSummaries();
    using namespace P4MapKit;
    std::vector<std::uint8_t> Header;
    P4Put(Header, 0x9e2a83c1); P4Put(Header, UINT32_MAX - 6);
    for (int Index = 0; Index < 5; ++Index) P4Put(Header, 0); // version fields, custom count, header size
    P4String(Header, "/Game/Original");
    const std::size_t Flags = Header.size();
    P4Put(Header, 0x80000000);
    for (int Index = 0; Index < 17; ++Index) P4Put(Header, 0);
    const std::size_t NameAt = Header.size();
    for (const std::string Name : {"/Script/Engine", "Package", "Class", "DataTable", "Asset", "None"})
    { P4String(Header, Name); P4Put(Header, 123); }
    const std::size_t ImportAt = Header.size();
    for (const std::uint32_t Word : {0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 0u, UINT32_MAX, 3u, 0u}) P4Put(Header, Word);
    const std::size_t ExportAt = Header.size();
    P4Put(Header, UINT32_MAX - 1); P4Put(Header, 0); P4Put(Header, 0); P4Put(Header, 0);
    P4Put(Header, 4); P4Put(Header, 0); P4Put(Header, 1);
    P4Put(Header, 8, 8); const std::size_t PayloadOffsetAt = Header.size(); P4Put(Header, 0, 8);
    for (int Index = 0; Index < 3; ++Index) P4Put(Header, 0);
    for (int Index = 0; Index < 4; ++Index) P4Put(Header, 0);
    P4Put(Header, 0); P4Put(Header, 0); P4Put(Header, 1); P4Put(Header, UINT32_MAX);
    for (int Index = 0; Index < 4; ++Index) P4Put(Header, 0);
    P4Patch(Header, 24, Header.size()); P4Patch(Header, PayloadOffsetAt, Header.size(), 8);
    P4Patch(Header, Flags + 4, 6); P4Patch(Header, Flags + 8, NameAt);
    P4Patch(Header, Flags + 20, 1); P4Patch(Header, Flags + 24, ExportAt);
    P4Patch(Header, Flags + 28, 2); P4Patch(Header, Flags + 32, ImportAt);
    const std::vector<std::uint8_t> Body{5, 0, 0, 0, 0, 0, 0, 0, 0xc1, 0x83, 0x2a, 0x9e};
    const auto Good = ReadPackage(Header, Body);
    Check(Good.Error.Code.empty()); Check(Good.Package == "/Game/Original");
    Check(Good.Names.size() == 6); Check(Good.Imports.size() == 2); Check(Good.Exports.size() == 1);
    Check(Good.Exports[0].Flags == 1); Check(Good.Exports[0].Size == 8);
    std::string Path;
    Check(Good.Reference(-2, Path)); Check(Path == "/Script/Engine.DataTable");
    Check(Good.Reference(1, Path)); Check(Path == "Asset"); Check(Good.Reference(0, Path)); Check(Path.empty());
    Check(!Good.Reference(INT32_MIN, Path)); Check(!Good.Reference(2, Path));
    auto Cycle = Good; Cycle.Imports[0].Outer = -2; Check(!Cycle.Reference(-1, Path));
    Cycle = Good; Cycle.Exports[0].Outer = 1; Check(!Cycle.Reference(1, Path));
    for (const auto& Spans : {Good.HeaderSpans, Good.BodySpans})
    {
        std::size_t End = 0;
        for (const auto& Span : Spans) { Check(Span.Offset == End); End += Span.Size; }
        Check(End == (Spans[0].Kind == "summary-prefix" ? Header.size() : Body.size()));
    }
    for (std::size_t Size = 0; Size < Header.size(); ++Size)
    { const auto Broken = ReadPackage(std::vector<std::uint8_t>(Header.begin(), Header.begin() + Size), Body); Check(!Broken.Error.Code.empty()); }
    for (std::size_t Size = 0; Size < 8; ++Size)
        Check(!ReadPackage(Header, std::vector<std::uint8_t>(Body.begin(), Body.begin() + Size)).Error.Code.empty());
    for (const auto At : {std::size_t(0), std::size_t(4), std::size_t(8), std::size_t(12), std::size_t(16),
        std::size_t(20), std::size_t(24), Flags + 4, Flags + 8, Flags + 20, Flags + 24, Flags + 28, Flags + 32,
        ImportAt, ImportAt + 16, ExportAt, ExportAt + 12, ExportAt + 16, ExportAt + 92})
    { auto Broken = Header; P4Patch(Broken, At, UINT32_MAX - 3); Check(!ReadPackage(Broken, Body).Error.Code.empty()); }
    auto Broken = Header; P4Patch(Broken, PayloadOffsetAt, UINT64_MAX, 8); Check(!ReadPackage(Broken, Body).Error.Code.empty());
    Broken = Header; P4Patch(Broken, ExportAt + 28, UINT64_MAX, 8); Check(!ReadPackage(Broken, Body).Error.Code.empty());
    Broken = Header; P4Patch(Broken, Flags + 8, ExportAt); Check(!ReadPackage(Broken, Body).Error.Code.empty());
    for (int Trial = 0; Trial < 2000; ++Trial)
    {
        auto Mutant = Header;
        const std::size_t Index = std::size_t(Trial) % Mutant.size(); Mutant[Index] ^= std::uint8_t(1u << (Trial % 8));
        const auto Read = ReadPackage(Mutant, Body);
        if (Read.Error.Code.empty())
        {
            std::vector<FSourceSpan> Spans;
            for (const auto& Span : Read.HeaderSpans) Spans.push_back({Span.Offset, Span.Size});
            Check(CompleteSpans(Mutant.size(), Spans));
        }
    }
    std::vector<FInventorySpan> Spans{{2, 2, "known", true}};
    Check(PartitionSpans(8, Spans)); Check(Spans.size() == 3);
    Spans = {{0, 2, "a", true}, {1, 1, "b", true}}; Check(!PartitionSpans(4, Spans));
    Spans = {{5, 1, "a", true}}; Check(!PartitionSpans(4, Spans));
    Spans = {{0, 5, "a", true}}; Check(!PartitionSpans(4, Spans));
    Spans = {{0, 0, "a", true}}; Check(PartitionSpans(0, Spans));

    for (const auto& Bytes : {std::vector<std::uint8_t>{0,0,0,0}, std::vector<std::uint8_t>{1,0,0,0,0},
        std::vector<std::uint8_t>{2,0,0,0,'a',0}, std::vector<std::uint8_t>{3,0,0,0,0xc3,0xb1,0},
        std::vector<std::uint8_t>{4,0,0,0,0xe2,0x82,0xac,0}, std::vector<std::uint8_t>{5,0,0,0,0xf0,0x9f,0x90,0x9c,0},
        std::vector<std::uint8_t>{0xfe,0xff,0xff,0xff,'a',0,0,0},
        std::vector<std::uint8_t>{0xfc,0xff,0xff,0xff,0xf1,0,0xac,0x20,'b',0,0,0},
        std::vector<std::uint8_t>{0xfd,0xff,0xff,0xff,0x3d,0xd8,0x1c,0xdc,0,0}})
    { FByteReader Reader(Bytes); Check(Reader.String(Path)); Check(Reader.Error.Code.empty()); }
    for (const auto& Bytes : {std::vector<std::uint8_t>{1,0,0,0,1}, std::vector<std::uint8_t>{3,0,0,0,0,'a',0},
        std::vector<std::uint8_t>{2,0,0,0,0xff,0}, std::vector<std::uint8_t>{2,0,0,0,0xc2,0},
        std::vector<std::uint8_t>{3,0,0,0,0xc2,'a',0}, std::vector<std::uint8_t>{4,0,0,0,0xe0,0x80,0x80,0},
        std::vector<std::uint8_t>{4,0,0,0,0xed,0xa0,0x80,0}, std::vector<std::uint8_t>{5,0,0,0,0xf4,0x90,0x80,0x80,0},
        std::vector<std::uint8_t>{0xfe,0xff,0xff,0xff,0,0,0,0},
        std::vector<std::uint8_t>{0xfe,0xff,0xff,0xff,0,0xdc,0,0},
        std::vector<std::uint8_t>{0xfe,0xff,0xff,0xff,0,0xd8,0,0},
        std::vector<std::uint8_t>{0xfd,0xff,0xff,0xff,0,0xd8,'a',0,0,0},
        std::vector<std::uint8_t>{0,0,0,0x80}})
    { FByteReader Reader(Bytes); Check(!Reader.String(Path)); Check(!Reader.Error.Code.empty()); }
    const std::vector<std::uint8_t> Empty;
    std::uint64_t Number = 0;
    FByteReader Reader(Empty, 1); Check(!Reader.Skip(0));
    FByteReader Width(Empty); Check(!Width.Unsigned(0, Number)); Check(!Width.Unsigned(9, Number));
    std::vector<std::uint8_t> Numbered{4,0,0,0,2,0,0,0}; FByteReader Names(Numbered);
    Check(Names.Name(Good.Names, Path)); Check(Path == "Asset_1");
    Check(Good.SoftPackageReferences.empty());

    // Soft package references: the summary's list, and soft object paths kept as names.
    std::vector<std::uint8_t> Soft;
    P4Put(Soft, 0x9e2a83c1); P4Put(Soft, UINT32_MAX - 6);
    for (int Index = 0; Index < 5; ++Index) P4Put(Soft, 0);
    P4String(Soft, "/Game/Original");
    const std::size_t SoftFlags = Soft.size();
    P4Put(Soft, 0x80000000);
    for (int Index = 0; Index < 17; ++Index) P4Put(Soft, 0);
    const std::size_t SoftNames = Soft.size();
    const std::vector<std::string> Words{"None", "/Game/Listed", "/Game/Path/Asset.Asset", "/Game/Original.Original",
        "/Script/Engine.Actor", "/Game/Path/Asset", "Plain.Word", "/Game/Map.Map:PersistentLevel"};
    for (const auto& Word : Words) { P4String(Soft, Word); P4Put(Soft, 0); }
    const std::size_t ListAt = Soft.size();
    P4Put(Soft, 1); P4Put(Soft, 0); P4Put(Soft, 1); P4Put(Soft, 0);
    P4Patch(Soft, 24, Soft.size());
    P4Patch(Soft, SoftFlags + 4, Words.size()); P4Patch(Soft, SoftFlags + 8, SoftNames);
    P4Patch(Soft, SoftFlags + 40, 2); P4Patch(Soft, SoftFlags + 44, ListAt);
    const auto SoftRead = ReadPackageHeader(Soft, 0);
    Check(SoftRead.Error.Code.empty());
    Check((SoftRead.SoftPackageReferences == std::vector<std::string>{"/Game/Listed", "/Game/Map", "/Game/Path/Asset"}));
    auto SoftBroken = Soft; P4Patch(SoftBroken, SoftFlags + 40, 3); Check(!ReadPackageHeader(SoftBroken, 0).Error.Code.empty());
    SoftBroken = Soft; P4Patch(SoftBroken, ListAt, 99); Check(!ReadPackageHeader(SoftBroken, 0).Error.Code.empty());
}
static void CheckPropertyReaders()
{
    using namespace P4MapKit;
    const std::vector<std::string> Names{"Value", "None", "IntProperty", "BoolProperty", "StructProperty", "Vector",
        "ByteProperty", "EnumProperty", "ArrayProperty", "SetProperty", "MapProperty", "NameProperty", "GhostProperty"};
    const auto Name = [&](std::vector<std::uint8_t>& Bytes, const std::string& Text, std::uint32_t Number = 0) {
        const auto At = std::find(Names.begin(), Names.end(), Text);
        P4Put(Bytes, std::size_t(At - Names.begin())); P4Put(Bytes, Number);
    };
    const auto Tag = [&](const std::string& Type, bool Guid = false) {
        std::vector<std::uint8_t> Bytes;
        Name(Bytes, "Value"); Name(Bytes, Type);
        P4Put(Bytes, Type == "BoolProperty" ? 0 : 4); P4Put(Bytes, 0);
        if (Type == "BoolProperty") Bytes.push_back(1);
        else if (Type == "StructProperty") { Name(Bytes, "Vector"); for (int Word = 0; Word < 4; ++Word) P4Put(Bytes, Word + 1); }
        else if (Type == "ByteProperty" || Type == "EnumProperty" || Type == "ArrayProperty" || Type == "SetProperty") Name(Bytes, "NameProperty");
        else if (Type == "MapProperty") { Name(Bytes, "NameProperty"); Name(Bytes, "IntProperty"); }
        Bytes.push_back(Guid ? 1 : 0);
        if (Guid) for (int Word = 0; Word < 4; ++Word) P4Put(Bytes, Word + 5);
        if (Type != "BoolProperty") P4Put(Bytes, 42);
        Name(Bytes, "None"); return Bytes;
    };
    for (const std::string Type : {"IntProperty", "BoolProperty", "StructProperty", "ByteProperty", "EnumProperty", "ArrayProperty", "SetProperty", "MapProperty"})
    {
        const auto Bytes = Tag(Type, true);
        const auto Read = ReadPropertyEnvelopes(Bytes, Names, 0, Bytes.size());
        Check(Read.Error.Code.empty()); Check(Read.Properties.size() == 1); Check(Read.End == Bytes.size());
        Check(Read.Properties[0].Type == Type); Check(Read.Properties[0].HasGuid);
        Check(Read.Properties[0].PropertyGuid[3] == 8);
        for (std::size_t Size = 0; Size < Bytes.size(); ++Size)
            Check(!ReadPropertyEnvelopes(Bytes, Names, 0, Size).Error.Code.empty());
    }
    auto Bytes = Tag("IntProperty");
    auto Duplicate = Bytes; Duplicate.resize(Duplicate.size() - 8); Duplicate.insert(Duplicate.end(), Bytes.begin(), Bytes.end());
    Check(ReadPropertyEnvelopes(Duplicate, Names, 0, Duplicate.size()).Error.Code == "duplicate-property");
    for (const auto At : {std::size_t(0), std::size_t(8), std::size_t(16), std::size_t(20)})
    { auto Broken = Bytes; P4Patch(Broken, At, UINT32_MAX); Check(!ReadPropertyEnvelopes(Broken, Names, 0, Broken.size()).Error.Code.empty()); }
    auto Broken = Bytes; Broken[24] = 2;
    Check(ReadPropertyEnvelopes(Broken, Names, 0, Broken.size()).Error.Code == "invalid-property-guid-flag");
    Broken = Tag("BoolProperty"); Broken[24] = 2;
    Check(ReadPropertyEnvelopes(Broken, Names, 0, Broken.size()).Error.Code == "invalid-bool-tag");
    Broken = Tag("BoolProperty"); P4Patch(Broken, 16, 1);
    Check(ReadPropertyEnvelopes(Broken, Names, 0, Broken.size()).Error.Code == "invalid-bool-tag");
    Broken = Tag("GhostProperty");
    Check(ReadPropertyEnvelopes(Broken, Names, 0, Broken.size()).Error.Code == "unsupported-property-tag");
    std::vector<std::uint8_t> Many;
    for (std::uint32_t Index = 0; Index < 100000; ++Index)
    { Name(Many, "Value", Index); Name(Many, "IntProperty"); P4Put(Many, 0); P4Put(Many, 0); Many.push_back(0); }
    Check(ReadPropertyEnvelopes(Many, Names, 0, Many.size()).Error.Code == "property-count-limit");
}

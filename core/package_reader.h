// Read-only legacy UE4.26 package inventory. Raw input remains owned by the caller.
// Decoding a directory or a tag header does not qualify its payload as editable.
#pragma once
#include "quality.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <unordered_map>

namespace P4MapKit
{
struct FReadError { std::size_t Offset = 0; std::string Code; };
class FByteReader
{
    const std::vector<std::uint8_t>& Data;
    std::size_t Position;
    std::size_t End;
public:
    FReadError Error;
    FByteReader(const std::vector<std::uint8_t>& Bytes, std::size_t Start = 0,
        std::size_t Stop = std::numeric_limits<std::size_t>::max())
        : Data(Bytes), Position(Start), End(Stop == std::numeric_limits<std::size_t>::max() ? Bytes.size() : Stop)
    { if (Start > End || End > Data.size()) Reject("invalid-bounds"); }
    bool Reject(const char* Code) { if (Error.Code.empty()) Error = {Position, Code}; return false; }
    std::size_t Tell() const { return Position; }
    std::size_t Limit() const { return End; }
    bool Skip(std::size_t Size)
    {
        if (!Error.Code.empty() || Position > End || Size > End - Position) return Reject("truncated");
        Position += Size; return true;
    }
    bool Unsigned(std::size_t Width, std::uint64_t& Value)
    {
        if (Width == 0 || Width > 8) return Reject("invalid-integer-width");
        const std::size_t At = Position;
        if (!Skip(Width)) return false;
        Value = 0;
        for (std::size_t Byte = 0; Byte < Width; ++Byte)
            Value |= std::uint64_t(Data[At + Byte]) << (Byte * 8);
        return true;
    }
    bool I32(std::int32_t& Value)
    {
        std::uint64_t Bits = 0;
        if (!Unsigned(4, Bits)) return false;
        Value = Bits <= std::uint64_t(INT32_MAX) ? std::int32_t(Bits) : -1 - std::int32_t(UINT32_MAX - Bits);
        return true;
    }
    bool U32(std::uint32_t& Value)
    { std::uint64_t Bits = 0; if (!Unsigned(4, Bits)) return false; Value = std::uint32_t(Bits); return true; }
    bool Count(std::size_t& Value, std::size_t Maximum = 100000)
    {
        std::int32_t Number = 0;
        if (!I32(Number)) return false;
        if (Number < 0 || std::uint64_t(Number) > Maximum) return Reject("invalid-count");
        Value = std::size_t(Number); return true;
    }
    bool String(std::string& Value)
    {
        std::int32_t Length = 0;
        if (!I32(Length)) return false;
        if (Length == 0) { Value.clear(); return true; }
        const std::uint64_t Units = Length < 0 ? std::uint64_t(-std::int64_t(Length)) : std::uint64_t(Length);
        if (Units > 1000000) return Reject("oversized-string");
        const std::size_t Width = Length < 0 ? 2 : 1;
        const std::size_t At = Position;
        if (!Skip(std::size_t(Units) * Width)) return false;
        for (std::size_t Byte = 0; Byte < Width; ++Byte)
            if (Data[Position - Width + Byte] != 0) return Reject("unterminated-string");
        Value.clear();
        if (Width == 1)
        {
            Value.assign(reinterpret_cast<const char*>(Data.data() + At), std::size_t(Units - 1));
            for (std::size_t Index = 0; Index < Value.size();)
            {
                const auto First = std::uint8_t(Value[Index++]);
                if (First == 0) return Reject("embedded-nul");
                if (First < 0x80) continue;
                const int Extra = First >= 0xc2 && First <= 0xdf ? 1 : First >= 0xe0 && First <= 0xef ? 2 :
                    First >= 0xf0 && First <= 0xf4 ? 3 : -1;
                if (Extra < 0 || std::size_t(Extra) > Value.size() - Index) return Reject("invalid-utf8");
                std::uint32_t Code = First & ((1u << (6 - Extra)) - 1);
                for (int Byte = 0; Byte < Extra; ++Byte)
                {
                    const auto Next = std::uint8_t(Value[Index++]);
                    if ((Next & 0xc0) != 0x80) return Reject("invalid-utf8");
                    Code = (Code << 6) | (Next & 0x3f);
                }
                if (Code < (Extra == 1 ? 0x80u : Extra == 2 ? 0x800u : 0x10000u) ||
                    Code > 0x10ffff || (Code >= 0xd800 && Code <= 0xdfff)) return Reject("invalid-utf8");
            }
            return true;
        }
        for (std::size_t Unit = 0; Unit + 1 < Units; ++Unit)
        {
            const auto ReadUnit = [&](std::size_t Index) {
                return std::uint32_t(Data[At + Index * 2]) | (std::uint32_t(Data[At + Index * 2 + 1]) << 8);
            };
            std::uint32_t Code = ReadUnit(Unit);
            if (Code == 0) return Reject("embedded-nul");
            if (Code >= 0xd800 && Code <= 0xdbff)
            {
                if (++Unit + 1 >= Units) return Reject("invalid-utf16");
                const std::uint32_t Low = ReadUnit(Unit);
                if (Low < 0xdc00 || Low > 0xdfff) return Reject("invalid-utf16");
                Code = 0x10000 + ((Code - 0xd800) << 10) + Low - 0xdc00;
            }
            else if (Code >= 0xdc00 && Code <= 0xdfff) return Reject("invalid-utf16");
            if (Code < 0x80) Value.push_back(char(Code));
            else
            {
                if (Code >= 0x10000) Value.push_back(char(0xf0 | (Code >> 18)));
                if (Code >= 0x800) Value.push_back(char((Code >= 0x10000 ? 0x80 : 0xe0) | ((Code >> 12) & 0x3f)));
                Value.push_back(char((Code >= 0x800 ? 0x80 : 0xc0) | ((Code >> 6) & 0x3f)));
                Value.push_back(char(0x80 | (Code & 0x3f)));
            }
        }
        return true;
    }
    bool Name(const std::vector<std::string>& Names, std::string& Value)
    {
        std::int32_t Index = 0, Number = 0;
        if (!I32(Index) || !I32(Number)) return false;
        if (Index < 0 || std::size_t(Index) >= Names.size() || Number < 0) return Reject("invalid-name");
        Value = Names[std::size_t(Index)] + (Number ? "_" + std::to_string(Number - 1) : ""); return true;
    }
};

struct FPackageImport { std::string ClassPackage, Class, Name; std::int32_t Outer = 0; };
struct FPackageExport
{
    std::string Name;
    std::int32_t Class = 0, Super = 0, Template = 0, Outer = 0;
    std::uint32_t Flags = 0, PackageFlags = 0;
    std::array<std::uint32_t, 4> Guid{};
    std::array<std::int32_t, 3> Filters{};
    std::int32_t NotAlwaysLoaded = 0, IsAsset = 0, FirstDependency = -1;
    std::array<std::size_t, 4> DependencyCounts{};
    std::size_t Offset = 0, Size = 0;
};
struct FInventorySpan { std::size_t Offset = 0, Size = 0; std::string Kind; bool Decoded = false; };
struct FPackageInventory
{
    std::string Package;
    std::int32_t LegacyVersion = 0, UE3Version = 0, UE4Version = 0, LicenseeVersion = 0;
    std::uint32_t Flags = 0;
    std::size_t HeaderSize = 0;
    // Optional independently bounded observation of the cooked UE4.26 summary
    // tail. Header census still works when that otherwise opaque tail is not
    // qualified; bulk readers must not invent its missing offset base.
    std::int64_t BulkDataStartOffset = 0;
    bool HasBulkDataStartOffset = false;
    FReadError BulkDataSummaryError;
    std::vector<std::string> Names;
    std::vector<FPackageImport> Imports;
    std::vector<FPackageExport> Exports;
    // Optional original-Zen identities verified against these exact converted
    // bytes. Legacy parsing never fills this map by guessing from names.
    std::map<std::int32_t,std::string> RecoveredImportReferences;
    // Packages named by soft reference: the summary's soft package reference list, and the package of
    // every soft object path in the name map. A cooked UE 4.26 package leaves that list empty and keeps
    // each soft object path as a name ("/Game/Folder/Asset.Asset"). Sorted, unique, without this package
    // and without script packages.
    std::vector<std::string> SoftPackageReferences;
    std::vector<FInventorySpan> HeaderSpans, BodySpans;
    FReadError Error;
    bool Reference(std::int32_t Index, std::string& Path) const
    {
        if(Index<0) { const auto Recovered=RecoveredImportReferences.find(Index); if(Recovered!=RecoveredImportReferences.end()) { Path=Recovered->second; return true; } }
        std::vector<std::string> Parts;
        std::unordered_set<std::int32_t> Seen;
        std::size_t Characters = 0;
        while (Index != 0)
        {
            if (!Seen.insert(Index).second || Seen.size() > 4096) return false;
            const std::uint64_t Slot = Index < 0 ? std::uint64_t(-std::int64_t(Index) - 1) : std::uint64_t(Index - 1);
            if (Slot >= (Index < 0 ? Imports.size() : Exports.size())) return false;
            const auto& Name = Index < 0 ? Imports[std::size_t(Slot)].Name : Exports[std::size_t(Slot)].Name;
            if (Name.size() >= 64 * 1024 * 1024 - Characters) return false;
            Characters += Name.size() + 1; Parts.push_back(Name);
            Index = Index < 0 ? Imports[std::size_t(Slot)].Outer : Exports[std::size_t(Slot)].Outer;
        }
        Path.clear();
        for (auto At = Parts.rbegin(); At != Parts.rend(); ++At) { if (!Path.empty()) Path += '.'; Path += *At; }
        return true;
    }
};

struct FPropertyEnvelope
{
    std::string Name, Type;
    std::vector<std::string> Detail;
    std::size_t TagOffset = 0, PayloadOffset = 0, Size = 0, ArrayIndex = 0;
    bool BoolValue = false, HasGuid = false;
    std::array<std::uint32_t, 4> StructGuid{}, PropertyGuid{};
};
struct FPropertyInventory
{
    std::vector<FPropertyEnvelope> Properties;
    std::size_t End = 0;
    FReadError Error;
};
// Reads one envelope, including its payload span; None is the list terminator.
// Shared by ordinary tagged lists and the inner descriptor of struct arrays.
inline bool ReadPropertyEnvelope(FByteReader& Reader, const std::vector<std::string>& Names,
    std::size_t MaximumPayload, FPropertyEnvelope& Property)
{
        Property.TagOffset = Reader.Tell();
        if (!Reader.Name(Names, Property.Name)) return false;
        if (Property.Name == "None") return true;
        if (!Reader.Name(Names, Property.Type) || !Reader.Count(Property.Size, MaximumPayload) ||
            !Reader.Count(Property.ArrayIndex, 1000000)) return false;
        std::size_t DetailCount = 0;
        if (Property.Type == "StructProperty" || Property.Type == "ByteProperty" || Property.Type == "EnumProperty" ||
            Property.Type == "ArrayProperty" || Property.Type == "SetProperty") DetailCount = 1;
        else if (Property.Type == "MapProperty") DetailCount = 2;
        else if (Property.Type == "BoolProperty")
        {
            std::uint64_t Value = 0;
            if (!Reader.Unsigned(1, Value)) return false;
            if (Value > 1 || Property.Size != 0) return Reader.Reject("invalid-bool-tag");
            Property.BoolValue = Value != 0;
        }
        else
        {
            static const std::unordered_set<std::string> Plain{"IntProperty", "Int8Property", "Int16Property", "Int64Property",
                "UInt16Property", "UInt32Property", "UInt64Property", "FloatProperty", "DoubleProperty", "NameProperty",
                "ObjectProperty", "ClassProperty", "WeakObjectProperty", "SoftObjectProperty", "SoftClassProperty",
                "LazyObjectProperty", "FieldPathProperty", "StrProperty", "TextProperty", "InterfaceProperty",
                "DelegateProperty", "MulticastDelegateProperty", "MulticastSparseDelegateProperty", "MulticastInlineDelegateProperty"};
            if (Plain.count(Property.Type) == 0) return Reader.Reject("unsupported-property-tag");
        }
        for (std::size_t Detail = 0; Detail < DetailCount; ++Detail)
        {
            std::string Name;
            if (!Reader.Name(Names, Name)) return false;
            Property.Detail.push_back(std::move(Name));
        }
        if (Property.Type == "StructProperty")
            for (auto& Word : Property.StructGuid) if (!Reader.U32(Word)) return false;
        std::uint64_t Guid = 0;
        if (!Reader.Unsigned(1, Guid)) return false;
        if (Guid > 1) return Reader.Reject("invalid-property-guid-flag");
        Property.HasGuid = Guid != 0;
        if (Property.HasGuid)
            for (auto& Word : Property.PropertyGuid) if (!Reader.U32(Word)) return false;
        Property.PayloadOffset = Reader.Tell();
        return Reader.Skip(Property.Size);
}
inline FPropertyInventory ReadPropertyEnvelopes(const std::vector<std::uint8_t>& Body,
    const std::vector<std::string>& Names, std::size_t Start, std::size_t End)
{
    FPropertyInventory Result;
    FByteReader Reader(Body, Start, End);
    std::unordered_set<std::string> Seen;
    std::size_t TagText = 0;
    const auto Failed = [&]() { Result.Error = Reader.Error; return Result; };
    for (std::size_t Count = 0; Count < 100000; ++Count)
    {
        FPropertyEnvelope Property;
        if (!ReadPropertyEnvelope(Reader, Names, Body.size(), Property)) return Failed();
        if (Property.Name == "None") { Result.End = Reader.Tell(); return Result; }
        std::size_t Characters = Property.Name.size() + Property.Type.size();
        for (const auto& Detail : Property.Detail) Characters += Detail.size();
        if (Characters > 64 * 1024 * 1024 - TagText) { Reader.Reject("property-tag-text-budget"); return Failed(); }
        TagText += Characters;
        std::string Identity = Property.Name;
        // Full Unicode FName identity remains an engine/schema admission check.
        for (auto& Character : Identity) if (Character >= 'A' && Character <= 'Z') Character += 'a' - 'A';
        Identity += '[' + std::to_string(Property.ArrayIndex) + ']';
        if (!Seen.insert(Identity).second) { Reader.Reject("duplicate-property"); return Failed(); }
        Result.Properties.push_back(std::move(Property));
    }
    Reader.Reject("property-count-limit"); return Failed();
}

inline bool PartitionSpans(std::size_t Size, std::vector<FInventorySpan>& Spans)
{
    std::sort(Spans.begin(), Spans.end(), [](const FInventorySpan& A, const FInventorySpan& B) { return A.Offset < B.Offset; });
    std::vector<FInventorySpan> Full;
    std::size_t End = 0;
    for (const auto& Span : Spans)
    {
        if (!Span.Size) continue;
        if (Span.Offset < End || Span.Offset > Size || Span.Size > Size - Span.Offset) return false;
        if (Span.Offset > End) Full.push_back({End, Span.Offset - End, "uninterpreted", false});
        Full.push_back(Span); End = Span.Offset + Span.Size;
    }
    if (End < Size) Full.push_back({End, Size - End, "uninterpreted", false});
    Spans = std::move(Full); return true;
}

// Header/import/export census using the actual companion file length, without loading its payload.
// This validates every recorded export span; it does not decode or qualify body data.
inline FPackageInventory ReadPackageHeader(const std::vector<std::uint8_t>& Header, std::size_t BodySize)
{
    FPackageInventory Result;
    FByteReader Reader(Header);
    const auto Failed = [&]() { Result.Error = Reader.Error; return Result; };
    std::uint32_t Magic = 0;
    if (Header.size() > 64 * 1024 * 1024)
    { Reader.Reject("package-streaming-required"); return Failed(); }
    if (BodySize > std::size_t(std::numeric_limits<std::int64_t>::max()))
    { Reader.Reject("package-body-size-out-of-range"); return Failed(); }
    if (!Reader.U32(Magic) || !Reader.I32(Result.LegacyVersion) || !Reader.I32(Result.UE3Version) ||
        !Reader.I32(Result.UE4Version) || !Reader.I32(Result.LicenseeVersion)) return Failed();
    // Versioned stock headers need version-dependent records, not guessed layouts.
    if (Magic != 0x9e2a83c1 || Result.LegacyVersion != -7 || Result.UE3Version != 0 ||
        Result.UE4Version != 0 || Result.LicenseeVersion != 0)
    { Reader.Reject("unsupported-legacy-version"); return Failed(); }
    std::size_t CustomCount = 0;
    if (!Reader.Count(CustomCount, 1000) || !Reader.Skip(CustomCount * 20) ||
        !Reader.Count(Result.HeaderSize, Header.size()) || !Reader.String(Result.Package) || !Reader.U32(Result.Flags)) return Failed();
    if (Result.HeaderSize != Header.size()) { Reader.Reject("header-size-mismatch"); return Failed(); }
    std::size_t NameCount = 0, ExportCount = 0, ImportCount = 0, SoftCount = 0;
    std::uint32_t NamesAt = 0, ExportsAt = 0, ImportsAt = 0, SoftAt = 0;
    // After the imports: depends offset, soft package reference count and offset, searchable names
    // offset, thumbnail table offset and the package guid.
    if (!Reader.Count(NameCount, std::min(std::size_t(1000000), Header.size() / 8)) || !Reader.U32(NamesAt) || !Reader.Skip(8) ||
        !Reader.Count(ExportCount, std::min(std::size_t(100000), Header.size() / 104)) || !Reader.U32(ExportsAt) ||
        !Reader.Count(ImportCount, std::min(std::size_t(100000), Header.size() / 28)) || !Reader.U32(ImportsAt) || !Reader.Skip(4) ||
        !Reader.Count(SoftCount, std::min(std::size_t(100000), Header.size() / 8)) || !Reader.U32(SoftAt) || !Reader.Skip(24)) return Failed();
    // Keep the original census independent of the optional summary tail. Only
    // filtered cooked -7/unversioned UE4.26 headers above establish this layout.
    // Native SaveBulkData stores legacy offsets relative to BulkDataStartOffset;
    // NoOffsetFixUp records instead carry their own file-relative offsets.
    Result.HeaderSpans.push_back({0, Reader.Tell(), "summary-prefix", false});
    if (Result.Flags & 0x80000000u) // PKG_FilterEditorOnly: no persistent/owner GUIDs.
    {
        std::size_t SummaryLimit = Header.size();
        for (const auto& Table : {std::make_pair(NameCount, NamesAt), std::make_pair(ExportCount, ExportsAt),
            std::make_pair(ImportCount, ImportsAt), std::make_pair(SoftCount, SoftAt)})
            if (Table.first) SummaryLimit = std::min(SummaryLimit, std::size_t(Table.second));
        FByteReader Tail(Header, Reader.Tell(), SummaryLimit);
        const auto ReadTail = [&]() {
            std::size_t Count = 0;
            if (!Tail.Count(Count, 1000) || !Tail.Skip(Count * 8)) return false; // Generations.
            for (int Version = 0; Version < 2; ++Version)
            {
                std::string Branch;
                // SavedBy/Compatible FEngineVersion: three uint16, uint32, FString.
                if (!Tail.Skip(10) || !Tail.String(Branch)) return false;
            }
            std::uint32_t Compression = 0;
            if (!Tail.U32(Compression) || !Tail.Count(Count, 100000)) return false;
            if (Compression != 0 || Count != 0) return Tail.Reject("package-summary-compression-unqualified");
            if (!Tail.Skip(4) || !Tail.Count(Count, 1000)) return false; // PackageSource, additional packages.
            for (std::size_t Index = 0; Index < Count; ++Index)
            { std::string Package; if (!Tail.String(Package)) return false; }
            std::uint64_t BulkStart = 0;
            if (!Tail.Skip(4) || !Tail.Unsigned(8, BulkStart)) return false; // AssetRegistryDataOffset, bulk base.
            if (BulkStart > std::uint64_t(INT64_MAX) || BulkStart > std::uint64_t(Header.size()) + BodySize)
                return Tail.Reject("invalid-package-bulk-offset-base");
            // Complete the known summary tail so table bytes cannot be mistaken
            // for a truncated summary's apparently plausible bulk offset.
            if (!Tail.Skip(4) || !Tail.Count(Count, 100000) || !Tail.Skip(Count * 4) || !Tail.Skip(8)) return false;
            Result.BulkDataStartOffset = std::int64_t(BulkStart);
            return true;
        };
        Result.HasBulkDataStartOffset = ReadTail();
        if (!Result.HasBulkDataStartOffset) Result.BulkDataSummaryError = Tail.Error;
    }
    else Result.BulkDataSummaryError = {Reader.Tell(), "package-summary-layout-unqualified"};
    FByteReader Names(Header, NamesAt);
    for (std::size_t Index = 0; Index < NameCount; ++Index)
    {
        std::string Name;
        if (!Names.String(Name) || !Names.Skip(4)) { Result.Error = Names.Error; return Result; }
        Result.Names.push_back(std::move(Name));
    }
    if (!Names.Error.Code.empty()) { Result.Error = Names.Error; return Result; }
    Result.HeaderSpans.push_back({NamesAt, Names.Tell() - NamesAt, "names-and-hashes", true});
    FByteReader Imports(Header, ImportsAt, std::size_t(ImportsAt) + ImportCount * 28);
    for (std::size_t Index = 0; Index < ImportCount; ++Index)
    {
        FPackageImport Item;
        if (!Imports.Name(Result.Names, Item.ClassPackage) || !Imports.Name(Result.Names, Item.Class) ||
            !Imports.I32(Item.Outer) || !Imports.Name(Result.Names, Item.Name)) { Result.Error = Imports.Error; return Result; }
        Result.Imports.push_back(std::move(Item));
    }
    if (!Imports.Error.Code.empty()) { Result.Error = Imports.Error; return Result; }
    Result.HeaderSpans.push_back({ImportsAt, ImportCount * 28, "imports", true});
    FByteReader Exports(Header, ExportsAt, std::size_t(ExportsAt) + ExportCount * 104);
    for (std::size_t Index = 0; Index < ExportCount; ++Index)
    {
        FPackageExport Item;
        std::uint64_t Size = 0, Offset = 0;
        bool Good = Exports.I32(Item.Class) && Exports.I32(Item.Super) && Exports.I32(Item.Template) &&
            Exports.I32(Item.Outer) && Exports.Name(Result.Names, Item.Name) && Exports.U32(Item.Flags) &&
            Exports.Unsigned(8, Size) && Exports.Unsigned(8, Offset);
        for (auto& Filter : Item.Filters) Good = Good && Exports.I32(Filter);
        for (auto& Word : Item.Guid) Good = Good && Exports.U32(Word);
        Good = Good && Exports.U32(Item.PackageFlags) && Exports.I32(Item.NotAlwaysLoaded) &&
            Exports.I32(Item.IsAsset) && Exports.I32(Item.FirstDependency);
        for (auto& Count : Item.DependencyCounts) Good = Good && Exports.Count(Count, 10000000);
        if (!Good) { Result.Error = Exports.Error; return Result; }
        if (Offset > std::uint64_t(std::numeric_limits<std::int64_t>::max()) || Size > std::uint64_t(std::numeric_limits<std::int64_t>::max()) ||
            Offset < Header.size() || Offset - Header.size() > BodySize || Size > BodySize - (Offset - Header.size()))
        { Result.Error = {Exports.Tell(), "export-payload-bounds"}; return Result; }
        Item.Offset = std::size_t(Offset - Header.size()); Item.Size = std::size_t(Size);
        Result.BodySpans.push_back({Item.Offset, Item.Size, "export-payload", false});
        Result.Exports.push_back(std::move(Item));
    }
    if (!Exports.Error.Code.empty()) { Result.Error = Exports.Error; return Result; }
    Result.HeaderSpans.push_back({ExportsAt, ExportCount * 104, "exports", true});
    // UE 4.26 writes each soft package reference as a name (index and number).
    FByteReader Soft(Header, SoftAt, std::size_t(SoftAt) + SoftCount * 8);
    std::vector<std::string> SoftPackages;
    for (std::size_t Index = 0; Index < SoftCount; ++Index)
    {
        std::string Name;
        if (!Soft.Name(Result.Names, Name)) { Result.Error = Soft.Error; return Result; }
        SoftPackages.push_back(std::move(Name));
    }
    if (!Soft.Error.Code.empty()) { Result.Error = Soft.Error; return Result; }
    Result.HeaderSpans.push_back({SoftAt, SoftCount * 8, "soft-package-references", true});
    for (const auto& Name : Result.Names)
    {
        const std::size_t Dot = Name.find('.');
        if (Name.size() > 1 && Name[0] == '/' && Dot != std::string::npos && Dot > 1 && Name.find(':') > Dot)
            SoftPackages.push_back(Name.substr(0, Dot));
    }
    for (auto& Name : SoftPackages)
        if (Name.size() > 1 && Name[0] == '/' && Name != Result.Package && Name.compare(0, 8, "/Script/") != 0 &&
            Name.find('.') == std::string::npos && Name.find('/', 1) != std::string::npos)
            Result.SoftPackageReferences.push_back(std::move(Name));
    std::sort(Result.SoftPackageReferences.begin(), Result.SoftPackageReferences.end());
    Result.SoftPackageReferences.erase(std::unique(Result.SoftPackageReferences.begin(), Result.SoftPackageReferences.end()),
        Result.SoftPackageReferences.end());
    // The entire summary tail is preserved as opaque until its own reader qualifies.
    std::string Reference;
    for (const auto& Item : Result.Imports)
        if (!Result.Reference(Item.Outer, Reference)) { Result.Error = {ImportsAt, "invalid-import-outer"}; return Result; }
    for (const auto& Item : Result.Exports)
        for (const auto Index : {Item.Class, Item.Super, Item.Template, Item.Outer})
            if (!Result.Reference(Index, Reference)) { Result.Error = {ExportsAt, "invalid-export-reference"}; return Result; }
    if (!PartitionSpans(Header.size(), Result.HeaderSpans) || !PartitionSpans(BodySize, Result.BodySpans))
        Result.Error = {0, "overlapping-package-spans"};
    return Result;
}

inline FPackageInventory ReadPackage(const std::vector<std::uint8_t>& Header,
    const std::vector<std::uint8_t>& Body)
{
    // Preserve the full reader's existing input-size refusal, including its offset and precedence.
    if (Body.size() > 64 * 1024 * 1024)
    {
        FPackageInventory Result;
        Result.Error = {0, "package-streaming-required"};
        return Result;
    }
    return ReadPackageHeader(Header, Body.size());
}
}

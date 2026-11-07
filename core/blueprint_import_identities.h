// Import identities for retoc's unresolved UE4 Initial package imports, read from the
// original Zen package. This never supplies defaults or changes null/script references.
#pragma once
#include "blueprint_json.h"
#include "package_reader.h"
#include "custom_material_hash.h"

namespace P4MapKit
{
namespace Blueprint
{
using FImportIdentityBytes = std::vector<std::uint8_t>;
using FImportIdentityHash = std::function<std::uint64_t(const std::string&)>;
class FVerifiedImportIdentities;
FVerifiedImportIdentities RecoverImportIdentities(const FImportIdentityBytes&, const FImportIdentityBytes&,
                                                  const FImportIdentityBytes&, const FImportIdentityBytes&,
                                                  const std::string&, const FImportIdentityHash&);

namespace ImportIdentityDetail
{
inline void Need(bool Condition, const char* Why)
{
    if (!Condition)
        throw Error(std::string("import-identities-") + Why);
}
inline std::uint64_t Read(const FImportIdentityBytes& Bytes, std::size_t At, std::size_t Width)
{
    Need(Width && Width <= 8 && At <= Bytes.size() && Width <= Bytes.size() - At, "raw-bounds");
    std::uint64_t Out = 0;
    for (std::size_t I = 0; I < Width; ++I)
        Out |= std::uint64_t(Bytes[At + I]) << (8 * I);
    return Out;
}
inline void Keys(const J& Value, std::initializer_list<const char*> Expected)
{
    Need(Value.Kind == J::K::Object && Value.Fields.size() == Expected.size(), "schema");
    for (const auto* Key : Expected)
        Need(Value.Has(Key), "schema");
}
inline bool Character(char C)
{
    return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
}
inline bool PackagePath(const std::string& Name)
{
    if (Name.size() > 4096 || (Name.compare(0, 6, "/Game/") && Name.compare(0, 8, "/Engine/")))
        return false;
    const auto Start = Name.compare(0, 6, "/Game/") == 0 ? 6u : 8u;
    bool Part = false;
    for (std::size_t I = Start; I < Name.size(); ++I)
    {
        if (Name[I] == '/')
        {
            if (!Part)
                return false;
            Part = false;
        }
        else
        {
            if (!Character(Name[I]))
                return false;
            Part = true;
        }
    }
    return Part;
}
inline std::string Normalize(std::string Name)
{
    for (char& C : Name)
    {
        if (C == '.' || C == ':')
            C = '/';
        else if (C >= 'A' && C <= 'Z')
            C = char(C + 'a' - 'A');
    }
    return Name;
}
inline std::string Hex64(std::uint64_t Value)
{
    std::string Out(16, '0');
    const char* Digits = "0123456789abcdef";
    for (int I = 15; I >= 0; --I)
    {
        Out[std::size_t(I)] = Digits[Value & 15];
        Value >>= 4;
    }
    return Out;
}
inline std::vector<std::string> ReadNames(const FImportIdentityBytes& Raw, std::size_t At, std::size_t Size)
{
    const auto Start = At, End = At + Size;
    Need(At >= 64 && At < End && End <= Raw.size(), "name-range");
    std::vector<std::string> Out;
    while (At < End)
    {
        Need(End - At >= 2, "name-header");
        const auto Header = (std::uint16_t(Raw[At]) << 8) | Raw[At + 1];
        At += 2;
        const bool Wide = (Header & 0x8000) != 0;
        const auto Count = std::size_t(Header & 0x7fff);
        Need(Count && Count <= 1024, "name-length");
        if (Wide && ((At - Start) & 1))
        {
            Need(At < End && Raw[At] == 0, "name-padding");
            ++At;
        }
        const auto Width = Wide ? 2u : 1u;
        Need(Count * Width <= End - At, "name-payload");
        // Reuse the strict legacy UTF-8/UTF-16 decoder, adding its terminator.
        FImportIdentityBytes Encoded(4);
        const auto Signed = Wide ? -std::int32_t(Count + 1) : std::int32_t(Count + 1);
        for (unsigned I = 0; I < 4; ++I)
            Encoded[I] = std::uint8_t(std::uint32_t(Signed) >> (8 * I));
        Encoded.insert(Encoded.end(), Raw.begin() + At, Raw.begin() + At + Count * Width);
        Encoded.insert(Encoded.end(), Width, 0);
        FByteReader Reader(Encoded);
        std::string Name;
        Need(Reader.String(Name), "name-encoding");
        Out.push_back(std::move(Name));
        At += Count * Width;
        Need(Out.size() <= 100000, "name-count");
    }
    return Out;
}
} // namespace ImportIdentityDetail

class FVerifiedImportIdentities
{
    std::string PackageName, HeaderDigest, BodyDigest, OriginalDigest, MetadataDigest;
    std::map<std::int32_t, std::string> Paths;
    static std::string Hash(const FImportIdentityBytes& Bytes)
    {
        return CustomMaterial::Hex(CustomMaterial::Sha256(Bytes));
    }
    FVerifiedImportIdentities(const std::string& Package, std::map<std::int32_t, std::string> Identities,
                              const FImportIdentityBytes& Header, const FImportIdentityBytes& Body,
                              const FImportIdentityBytes& Original, const FImportIdentityBytes& Metadata)
        : PackageName(Package), HeaderDigest(Hash(Header)), BodyDigest(Hash(Body)), OriginalDigest(Hash(Original)),
          MetadataDigest(Hash(Metadata)), Paths(std::move(Identities))
    {
    }
    friend FVerifiedImportIdentities RecoverImportIdentities(const FImportIdentityBytes&, const FImportIdentityBytes&,
                                                             const FImportIdentityBytes&, const FImportIdentityBytes&,
                                                             const std::string&, const FImportIdentityHash&);

  public:
    const std::string& Package() const
    {
        return PackageName;
    }
    const std::map<std::int32_t, std::string>& Imports() const
    {
        return Paths;
    }
    bool MatchesSource(const FImportIdentityBytes& Header, const FImportIdentityBytes& Body) const
    {
        return HeaderDigest == Hash(Header) && BodyDigest == Hash(Body);
    }
    const std::string& OriginalZenSha256() const
    {
        return OriginalDigest;
    }
    const std::string& OriginalMetadataSha256() const
    {
        return MetadataDigest;
    }
};

namespace ImportIdentityDetail
{
// Internal result only. All consumers below invoke the raw-byte validator;
// this aggregate is never accepted as external provenance or coverage authority.
struct FLegacyZenCorrespondence
{
    FPackageInventory Legacy;
    std::vector<std::string> Names;
    std::vector<std::uint64_t> OriginalImports;
    std::set<std::uint64_t> ImportedIds;
    std::size_t CookedHeaderSize = 0;
    std::uint32_t PackageFlags = 0;
};
inline FLegacyZenCorrespondence ReadLegacyZenCorrespondence(const FImportIdentityBytes& Header,
                                                            const FImportIdentityBytes& Body,
                                                            const FImportIdentityBytes& Raw,
                                                            const FImportIdentityBytes& MetadataBytes,
                                                            const std::string& Name)
{
    constexpr std::size_t Limit = 64u * 1024u * 1024u;
    Need(Header.size() && Header.size() <= Limit && Body.size() <= Limit && Raw.size() >= 64 && Raw.size() <= Limit &&
             MetadataBytes.size() && MetadataBytes.size() <= 1024u * 1024u,
         "input-limit");
    Need(PackagePath(Name), "package-path");
    const auto Legacy = ReadPackage(Header, Body);
    Need(Legacy.Error.Code.empty(), "legacy-invalid");
    Need(Legacy.Package == Name, "package-identity");
    const auto Metadata = J::Parse(std::string(MetadataBytes.begin(), MetadataBytes.end()), 1024u * 1024u, 400000, 64);
    Need(Metadata["toc_version"].S() == "DirectoryIndex" && Metadata["container_header_version"].S() == "Initial",
         "original-version");
    const auto& Store = Metadata["store_entry"];
    const auto Bundles = Store["export_bundle_count"].U();
    Need(Bundles && Bundles <= 100000 && Store["export_count"].U() == Legacy.Exports.size(), "export-count");
    const auto& Imported = Store["imported_packages"];
    Need(Imported.Kind == J::K::Array && Imported.Items.size() <= 100000, "imported-packages");
    std::set<std::uint64_t> ImportedIds;
    for (const auto& Id : Imported.Items)
        Need(ImportedIds.insert(Id.U()).second, "duplicate-package-id");
    std::array<std::size_t, 16> Summary{};
    for (std::size_t I = 0; I < Summary.size(); ++I)
    {
        const auto V = Read(Raw, 4 * I, 4);
        Need(I < 6 || V <= std::uint64_t(INT32_MAX), "original-offset-sign");
        Summary[I] = std::size_t(V);
    }
    Need(64 <= Summary[6] && Summary[6] <= Summary[8] && Summary[8] <= Summary[10] && Summary[10] <= Summary[11] &&
             Summary[11] <= Summary[12] && Summary[12] <= Summary[13] && Summary[13] <= Raw.size() &&
             Summary[14] <= Raw.size() - Summary[13] && Summary[7] <= Summary[8] - Summary[6] &&
             Summary[9] == Summary[10] - Summary[8] && Summary[5] >= 64,
         "original-layout");
    // CookedHeaderSize is the original loose package's virtual export origin.
    // It need not fit in the Zen file. Validate every export relative to it
    // below; physical payload positions come exclusively from the bundle graph.
    const auto Names = ReadNames(Raw, Summary[6], Summary[7]);
    Need(Summary[9] == 8 * (Names.size() + 1) && Legacy.Names.size() >= Names.size() &&
             std::equal(Names.begin(), Names.end(), Legacy.Names.begin()),
         "name-map-mismatch");
    Need((Summary[0] >> 30) == 0 && Summary[0] < Names.size() && Summary[1] == 0 && Names[Summary[0]] == Name,
         "original-package");
    const auto ImportBytes = Summary[11] - Summary[10];
    Need(ImportBytes % 8 == 0 && ImportBytes / 8 <= Legacy.Imports.size(), "original-import-count");
    std::vector<std::uint64_t> OriginalImports;
    for (std::size_t I = 0; I < ImportBytes / 8; ++I)
        OriginalImports.push_back(Read(Raw, Summary[10] + 8 * I, 8));
    Need(Summary[12] - Summary[11] == 72 * Legacy.Exports.size(), "original-export-layout");
    const auto OriginalIndex = [&](std::int32_t Index)
    {
        if (!Index)
            return UINT64_MAX;
        if (Index > 0)
        {
            Need(std::uint64_t(Index) <= Legacy.Exports.size(), "export-reference");
            return std::uint64_t(Index - 1);
        }
        const auto At = std::uint64_t(-std::int64_t(Index) - 1);
        Need(At < OriginalImports.size(), "import-reference");
        return OriginalImports[std::size_t(At)];
    };
    for (std::size_t I = 0; I < Legacy.Exports.size(); ++I)
    {
        const auto At = Summary[11] + 72 * I;
        const auto Offset = Read(Raw, At, 8), Size = Read(Raw, At + 8, 8), NameIndex = Read(Raw, At + 16, 4),
                   Number = Read(Raw, At + 20, 4);
        Need((NameIndex >> 30) == 0 && NameIndex < Names.size(), "export-name-index");
        const auto ExportName = Names[std::size_t(NameIndex)] + (Number ? "_" + std::to_string(Number - 1) : "");
        const auto& Export = Legacy.Exports[I];
        Need(Export.Name == ExportName && Size == Export.Size && Offset >= Summary[5] &&
                 Offset - Summary[5] == Export.Offset && Read(Raw, At + 24, 8) == OriginalIndex(Export.Outer) &&
                 Read(Raw, At + 32, 8) == OriginalIndex(Export.Class) &&
                 Read(Raw, At + 40, 8) == OriginalIndex(Export.Super) &&
                 Read(Raw, At + 48, 8) == OriginalIndex(Export.Template) && Read(Raw, At + 64, 4) == Export.Flags,
             "export-mismatch");
    }
    Need(Bundles <= (Summary[13] - Summary[12]) / 8, "bundle-head-bounds");
    std::size_t Total = 0;
    for (std::size_t I = 0; I < Bundles; ++I)
    {
        const auto First = Read(Raw, Summary[12] + 8 * I, 4), Count = Read(Raw, Summary[12] + 8 * I + 4, 4);
        Need(First == Total && Count <= 2 * Legacy.Exports.size() - std::min(Total, 2 * Legacy.Exports.size()),
             "bundle-order");
        Total += std::size_t(Count);
    }
    const auto Entries = Summary[12] + 8 * std::size_t(Bundles);
    Need(Total == 2 * Legacy.Exports.size() && Summary[13] - Entries == 8 * Total, "bundle-coverage");
    // Parse the original graph boundary, even though import identities do not use arcs.
    auto GraphAt = Summary[13];
    const auto GraphEnd = GraphAt + Summary[14];
    Need(GraphEnd - GraphAt >= 4, "graph-size");
    const auto GraphCount = Read(Raw, GraphAt, 4);
    GraphAt += 4;
    Need(GraphCount <= 100000, "graph-count");
    for (std::size_t I = 0; I < GraphCount; ++I)
    {
        Need(GraphAt <= GraphEnd && GraphEnd - GraphAt >= 12, "graph-record");
        const auto Id = Read(Raw, GraphAt, 8), Arcs = Read(Raw, GraphAt + 8, 4);
        GraphAt += 12;
        // A record may name a package the store entry does not import: the localized copies of an imported asset
        // (L10N/<culture>/...), in 1.1.1 and 2.0.0 alike. Only the bounds are checked.
        (void)Id;
        Need(Arcs <= 100000 && Arcs <= (GraphEnd - GraphAt) / 8, "graph-arcs");
        GraphAt += 8 * std::size_t(Arcs);
    }
    Need(GraphAt == GraphEnd, "graph-tail");
    std::set<std::size_t> Created, Serialized;
    auto Cursor = GraphEnd;
    for (std::size_t I = 0; I < Total; ++I)
    {
        const auto Index = Read(Raw, Entries + 8 * I, 4), Kind = Read(Raw, Entries + 8 * I + 4, 4);
        Need(Index < Legacy.Exports.size() && Kind <= 1, "bundle-command");
        if (!Kind)
            Need(Created.insert(std::size_t(Index)).second, "duplicate-create");
        else
        {
            Need(Created.count(std::size_t(Index)) && Serialized.insert(std::size_t(Index)).second, "serialize-order");
            const auto& Export = Legacy.Exports[std::size_t(Index)];
            Need(Cursor <= Raw.size() && Export.Size <= Raw.size() - Cursor &&
                     std::equal(Body.begin() + Export.Offset, Body.begin() + Export.Offset + Export.Size,
                                Raw.begin() + Cursor),
                 "export-payload");
            Cursor += Export.Size;
        }
    }
    Need(Serialized.size() == Legacy.Exports.size() && Cursor == Raw.size(), "payload-tail");
    return {Legacy, Names, OriginalImports, ImportedIds, Summary[5], std::uint32_t(Summary[4])};
}
} // namespace ImportIdentityDetail

// The original Zen package and its store entry give the names of the packages the legacy
// package's unresolved imports name. Every structural field is checked against the legacy pair.
inline FVerifiedImportIdentities RecoverImportIdentities(const FImportIdentityBytes& Header,
                                                         const FImportIdentityBytes& Body,
                                                         const FImportIdentityBytes& Raw,
                                                         const FImportIdentityBytes& MetadataBytes,
                                                         const std::string& Name, const FImportIdentityHash& HashName)
{
    using namespace ImportIdentityDetail;
    Need(bool(HashName), "input-limit");
    const auto Correspondence = ReadLegacyZenCorrespondence(Header, Body, Raw, MetadataBytes, Name);
    const auto& Legacy = Correspondence.Legacy;
    const auto& Names = Correspondence.Names;
    const auto& OriginalImports = Correspondence.OriginalImports;
    const auto& ImportedIds = Correspondence.ImportedIds;
    constexpr std::uint64_t Mask = (std::uint64_t(1) << 62) - 1;
    std::vector<bool> Referenced(Legacy.Imports.size(), false);
    const auto MarkReferences = [&](const FImportIdentityBytes& Bytes)
    {
        // FPackageIndex is a little-endian signed 32-bit value. Scan every
        // byte position in both the header and body, including opaque payloads
        // and preload tables. This deliberately over-approximates references:
        // an incidental matching integer still requires identity recovery.
        std::uint32_t Window = 0;
        for (std::size_t At = 0; At < Bytes.size(); ++At)
        {
            Window = (Window >> 8) | (std::uint32_t(Bytes[At]) << 24);
            if (At < 3 || Window < 0x80000000u)
                continue;
            const auto Number = std::uint32_t(0) - Window;
            if (Number && Number <= Referenced.size())
                Referenced[Number - 1] = true;
        }
    };
    MarkReferences(Header);
    MarkReferences(Body);
    std::set<std::pair<std::string, std::string>> Candidates;
    std::set<std::string> ObjectNames;
    for (const auto& Object : Names)
        if (!Object.empty() && Object.size() <= 1024 && std::all_of(Object.begin(), Object.end(), Character))
            ObjectNames.insert(Object);
    for (const auto& Candidate : Names)
        if (PackagePath(Candidate) && ImportedIds.count(HashName(Normalize(Candidate))))
        {
            Candidates.emplace(Candidate, Candidate + "." + Candidate.substr(Candidate.rfind('/') + 1));
            // Export names are not necessarily the package basename: generated
            // classes and their default objects are separate public exports.
            // Only exact names from this original package's name map qualify;
            // the original import hash below must identify exactly one pair.
            Need(ObjectNames.size() <= 1000000 - std::min<std::size_t>(Candidates.size(), 1000000), "candidate-limit");
            for (const auto& Object : ObjectNames)
                Candidates.emplace(Candidate, Candidate + "." + Object);
        }
    std::set<std::uint64_t> Wanted;
    for (const auto Index : OriginalImports)
        if ((Index >> 62) == 2)
            Wanted.insert(Index & Mask);
    std::set<std::pair<std::string, std::string>> Expanded;
    for (unsigned Depth = 0; Depth < 8; ++Depth)
    {
        std::vector<std::pair<std::string, std::string>> Parents;
        for (const auto& Candidate : Candidates)
            if (!Expanded.count(Candidate) && Wanted.count(HashName(Normalize(Candidate.second)) & Mask))
            {
                Expanded.insert(Candidate);
                Parents.push_back(Candidate);
            }
        if (Parents.empty())
            break;
        for (const auto& Parent : Parents)
        {
            Need(ObjectNames.size() <= 1000000 - std::min<std::size_t>(Candidates.size(), 1000000), "candidate-limit");
            // A nested export's outer must itself have a matching original
            // import identity. Never enumerate arbitrary path combinations.
            for (const auto& Object : ObjectNames)
                Candidates.emplace(Parent.first, Parent.second + "." + Object);
        }
    }
    std::map<std::int32_t, std::string> Paths;
    std::size_t Unused = 0;
    for (std::size_t I = 0; I < Legacy.Imports.size(); ++I)
    {
        const auto Index = -std::int32_t(I) - 1;
        std::string Path;
        if (Legacy.Imports[I].Name != "UnknownExport" || !Legacy.Reference(Index, Path) ||
            Path != "/Engine/UnknownPackage.UnknownExport")
            continue;
        if (!Referenced[I])
        {
            ++Unused;
            continue;
        }
        Need(I < OriginalImports.size(), "zen-index");
        const auto RawIndex = OriginalImports[I];
        Need((RawIndex >> 62) == 2, "nonnull-package-import");
        std::vector<std::pair<std::string, std::string>> Matches;
        for (const auto& Candidate : Candidates)
            if ((HashName(Normalize(Candidate.second)) & Mask) == (RawIndex & Mask))
                Matches.push_back(Candidate);
        // Unreal's names ignore case, so spellings that differ only by case are one object. The hash
        // above ignores it too. The spelling of the package's own path is the one kept.
        Need(!Matches.empty(), "candidate-ambiguity");
        for (const auto& Match : Matches)
            Need(Normalize(Match.second) == Normalize(Matches[0].second), "candidate-ambiguity");
        const auto Spelled = std::find_if(Matches.begin(), Matches.end(), [](const auto& Match)
            { return Match.second == Match.first + "." + Match.first.substr(Match.first.rfind('/') + 1); });
        const auto& Candidate = Spelled != Matches.end() ? *Spelled : Matches[0];
        // A missing engine object is never brought in from the game's containers.
        Need(Candidate.first.compare(0, 6, "/Game/") == 0, "target-scope");
        Need(Paths.emplace(Index, Candidate.second).second, "duplicate-row");
    }
    Need(!Paths.empty() || Unused, "row-coverage");
    return FVerifiedImportIdentities(Name, std::move(Paths), Header, Body, Raw, MetadataBytes);
}
} // namespace Blueprint
} // namespace P4MapKit

// Exact lexical texture-profile source inventory. No config merge, original
// defaults, engine enum aliases, filesystem effects or editable admission.
#pragma once
#include "package_reader.h"
#include <functional>

namespace P4MapKit
{
enum class ETextureGroupContract { Original111_64 = 1 };
struct FTextureGroupRecord { std::string Name; std::int64_t Value = 0; };
struct FTextureProfileMember { std::string Key, Raw, Value; bool DuplicateKey = false; };
struct FTextureProfileEntry
{
    std::string Section, Key, Operation, Raw, Value, Group, LineEnding;
    std::size_t Offset = 0, End = 0, Line = 0;
    bool IsGroupRow = false, OriginalGroupKnown = false, DuplicateRow = false;
    std::vector<FTextureProfileMember> Members;
};
struct FTextureProfileInventory
{
    ETextureGroupContract Contract = ETextureGroupContract::Original111_64;
    std::vector<FTextureGroupRecord> Groups;
    std::vector<FTextureProfileEntry> Entries;
    std::size_t SourceBytes = 0, BomBytes = 0, Lines = 0, CopiedBytes = 0;
    // Source lexical completeness never stands in for these separate proofs.
    bool EffectiveConfigResolved = false, OriginalDefaultsQualified = false;
    bool ProfileInheritanceResolved = false, C4BehaviorQualified = false;
};
struct FTextureProfileLimits
{
    std::size_t SourceBytes = 8u * 1024u * 1024u, CopiedBytes = 64u * 1024u * 1024u;
    std::size_t Entries = 100000, Lines = 1000000, Members = 1024, Depth = 24;
    std::size_t NameBytes = 4096, SelectedSections = 64;
};
namespace TextureProfileDetail
{
inline std::string Identity(std::string Name)
{ for (auto& C : Name) if (C >= 'A' && C <= 'Z') C += 'a' - 'A'; return Name; }
inline std::string Trim(const std::string& Text)
{
    const auto Start = Text.find_first_not_of(" \t\r\n");
    return Start == std::string::npos ? "" : Text.substr(Start, Text.find_last_not_of(" \t\r\n") - Start + 1);
}
inline bool Identifier(const std::string& Name)
{
    if (Name.empty()) return false;
    for (const unsigned char C : Name)
        if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_')) return false;
    return true;
}
struct FReader
{
    const FTextureProfileLimits& Limits;
    const std::function<bool()>& Cancel;
    FReadError& Error;
    std::size_t Copied = 0;
    bool Reject(std::size_t At, const char* Code) { Error = {At, Code}; return false; }
    bool Continue(std::size_t At)
    { return !Cancel || !Cancel() || Reject(At, "texture-profile-cancelled"); }
    bool Charge(std::size_t Bytes, std::size_t At)
    {
        if (Copied > Limits.CopiedBytes || Bytes > Limits.CopiedBytes - Copied)
            return Reject(At, "texture-profile-copy-limit");
        Copied += Bytes; return true;
    }
    bool UTF8(const std::string& Text)
    {
        std::size_t NextCancel = 0;
        for (std::size_t At = 0; At < Text.size();)
        {
            if (At >= NextCancel)
            {
                if (!Continue(At)) return false;
                NextCancel = At + std::min<std::size_t>(4096, Text.size() - At);
            }
            const auto Start = At; const auto First = std::uint8_t(Text[At++]);
            if (!First) return Reject(Start, "texture-profile-embedded-nul");
            if (First < 0x80) continue;
            const int Extra = First >= 0xc2 && First <= 0xdf ? 1 : First >= 0xe0 && First <= 0xef ? 2 :
                First >= 0xf0 && First <= 0xf4 ? 3 : -1;
            if (Extra < 0 || std::size_t(Extra) > Text.size() - At) return Reject(Start, "texture-profile-invalid-utf8");
            std::uint32_t Code = First & ((1u << (6 - Extra)) - 1);
            for (int Index = 0; Index < Extra; ++Index)
            {
                const auto Next = std::uint8_t(Text[At++]);
                if ((Next & 0xc0) != 0x80) return Reject(Start, "texture-profile-invalid-utf8");
                Code = (Code << 6) | (Next & 0x3f);
            }
            if (Code < (Extra == 1 ? 0x80u : Extra == 2 ? 0x800u : 0x10000u) ||
                Code > 0x10ffff || (Code >= 0xd800 && Code <= 0xdfff)) return Reject(Start, "texture-profile-invalid-utf8");
        }
        return true;
    }
    bool Tuple(const std::string& Text, std::size_t Offset, std::vector<FTextureProfileMember>& Out)
    {
        if (Text.size() < 2 || Text.front() != '(' || Text.back() != ')')
            return Reject(Offset, "texture-profile-tuple-boundary");
        std::vector<FTextureProfileMember> Members; std::unordered_set<std::string> Names;
        std::size_t Start = 1, Depth = 0; bool Quoted = false, Escape = false;
        const auto Add = [&](std::size_t End) {
            if (Members.size() >= Limits.Members) return Reject(Offset + Start, "texture-profile-member-limit");
            const auto Raw = Text.substr(Start, End - Start); const auto Equal = Raw.find('=');
            if (Equal == std::string::npos) return Reject(Offset + Start, "texture-profile-member-assignment");
            FTextureProfileMember Member; Member.Key = Trim(Raw.substr(0, Equal));
            if (!Identifier(Member.Key) || Member.Key.size() > Limits.NameBytes)
                return Reject(Offset + Start, "texture-profile-member-key");
            Member.Raw = Raw; Member.Value = Trim(Raw.substr(Equal + 1));
            const auto Key = Identity(Member.Key);
            if (!Charge(Member.Key.size(), Offset + Start) || !Charge(Member.Raw.size(), Offset + Start) ||
                !Charge(Member.Value.size(), Offset + Start) || !Charge(Key.size(), Offset + Start)) return false;
            Member.DuplicateKey = !Names.insert(Key).second;
            Members.push_back(std::move(Member)); return true;
        };
        for (std::size_t At = 1; At < Text.size() - 1; ++At)
        {
            if ((At & 4095) == 0 && !Continue(Offset + At)) return false;
            const char C = Text[At];
            if (Escape) { Escape = false; continue; }
            if (Quoted && C == '\\') { Escape = true; continue; }
            if (C == '"') { Quoted = !Quoted; continue; }
            if (Quoted) continue;
            if (C == '(') { if (Depth >= Limits.Depth) return Reject(Offset + At, "texture-profile-depth-limit"); ++Depth; }
            else if (C == ')') { if (!Depth) return Reject(Offset + At, "texture-profile-tuple-depth"); --Depth; }
            else if (C == ',' && !Depth) { if (!Add(At)) return false; Start = At + 1; }
        }
        if (Quoted || Escape || Depth) return Reject(Offset, "texture-profile-tuple-unbalanced");
        if (!Add(Text.size() - 1)) return false;
        Out = std::move(Members); return true;
    }
};
}

// Records must come from the caller's independently source-bound original enum.
// This checks the explicit 1.1.1 64-group contract, not a stock ordinal mapping.
inline bool ReadTextureProfileInventory(const std::string& Source,
    const std::vector<FTextureGroupRecord>& OriginalGroups, ETextureGroupContract Contract,
    const std::vector<std::string>& SelectedSections, FTextureProfileInventory& Out, FReadError& Error,
    const FTextureProfileLimits& Limits = {}, const std::function<bool()>& Cancel = {})
{
    using namespace TextureProfileDetail;
    Error = {}; FReader Reader{Limits, Cancel, Error, 0};
    if (!Reader.Continue(0)) return false;
    if (Contract != ETextureGroupContract::Original111_64 || OriginalGroups.size() != 65)
        return Reader.Reject(0, "texture-group-contract-unqualified");
    FTextureProfileInventory Pending; Pending.Contract = Contract;
    std::unordered_set<std::string> GroupNames; std::array<bool, 65> Values{};
    for (std::size_t Index = 0; Index < OriginalGroups.size(); ++Index)
    {
        const auto& Group = OriginalGroups[Index];
        if (!Reader.Continue(Index)) return false;
        if (Group.Name.size() > Limits.NameBytes || !Identifier(Group.Name) || Group.Name.compare(0, 13, "TEXTUREGROUP_") != 0 ||
            Group.Name.size() == 13 || !GroupNames.insert(Identity(Group.Name)).second)
            return Reader.Reject(Index, "texture-group-name-or-duplicate");
        if (Group.Value < 0 || Group.Value > 64 || Values[std::size_t(Group.Value)])
            return Reader.Reject(Index, "texture-group-value-or-duplicate");
        Values[std::size_t(Group.Value)] = true;
        if ((Group.Name == "TEXTUREGROUP_MAX") != (Index == 64) || (Index == 64 && Group.Value != 64))
            return Reader.Reject(Index, "texture-group-maximum-sentinel");
        if (!Reader.Charge(Group.Name.size(), Index) || !Reader.Charge(Group.Name.size(), Index)) return false;
        Pending.Groups.push_back(Group);
    }
    GroupNames.erase(Identity("TEXTUREGROUP_MAX"));
    if (Source.size() > Limits.SourceBytes) return Reader.Reject(0, "texture-profile-source-limit");
    if (!Reader.UTF8(Source)) return false;
    if (SelectedSections.empty() || SelectedSections.size() > Limits.SelectedSections)
        return Reader.Reject(0, "texture-profile-selection-limit");
    std::unordered_set<std::string> Sections;
    for (const auto& Section : SelectedSections)
    {
        if (Section.empty() || Section.size() > Limits.NameBytes || !Reader.UTF8(Section) ||
            Section.find_first_of("[]\r\n") != std::string::npos || !Sections.insert(Section).second)
            return Error.Code.empty() ? Reader.Reject(0, "texture-profile-section-selection") : false;
        if (!Reader.Charge(Section.size(), 0)) return false;
    }
    Pending.SourceBytes = Source.size();
    Pending.BomBytes = Source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    std::string Section; std::unordered_set<std::string> Seen;
    for (std::size_t Start = Pending.BomBytes; Start < Source.size();)
    {
        if (!Reader.Continue(Start)) return false;
        if (Pending.Lines >= Limits.Lines) return Reader.Reject(Start, "texture-profile-line-limit");
        ++Pending.Lines;
        const auto Stop = Source.find_first_of("\r\n", Start);
        const auto End = Stop == std::string::npos ? Source.size() : Stop;
        auto Next = Stop == std::string::npos ? Source.size() : Stop + 1;
        if (Stop != std::string::npos && Source[Stop] == '\r' && Next < Source.size() && Source[Next] == '\n') ++Next;
        const std::string Raw = Source.substr(Start, End - Start), Line = Trim(Raw);
        const auto Offset = Start; Start = Next;
        if (Line.empty() || Line[0] == ';' || Line[0] == '#') continue;
        if (Line[0] == '[')
        {
            if (Line.back() != ']' || Line.find(']', 1) != Line.size() - 1)
                return Reader.Reject(Offset, "texture-profile-section-boundary");
            Section = Line.substr(1, Line.size() - 2);
            if (Section.empty() || Section.size() > Limits.NameBytes) return Reader.Reject(Offset, "texture-profile-section-name");
            continue;
        }
        if (Sections.count(Section) == 0) continue;
        const auto Equal = Line.find('=');
        if (Equal == std::string::npos) return Reader.Reject(Offset, "texture-profile-assignment");
        if (Pending.Entries.size() >= Limits.Entries) return Reader.Reject(Offset, "texture-profile-entry-limit");
        FTextureProfileEntry Entry; Entry.Section = Section; Entry.Key = Trim(Line.substr(0, Equal)); Entry.Operation = "set";
        if (!Entry.Key.empty() && (Entry.Key[0] == '+' || Entry.Key[0] == '-' || Entry.Key[0] == '.' || Entry.Key[0] == '!'))
        { Entry.Operation.assign(1, Entry.Key[0]); Entry.Key.erase(0, 1); }
        if (Entry.Key.empty() || Entry.Key.size() > Limits.NameBytes) return Reader.Reject(Offset, "texture-profile-key");
        Entry.Raw = Raw; Entry.Value = Trim(Line.substr(Equal + 1)); Entry.Offset = Offset; Entry.End = End;
        Entry.LineEnding = Source.substr(End, Next - End); Entry.Line = Pending.Lines;
        Entry.IsGroupRow = Identity(Entry.Key) == "texturelodgroups";
        for (const auto* Text : {&Entry.Section, &Entry.Key, &Entry.Operation, &Entry.Raw, &Entry.Value, &Entry.LineEnding})
            if (!Reader.Charge(Text->size(), Offset)) return false;
        if (Entry.IsGroupRow && Entry.Operation != "!")
        {
            const auto ValueOffset = Offset + Raw.find('=') + 1;
            if (!Reader.Tuple(Entry.Value, ValueOffset, Entry.Members)) return false;
            bool HasGroup = false;
            for (const auto& Member : Entry.Members) if (Identity(Member.Key) == "group")
            {
                if (HasGroup) return Reader.Reject(Offset, "texture-profile-duplicate-group-member");
                HasGroup = true; Entry.Group = Member.Value;
            }
            if (!HasGroup || Entry.Group.empty()) return Reader.Reject(Offset, "texture-profile-missing-group");
            Entry.OriginalGroupKnown = GroupNames.count(Identity(Entry.Group)) != 0;
            if (!Reader.Charge(Entry.Group.size(), Offset)) return false;
        }
        // Preserve duplicate operations; do not apply UE's +/./-/! semantics.
        const std::string Duplicate = Entry.Section + "\n" + Identity(Entry.Key) + "\n" + Entry.Operation + "\n" + Entry.Value;
        if (!Reader.Charge(Duplicate.size(), Offset)) return false;
        Entry.DuplicateRow = !Seen.insert(Duplicate).second;
        Pending.Entries.push_back(std::move(Entry));
    }
    if (!Reader.Continue(Source.size())) return false;
    Pending.CopiedBytes = Reader.Copied; Out = std::move(Pending); return true;
}
}

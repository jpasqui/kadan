#pragma once
// Portable boundary: Unreal supplies typed edits and verified origin evidence. This code writes
// only neutral mod files; retoc/repak and filesystem publication remain the caller's responsibility.
#include "mod_folders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace P4MapKit { namespace Export {
// Tsuchi's current public API/schema is version 0; status block versions are unrelated.
constexpr unsigned MinimumFrameworkApi = 0;
struct Descriptor { std::string Id, Name, Version; unsigned Format = 1; };
struct Names { std::string Root, Container; };
struct Transform {
    std::array<double, 3> Location{{0,0,0}}, Rotation{{0,0,0}}, Scale{{1,1,1}};
};
// A value the framework writes into a field of one of the game's actors before it begins play: a
// number, true or false, or text (a name, a string, an enum member's name or an asset's object path).
struct SetValue {
    enum class Kind { Boolean, Integer, Number, Text } Type = Kind::Number;
    bool Boolean = false; long long Integer = 0; double Number = 0; std::string Text;
};
struct LevelEdit {
    std::vector<std::string> Remove; std::map<std::string, Transform> Move;
    std::map<std::string, std::map<std::string, SetValue>> Set; // actor, then the value's path from the actor
};
// Moves and removals of rows of one of the game's own placement lists, by the row's placement number
// (GenerateInfo.DebugUniqueId). The game builds its gameplay objects from these rows, not from the level.
struct PlacementEdit {
    std::vector<std::uint64_t> Remove; std::map<std::uint64_t, Transform> Move;
};
struct MapEdit {
    std::string Package;
    std::vector<std::string> Levels;
    // Placement lists of the mod's own (ActorPlacementInfoAsset packages) the framework gives the map.
    std::vector<std::string> Placements;
    std::map<std::string, LevelEdit> Changes;
    // Changes to the game's own placement lists, by the package of the list (an AP_ asset of the game's).
    std::map<std::string, PlacementEdit> PlacementChanges;
    bool Navigation = false;
};
inline bool Alnum(char C) { return (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'); }
// Tsuchi keeps the ID tsuchi and every ID under tsuchi. for itself and blocks a mod folder named so.
inline bool ReservedId(const std::string& Id) { return Id == "tsuchi" || Id.compare(0,7,"tsuchi.") == 0; }
inline bool ValidId(const std::string& Id) {
    if (Id.empty() || Id.size() > 47 || !Alnum(Id.front()) || !Alnum(Id.back()) || ReservedId(Id)) return false;
    for (char C : Id) if (!Alnum(C) && C != '.' && C != '_' && C != '-') return false;
    return true;
}
inline bool ValidVersion(const std::string& Text) {
    std::size_t At = 0;
    for (unsigned Part = 0; Part < 3; ++Part) {
        const std::size_t First = At;
        while (At < Text.size() && Text[At] >= '0' && Text[At] <= '9') ++At;
        if (First == At || At - First > 9 || (At - First > 1 && Text[First] == '0')) return false;
        if (Part < 2) { if (At == Text.size() || Text[At++] != '.') return false; }
    }
    return At == Text.size();
}
inline bool Validate(const Descriptor& Mod, std::string& Error) {
    if (Mod.Format != 1 || !ValidId(Mod.Id)) { Error = "Invalid format or mod ID"; return false; }
    if (!ValidVersion(Mod.Version)) { Error = "Invalid mod version"; return false; }
    if (Mod.Name.empty() || Mod.Name.size() > 127) { Error = "Invalid mod name"; return false; }
    for (unsigned char C : Mod.Name) if (C < 32) { Error = "Control character in mod name"; return false; }
    return true;
}
inline bool DerivedNames(const Descriptor& Mod, Names& Result, std::string& Error) {
    if (!Validate(Mod, Error)) return false;
    // The mod's ID, with the dots Unreal does not allow in a folder name as underscores.
    std::string Slug;
    for (char C : Mod.Id) Slug += C == '.' ? '_' : C;
    // A live preview's mod has its packages in Kadan's preview folder, every other mod in its own folder.
    const std::string Folder = PreviewModId(Mod.Id) ? P4_PREVIEW_FOLDER "/" : P4_MODS_FOLDER "/";
    Result = Names{Folder + Slug, "kadan_" + Slug}; return true;
}
inline bool Plain(const std::string& Text) {
    if (Text.empty() || Text.size() > 127) return false;
    for (char C : Text) if (!Alnum(C) && !(C >= 'A' && C <= 'Z') && C != '_' && C != '-') return false;
    return true;
}
inline bool PackageName(const std::string& Text) {
    if (Text.compare(0,6,"/Game/") != 0) return false;
    std::size_t At = 1;
    while (At < Text.size()) {
        const auto End = Text.find('/', At);
        if (!Plain(Text.substr(At, End == std::string::npos ? End : End - At))) return false;
        if (End == std::string::npos) return true;
        At = End + 1;
    }
    return false;
}
inline std::string Quote(const std::string& Text) {
    const char Hex[] = "0123456789abcdef"; std::string Out = "\"";
    for (unsigned char C : Text) {
        if (C == '"' || C == '\\') { Out += '\\'; Out += char(C); }
        else if (C < 32) { Out += "\\u00"; Out += Hex[C >> 4]; Out += Hex[C & 15]; }
        else Out += char(C);
    }
    return Out + '"';
}
inline std::string Vector(const std::array<double,3>& Values) {
    std::ostringstream Out; Out.imbue(std::locale::classic()); Out << '[' << std::setprecision(17);
    for (unsigned I = 0; I < 3; ++I) { if (I) Out << ','; Out << Values[I]; } return Out.str() + ']';
}
inline bool ValidTransform(const Transform& Value) {
    for (const auto* Values : {&Value.Location, &Value.Rotation, &Value.Scale})
        for (double Number : *Values) if (!std::isfinite(Number) || std::abs(Number) >= 1e9) return false;
    return true;
}
// A path as the framework's reflection takes one: field names joined by dots, with [n] for an element.
inline bool FieldPath(const std::string& Text) {
    if (Text.empty() || Text.size() > 255 || Text.front() == '.' || Text.back() == '.') return false;
    char Last = 0;
    for (char C : Text) {
        if (!Alnum(C) && !(C >= 'A' && C <= 'Z') && C != '_' && C != '.' && C != '[' && C != ']') return false;
        if (C == '.' && Last == '.') return false;
        Last = C;
    }
    return true;
}
inline bool ValidSetValue(const SetValue& Value) {
    if (Value.Type == SetValue::Kind::Number) return std::isfinite(Value.Number);
    if (Value.Type == SetValue::Kind::Text) return Value.Text.find('\0') == std::string::npos && Value.Text.size() <= 4096;
    return true;
}
inline std::string ValueJson(const SetValue& Value) {
    if (Value.Type == SetValue::Kind::Boolean) return Value.Boolean ? "true" : "false";
    if (Value.Type == SetValue::Kind::Text) return Quote(Value.Text);
    std::ostringstream Out; Out.imbue(std::locale::classic());
    if (Value.Type == SetValue::Kind::Integer) Out << Value.Integer; else Out << std::setprecision(17) << Value.Number;
    return Out.str();
}
inline std::string StringList(const std::vector<std::string>& Values) {
    std::string Out = "[";
    for (std::size_t I = 0; I < Values.size(); ++I) { if (I) Out += ','; Out += Quote(Values[I]); } return Out + ']';
}
inline bool MapFiles(const Descriptor& Mod, const MapEdit& Map, std::string& Entry,
    std::map<std::string,std::string>& Files, std::string& Error, const std::string& FileStem = {}) {
    // Build privately. A failed conversion never returns half a manifest.
    if (!Validate(Mod, Error) || !PackageName(Map.Package)) { Error = "Invalid mod descriptor or map package"; return false; }
    Names Naming; if (!DerivedNames(Mod, Naming, Error)) return false;
    // A creator's level ships at the path it has (Tsuchi loads any package path), except in Kadan's live preview folder.
    std::vector<std::string> SeenLevels;
    for (const auto& Level : Map.Levels) {
        if (!PackageName(Level) || PreviewFolderPackage(Level)) {
            Error = "A mod level must be a package outside Kadan's live preview folder";
            return false;
        }
        std::string Lower = Level;
        for (char& C : Lower)
            if (C >= 'A' && C <= 'Z')
                C = char(C + 32);
        if (std::find(SeenLevels.begin(), SeenLevels.end(), Lower) != SeenLevels.end()) {
            Error = "A mod level is named twice";
            return false;
        }
        SeenLevels.push_back(Lower);
    }
    std::vector<std::string> SeenLists;
    for (const auto& List : Map.Placements) {
        if (!PackageName(List) || List.compare(0, Naming.Root.size() + 1, Naming.Root + '/') != 0) { Error = "A placement list must be inside its identity root"; return false; }
        std::string Lower = List; for (char& C : Lower) if (C >= 'A' && C <= 'Z') C = char(C + 32);
        if (std::find(SeenLists.begin(), SeenLists.end(), Lower) != SeenLists.end()) { Error = "A placement list is named twice"; return false; }
        SeenLists.push_back(Lower);
    }
    std::string Changes = "{\"levels\":{"; bool HasChanges = false;
    for (const auto& Pair : Map.Changes) {
        if (!Plain(Pair.first)) { Error = "Invalid changed sublevel name"; return false; }
        const auto& Edit = Pair.second;
        for (const auto& Actor : Edit.Remove) if (!Plain(Actor)) { Error = "Invalid removed actor name"; return false; }
        for (const auto& Move : Edit.Move) if (!Plain(Move.first) || !ValidTransform(Move.second)) { Error = "Invalid actor move"; return false; }
        for (const auto& Actor : Edit.Set) {
            if (!Plain(Actor.first) || Actor.second.empty()) { Error = "Invalid actor value change"; return false; }
            for (const auto& Value : Actor.second) if (!FieldPath(Value.first) || !ValidSetValue(Value.second)) { Error = "Invalid actor value change"; return false; }
        }
        if (Edit.Remove.empty() && Edit.Move.empty() && Edit.Set.empty()) continue;
        if (HasChanges) Changes += ','; HasChanges = true;
        Changes += Quote(Pair.first) + ":{"; bool Part = false;
        if (!Edit.Remove.empty()) { Changes += "\"remove\":" + StringList(Edit.Remove); Part = true; }
        if (!Edit.Move.empty()) {
            if (Part) Changes += ',';
            Changes += "\"move\":{"; bool First = true; Part = true;
            for (const auto& Move : Edit.Move) {
                if (!First) Changes += ','; First = false;
                Changes += Quote(Move.first) + ":{\"location\":" + Vector(Move.second.Location) + ",\"rotation\":" + Vector(Move.second.Rotation) + ",\"scale\":" + Vector(Move.second.Scale) + '}';
            }
            Changes += '}';
        }
        if (!Edit.Set.empty()) {
            if (Part) Changes += ',';
            Changes += "\"set\":{"; bool First = true;
            for (const auto& Actor : Edit.Set) {
                if (!First) Changes += ','; First = false;
                Changes += Quote(Actor.first) + ":{"; bool FirstValue = true;
                for (const auto& Value : Actor.second) {
                    if (!FirstValue) Changes += ','; FirstValue = false;
                    Changes += Quote(Value.first) + ':' + ValueJson(Value.second);
                }
                Changes += '}';
            }
            Changes += '}';
        }
        Changes += '}';
    }
    Changes += "}}\n";
    // Rows of the game's lists: the list is the game's (never the mod's), an AP_ asset, and a row is
    // moved or removed once. The numbers are written as decimal text, as Tsuchi reads them.
    std::string RowChanges; std::vector<std::string> SeenGameLists;
    for (const auto& Pair : Map.PlacementChanges) {
        const auto& List = Pair.first; const auto& Edit = Pair.second;
        const auto Leaf = List.substr(List.rfind('/') + 1);
        std::string Lower = List; for (char& C : Lower) if (C >= 'A' && C <= 'Z') C = char(C + 32);
        if (!PackageName(List) || List.compare(0, Naming.Root.size() + 1, Naming.Root + '/') == 0 || Lower.compare(0, 11, "/game/mods/") == 0 ||
            Leaf.compare(0, 3, "AP_") != 0) { Error = "A placement change must name one of the game's placement lists"; return false; }
        if (std::find(SeenGameLists.begin(), SeenGameLists.end(), Lower) != SeenGameLists.end()) { Error = "A placement list is named twice"; return false; }
        SeenGameLists.push_back(Lower);
        if (Edit.Remove.empty() && Edit.Move.empty()) continue;
        std::vector<std::uint64_t> Rows(Edit.Remove);
        for (const auto& Move : Edit.Move) { if (!ValidTransform(Move.second)) { Error = "Invalid placement row move"; return false; } Rows.push_back(Move.first); }
        std::sort(Rows.begin(), Rows.end());
        if (std::adjacent_find(Rows.begin(), Rows.end()) != Rows.end() || (!Rows.empty() && Rows.front() == 0)) { Error = "A placement row is changed twice or has no number"; return false; }
        if (!RowChanges.empty()) RowChanges += ',';
        RowChanges += Quote(List) + ":{"; bool Part = false;
        if (!Edit.Move.empty()) {
            RowChanges += "\"move\":{"; bool First = true; Part = true;
            for (const auto& Move : Edit.Move) {
                if (!First) RowChanges += ','; First = false;
                RowChanges += Quote(std::to_string(Move.first)) + ":{\"location\":" + Vector(Move.second.Location) + ",\"rotation\":" + Vector(Move.second.Rotation) + ",\"scale\":" + Vector(Move.second.Scale) + '}';
            }
            RowChanges += '}';
        }
        if (!Edit.Remove.empty()) {
            if (Part) RowChanges += ',';
            std::vector<std::uint64_t> Removed(Edit.Remove); std::sort(Removed.begin(), Removed.end());
            RowChanges += "\"remove\":[";
            for (std::size_t I = 0; I < Removed.size(); ++I) { if (I) RowChanges += ','; RowChanges += Quote(std::to_string(Removed[I])); }
            RowChanges += ']';
        }
        RowChanges += '}';
    }
    if (!HasChanges && RowChanges.empty() && Map.Levels.empty() && Map.Placements.empty() && !Map.Navigation) { Error = "Nothing of the map was changed"; return false; }
    std::map<std::string,std::string> Prepared;
    const auto Leaf = FileStem.empty() ? Map.Package.substr(Map.Package.rfind('/') + 1) : FileStem;
    if (!Plain(Leaf)) { Error = "Invalid map output name"; return false; }
    std::string Candidate = "{\"map\":" + Quote(Map.Package);
    if (!Map.Levels.empty()) Candidate += ",\"levels\":" + StringList(Map.Levels);
    if (!Map.Placements.empty()) Candidate += ",\"placements\":" + StringList(Map.Placements);
    if (!RowChanges.empty()) Candidate += ",\"placement_changes\":{" + RowChanges + '}';
    if (HasChanges) { const std::string Path = "maps/" + Leaf + ".json"; Candidate += ",\"changes\":" + Quote(Path); Prepared[Path] = Changes; }
    if (Map.Navigation) Candidate += ",\"navigation\":" + Quote("maps/" + Leaf + ".nav");
    Candidate += '}'; Entry.swap(Candidate);
    Files.swap(Prepared); return true;
}
inline bool FrameworkFiles(const Descriptor& Mod, const std::vector<MapEdit>& Maps,
    std::map<std::string,std::string>& Files, std::string& Error) {
    if (Maps.empty() || Maps.size() > 256) { Error = "Choose between one and 256 edited maps"; return false; }
    std::vector<std::string> Seen;
    std::map<std::string,std::string> Prepared;
    std::string Entries;
    for (std::size_t Index = 0; Index < Maps.size(); ++Index) {
        const auto& Map = Maps[Index];
        std::string Key = Map.Package;
        for (char& C : Key) if (C >= 'A' && C <= 'Z') C = char(C + 'a' - 'A');
        if (std::find(Seen.begin(), Seen.end(), Key) != Seen.end()) { Error = "An edited map is selected twice"; return false; }
        Seen.push_back(Key);
        // Multi-map outputs have disjoint filenames even when two maps have the same asset name.
        const auto Leaf = Map.Package.substr(Map.Package.rfind('/') + 1);
        const std::string Stem = Maps.size() == 1 ? Leaf : std::to_string(Index + 1) + '_' + Leaf;
        std::string Entry; std::map<std::string,std::string> Parts;
        if (!MapFiles(Mod, Map, Entry, Parts, Error, Stem)) return false;
        for (const auto& Part : Parts) if (!Prepared.emplace(Part).second) { Error = "Map output filenames collide"; return false; }
        if (!Entries.empty()) Entries += ',';
        Entries += Entry;
    }
    Prepared["mod.json"] = "{\"id\":" + Quote(Mod.Id) + ",\"name\":" + Quote(Mod.Name) + ",\"version\":" + Quote(Mod.Version) +
        ",\"framework\":" + std::to_string(MinimumFrameworkApi) + ",\"maps\":[" + Entries + "]}\n";
    Files.swap(Prepared); return true;
}
inline bool FrameworkFiles(const Descriptor& Mod, const MapEdit& Map,
    std::map<std::string,std::string>& Files, std::string& Error) {
    return FrameworkFiles(Mod, std::vector<MapEdit>{Map}, Files, Error);
}
// Admission evidence is private, never serialized into a shareable mod. The caller must verify the
// candidate digest against cooked bytes, verify the source is creator-authored, and provide the
// original digest from the user's game. A changed game package alone proves no author origin.
struct Origin {
    std::string Package, Kind, CookedDigest, OriginalDigest;
    bool CreatorAuthored = false, CookedDigestVerified = false, SourceVerified = false;
};
inline bool Digest(const std::string& Text) {
    if (Text.size() != 64) return false;
    for (char C : Text) if (!(C >= '0' && C <= '9') && !(C >= 'a' && C <= 'f')) return false;
    return true;
}
inline bool AdmitPackage(const std::string& Package, bool IsGamePackage, const Origin* Proof, std::string& Error) {
    if (!PackageName(Package)) { Error = "Invalid cooked package name"; return false; }
    if (!IsGamePackage) return true;
    if (!Proof || Proof->Package != Package || !Proof->CreatorAuthored || !Proof->CookedDigestVerified || !Proof->SourceVerified ||
        !Digest(Proof->CookedDigest) || !Digest(Proof->OriginalDigest) || Proof->CookedDigest == Proof->OriginalDigest ||
        (Proof->Kind != "Texture2D" && Proof->Kind != "StaticMesh" && Proof->Kind != "SoundWave")) {
        Error = "A game-path package needs verified creator-authored replacement evidence"; return false;
    }
    return true;
}
struct CookedPackage { std::string Package; bool IsGamePackage = false; const Origin* Proof = nullptr; };
inline bool CheckPackageInventory(const std::vector<CookedPackage>& Packages, bool GameInventoryAvailable, std::string& Error) {
    if (!Packages.empty() && !GameInventoryAvailable) { Error = "The game package inventory is unavailable"; return false; }
    std::vector<std::string> Seen;
    for (const auto& Package : Packages) {
        std::string Canonical = Package.Package;
        for (char& C : Canonical) if (C >= 'A' && C <= 'Z') C = char(C + 'a' - 'A');
        if (std::find(Seen.begin(), Seen.end(), Canonical) != Seen.end()) { Error = "A cooked package is duplicated"; return false; }
        Seen.push_back(Canonical);
        if (!AdmitPackage(Package.Package, Package.IsGamePackage, Package.Proof, Error)) return false;
    }
    return true;
}
inline bool AllowedFile(const Descriptor& Mod, const std::string& Relative, std::string& Error) {
    Names Naming; if (!DerivedNames(Mod, Naming, Error)) return false;
    const std::string Own = "romfs/tsuchi/mods/" + Mod.Id + '/';
    // A mod's shader library travels inside its .pak, where Tsuchi opens it. sounds.bnk is the creator's sound
    // bank, which mod.json names under "banks".
    if (Relative == Own + "mod.json" || Relative == Own + "sounds.bnk") return true;
    const std::string Maps = Own + "maps/";
    if (Relative.compare(0, Maps.size(), Maps) == 0) {
        const auto File = Relative.substr(Maps.size()); const auto Dot = File.rfind('.');
        if (Dot != std::string::npos && Plain(File.substr(0,Dot)) && (File.substr(Dot) == ".json" || File.substr(Dot) == ".nav")) return true;
    }
    for (const char* Extension : {".pak", ".ucas", ".utoc"})
        if (Relative == Own + Naming.Container + "_P" + Extension) return true;
    Error = "The mod carries an unexpected file"; return false;
}
} } // namespace P4MapKit::Export

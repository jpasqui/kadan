// User-owned original-game declaration evidence. Validation binds identities and
// bytes; it does not authenticate the producer or prove a complete live capture.
#pragma once
#include "blueprint_json.h"
#include "custom_material_hash.h"

namespace P4MapKit { namespace OriginalDeclarations {
using Blueprint::J;
using Blueprint::Error;
using CustomMaterial::Bytes;
struct Input
{
    std::string Name, Path, Sha256, Producer, SourceExecutableSha256, Provenance;
    unsigned Schema = 0;
};
struct Manifest
{
    std::string Format, GameVersion, OriginalExecutableSha256, EvidenceStatus;
    unsigned Version = 0;
    std::vector<std::string> Modules;
    std::vector<Input> Inputs;
};
inline const std::set<std::string>& ModuleIds()
{
    static const std::set<std::string> Values{"P4StubsCore", "Carrot4", "AkAudio", "EasyParticle", "UIParticle", "SwitchRuntimeSettings"};
    return Values;
}
inline bool Digest(const std::string& Value)
{
    return Value.size() == 64 && std::all_of(Value.begin(), Value.end(), [](char C) { return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'); });
}
inline std::string Fold(std::string Value)
{
    for (auto& C : Value) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
    return Value;
}
inline bool RelativePath(const std::string& Value)
{
    if (Value.empty() || Value.size() > 512 || Value.front() == '/' || Value.back() == '/') return false;
    std::size_t Start = 0;
    while (Start < Value.size())
    {
        const auto End = Value.find('/', Start);
        const auto Part = Value.substr(Start, End == std::string::npos ? std::string::npos : End - Start);
        if (Part.empty() || Part == "." || Part == ".." || Part.back() == '.' || Part.back() == ' ') return false;
        for (const char C : Part) if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-' || C == '.')) return false;
        const auto Device = Fold(Part.substr(0, Part.find('.')));
        if (Device == "con" || Device == "prn" || Device == "aux" || Device == "nul" ||
            (Device.size() == 4 && (Device.substr(0, 3) == "com" || Device.substr(0, 3) == "lpt") && Device[3] >= '1' && Device[3] <= '9')) return false;
        if (End == std::string::npos) break;
        Start = End + 1;
    }
    return true;
}
inline bool ValidateManifest(const Manifest& Value, const std::vector<std::string>& InstalledModules, std::string& Why)
{
    const auto Refuse = [&](const std::string& Message) { Why = Message; return false; };
    Why.clear();
    if (Value.Format != "p4-original-declarations" || Value.Version != 1) return Refuse("original declaration manifest version unsupported");
    // Layout extraction presently supports this game's original 1.1.1 only.
    if (Value.GameVersion != "1.1.1" || !Digest(Value.OriginalExecutableSha256)) return Refuse("original executable identity required");
    if (Value.EvidenceStatus != "user-supplied") return Refuse("manifest does not authenticate extraction; user-supplied evidence status required");
    if (Value.Modules.size() != ModuleIds().size() || std::set<std::string>(Value.Modules.begin(), Value.Modules.end()) != ModuleIds()) return Refuse("six original runtime module identities required");
    for (const auto& Module : InstalledModules) for (const auto& Expected : ModuleIds())
        if (Fold(Module) == Fold(Expected)) return Refuse("installed module identity collision: " + Module);
    if (Value.Inputs.size() != 3) return Refuse("all three original evidence inputs required");
    std::set<std::string> Names, Paths;
    for (const auto& Item : Value.Inputs)
    {
        const bool Runtime = Item.Name == "rich_reflection" || Item.Name == "default_objects";
        const bool Bytecode = Item.Name == "zero_constructor";
        if ((!Runtime && !Bytecode) || !Names.insert(Item.Name).second) return Refuse("unknown or duplicate original evidence input");
        if (Item.Schema != (Item.Name == "rich_reflection" ? 2u : 1u)) return Refuse("original evidence schema unsupported: " + Item.Name);
        if (!RelativePath(Item.Path) || !Paths.insert(Fold(Item.Path)).second) return Refuse("original evidence path invalid or aliased");
        if (!Digest(Item.Sha256) || Item.SourceExecutableSha256 != Value.OriginalExecutableSha256) return Refuse("original evidence hash or executable identity mismatch");
        if (Item.Producer.empty() || Item.Producer.size() > 128 || !std::all_of(Item.Producer.begin(), Item.Producer.end(), [](char C) { return C >= 32 && C <= 126; })) return Refuse("original evidence producer required");
        if (Item.Provenance != (Runtime ? "owner-game-runtime-capture" : "owner-game-bytecode-analysis")) return Refuse("original evidence provenance mismatch");
    }
    return true;
}
inline void ExactFields(const J& Value, const std::set<std::string>& Expected)
{
    if (Value.Kind != J::K::Object || Value.Fields.size() != Expected.size()) throw Error("original declaration manifest fields required");
    for (const auto& Field : Value.Fields) if (!Expected.count(Field.first)) throw Error("unknown original declaration manifest field: " + Field.first);
}
inline Manifest ParseManifest(const std::string& Text)
{
    const auto Root = J::Parse(Text, 64 * 1024, 128, 8);
    ExactFields(Root, {"format", "version", "game_version", "original_executable_sha256", "evidence_status", "modules", "inputs"});
    if (Root["version"].Kind != J::K::Number || Root["version"].Text != "1") throw Error("exact original declaration version required");
    if (Root["modules"].Kind != J::K::Array || Root["inputs"].Kind != J::K::Array) throw Error("original declaration arrays required");
    Manifest Out;
    Out.Format = Root["format"].S(); Out.Version = 1; Out.GameVersion = Root["game_version"].S();
    Out.OriginalExecutableSha256 = Root["original_executable_sha256"].S(); Out.EvidenceStatus = Root["evidence_status"].S();
    for (const auto& Module : Root["modules"].Items) Out.Modules.push_back(Module.S());
    for (const auto& Row : Root["inputs"].Items)
    {
        ExactFields(Row, {"name", "schema", "path", "sha256", "producer", "source_executable_sha256", "provenance"});
        if (Row["schema"].Kind != J::K::Number || (Row["schema"].Text != "1" && Row["schema"].Text != "2")) throw Error("exact original evidence schema required");
        Input Item; Item.Name = Row["name"].S(); Item.Schema = unsigned(Row["schema"].U()); Item.Path = Row["path"].S();
        Item.Sha256 = Row["sha256"].S(); Item.Producer = Row["producer"].S(); Item.SourceExecutableSha256 = Row["source_executable_sha256"].S(); Item.Provenance = Row["provenance"].S();
        Out.Inputs.push_back(std::move(Item));
    }
    std::string Why;
    if (!ValidateManifest(Out, {}, Why)) throw Error(Why);
    return Out;
}
inline bool VerifyInput(const Manifest& Value, const std::string& Name, const std::string& RelativeIdentity, const Bytes& LoadedBytes, std::string& Why)
{
    if (!ValidateManifest(Value, {}, Why)) return false;
    for (const auto& Item : Value.Inputs) if (Item.Name == Name)
    {
        // Exact bytes are hashed, including whitespace. No compaction or rewrite.
        const std::size_t Limit = (Name == "rich_reflection" ? 128u : Name == "default_objects" ? 64u : 4u) * 1024u * 1024u;
        if (LoadedBytes.empty() || LoadedBytes.size() > Limit) { Why = "original evidence byte budget exceeded or empty"; return false; }
        if (Item.Path != RelativeIdentity) { Why = "original evidence path identity mismatch"; return false; }
        if (CustomMaterial::Hex(CustomMaterial::Sha256(LoadedBytes)) != Item.Sha256) { Why = "original evidence bytes changed"; return false; }
        Why.clear(); return true;
    }
    Why = "original evidence input identity unknown"; return false;
}
}} // namespace P4MapKit::OriginalDeclarations

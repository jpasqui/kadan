// The Groink v135 PCM one-shot authoring path, shared by native editor export.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include <limits>

namespace P4MapKit { namespace WwisePCM
{
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t MaxBankBytes = 16 * 1024 * 1024;
struct Sample { std::string Label; std::uint32_t Rate = 0, Channels = 0; Bytes PCM; };
struct Event { std::string Label, Name; std::uint32_t EventId = 0, SoundId = 0, ActionId = 0, MediaId = 0; };
struct Bank { Bytes Data; std::uint32_t Id = 0; std::vector<Event> Events; };
inline void U16(Bytes& B, std::uint16_t V) { B.push_back(std::uint8_t(V)); B.push_back(std::uint8_t(V >> 8)); }
inline void U32(Bytes& B, std::uint32_t V) { for (int I = 0; I < 4; ++I) B.push_back(std::uint8_t(V >> (8 * I))); }
inline void Append(Bytes& B, const Bytes& Other) { B.insert(B.end(), Other.begin(), Other.end()); }
inline void Chunk(Bytes& B, const char* Tag, const Bytes& Data)
{
    if (Data.size() > MaxBankBytes - 8 || B.size() > MaxBankBytes - Data.size() - 8)
        throw std::runtime_error("Wwise bank exceeds 16 MiB");
    B.insert(B.end(), Tag, Tag + 4); U32(B, std::uint32_t(Data.size())); Append(B, Data);
}
inline std::uint32_t Id(const std::string& Name)
{
    std::uint32_t H = 2166136261u;
    for (unsigned char C : Name)
    {
        if (C >= 128) throw std::runtime_error("Wwise names must use ASCII");
        if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
        H = (H * 16777619u) ^ C;
    }
    return H;
}
inline bool Label(const std::string& Name)
{
    if (Name.empty() || Name.size() > 48) return false;
    for (char C : Name) if (!(C >= 'a' && C <= 'z') && !(C >= '0' && C <= '9') && C != '_') return false;
    return true;
}
inline bool Namespace(const std::string& Name)
{
    if (Name.empty() || Name.size() > 96 || Name[0] < 'a' || Name[0] > 'z') return false;
    for (char C : Name) if (!(C >= 'a' && C <= 'z') && !(C >= '0' && C <= '9') && C != '_' && C != '.' && C != '-') return false;
    return true;
}
inline bool EventAssetName(const std::string& Name)
{
    // Wwise's name hash is ASCII; an actual UObject asset name cannot contain
    // the dots/path separators used by a standalone bank namespace.
    if (Name.empty() || Name.size() > 96) return false;
    std::string Lower;
    for (unsigned char C : Name)
    {
        if (!(C >= 'a' && C <= 'z') && !(C >= 'A' && C <= 'Z') && !(C >= '0' && C <= '9') && C != '_') return false;
        Lower.push_back(char(C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C));
    }
    return Lower != "none";
}
inline Bytes Media(const Sample& S)
{
    if ((S.Channels != 1 && S.Channels != 2) || S.Rate < 8000 || S.Rate > 192000 || S.PCM.empty() ||
        S.PCM.size() % (S.Channels * 2) || S.PCM.size() / (S.Channels * 2) > std::size_t(S.Rate) * 30)
        throw std::runtime_error("Wwise requires PCM16 mono or stereo of at most 30 seconds");
    Bytes Format;
    U16(Format, 0xfffe); U16(Format, std::uint16_t(S.Channels)); U32(Format, S.Rate);
    U32(Format, S.Rate * S.Channels * 2); U16(Format, std::uint16_t(S.Channels * 2)); U16(Format, 16);
    U16(Format, 6); U16(Format, 16); U32(Format, S.Channels | (1u << 8) | ((S.Channels == 1 ? 4u : 3u) << 12));
    Bytes Riff{'W','A','V','E'};
    Chunk(Riff, "fmt ", Format); Chunk(Riff, "JUNK", Bytes(4)); Chunk(Riff, "data", S.PCM);
    Bytes Out{'R','I','F','F'}; U32(Out, std::uint32_t(Riff.size())); Append(Out, Riff); return Out;
}
inline Bytes Sound(std::uint32_t MediaId, std::uint32_t Size, std::uint32_t Bus, bool Spatial)
{
    Bytes B; U32(B, 0x10001); B.push_back(0); U32(B, MediaId); U32(B, Size); B.push_back(0x80);
    Append(B, {1,0,0}); U32(B, Bus); U32(B, 0); Append(B, {0,0,0});
    if (Spatial) Append(B, {3,1}); else B.push_back(1);
    B.push_back(0); U32(B, 0); Append(B, {0x10,0,0,0,0,0,0,0,0,0}); return B;
}
inline void Object(Bytes& B, std::uint8_t Kind, std::uint32_t ObjectId, const Bytes& Body)
{ B.push_back(Kind); U32(B, std::uint32_t(Body.size() + 4)); U32(B, ObjectId); Append(B, Body); }
inline Bank Build(const std::vector<Sample>& Samples, const std::string& Name,
    std::uint32_t ProjectId, std::uint32_t Bus, const std::set<std::uint32_t>& Reserved = {}, bool Spatial = true,
    const std::vector<std::string>& EventNames = {})
{
    if (!Namespace(Name) || Samples.empty() || Samples.size() > 64) throw std::runtime_error("Wwise bank needs a namespace and 1 to 64 samples");
    if (!EventNames.empty() && EventNames.size() != Samples.size()) throw std::runtime_error("Every selected sample needs its exact event asset name");
    Bank Out; Out.Id = Id(Name + ".bank");
    std::set<std::uint32_t> Used;
    const auto Claim = [&](std::uint32_t Value) {
        if (!Value || Reserved.count(Value) || !Used.insert(Value).second) throw std::runtime_error("Wwise IDs collide");
    };
    Claim(Out.Id);
    Bytes Data, Index, Objects;
    for (std::size_t I = 0; I < Samples.size(); ++I)
    {
        const Sample& S = Samples[I];
        if (!Label(S.Label)) throw std::runtime_error("Wwise sample label is invalid");
        Event E; E.Label = S.Label; const std::string Base = Name + "." + S.Label;
        E.Name = EventNames.empty() ? Base : EventNames[I];
        if (!EventNames.empty() && !EventAssetName(E.Name)) throw std::runtime_error("Wwise event needs a valid ASCII UObject asset name");
        E.EventId = Id(E.Name); E.SoundId = Id(Base + ".sound"); E.ActionId = Id(Base + ".play"); E.MediaId = Id(Base + ".media");
        for (auto Value : {E.EventId, E.SoundId, E.ActionId, E.MediaId}) Claim(Value);
        Bytes Wem = Media(S);
        Data.resize(Data.size() + ((16 - Data.size() % 16) % 16), 0);
        if (Data.size() > MaxBankBytes - Wem.size()) throw std::runtime_error("Wwise bank exceeds 16 MiB");
        U32(Index, E.MediaId); U32(Index, std::uint32_t(Data.size())); U32(Index, std::uint32_t(Wem.size())); Append(Data, Wem);
        Object(Objects, 2, E.SoundId, Sound(E.MediaId, std::uint32_t(Wem.size()), Bus, Spatial));
        Bytes Action; U16(Action, 0x403); U32(Action, E.SoundId); Append(Action, {0,0,0,4}); U32(Action, Out.Id);
        Object(Objects, 3, E.ActionId, Action);
        Bytes Play{1}; U32(Play, E.ActionId); Object(Objects, 4, E.EventId, Play);
        Out.Events.push_back(E);
    }
    Bytes Header; U32(Header, 135); U32(Header, Out.Id); U32(Header, 0); U32(Header, 16); U32(Header, ProjectId);
    Header.resize(Header.size() + ((16 - (8 + Header.size() + 8 + Index.size() + 8) % 16) % 16), 0);
    Bytes Hirc; U32(Hirc, std::uint32_t(Samples.size() * 3)); Append(Hirc, Objects);
    Chunk(Out.Data, "BKHD", Header); Chunk(Out.Data, "DIDX", Index); Chunk(Out.Data, "DATA", Data); Chunk(Out.Data, "HIRC", Hirc);
    return Out;
}
}}

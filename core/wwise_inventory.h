#pragma once
#include "wwise_pcm.h"
#include <cstring>

namespace P4MapKit { namespace WwisePCM
{
struct Inventory
{
    std::set<std::uint32_t> Ids;
    std::uint32_t Project = 0;
    std::size_t Banks = 0;
    bool HasInit = false;
};
inline std::uint32_t ReadU32(const Bytes& B, std::size_t At)
{
    if (At > B.size() || B.size() - At < 4) throw std::runtime_error("Truncated Wwise integer");
    return std::uint32_t(B[At]) | (std::uint32_t(B[At + 1]) << 8) |
        (std::uint32_t(B[At + 2]) << 16) | (std::uint32_t(B[At + 3]) << 24);
}
// Reads identities from the user's actual banks, never copies their media/HIRC into creator output.
inline void ReadBankInventory(const Bytes& B, bool Init, Inventory& Out)
{
    Inventory Next = Out;
    bool Header = false, Hierarchy = false, MediaIndex = false;
    std::uint32_t Project = 0;
    for (std::size_t At = 0; At < B.size();)
    {
        if (B.size() - At < 8) throw std::runtime_error("Truncated Wwise chunk");
        const std::size_t Size = ReadU32(B, At + 4), Start = At + 8;
        if (Size > B.size() - Start) throw std::runtime_error("Truncated Wwise chunk body");
        const auto Tag = [&](const char* S) { return std::memcmp(B.data() + At, S, 4) == 0; };
        if (Tag("BKHD"))
        {
            if (Header || Size < 20 || (ReadU32(B, Start) != 135 && (Init || ReadU32(B, Start) != 134)))
                throw std::runtime_error("Expected Wwise v134/v135 bank with v135 Init");
            Header = true; Project = ReadU32(B, Start + 16);
            const auto BankId = ReadU32(B, Start + 4);
            if (!BankId || !Project) throw std::runtime_error("Invalid Wwise bank identity");
            Next.Ids.insert(BankId);
        }
        else if (Tag("DIDX"))
        {
            if (MediaIndex || Size % 12) throw std::runtime_error("Invalid Wwise media index");
            MediaIndex = true;
            for (std::size_t Offset = 0; Offset < Size; Offset += 12) Next.Ids.insert(ReadU32(B, Start + Offset));
        }
        else if (Tag("HIRC"))
        {
            if (Hierarchy || Size < 4) throw std::runtime_error("Invalid Wwise hierarchy");
            Hierarchy = true;
            const auto Count = ReadU32(B, Start);
            if (Count > (Size - 4) / 9) throw std::runtime_error("Invalid Wwise object count");
            std::size_t Cursor = Start + 4;
            for (std::uint32_t I = 0; I < Count; ++I)
            {
                if (Cursor > Start + Size || Start + Size - Cursor < 9) throw std::runtime_error("Truncated Wwise object");
                const auto Kind = B[Cursor]; const std::size_t ObjectSize = ReadU32(B, Cursor + 1);
                if (ObjectSize < 4 || ObjectSize > Start + Size - Cursor - 5) throw std::runtime_error("Invalid Wwise object size");
                Next.Ids.insert(ReadU32(B, Cursor + 5));
                // Sound source ID follows its plugin ID and stream kind; this also reserves streamed media.
                if (Kind == 2)
                {
                    if (ObjectSize < 13) throw std::runtime_error("Truncated Wwise Sound source");
                    Next.Ids.insert(ReadU32(B, Cursor + 14));
                }
                Cursor += 5 + ObjectSize;
            }
            if (Cursor != Start + Size) throw std::runtime_error("Trailing Wwise hierarchy bytes");
        }
        At = Start + Size;
    }
    if (!Header || (Init && !Hierarchy)) throw std::runtime_error("Wwise bank header or Init hierarchy missing");
    if (Init)
    {
        if (Next.HasInit && Next.Project != Project) throw std::runtime_error("Wwise Init projects disagree");
        Next.Project = Project; Next.HasInit = true;
    }
    ++Next.Banks;
    Out = std::move(Next);
}
}}

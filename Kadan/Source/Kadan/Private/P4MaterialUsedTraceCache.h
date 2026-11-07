#pragma once
#include "CoreMinimal.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_used_resources.h"
THIRD_PARTY_INCLUDES_END

// A shader trace is determined by its owner, held archive and every source read, including
// absent packages. Reference-only outcomes are just as reusable as successful traces. Failed
// resolver calls are not complete read witnesses and must never become cached refusals.
class FP4MaterialUsedTraceCache
{
    using FRaw = P4MapKit::Material::MaterialOriginalImportRawAsset;
    using FResolver = P4MapKit::Material::MaterialOriginalImportRawResolver;
    using FArchive = P4MapKit::Material::MaterialOriginalArchiveAccess;
    using FDiscovery = P4MapKit::Material::FMaterialUsedResourceDiscovery;

public:
    using FReads = std::vector<std::pair<std::string, std::string>>;

private:
    struct FMemo
    {
        FReads Reads;
        FDiscovery Discovery;
    };
    std::map<std::string, std::vector<FMemo>> Memos;
    std::size_t Kept = 0;

    static bool BoundArchive(const std::string& ArchiveSha256)
    {
        return ArchiveSha256.size() == 64 && ArchiveSha256.find_first_not_of("0123456789abcdef") == std::string::npos;
    }
    static std::string Key(const std::string& ArchiveSha256, const std::string& Owner)
    {
        return ArchiveSha256 + "\n" + Owner;
    }

public:
    int32 GraphHits = 0;

    static std::string Digest(const FRaw& In)
    {
        FSHA1 Hash;
        const uint64 Sizes[2] = {In.Header ? uint64(In.Header->size()) : 0, In.Body ? uint64(In.Body->size()) : 0};
        Hash.Update(reinterpret_cast<const uint8*>(Sizes), sizeof(Sizes));
        if (Sizes[0])
            Hash.Update(In.Header->data(), Sizes[0]);
        if (Sizes[1])
            Hash.Update(In.Body->data(), Sizes[1]);
        Hash.Final();
        uint8 Value[20];
        Hash.GetHash(Value);
        return std::string(reinterpret_cast<const char*>(Value), sizeof(Value));
    }

    // True when every read of a memo returns the same bytes (or absence) through Read now.
    static bool SameReads(const FReads& Reads, const FResolver& Read)
    {
        try
        {
            for (const auto& Was : Reads)
            {
                FRaw Now;
                const bool Found = Read(Was.first, Now);
                if (Found == Was.second.empty() || (Found && Digest(Now) != Was.second))
                    return false;
            }
        }
        catch (const P4MapKit::Material::Refused&)
        {
            return false;
        }
        return true;
    }

    // A resolver that records each read as a witness. A read that throws is no witness at all.
    static FResolver Logging(const FResolver& Read, FReads& Reads, bool& Cacheable)
    {
        return [&Read, &Reads, &Cacheable](const std::string& Package, FRaw& Out)
        {
            try
            {
                const bool Found = Read(Package, Out);
                Reads.emplace_back(Package, Found ? Digest(Out) : std::string());
                return Found;
            }
            catch (...)
            {
                Cacheable = false;
                throw;
            }
        };
    }

    // A memo for this owner and held archive whose every read returns the same bytes now.
    bool Find(const std::string& Owner, const FResolver& Read, const std::string& ArchiveSha256, FDiscovery& Out)
    {
        check(IsInGameThread());
        if (!BoundArchive(ArchiveSha256))
            return false;
        const auto Prior = Memos.find(Key(ArchiveSha256, Owner));
        if (Prior == Memos.end())
            return false;
        for (const auto& Memo : Prior->second)
            if (SameReads(Memo.Reads, Read))
            {
                Out = Memo.Discovery;
                return true;
            }
        return false;
    }

    void Keep(const std::string& Owner, const std::string& ArchiveSha256, FReads Reads, const FDiscovery& Discovery)
    {
        check(IsInGameThread());
        if (!BoundArchive(ArchiveSha256))
            return;
        if (Kept >= 4096)
        {
            Memos.clear();
            Kept = 0;
        }
        auto& Slot = Memos[Key(ArchiveSha256, Owner)];
        if (Slot.size() < 8)
        {
            Slot.push_back({std::move(Reads), Discovery});
            ++Kept;
        }
    }

    // The owner graph a complete trace recovered, when every read of that trace returns the same bytes
    // through Read and the owner was recovered with the same parameter collections.
    bool FindGraph(const std::string& Owner, const FResolver& Read, const std::string& ArchiveSha256,
        const std::vector<std::string>& Collections, P4MapKit::Material::Json& Graph)
    {
        check(IsInGameThread());
        if (!BoundArchive(ArchiveSha256))
            return false;
        const auto Prior = Memos.find(Key(ArchiveSha256, Owner));
        if (Prior == Memos.end())
            return false;
        std::vector<std::string> Wanted = Collections;
        std::sort(Wanted.begin(), Wanted.end());
        for (const auto& Memo : Prior->second)
        {
            if (Memo.Discovery.Status != P4MapKit::Material::EMaterialUsedResourceStatus::Complete)
                continue;
            std::vector<std::string> Used = Memo.Discovery.CollectionPackages;
            std::sort(Used.begin(), Used.end());
            if (Used != Wanted || !SameReads(Memo.Reads, Read))
                continue;
            Graph = Memo.Discovery.Graph;
            ++GraphHits;
            return true;
        }
        return false;
    }

    template <typename TTrace>
    FDiscovery Discover(const std::string& Owner, const FResolver& Read, const FArchive& Archive,
        const std::string& ArchiveSha256, const TTrace& Trace)
    {
        FDiscovery Known;
        if (Find(Owner, Read, ArchiveSha256, Known))
            return Known;
        FReads Reads;
        bool Cacheable = true;
        auto Discovery = Trace(Owner, Logging(Read, Reads, Cacheable), Archive);
        if (Cacheable)
            Keep(Owner, ArchiveSha256, std::move(Reads), Discovery);
        return Discovery;
    }
};

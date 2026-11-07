#pragma once
#include "CoreMinimal.h"
#include "P4GamePreviewContent.h"

class FP4GameLink;
struct FP4GameReply;
class IFileHandle;

// Prepares creator assets in the already running game. The Lua cache owns mounted
// bundles for the lifetime of that game, independently of an editor connection.
class FP4GamePreviewUpload : public TSharedFromThis<FP4GamePreviewUpload>
{
public:
    using FDone = TFunction<void(bool, const FString&)>;
    explicit FP4GamePreviewUpload(FP4GameLink& InLink, FDone InDone);
    ~FP4GamePreviewUpload();
    void Start(const FP4GamePreviewStartResult* FrozenStart = nullptr);
    // Call before destroying the link. In-flight callbacks use weak ownership.
    void Cancel();
    const FString& Status() const { return Progress; }
    const FString& Fingerprint() const { return Plan.Fingerprint; }
    // The creator packages this Play prepared for the game.
    const TArray<FString>& Packages() const { return Plan.SourcePackages; }
    // Empty for an ordinary game map. Scratch worlds use a reserved native area.
    const FString& PreviewWorldName() const { return ReservedWorld; }
    // The caller can travel to a neutral map and start a new task. Mounted content
    // stays cached, so that retry needs neither a recook nor an emulator restart.
    bool NeedsNeutralTravel() const { return bNeedsNeutralTravel; }

private:
    struct FPart
    {
        FString Path;
        int32 Kind = 0;
        int64 Size = 0;
    };
    void CheckCache();
    void ReserveWorld(int32 Attempt = 0);
    void Cook();
    void BeginBundle(int32 Attempt = 0);
    void StageAliases();
    void SendNextChunk();
    void SendChunk(int32 Attempt = 0);
    void Mount(int32 Attempt = 0);
    void ResolveMount(const FString& Why, int32 Attempt = 0);
    void Activate(int32 Attempt = 0);
    void Finish(bool bOk, const FString& Error = FString());
    void ReleaseUnfinished();
    FString CacheEntry() const;
    FString OwnEntry() const;
    FString Failure(const FP4GameReply& Reply) const;

    FP4GameLink* Link;
    FDone Done;
    FP4GamePreviewContentPlan Plan;
    FP4GamePreviewContentResult Content;
    FString UploadId, Bundle, SpoolStem, ReservedWorld, Progress = TEXT("Checking imported assets");
    TArray<FPart> Parts;
    TArray<TPair<FString, FString>> Aliases;
    TUniquePtr<IFileHandle> File;
    TArray<uint8> Chunk;
    int32 PartIndex = 0, AliasIndex = 0, PreviewSlot = 0;
    int64 Offset = 0, Sent = 0, Total = 0;
    bool bFinished = false, bMounted = false, bCached = false, bNeedsNeutralTravel = false;
};

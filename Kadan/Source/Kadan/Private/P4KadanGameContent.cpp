#include "P4KadanGameContent.h"
#include "P4Common.h"
#include "P4GameNames.h"
#include "P4CreatorSource.h"
#include "P4GameReplacement.h"
#include "P4MaterialInstanceCopyContent.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "MeshDescription.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Sound/SoundWave.h"
#include "UObject/ObjectKey.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

namespace
{
const TCHAR* GameRoot = TEXT(P4_GAME_ASSET_ROOT);

// The content a creator could ship again unchanged. Empty for an asset of another kind or without that content.
FString ContentDigest(UObject* Asset)
{
    FSHA1 Hash;
    int64 Bytes = 0;
    const auto Add = [&](const void* Data, int64 Size)
    {
        if (Data && Size > 0)
        {
            Hash.Update(static_cast<const uint8*>(Data), uint64(Size));
            Bytes += Size;
        }
    };
    if (UTexture* Texture = Cast<UTexture>(Asset))
    {
        if (!Texture->Source.IsValid())
            return FString();
        const int32 Shape[4] = {Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), Texture->Source.GetNumSlices(),
            int32(Texture->Source.GetFormat())};
        Add(Shape, sizeof(Shape));
        TArray64<uint8> Mip;
        if (!Texture->Source.GetMipData(Mip, 0))
            return FString();
        Add(Mip.GetData(), Mip.Num());
    }
    else if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
    {
        FMeshDescription* Description = Mesh->GetMeshDescription(0);
        if (!Description)
            return FString();
        TArray<uint8> Data;
        FMemoryWriter Writer(Data);
        Writer << *Description;
        Add(Data.GetData(), Data.Num());
    }
    else if (USoundWave* Sound = Cast<USoundWave>(Asset))
    {
        const int64 Size = Sound->RawData.GetBulkDataSize();
        if (Size <= 0)
            return FString();
        Add(Sound->RawData.LockReadOnly(), Size);
        Sound->RawData.Unlock();
    }
    if (Bytes <= int64(sizeof(int32) * 4))
        return FString();
    Hash.Final();
    uint8 Digest[20];
    Hash.GetHash(Digest);
    return Asset->GetClass()->GetName() + TEXT(":") + BytesToHex(Digest, 20);
}

// A package's files as they are on disk now, so a stored digest is used only while they are unchanged.
FString Stamp(const FString& Package)
{
    FString Out;
    for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk")})
    {
        const FString File = FPackageName::LongPackageNameToFilename(Package, Extension);
        const int64 Size = IFileManager::Get().FileSize(*File);
        if (Size >= 0)
            Out += FString::Printf(
                TEXT("%s%lld@%lld;"), Extension, Size, IFileManager::Get().GetTimeStamp(*File).GetTicks());
    }
    return Out;
}

struct FGameDigests
{
    TMap<FString, TPair<FString, FString>> Packages; // package: stamp, digest ("" when not game content)
    TMap<FString, FString> ByDigest; // digest: game package
    TSet<FName> Classes; // the kinds already gathered this session
    bool Loaded = false;
};
FGameDigests& Digests()
{
    static FGameDigests State;
    return State;
}
FString DigestFile()
{
    return FPaths::ProjectSavedDir() / TEXT("Kadan/GameContentDigests.json");
}

void Load(FGameDigests& State)
{
    State.Loaded = true;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *DigestFile()) || !P4ParseJson(Text, Root) ||
        Root->GetIntegerField(TEXT("format")) != 1)
        return;
    const TSharedPtr<FJsonObject>* Packages = nullptr;
    if (!Root->TryGetObjectField(TEXT("packages"), Packages))
        return;
    for (const auto& Pair : (*Packages)->Values)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        if (Pair.Value->TryGetObject(Row))
            State.Packages.Add(Pair.Key,
                TPair<FString, FString>((*Row)->GetStringField(TEXT("stamp")), (*Row)->GetStringField(TEXT("digest"))));
    }
}

void Save(const FGameDigests& State)
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>(), Packages = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("format"), 1);
    for (const auto& Pair : State.Packages)
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("stamp"), Pair.Value.Key);
        Row->SetStringField(TEXT("digest"), Pair.Value.Value);
        Packages->SetObjectField(Pair.Key, Row);
    }
    Root->SetObjectField(TEXT("packages"), Packages);
    FString Text;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text, *DigestFile());
}

// How many FP4GameContentGatherHold scopes are open.
int32 Holds = 0;

// The packages loaded now, to tell which ones a gather loads.
TSet<FObjectKey> LoadedPackages()
{
    TSet<FObjectKey> Out;
    for (TObjectIterator<UPackage> It; It; ++It)
        Out.Add(FObjectKey(*It));
    return Out;
}

// Lets go of the packages loaded since Before that nothing else holds. An editor keeps every loaded asset while it has
// RF_Standalone, so a gather that loads every game texture or mesh of an imported game (14,000 of them for the whole
// game, 2026-10-22) would hold them all until the editor's next garbage collection, which never comes inside one
// export: the export ran out of memory. Their assets lose the flag, garbage is collected, and any still held by
// something else get the flag back. Changed packages and packages loaded before are left as they are.
void ReleaseLoadedSince(const TSet<FObjectKey>& Before)
{
    if (Holds > 0 || IsGarbageCollecting() || GIsSavingPackage || IsAsyncLoading())
        return;
    TArray<TWeakObjectPtr<UObject>> Cleared;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        UPackage* Package = *It;
        if (Before.Contains(FObjectKey(Package)) || Package == GetTransientPackage() || Package->IsDirty() ||
            Package->IsRooted())
            continue;
        ForEachObjectWithPackage(Package,
            [&Cleared](UObject* Object)
            {
                if (Object->HasAnyFlags(RF_Standalone))
                {
                    Object->ClearFlags(RF_Standalone);
                    Cleared.Add(Object);
                }
                return true;
            });
    }
    if (!Cleared.Num())
        return;
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    for (const TWeakObjectPtr<UObject>& Kept : Cleared)
        if (UObject* Object = Kept.Get())
            Object->SetFlags(RF_Standalone);
}

// Game assets read per collection: about a gigabyte of loaded textures or meshes at most.
constexpr int32 ReleaseEvery = 128;

// Only a game texture of the same size can hold the same pixels: its registry entry has the size (UTexture2D's
// "Dimensions", its source's logical size), so others are not loaded to be read. Empty when every one is read.
FString TextureSizeTag(const UObject* Asset)
{
    const UTexture2D* Texture = Cast<UTexture2D>(Asset);
    if (!Texture || Texture->GetClass() != UTexture2D::StaticClass() || !Texture->Source.IsValid())
        return FString();
    const FIntPoint Size = Texture->Source.GetLogicalSize();
    return FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
}

void Gather(FGameDigests& State, UClass* Kind, const FString& SizeTag, UObject* Asking)
{
    const FName Key(*(Kind->GetName() + (SizeTag.IsEmpty() ? FString() : TEXT(":") + SizeTag)));
    if (State.Classes.Contains(Kind->GetFName()) || State.Classes.Contains(Key))
        return;
    State.Classes.Add(Key);
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FARFilter Filter;
    Filter.ClassNames.Add(Kind->GetFName());
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(GameRoot);
    Filter.bRecursivePaths = true;
    TArray<FAssetData> Found;
    Registry.GetAssets(Filter, Found);
    // The asset asked about stays loaded whatever is collected.
    TStrongObjectPtr<UObject> Keep(Asking);
    TSet<FObjectKey> Before = LoadedPackages();
    const double Start = FPlatformTime::Seconds();
    int32 Read = 0, Skipped = 0, SinceRelease = 0;
    bool Changed = false;
    for (const FAssetData& Data : Found)
    {
        if (!SizeTag.IsEmpty() && Data.AssetClass == UTexture2D::StaticClass()->GetFName())
        {
            FString Dimensions;
            if (Data.GetTagValue(FName(TEXT("Dimensions")), Dimensions) && !Dimensions.IsEmpty() &&
                Dimensions != SizeTag)
            {
                ++Skipped;
                continue;
            }
        }
        const FString Package = Data.PackageName.ToString(), Now = Stamp(Package);
        TPair<FString, FString>* Known = State.Packages.Find(Package);
        if (!Known || Known->Key != Now)
        {
            const bool WasLoaded = Data.IsAssetLoaded();
            UObject* Game = Data.GetAsset();
            ++Read;
            FString Origin;
            // Only the game's own assets count; a creator asset at a game path does not. A replacement keeps the
            // digest of the game content it took the place of, which the game still has.
            const bool Own = Game && P4HasIntrinsicGameSource(Game, &Origin) && Origin == Package &&
                !P4HasCreatorSource(Game);
            FP4GameReplacement Replaced;
            const FString Digest = Own                         ? ContentDigest(Game)
                : Game && P4IsGameReplacement(Game, &Replaced) ? Replaced.GameDigest
                                                               : FString();
            State.Packages.Add(Package, TPair<FString, FString>(Now, Digest));
            Known = State.Packages.Find(Package);
            Changed = true;
            if (!WasLoaded && ++SinceRelease >= ReleaseEvery)
            {
                ReleaseLoadedSince(Before);
                Before = LoadedPackages();
                SinceRelease = 0;
            }
        }
        if (!Known->Value.IsEmpty())
            State.ByDigest.Add(Known->Value, Package);
    }
    if (SinceRelease)
        ReleaseLoadedSince(Before);
    if (Changed)
        Save(State);
    if (Read)
        UE_LOG(LogTemp, Display,
            TEXT(
                "Kadan: read %d game %s assets to compare content (%d of another size left unread) in %.1f s, %.2f GiB in use"),
            Read, *Key.ToString(), Skipped, FPlatformTime::Seconds() - Start,
            double(FPlatformMemory::GetStats().UsedPhysical) / double(1ull << 30));
    if (Read && Holds > 0)
        UE_LOG(LogTemp, Warning,
            TEXT(
                "Kadan: the game's %s assets were read while garbage collection was held, so they stay loaded until the editor collects garbage"),
            *Key.ToString());
}

// The kind of game asset a creator asset is compared with, or null when it is not compared by content.
UClass* ComparedKind(const UObject* Asset)
{
    if (Asset->IsA<UTexture>())
    {
        return UTexture::StaticClass();
    }
    if (Asset->IsA<UStaticMesh>())
    {
        return UStaticMesh::StaticClass();
    }
    if (Asset->IsA<USoundWave>())
    {
        return USoundWave::StaticClass();
    }
    return nullptr;
}
} //namespace

FP4GameContentGatherHold::FP4GameContentGatherHold()
{
    check(IsInGameThread());
    ++Holds;
}

FP4GameContentGatherHold::~FP4GameContentGatherHold()
{
    --Holds;
}

void P4PrepareGameContentMatches(const TArray<UObject*>& Assets)
{
    if (!IsInGameThread())
    {
        return;
    }
    FGameDigests& State = Digests();
    if (!State.Loaded)
    {
        Load(State);
    }
    for (UObject* Asset : Assets)
    {
        UClass* Kind = IsValid(Asset) ? ComparedKind(Asset) : nullptr;
        if (Kind)
        {
            Gather(State, Kind, TextureSizeTag(Asset), Asset);
        }
    }
}

bool P4FindGameContentMatch(UObject* Asset, FString& GamePackage)
{
    GamePackage.Reset();
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    if (Asset->IsA<UMaterialInstanceConstant>())
    {
        const FP4CopiedMICContent Copy = P4CompareCopiedMaterialInstanceContent(Asset);
        if (Copy.State != EP4CopiedMICContent::Equal)
            return false;
        GamePackage = Copy.OriginalPackage;
        return true; // Exact intrinsic origin only; never gather every game MIC.
    }
    UClass* Kind = ComparedKind(Asset);
    if (!Kind)
        return false;
    const FString Digest = ContentDigest(Asset);
    if (Digest.IsEmpty())
        return false;
    // A replacement holding again the game content of its own path.
    FP4GameReplacement Replaced;
    if (P4IsGameReplacement(Asset, &Replaced) && Replaced.GameDigest == Digest)
    {
        GamePackage = Replaced.Package;
        return true;
    }
    FGameDigests& State = Digests();
    if (!State.Loaded)
        Load(State);
    Gather(State, Kind, TextureSizeTag(Asset), Asset);
    const FString* Found = State.ByDigest.Find(Digest);
    if (!Found || *Found == Asset->GetOutermost()->GetName())
        return false;
    GamePackage = *Found;
    return true;
}

FString P4GameContentDigest(UObject* Asset)
{
    return IsInGameThread() && IsValid(Asset) ? ContentDigest(Asset) : FString();
}

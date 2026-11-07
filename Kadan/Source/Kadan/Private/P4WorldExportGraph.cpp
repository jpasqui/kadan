#include "P4WorldExportGraph.h"
#include "P4GamePaths.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Misc/PackageName.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool P4GraphPackage(const FString& Name)
{
    for (TCHAR Character : Name)
        if (Character == 0)
            return false;
    return P4IsValidContentPackage(Name);
}

struct FP4GraphFrame
{
    UWorld* World = nullptr;
    int32 Next = 0;
};
} //namespace

FP4WorldStreamingGraphResult P4InspectWorldStreamingGraph(UWorld* World, const FString& TargetPackage)
{
    FP4WorldStreamingGraphResult Result;
    const auto Fail = [&](EP4WorldStreamingGraphError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(
            EP4WorldStreamingGraphError::EditorThreadRequired, TEXT("Streaming inspection requires the editor thread"));
    if (!IsValid(World) || !World->PersistentLevel || World->HasAnyFlags(RF_ClassDefaultObject))
        return Fail(EP4WorldStreamingGraphError::WorldRequired, TEXT("Expected an actual authored World"));
    const FString SourcePackage = World->GetOutermost()->GetName();
    Result.SourcePackage = SourcePackage;
    Result.TargetPackage = TargetPackage;
    if (!P4GraphPackage(SourcePackage) || !P4GraphPackage(TargetPackage) ||
        SourcePackage.Equals(TargetPackage, ESearchCase::IgnoreCase))
        return Fail(EP4WorldStreamingGraphError::InvalidPackages,
            TEXT("Streaming export needs distinct /Game source and destination packages"));

    // Retain every loaded World while discovering descendants. Inspection never
    // flushes streaming, creates packages, dirties settings or changes visibility.
    TArray<TStrongObjectPtr<UWorld>> KeepAlive;
    TArray<FP4GraphFrame> Stack;
    TSet<FName> Active, Visited, Destinations;
    int32 StreamCount = 0;
    FString Error;
    EP4WorldStreamingGraphError ErrorCode = EP4WorldStreamingGraphError::None;
    auto Enter = [&](UWorld* Current)
    {
        if (!IsValid(Current) || !Current->PersistentLevel)
        {
            ErrorCode = EP4WorldStreamingGraphError::MissingWorld;
            Error = TEXT("Missing authored streaming World");
            return false;
        }
        const FString Package = Current->GetOutermost()->GetName();
        if (!P4GraphPackage(Package) || Current->GetName() != FPackageName::GetLongPackageAssetName(Package))
        {
            ErrorCode = EP4WorldStreamingGraphError::InvalidWorldIdentity;
            Error = TEXT("Streaming reference is not its /Game World asset: ") + Package;
            return false;
        }
        if (Visited.Num() >= 4096 || Stack.Num() >= 128)
        {
            ErrorCode = EP4WorldStreamingGraphError::WorldLimit;
            Error = TEXT("Streaming graph exceeds the supported 4096 Worlds or 128 nesting levels");
            return false;
        }
        FString Target = TargetPackage;
        if (!Package.Equals(SourcePackage, ESearchCase::IgnoreCase))
        {
            FString Digest;
            if (!P4HashUtf8(Package, Digest))
            {
                ErrorCode = EP4WorldStreamingGraphError::DestinationIdentityFailed;
                Error = TEXT("Cannot compute streaming destination identity");
                return false;
            }
            Target += TEXT("_Stream_") + Digest.Left(12);
        }
        if (!P4GraphPackage(Target) || Destinations.Contains(FName(*Target)))
        {
            ErrorCode = EP4WorldStreamingGraphError::DestinationCollision;
            Error = TEXT("Streaming destination is invalid or collides: ") + Target;
            return false;
        }
        TArray<FP4WorldStreamingEntry> Streams;
        TSet<FName> StreamNames;
        for (ULevelStreaming* Stream : Current->GetStreamingLevels())
        {
            if (!Stream)
            {
                ErrorCode = EP4WorldStreamingGraphError::NullEntry;
                Error = TEXT("Null streaming entry cannot be exported faithfully: ") + Package;
                return false;
            }
            if (Stream->GetTypedOuter<UWorld>() != Current || StreamNames.Contains(Stream->GetFName()))
            {
                ErrorCode = EP4WorldStreamingGraphError::InvalidEntryOwner;
                Error = TEXT("Streaming entry is duplicated or owned by another World: ") + Package;
                return false;
            }
            StreamNames.Add(Stream->GetFName());
            if (++StreamCount > 65536)
            {
                ErrorCode = EP4WorldStreamingGraphError::EntryLimit;
                Error = TEXT("Streaming graph exceeds 65536 entries");
                return false;
            }
            const FString Child = Stream->GetWorldAssetPackageName();
            const FSoftObjectPath ChildPath = Stream->GetWorldAsset().ToSoftObjectPath();
            if (!P4GraphPackage(Child) || !ChildPath.GetSubPathString().IsEmpty() ||
                ChildPath.GetAssetName() != FPackageName::GetLongPackageAssetName(Child))
            {
                ErrorCode = EP4WorldStreamingGraphError::InvalidReference;
                Error = TEXT("Missing or invalid project streaming reference: ") + Child;
                return false;
            }
            const FString Loaded = Stream->GetLoadedLevel() ? Stream->GetLoadedLevel()->GetOutermost()->GetName()
                                                            : FString();
            if ((!Stream->PackageNameToLoad.IsNone() && Stream->PackageNameToLoad != FName(*Child)) ||
                (!Loaded.IsEmpty() && !Loaded.Equals(Child, ESearchCase::IgnoreCase)))
            {
                ErrorCode = EP4WorldStreamingGraphError::PackageOverride;
                Error = TEXT("Streaming package override needs an exact export adapter: ") + Child;
                return false;
            }
            FP4WorldStreamingEntry Row;
            Row.StreamName = Stream->GetName();
            Row.Package = Child;
            Row.LoadedPackage = Loaded;
            Row.PackageToLoad = Stream->PackageNameToLoad.IsNone() ? FString() : Stream->PackageNameToLoad.ToString();
            Row.Loaded = Stream->ShouldBeLoaded();
            Row.Visible = Stream->GetShouldBeVisibleFlag();
            Row.EditorVisible = Stream->GetShouldBeVisibleInEditor();
            Streams.Add(MoveTemp(Row));
        }
        FP4WorldStreamingNode Node;
        Node.Target = Target;
        Node.Streams = MoveTemp(Streams);
        Result.Nodes.Add(Package, MoveTemp(Node));
        Destinations.Add(FName(*Target));
        Visited.Add(FName(*Package));
        Active.Add(FName(*Package));
        KeepAlive.Emplace(Current);
        FP4GraphFrame Frame;
        Frame.World = Current;
        Stack.Add(Frame);
        return true;
    };
    if (!Enter(World))
        return Fail(ErrorCode, Error);
    while (Stack.Num())
    {
        FP4GraphFrame& Frame = Stack.Last();
        if (Frame.Next == Frame.World->GetStreamingLevels().Num())
        {
            Active.Remove(Frame.World->GetOutermost()->GetFName());
            Stack.Pop();
            continue;
        }
        ULevelStreaming* Stream = Frame.World->GetStreamingLevels()[Frame.Next++];
        const FString Child = Stream->GetWorldAssetPackageName();
        const FName Identity(*Child);
        if (Active.Contains(Identity))
            return Fail(EP4WorldStreamingGraphError::Cycle, TEXT("Streaming levels form a cycle: ") + Child);
        if (Visited.Contains(Identity))
            continue;
        const FString ObjectPath = P4ObjectPathOf(Child);
        UWorld* ChildWorld = FindObject<UWorld>(nullptr, *ObjectPath);
        if (!ChildWorld && FPackageName::DoesPackageExist(Child))
            ChildWorld = LoadObject<UWorld>(nullptr, *ObjectPath);
        else if (!ChildWorld && P4QueryOriginalAsset(Frame.World).IsOriginal())
        {
            // A game level can name a level the game does not ship (a test or bake level). The game
            // loads nothing there, so the graph keeps the entry and has no layer for it.
            Result.UnshippedStreams.AddUnique(Child);
            Visited.Add(Identity);
            continue;
        }
        if (!IsValid(ChildWorld) || ChildWorld->GetOutermost()->GetName() != Child)
            return Fail(EP4WorldStreamingGraphError::MissingWorld, TEXT("Missing authored streaming World: ") + Child);
        if (!Enter(ChildWorld))
            return Fail(ErrorCode, Error);
    }
    for (const auto& Pair : Result.Nodes)
        if (Destinations.Contains(FName(*Pair.Key)))
            return Fail(EP4WorldStreamingGraphError::DestinationSourceCollision,
                TEXT("A streaming destination collides with an authored source: ") + Pair.Key);
    return Result;
}

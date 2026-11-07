#include "P4ExportWork.h"
#include "Misc/Paths.h"

namespace
{
FString VolumeRoot(const FString& Path)
{
    // Unknown and network roots choose a sibling of the output rather than
    // assuming that two network paths resolve to one filesystem volume.
    return Path.Len() >= 3 && FChar::IsAlpha(Path[0]) && Path[1] == TCHAR(':') && Path[2] == TCHAR('/')
        ? Path.Left(2) : FString();
}
}

FString P4ExportWorkingDirectory(
    const FString& OutputDirectory,
    const FString& RequestedWorkDirectory,
    const FString& ModId,
    const TArray<FString>& MountedDirectories)
{
    if (OutputDirectory.IsEmpty() || ModId.IsEmpty() ||
        FPaths::GetCleanFilename(ModId) != ModId || ModId == TEXT(".") || ModId == TEXT(".."))
    {
        return FString();
    }
    FString Parent = FPaths::ConvertRelativePathToFull(OutputDirectory);
    FPaths::NormalizeDirectoryName(Parent);
    TArray<FString> Mounted;
    for (const FString& Directory : MountedDirectories)
    {
        FString Full = FPaths::ConvertRelativePathToFull(Directory);
        FPaths::NormalizeDirectoryName(Full);
        Mounted.Add(MoveTemp(Full));
    }
    const auto IsMounted = [&](const FString& Candidate)
    {
        return Mounted.ContainsByPredicate([&](const FString& Directory)
        {
            return Candidate.Equals(Directory, ESearchCase::IgnoreCase) ||
                FPaths::IsUnderDirectory(Candidate, Directory);
        });
    };
    const FString OutputVolume = VolumeRoot(Parent);
    FString Requested = FPaths::ConvertRelativePathToFull(RequestedWorkDirectory /
        (ModId + TEXT(".export-work")));
    FPaths::NormalizeDirectoryName(Requested);
    const FString Target = Parent / ModId;
    if (!RequestedWorkDirectory.IsEmpty() && !OutputVolume.IsEmpty() &&
        VolumeRoot(Requested).Equals(OutputVolume, ESearchCase::IgnoreCase) && !IsMounted(Requested) &&
        !Requested.Equals(Target, ESearchCase::IgnoreCase) &&
        !FPaths::IsUnderDirectory(Requested, Target) && !FPaths::IsUnderDirectory(Target, Requested))
    {
        // Preserve reusable work and preview cleanup when its volume is already correct.
        // The caller still checks every component for links before touching either path.
        return Requested;
    }
    const FString OutputParent = FPaths::GetPath(Parent);
    const FString OutsideOutputParent = FPaths::GetPath(OutputParent);
    const bool AboveNetworkShare = Parent.StartsWith(TEXT("//")) &&
        OutsideOutputParent.Find(TEXT("/"), ESearchCase::CaseSensitive,
            ESearchDir::FromStart, 2) == INDEX_NONE;
    if (!OutsideOutputParent.IsEmpty() && !AboveNetworkShare)
    {
        Parent = OutsideOutputParent;
    }
    else if (!OutputVolume.IsEmpty())
    {
        Parent = OutputVolume + TEXT("/");
    }
    else
    {
        Parent = OutputParent;
    }
    while (IsMounted(Parent))
    {
        const FString Next = FPaths::GetPath(Parent);
        if (Next.IsEmpty() || Next == Parent)
        {
            return FString();
        }
        Parent = Next;
    }
    // Keep private work outside the output folder and its immediate parent. A
    // load folder can treat every child as a mod, even when the child is hidden.
    return Parent / TEXT(".kadan-export-work") / (ModId + TEXT(".export-work"));
}

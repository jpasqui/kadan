#include "P4KadanPathDefaults.h"
#include "P4GameNames.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "P4GameBuild.h"

namespace
{
bool ToolExists(const FString& File)
{
    return !File.IsEmpty() && IFileManager::Get().FileExists(*File) && IFileManager::Get().FileSize(*File) > 0;
}
FString Tool(const FString& Current, const FString& Name, const FString& Plugin, const FString& Project,
    const FString& SearchPath)
{
    if (ToolExists(Current))
        return Current;
    if (!Current.IsEmpty() && FPaths::IsRelative(Current) && ToolExists(Project / Current))
        return FPaths::ConvertRelativePathToFull(Project / Current);
    const FString File = Name + TEXT(".exe");
    TArray<FString> Candidates;
    if (!Plugin.IsEmpty())
        Candidates.Add(Plugin / TEXT("Tools") / File);
    Candidates.Add(Project / TEXT("Tools") / File);
    Candidates.Add(Project / TEXT("Tools") / Name / File);
    Candidates.Add(Project / File);
    TArray<FString> Folders;
    SearchPath.ParseIntoArray(Folders, TEXT(";"), true);
    for (FString Folder : Folders)
    {
        Folder = Folder.TrimStartAndEnd();
        if (Folder.StartsWith(TEXT("\"")) && Folder.EndsWith(TEXT("\"")))
            Folder = Folder.Mid(1, Folder.Len() - 2);
        // Empty/relative PATH entries depend on process cwd; never choose a tool from them.
        if (!Folder.IsEmpty() && !FPaths::IsRelative(Folder))
            Candidates.Add(Folder / File);
    }
    for (const FString& Candidate : Candidates)
        if (ToolExists(Candidate))
            return FPaths::ConvertRelativePathToFull(Candidate);
    return Current; // Keep an invalid explicit value visible so the creator can correct it.
}
} //namespace

FP4KadanPathDefaults P4KadanDiscoverPaths(const FP4KadanPathDefaults& Current, const FString& PluginRoot,
    const FString& ProjectRoot, const FString& SavedRoot, const FString& EnvironmentPath)
{
    FP4KadanPathDefaults Out = Current;
    Out.MissingTools.Reset();
    Out.Retoc = Tool(Current.Retoc, TEXT("retoc"), PluginRoot, ProjectRoot, EnvironmentPath);
    Out.Repak = Tool(Current.Repak, TEXT("repak"), PluginRoot, ProjectRoot, EnvironmentPath);
    if (!ToolExists(Out.Retoc))
        Out.MissingTools.Add(TEXT("retoc.exe"));
    if (!ToolExists(Out.Repak))
        Out.MissingTools.Add(TEXT("repak.exe"));
    if (Out.ExportFolder.IsEmpty())
        Out.ExportFolder = FPaths::ConvertRelativePathToFull(ProjectRoot / TEXT("Exports"));
    if (Out.GameFolder.IsEmpty())
    {
        const FString Converted = SavedRoot / TEXT("Kadan/ConvertedOriginals");
        Out.GameFolder = IFileManager::Get().DirectoryExists(*(Converted / TEXT(P4_GAME_CONTENT_FOLDER)))
            ? FPaths::ConvertRelativePathToFull(Converted)
            : FPaths::ConvertRelativePathToFull(ProjectRoot / TEXT("GamePackages"));
    }
    if (Out.GamePaks.IsEmpty())
        for (const FString& Candidate :
            {ProjectRoot / TEXT("GamePaks"), ProjectRoot / TEXT("Game/Paks"), ProjectRoot / TEXT("Content/Paks")})
            if (P4HasKnownGameContainer(Candidate))
            {
                Out.GamePaks = FPaths::ConvertRelativePathToFull(Candidate);
                break;
            }
    return Out;
}

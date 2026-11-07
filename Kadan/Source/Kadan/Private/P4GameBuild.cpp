#include "P4GameBuild.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "P4NativeHash.h"

namespace
{
std::vector<std::string> ContainerNames(const FString& Paks)
{
    std::vector<std::string> Names;
    for (const TCHAR* Pattern : {TEXT("*.pak"), TEXT("*.utoc"), TEXT("*.ucas")})
    {
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(Paks / Pattern), true, false);
        for (const FString& Name : Found)
            Names.push_back(TCHAR_TO_UTF8(*Name));
    }
    return Names;
}

void Fill(FP4GameBuildInfo& Out, const P4MapKit::FGameFamily* Family, const P4MapKit::FGameBuild* Build,
    const FString& Container)
{
    Out.Family = Family;
    Out.Build = Build;
    Out.Container = Container;
    Out.Engine = UTF8_TO_TCHAR(Family->Engine);
    Out.ShaderArchive = Build ? FString(UTF8_TO_TCHAR(Build->ShaderArchive))
                              : FString(TEXT("ShaderArchive-Carrot4-")) + UTF8_TO_TCHAR(Family->ShaderFormat) +
            TEXT(".ushaderbytecode");
}

// The global container's identity, kept per folder while its two files keep their sizes and times.
struct FGlobalKey
{
    int64 TocSize = -1, CasSize = -1;
    FDateTime TocTime, CasTime;
    FString TocSha, CasSha;
};
FCriticalSection GlobalLock;
TMap<FString, FGlobalKey> GlobalCache;

bool GlobalIdentity(const FString& Paks, FGlobalKey& Out)
{
    const FString Toc = Paks / TEXT("global.utoc"), Cas = Paks / TEXT("global.ucas");
    IFileManager& Files = IFileManager::Get();
    const int64 TocSize = Files.FileSize(*Toc), CasSize = Files.FileSize(*Cas);
    if (TocSize <= 0 || CasSize <= 0)
        return false;
    const FDateTime TocTime = Files.GetTimeStamp(*Toc), CasTime = Files.GetTimeStamp(*Cas);
    const FString Key = FPaths::ConvertRelativePathToFull(Paks);
    {
        FScopeLock Lock(&GlobalLock);
        if (const FGlobalKey* Known = GlobalCache.Find(Key))
            if (Known->TocSize == TocSize && Known->CasSize == CasSize && Known->TocTime == TocTime &&
                Known->CasTime == CasTime)
            {
                Out = *Known;
                return true;
            }
    }
    FGlobalKey Fresh;
    Fresh.TocTime = TocTime;
    Fresh.CasTime = CasTime;
    if (!P4HashFile(Toc, 64 * 1024 * 1024, Fresh.TocSize, Fresh.TocSha) ||
        !P4HashFile(Cas, 64 * 1024 * 1024, Fresh.CasSize, Fresh.CasSha))
        return false;
    FScopeLock Lock(&GlobalLock);
    GlobalCache.Add(Key, Fresh);
    Out = Fresh;
    return true;
}
} // namespace

bool P4ResolveGameBuild(const FString& Paks, FP4GameBuildInfo& Out, FString& Error)
{
    Out = FP4GameBuildInfo();
    const std::vector<std::string> Names = Paks.IsEmpty() ? std::vector<std::string>() : ContainerNames(Paks);
    const std::vector<const P4MapKit::FGameBuild*> Candidates = P4MapKit::BuildsWithContainer(Names);
    if (Candidates.empty())
    {
        Fill(Out, P4MapKit::FindGameFamily("switch"), nullptr, TEXT("Carrot4-Switch"));
        return true;
    }
    const P4MapKit::FGameFamily* Family = P4MapKit::FamilyOfContainers(Names);
    if (!Family)
    {
        Error = TEXT("The game Paks folder holds the containers of two different game builds. Keep one game's files in it.");
        return false;
    }
    FGlobalKey Global;
    if (GlobalIdentity(Paks, Global))
    {
        std::string Unknown;
        if (const P4MapKit::FGameBuild* Build = P4MapKit::IdentifyGameBuild(Names, uint64(Global.TocSize),
                TCHAR_TO_UTF8(*Global.TocSha), uint64(Global.CasSize), TCHAR_TO_UTF8(*Global.CasSha), Unknown))
        {
            Fill(Out, Family, Build, UTF8_TO_TCHAR(Build->Container));
            return true;
        }
    }
    // A known container with an unknown global container: a build of a known family Kadan has not been told about.
    Fill(Out, Family, nullptr, UTF8_TO_TCHAR(Candidates.front()->Container));
    return true;
}

FString P4GameContainerIn(const FString& Paks)
{
    FP4GameBuildInfo Info;
    FString Error;
    P4ResolveGameBuild(Paks, Info, Error);
    return Info.Container.IsEmpty() ? FString(TEXT("Carrot4-Switch")) : Info.Container;
}

FString P4GameEngineFor(const FString& Paks)
{
    FP4GameBuildInfo Info;
    FString Error;
    P4ResolveGameBuild(Paks, Info, Error);
    return Info.Engine.IsEmpty() ? FString(TEXT("UE4_26")) : Info.Engine;
}

bool P4IsKnownGlobalContainer(int64 TocSize, const FString& TocSha256, int64 CasSize, const FString& CasSha256)
{
    for (const P4MapKit::FGameBuild& Build : P4MapKit::GameBuilds())
        if (uint64(TocSize) == Build.GlobalTocSize && uint64(CasSize) == Build.GlobalCasSize &&
            P4ExactNativeString(TocSha256, UTF8_TO_TCHAR(Build.GlobalTocSha256)) &&
            P4ExactNativeString(CasSha256, UTF8_TO_TCHAR(Build.GlobalCasSha256)))
            return true;
    return false;
}

FString P4GameShaderArchiveFor(const FString& Paks)
{
    FP4GameBuildInfo Info;
    FString Error;
    P4ResolveGameBuild(Paks, Info, Error);
    return Info.ShaderArchive;
}

bool P4HasKnownGameContainer(const FString& Folder)
{
    IFileManager& Files = IFileManager::Get();
    for (const P4MapKit::FGameBuild& Build : P4MapKit::GameBuilds())
    {
        const FString Stem = Folder / UTF8_TO_TCHAR(Build.Container);
        if (Files.FileSize(*(Stem + TEXT(".pak"))) > 0 && Files.FileSize(*(Stem + TEXT(".utoc"))) > 0 &&
            Files.FileSize(*(Stem + TEXT(".ucas"))) > 0)
            return true;
    }
    return false;
}

FString P4GameBuildUnsupported(const FString& Paks)
{
    FP4GameBuildInfo Info;
    FString Error;
    if (!P4ResolveGameBuild(Paks, Info, Error))
        return Error;
    if (Info.Family && FString(UTF8_TO_TCHAR(Info.Family->Id)) == TEXT("switch2"))
        return TEXT("These are the game files of Pikmin 4 for Nintendo Switch 2 (the Nintendo Switch 2 Edition or its "
                    "demo). This version of Kadan imports from and exports for Pikmin 4 on Nintendo Switch (1.1.1 and "
                    "2.0.0). Choose the Paks folder of the Nintendo Switch version.");
    return FString();
}

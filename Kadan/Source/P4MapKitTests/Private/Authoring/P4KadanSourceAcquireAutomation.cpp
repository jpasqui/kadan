#include "P4KadanSourceAcquire.h"
#include "P4KadanImport.h"
#include "P4ExportProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
struct FSourceFileState
{
    uint32 Attributes = 0;
    uint64 Created = 0, Written = 0, Size = 0;
    bool operator==(const FSourceFileState& Other) const
    {
        return Attributes == Other.Attributes && Created == Other.Created && Written == Other.Written &&
            Size == Other.Size;
    }
};
bool ObserveSourceFile(const FString& File, FSourceFileState& Out)
{
#if PLATFORM_WINDOWS
    const HANDLE Handle = CreateFileW(*File, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION Info = {};
    const bool Good = GetFileInformationByHandle(Handle, &Info) != 0;
    CloseHandle(Handle);
    if (!Good)
        return false;
    const auto Time = [](const FILETIME& Value)
    { return (uint64(Value.dwHighDateTime) << 32) | uint64(Value.dwLowDateTime); };
    Out.Attributes = uint32(Info.dwFileAttributes);
    Out.Created = Time(Info.ftCreationTime);
    Out.Written = Time(Info.ftLastWriteTime);
    Out.Size = (uint64(Info.nFileSizeHigh) << 32) | uint64(Info.nFileSizeLow);
    return true;
#else
    return false;
#endif
}
bool MakeObservedSourceLink(const FString& Source, const FString& Destination, const FString& OwnedRoot)
{
    const FString Full = FPaths::ConvertRelativePathToFull(Destination);
    if (!FPaths::IsUnderDirectory(Full, OwnedRoot) || FPaths::IsSamePath(Full, Source))
        return false;
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Full), true))
        return false;
    if (IFileManager::Get().FileExists(*Full) && !IFileManager::Get().Delete(*Full, false, false))
        return false;
#if PLATFORM_WINDOWS
    return CreateHardLinkW(*Full, *Source, nullptr) != 0;
#else
    return false;
#endif
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanSourceAcquireTest, "P4MapKit.Kadan.SourceAcquire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanSourceAcquireTest::RunTest(const FString&)
{
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    FP4KadanImportSettings Settings;
    Settings.GameFolder = NativeRoot / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Retoc.IsEmpty())
        Settings.Retoc = FPaths::GetPath(FPaths::GetPath(NativeRoot)) / TEXT("private/retoc/retoc.exe");
    if (Settings.Repak.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(NativeRoot)) / TEXT("private/repak/repak.exe");
    const FString Donor = TEXT(
        "/Game/Carrot4/Demo/Stage/Area500/Area500_OP1_WarpTube/Materials/MI_demo_Area500_OP1_SecondHalf_C001_StarTube01");
    const FString Parent = TEXT("/Game/Carrot4/MasterMaterial/demo/M_demo_Translucent_Unlit_00_Master");
    FP4KadanSourceInputs Out;
    FString Error;
    if (!TestFalse(TEXT("Acquisition rejects traversal before invoking tools"),
            P4KadanAcquireSourceClosure(Settings, {TEXT("/Game/../Other")}, Out, Error)))
        return false;
    if (!TestTrue(TEXT("A failed acquisition exposes no usable root"), Out.Root.IsEmpty()))
        return false;
    FP4KadanImportSettings MissingTools;
    if (!TestFalse(TEXT("Acquisition requires original containers and a native converter"),
            P4KadanAcquireSourceClosure(MissingTools, {Donor}, Out, Error)))
        return false;
    if (!TestTrue(TEXT("Original containers are configured"), IFileManager::Get().DirectoryExists(*Settings.GamePaks)))
        return false;
    FP4KadanImportSettings PakSettings = Settings;
    PakSettings.GameFolder.Reset();
    if (!TestTrue(
            TEXT("Acquires the real donor, hard dependencies and pak shader archive without an extracted game folder"),
            P4KadanAcquireSourceClosure(PakSettings, {Donor}, Out, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Actual parent is part of the source closure"), Out.Packages.Contains(Parent));
    TestTrue(TEXT("Actual donor is part of the source closure"), Out.Packages.Contains(Donor));
    TestTrue(TEXT("A game shader archive is available"), IFileManager::Get().FileSize(*Out.ShaderArchive) > 0);
    const FString Header = Out.Root / TEXT("Carrot4/Content/") + Donor.Mid(6) + TEXT(".uasset");
    const FDateTime HeaderTime = IFileManager::Get().GetTimeStamp(*Header);
    const FString Archive = Out.ShaderArchive, Root = Out.Root;
    TestTrue(TEXT("The real pak archive is published at the owned canonical shader cache path"),
        FPaths::IsSamePath(Archive, Root / TEXT("Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode")));
    const FDateTime ArchiveTime = IFileManager::Get().GetTimeStamp(*Archive);
    TestFalse(TEXT("Binary tool streaming never truncates an existing archive"),
        P4RunExportToolToFile(Settings.Repak,
            {TEXT("get"), Settings.GamePaks / TEXT("Carrot4-Switch.pak"),
                TEXT("Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode")},
            120, Archive, int64(1024) * 1024 * 1024, Error));
    TestTrue(TEXT("Refused overwrite preserves the validated archive"),
        IFileManager::Get().GetTimeStamp(*Archive) == ArchiveTime);
    const FString LongDirectory = Root / (TEXT("ExclusiveOutput_") + FString::ChrN(160, TEXT('x'))) /
        (TEXT("Segment_") + FString::ChrN(100, TEXT('y')));
    const FString LongFile = LongDirectory / TEXT("tool-help.txt");
    if (!TestTrue(TEXT("The long-path stream probe stays under the owned source cache"),
            FPaths::IsUnderDirectory(LongFile, Root)) ||
        !TestTrue(TEXT("Creates the small long-path stream fixture"),
            IFileManager::Get().MakeDirectory(*LongDirectory, true)))
        return false;
    if (IFileManager::Get().FileExists(*LongFile) &&
        !TestTrue(
            TEXT("Resets only the prior small stream fixture"), IFileManager::Get().Delete(*LongFile, false, false)))
        return false;
    if (!TestTrue(TEXT("Exclusive streaming supports a path longer than MAX_PATH"),
            LongFile.Len() > 260 &&
                P4RunExportToolToFile(Settings.Repak, {TEXT("--help")}, 30, LongFile, 64 * 1024, Error)))
    {
        AddError(Error);
        return false;
    }
    const int64 HelpSize = IFileManager::Get().FileSize(*LongFile);
    const FDateTime HelpTime = IFileManager::Get().GetTimeStamp(*LongFile);
    TestTrue(TEXT("The small native tool stream is complete"), HelpSize > 0 && HelpSize < 64 * 1024);
    TestFalse(TEXT("An existing long-path output is refused before tool execution"),
        P4RunExportToolToFile(Settings.Repak, {TEXT("--help")}, 30, LongFile, 64 * 1024, Error));
    TestTrue(TEXT("Refusal preserves long-path output size and write time"),
        IFileManager::Get().FileSize(*LongFile) == HelpSize && IFileManager::Get().GetTimeStamp(*LongFile) == HelpTime);
    TestTrue(
        TEXT("Removes only the small private stream fixture"), IFileManager::Get().Delete(*LongFile, false, false));
    // A real Switch source acquisition must obtain the archive from its exact
    // pak entry when no extracted copy is configured. A warm cache must work
    // without invoking repak again, and an explicit bad selection must not be
    // silently replaced by that otherwise valid cache.
    FP4KadanImportSettings Warm = PakSettings;
    Warm.GameFolder.Reset();
    Warm.Repak = Root / TEXT("missing-repak.exe");
    FP4KadanSourceInputs WarmOut;
    if (!TestTrue(TEXT("The real game shader archive cache needs no second pak extraction"),
            P4KadanAcquireSourceClosure(Warm, {Donor}, WarmOut, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Warm acquisition keeps the validated archive identity"), WarmOut.ShaderArchive, Archive);
    TestTrue(TEXT("Warm acquisition does not rewrite the game shader archive"),
        IFileManager::Get().GetTimeStamp(*Archive) == ArchiveTime);
    FP4KadanImportSettings Explicit = Settings;
    Explicit.ShaderArchive = Root / TEXT("missing-explicit.ushaderbytecode");
    FP4KadanSourceInputs Refused;
    TestFalse(TEXT("An explicit missing archive never falls back to the game pak or cache"),
        P4KadanAcquireSourceClosure(Explicit, {Donor}, Refused, Error));
    TestTrue(TEXT("An explicit archive failure exposes no usable root"), Refused.Root.IsEmpty());
    TestTrue(TEXT("An explicit archive failure preserves the complete cache"),
        IFileManager::Get().GetTimeStamp(*Archive) == ArchiveTime);
    // Use two actual donor inputs as external source links in the same owned
    // fixture root. This proves real header/body preservation even when the
    // configured Cave extraction happens not to contain this demo donor.
    const FString OwnedSourceRoot = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Root));
    const FString ObservedRoot = OwnedSourceRoot / TEXT("SourcePreservationInputs");
    const FString SourceHeader = ObservedRoot / TEXT("Carrot4/Content/") + Donor.Mid(6) + TEXT(".uasset");
    const FString SourceBody = FPaths::ChangeExtension(SourceHeader, TEXT("uexp"));
    if (!TestTrue(TEXT("Reuses actual donor header as an observed external source link"),
            MakeObservedSourceLink(Header, SourceHeader, OwnedSourceRoot)) ||
        !TestTrue(TEXT("Reuses actual donor body as an observed external source link"),
            MakeObservedSourceLink(FPaths::ChangeExtension(Header, TEXT("uexp")), SourceBody, OwnedSourceRoot)))
        return false;
    TMap<FString, FSourceFileState> SourceStates;
    const auto Observe = [&](const FString& File)
    {
        if (!IFileManager::Get().FileExists(*File))
            return true;
        FSourceFileState State;
        if (!TestTrue(TEXT("Observes source attributes and timestamps: ") + File, ObserveSourceFile(File, State)))
            return false;
        SourceStates.Add(File, State);
        return true;
    };
    for (const FString& Package : Out.Packages)
        for (const FString& Base : {Settings.GameFolder / TEXT("Carrot4/Content/") + Package.Mid(6),
                 Settings.GameFolder / Package.Mid(6), Settings.GameFolder / TEXT("Game") / Package.Mid(6)})
            for (const FString& Extension :
                {FString(TEXT("uasset")), FString(TEXT("uexp")), FString(TEXT("ubulk")), FString(TEXT("uptnl"))})
                if (!Observe(Base + TEXT(".") + Extension))
                    return false;
    if (!Observe(SourceHeader) || !Observe(SourceBody) || !Observe(Archive))
        return false;
    if (!TestTrue(TEXT("Actual source header, body and archive observations are present"),
            SourceStates.Contains(SourceHeader) && SourceStates.Contains(SourceBody) && SourceStates.Contains(Archive)))
        return false;
    TMap<FString, FSourceFileState> CacheStates;
    for (const FString& Package : Out.Packages)
        for (const FString& Extension :
            {FString(TEXT("uasset")), FString(TEXT("uexp")), FString(TEXT("ubulk")), FString(TEXT("uptnl"))})
        {
            const FString File = Root / TEXT("Carrot4/Content/") + Package.Mid(6) + TEXT(".") + Extension;
            if (!IFileManager::Get().FileExists(*File))
                continue;
            FSourceFileState State;
            if (!TestTrue(TEXT("Observes the complete cache file: ") + File, ObserveSourceFile(File, State)))
                return false;
            CacheStates.Add(File, State);
        }
    AddInfo(FString::Printf(
        TEXT(
            "Observed %d source inputs and %d complete cache files; comparisons cover attributes, creation/write times and sizes."),
        SourceStates.Num(), CacheStates.Num()));
    const auto SourcesUnchanged = [&](const FString& Stage)
    {
        bool Good = true;
        for (const auto& Pair : SourceStates)
        {
            FSourceFileState After;
            Good = TestTrue(Stage + TEXT(" preserves source flags/timestamps/size: ") + Pair.Key,
                       ObserveSourceFile(Pair.Key, After) && After == Pair.Value) &&
                Good;
        }
        return Good;
    };
    FP4KadanImportSettings RepeatSettings = Settings;
    RepeatSettings.GameFolder = ObservedRoot;
    RepeatSettings.ShaderArchive = Archive;
    FP4KadanSourceInputs Again;
    if (!TestTrue(TEXT("Repeated acquisition reuses the same source cache"),
            P4KadanAcquireSourceClosure(RepeatSettings, {Donor}, Again, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Cache root remains stable"), Again.Root, Root);
    TestEqual(TEXT("Existing game shader archive remains the same file"), Again.ShaderArchive, Archive);
    TestTrue(TEXT("Converted donor modification time remains unchanged"),
        IFileManager::Get().GetTimeStamp(*Header) == HeaderTime);
    TestTrue(TEXT("Game shader archive modification time remains unchanged"),
        IFileManager::Get().GetTimeStamp(*Archive) == ArchiveTime);
    TestEqual(TEXT("Repeated closure stays the same"), Again.Packages.Num(), Out.Packages.Num());
    if (!SourcesUnchanged(TEXT("Repeat")))
        return false;
    for (const auto& Pair : CacheStates)
    {
        FSourceFileState After;
        if (!TestTrue(TEXT("Repeat preserves every complete cache file metadata: ") + Pair.Key,
                ObserveSourceFile(Pair.Key, After) && After == Pair.Value))
            return false;
    }
    // Simulate a timed-out conversion with both files present. Unlink first:
    // never truncate a cache body that may share an external source inode.
    const FString Body = FPaths::ConvertRelativePathToFull(FPaths::ChangeExtension(Header, TEXT("uexp")));
    const FString OwnedRoot = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Kadan/SourceGame/Packages"));
    if (!TestTrue(TEXT("Repair targets the exact owned source cache"),
            FPaths::IsSamePath(Out.Root, OwnedRoot) && FPaths::IsUnderDirectory(Body, OwnedRoot)))
        return false;
    if (!TestTrue(TEXT("Unlinks only the owned cache body"), IFileManager::Get().Delete(*Body, false, false)))
        return false;
    TArray<uint8> Truncated;
    Truncated.Add(0);
    if (!TestTrue(TEXT("Stages an interrupted owned conversion"), FFileHelper::SaveArrayToFile(Truncated, *Body)))
        return false;
    FP4KadanSourceInputs Repaired;
    if (!TestTrue(TEXT("Repairs a truncated body even when both package files exist"),
            P4KadanAcquireSourceClosure(RepeatSettings, {Donor}, Repaired, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("A complete actual body is available again"), IFileManager::Get().FileSize(*Body) > 1);
    TestEqual(TEXT("Repair preserves the reused archive"), Repaired.ShaderArchive, Archive);
    return SourcesUnchanged(TEXT("Repair"));
}

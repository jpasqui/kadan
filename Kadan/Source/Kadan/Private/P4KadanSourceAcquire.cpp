#include "P4KadanSourceAcquire.h"
#include "P4GameBuild.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4KadanImport.h"
#include "P4ExportProcess.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/blueprint_import_identities.h"
#include "P4MapKitCore/source_package_paths.h"
THIRD_PARTY_INCLUDES_END
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
using FBytes = P4MapKit::Blueprint::FImportIdentityBytes;
bool Bytes(const FString& File, FBytes& Out)
{
    const int64 Size = IFileManager::Get().FileSize(*File);
    TArray<uint8> Data;
    if (Size < 0 || Size > 64 * 1024 * 1024 || !FFileHelper::LoadFileToArray(Data, *File, FILEREAD_Silent))
        return false;
    Out.assign(Data.GetData(), Data.GetData() + Data.Num());
    return true;
}
bool PackageReady(const FString& File, const FString& Package)
{
    FBytes Header;
    const int64 BodySize = IFileManager::Get().FileSize(*FPaths::ChangeExtension(File, TEXT("uexp")));
    if (BodySize < 0 || !Bytes(File, Header))
        return false;
    const auto Inventory = P4MapKit::ReadPackageHeader(Header, std::size_t(BodySize));
    if (!Inventory.Error.Code.empty() ||
        !FString(UTF8_TO_TCHAR(Inventory.Package.c_str())).Equals(Package, ESearchCase::IgnoreCase))
        return false;
    bool Owner = false;
    std::size_t End = 0;
    for (const auto& Export : Inventory.Exports)
    {
        End = FMath::Max(End, Export.Offset + Export.Size);
        std::string Class;
        if (Export.Outer == 0 && Inventory.Reference(Export.Class, Class) &&
            (Class == "/Script/Engine.Material" || Class == "/Script/Engine.MaterialInstanceConstant"))
            Owner = true;
    }
    if (Owner)
    {
        // Owning material verification accepts either no footer or the
        // complete UE magic. Detect an interrupted footer before reuse.
        if (std::size_t(BodySize) == End)
            return true;
        if (std::size_t(BodySize) - End != 4)
            return false;
        TUniquePtr<FArchive> Reader(
            IFileManager::Get().CreateFileReader(*FPaths::ChangeExtension(File, TEXT("uexp")), FILEREAD_Silent));
        if (!Reader)
            return false;
        Reader->Seek(int64(End));
        uint32 Magic = 0;
        *Reader << Magic;
        return !Reader->IsError() && Magic == 0x9e2a83c1;
    }
    return true;
}
bool UnlinkOutputs(const FString& File, FString& Error)
{
    for (const FString& Extension :
        {FString(TEXT("uasset")), FString(TEXT("uexp")), FString(TEXT("ubulk")), FString(TEXT("uptnl"))})
    {
        const FString Partial = FPaths::ChangeExtension(File, Extension);
        // A hard link shares read-only attributes with the source. Do
        // not clear them merely to remove this cache directory entry.
        if (IFileManager::Get().FileExists(*Partial) && !IFileManager::Get().Delete(*Partial, false, false))
        {
            Error = TEXT("A partial source cache output is busy: ") + Partial;
            return false;
        }
    }
    return true;
}
bool GamePackage(const FString& Name)
{
    return P4IsValidContentPackageIgnoringCase(Name) &&
        P4MapKit::Blueprint::ImportIdentityDetail::PackagePath(TCHAR_TO_UTF8(*Name));
}
FString RelativeHeader(const FString& Package)
{
    return P4ContainerRelativePath(Package) + TEXT(".uasset");
}
bool Reuse(const FString& From, const FString& To, FString& Error)
{
    if (From == To || IFileManager::Get().FileExists(*To))
        return true;
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(To), true))
    {
        Error = TEXT("Cannot create the source cache folder.");
        return false;
    }
#if PLATFORM_WINDOWS
    // Source packages, including bulk data, stay on the same SSD. Do not
    // silently copy a large game payload when a link cannot be made.
    if (CreateHardLinkW(*To, *From, nullptr))
        return true;
#endif
    Error = TEXT("Cannot reuse this source file with a hard link: ") + From;
    return false;
}
bool Run(const FP4KadanImportSettings& Settings, const TArray<FString>& Arguments, FString& Error)
{
    TArray<uint8> Output;
    return P4RunExportTool(FPaths::ConvertRelativePathToFull(Settings.Retoc), Arguments, 120, Output, Error);
}
FString ExistingHeader(const FString& Folder, const FString& Package)
{
    if (Folder.IsEmpty())
        return FString();
    const FString Relative = RelativeHeader(Package);
    for (const FString& File : {Folder / Relative, Folder / P4ContentRelativePath(Package) + TEXT(".uasset"),
             Folder / TEXT("Game") / P4ContentRelativePath(Package) + TEXT(".uasset")})
        if (IFileManager::Get().FileExists(*File))
            return FPaths::ConvertRelativePathToFull(File);
    return FString();
}
bool ArchiveReady(const FString& File)
{
    // Inspect the archive index without loading or rewriting its shader
    // payload. A truncated conversion must not poison later cache reuse.
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File, FILEREAD_Silent));
    if (!Reader || Reader->TotalSize() < 28 || Reader->TotalSize() > (int64(1) << 31))
        return false;
    const auto Word = [&]()
    {
        uint32 Value = 0;
        *Reader << Value;
        return Value;
    };
    if (Word() != 2)
        return false;
    int64 Starts[6] = {};
    uint32 Counts[6] = {};
    int32 Array = 0;
    for (const int32 Stride : {20, 20, 16, 17, 16, 4})
    {
        const uint32 Count = Word();
        if (Reader->IsError() || Count > 2000000 || int64(Count) * Stride > Reader->TotalSize() - Reader->Tell())
            return false;
        Starts[Array] = Reader->Tell();
        Counts[Array++] = Count;
        Reader->Seek(Reader->Tell() + int64(Count) * Stride);
    }
    if (!Counts[0] || !Counts[1] || Counts[0] != Counts[2] || Counts[1] != Counts[3])
        return false;
    const int64 PayloadBytes = Reader->TotalSize() - Reader->Tell();
    for (uint32 Index = 0; Index < Counts[3]; ++Index)
    {
        Reader->Seek(Starts[3] + int64(Index) * 17);
        uint64 Offset = 0;
        *Reader << Offset;
        const uint32 Size = Word(), Full = Word();
        if (Reader->IsError() || !Size || !Full || Size > (1u << 20) || Full > (1u << 20) ||
            Offset > uint64(PayloadBytes) || Size > uint64(PayloadBytes) - Offset)
            return false;
    }
    return !Reader->IsError();
}
bool ShaderArchive(const FP4KadanImportSettings& Settings, const FString& Root, FString& Archive, FString& Error)
{
    if (!Settings.ShaderArchive.IsEmpty())
    {
        Archive = FPaths::ConvertRelativePathToFull(Settings.ShaderArchive);
        if (ArchiveReady(Archive))
            return true;
        const FString Owned = Root / TEXT(P4_GAME_CONTENT_FOLDER) / P4GameShaderArchiveFor(Settings.GamePaks);
        if (!FPaths::IsSamePath(Archive, Owned))
        {
            Error = TEXT("The configured game shader archive cannot be read.");
            return false;
        }
        if (IFileManager::Get().FileExists(*Owned) && !IFileManager::Get().Delete(*Owned, false, true))
        {
            Error = TEXT("An incomplete shader cache output is busy.");
            return false;
        }
    }
    TArray<FString> Files;
    for (const FString& Folder : {Settings.GameFolder, Root})
    {
        if (Folder.IsEmpty())
            continue;
        Files.Empty();
        IFileManager::Get().FindFilesRecursive(Files, *Folder, *P4GameShaderArchiveFor(Settings.GamePaks), true, false);
        Files.Sort();
        if (Files.Num() > 1)
        {
            Error = TEXT("Choose one game shader archive; this source folder holds several.");
            return false;
        }
        if (Files.Num() == 1)
        {
            if (ArchiveReady(Files[0]))
            {
                Archive = FPaths::ConvertRelativePathToFull(Files[0]);
                return true;
            }
            if (!FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Folder), Root))
            {
                Error = TEXT("The existing game shader archive is incomplete or invalid.");
                return false;
            }
            if (!IFileManager::Get().Delete(*Files[0], false, true))
            {
                Error = TEXT("An incomplete shader cache output is busy.");
                return false;
            }
        }
    }
    const FString Library = P4GameShaderArchiveFor(Settings.GamePaks);
    Archive = Root / TEXT(P4_GAME_CONTENT_FOLDER) / Library;
    const FString Container = P4GameContainerIn(Settings.GamePaks);
    const FString Pak = FPaths::ConvertRelativePathToFull(Settings.GamePaks / (Container + TEXT(".pak")));
    if (Settings.Repak.IsEmpty())
    {
        Error = TEXT(
            "Kadan needs repak.exe to read the game's shader library, and no repak is set. Kadan looks for it in its Tools folder.");
        return false;
    }
    if (!IFileManager::Get().FileExists(*Settings.Repak))
    {
        Error = TEXT("Kadan needs repak.exe to read the game's shader library, and it is not at ") + Settings.Repak +
            TEXT(".");
        return false;
    }
    if (!IFileManager::Get().FileExists(*Pak))
    {
        Error = TEXT("The game's shader library is in ") + Container + TEXT(".pak, and the game Paks folder ") +
            Settings.GamePaks + TEXT(" does not have it.");
        return false;
    }
    // This library is a raw companion-pak file, not a Zen shader library.
    // Stream only that exact file; never buffer or extract the entire pak.
    const FString Partial = Archive + TEXT(".partial");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Archive), true);
    if (IFileManager::Get().FileExists(*Partial) && !IFileManager::Get().Delete(*Partial, false, false))
    {
        Error = TEXT("The partial shader archive is busy.");
        return false;
    }
    TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*Partial));
    if (!Writer)
    {
        Error = TEXT("The shader archive cache cannot be written.");
        return false;
    }
    const TFunction<bool(const uint8*, int32, FString&)> Sink = [&](const uint8* Data, int32 Count, FString& Why)
    {
        Writer->Serialize(const_cast<uint8*>(Data), Count);
        if (Writer->IsError())
        {
            Why = TEXT("The shader archive cache write failed.");
            return false;
        }
        return true;
    };
    const bool Extracted = P4RunExportToolStream(FPaths::ConvertRelativePathToFull(Settings.Repak),
        {TEXT("get"), Pak, TEXT(P4_GAME_CONTENT_FOLDER "/") + Library}, 120, Sink, (int64(1) << 31) - 1,
        Error);
    const bool Written = Writer->Close() && !Writer->IsError();
    Writer.Reset();
    if (!Extracted || !Written || !ArchiveReady(Partial))
    {
        IFileManager::Get().Delete(*Partial, false, false);
        if (Error.IsEmpty())
            Error = TEXT("The game's raw Switch shader archive is incomplete or unreadable.");
        return false;
    }
    if (!IFileManager::Get().Move(*Archive, *Partial, false, false, false, true))
    {
        IFileManager::Get().Delete(*Partial, false, false);
        Error = TEXT("The validated shader archive could not be published.");
        return false;
    }
    return true;
}
} //namespace

bool P4KadanAcquireShaderArchive(const FP4KadanImportSettings& Configured, FString& Archive, FString& Error)
{
    Archive.Empty();
    Error.Empty();
    if (!IsInGameThread())
    {
        Error = TEXT("Shader source acquisition requires the editor's main thread.");
        return false;
    }
    // The existing converted cache is keyed to the selected containers and retoc.
    // Do not borrow a library from some other project's material experiment.
    if (!IFileManager::Get().DirectoryExists(*Configured.GamePaks) ||
        !IFileManager::Get().FileExists(*Configured.Retoc))
    {
        Error = TEXT("Configure the original game Paks folder and retoc before acquiring the material shader archive.");
        return false;
    }
    const FString Root = P4KadanConversionCache(Configured);
    if (Root.IsEmpty())
    {
        Error = TEXT("The selected game shader cache could not be validated.");
        return false;
    }
    return ShaderArchive(Configured, Root, Archive, Error);
}

bool P4KadanAcquireSourceClosure(
    const FP4KadanImportSettings& Configured, const TArray<FString>& Roots, FP4KadanSourceInputs& Out, FString& Error)
{
    Out = FP4KadanSourceInputs();
    Error.Empty();
    if (!IsInGameThread())
    {
        Error = TEXT("Source acquisition must run on the editor's main thread.");
        return false;
    }
    if (Roots.Num() == 0 || Roots.Num() > 8)
    {
        Error = TEXT("Choose between one and eight material source packages.");
        return false;
    }
    for (const FString& Package : Roots)
        if (!GamePackage(Package))
        {
            Error = TEXT("Invalid game source package: ") + Package;
            return false;
        }
    if (!IFileManager::Get().DirectoryExists(*Configured.GamePaks) ||
        !IFileManager::Get().FileExists(*Configured.Retoc))
    {
        Error = TEXT("Configure the original game Paks folder and retoc before acquiring material sources.");
        return false;
    }
    const FString Cache = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/SourceGame"));
    const FString Root = Cache / TEXT("Packages");
    if (!IFileManager::Get().MakeDirectory(*Root, true))
    {
        Error = TEXT("Cannot create the game source cache.");
        return false;
    }
    TArray<FString> Pending = Roots;
    TSet<FString> Seen;
    std::map<std::uint64_t, std::string> SourcePaths;
    const auto SourcePath = [&](const FString& Package, FString& Relative)
    {
        std::string Why, Actual;
        if (SourcePaths.empty())
        {
            TArray<uint8> Listing;
            if (!P4RunExportTool(FPaths::ConvertRelativePathToFull(Configured.Retoc),
                    {TEXT("list"), FPaths::ConvertRelativePathToFull(Configured.GamePaks), TEXT("--package"),
                        TEXT("--path")},
                    120, Listing, Error))
                return false;
            const std::string Text(reinterpret_cast<const char*>(Listing.GetData()), Listing.Num());
            if (!P4MapKit::ReadOriginalSourcePaths(Text, SourcePaths, Why))
            {
                Error = UTF8_TO_TCHAR(Why.c_str());
                return false;
            }
        }
        if (!P4MapKit::OriginalSourcePath(
                SourcePaths, P4ExportPackageId(Package), TCHAR_TO_UTF8(*RelativeHeader(Package)), Actual, Why))
        {
            Error = Package + TEXT(": ") + UTF8_TO_TCHAR(Why.c_str());
            return false;
        }
        Relative = UTF8_TO_TCHAR(Actual.c_str());
        return true;
    };
    while (Pending.Num())
    {
        const FString Package = Pending.Pop(false);
        if (Seen.Contains(Package))
            continue;
        if (!GamePackage(Package))
        {
            Error = TEXT("Invalid referenced game package: ") + Package;
            return false;
        }
        if (Seen.Num() >= 256)
        {
            Error = TEXT("The material source closure exceeds 256 packages.");
            return false;
        }
        Seen.Add(Package);
        const FString File = Root / RelativeHeader(Package);
        FString Existing = ExistingHeader(Configured.GameFolder, Package);
        if (!Existing.IsEmpty() && !PackageReady(Existing, Package))
            Existing.Empty();
        const bool CacheReady = PackageReady(File, Package);
        // Remove only the owned links/files. A truncated body may exist, so
        // presence alone is not proof that the previous conversion completed.
        if (!CacheReady && !UnlinkOutputs(File, Error))
            return false;
        if (!Existing.IsEmpty() || !CacheReady)
        {
            if (!Existing.IsEmpty())
            {
                for (const FString& Extension :
                    {FString(TEXT("uasset")), FString(TEXT("uexp")), FString(TEXT("ubulk")), FString(TEXT("uptnl"))})
                {
                    const FString From = FPaths::ChangeExtension(Existing, Extension);
                    if (IFileManager::Get().FileExists(*From) &&
                        !Reuse(From, FPaths::ChangeExtension(File, Extension), Error))
                        return false;
                }
            }
            else
            {
                FString Relative;
                if (!SourcePath(Package, Relative))
                    return false;
                // A partial prior cache may contain hard links. Unlink these
                // exact owned outputs before conversion; never truncate a link.
                if (!UnlinkOutputs(File, Error))
                    return false;
                if (!Run(Configured,
                        {TEXT("to-legacy"), FPaths::ConvertRelativePathToFull(Configured.GamePaks), Root,
                            TEXT("--version"), P4GameEngineFor(Configured.GamePaks), TEXT("--no-shaders"), TEXT("--no-script-objects"),
                            TEXT("-f"), Relative},
                        Error))
                    return false;
            }
        }
        FBytes Header;
        const FString BodyFile = FPaths::ChangeExtension(File, TEXT("uexp"));
        const int64 BodySize = IFileManager::Get().FileSize(*BodyFile);
        if (!Bytes(File, Header) || BodySize < 0)
        {
            Error = TEXT("The game source package is unavailable: ") + Package;
            return false;
        }
        const auto Inventory = P4MapKit::ReadPackageHeader(Header, std::size_t(BodySize));
        if (!Inventory.Error.Code.empty() ||
            !FString(UTF8_TO_TCHAR(Inventory.Package.c_str())).Equals(Package, ESearchCase::IgnoreCase))
        {
            Error = TEXT("The acquired package does not match its requested source identity: ") + Package;
            return false;
        }
        bool MaterialOwner = false;
        for (const auto& Export : Inventory.Exports)
        {
            std::string Class;
            if (Export.Outer == 0 && Inventory.Reference(Export.Class, Class) &&
                (Class == "/Script/Engine.Material" || Class == "/Script/Engine.MaterialInstanceConstant"))
                MaterialOwner = true;
        }
        if (Roots.Contains(Package) && !MaterialOwner)
        {
            Error = TEXT("The requested owning source is not a game Material or MaterialInstanceConstant: ") + Package;
            return false;
        }
        for (const auto& Import : Inventory.Imports)
            if (Import.Outer == 0 && Import.Class == "Package" && Import.Name.compare(0, 6, "/Game/") == 0)
                Pending.AddUnique(UTF8_TO_TCHAR(Import.Name.c_str()));
        Out.Packages.Add(Package);
    }
    if (!ShaderArchive(Configured, Root, Out.ShaderArchive, Error))
        return false;
    Out.Root = Root;
    Out.Packages.Sort();
    return true;
}

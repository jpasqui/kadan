#include "P4KadanContainerIndex.h"
#include "P4GameNames.h"
#include "P4MapKitCore/package_reader.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

bool FP4KadanContainerIndex::Read(const FString& Listing, FString& Error)
{
    Paths.Reset();
    Ambiguous.Reset();
    Error.Reset();
    Ready = false;
    if (Listing.Len() > 64 * 1024 * 1024)
    {
        Error = TEXT("The game's container index exceeds the bounded import size.");
        return false;
    }
    TArray<FString> Lines;
    Listing.ParseIntoArrayLines(Lines, false);
    if (Lines.Num() > 262144)
    {
        Error = TEXT("The game's container index has too many rows.");
        return false;
    }
    for (const FString& Line : Lines)
    {
        if (Line.Len() > 8192)
        {
            Error = TEXT("A container index row exceeds the path limit.");
            return false;
        }
        TArray<FString> Fields;
        Line.ParseIntoArrayWS(Fields);
        if (Fields.Num() < 3 || Fields[2] != TEXT("ExportBundleData"))
            continue;
        if (Fields.Num() != 4)
        {
            Error = TEXT("A package container index row is malformed.");
            return false;
        }
        FString Path = Fields[3];
        if (!Path.RemoveFromStart(TEXT("../../../")))
        {
            Error = TEXT("A package container path has an unsupported mount.");
            return false;
        }
        TArray<FString> Parts;
        Path.ParseIntoArray(Parts, TEXT("/"), false);
        if (Parts.Contains(TEXT("..")) || Parts.Contains(TEXT(".")) || Parts.Contains(TEXT("")) ||
            Path.Contains(TEXT("\\")))
        {
            Error = TEXT("A package container path contains traversal or an empty segment.");
            return false;
        }
        const FString Prefix = TEXT(P4_GAME_CONTENT_FOLDER "/");
        if (!Path.StartsWith(Prefix))
            continue; // Original /Engine is outside this /Game hydration route.
        const FString Extension = FPaths::GetExtension(Path);
        if (Extension != TEXT("uasset") && Extension != TEXT("umap"))
        {
            Error = TEXT("A package container row does not name a package header.");
            return false;
        }
        const FString Package = TEXT("/Game/") +
            Path.Mid(Prefix.Len(), Path.Len() - Prefix.Len() - Extension.Len() - 1);
        if (!FPackageName::IsValidLongPackageName(Package))
        {
            Error = TEXT("A container row names an invalid game package.");
            return false;
        }
        const FName Identity(*Package);
        if (const FString* Prior = Paths.Find(Identity))
        {
            if (!Prior->Equals(Path, ESearchCase::CaseSensitive))
                Ambiguous.Add(Identity);
        }
        else
            Paths.Add(Identity, Path);
    }
    if (!Paths.Num())
    {
        Error = TEXT("The game's container index contains no game package paths.");
        return false;
    }
    Ready = true;
    return true;
}

bool FP4KadanContainerIndex::Resolve(const FString& Package, FString& Path, FString& Error) const
{
    Path.Reset();
    Error.Reset();
    const FName Identity(*Package);
    if (!Ready)
    {
        Error = TEXT("The game's container index has not been read successfully.");
        return false;
    }
    if (Ambiguous.Contains(Identity))
    {
        Error = TEXT("The game's containers contain ambiguous paths for this package identity.");
        return false;
    }
    const FString* Found = Paths.Find(Identity);
    if (!Found)
    {
        Error = TEXT("This package is absent from the game's container index.");
        return false;
    }
    Path = *Found;
    return true;
}

bool FP4KadanContainerIndex::Enumerate(TArray<FP4KadanContainerPackage>& Out, FString& Error) const
{
    Out.Reset();
    Error.Reset();
    if (!Ready)
    {
        Error = TEXT("The game's container index has not been read successfully.");
        return false;
    }
    if (Ambiguous.Num())
    {
        Error = TEXT("The game's containers contain ambiguous package paths; choose one complete game version.");
        return false;
    }
    const FString Prefix = TEXT(P4_GAME_CONTENT_FOLDER "/");
    for (const auto& Pair : Paths)
    {
        FP4KadanContainerPackage Row;
        Row.Path = Pair.Value;
        const FString Extension = FPaths::GetExtension(Row.Path);
        Row.Package = TEXT("/Game/") + Row.Path.Mid(Prefix.Len(), Row.Path.Len() - Prefix.Len() - Extension.Len() - 1);
        Row.IsMap = Extension == TEXT("umap");
        Out.Add(MoveTemp(Row));
    }
    Out.Sort(
        [](const FP4KadanContainerPackage& A, const FP4KadanContainerPackage& B) { return A.Package < B.Package; });
    return true;
}

bool P4KadanExtractedPackageReady(const FString& Header, const FString& Package, FString& Error)
{
    Error.Reset();
    const int64 HeaderSize = IFileManager::Get().FileSize(*Header);
    const FString Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
    const int64 BodySize = IFileManager::Get().FileSize(*Body);
    TArray<uint8> Bytes;
    if (HeaderSize <= 0 || HeaderSize > 64 * 1024 * 1024 || BodySize <= 0 ||
        !FFileHelper::LoadFileToArray(Bytes, *Header, FILEREAD_Silent))
    {
        Error = TEXT("The container conversion did not produce a complete header and body.");
        return false;
    }
    const auto Read = P4MapKit::ReadPackageHeader(
        std::vector<uint8_t>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()), std::size_t(BodySize));
    if (!Read.Error.Code.empty() || FName(UTF8_TO_TCHAR(Read.Package.c_str())) != FName(*Package))
    {
        Error = TEXT("The converted package header is invalid or has a different package identity.");
        return false;
    }
    return true;
}

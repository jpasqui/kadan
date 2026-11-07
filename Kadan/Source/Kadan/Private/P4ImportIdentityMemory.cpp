#include "P4ImportIdentityMemory.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"

namespace
{
struct FRemembered
{
    FSHAHash Header, Body;
    std::map<std::int32_t, std::string> Imports;
};
FCriticalSection Guard;
TMap<FString, FRemembered> Remembered;

FSHAHash Digest(const std::vector<std::uint8_t>& Bytes)
{
    FSHAHash Out;
    FSHA1::HashBuffer(Bytes.data(), Bytes.size(), Out.Hash);
    return Out;
}
// The identities depend only on the game's bytes, so they are kept for this computer under those bytes' digests.
// A Blueprint the import library hands a later project was never planned there, and finds them here.
FString KeptFile(const FString& Package, const FSHAHash& Header, const FSHAHash& Body)
{
    FSHAHash Key;
    const FString Text = TEXT("kadan-import-identities/1\n") + Package.ToLower() + TEXT("\n") + Header.ToString() +
        TEXT("\n") + Body.ToString();
    const FTCHARToUTF8 Utf8(*Text);
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Key.Hash);
    return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("Kadan/ImportIdentities"), Key.ToString() + TEXT(".txt"));
}
void Keep(const FString& File, const std::map<std::int32_t, std::string>& Imports)
{
    if (IFileManager::Get().FileExists(*File))
        return;
    FString Text;
    for (const auto& Import : Imports)
        Text += FString::Printf(TEXT("%d\t%s\n"), Import.first, UTF8_TO_TCHAR(Import.second.c_str()));
    const FString Part = File + TEXT(".part");
    if (FFileHelper::SaveStringToFile(Text, *Part, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        !IFileManager::Get().Move(*File, *Part, true, true))
        IFileManager::Get().Delete(*Part, false, true, true);
}
std::map<std::int32_t, std::string> Kept(const FString& File)
{
    std::map<std::int32_t, std::string> Imports;
    TArray<FString> Lines;
    if (IFileManager::Get().FileSize(*File) > 4 * 1024 * 1024 || !FFileHelper::LoadFileToStringArray(Lines, *File))
        return Imports;
    for (const FString& Line : Lines)
    {
        FString Index, Path;
        int32 Number = 0;
        if (!Line.Split(TEXT("\t"), &Index, &Path) || !LexTryParseString(Number, *Index) || Path.IsEmpty())
            return {};
        Imports.emplace(Number, TCHAR_TO_UTF8(*Path));
    }
    return Imports;
}
} // namespace

void P4RememberImportIdentities(const FString& Package, const std::vector<std::uint8_t>& Header,
    const std::vector<std::uint8_t>& Body, const std::map<std::int32_t, std::string>& Imports)
{
    if (Imports.empty())
        return;
    FRemembered Entry{Digest(Header), Digest(Body), Imports};
    Keep(KeptFile(Package, Entry.Header, Entry.Body), Imports);
    FScopeLock Lock(&Guard);
    Remembered.Add(Package.ToLower(), MoveTemp(Entry));
}

std::map<std::int32_t, std::string> P4RememberedImportIdentities(
    const FString& Package, const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body)
{
    const FSHAHash HeaderDigest = Digest(Header), BodyDigest = Digest(Body);
    {
        FScopeLock Lock(&Guard);
        const FRemembered* Found = Remembered.Find(Package.ToLower());
        if (Found && Found->Header == HeaderDigest && Found->Body == BodyDigest)
            return Found->Imports;
    }
    return Kept(KeptFile(Package, HeaderDigest, BodyDigest));
}

#pragma once
// Exact fixture rename only. The caller still owns namespace/transaction admission.
#include <string>
#include <cstdint>
#if (defined(P4_EXACT_MOVE_HOST_TEST) && defined(_WIN32)) || (defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS)
#if defined(P4_EXACT_MOVE_HOST_TEST)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include "Windows/WindowsHWrapper.h"
#endif
namespace P4ExactFixtureMove
{
static constexpr std::uint64_t MaxBytes = 64ull * 1024 * 1024;
struct FSnapshot
{
    BY_HANDLE_FILE_INFORMATION Info{};
    std::string Sha256;
    bool Valid = false;
};
struct FResult
{
    bool Moved = false, AttrsRestored = false, Exact = false;
    DWORD WinError = 0;
    std::string Error;
    BY_HANDLE_FILE_INFORMATION Before{}, After{};
};
struct FHandle
{
    HANDLE Value = INVALID_HANDLE_VALUE;
    explicit FHandle(HANDLE In) : Value(In) {}
    ~FHandle()
    {
        if (Value != INVALID_HANDLE_VALUE)
            CloseHandle(Value);
    }
    FHandle(const FHandle&) = delete;
    FHandle& operator=(const FHandle&) = delete;
};
inline bool Same(const BY_HANDLE_FILE_INFORMATION& A, const BY_HANDLE_FILE_INFORMATION& B, bool Attributes = true)
{
    return A.dwVolumeSerialNumber == B.dwVolumeSerialNumber && A.nFileIndexHigh == B.nFileIndexHigh &&
        A.nFileIndexLow == B.nFileIndexLow && A.nFileSizeHigh == B.nFileSizeHigh && A.nFileSizeLow == B.nFileSizeLow &&
        A.nNumberOfLinks == B.nNumberOfLinks && CompareFileTime(&A.ftCreationTime, &B.ftCreationTime) == 0 &&
        CompareFileTime(&A.ftLastWriteTime, &B.ftLastWriteTime) == 0 &&
        (!Attributes || A.dwFileAttributes == B.dwFileAttributes);
}
inline bool Regular(const BY_HANDLE_FILE_INFORMATION& I)
{
    return !(I.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) && I.nNumberOfLinks > 0 &&
        ((std::uint64_t(I.nFileSizeHigh) << 32) | I.nFileSizeLow) <= MaxBytes;
}
inline bool Digest(const std::string& S)
{
    if (S.size() != 64)
        return false;
    for (char C : S)
        if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f')))
            return false;
    return true;
}
// Absolute drive paths only; reject alternate streams, dot aliases and every reparse ancestor.
inline bool SafePath(const std::wstring& Input, bool MissingLeaf = false)
{
    if (Input.size() < 4 || Input.size() > 32760 || Input.find(L'\0') != std::wstring::npos)
        return false;
    std::wstring Path = Input;
    for (auto& C : Path)
        if (C == L'/')
            C = L'\\';
    if (!((Path[0] >= L'A' && Path[0] <= L'Z') || (Path[0] >= L'a' && Path[0] <= L'z')) || Path[1] != L':' ||
        Path[2] != L'\\' || Path.find(L':', 2) != std::wstring::npos)
        return false;
    wchar_t Full[32768];
    const DWORD N = GetFullPathNameW(Path.c_str(), 32768, Full, nullptr);
    if (!N || N >= 32768 || CompareStringOrdinal(Path.c_str(), -1, Full, -1, true) != CSTR_EQUAL)
        return false;
    for (std::size_t End = 3;;)
    {
        End = Path.find(L'\\', End);
        const bool Leaf = End == std::wstring::npos;
        const auto Part = Leaf ? Path : Path.substr(0, End);
        const DWORD A = GetFileAttributesW(Part.c_str());
        if (A == INVALID_FILE_ATTRIBUTES)
            return Leaf && MissingLeaf && GetLastError() == ERROR_FILE_NOT_FOUND;
        if (A & FILE_ATTRIBUTE_REPARSE_POINT)
            return false;
        if (Leaf)
            return true;
        if (!(A & FILE_ATTRIBUTE_DIRECTORY))
            return false;
        ++End;
    }
}
inline HANDLE Open(const std::wstring& Path, bool WriteAttributes)
{
    // Attribute-only handles do not establish a data-sharing lease on Windows.
    return CreateFileW(Path.c_str(),
        FILE_READ_DATA | FILE_READ_ATTRIBUTES | (WriteAttributes ? FILE_WRITE_ATTRIBUTES : 0),
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}
// Hash(Path, MaxBytes, OutLowercaseSha256) must bound reads and report failure.
// No hash implementation or additional SDK dependency is introduced here.
template <class THash>
bool Capture(const std::wstring& Path, THash Hash, FSnapshot& Out)
{
    Out = FSnapshot{};
    if (!SafePath(Path))
        return false;
    FHandle H(Open(Path, false));
    BY_HANDLE_FILE_INFORMATION After{};
    if (H.Value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(H.Value, &Out.Info) || !Regular(Out.Info) ||
        !Hash(Path, MaxBytes, Out.Sha256) || !Digest(Out.Sha256) || !GetFileInformationByHandle(H.Value, &After) ||
        !Same(Out.Info, After))
        return false;
    FHandle Named(Open(Path, false));
    BY_HANDLE_FILE_INFORMATION AtName{};
    Out.Valid = Named.Value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(Named.Value, &AtName) &&
        Same(Out.Info, AtName);
    return Out.Valid;
}
#if defined(P4_EXACT_MOVE_HOST_TEST)
enum class EHostDelta
{
    None,
    AddedArchive,
    AddedHidden
};
#endif
template <class THash>
FResult Move(const std::wstring& From, const std::wstring& To, const FSnapshot& Expected, THash Hash
#if defined(P4_EXACT_MOVE_HOST_TEST)
    ,
    EHostDelta Delta = EHostDelta::None
#endif
)
{
    FResult R;
    auto Fail = [&](const char* Why, DWORD Code = 0)
    {
        R.Error = Why;
        R.WinError = Code;
        return R;
    };
    if (!Expected.Valid || !Regular(Expected.Info) || !Digest(Expected.Sha256) || !SafePath(From) ||
        !SafePath(To, true))
        return Fail("admission");
    if (GetFileAttributesW(To.c_str()) != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND)
        return Fail("destination-not-absent");
    FHandle H(Open(From, true));
    if (H.Value == INVALID_HANDLE_VALUE)
        return Fail("source-open", GetLastError());
    if (!GetFileInformationByHandle(H.Value, &R.Before) || !Same(Expected.Info, R.Before))
        return Fail("source-identity");
    std::string BeforeHash;
    if (!Hash(From, MaxBytes, BeforeHash) || BeforeHash != Expected.Sha256)
        return Fail("source-hash");
    BY_HANDLE_FILE_INFORMATION Stable{};
    if (!GetFileInformationByHandle(H.Value, &Stable) || !Same(R.Before, Stable))
        return Fail("source-changed");
    FHandle Named(Open(From, false));
    BY_HANDLE_FILE_INFORMATION AtName{};
    if (Named.Value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(Named.Value, &AtName) ||
        !Same(R.Before, AtName))
        return Fail("source-name-changed");
    if (!MoveFileExW(From.c_str(), To.c_str(), 0))
        return Fail("rename", GetLastError());
    R.Moved = true;
#if defined(P4_EXACT_MOVE_HOST_TEST)
    if (Delta != EHostDelta::None)
    {
        FILE_BASIC_INFO Inject{};
        Inject.FileAttributes = R.Before.dwFileAttributes |
            (Delta == EHostDelta::AddedArchive ? FILE_ATTRIBUTE_ARCHIVE : FILE_ATTRIBUTE_HIDDEN);
        if (!SetFileInformationByHandle(H.Value, FileBasicInfo, &Inject, sizeof(Inject)))
            return Fail("host-injection", GetLastError());
    }
#endif
    if (!GetFileInformationByHandle(H.Value, &R.After) || !Same(R.Before, R.After, false))
        return Fail("moved-identity");
    if (R.After.dwFileAttributes != R.Before.dwFileAttributes &&
        R.After.dwFileAttributes != (R.Before.dwFileAttributes | FILE_ATTRIBUTE_ARCHIVE))
        return Fail("unsupported-attributes");
    if (R.After.dwFileAttributes != R.Before.dwFileAttributes)
    {
        FILE_BASIC_INFO Restore{};
        Restore.FileAttributes = R.Before.dwFileAttributes;
        if (!SetFileInformationByHandle(H.Value, FileBasicInfo, &Restore, sizeof(Restore)))
            return Fail("restore-attributes", GetLastError());
    }
    if (!GetFileInformationByHandle(H.Value, &R.After) || !Same(R.Before, R.After))
        return Fail("restored-identity");
    R.AttrsRestored = true;
    const DWORD Old = GetFileAttributesW(From.c_str());
    const DWORD OldError = GetLastError();
    if (Old != INVALID_FILE_ATTRIBUTES || OldError != ERROR_FILE_NOT_FOUND || !SafePath(To))
        return Fail("namespace-after");
    FHandle Destination(Open(To, false));
    BY_HANDLE_FILE_INFORMATION AtDestination{};
    std::string AfterHash;
    if (Destination.Value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(Destination.Value, &AtDestination) ||
        !Same(R.Before, AtDestination) || !Hash(To, MaxBytes, AfterHash) || AfterHash != Expected.Sha256 ||
        !GetFileInformationByHandle(H.Value, &R.After) || !Same(R.Before, R.After))
        return Fail("final-exact");
    R.Exact = true;
    return R;
}
} //namespace P4ExactFixtureMove
#endif

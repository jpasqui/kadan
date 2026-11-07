#pragma once
// Native deny-write/delete handles, shared by the reader and direct host controls.
#include <string>
#if (defined(P4_REGISTRY_LEASE_HOST_TEST) && defined(_WIN32)) || (defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS)
#if defined(P4_REGISTRY_LEASE_HOST_TEST)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include "Windows/WindowsHWrapper.h"
#endif

namespace P4RegistryLease
{
class FInput
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION Identity{};
    std::wstring Path;
    bool Directory = false;
    static bool Same(const BY_HANDLE_FILE_INFORMATION& A, const BY_HANDLE_FILE_INFORMATION& B, bool IsDirectory)
    {
        if (A.dwVolumeSerialNumber != B.dwVolumeSerialNumber || A.nFileIndexHigh != B.nFileIndexHigh ||
            A.nFileIndexLow != B.nFileIndexLow)
            return false;
        // Directory membership is checked separately against the relevant exact file set.
        // Scratch children may legitimately change directory timestamps/size/other attributes.
        if (IsDirectory)
            return bool(A.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                bool(B.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        return A.nFileSizeHigh == B.nFileSizeHigh && A.nFileSizeLow == B.nFileSizeLow &&
            A.dwFileAttributes == B.dwFileAttributes &&
            A.ftLastWriteTime.dwHighDateTime == B.ftLastWriteTime.dwHighDateTime &&
            A.ftLastWriteTime.dwLowDateTime == B.ftLastWriteTime.dwLowDateTime;
    }
    static HANDLE OpenHandle(const std::wstring& Name, bool IsDirectory)
    {
        return CreateFileW(Name.c_str(), IsDirectory ? FILE_READ_ATTRIBUTES : GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, IsDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, nullptr);
    }

public:
    FInput() = default;
    FInput(const FInput&) = delete;
    FInput& operator=(const FInput&) = delete;
    ~FInput()
    {
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
    }
    bool Open(const std::wstring& Name, bool IsDirectory = false)
    {
        if (Handle != INVALID_HANDLE_VALUE || Name.empty() || Name.find(L'\0') != std::wstring::npos)
            return false;
        Handle = OpenHandle(Name, IsDirectory);
        if (Handle == INVALID_HANDLE_VALUE)
            return false;
        if (!GetFileInformationByHandle(Handle, &Identity) ||
            bool(Identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != IsDirectory)
        {
            CloseHandle(Handle);
            Handle = INVALID_HANDLE_VALUE;
            return false;
        }
        Path = Name;
        Directory = IsDirectory;
        return true;
    }
    bool Current() const
    {
        BY_HANDLE_FILE_INFORMATION Held{}, Now{};
        if (Handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(Handle, &Held) ||
            !Same(Identity, Held, Directory))
            return false;
        // Also re-resolve the selected name: a changed ancestor alias is not the same selected source.
        const HANDLE Named = OpenHandle(Path, Directory);
        if (Named == INVALID_HANDLE_VALUE)
            return false;
        const bool Okay = GetFileInformationByHandle(Named, &Now) && Same(Identity, Now, Directory);
        CloseHandle(Named);
        return Okay;
    }
    unsigned long long Size() const
    {
        return (static_cast<unsigned long long>(Identity.nFileSizeHigh) << 32) | Identity.nFileSizeLow;
    }
};
} //namespace P4RegistryLease
#else
namespace P4RegistryLease
{
// An unavailable immutable-lease API is a refusal, never timestamp authority.
class FInput
{
public:
    FInput() = default;
    FInput(const FInput&) = delete;
    FInput& operator=(const FInput&) = delete;
    bool Open(const std::wstring&, bool = false) { return false; }
    bool Current() const { return false; }
    unsigned long long Size() const { return 0; }
};
} //namespace P4RegistryLease
#endif

#include "P4NativeHash.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "bcrypt.lib")
#endif

namespace
{
#if PLATFORM_WINDOWS
// SHA-256 by the platform's own provider.
class FP4Sha256
{
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    BCRYPT_HASH_HANDLE Hash = nullptr;
    TArray<uint8> State;

public:
    FP4Sha256()
    {
        ULONG Size = 0, Written = 0;
        if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(
                Algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&Size), sizeof(Size), &Written, 0) < 0 ||
            Written != sizeof(Size) || !Size || Size > 1024 * 1024)
            return;
        State.SetNumUninitialized(Size);
        if (BCryptCreateHash(Algorithm, &Hash, State.GetData(), Size, nullptr, 0, 0) < 0)
            Hash = nullptr;
    }
    ~FP4Sha256()
    {
        if (Hash)
            BCryptDestroyHash(Hash);
        if (Algorithm)
            BCryptCloseAlgorithmProvider(Algorithm, 0);
    }
    FP4Sha256(const FP4Sha256&) = delete;
    FP4Sha256& operator=(const FP4Sha256&) = delete;
    bool Update(uint8* Bytes, int32 Size) { return Hash && BCryptHashData(Hash, Bytes, Size, 0) >= 0; }
    bool Finish(FString& Out)
    {
        uint8 Digest[32];
        if (!Hash || BCryptFinishHash(Hash, Digest, sizeof(Digest), 0) < 0)
            return false;
        Out = BytesToHex(Digest, sizeof(Digest)).ToLower();
        return true;
    }
};
#endif
} //namespace

bool P4HashSourceBuffers(const uint8* Header, int64 HeaderSize, const uint8* Body, int64 BodySize, FString& Digest)
{
    Digest.Empty();
#if PLATFORM_WINDOWS
    // Each package reader already caps its individual input at 64 MiB.
    constexpr int64 Limit = 64 * 1024 * 1024;
    if (HeaderSize < 0 || BodySize < 0 || HeaderSize > Limit || BodySize > Limit || (HeaderSize && !Header) ||
        (BodySize && !Body))
        return false;
    FP4Sha256 Hash;
    const uint8* Buffers[] = {Header, Body};
    const int64 Sizes[] = {HeaderSize, BodySize};
    for (int32 Index = 0; Index < 2; ++Index)
        for (int64 At = 0; At < Sizes[Index];)
        {
            const int32 Count = int32(FMath::Min<int64>(64 * 1024, Sizes[Index] - At));
            if (!Hash.Update(const_cast<uint8*>(Buffers[Index] + At), Count))
                return false;
            At += Count;
        }
    return Hash.Finish(Digest);
#else
    return false;
#endif
}

bool P4HashByteStream(const TFunction<bool(const TFunction<bool(const uint8*, int32)>&)>& Stream, FString& Digest)
{
    Digest.Empty();
#if PLATFORM_WINDOWS
    if (!Stream)
        return false;
    FP4Sha256 Hash;
    bool Failed = false;
    const TFunction<bool(const uint8*, int32)> Update = [&](const uint8* Bytes, int32 Size)
    {
        if (Failed || Size < 0 || Size > 64 * 1024 || (Size && !Bytes))
        {
            Failed = true;
            return false;
        }
        if (!Hash.Update(const_cast<uint8*>(Bytes), Size))
        {
            Failed = true;
            return false;
        }
        return true;
    };
    if (!Stream(Update) || Failed)
        return false;
    return Hash.Finish(Digest);
#else
    return false;
#endif
}

bool P4HashUtf8(const FString& Text, FString& Digest)
{
    Digest.Empty();
#if PLATFORM_WINDOWS
    if (int64(Text.Len()) * sizeof(TCHAR) > 64 * 1024 * 1024)
        return false;
    for (int32 Index = 0; Index < Text.Len(); ++Index)
    {
        const uint32 Character = uint32(Text[Index]);
        if (!Character || (Character >= 0xdc00 && Character <= 0xdfff))
            return false;
        if (Character >= 0xd800 && Character <= 0xdbff)
        {
            if (++Index >= Text.Len() || uint32(Text[Index]) < 0xdc00 || uint32(Text[Index]) > 0xdfff)
                return false;
        }
    }
    FTCHARToUTF8 Bytes(*Text, Text.Len());
    if (Bytes.Length() > 64 * 1024 * 1024)
        return false;
    return P4HashSourceBuffers(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), nullptr, 0, Digest);
#else
    return false;
#endif
}

bool P4HashFile(const FString& Path, int64 Limit, int64& Size, FString& Digest)
{
    Size = 0;
    Digest.Empty();
    TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
    if (!File || Limit < 0 || File->Size() < 0 || File->Size() > Limit)
        return false;
    Size = File->Size();
    const int64 Whole = Size;
    return P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Update)
        {
            TArray<uint8> Bytes;
            Bytes.SetNumUninitialized(64 * 1024);
            for (int64 At = 0; At < Whole;)
            {
                const int32 Count = int32(FMath::Min<int64>(Bytes.Num(), Whole - At));
                if (!File->Read(Bytes.GetData(), Count) || !Update(Bytes.GetData(), Count))
                    return false;
                At += Count;
            }
            return File->Size() == Whole;
        },
        Digest);
}

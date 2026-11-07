#pragma once
#include "CoreMinimal.h"

// Text compared code unit by code unit, embedded NULs included. FString's ordinary equality ignores
// case and is unsuitable for digests and for values taken from a file.
inline bool P4ExactNativeString(const FString& A, const FString& B)
{
    return A.Len() == B.Len() && (A.IsEmpty() ||
        FMemory::Memcmp(A.GetCharArray().GetData(), B.GetCharArray().GetData(), SIZE_T(A.Len()) * sizeof(TCHAR)) == 0);
}

// SHA-256 of a text's UTF-8, as small hexadecimal. Text of more than 64 MiB, or that is not valid
// UTF-16, is refused.
KADAN_API bool P4HashUtf8(const FString& Text, FString& Digest);
// SHA-256 of exactly the two buffers a reader was given, one after the other: the bytes that are
// decoded are the bytes that are hashed, so nothing is read from a file a second time.
KADAN_API bool P4HashSourceBuffers(const uint8* Header, int64 HeaderSize, const uint8* Body, int64 BodySize, FString& Digest);
// SHA-256 of pieces the caller hands over, each of at most 64 KiB. A stream that fails gives no digest.
KADAN_API bool P4HashByteStream(const TFunction<bool(const TFunction<bool(const uint8*, int32)>&)>& Stream, FString& Digest);
// SHA-256 of a file of at most Limit bytes, read in pieces. Size is how many bytes it has.
KADAN_API bool P4HashFile(const FString& Path, int64 Limit, int64& Size, FString& Digest);

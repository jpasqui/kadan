#pragma once
#include "P4GameNativeTails.h"

namespace P4LegacyVectorField
{
// The old reference importer wrote precisely this empty inline bulk header.
// A size match alone never qualifies unknown/edited native data.
inline bool EmptyTail(const uint8* Data, int64 Size, uint64 ExpectedOffset, FP4VectorFieldTail& Out, FString& Error)
{
    if (Size != 24 || !P4ReadVectorFieldTail(Data, Size, Out, Error) || Out.Flags != 0x48 || Out.Elements ||
        Out.Payload.Num())
    {
        Error = TEXT("legacy-vector-field-current-tail-not-canonical-empty");
        return false;
    }
    uint64 Offset = 0;
    for (int32 Byte = 0; Byte < 8; ++Byte)
        Offset |= uint64(Data[16 + Byte]) << (8 * Byte);
    if (Offset != ExpectedOffset)
    {
        Error = TEXT("legacy-vector-field-current-inline-offset-differs");
        return false;
    }
    Error.Reset();
    return true;
}
} //namespace P4LegacyVectorField

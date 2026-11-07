#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/property_decoder.h"

// A package's two files as they were read, with what a decode of its values needs: the reader's
// answer and the struct kinds of the maps of its type. Plain bytes and names only.
struct FP4OriginalDecode
{
    std::vector<std::uint8_t> Header, Body;
    P4MapKit::FPackageInventory Inventory;
    std::vector<P4MapKit::FMapStructCodec> Codecs;
    P4MapKit::FReadError Error{0, "missing-oversized-or-unreadable-source"};
    FString Digest, SchemaPath;
    bool CodecsCopied = false;
};

// What kind of struct the keys and values of each map are, for a type and every struct inside it. A file
// does not say this of a map, so a reader takes it from the type. Editor thread only. False when two
// structs inside the type have one name.
KADAN_API bool P4MapStructCodecs(UStruct* Type, std::vector<P4MapKit::FMapStructCodec>& Codecs);

void P4ReadOriginalDecode(const FString& Header, const FString& Body, FP4OriginalDecode& State);
bool P4CopyOriginalCodecs(FP4OriginalDecode& State);
FString P4DecodeOriginalValues(FP4OriginalDecode& State);

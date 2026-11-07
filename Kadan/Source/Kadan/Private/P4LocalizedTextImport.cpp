#include "P4LocalizedTextImport.h"
#include "Internationalization/Text.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "P4MapKitCore/property_decoder.h"

bool P4MakePersistentLocalizedText(uint32 Flags, const FString& Namespace, const FString& Key, const FString& Source,
                                   FText& Out)
{
    if (Flags != 0 && Flags != ETextFlag::Immutable)
        return false;
    // Use the engine's base-history archive representation. ChangeKey/FromString
    // construct mutable text; they cannot represent the source's Immutable bit.
    // Persistent native reading retains that bit and the localization identity.
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, true);
    const bool Literal = Namespace.IsEmpty() && Key.IsEmpty();
    if (Literal)
        Flags |= ETextFlag::CultureInvariant;
    int8 History = Literal ? -1 : 0;
    FString StoredNamespace = Namespace;
    FString StoredKey = Key;
    FString StoredSource = Source;
    Writer << Flags << History;
    if (Literal)
    {
        uint32 HasSource = Source.IsEmpty() ? 0 : 1;
        Writer << HasSource;
        if (HasSource)
            Writer << StoredSource;
    }
    else
        Writer << StoredNamespace << StoredKey << StoredSource;
    if (Writer.IsError())
        return false;
    FMemoryReader Reader(Bytes, true);
    FText Read;
    Reader << Read;
    if (Reader.IsError() || Reader.Tell() != Bytes.Num() || FTextInspector::GetFlags(Read) != Flags)
        return false;
    TArray<uint8> Readback;
    FMemoryWriter Check(Readback, true);
    FText Copy = Read;
    Check << Copy;
    if (Check.IsError())
        return false;
    // FTextKey and FString can encode an empty string with different archive
    // lengths. Compare the independently decoded flags/history/identity/source,
    // never raw padding or the translated display string.
    P4MapKit::FPackageInventory Package;
    const std::vector<uint8> Expected(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    const std::vector<uint8> Actual(Readback.GetData(), Readback.GetData() + Readback.Num());
    P4MapKit::FPropertyDecoder ExpectedReader(Package, Expected), ActualReader(Package, Actual);
    P4MapKit::FPropertyEnvelope Tag;
    Tag.Type = "TextProperty";
    Tag.Size = Expected.size();
    P4MapKit::FDecodedValue ExpectedValue, ActualValue;
    if (!ExpectedReader.Property(Tag, ExpectedValue))
        return false;
    Tag.Size = Actual.size();
    if (!ActualReader.Property(Tag, ActualValue) ||
        P4MapKit::DecodedJson(ExpectedValue) != P4MapKit::DecodedJson(ActualValue))
        return false;
    Out = Read;
    return true;
}

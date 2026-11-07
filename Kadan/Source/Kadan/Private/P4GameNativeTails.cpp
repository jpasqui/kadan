#include "P4GameNativeTails.h"
#include "P4GameNames.h"
#include "P4StockEngineFieldContent.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "P4MapKitCore/package_reader.h"
#include "P4TextureBuild.h"
#include "Misc/Base64.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Engine/FontFace.h"
#include "VectorField/VectorFieldStatic.h"
#include <map>

namespace
{
const TCHAR* const TailKey = TEXT("P4GameNativeTail");
const TCHAR* const FoliageFormat = TEXT("foliage-grid-v1");

uint32 U32(const uint8* At)
{
    return uint32(At[0]) | uint32(At[1]) << 8 | uint32(At[2]) << 16 | uint32(At[3]) << 24;
}
int64 I64(const uint8* At)
{
    return int64(uint64(U32(At)) | uint64(U32(At + 4)) << 32);
}
FString Sha(const uint8* Data, int64 Size)
{
    FString Out;
    return P4HashSourceBuffers(Data, int32(Size), nullptr, 0, Out) ? Out : FString(TEXT("unhashed"));
}
bool HasTailRecord(const UObject* Object)
{
    return IsValid(Object) && Object->GetOutermost() &&
        Object->GetOutermost()->GetMetaData()->HasValue(Object, TailKey);
}
// Whether the object's class or one of its parents writes data the editor keeps with the asset.
bool KeepsTail(const UObject* Object)
{
    for (const UClass* Class = Object ? Object->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
        if (const FP4NativeTailReader* Reader = P4FindNativeTailReader(Class->GetPathName()))
            if (Reader->Kept)
                return true;
    return false;
}

bool DescribeVectorField(const uint8* Data, int64 Size, FString& Line, FString& Error)
{
    FP4VectorFieldTail Read;
    if (!P4ReadVectorFieldTail(Data, Size, Read, Error))
        return false;
    Line = P4VectorFieldTailLine(Read);
    return true;
}

bool FillVectorField(UObject* Object, const uint8* Data, int64 Size, FString& Error)
{
    FP4VectorFieldTail Read;
    return P4ReadVectorFieldTail(Data, Size, Read, Error) && P4FillVectorFieldSource(Object, Read, Error);
}

bool DescribeFoliageGrid(const uint8* Data, int64 Size, FString& Line, FString& Error)
{
    FP4FoliageGridTail Read;
    if (!P4ReadFoliageGridTail(Data, Size, Read, Error))
        return false;
    Line = P4FoliageGridTailLine(Read);
    return true;
}

bool FillFoliageGrid(UObject* Object, const uint8* Data, int64 Size, FString& Error)
{
    FP4FoliageGridTail Read;
    return P4ReadFoliageGridTail(Data, Size, Read, Error) && P4StoreFoliageGridTail(Object, Read, Error);
}

// Wwise asset data: UObject's id flag (none), then its bulk data's flags, count and size, all 0, and an
// offset that only says where the package puts it. The game's banks and events keep their sound elsewhere.
bool DescribeEmptyAssetData(const uint8* Data, int64 Size, FString& Line, FString& Error)
{
    if (!Data || Size != 24 || U32(Data) || U32(Data + 4) || U32(Data + 8) || U32(Data + 12))
    {
        Error = TEXT("wwise-asset-data-tail-is-not-empty-bulk-data");
        return false;
    }
    Line = TEXT("empty bulk data");
    return true;
}

bool FillEmptyAssetData(UObject*, const uint8*, int64, FString&)
{
    return true;
}

// Wwise platform data: UObject's id flag (none), then the object that is its current asset data.
int32 PlatformDataObject(const uint8* Data, int64 Size)
{
    return Data && Size == 8 && !U32(Data) ? int32(U32(Data + 4)) : 0;
}

bool DescribePlatformData(const uint8* Data, int64 Size, FString& Line, FString& Error)
{
    if (PlatformDataObject(Data, Size) <= 0)
    {
        Error = TEXT("wwise-platform-data-tail-names-no-object-of-the-package");
        return false;
    }
    Line = TEXT("current asset data");
    return true;
}

// UFont::Serialize: UObject's id flag (none), then its character remap, empty in the game's fonts.
bool DescribeFont(const uint8* Data, int64 Size, FString& Line, FString& Error)
{
    if (!Data || Size != 8 || U32(Data) || U32(Data + 4))
    {
        Error = TEXT("font-tail-has-a-character-remap");
        return false;
    }
    Line = TEXT("no character remap");
    return true;
}

// UFontFace::Serialize when cooking: UObject's id flag (none), whether the font's file is inline and, when it is, the
// file with its size. A face that loads from a file of its own has only the flag.
bool ReadFontFace(const uint8* Data, int64 Size, const uint8*& File, int32& FileSize, FString& Error)
{
    File = nullptr;
    FileSize = 0;
    if (Data && Size == 8 && !U32(Data) && !U32(Data + 4))
        return true;
    if (!Data || Size < 12 || U32(Data) || U32(Data + 4) != 1 || int64(U32(Data + 8)) != Size - 12)
    {
        Error = TEXT("font-face-tail-not-read");
        return false;
    }
    File = Data + 12;
    FileSize = int32(Size - 12);
    return true;
}

bool DescribeFontFace(const uint8* Data, int64 Size, FString& Line, FString& Error)
{
    const uint8* File = nullptr;
    int32 FileSize = 0;
    if (!ReadFontFace(Data, Size, File, FileSize, Error))
        return false;
    Line = File ? FString::Printf(TEXT("inline font of %d bytes, %s"), FileSize, *Sha(File, FileSize))
                : FString(TEXT("no inline font"));
    return true;
}

bool FillFontFace(UObject* Object, const uint8* Data, int64 Size, FString& Error)
{
    const uint8* File = nullptr;
    int32 FileSize = 0;
    UFontFace* Face = Cast<UFontFace>(Object);
    if (!Face || !ReadFontFace(Data, Size, File, FileSize, Error))
        return false;
    if (File)
        Face->FontFaceData->SetData(TArray<uint8>(File, FileSize));
    return true;
}

// UVectorFieldStatic::Serialize, the game's UFoliageGridDataAsset::Serialize at 01d491d0, and the Serialize of
// the Wwise integration's classes (its source is not in the game, the bytes of all 3,517 audio packages are).
const FP4NativeTailReader Readers[] = {
    {TEXT("/Script/Engine.VectorFieldStatic"), false, &DescribeVectorField, &FillVectorField},
    {TEXT(P4_GAME_SCRIPT_PACKAGE ".FoliageGridDataAsset"), true, &DescribeFoliageGrid, &FillFoliageGrid},
    {TEXT("/Script/Engine.Font"), false, &DescribeFont, &FillEmptyAssetData},
    {TEXT("/Script/Engine.FontFace"), false, &DescribeFontFace, &FillFontFace},
    {TEXT("/Script/AkAudio.AkAssetDataWithMedia"), false, &DescribeEmptyAssetData, &FillEmptyAssetData},
    {TEXT("/Script/AkAudio.AkAudioEventData"), false, &DescribeEmptyAssetData, &FillEmptyAssetData},
    {TEXT("/Script/AkAudio.AkAssetPlatformData"), false, &DescribePlatformData, &FillEmptyAssetData,
        &PlatformDataObject, TEXT("CurrentAssetData")},
};
} //namespace

TArrayView<const FP4NativeTailReader> P4NativeTailReaders()
{
    return MakeArrayView(Readers);
}

const FP4NativeTailReader* P4FindNativeTailReader(const FString& Class)
{
    for (const FP4NativeTailReader& Reader : Readers)
        if (Class == Reader.Class)
            return &Reader;
    return nullptr;
}

bool P4ReadFoliageGridTail(const uint8* Data, int64 Size, FP4FoliageGridTail& Out, FString& Error)
{
    Out = FP4FoliageGridTail();
    // UObject's id flag (an object no lazy reference names has none), then the version.
    if (!Data || Size < 5 || U32(Data))
    {
        Error = TEXT("foliage-grid-tail-has-no-plain-object-id");
        return false;
    }
    Out.Version = Data[4];
    if (Out.Version == 0)
    {
        if (Size == 5)
            return true;
        Error = TEXT("foliage-grid-tail-version-0-has-more-data");
        return false;
    }
    // Version 1 cells hold two bytes the game skips; only the game's own version 2 is read.
    if (Out.Version != 2 || Size < 9)
    {
        Error = FString::Printf(TEXT("foliage-grid-tail-version-not-read: %d"), Out.Version);
        return false;
    }
    const int64 Count = int32(U32(Data + 5));
    if (Count < 0 || Size != 9 + Count * 4)
    {
        Error = TEXT("foliage-grid-tail-cell-count-differs");
        return false;
    }
    Out.Cells.Append(Data + 9, int32(Count * 4));
    Error.Reset();
    return true;
}

void P4WriteFoliageGridTail(const FP4FoliageGridTail& Tail, TArray<uint8>& Out)
{
    Out.Reset();
    Out.AddZeroed(4);
    Out.Add(Tail.Version);
    if (!Tail.Version)
        return;
    const uint32 Count = uint32(Tail.Cells.Num() / 4);
    for (int32 Shift = 0; Shift < 32; Shift += 8)
        Out.Add(uint8(Count >> Shift));
    Out.Append(Tail.Cells);
}

FString P4FoliageGridTailLine(const FP4FoliageGridTail& Tail)
{
    return FString::Printf(TEXT("foliage grid version %d, %d cells, cells sha256 %s"), Tail.Version,
        Tail.Cells.Num() / 4, *Sha(Tail.Cells.GetData(), Tail.Cells.Num()));
}

bool P4ReadVectorFieldTail(const uint8* Data, int64 Size, FP4VectorFieldTail& Out, FString& Error)
{
    Out = FP4VectorFieldTail();
    if (!Data || Size < 4 + 4 || U32(Data))
    {
        Error = TEXT("vector-field-tail-has-no-plain-object-id");
        return false;
    }
    int64 At = 4;
    Out.Flags = U32(Data + At);
    At += 4;
    // Stored after its header in the same file: not at the end of the file, not in another file, not
    // compressed, not left out.
    const uint32 Refused = 0x1 | 0x2 | 0x20 | 0x100 | 0x200 | 0x800 | 0x4000;
    if (Out.Flags & Refused)
    {
        Error = FString::Printf(TEXT("vector-field-bulk-data-not-inline: flags %08x"), Out.Flags);
        return false;
    }
    const bool Wide = (Out.Flags & 0x2000) != 0;
    if (Size < At + (Wide ? 16 : 8) + 8)
    {
        Error = TEXT("vector-field-bulk-header-truncated");
        return false;
    }
    int64 OnDisk = 0;
    if (Wide)
    {
        Out.Elements = I64(Data + At);
        OnDisk = I64(Data + At + 8);
        At += 16;
    }
    else
    {
        Out.Elements = int32(U32(Data + At));
        OnDisk = int32(U32(Data + At + 4));
        At += 8;
    }
    At += 8; // its offset in the file, which says only where the payload is
    if (Out.Elements < 0 || Out.Elements > MAX_int32 || OnDisk != Out.Elements || Size != At + OnDisk)
    {
        Error = TEXT("vector-field-bulk-payload-size-differs");
        return false;
    }
    Out.Payload.Append(Data + At, int32(OnDisk));
    Error.Reset();
    return true;
}

FString P4VectorFieldTailLine(const FP4VectorFieldTail& Tail)
{
    return FString::Printf(TEXT("vector field source bulk flags %08x, %lld bytes, sha256 %s"), Tail.Flags,
        Tail.Elements, *Sha(Tail.Payload.GetData(), Tail.Payload.Num()));
}

bool P4FillVectorFieldSource(UObject* Object, const FP4VectorFieldTail& Tail, FString& Error)
{
    UVectorFieldStatic* Field = Cast<UVectorFieldStatic>(Object);
    if (!Field || Tail.Payload.Num() != Tail.Elements)
    {
        Error = TEXT("vector-field-source-target-invalid");
        return false;
    }
    // The source is half-float RGBA per cell (UVectorFieldStatic::InitResource reads it as FFloat16Color).
    if (int64(Field->SizeX) * Field->SizeY * Field->SizeZ * 8 != Tail.Elements)
    {
        Error = TEXT("vector-field-source-size-differs-from-its-dimensions");
        return false;
    }
    void* Memory = Field->SourceData.Lock(LOCK_READ_WRITE);
    Memory = Field->SourceData.Realloc(Tail.Elements);
    FMemory::Memcpy(Memory, Tail.Payload.GetData(), Tail.Payload.Num());
    Field->SourceData.Unlock();
    Error.Reset();
    return true;
}

bool P4StoreFoliageGridTail(UObject* Object, const FP4FoliageGridTail& Tail, FString& Error)
{
    if (!IsInGameThread() || !KeepsTail(Object))
    {
        Error = TEXT("foliage-grid-tail-target-invalid");
        return false;
    }
    if ((Tail.Version != 0 && Tail.Version != 2) || Tail.Cells.Num() % 4 || (!Tail.Version && Tail.Cells.Num()))
    {
        Error = TEXT("foliage-grid-tail-data-invalid");
        return false;
    }
    const FString Text = FString(FoliageFormat) + TEXT("\n") + FString::FromInt(Tail.Version) + TEXT("\n") +
        FBase64::Encode(Tail.Cells);
    Object->GetOutermost()->GetMetaData()->SetValue(Object, TailKey, *Text);
    FP4FoliageGridTail Again;
    if (!P4StoredFoliageGridTail(Object, Again) || Again.Version != Tail.Version || Again.Cells != Tail.Cells)
    {
        Error = TEXT("foliage-grid-tail-record-not-read-back");
        return false;
    }
    Error.Reset();
    return true;
}

bool P4StoredFoliageGridTail(const UObject* Object, FP4FoliageGridTail& Out)
{
    Out = FP4FoliageGridTail();
    if (!IsValid(Object) || !Object->GetOutermost())
        return false;
    UMetaData* Data = Object->GetOutermost()->GetMetaData();
    if (!Data || !Data->HasValue(Object, TailKey))
        return false;
    TArray<FString> Parts;
    FString(Data->GetValue(Object, TailKey)).ParseIntoArray(Parts, TEXT("\n"), false);
    int32 Version = -1;
    if (Parts.Num() != 3 || Parts[0] != FoliageFormat || !LexTryParseString(Version, *Parts[1]) ||
        (Version != 0 && Version != 2))
        return false;
    Out.Version = uint8(Version);
    if (!FBase64::Decode(Parts[2], Out.Cells) || Out.Cells.Num() % 4 || (!Version && Out.Cells.Num()))
        return false;
    return true;
}

bool P4PackageHasGameNativeTails(UPackage* Package)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
        if (IsValid(Object) && !Object->IsEditorOnly() && !Object->HasAnyFlags(RF_Transient) && HasTailRecord(Object))
            return true;
    return false;
}

bool P4TranslateGameNativeTailCook(
    UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    Error.Reset();
    TMap<FString, FP4FoliageGridTail> Wanted;
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package, Objects, true);
        for (UObject* Object : Objects)
        {
            FP4FoliageGridTail Tail;
            if (!IsValid(Object) || Object->IsEditorOnly() || Object->HasAnyFlags(RF_Transient) ||
                !HasTailRecord(Object))
                continue;
            if (!KeepsTail(Object) || !P4StoredFoliageGridTail(Object, Tail))
            {
                Error = TEXT("native-tail-record-invalid: ") + Object->GetPathName();
                return false;
            }
            FString Path = Object->GetPathName(Package);
            Path.ReplaceInline(TEXT(":"), TEXT("."));
            Wanted.Add(Path, MoveTemp(Tail));
        }
    }
    if (!Wanted.Num())
        return true;
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Native.Error.Code.c_str());
        return false;
    }
    if (!P4ValidateStockEngineCookRelocation(Header, Body, Error))
        return false;
    std::map<std::size_t, std::vector<uint8>> Replacements;
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
    {
        std::string Path;
        if (!Native.Reference(int32(At + 1), Path))
        {
            Error = TEXT("native-tail-cook-export-unnamed");
            return false;
        }
        const FP4FoliageGridTail* Tail = Wanted.Find(UTF8_TO_TCHAR(Path.c_str()));
        if (!Tail)
            continue;
        const auto& Export = Native.Exports[At];
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Body, Native.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Tags.Error.Code.empty())
        {
            Error = TEXT("native-tail-cook-properties-unreadable");
            return false;
        }
        // The project's class writes only UObject's id flag after its properties: that is replaced by the
        // game's whole tail, which starts with the same flag.
        const std::size_t End = Export.Offset + Export.Size;
        if (End - Tags.End != 4 || Body[Tags.End] || Body[Tags.End + 1] || Body[Tags.End + 2] || Body[Tags.End + 3])
        {
            Error = TEXT("native-tail-cook-object-already-has-native-data: ") + FString(UTF8_TO_TCHAR(Path.c_str()));
            return false;
        }
        TArray<uint8> Written;
        P4WriteFoliageGridTail(*Tail, Written);
        std::vector<uint8> Rebuilt(Body.begin() + Export.Offset, Body.begin() + Tags.End);
        Rebuilt.insert(Rebuilt.end(), Written.GetData(), Written.GetData() + Written.Num());
        Replacements.emplace(At, std::move(Rebuilt));
    }
    if (int32(Replacements.size()) != Wanted.Num())
    {
        Error = TEXT("native-tail-cook-object-not-serialized");
        return false;
    }
    P4MapKit::Export::RewrittenPackage Candidate;
    std::string Why;
    if (!P4MapKit::Export::RebuildCookedExports(Header, Body, Native.Names, Replacements, Candidate, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    if (!P4RelocatePackageTextureImages(Header, Body, Candidate.Header, Candidate.Body, Error))
        return false;
    // Read the actual output again: the tail counts only when it reads back as the kept one.
    const auto Checked = P4MapKit::ReadPackage(Candidate.Header, Candidate.Body);
    if (!Checked.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Checked.Error.Code.c_str());
        return false;
    }
    for (const auto& Pair : Replacements)
    {
        const auto& Export = Checked.Exports[Pair.first];
        std::string Path;
        Checked.Reference(int32(Pair.first + 1), Path);
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Candidate.Body, Checked.Names, Export.Offset, Export.Offset + Export.Size);
        FP4FoliageGridTail Read;
        FString Problem;
        const FP4FoliageGridTail& Kept = Wanted[UTF8_TO_TCHAR(Path.c_str())];
        if (!Tags.Error.Code.empty() ||
            !P4ReadFoliageGridTail(
                Candidate.Body.data() + Tags.End, int64(Export.Offset + Export.Size - Tags.End), Read, Problem) ||
            Read.Version != Kept.Version || Read.Cells != Kept.Cells)
        {
            Error = TEXT("native-tail-cook-readback-differs: ") + FString(UTF8_TO_TCHAR(Path.c_str()));
            return false;
        }
    }
    Header = std::move(Candidate.Header);
    Body = std::move(Candidate.Body);
    return true;
}

#include "P4TextureInventory.h"
#include "P4MapKitCore/texture_payload.h"
#include "P4MapKitCore/texture_pixels.h"
#include "P4NativeHash.h"
#include "P4TextureProperties.h"
#include "Engine/Texture.h"
#include "RHI.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
bool TextureShape(const std::string& Class, P4MapKit::ETextureShape& Shape, bool& LightMap)
{
    LightMap = Class == "/Script/Engine.LightMapTexture2D";
    if (Class == "/Script/Engine.Texture2D" || LightMap || Class == "/Script/Engine.ShadowMapTexture2D")
        Shape = P4MapKit::ETextureShape::Plane;
    else if (Class == "/Script/Engine.TextureCube")
        Shape = P4MapKit::ETextureShape::Cube;
    else if (Class == "/Script/Engine.Texture2DArray")
        Shape = P4MapKit::ETextureShape::Array;
    else if (Class == "/Script/Engine.VolumeTexture")
        Shape = P4MapKit::ETextureShape::Volume;
    else
        return false;
    return true;
}
bool TexturePixelLayout(const std::string& Name, P4MapKit::FTexturePixelLayout& Layout)
{
    if (Name.size() >= NAME_SIZE || Name.compare(0, 3, "PF_") != 0)
        return false;
    // These formats need special minimum-block or planar/platform layouts;
    // a generic block footprint cannot establish their complete byte layout.
    if (Name == "PF_PVRTC2" || Name == "PF_PVRTC4" || Name == "PF_NV12" || Name.compare(0, 16, "PF_PLATFORM_HDR_") == 0)
        return false;
    UEnum* Enum = UTexture::GetPixelFormatEnum();
    const int64 Value = Enum ? Enum->GetValueByName(FName(UTF8_TO_TCHAR(Name.c_str()))) : INDEX_NONE;
    if (Value <= PF_Unknown || Value >= PF_MAX)
        return false;
    const auto& Info = GPixelFormats[Value];
    if (Info.BlockSizeX <= 0 || Info.BlockSizeY <= 0 || Info.BlockSizeZ <= 0 || Info.BlockBytes <= 0)
        return false;
    Layout = {Name, uint32(Info.BlockSizeX), uint32(Info.BlockSizeY), uint32(Info.BlockSizeZ), uint32(Info.BlockBytes)};
    return true;
}
const TCHAR* TextureBulkKind(P4MapKit::EBulkSource Source)
{
    using P4MapKit::EBulkSource;
    switch (Source)
    {
        case EBulkSource::Body:
            return TEXT("uexp");
        case EBulkSource::Regular:
            return TEXT("ubulk");
        case EBulkSource::Optional:
            return TEXT("uptnl");
        case EBulkSource::Mapped:
            return TEXT("m.ubulk");
        default:
            return TEXT("unavailable");
    }
}
FString TextureExact(std::uint64_t Value)
{
    return FString::Printf(TEXT("%llu"), uint64(Value));
}
FString TextureSigned(std::int64_t Value)
{
    return FString::Printf(TEXT("%lld"), int64(Value));
}
const TCHAR* TextureTexelName(P4MapKit::ETextureTexels Texels)
{
    switch (Texels)
    {
        case P4MapKit::ETextureTexels::Gray8:
            return TEXT("G8");
        case P4MapKit::ETextureTexels::Gray16:
            return TEXT("G16");
        case P4MapKit::ETextureTexels::RGBA16F:
            return TEXT("RGBA16F");
        default:
            return TEXT("RGBA8");
    }
}
bool TexturePixelHash(const P4MapKit::FTexturePlatformPayload& Platform, const P4MapKit::FTextureMipPayload& Mip,
    P4MapKit::ETextureShape Shape, const std::vector<std::uint8_t>& Body, const FString& HeaderFile, FString& Digest,
    P4MapKit::FTextureDecodeFormat& Format, uint64& PixelBytes, P4MapKit::FReadError& Error)
{
    using namespace P4MapKit;
    const auto& Range = Mip.Bulk;
    const auto Reject = [&](const char* Code)
    {
        Error = {Range.Header.Start, Code};
        return false;
    };
    if (Range.Source == EBulkSource::None)
        return Reject("texture-pixel-mip-unavailable");
    if (!TextureDecodeFormat(Platform.Format, Format))
        return Reject("texture-pixel-codec-unqualified");
    const uint32 Slices = Shape == ETextureShape::Cube ? Platform.Slices : Mip.LogicalDepth;
    PixelBytes = uint64(Mip.LogicalWidth) * Mip.LogicalHeight * Slices * Format.PixelBytes;
    TUniquePtr<FArchive> File;
    if (Range.Source != EBulkSource::Body)
    {
        File.Reset(
            IFileManager::Get().CreateFileReader(*FPaths::ChangeExtension(HeaderFile, TextureBulkKind(Range.Source))));
        if (!File || File->TotalSize() < 0 || Range.Offset > uint64(File->TotalSize()) ||
            Range.Size > uint64(File->TotalSize()) - Range.Offset)
            return Reject("texture-bulk-file-unreadable-or-changed");
    }
    const bool Hashed = P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Update)
        {
            return DecodeTextureRows(
                Platform.Format, Mip.LogicalWidth, Mip.LogicalHeight, Slices, Range.Size, MAX_uint64,
                [&](std::uint64_t At, std::uint8_t* Bytes, std::size_t Count)
                {
                    if (At > Range.Size || Count > Range.Size - At)
                        return false;
                    const auto Offset = Range.Offset + At;
                    if (Range.Source == EBulkSource::Body)
                    {
                        if (Offset > Body.size() || Count > Body.size() - Offset)
                            return false;
                        FMemory::Memcpy(Bytes, Body.data() + std::size_t(Offset), Count);
                        return true;
                    }
                    File->Seek(int64(Offset));
                    File->Serialize(Bytes, int64(Count));
                    return !File->IsError();
                },
                [&](std::uint32_t, std::uint32_t, const std::uint8_t* Bytes, std::size_t Count)
                {
                    for (std::size_t At = 0; At < Count; At += 65536)
                        if (!Update(Bytes + At, int32(std::min<std::size_t>(65536, Count - At))))
                            return false;
                    return true;
                },
                {}, Error);
        },
        Digest);
    if (!Hashed && Error.Code.empty())
        return Reject("texture-pixel-hash-failed");
    return Hashed;
}
bool TextureRangeHash(const P4MapKit::FByteBulkRange& Range, const std::vector<std::uint8_t>& Body,
    const FString& HeaderFile, FString& Digest, P4MapKit::FReadError& Error)
{
    using P4MapKit::EBulkSource;
    if (Range.Source == EBulkSource::None)
    {
        Digest.Empty();
        return true;
    }
    TUniquePtr<FArchive> File;
    if (Range.Source != EBulkSource::Body)
    {
        File.Reset(
            IFileManager::Get().CreateFileReader(*FPaths::ChangeExtension(HeaderFile, TextureBulkKind(Range.Source))));
        if (!File || File->TotalSize() < 0 || Range.Offset > uint64(File->TotalSize()) ||
            Range.Size > uint64(File->TotalSize()) - Range.Offset)
        {
            Error = {Range.Header.Start, "texture-bulk-file-unreadable-or-changed"};
            return false;
        }
    }
    const bool Hashed = P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Update)
        {
            return P4MapKit::StreamByteBulk(
                Range,
                [&](EBulkSource Source, std::uint64_t At, std::uint8_t* Bytes, std::size_t Count)
                {
                    if (Source == EBulkSource::Body)
                    {
                        if (At > Body.size() || Count > Body.size() - At)
                            return false;
                        FMemory::Memcpy(Bytes, Body.data() + std::size_t(At), Count);
                        return true;
                    }
                    File->Seek(int64(At));
                    File->Serialize(Bytes, int64(Count));
                    return !File->IsError();
                },
                [&](const std::uint8_t* Bytes, std::size_t Count) { return Update(Bytes, int32(Count)); }, {}, Error);
        },
        Digest);
    if (!Hashed && Error.Code.empty())
        Error = {Range.Header.Start, "texture-payload-hash-failed"};
    return Hashed;
}
} //namespace

bool P4TextureShape(const std::string& Class, P4MapKit::ETextureShape& Shape, bool& LightMap)
{
    return TextureShape(Class, Shape, LightMap);
}
bool P4TexturePixelLayout(const std::string& Name, P4MapKit::FTexturePixelLayout& Layout)
{
    return TexturePixelLayout(Name, Layout);
}

TSharedPtr<FJsonObject> P4TexturePayloadInventory(const P4MapKit::FPackageInventory& Inventory,
    const P4MapKit::FPackageExport& Export, const P4MapKit::FPropertyInventory& Properties,
    const std::vector<std::uint8_t>& Body, const FString& HeaderFile, bool DecodePixels)
{
    using namespace P4MapKit;
    std::string Class;
    ETextureShape Shape;
    bool LightMap = false;
    if (!Inventory.Reference(Export.Class, Class) || !TextureShape(Class, Shape, LightMap))
        return nullptr;
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("editable_import_qualified"), false);
    Result->SetBoolField(TEXT("original_editable_source_recovered"), false);
    Result->SetBoolField(TEXT("pixels_decoded"), false);
    Result->SetBoolField(TEXT("native_skip_offsets_qualified"), false);
    FP4TexturePropertyDocument PropertyDocument;
    FString PropertyError;
    UClass* Schema = IsInGameThread() ? FindObject<UClass>(nullptr, UTF8_TO_TCHAR(Class.c_str())) : nullptr;
    const bool PropertyValid = P4InspectTextureProperties(
        Inventory, Export, Properties, Body, Schema, PropertyDocument, PropertyError);
    Result->SetBoolField(TEXT("properties_valid"), PropertyValid);
    Result->SetStringField(TEXT("property_error"), PropertyError);
    if (PropertyValid)
        Result->SetObjectField(TEXT("property_inventory"), P4TexturePropertyReport(PropertyDocument));
    FReadError Error = Properties.Error;
    FTextureNativePayload Payload;
    FByteBulkContext Context;
    Context.HeaderSize = Inventory.HeaderSize;
    Context.HasBulkStart = Inventory.HasBulkDataStartOffset;
    Context.BulkStart = Inventory.BulkDataStartOffset;
    Context.FileSizes[0] = int64(Body.size());
    const TCHAR* Suffixes[] = {TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")};
    for (int32 Index = 0; Index < 3; ++Index)
        Context.FileSizes[std::size_t(Index + 1)] = IFileManager::Get().FileSize(
            *FPaths::ChangeExtension(HeaderFile, Suffixes[Index]));
    FByteReader Reader(Body, Properties.End, Export.Offset + Export.Size);
    if (Error.Code.empty() &&
        !ReadCookedTexturePayload(Reader, Inventory.Names, Shape, LightMap, Context, TexturePixelLayout, Payload))
        Error = Reader.Error;
    TArray<TSharedPtr<FJsonValue>> Platforms;
    if (Error.Code.empty())
        for (const auto& Platform : Payload.Platforms)
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("format"), UTF8_TO_TCHAR(Platform.Format.c_str()));
            Item->SetStringField(TEXT("serialized_skip_offset"), TextureSigned(Platform.SkipOffset));
            Item->SetNumberField(TEXT("width"), Platform.Width);
            Item->SetNumberField(TEXT("height"), Platform.Height);
            Item->SetNumberField(TEXT("packed_data"), Platform.Packed);
            Item->SetNumberField(TEXT("slices"), Platform.Slices);
            Item->SetNumberField(TEXT("extra_data"), Platform.ExtraData);
            Item->SetNumberField(TEXT("mips_in_tail"), Platform.MipsInTail);
            Item->SetNumberField(TEXT("first_mip"), Platform.FirstMip);
            Item->SetNumberField(TEXT("start"), double(Platform.Start));
            Item->SetNumberField(TEXT("end"), double(Platform.End));
            TArray<TSharedPtr<FJsonValue>> Mips;
            for (const auto& Mip : Platform.Mips)
            {
                FString Digest;
                if (!TextureRangeHash(Mip.Bulk, Body, HeaderFile, Digest, Error))
                    break;
                auto Data = MakeShared<FJsonObject>();
                Data->SetNumberField(TEXT("width"), Mip.Width);
                Data->SetNumberField(TEXT("height"), Mip.Height);
                Data->SetNumberField(TEXT("depth"), Mip.Depth);
                Data->SetNumberField(TEXT("logical_width"), Mip.LogicalWidth);
                Data->SetNumberField(TEXT("logical_height"), Mip.LogicalHeight);
                Data->SetNumberField(TEXT("logical_depth"), Mip.LogicalDepth);
                Data->SetNumberField(TEXT("bulk_flags"), Mip.Bulk.Header.Flags);
                Data->SetStringField(TEXT("element_count"), TextureExact(Mip.Bulk.Header.Count));
                Data->SetStringField(TEXT("serialized_size"), TextureExact(Mip.Bulk.Header.Size));
                Data->SetStringField(TEXT("serialized_offset"), TextureSigned(Mip.Bulk.Header.Offset));
                Data->SetNumberField(TEXT("old_version"), Mip.Bulk.Header.OldVersion);
                Data->SetStringField(TEXT("storage"), TextureBulkKind(Mip.Bulk.Source));
                Data->SetStringField(TEXT("offset"), TextureExact(Mip.Bulk.Offset));
                Data->SetStringField(TEXT("bytes"), TextureExact(Mip.Bulk.Size));
                Data->SetStringField(TEXT("sha256"), Digest);
                if (DecodePixels)
                {
                    FTextureDecodeFormat Format;
                    uint64 PixelBytes = 0;
                    if (!TexturePixelHash(Platform, Mip, Shape, Body, HeaderFile, Digest, Format, PixelBytes, Error))
                        break;
                    Data->SetStringField(TEXT("pixel_encoding"), TextureTexelName(Format.Texels));
                    Data->SetStringField(TEXT("pixel_bytes"), TextureExact(PixelBytes));
                    Data->SetStringField(TEXT("pixel_sha256"), Digest);
                }
                Mips.Add(MakeShared<FJsonValueObject>(Data));
            }
            if (!Error.Code.empty())
                break;
            Item->SetArrayField(TEXT("mips"), Mips);
            Platforms.Add(MakeShared<FJsonValueObject>(Item));
        }
    Result->SetBoolField(TEXT("metadata_valid"), Error.Code.empty());
    Result->SetStringField(TEXT("error"), UTF8_TO_TCHAR(Error.Code.c_str()));
    Result->SetNumberField(TEXT("error_offset"), double(Error.Offset));
    if (Error.Code.empty())
    {
        Result->SetBoolField(TEXT("pixels_decoded"), DecodePixels);
        Result->SetBoolField(TEXT("normal_z_reconstructed"), false);
        Result->SetBoolField(TEXT("color_transfer_applied"), false);
        Result->SetNumberField(TEXT("start"), double(Payload.Start));
        Result->SetNumberField(TEXT("end"), double(Payload.End));
        Result->SetBoolField(TEXT("has_guid"), Payload.HasGuid);
        TArray<TSharedPtr<FJsonValue>> Guid, Strip;
        for (const auto Word : Payload.Guid)
            Guid.Add(MakeShared<FJsonValueNumber>(Word));
        for (const auto Flag : Payload.StripFlags)
            Strip.Add(MakeShared<FJsonValueNumber>(Flag));
        Result->SetArrayField(TEXT("guid_words"), Guid);
        Result->SetArrayField(TEXT("strip_flags"), Strip);
        Result->SetNumberField(TEXT("lightmap_flags"), Payload.LightMapFlags);
        Result->SetArrayField(TEXT("platforms"), Platforms);
    }
    return Result;
}

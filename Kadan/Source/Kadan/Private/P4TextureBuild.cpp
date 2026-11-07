#include "P4TextureBuild.h"
#include "P4TextureAuthoring.h"
#include "P4TextureInventory.h"
#include "P4TextureProperties.h"
#include "P4MapKitCore/texture_payload.h"
#include "P4MapKitCore/texture_pixels.h"
#include "P4MapKitCore/physics_asset_native.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"

namespace
{
FString Text(const std::string& Value)
{
    return UTF8_TO_TCHAR(Value.c_str());
}
bool ReadFile(const FString& Path, std::vector<std::uint8_t>& Out)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
        return false;
    Out.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    return true;
}
// One mip decoded to rows of pixels, slice after slice. Empty when it could not be decoded.
TArray<uint8> Decoded(const std::string& Format, uint32 Width, uint32 Height, uint32 Slices, uint64 Bytes,
    const TFunctionRef<bool(uint64 At, uint8* Out, SIZE_T Count)>& ReadAt)
{
    P4MapKit::FTextureDecodeFormat Layout;
    P4MapKit::FReadError Error;
    TArray<uint8> Out;
    if (!P4MapKit::TextureDecodeFormat(Format, Layout))
        return Out;
    const SIZE_T Row = SIZE_T(Width) * Layout.PixelBytes;
    Out.SetNumZeroed(int32(Row * Height * Slices));
    const bool Good = P4MapKit::DecodeTextureRows(
        Format, Width, Height, Slices, Bytes, uint64(Out.Num()),
        [&](std::uint64_t At, std::uint8_t* To, std::size_t Count) { return ReadAt(At, To, Count); },
        [&](std::uint32_t Slice, std::uint32_t Line, const std::uint8_t* From, std::size_t Count)
        {
            if (Count != Row || Slice >= Slices || Line >= Height)
                return false;
            FMemory::Memcpy(Out.GetData() + (SIZE_T(Slice) * Height + Line) * Row, From, Count);
            return true;
        },
        {}, Error);
    if (!Good)
        Out.Reset();
    return Out;
}
} //namespace

bool P4RecoverOriginalTextureCookFormat(UTexture* Texture, const FString& GameFormat)
{
    if (!IsInGameThread() || !IsValid(Texture) || GameFormat != TEXT("PF_DXT5") || !Texture->Source.IsValid() ||
        Texture->Source.GetNumBlocks() != 1 || Texture->Source.GetNumLayers() != 1 ||
        Texture->Source.GetFormat() != TSF_BGRA8 || Texture->CompressionNone || Texture->CompressionNoAlpha ||
        Texture->CompressionYCoCg ||
        (Texture->CompressionSettings != TC_Default && Texture->CompressionSettings != TC_Masks) ||
        (Texture->MipGenSettings != TMGS_LeaveExistingMips && Texture->MipGenSettings != TMGS_NoMipmaps))
        return false;
    // UE4.26 GetDefaultTextureFormatName selects DXT5 when this editor-only
    // setting is set, rather than AutoDXT which discards an all-opaque alpha
    // channel. TextureCompressor only dithers when filtering/generated mips or
    // downscaling; our surviving mips are kept at their original dimensions.
    // This reconstructs a compatible cook choice, not the missing artist flag.
    // In NoMipmaps mode a group's inherited downscale could invoke the dither
    // filter. Explicitly retain the surviving payload's resolution instead.
    Texture->Downscale.Default = 1.0f;
    Texture->Downscale.PerPlatform.Empty();
    Texture->bDitherMipMapAlpha = true;
    return true;
}

UTexture* P4BuildOriginalPackageTexture(const FString& HeaderFile, const std::vector<std::uint8_t>& Body,
    const P4MapKit::FPackageInventory& Package, const P4MapKit::FPackageExport* Export, FP4TextureBuildReport& Report,
    FString& Error, bool CompareWithCook)
{
    using P4MapKit::EBulkSource;
    Report = FP4TextureBuildReport();
    Error.Reset();
    std::string Class;
    P4MapKit::ETextureShape Shape = P4MapKit::ETextureShape::Plane;
    bool LightMap = false;
    if (!Package.Reference(Export->Class, Class) || !P4TextureShape(Class, Shape, LightMap))
    {
        Error = TEXT("texture-build-export-is-not-a-texture");
        return nullptr;
    }
    Report.Class = Text(Class);
    Report.Package = Text(Package.Package);
    Report.Object = Text(Export->Name);

    const auto Properties = P4MapKit::ReadPropertyEnvelopes(
        Body, Package.Names, Export->Offset, Export->Offset + Export->Size);
    FP4TexturePropertyDocument Document;
    if (!P4InspectTextureProperties(
            Package, *Export, Properties, Body, FindObject<UClass>(nullptr, *Report.Class), Document, Error))
        return nullptr;
    if (!Document.NativePropertiesRepresentable)
    {
        Error = Document.NativePropertyIssues.Num() ? Document.NativePropertyIssues[0]
                                                    : FString(TEXT("texture-build-properties-not-representable"));
        return nullptr;
    }
    Report.GameTextureGroup = Document.GameTextureGroup;

    const TCHAR* const Suffixes[] = {TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")};
    P4MapKit::FTextureNativePayload Payload;
    P4MapKit::FByteBulkContext Context;
    Context.HeaderSize = Package.HeaderSize;
    Context.FileSizes[0] = int64(Body.size());
    for (int32 Index = 0; Index < 3; ++Index)
        Context.FileSizes[std::size_t(Index + 1)] = IFileManager::Get().FileSize(
            *FPaths::ChangeExtension(HeaderFile, Suffixes[Index]));
    P4MapKit::FByteReader Reader(Body, Properties.End, Export->Offset + Export->Size);
    if (!P4MapKit::ReadCookedTexturePayload(
            Reader, Package.Names, Shape, LightMap, Context, P4TexturePixelLayout, Payload))
    {
        Error = Text(Reader.Error.Code);
        return nullptr;
    }
    if (Payload.Platforms.size() != 1 || Payload.Platforms[0].Mips.empty())
    {
        Error = TEXT("texture-build-expects-one-platform-with-mips");
        return nullptr;
    }
    const P4MapKit::FTexturePlatformPayload& Platform = Payload.Platforms[0];

    // Mip bytes are in the .uexp or in a file next to it. Files are opened when first read.
    TUniquePtr<FArchive> Files[3];
    const auto ReadAt = [&](EBulkSource Source, uint64 At, uint8* Out, SIZE_T Count)
    {
        if (Source == EBulkSource::Body)
        {
            if (At > Body.size() || Count > Body.size() - At)
                return false;
            FMemory::Memcpy(Out, Body.data() + std::size_t(At), Count);
            return true;
        }
        const int32 Slot = Source == EBulkSource::Regular ? 0
            : Source == EBulkSource::Optional             ? 1
            : Source == EBulkSource::Mapped               ? 2
                                                          : -1;
        if (Slot < 0)
            return false;
        if (!Files[Slot])
            Files[Slot].Reset(
                IFileManager::Get().CreateFileReader(*FPaths::ChangeExtension(HeaderFile, Suffixes[Slot])));
        FArchive* File = Files[Slot].Get();
        if (!File || File->TotalSize() < 0 || At > uint64(File->TotalSize()) || Count > uint64(File->TotalSize()) - At)
            return false;
        File->Seek(int64(At));
        File->Serialize(Out, int64(Count));
        return !File->IsError();
    };
    FP4TextureSourceRequest Request;
    Request.NativeClass = Report.Class;
    Request.Shape = Shape;
    Request.Platform = &Platform;
    Request.LightMapFlags = Payload.LightMapFlags;
    Request.MaxDecodedBytes = 1024ull * 1024ull * 1024ull;
    Request.ReadAt = ReadAt;
    FP4TextureSourceResult Source;
    P4MapKit::FReadError ReadError;
    if (!P4BuildPrivateTextureSource(Request, Source, ReadError))
    {
        Error = Text(ReadError.Code);
        return nullptr;
    }
    UTexture* Texture = Source.Texture;
    TStrongObjectPtr<UTexture> Guard(Texture);
    Report.SourceEqual = Source.SourceReadbackVerified;
    Report.FirstMip = int32(Source.SurvivingFirstMip);

    FString Canonical;
    if (!P4PrepareTextureProperties(Texture, Document, Canonical, Error))
    {
        Texture->MarkPendingKill();
        return nullptr;
    }
    Report.PropertiesEqual = true;
    // The source holds the game's own mips, so the engine keeps them and makes none of its own.
    Texture->MipGenSettings = Platform.Mips.size() > 1 ? TMGS_LeaveExistingMips : TMGS_NoMipmaps;
    Texture->PowerOfTwoMode = ETexturePowerOfTwoSetting::None;
    Texture->DeferCompression = false;
    P4RecoverOriginalTextureCookFormat(Texture, Text(Platform.Format));
    if (!CompareWithCook)
    {
        Report.GameFormat = Text(Platform.Format);
        Report.Mips = int32(Platform.Mips.size());
        Report.Width = int32(Platform.Mips[0].LogicalWidth);
        Report.Height = int32(Platform.Mips[0].LogicalHeight);
        Report.Equal = Report.SourceEqual && Report.PropertiesEqual;
        return Texture;
    }
    // The engine compresses nothing for display in an editor that has no renderer, and what a mod
    // ships is a cook's output. So the texture is compressed as a cook does it, for the platform the
    // kit's export cooks for. That runs on worker threads.
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!Target)
    {
        Error = TEXT("texture-build-no-cook-platform");
        Texture->MarkPendingKill();
        return nullptr;
    }
    Texture->BeginCacheForCookedPlatformData(Target);
    const double Deadline = FPlatformTime::Seconds() + 300.0;
    while (!Texture->IsCachedCookedPlatformDataLoaded(Target) && FPlatformTime::Seconds() < Deadline)
        FPlatformProcess::Sleep(0.002f);

    const P4MapKit::FTextureMipPayload& Top = Platform.Mips[0];
    const uint32 Slices = Shape == P4MapKit::ETextureShape::Cube ? Platform.Slices : Top.LogicalDepth;
    Report.GameFormat = Text(Platform.Format);
    Report.Mips = int32(Platform.Mips.size());
    Report.Width = int32(Top.LogicalWidth);
    Report.Height = int32(Top.LogicalHeight);
    Report.Slices = int32(Slices);
    TMap<FString, FTexturePlatformData*>* Cooked = Texture->GetCookedPlatformData();
    FTexturePlatformData* Built = Cooked && Cooked->Num() == 1 && Texture->IsCachedCookedPlatformDataLoaded(Target)
        ? Cooked->CreateIterator().Value()
        : nullptr;
    if (!Built)
    {
        Error = TEXT("texture-build-engine-made-no-platform-data");
        Texture->ClearAllCachedCookedPlatformData();
        Texture->MarkPendingKill();
        return nullptr;
    }
    const UEnum* Formats = UTexture::GetPixelFormatEnum();
    Report.BuiltFormat = Formats ? Formats->GetNameStringByValue(int64(Built->PixelFormat)) : FString();
    Report.BuiltWidth = Built->SizeX;
    Report.BuiltHeight = Built->SizeY;
    Report.BuiltMips = Built->Mips.Num();
    Report.BuiltSlices = Shape == P4MapKit::ETextureShape::Cube ? 6 : Built->GetNumSlices();
    Report.LayoutEqual = Report.BuiltFormat.Equals(Report.GameFormat, ESearchCase::CaseSensitive) &&
        Report.BuiltWidth == Report.Width && Report.BuiltHeight == Report.Height && Report.BuiltMips == Report.Mips &&
        Report.BuiltSlices == Report.Slices;

    // How far the engine's compression of the decoded pixels is from the game's own, for eight-bit formats.
    P4MapKit::FTextureDecodeFormat Layout;
    if (Report.LayoutEqual && P4MapKit::TextureDecodeFormat(Platform.Format, Layout) &&
        Layout.Texels == P4MapKit::ETextureTexels::RGBA8)
    {
        // A copy of each built mip. A cook keeps every mip in memory.
        TArray<void*> BuiltMips;
        BuiltMips.SetNumZeroed(Report.BuiltMips);
        TArray<int64> BuiltMipSizes;
        BuiltMipSizes.SetNumZeroed(Report.BuiltMips);
        for (int32 Index = 0; Index < Report.BuiltMips; ++Index)
        {
            const FByteBulkData& Bulk = Built->Mips[Index].BulkData;
            if (!Bulk.IsBulkDataLoaded() || Bulk.GetBulkDataSize() <= 0)
                continue;
            BuiltMipSizes[Index] = Bulk.GetBulkDataSize();
            BuiltMips[Index] = FMemory::Malloc(SIZE_T(Bulk.GetBulkDataSize()));
            FMemory::Memcpy(BuiltMips[Index], Bulk.LockReadOnly(), SIZE_T(Bulk.GetBulkDataSize()));
            Bulk.Unlock();
        }
        if (!BuiltMips.Contains(nullptr))
        {
            int32 Largest = 0;
            bool Measured = true, BytesEqual = true;
            for (int32 Index = 0; Measured && Index < Report.Mips; ++Index)
            {
                const P4MapKit::FTextureMipPayload& Mip = Platform.Mips[std::size_t(Index)];
                const uint8* Theirs = static_cast<const uint8*>(BuiltMips[Index]);
                if (uint64(BuiltMipSizes[Index]) != Mip.Bulk.Size)
                {
                    Measured = false;
                    break;
                }
                uint8 Chunk[64 * 1024];
                for (uint64 At = 0; Measured && At < Mip.Bulk.Size; At += sizeof Chunk)
                {
                    const SIZE_T Count = SIZE_T(FMath::Min<uint64>(sizeof Chunk, Mip.Bulk.Size - At));
                    Measured = ReadAt(Mip.Bulk.Source, Mip.Bulk.Offset + At, Chunk, Count);
                    if (Measured && FMemory::Memcmp(Chunk, Theirs + At, Count))
                        BytesEqual = false;
                }
                if (!Measured)
                    break;
                const TArray<uint8> Game = Decoded(Platform.Format, Mip.LogicalWidth, Mip.LogicalHeight, Slices,
                    Mip.Bulk.Size, [&](uint64 At, uint8* Out, SIZE_T Count)
                    { return ReadAt(Mip.Bulk.Source, Mip.Bulk.Offset + At, Out, Count); });
                const TArray<uint8> Engine = Decoded(Platform.Format, Mip.LogicalWidth, Mip.LogicalHeight, Slices,
                    Mip.Bulk.Size,
                    [&](uint64 At, uint8* Out, SIZE_T Count)
                    {
                        if (!Theirs || At > uint64(BuiltMipSizes[Index]) || Count > uint64(BuiltMipSizes[Index]) - At)
                            return false;
                        FMemory::Memcpy(Out, Theirs + At, Count);
                        return true;
                    });
                Measured = Game.Num() != 0 && Game.Num() == Engine.Num();
                for (int32 At = 0; Measured && At < Game.Num(); ++At)
                    Largest = FMath::Max(Largest, FMath::Abs(int32(Game[At]) - int32(Engine[At])));
            }
            if (Measured)
            {
                Report.LargestBuiltDifference = Largest;
                Report.BuiltBytesCompared = true;
                Report.BuiltBytesEqual = BytesEqual;
            }
        }
        for (void* Mip : BuiltMips)
            if (Mip)
                FMemory::Free(Mip);
    }
    Report.Equal = Report.SourceEqual && Report.PropertiesEqual && Report.LayoutEqual;
    Texture->ClearAllCachedCookedPlatformData();
    return Texture;
}

UTexture* P4BuildOriginalTexture(
    const FString& HeaderFile, const FString& BodyFile, FP4TextureBuildReport& Report, FString& Error)
{
    using P4MapKit::EBulkSource;
    Report = FP4TextureBuildReport();
    Error.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("texture-build-requires-editor-thread");
        return nullptr;
    }
    std::vector<std::uint8_t> Header, Body;
    if (!ReadFile(HeaderFile, Header) ||
        !ReadFile(BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile, Body))
    {
        Error = TEXT("texture-build-source-unreadable");
        return nullptr;
    }
    const P4MapKit::FPackageInventory Package = P4MapKit::ReadPackage(Header, Body);
    if (!Package.Error.Code.empty())
    {
        Error = Text(Package.Error.Code);
        return nullptr;
    }

    const P4MapKit::FPackageExport* Export = nullptr;
    for (const auto& Candidate : Package.Exports)
    {
        std::string Name;
        P4MapKit::ETextureShape Kind;
        bool Light = false;
        if (!Package.Reference(Candidate.Class, Name) || !P4TextureShape(Name, Kind, Light))
            continue;
        if (Export)
        {
            Error = TEXT("texture-build-more-than-one-texture-in-package");
            return nullptr;
        }
        Export = &Candidate;
    }
    if (!Export)
    {
        Error = TEXT("texture-build-no-texture-in-package");
        return nullptr;
    }
    return P4BuildOriginalPackageTexture(
        HeaderFile, Body, Package, Export, Report, Error);
}

FString P4BuildOriginalTextureReport(const FString& HeaderFile, const FString& BodyFile)
{
    FP4TextureBuildReport Report;
    FString Error;
    TStrongObjectPtr<UTexture> Texture(P4BuildOriginalTexture(HeaderFile, BodyFile, Report, Error));
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Texture.IsValid());
    Result->SetStringField(TEXT("error"), Error);
    Result->SetStringField(TEXT("class"), Report.Class);
    Result->SetStringField(TEXT("package"), Report.Package);
    Result->SetStringField(TEXT("game_format"), Report.GameFormat);
    Result->SetStringField(TEXT("built_format"), Report.BuiltFormat);
    Result->SetStringField(TEXT("game_texture_group"), Report.GameTextureGroup);
    Result->SetNumberField(TEXT("width"), Report.Width);
    Result->SetNumberField(TEXT("height"), Report.Height);
    Result->SetNumberField(TEXT("slices"), Report.Slices);
    Result->SetNumberField(TEXT("mips"), Report.Mips);
    Result->SetNumberField(TEXT("built_width"), Report.BuiltWidth);
    Result->SetNumberField(TEXT("built_height"), Report.BuiltHeight);
    Result->SetNumberField(TEXT("built_slices"), Report.BuiltSlices);
    Result->SetNumberField(TEXT("built_mips"), Report.BuiltMips);
    Result->SetBoolField(TEXT("source_equal"), Report.SourceEqual);
    Result->SetBoolField(TEXT("properties_equal"), Report.PropertiesEqual);
    Result->SetBoolField(TEXT("layout_equal"), Report.LayoutEqual);
    Result->SetBoolField(TEXT("equal"), Report.Equal);
    Result->SetBoolField(TEXT("built_bytes_compared"), Report.BuiltBytesCompared);
    Result->SetBoolField(TEXT("built_bytes_equal"), Report.BuiltBytesEqual);
    Result->SetNumberField(TEXT("largest_built_difference"), Report.LargestBuiltDifference);
    // The texture was only built to be compared. Nothing of it stays.
    if (Texture.IsValid())
        Texture->MarkPendingKill();
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

bool P4ReadPackageTextureImage(const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body,
    const std::string& Class, std::size_t TailStart, std::size_t TailEnd, UTexture* Into, FString& Error)
{
    using P4MapKit::EBulkSource;
    Error.Reset();
    if (Into && !IsInGameThread())
    {
        Error = TEXT("texture-image-requires-editor-thread");
        return false;
    }
    P4MapKit::ETextureShape Shape = P4MapKit::ETextureShape::Plane;
    bool LightMap = false;
    if (!P4TextureShape(Class, Shape, LightMap) || Shape != P4MapKit::ETextureShape::Plane || LightMap)
    {
        Error = TEXT("texture-image-not-of-a-plain-texture");
        return false;
    }
    if (TailStart > TailEnd || TailEnd > Body.size())
    {
        Error = TEXT("texture-image-outside-the-package");
        return false;
    }
    P4MapKit::FTextureNativePayload Payload;
    P4MapKit::FByteBulkContext Context;
    Context.HeaderSize = Package.HeaderSize;
    Context.FileSizes[0] = int64(Body.size());
    P4MapKit::FByteReader Reader(Body, TailStart, TailEnd);
    if (!P4MapKit::ReadCookedTexturePayload(
            Reader, Package.Names, Shape, LightMap, Context, P4TexturePixelLayout, Payload))
    {
        Error = Text(Reader.Error.Code);
        return false;
    }
    if (Reader.Tell() != TailEnd)
    {
        Error = TEXT("texture-image-leaves-data-unread");
        return false;
    }
    if (Payload.Platforms.size() != 1 || Payload.Platforms[0].Mips.empty())
    {
        Error = TEXT("texture-image-expects-one-platform-with-mips");
        return false;
    }
    const P4MapKit::FTexturePlatformPayload& Platform = Payload.Platforms[0];
    for (const auto& Mip : Platform.Mips)
        if (Mip.Bulk.Source != EBulkSource::Body)
        {
            Error = TEXT("texture-image-mips-not-inside-the-package");
            return false;
        }
    if (!Into)
        return true;
    FP4TextureSourceRequest Request;
    Request.NativeClass = Text(Class);
    Request.Shape = Shape;
    Request.Platform = &Platform;
    Request.LightMapFlags = Payload.LightMapFlags;
    Request.MaxDecodedBytes = 256ull * 1024ull * 1024ull;
    Request.ReadAt = [&Body](EBulkSource Source, uint64 At, uint8* Out, SIZE_T Count)
    {
        if (Source != EBulkSource::Body || At > Body.size() || Count > Body.size() - At)
            return false;
        FMemory::Memcpy(Out, Body.data() + std::size_t(At), Count);
        return true;
    };
    FP4TextureSourceResult Source;
    P4MapKit::FReadError ReadError;
    if (!P4BuildPrivateTextureSource(Request, Source, ReadError))
    {
        Error = Text(ReadError.Code);
        return false;
    }
    UTexture* Built = Source.Texture;
    TStrongObjectPtr<UTexture> Guard(Built);
    ON_SCOPE_EXIT
    {
        if (Built)
            Built->MarkPendingKill();
    };
    FTextureSource& In = Built->Source;
    if (In.GetNumBlocks() != 1 || In.GetNumLayers() != 1)
    {
        Error = TEXT("texture-image-has-blocks-or-layers");
        return false;
    }
    TArray64<uint8> Pixels;
    for (int32 Mip = 0; Mip < In.GetNumMips(); ++Mip)
    {
        TArray64<uint8> One;
        if (!In.GetMipData(One, Mip) || One.Num() != In.CalcMipSize(Mip))
        {
            Error = TEXT("texture-image-built-source-unreadable");
            return false;
        }
        Pixels.Append(One);
    }
    Into->Source.Init(
        In.GetSizeX(), In.GetSizeY(), In.GetNumSlices(), In.GetNumMips(), In.GetFormat(), Pixels.GetData());
    Into->Source.UseHashAsGuid();
    // As ALandscapeProxy::CreateLandscapeTexture makes a height map: the game's mips kept, the image stored as it is.
    Into->MipGenSettings = In.GetNumMips() > 1 ? TMGS_LeaveExistingMips : TMGS_NoMipmaps;
    Into->CompressionNone = Platform.Format == "PF_B8G8R8A8";
    return true;
}

bool P4PlacePackageInlinePhysicsBulk(const std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    const auto Package = P4MapKit::ReadPackage(Header, Body);
    if (!Package.Error.Code.empty())
    {
        Error = Text(Package.Error.Code);
        return false;
    }
    for (const auto& Export : Package.Exports)
    {
        std::string Class;
        if (!Package.Reference(Export.Class, Class) || Class != "/Script/Engine.BodySetup")
            continue;
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
        P4MapKit::FByteBulkContext Context;
        Context.HeaderSize = Package.HeaderSize;
        Context.FileSizes[0] = int64(Body.size());
        if (!Tags.Error.Code.empty() ||
            !P4MapKit::ReadInlinePhysicsBodyBulk(Body, Package.Names, Tags.End, Export.Offset + Export.Size, Context, false)
                 .Error.Code.empty())
            continue;
        bool Changed = false;
        P4MapKit::FReadError Problem;
        if (!P4MapKit::PlaceInlinePhysicsBodyBulk(
                Body, Package.Names, Tags.End, Export.Offset + Export.Size, Package.HeaderSize, Changed, Problem))
        {
            Error = TEXT("physics-body-placement-refused: ") + Text(Problem.Code);
            return false;
        }
    }
    Error.Reset();
    return true;
}

bool P4RelocatePackageTextureImages(const std::vector<uint8>& BeforeHeader, const std::vector<uint8>& BeforeBody,
    const std::vector<uint8>& AfterHeader, std::vector<uint8>& AfterBody, FString& Error)
{
    const auto Before = P4MapKit::ReadPackage(BeforeHeader, BeforeBody),
               After = P4MapKit::ReadPackage(AfterHeader, AfterBody);
    if (!Before.Error.Code.empty() || !After.Error.Code.empty() || Before.Exports.size() != After.Exports.size())
    {
        Error = TEXT("texture-relocation-package-invalid");
        return false;
    }
    auto Candidate = AfterBody;
    const auto Put64 = [&](std::size_t At, uint64 Value)
    {
        for (std::size_t Byte = 0; Byte < 8; ++Byte)
            Candidate[At + Byte] = uint8(Value >> (Byte * 8));
    };
    for (std::size_t Slot = 0; Slot < Before.Exports.size(); ++Slot)
    {
        const auto& Old = Before.Exports[Slot];
        const auto& New = After.Exports[Slot];
        std::string Class;
        if (!Before.Reference(Old.Class, Class))
        {
            Error = TEXT("texture-relocation-class-invalid");
            return false;
        }
        if (Class == "/Script/Engine.BodySetup")
        {
            const auto Tags = P4MapKit::ReadPropertyEnvelopes(
                BeforeBody, Before.Names, Old.Offset, Old.Offset + Old.Size);
            const auto NewTags = P4MapKit::ReadPropertyEnvelopes(
                Candidate, After.Names, New.Offset, New.Offset + New.Size);
            P4MapKit::FReadError Problem;
            if (!Tags.Error.Code.empty() || !NewTags.Error.Code.empty() ||
                !P4MapKit::RelocateInlinePhysicsBodyBulk(BeforeBody, Before.Names, Tags.End, Old.Offset + Old.Size,
                    Before.HeaderSize, Candidate, After.Names, NewTags.End, New.Offset + New.Size, After.HeaderSize,
                    Problem))
            {
                Error = TEXT("physics-body-relocation-refused: ") + Text(Problem.Code);
                return false;
            }
            continue;
        }
        if (Class != "/Script/Engine.Texture2D")
            continue;
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(BeforeBody, Before.Names, Old.Offset, Old.Offset + Old.Size);
        const auto NewTags = P4MapKit::ReadPropertyEnvelopes(Candidate, After.Names, New.Offset, New.Offset + New.Size);
        if (!Tags.Error.Code.empty() || !NewTags.Error.Code.empty())
        {
            Error = TEXT("texture-relocation-tags-invalid");
            return false;
        }
        P4MapKit::FByteBulkContext Context;
        Context.HeaderSize = Before.HeaderSize;
        Context.FileSizes[0] = int64(BeforeBody.size());
        P4MapKit::FByteReader Reader(BeforeBody, Tags.End, Old.Offset + Old.Size);
        P4MapKit::FTextureNativePayload Payload;
        if (!P4MapKit::ReadCookedTexturePayload(
                Reader, Before.Names, P4MapKit::ETextureShape::Plane, false, Context, P4TexturePixelLayout, Payload) ||
            Reader.Tell() != Old.Offset + Old.Size)
        {
            Error = FString(TEXT("texture-relocation-native-layout-unqualified: ")) +
                UTF8_TO_TCHAR(Reader.Error.Code.c_str());
            return false;
        }
        const int64 BodyDelta = int64(NewTags.End) - int64(Tags.End);
        const int64 FileDelta = int64(After.HeaderSize) - int64(Before.HeaderSize) + BodyDelta;
        for (const auto& Platform : Payload.Platforms)
        {
            if (Platform.SkipOffset != int64(Before.HeaderSize + Platform.End))
            {
                Error = FString::Printf(
                    TEXT(
                        "texture-relocation-skip-offset-unqualified: %s skip=%lld expected=%lld header=%lld platform_end=%lld"),
                    UTF8_TO_TCHAR(Old.Name.c_str()), int64(Platform.SkipOffset),
                    int64(Before.HeaderSize + Platform.End), int64(Before.HeaderSize), int64(Platform.End));
                return false;
            }
            Put64(std::size_t(int64(Platform.Start) + BodyDelta), uint64(Platform.SkipOffset + FileDelta));
            for (const auto& Mip : Platform.Mips)
            {
                const auto& Bulk = Mip.Bulk;
                if (Bulk.Source != P4MapKit::EBulkSource::Body || (Bulk.Header.Flags & P4MapKit::BulkAtEnd) ||
                    Bulk.Offset != Bulk.Header.End || Bulk.Offset + Bulk.Size > Old.Offset + Old.Size)
                {
                    Error = TEXT("texture-relocation-bulk-not-inline");
                    return false;
                }
                if (Bulk.Header.Offset != int64(Before.HeaderSize + Bulk.Offset))
                {
                    Error = TEXT("texture-relocation-bulk-offset-unqualified");
                    return false;
                }
                const std::size_t Width = Bulk.Header.Flags & P4MapKit::BulkSize64 ? 8 : 4;
                Put64(std::size_t(int64(Bulk.Header.Start + 4 + 2 * Width) + BodyDelta),
                    uint64(Bulk.Header.Offset + FileDelta));
                if (!std::equal(BeforeBody.begin() + Bulk.Offset, BeforeBody.begin() + Bulk.Offset + Bulk.Size,
                        Candidate.begin() + std::size_t(int64(Bulk.Offset) + BodyDelta)))
                {
                    Error = TEXT("texture-relocation-pixels-changed");
                    return false;
                }
            }
        }
        FString Why;
        if (!P4ReadPackageTextureImage(After, Candidate, Class, NewTags.End, New.Offset + New.Size, nullptr, Why))
        {
            Error = TEXT("texture-relocation-readback-failed: ") + Why;
            return false;
        }
    }
    AfterBody = std::move(Candidate);
    Error.Reset();
    return true;
}

#include "P4Replacement.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4BoundedSourceRead.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

UTexture2D* FP4CreatorTextureSource::Texture() const
{
    return Asset.Get();
}

bool P4VerifyCreatorTextureSource(UTexture2D* Asset, const FString& File, FP4CreatorTextureSource& Out, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Asset))
    {
        Error = TEXT("Creator source verification requires a texture on the editor thread");
        return false;
    }
    const FP4OriginalStatus Original = P4QueryOriginalAsset(Asset);
    if (Original.IsOriginal() || Original.State != EP4OriginalState::NotImported || P4IsVerifiedStandIn(Asset))
    {
        Error = TEXT("Choose a texture you imported from your own image");
        return false;
    }
    const FString Extension = FPaths::GetExtension(File).ToLower();
    if (Extension != TEXT("png") && Extension != TEXT("jpg") && Extension != TEXT("jpeg") && Extension != TEXT("tga") &&
        Extension != TEXT("bmp"))
    {
        Error = TEXT("Creator texture source must be an image file, never an Unreal package");
        return false;
    }
    const int64 Size = IFileManager::Get().FileSize(*File);
    TArray<uint8> Bytes;
    if (Size <= 0 || Size > 64 * 1024 * 1024 || !P4ReadBoundedSourceFile(File, 64 * 1024 * 1024, Bytes, Error) ||
        Bytes.Num() != Size)
    {
        Error = TEXT("Creator image is unreadable or exceeds 64 MiB");
        return false;
    }
    FString Digest;
    if (!P4HashSourceBuffers(Bytes.GetData(), Bytes.Num(), nullptr, 0, Digest))
    {
        Error = TEXT("Cannot hash actual creator source bytes");
        return false;
    }
    TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
    Factory->bCreateMaterial = false;
    Factory->bDeferCompression = true;
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4CreatorSource/") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    const uint8* Start = Bytes.GetData();
    TStrongObjectPtr<UTexture2D> Decoded(Cast<UTexture2D>(
        Factory->FactoryCreateBinary(UTexture2D::StaticClass(), Package.Get(), FName(TEXT("CreatorImage")),
            RF_Transient, nullptr, *Extension, Start, Bytes.GetData() + Bytes.Num(), GWarn)));
    if (!Decoded.IsValid())
    {
        Error = TEXT("Engine factory refused the creator image");
        return false;
    }
    FTextureSource& A = Asset->Source;
    FTextureSource& B = Decoded->Source;
    if (!A.IsValid() || !B.IsValid() || A.GetSizeX() != B.GetSizeX() || A.GetSizeY() != B.GetSizeY() ||
        A.GetNumSlices() != B.GetNumSlices() || A.GetNumMips() != B.GetNumMips() || A.GetFormat() != B.GetFormat() ||
        A.GetNumLayers() != 1 || A.GetNumBlocks() != 1 || B.GetNumLayers() != 1 || B.GetNumBlocks() != 1)
    {
        Error = TEXT("Texture source shape differs from the freshly decoded creator image");
        return false;
    }
    for (int32 Mip = 0; Mip < A.GetNumMips(); ++Mip)
    {
        TArray64<uint8> Have, Want;
        if (!A.GetMipData(Have, Mip) || !B.GetMipData(Want, Mip) || Have != Want)
        {
            Error = TEXT("Texture retains data other than the freshly decoded creator image");
            return false;
        }
    }
    FP4CreatorTextureSource Result;
    Result.Verified = true;
    Result.SourceDigest = MoveTemp(Digest);
    Result.Asset = Asset;
    Out = MoveTemp(Result);
    Error.Reset();
    return true;
}

#include "P4TextureBuild.h"
#include "P4TextureContent.h"
#include "P4PackageSave.h"
#include "P4AuthoringRows.h"
#include "P4MapKitCore/texture_pixels.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OpaqueTextureCookTest, "P4MapKit.Native.OpaqueBC3CookPreservesMips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4OpaqueTextureCookTest::RunTest(const FString&)
{
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageName = TEXT("/Game/P4OpaqueTexture_") + Id;
    const FString ReloadName = TEXT("/Temp/P4OpaqueTextureReload_") + Id;
    const FString File = FPackageName::LongPackageNameToFilename(PackageName, TEXT(".uasset"));
    UPackage* Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>(Package, TEXT("Texture"), RF_Public | RF_Standalone));
    TStrongObjectPtr<UTexture2D> Reloaded;
    ON_SCOPE_EXIT
    {
        for (UTexture2D* Made : {Texture.Get(), Reloaded.Get()})
            if (Made)
            {
                Made->ClearAllCachedCookedPlatformData();
                ResetLoaders(Made->GetOutermost());
                Made->GetOutermost()->SetDirtyFlag(false);
                Made->ClearFlags(RF_Public | RF_Standalone);
                Made->MarkPendingKill();
            }
        for (const TCHAR* Suffix : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
            IFileManager::Get().Delete(*FPaths::ChangeExtension(File, Suffix), false, true, true);
    };
    const FColor Colors[] = {FColor::Red, FColor::Green, FColor::Blue, FColor::White};
    TArray64<uint8> Pixels;
    for (int32 Mip = 0, Size = 8; Mip < 4; ++Mip, Size >>= 1)
        for (int32 Pixel = 0; Pixel < Size * Size; ++Pixel)
            Pixels.Append(reinterpret_cast<const uint8*>(&Colors[Mip]), sizeof(FColor));
    Texture->Source.Init(8, 8, 1, 4, TSF_BGRA8, Pixels.GetData());
    Texture->Source.UseHashAsGuid();
    Texture->SRGB = false;
    Texture->MipGenSettings = TMGS_LeaveExistingMips;
    TestTrue(TEXT("Recover the payload's BC3 cook choice without making new mips"),
        P4RecoverOriginalTextureCookFormat(Texture.Get(), TEXT("PF_DXT5")));
    TestTrue(TEXT("BC3 selector survives in the native editor setting"), bool(Texture->bDitherMipMapAlpha));
    const FString State = P4TextureState(Texture.Get());
    const auto SourceExact = [&](UTexture2D* Made)
    {
        int64 At = 0;
        int64 Expected = 0;
        for (int32 Mip = 0, Size = 8; Mip < Made->Source.GetNumMips(); ++Mip, Size >>= 1)
        {
            TArray64<uint8> Read;
            Expected += int64(Size) * Size * sizeof(FColor);
            if (!Made->Source.GetMipData(Read, Mip) || Read.Num() != int64(Size) * Size * sizeof(FColor) ||
                Read.Num() > Pixels.Num() - At ||
                FMemory::Memcmp(Read.GetData(), Pixels.GetData() + At, SIZE_T(Read.Num())))
                return false;
            At += Read.Num();
        }
        return At == Expected;
    };
    auto& Manager = FModuleManager::LoadModuleChecked<ITargetPlatformManagerModule>(TEXT("TargetPlatform"));
    ITargetPlatform* Target = Manager.FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!TestNotNull(TEXT("Existing Windows cook platform"), Target))
        return false;
    const auto CookExact = [&](UTexture2D* Made, TArray<uint8>& Compressed)
    {
        Made->BeginCacheForCookedPlatformData(Target);
        const double Deadline = FPlatformTime::Seconds() + 300;
        while (!Made->IsCachedCookedPlatformDataLoaded(Target) && FPlatformTime::Seconds() < Deadline)
            FPlatformProcess::Sleep(0.002f);
        TMap<FString, FTexturePlatformData*>* Platforms = Made->GetCookedPlatformData();
        FTexturePlatformData* Built = Platforms && Platforms->Num() == 1 &&
                Made->IsCachedCookedPlatformDataLoaded(Target)
            ? Platforms->CreateIterator().Value()
            : nullptr;
        if (!TestTrue(TEXT("Opaque input still cooks as BC3 with every provided mip"),
                Built && Built->PixelFormat == PF_DXT5 && Built->SizeX == 8 && Built->SizeY == 8 &&
                    Built->Mips.Num() == Made->Source.GetNumMips()))
            return false;
        for (int32 Mip = 0, Size = 8; Mip < Made->Source.GetNumMips(); ++Mip, Size >>= 1)
        {
            const FByteBulkData& Bulk = Built->Mips[Mip].BulkData;
            if (!TestTrue(TEXT("Cook has readable bulk"), Bulk.IsBulkDataLoaded() && Bulk.GetBulkDataSize() > 0))
                return false;
            const uint8* Bytes = static_cast<const uint8*>(Bulk.LockReadOnly());
            ON_SCOPE_EXIT
            {
                Bulk.Unlock();
            };
            const uint64 Count = uint64(Bulk.GetBulkDataSize());
            Compressed.Append(Bytes, int32(Count));
            P4MapKit::FReadError Error;
            const bool Exact = P4MapKit::DecodeTextureRows(
                "PF_DXT5", Size, Size, 1, Count, uint64(Size) * Size * 4,
                [&](uint64 At, uint8* Out, SIZE_T N)
                {
                    if (At > Count || N > Count - At)
                        return false;
                    FMemory::Memcpy(Out, Bytes + At, N);
                    return true;
                },
                [&](uint32, uint32, const uint8* Row, SIZE_T N)
                {
                    for (SIZE_T At = 0; At < N; At += 4)
                        if (Row[At] != Colors[Mip].R || Row[At + 1] != Colors[Mip].G || Row[At + 2] != Colors[Mip].B ||
                            Row[At + 3] != 255)
                            return false;
                    return true;
                },
                {}, Error);
            if (!TestTrue(TEXT("Every cooked color and alpha channel equals its provided literal mip"), Exact))
                return false;
        }
        TestTrue(TEXT("Cook leaves all provided source bytes exact"), SourceExact(Made));
        Made->ClearAllCachedCookedPlatformData();
        return true;
    };
    TArray<uint8> First, Second, SavedCook;
    if (!CookExact(Texture.Get(), First) || !CookExact(Texture.Get(), Second))
        return false;
    TestTrue(TEXT("Repeat cook produces byte-identical compressed bulk for the same source/settings"), First == Second);
    TestEqual(TEXT("Repeat cook leaves the native texture state exact"), P4TextureState(Texture.Get()), State);
    FString Error;
    if (!TestTrue(TEXT("Save through native package save preserves exact state"),
            P4SaveAssetPackage(
                Texture.Get(), [&]() { return P4TextureState(Texture.Get()) == State && SourceExact(Texture.Get()); },
                Error)))
    {
        AddError(Error);
        return false;
    }
    UPackage* SavedPackage = LoadPackage(CreatePackage(*ReloadName), *File, LOAD_NoWarn);
    Reloaded.Reset(SavedPackage ? FindObject<UTexture2D>(SavedPackage, TEXT("Texture")) : nullptr);
    if (!TestNotNull(TEXT("Native saved texture reloads"), Reloaded.Get()))
        return false;
    TestTrue(TEXT("Save/reload retains exact provided source bytes and format selector"),
        SourceExact(Reloaded.Get()) && Reloaded->bDitherMipMapAlpha);
    TestEqual(TEXT("Save/reload retains exact texture state"), P4TextureState(Reloaded.Get()), State);
    if (!CookExact(Reloaded.Get(), SavedCook))
        return false;
    TestTrue(TEXT("Reloaded texture repeats the same compressed bulk bytes"), First == SavedCook);
    Texture->MipGenSettings = TMGS_SimpleAverage;
    Texture->bDitherMipMapAlpha = false;
    TestFalse(TEXT("Never enable the selector for generated mips"),
        P4RecoverOriginalTextureCookFormat(Texture.Get(), TEXT("PF_DXT5")));
    Texture->MipGenSettings = TMGS_NoMipmaps;
    TestTrue(TEXT("No-mipmap mode also permits the selector"),
        P4RecoverOriginalTextureCookFormat(Texture.Get(), TEXT("PF_DXT5")));
    Texture->Source.Init(8, 8, 1, 1, TSF_BGRA8, Pixels.GetData());
    Texture->Source.UseHashAsGuid();
    TArray<uint8> NoMipCook, NoMipAgain;
    if (!CookExact(Texture.Get(), NoMipCook) || !CookExact(Texture.Get(), NoMipAgain))
        return false;
    TestTrue(TEXT("NoMipmaps also retains exact source/color/alpha and repeat compressed bytes"),
        NoMipCook == NoMipAgain && SourceExact(Texture.Get()));
    Texture->bDitherMipMapAlpha = false;
    TestFalse(TEXT("BC1 import does not acquire the BC3 selector"),
        P4RecoverOriginalTextureCookFormat(Texture.Get(), TEXT("PF_DXT1")));
    TestFalse(TEXT("Other formats leave the setting unchanged"), bool(Texture->bDitherMipMapAlpha));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GeyserTextureCookTest, "P4MapKit.Native.GeyserTextureCookLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GeyserTextureCookTest::RunTest(const FString&)
{
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TEXTURE_GAME"));
    if (!TestTrue(TEXT("Use an existing retained Geyser texture source"),
            Header.EndsWith(TEXT("T_stg_GeyserStone_GY01_M.uasset")) && FPaths::FileExists(Header)))
        return false;
    TSharedPtr<FJsonObject> Report;
    if (!TestTrue(TEXT("Build the existing game texture without publishing Content"),
            FJsonSerializer::Deserialize(
                TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeBuildOriginalTexture(Header)), Report) &&
                Report.IsValid() && Report->GetBoolField(TEXT("valid"))))
        return false;
    for (const TCHAR* Field : {TEXT("source_equal"), TEXT("properties_equal"), TEXT("layout_equal"), TEXT("equal"),
             TEXT("built_bytes_compared")})
        TestTrue(Field, Report->GetBoolField(Field));
    TestEqual(TEXT("Reconstructed cook retains game format"), Report->GetStringField(TEXT("built_format")),
        FString(TEXT("PF_DXT5")));
    TestEqual(TEXT("Reconstructed cook retains game mip count"), Report->GetIntegerField(TEXT("built_mips")), 7);
    AddInfo(FString::Printf(
        TEXT(
            "Compressed bytes identical: %s; measured recompression pixel delta: %d/255 (separate from source/layout equality)"),
        Report->GetBoolField(TEXT("built_bytes_equal")) ? TEXT("yes") : TEXT("no"),
        Report->GetIntegerField(TEXT("largest_built_difference"))));
    return !HasAnyErrors();
}
#endif

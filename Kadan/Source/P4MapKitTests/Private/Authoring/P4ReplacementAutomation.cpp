#if P4_MAPKIT_TESTS
#include "P4Replacement.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorTextureSourceTest, "P4MapKit.Export.CreatorTextureSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorTextureSourceTest::RunTest(const FString&)
{
    const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit") /
        (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".bmp"));
    struct FCleanup
    {
        FString File;
        ~FCleanup() { IFileManager::Get().Delete(*File); }
    } Cleanup{File};
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    // A complete creator image, decoded afresh by the real Unreal image factory.
    TArray<uint8> Image;
    Image.SetNumZeroed(70);
    Image[0] = 'B';
    Image[1] = 'M';
    Image[2] = 70;
    Image[10] = 54;
    Image[14] = 40;
    Image[18] = 2;
    Image[22] = 2;
    Image[26] = 1;
    Image[28] = 32;
    Image[34] = 16;
    TArray<uint8> Pixels;
    Pixels.SetNumZeroed(16);
    for (int32 I = 0; I < 4; ++I)
    {
        Image[54 + I * 4 + 1] = 255;
        Image[54 + I * 4 + 3] = 255;
        Pixels[I * 4 + 1] = 255;
        Pixels[I * 4 + 3] = 255;
    }
    TestTrue(TEXT("Actual creator image written"), FFileHelper::SaveArrayToFile(Image, *File));
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/CreatorSourceTest/") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>(Package.Get(), TEXT("Texture"), RF_Transient));
    Texture->Source.Init(2, 2, 1, 1, TSF_BGRA8, Pixels.GetData());
    FP4CreatorTextureSource Proof;
    FString Error;
    TestTrue(TEXT("Fresh source decoding equals every creator source pixel"),
        P4VerifyCreatorTextureSource(Texture.Get(), File, Proof, Error));
    TestTrue(TEXT("Source evidence established"), Proof.IsVerified());
    TestEqual(TEXT("Actual source bytes digest retained"), Proof.Digest().Len(), 64);
    TestTrue(TEXT("Evidence identifies asset"), Proof.Texture() == Texture.Get());
    Pixels[0] = 123;
    Texture->Source.Init(2, 2, 1, 1, TSF_BGRA8, Pixels.GetData());
    TestFalse(TEXT("Mixed or edited source data is refused"),
        P4VerifyCreatorTextureSource(Texture.Get(), File, Proof, Error));
    TestFalse(TEXT("Embedded native asset is not creator source"),
        P4VerifyCreatorTextureSource(Texture.Get(), File + TEXT(".uasset"), Proof, Error));
    TestFalse(TEXT("Missing creator file refused"),
        P4VerifyCreatorTextureSource(Texture.Get(), File + TEXT(".bmp"), Proof, Error));
    // Source evidence deliberately cannot admit or manufacture a cooked/container proof.
    return true;
}
#endif

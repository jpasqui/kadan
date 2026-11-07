#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "P4MapKitCore/quality.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Async/Async.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"
#include "P4NativeHash.h"
#include "Misc/PackageName.h"
#include "AssetRegistryModule.h"
#include "HAL/ThreadSafeBool.h"
#include "P4OriginalDecode.h"
#include "HAL/ThreadSafeCounter.h"
#include "P4NativeValueStaging.h"
#include "HAL/PlatformFileManager.h"
#include "P4MapKitCore/bulk_data.h"
#include "Serialization/BulkData.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "P4MapKitCore/texture_payload.h"
#include "Engine/Texture2D.h"
#include "P4TextureAuthoring.h"
#include "P4TextureProperties.h"
#include "P4StaticMeshStaging.h"
#include "P4TextureContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ByteBulkArchiveTest, "P4MapKit.Quality.ByteBulkArchive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ByteBulkArchiveTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4MapKit;
    // Generate archives with the installed engine's actual serializer, rather
    // than using the reader or its hand-built unit fixture as the oracle.
    const uint8 Expected[] = {0x18, 0xa7, 0xff};
    for (const uint32 Flags : {uint32(0), uint32(BULKDATA_ForceInlinePayload),
             uint32(BULKDATA_Size64Bit | BULKDATA_SingleUse), uint32(BULKDATA_Unused)})
    {
        FByteBulkData EngineSource;
        EngineSource.Lock(LOCK_READ_WRITE);
        void* Data = EngineSource.Realloc(UE_ARRAY_COUNT(Expected));
        FMemory::Memcpy(Data, Expected, sizeof(Expected));
        EngineSource.Unlock();
        EngineSource.SetBulkDataFlags(Flags);
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes, true);
        EngineSource.Serialize(Writer, nullptr);
        TestFalse(TEXT("Engine serialization succeeds"), Writer.IsError());
        std::vector<std::uint8_t> Input(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
        FByteReader Reader(Input);
        FByteBulkContext Context;
        Context.FileSizes[0] = Bytes.Num();
        FByteBulkRange Range;
        if (!TestTrue(
                TEXT("Native reader accepts actual engine byte-bulk archive"), ReadByteBulk(Reader, Context, Range)))
            return false;
        TestTrue(TEXT("Actual archive flags retained"), Range.Header.Flags == Flags);
        TestTrue(TEXT("Actual archive count retained"), Range.Header.Count == UE_ARRAY_COUNT(Expected));
        TestTrue(TEXT("Native reader consumes complete actual archive"), Reader.Tell() == Input.size());
        const bool Unused = (Flags & BULKDATA_Unused) != 0;
        TestTrue(TEXT("Unavailable source bytes retain unused state"), (Range.Source == EBulkSource::None) == Unused);
        TestTrue(TEXT("Actual archive length retained"), Range.Size == (Unused ? 0 : sizeof(Expected)));
        FByteBulkData EngineReload;
        FMemoryReader Reload(Bytes, true);
        EngineReload.Serialize(Reload, nullptr);
        TestFalse(TEXT("Independent engine reload succeeds"), Reload.IsError());
        TestTrue(TEXT("Engine and native consumed positions agree"), uint64(Reload.Tell()) == Reader.Tell());
        if (!Unused)
        {
            const void* Reloaded = EngineReload.LockReadOnly();
            TestTrue(
                TEXT("Independent engine payload exact"), FMemory::Memcmp(Reloaded, Expected, sizeof(Expected)) == 0);
            EngineReload.Unlock();
            TArray<uint8> Copied;
            FReadError Error;
            const bool Streamed = StreamByteBulk(
                Range,
                [&](EBulkSource Source, std::uint64_t At, std::uint8_t* Target, std::size_t Count)
                {
                    if (Source != EBulkSource::Body || At > Input.size() || Count > Input.size() - At)
                        return false;
                    FMemory::Memcpy(Target, Input.data() + std::size_t(At), Count);
                    return true;
                },
                [&](const std::uint8_t* Chunk, std::size_t Count)
                {
                    Copied.Append(Chunk, int32(Count));
                    return true;
                },
                {}, Error);
            TestTrue(TEXT("Native streaming succeeds on engine bytes"), Streamed);
            TestTrue(TEXT("Native stream equals independent engine payload"),
                Copied.Num() == int32(sizeof(Expected)) &&
                    FMemory::Memcmp(Copied.GetData(), Expected, sizeof(Expected)) == 0);
        }
    }
    // No original Content, asset admission, decoder fallback or editor actor is
    // introduced by testing the low-level archive reader.
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePayloadArchiveTest, "P4MapKit.Quality.TexturePayloadArchive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePayloadArchiveTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4MapKit;
    for (const auto Shape : {ETextureShape::Plane, ETextureShape::Cube, ETextureShape::Array, ETextureShape::Volume})
    {
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes, true);
        int64 Skip = 0;
        Writer << Skip;
        int32 Width = 4, Height = 4;
        uint32 Packed = Shape == ETextureShape::Cube ? 0x80000006u : Shape == ETextureShape::Plane ? 1u : 2u;
        Writer << Width << Height << Packed;
        FString Format(TEXT("PF_B8G8R8A8"));
        Writer << Format;
        int32 First = 0, Count = 3;
        Writer << First << Count;
        TArray<TArray<uint8>> OriginalMips;
        TArray<int64> MipPositions;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            int32 X = 4 >> Index, Y = X;
            int32 Z = Shape == ETextureShape::Volume ? FMath::Max(1, 2 >> Index)
                : Shape == ETextureShape::Array      ? 2
                                                     : 1;
            TArray<uint8> Raw;
            Raw.SetNumUninitialized(X * Y * Z * 4 * (Shape == ETextureShape::Cube ? 6 : 1));
            for (int32 At = 0; At < Raw.Num(); ++At)
                Raw[At] = uint8((At + Index) % 251);
            MipPositions.Add(Writer.Tell());
            bool Cooked = true;
            Writer << Cooked;
            FByteBulkData Bulk;
            Bulk.Lock(LOCK_READ_WRITE);
            FMemory::Memcpy(Bulk.Realloc(Raw.Num()), Raw.GetData(), Raw.Num());
            Bulk.Unlock();
            Bulk.SetBulkDataFlags(BULKDATA_ForceInlinePayload);
            Bulk.Serialize(Writer, nullptr);
            Writer << X << Y << Z;
            OriginalMips.Add(MoveTemp(Raw));
        }
        bool Virtual = false;
        Writer << Virtual;
        Skip = Writer.Tell();
        Writer.Seek(0);
        Writer << Skip;
        std::vector<std::uint8_t> Input(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
        FByteReader Reader(Input);
        FByteBulkContext Context;
        Context.FileSizes[0] = Bytes.Num();
        FTexturePlatformPayload Decoded;
        if (!TestTrue(TEXT("Native cooked platform metadata parses"),
                ReadTexturePlatformPayload(
                    Reader, "PF_B8G8R8A8", Shape, {"PF_B8G8R8A8", 1, 1, 1, 4}, Context, Decoded)))
            return false;
        TestTrue(TEXT("Native consumes complete platform archive"), Reader.Tell() == Input.size());
        TestTrue(TEXT("Native preserves declared platform dimensions/slices"),
            Decoded.Width == uint32(Width) && Decoded.Height == uint32(Height) && Decoded.Packed == Packed &&
                Decoded.Mips.size() == 3);
        // SerializeCooked is not exported by the installed engine. Its actual
        // exported mip reader provides the independent byte/dimension oracle;
        // no replacement engine platform serializer is invented for this test.
        for (int32 Index = 0; Index < OriginalMips.Num(); ++Index)
        {
            FMemoryReader Reload(Bytes, true);
            Reload.Seek(MipPositions[Index]);
            FTexture2DMipMap Actual;
            Actual.Serialize(Reload, nullptr, Index);
            const auto& Native = Decoded.Mips[std::size_t(Index)];
            TestFalse(TEXT("Installed engine independently loads cooked mip archive"), Reload.IsError());
            TestTrue(TEXT("Every native mip dimension/count agrees with engine"),
                Native.Width == uint32(Actual.SizeX) && Native.Height == uint32(Actual.SizeY) &&
                    Native.Depth == uint32(Actual.SizeZ) &&
                    Native.Bulk.Size == uint64(Actual.BulkData.GetBulkDataSize()));
            const void* Data = Actual.BulkData.LockReadOnly();
            TestTrue(TEXT("Every original mip byte survives independent engine load"),
                FMemory::Memcmp(Data, OriginalMips[Index].GetData(), OriginalMips[Index].Num()) == 0);
            Actual.BulkData.Unlock();
        }
    }
    FString Digest;
    const uint8 ABC[] = {'a', 'b', 'c'};
    TestTrue(TEXT("Chunk stream hashes exact data"),
        P4HashByteStream([&](const TFunction<bool(const uint8*, int32)>& Update)
            { return Update(ABC, 1) && Update(ABC + 1, 2); }, Digest));
    TestEqual(TEXT("Chunk hash independently known"), Digest,
        FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
    TestFalse(TEXT("Failed stream returns no partial hash"),
        P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Update)
            {
                Update(ABC, 1);
                return false;
            },
            Digest));
    TestTrue(TEXT("Failed stream hash empty"), Digest.IsEmpty());
    TestFalse(TEXT("Ignored invalid chunk still refuses"),
        P4HashByteStream(
            [](const TFunction<bool(const uint8*, int32)>& Update)
            {
                Update(nullptr, 1);
                return true;
            },
            Digest));
    TestTrue(TEXT("Invalid stream hash empty"), Digest.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceTest, "P4MapKit.Quality.TextureSourceConstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FString Failure;
    uint32 Checks = 0;
    const bool Passed = P4TestPrivateTextureSource(Failure, Checks);
    TestTrue(*(TEXT("complete private native texture source: ") + Failure), Passed);
    TestTrue(TEXT("texture source fixtures exercised"), Checks > 50);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertiesTest, "P4MapKit.Quality.TextureProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertiesTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    return P4TestTextureProperties(*this);
}
// An array written from values holds exactly the rows given. A member a row leaves out is that
// row's default, not what an earlier row in the same place held.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalTextureBuiltTest, "P4MapKit.Quality.OriginalTextureBuiltEqualsGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalTextureBuiltTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString Header = FPaths::ConvertRelativePathToFull(Root / TEXT("../..")) /
        TEXT(
            "private/graphics-audit/sample-assets/Carrot4/Content/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D.uasset");
    if (!TestTrue(TEXT("the game's sample texture and its bulk file are on disk"),
            FPaths::FileExists(Header) && FPaths::FileExists(FPaths::ChangeExtension(Header, TEXT("uexp"))) &&
                FPaths::FileExists(FPaths::ChangeExtension(Header, TEXT("ubulk")))))
        return false;
    TSharedPtr<FJsonObject> Report;
    if (!TestTrue(TEXT("the build reports"),
            FJsonSerializer::Deserialize(
                TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeBuildOriginalTexture(Header)), Report) &&
                Report.IsValid()))
        return false;
    if (!TestTrue(TEXT("the texture is made and built"), Report->GetBoolField(TEXT("valid"))))
    {
        AddError(Report->GetStringField(TEXT("error")));
        return false;
    }
    TestEqual(TEXT("class"), Report->GetStringField(TEXT("class")), FString(TEXT("/Script/Engine.Texture2D")));
    TestEqual(TEXT("package"), Report->GetStringField(TEXT("package")),
        FString(TEXT("/Game/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D")));
    TestEqual(TEXT("the game's pixel format"), Report->GetStringField(TEXT("game_format")), FString(TEXT("PF_DXT5")));
    TestEqual(TEXT("the engine compresses to the same format"), Report->GetStringField(TEXT("built_format")),
        FString(TEXT("PF_DXT5")));
    TestEqual(TEXT("the game's texture group is kept by name"), Report->GetStringField(TEXT("game_texture_group")),
        FString(TEXT("TEXTUREGROUP_Characters_ColorMap")));
    for (const TCHAR* Field : {TEXT("width"), TEXT("height"), TEXT("built_width"), TEXT("built_height")})
        TestEqual(Field, Report->GetIntegerField(Field), 512);
    for (const TCHAR* Field : {TEXT("mips"), TEXT("built_mips")})
        TestEqual(Field, Report->GetIntegerField(Field), 10);
    for (const TCHAR* Field : {TEXT("slices"), TEXT("built_slices")})
        TestEqual(Field, Report->GetIntegerField(Field), 1);
    for (const TCHAR* Field : {TEXT("source_equal"), TEXT("properties_equal"), TEXT("layout_equal"), TEXT("equal")})
        TestTrue(FString(TEXT("the texture equals the game's: ")) + Field, Report->GetBoolField(Field));
    // The engine compresses the decoded pixels again. How close that lands is measured, and recorded.
    const int32 Difference = Report->GetIntegerField(TEXT("largest_built_difference"));
    TestTrue(
        TEXT("the engine's compressed pixels were compared with the game's"), Difference >= 0 && Difference <= 255);
    AddInfo(FString::Printf(
        TEXT("largest difference of a compressed pixel channel from the game's: %d of 255"), Difference));
    return true;
}
// A game texture kept in Content: made at its game path with a record of what it was imported as,
// saved, read back from the saved file, left alone by a second import, and known as edited once a
// setting or a pixel changes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalTextureKeptTest, "P4MapKit.Quality.OriginalTextureKeptInContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalTextureKeptTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString Header = FPaths::ConvertRelativePathToFull(Root / TEXT("../..")) /
        TEXT(
            "private/graphics-audit/sample-assets/Carrot4/Content/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D.uasset");
    const FString Package = TEXT("/Game/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D"),
                  Name = TEXT("T_chr_Happy_00_T00_D");
    const FString Reload = TEXT("/Temp/P4OriginalTextureKeptReload");
    FString File;
    if (!TestTrue(TEXT("the texture's place in Content has a file name"),
            FPackageName::TryConvertLongPackageNameToFilename(Package, File, TEXT(".uasset"))))
        return false;
    File = FPaths::ConvertRelativePathToFull(File);
    if (!TestTrue(TEXT("the game's sample texture is on disk"), FPaths::FileExists(Header)) ||
        !TestFalse(TEXT("nothing is at the texture's place before the test"),
            FPaths::FileExists(File) || FindPackage(nullptr, *Package) != nullptr))
        return false;
    ON_SCOPE_EXIT
    {
        // Everything the test made goes: both loaded copies, the saved file, and the folders if empty.
        for (const FString& Loaded : {Package, Reload})
            if (UPackage* Made = FindPackage(nullptr, *Loaded))
            {
                if (UObject* Asset = FindObject<UObject>(Made, *Name))
                {
                    if (Loaded == Package)
                        FAssetRegistryModule::AssetDeleted(Asset);
                    Asset->ClearFlags(RF_Public | RF_Standalone);
                    Asset->MarkPendingKill();
                }
                ResetLoaders(Made);
                Made->SetDirtyFlag(false);
            }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        IFileManager::Get().Delete(*File, false, true, true);
        const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        for (FString Folder = FPaths::GetPath(File);
            FPaths::IsUnderDirectory(Folder, Content) && !FPaths::IsSamePath(Folder, Content);
            Folder = FPaths::GetPath(Folder))
            if (!IFileManager::Get().DeleteDirectory(*Folder, false, false))
                break;
    };
    const auto Json = [](const FString& Reply)
    {
        TSharedPtr<FJsonObject> Out;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Reply), Out);
        return Out.IsValid() ? Out : MakeShared<FJsonObject>();
    };

    const TSharedPtr<FJsonObject> First = Json(UP4AuthoringTools::NativeImportOriginalTexture(Header, FString(), true));
    if (!TestTrue(
            TEXT("the texture is imported"), First->HasField(TEXT("valid")) && First->GetBoolField(TEXT("valid"))))
    {
        AddError(First->HasField(TEXT("error")) ? First->GetStringField(TEXT("error")) : FString(TEXT("no report")));
        return false;
    }
    TestTrue(TEXT("it was made"), First->GetBoolField(TEXT("created")));
    TestFalse(TEXT("nothing was replaced"), First->GetBoolField(TEXT("replaced")));
    TestTrue(TEXT("it was saved"), First->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("it is at its game path"), First->GetStringField(TEXT("asset")), Package + TEXT(".") + Name);
    TestEqual(TEXT("the game's pixel format is recorded"), First->GetStringField(TEXT("game_format")),
        FString(TEXT("PF_DXT5")));
    TestEqual(TEXT("the game's texture group is recorded"), First->GetStringField(TEXT("game_texture_group")),
        FString(TEXT("TEXTUREGROUP_Characters_ColorMap")));
    TestEqual(TEXT("no larger mips were left out by the game's cook"), First->GetIntegerField(TEXT("first_mip")), 0);
    const FString State = First->GetStringField(TEXT("state"));
    TestEqual(TEXT("the state is a digest"), State.Len(), 64);
    TestEqual(TEXT("the recorded state is the texture's state"), First->GetStringField(TEXT("imported_state")), State);

    UTexture2D* Texture = FindObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name));
    if (!TestNotNull(TEXT("an ordinary texture is in Content"), Texture))
        return false;
    TestTrue(TEXT("it is an asset others can refer to"),
        Texture->HasAllFlags(RF_Public | RF_Standalone) && Texture->IsAsset());
    TestEqual(TEXT("source width"), Texture->Source.GetSizeX(), 512);
    TestEqual(TEXT("source height"), Texture->Source.GetSizeY(), 512);
    TestEqual(TEXT("the game's ten mips are the source"), Texture->Source.GetNumMips(), 10);
    TestEqual(TEXT("the group is the stock one at the game's value"), int32(Texture->LODGroup),
        int32(TEXTUREGROUP_Project01));
    TestTrue(TEXT("the saved file is there"), FPaths::FileExists(File));
    TestFalse(TEXT("the package is not left dirty"), Texture->GetOutermost()->IsDirty());
    TestEqual(TEXT("the texture's state after the save"), P4TextureState(Texture), State);

    const TSharedPtr<FJsonObject> Status = Json(UP4AuthoringTools::NativeOriginalImportDiagnostic(Texture));
    TestTrue(TEXT("it is known as imported from the game"),
        Status->GetBoolField(TEXT("valid")) && Status->GetBoolField(TEXT("original")));
    TestTrue(TEXT("it is known as untouched"), Status->GetBoolField(TEXT("unchanged")));

    // The saved file, read into a second package, holds the same texture and the same record.
    UPackage* Second = LoadPackage(CreatePackage(*Reload), *File, LOAD_NoWarn);
    UTexture2D* Saved = Second ? FindObject<UTexture2D>(Second, *Name) : nullptr;
    if (TestNotNull(TEXT("the saved file loads"), Saved))
    {
        TestTrue(TEXT("the saved texture is another object"), Saved != Texture);
        TestEqual(TEXT("the saved texture's state"), P4TextureState(Saved), State);
        TestTrue(TEXT("the saved file holds the record"),
            Second->GetMetaData()->GetValue(Saved, TEXT("P4NativeTextureBaseline")).Contains(State));
    }

    const TSharedPtr<FJsonObject> Again = Json(UP4AuthoringTools::NativeImportOriginalTexture(Header, FString(), true));
    TestTrue(TEXT("a second import of the same file succeeds"), Again->GetBoolField(TEXT("valid")));
    TestFalse(TEXT("and makes nothing"), Again->GetBoolField(TEXT("created")));
    TestFalse(TEXT("and replaces nothing"), Again->GetBoolField(TEXT("replaced")));
    TestFalse(TEXT("and saves nothing"), Again->GetBoolField(TEXT("saved")));
    TestTrue(
        TEXT("and leaves the same object"), FindObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name)) == Texture);

    // A changed setting.
    Texture->SRGB = !Texture->SRGB;
    Texture->MarkPackageDirty();
    TestFalse(TEXT("a changed setting is seen"),
        Json(UP4AuthoringTools::NativeOriginalImportDiagnostic(Texture))->GetBoolField(TEXT("unchanged")));
    const TSharedPtr<FJsonObject> Refused = Json(
        UP4AuthoringTools::NativeImportOriginalTexture(Header, FString(), true));
    TestFalse(TEXT("an edited texture is not imported over"), Refused->GetBoolField(TEXT("valid")));
    TestEqual(TEXT("for that reason"), Refused->GetStringField(TEXT("error")),
        FString(TEXT("texture-import-would-replace-an-edited-texture")));
    TestTrue(TEXT("and the edit is still there"), !Saved || Texture->SRGB != Saved->SRGB);
    Texture->SRGB = !Texture->SRGB;
    TestTrue(TEXT("the setting put back makes it untouched again"),
        Json(UP4AuthoringTools::NativeOriginalImportDiagnostic(Texture))->GetBoolField(TEXT("unchanged")));

    // A changed pixel, in the smallest mip.
    if (uint8* Pixels = Texture->Source.LockMip(9))
    {
        Pixels[0] ^= 0x40;
        Texture->Source.UnlockMip(9);
        TestFalse(TEXT("a changed pixel is seen"),
            Json(UP4AuthoringTools::NativeOriginalImportDiagnostic(Texture))->GetBoolField(TEXT("unchanged")));
    }
    else
        AddError(TEXT("the smallest mip of the source could not be locked"));

    // A texture the kit did not import.
    UTexture2D* Plain = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
    const TSharedPtr<FJsonObject> Other = Json(UP4AuthoringTools::NativeOriginalImportDiagnostic(Plain));
    TestTrue(TEXT("a texture of the user's own is answered"), Other->GetBoolField(TEXT("valid")));
    TestFalse(TEXT("and is not taken for a game texture"), Other->GetBoolField(TEXT("original")));
    TestFalse(TEXT("no texture is refused"),
        Json(UP4AuthoringTools::NativeOriginalImportDiagnostic(nullptr))->GetBoolField(TEXT("valid")));
    return true;
}
// A material made from a node graph in the kit's JSON: every node an expression, links through the
// expression's own pin for their channels or through a mask, the graph read back, built again from what
// was read, saved, and read from the saved file. And what is refused.
#endif

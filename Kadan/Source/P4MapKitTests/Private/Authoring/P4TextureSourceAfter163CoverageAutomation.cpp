// PRIVATE draft bound to immutable native163; root owns promotion/build/editor.
#include "P4TextureAuthoring.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4TextureSourceAfter163
{
using namespace P4MapKit;
// The source producer is a genuine native TextureSource populated through
// its public editor API. No PlatformData, renderer or cooked-asset admission.
struct FSourceCustodyFixture
{
    TStrongObjectPtr<UTexture2D> Producer;
    FTexturePlatformPayload Platform;
    TArray<TArray64<uint8>> MipBytes;
    std::vector<uint8> HeldBytes;
    uint32 RowReadsPerPass = 0;
    explicit FSourceCustodyFixture(ETextureSourceFormat Format = TSF_BGRA8, const char* PixelFormat = "PF_B8G8R8A8")
        : Producer(NewObject<UTexture2D>(CreatePackage(*FString::Printf(TEXT("/Game/P4TextureSourceAfter163_%s"),
                                             *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
              NAME_None, RF_Public | RF_Standalone))
    {
        const int32 BytesPerPixel = FTextureSource::GetBytesPerPixel(Format);
        Producer->Source.Init(4, 2, 1, 3, Format);
        for (int32 Mip = 0, Width = 4, Height = 2; Mip < 3;
            ++Mip, Width = FMath::Max(1, Width >> 1), Height = FMath::Max(1, Height >> 1))
        {
            uint8* Data = Producer->Source.LockMip(Mip);
            if (Data)
            {
                // Each channel, pixel and mip differs; includes zero alpha.
                for (int32 At = 0; At < Width * Height * BytesPerPixel; ++At)
                    Data[At] = uint8((At * 19 + Mip * 67) & 255);
                Producer->Source.UnlockMip(Mip);
            }
            TArray64<uint8> Bytes;
            if (Producer->Source.GetMipData(Bytes, Mip))
                MipBytes.Add(MoveTemp(Bytes));
            RowReadsPerPass += uint32(Height);
        }
        Platform.Format = PixelFormat;
        Platform.Width = 4;
        Platform.Height = 2;
        Platform.Slices = 1;
        Platform.FirstMip = 3;
        Producer->SRGB = false;
        Producer->Filter = TF_Nearest;
        Producer->LODBias = 3;
        Producer->AddressX = TA_Clamp;
        Producer->AddressY = TA_Mirror;
        // Prefix and inter-mip guards exercise absolute source offsets.
        HeldBytes.assign(9, 0xd7);
        for (int32 Mip = 0, Width = 4, Height = 2; Mip < MipBytes.Num();
            ++Mip, Width = FMath::Max(1, Width >> 1), Height = FMath::Max(1, Height >> 1))
        {
            FTextureMipPayload Item;
            Item.LogicalWidth = uint32(Width);
            Item.LogicalHeight = uint32(Height);
            Item.LogicalDepth = 1;
            Item.Bulk.Source = EBulkSource::Body;
            Item.Bulk.Offset = HeldBytes.size();
            Item.Bulk.Size = uint64(MipBytes[Mip].Num());
            HeldBytes.insert(HeldBytes.end(), MipBytes[Mip].GetData(), MipBytes[Mip].GetData() + MipBytes[Mip].Num());
            HeldBytes.insert(HeldBytes.end(), 5, 0x6b);
            Platform.Mips.push_back(Item);
        }
        Producer->GetOutermost()->SetDirtyFlag(false);
    }
    ~FSourceCustodyFixture()
    {
        Producer->Source.Init(0, 0, 0, 0, TSF_Invalid);
        Producer->ClearFlags(RF_Public | RF_Standalone);
        Producer->MarkPendingKill();
    }
    bool Complete() const { return MipBytes.Num() == 3 && Platform.Mips.size() == 3 && RowReadsPerPass == 4; }
    FP4TextureSourceRequest Request()
    {
        FP4TextureSourceRequest Result;
        Result.NativeClass = UTexture2D::StaticClass()->GetPathName();
        Result.Platform = &Platform;
        Result.MaxDecodedBytes = 1024;
        Result.ReadAt = [this](EBulkSource Source, uint64 At, uint8* Dest, SIZE_T Count)
        { return Read(Source, At, Dest, Count); };
        return Result;
    }
    bool Read(EBulkSource Source, uint64 At, uint8* Dest, SIZE_T Count) const
    {
        if (Source != EBulkSource::Body || At > HeldBytes.size() || Count > HeldBytes.size() - SIZE_T(At))
            return false;
        FMemory::Memcpy(Dest, HeldBytes.data() + SIZE_T(At), Count);
        return true;
    }
    bool SamePixels()
    {
        if (Producer->Source.GetSizeX() != 4 || Producer->Source.GetSizeY() != 2 ||
            Producer->Source.GetNumMips() != 3 || Producer->Source.GetNumSlices() != 1)
            return false;
        for (int32 Mip = 0; Mip < MipBytes.Num(); ++Mip)
        {
            TArray64<uint8> Current;
            if (!Producer->Source.GetMipData(Current, Mip) || Current != MipBytes[Mip])
                return false;
        }
        return true;
    }
};
TArray<uint8> NativeSnapshot(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
struct FPreservationOracle
{
    FSourceCustodyFixture& Source;
    std::vector<uint8> Bytes;
    TArray<uint8> NativeBytes;
    FGuid SourceId;
    EObjectFlags Flags;
    UObject* Outer;
    bool Dirty;
    int32 Pre = 0, Post = 0;
    FDelegateHandle PreHandle, PostHandle;
    explicit FPreservationOracle(FSourceCustodyFixture& InSource)
        : Source(InSource), Bytes(InSource.HeldBytes), NativeBytes(NativeSnapshot(InSource.Producer.Get())),
          SourceId(InSource.Producer->Source.GetId()), Flags(InSource.Producer->GetFlags()),
          Outer(InSource.Producer->GetOuter()), Dirty(InSource.Producer->GetOutermost()->IsDirty())
    {
        PreHandle = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Target, const FEditPropertyChain&)
            {
                if (Target == Source.Producer.Get())
                    ++Pre;
            });
        PostHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Target, FPropertyChangedEvent&)
            {
                if (Target == Source.Producer.Get())
                    ++Post;
            });
    }
    ~FPreservationOracle()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreHandle);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PostHandle);
    }
    void Check(FAutomationTestBase& Test)
    {
        Test.TestTrue(
            TEXT("Held original bytes, prefix and every inter-mip guard are exact"), Source.HeldBytes == Bytes);
        Test.TestTrue(TEXT("Native producer serialization and source GUID remain exact"),
            NativeSnapshot(Source.Producer.Get()) == NativeBytes && Source.Producer->Source.GetId() == SourceId);
        Test.TestTrue(TEXT("Independent public native mip readback remains exact"), Source.SamePixels());
        Test.TestTrue(TEXT("Original flags, package, dirty state and unbuilt resource remain unchanged"),
            Source.Producer->GetFlags() == Flags && Source.Producer->GetOuter() == Outer &&
                Source.Producer->GetOutermost()->IsDirty() == Dirty && Source.Producer->PlatformData == nullptr &&
                Source.Producer->Resource == nullptr);
        Test.TestTrue(TEXT("Independent native sRGB/filter/address/bias settings and callbacks are preserved"),
            !Source.Producer->SRGB && Source.Producer->Filter == TF_Nearest && Source.Producer->LODBias == 3 &&
                Source.Producer->AddressX == TA_Clamp && Source.Producer->AddressY == TA_Mirror && Pre == 0 &&
                Post == 0);
    }
};
FP4TextureSourceResult Previous(FSourceCustodyFixture& Source)
{
    FP4TextureSourceResult Result;
    Result.Texture = Source.Producer.Get();
    Result.DecodedBytes = 97;
    Result.SurvivingFirstMip = 17;
    Result.SourceReadbackVerified = true;
    return Result;
}
void NoOutput(FAutomationTestBase& Test, const FP4TextureSourceResult& Result)
{
    Test.TestTrue(TEXT("Refusal clears previous texture, byte count, mip identity and verification"),
        Result.Texture == nullptr && Result.DecodedBytes == 0 && Result.SurvivingFirstMip == 0 &&
            !Result.SourceReadbackVerified);
}
void Refuse(
    FAutomationTestBase& Test, FSourceCustodyFixture& Source, const FP4TextureSourceRequest& Request, const char* Code)
{
    FPreservationOracle Oracle(Source);
    auto Result = Previous(Source);
    FReadError Error;
    const bool Accepted = P4BuildPrivateTextureSource(Request, Result, Error);
    Test.TestFalse(FString::Printf(TEXT("Source refuses precisely: %s"), UTF8_TO_TCHAR(Code)), Accepted);
    Test.TestTrue(TEXT("Exact source refusal code is retained"), Error.Code == Code);
    NoOutput(Test, Result);
    Oracle.Check(Test);
}
void ReleaseCandidate(FP4TextureSourceResult& Result)
{
    if (Result.Texture)
    {
        Result.Texture->Source.Init(0, 0, 0, 0, TSF_Invalid);
        Result.Texture->MarkPendingKill();
    }
    Result = {};
}
} //namespace P4TextureSourceAfter163

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceSidecarCustodyDraft,
    "P4MapKit.Quality.TextureSourceSidecarCustodyDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceSidecarCustodyDraft::RunTest(const FString&)
{
    using namespace P4TextureSourceAfter163;
    FSourceCustodyFixture Source;
    if (!TestTrue(TEXT("Public engine source producer supplies all three exact rectangular mips"), Source.Complete()))
        return false;
    FPreservationOracle Oracle(Source);
    // Exact enum sidecars, same immutable held pixels; no filesystem dependency.
    for (EBulkSource Kind : {EBulkSource::Regular, EBulkSource::Optional, EBulkSource::Mapped})
    {
        auto Platform = Source.Platform;
        for (auto& Mip : Platform.Mips)
            Mip.Bulk.Source = Kind;
        auto Request = Source.Request();
        Request.Platform = &Platform;
        uint32 Reads = 0;
        bool RangesExact = true;
        Request.ReadAt = [&](EBulkSource Actual, uint64 At, uint8* Dest, SIZE_T Count)
        {
            ++Reads;
            bool InMip = false;
            for (const auto& Mip : Platform.Mips)
                if (At >= Mip.Bulk.Offset && At <= Mip.Bulk.Offset + Mip.Bulk.Size &&
                    Count <= Mip.Bulk.Offset + Mip.Bulk.Size - At)
                    InMip = true;
            RangesExact = RangesExact && Actual == Kind && InMip;
            return RangesExact && Source.Read(EBulkSource::Body, At, Dest, Count);
        };
        auto Result = Previous(Source);
        FReadError Error;
        if (!TestTrue(TEXT("Genuine surviving source mips construct from each bounded sidecar"),
                P4BuildPrivateTextureSource(Request, Result, Error)))
            return false;
        TStrongObjectPtr<UTexture> Hold(Result.Texture);
        ON_SCOPE_EXIT
        {
            ReleaseCandidate(Result);
        };
        TestTrue(TEXT("Reader uses exact sidecar identity and only qualified mip ranges twice"),
            RangesExact && Reads == 2 * Source.RowReadsPerPass);
        TestTrue(TEXT("Success replaces prior output with exact private class and surviving first mip"),
            Result.Texture != Source.Producer.Get() && Result.Texture->GetClass() == UTexture2D::StaticClass() &&
                Result.Texture->GetOutermost() == GetTransientPackage() && Result.Texture->HasAnyFlags(RF_Transient) &&
                Result.SourceReadbackVerified && Result.SurvivingFirstMip == 3 && Result.DecodedBytes == 44);
        TestTrue(TEXT("Native source stores rectangular metadata and ordinary BGRA format"),
            Result.Texture->Source.GetFormat() == TSF_BGRA8 && Result.Texture->Source.GetSizeX() == 4 &&
                Result.Texture->Source.GetSizeY() == 2 && Result.Texture->Source.GetNumMips() == 3 &&
                Result.Texture->Source.GetNumSlices() == 1);
        for (int32 Mip = 0; Mip < Source.MipBytes.Num(); ++Mip)
        {
            TArray64<uint8> Actual;
            TestTrue(TEXT("Every source mip equals independent engine producer bytes including raw alpha"),
                Result.Texture->Source.GetMipData(Actual, Mip) && Actual == Source.MipBytes[Mip]);
        }
        TestTrue(TEXT("Private source construction does not trigger platform or renderer build"),
            CastChecked<UTexture2D>(Result.Texture)->PlatformData == nullptr && Result.Texture->Resource == nullptr);
        Oracle.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceReadbackChannelsDraft,
    "P4MapKit.Quality.TextureSourceReadbackChannelsDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceReadbackChannelsDraft::RunTest(const FString&)
{
    using namespace P4TextureSourceAfter163;
    FSourceCustodyFixture Source;
    if (!TestTrue(TEXT("Actual native producer supplies BGRA source rows"), Source.Complete()))
        return false;
    for (SIZE_T Channel = 0; Channel < 4; ++Channel)
    {
        auto Request = Source.Request();
        uint32 Reads = 0;
        Request.ReadAt = [&](EBulkSource Kind, uint64 At, uint8* Dest, SIZE_T Count)
        {
            ++Reads;
            if (!Source.Read(Kind, At, Dest, Count))
                return false;
            // Callback violates immutable custody only in returned scratch bytes.
            // Original source, held files and written candidate remain untouched.
            if (Reads == Source.RowReadsPerPass + 1)
                Dest[Channel] ^= 1;
            return true;
        };
        Refuse(*this, Source, Request, "texture-source-readback-bytes-mismatch");
        TestTrue(TEXT("Each B/G/R/A difference refuses on first verification row after all mips were written"),
            Reads == Source.RowReadsPerPass + 1);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceGrayReadbackDraft, "P4MapKit.Quality.TextureSourceGrayReadbackDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceGrayReadbackDraft::RunTest(const FString&)
{
    using namespace P4TextureSourceAfter163;
    FSourceCustodyFixture Source(TSF_G16, "PF_G16");
    if (!TestTrue(TEXT("Public native source stores 16-bit grayscale without color conversion"), Source.Complete()))
        return false;
    for (SIZE_T Byte = 0; Byte < 2; ++Byte)
    {
        auto Request = Source.Request();
        uint32 Reads = 0;
        Request.ReadAt = [&](EBulkSource Kind, uint64 At, uint8* Dest, SIZE_T Count)
        {
            ++Reads;
            if (!Source.Read(Kind, At, Dest, Count))
                return false;
            if (Reads == Source.RowReadsPerPass + 1)
                Dest[Byte] ^= 0x80;
            return true;
        };
        Refuse(*this, Source, Request, "texture-source-readback-bytes-mismatch");
        TestTrue(TEXT("Changing either raw G16 byte refuses before any output becomes qualified"),
            Reads == Source.RowReadsPerPass + 1);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceVerificationReaderFailureDraft,
    "P4MapKit.Quality.TextureSourceVerificationReaderFailureDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceVerificationReaderFailureDraft::RunTest(const FString&)
{
    using namespace P4TextureSourceAfter163;
    FSourceCustodyFixture Source;
    if (!TestTrue(TEXT("Native producer has complete surviving source"), Source.Complete()))
        return false;
    auto Request = Source.Request();
    uint32 Reads = 0;
    Request.ReadAt = [&](EBulkSource Kind, uint64 At, uint8* Dest, SIZE_T Count)
    {
        ++Reads;
        return Reads <= Source.RowReadsPerPass && Source.Read(Kind, At, Dest, Count);
    };
    Refuse(*this, Source, Request, "texture-pixel-read-failed");
    TestTrue(TEXT("Verification read failure retains reader error rather than claiming a pixel mismatch"),
        Reads == Source.RowReadsPerPass + 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceTerminalCancellationDraft,
    "P4MapKit.Quality.TextureSourceTerminalCancellationDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceTerminalCancellationDraft::RunTest(const FString&)
{
    using namespace P4TextureSourceAfter163;
    FSourceCustodyFixture Source;
    if (!TestTrue(TEXT("Native producer has complete source for terminal cancellation"), Source.Complete()))
        return false;
    auto Request = Source.Request();
    uint32 Reads = 0, TerminalChecks = 0;
    Request.ReadAt = [&](EBulkSource Kind, uint64 At, uint8* Dest, SIZE_T Count)
    {
        ++Reads;
        return Source.Read(Kind, At, Dest, Count);
    };
    // Exact source-reviewed final-row sequence: after read, before row sink,
    // decoder completion, builder completion. Only the last check cancels.
    Request.Cancel = [&]() { return Reads == 2 * Source.RowReadsPerPass && ++TerminalChecks == 4; };
    Refuse(*this, Source, Request, "texture-pixel-cancelled");
    TestTrue(TEXT("All native writes and independent verification reads completed before final cancellation"),
        Reads == 2 * Source.RowReadsPerPass && TerminalChecks == 4);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceInputContractDraft, "P4MapKit.Quality.TextureSourceInputContractDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceInputContractDraft::RunTest(const FString&)
{
    using namespace P4TextureSourceAfter163;
    FSourceCustodyFixture Source;
    if (!TestTrue(TEXT("Every corrupt envelope starts with complete actual engine source"), Source.Complete()))
        return false;
    for (int32 Case = 0; Case < 23; ++Case)
    {
        auto Platform = Source.Platform;
        auto Request = Source.Request();
        Request.Platform = &Platform;
        uint32 Reads = 0;
        Request.ReadAt = [&](EBulkSource Kind, uint64 At, uint8* Dest, SIZE_T Count)
        {
            ++Reads;
            return Source.Read(Kind, At, Dest, Count);
        };
        const char* Expected = "texture-source-dimensions-unqualified";
        // Refusal-only corruptions; these never become native source objects.
        if (Case == 0)
        {
            Request.Platform = nullptr;
            Expected = "missing-texture-source-input";
        }
        if (Case == 1)
        {
            Request.ReadAt = {};
            Expected = "missing-texture-source-input";
        }
        // PF_R32_FLOAT is a real installed engine format, not an invented codec.
        if (Case == 2)
        {
            Platform.Format = "PF_R32_FLOAT";
            Expected = "texture-pixel-codec-unqualified";
        }
        if (Case == 3)
        {
            Platform.Mips.clear();
            Expected = "texture-source-mips-or-budget-unqualified";
        }
        if (Case == 4)
        {
            Platform.Mips.resize(33, Platform.Mips.back());
            Expected = "texture-source-mips-or-budget-unqualified";
        }
        if (Case == 5)
            Platform.Mips[0].LogicalWidth = 0;
        if (Case == 6)
            Platform.Mips[0].LogicalHeight = 0;
        if (Case == 7)
            Platform.Mips[0].LogicalWidth = 16385;
        if (Case == 8)
            Platform.Mips[0].LogicalHeight = 16385;
        if (Case == 9)
            Platform.Mips[0].LogicalDepth = 0;
        if (Case == 10)
            Platform.Mips[0].LogicalDepth = 2049;
        if (Case == 11)
        {
            Platform.Mips[0].LogicalDepth = 2;
            Expected = "texture-source-shape-mismatch";
        }
        if (Case == 12)
        {
            Request.Shape = ETextureShape::Cube;
            Request.NativeClass = TEXT("/Script/Engine.TextureCube");
            Platform.Slices = 5;
            Expected = "texture-source-shape-mismatch";
        }
        if (Case == 13)
        {
            Request.Shape = ETextureShape::Cube;
            Request.NativeClass = TEXT("/Script/Engine.TextureCube");
            Platform.Slices = 6;
            Expected = "texture-source-shape-mismatch";
        }
        if (Case == 14)
        {
            Request.Shape = ETextureShape::Array;
            Request.NativeClass = UTexture2DArray::StaticClass()->GetPathName();
            Platform.Mips[0].LogicalDepth = MAX_ARRAY_SLICES + 1;
            Expected = "texture-source-shape-mismatch";
        }
        if (Case == 15)
        {
            ++Platform.Mips[1].LogicalWidth;
            Expected = "texture-source-mip-dimensions-mismatch";
        }
        if (Case == 16)
        {
            ++Platform.Mips[1].LogicalHeight;
            Expected = "texture-source-mip-dimensions-mismatch";
        }
        if (Case == 17)
        {
            Platform.Mips[1].Bulk.Source = EBulkSource::None;
            Expected = "texture-pixel-mip-unavailable";
        }
        if (Case == 18)
        {
            --Platform.Mips[1].Bulk.Size;
            Expected = "texture-pixel-source-size-mismatch";
        }
        if (Case == 19)
        {
            Request.MaxDecodedBytes = 31;
            Expected = "texture-pixel-output-budget-exceeded";
        }
        if (Case == 20)
        {
            Request.MaxDecodedBytes = 39;
            Expected = "texture-pixel-output-budget-exceeded";
        }
        if (Case == 21)
        {
            Platform.Mips[1].Bulk.Offset = MAX_uint64 - 3;
            Expected = "texture-source-range-overflow";
        }
        if (Case == 22)
        {
            Request.NativeClass = UTextureRenderTarget2D::StaticClass()->GetPathName();
            Expected = "texture-source-native-class-unqualified";
        }
        Refuse(*this, Source, Request, Expected);
        TestTrue(FString::Printf(TEXT("Malformed input case %d refuses before source callbacks"), Case), Reads == 0);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceCallerErrorThreadDraft,
    "P4MapKit.Quality.TextureSourceCallerErrorThreadDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceCallerErrorThreadDraft::RunTest(const FString&)
{
    using namespace P4TextureSourceAfter163;
    FSourceCustodyFixture Source;
    if (!TestTrue(TEXT("Genuine native source complete before boundary calls"), Source.Complete()))
        return false;
    FPreservationOracle Oracle(Source);
    auto Request = Source.Request();
    uint32 Reads = 0, Cancels = 0;
    Request.ReadAt = [&](EBulkSource Kind, uint64 At, uint8* Dest, SIZE_T Count)
    {
        ++Reads;
        return Source.Read(Kind, At, Dest, Count);
    };
    Request.Cancel = [&]()
    {
        ++Cancels;
        return false;
    };
    auto Result = Previous(Source);
    FReadError Error{29, "caller-original-error"};
    TestFalse(TEXT("Existing caller error refuses without reinterpretation"),
        P4BuildPrivateTextureSource(Request, Result, Error));
    TestTrue(TEXT("Exact caller diagnostic offset and text survive"),
        Error.Offset == 29 && Error.Code == "caller-original-error");
    NoOutput(*this, Result);
    TestTrue(TEXT("Existing error causes no source or cancel callback"), Reads == 0 && Cancels == 0);
    auto WorkerResult = Previous(Source);
    FReadError WorkerError;
    const bool Accepted = Async(EAsyncExecution::Thread,
        [&]()
        {
            return P4BuildPrivateTextureSource(Request, WorkerResult, WorkerError);
        }).Get();
    TestFalse(TEXT("Worker refuses before UObject allocation or source traversal"), Accepted);
    TestTrue(TEXT("Worker boundary has exact game-thread diagnostic and zero callbacks"),
        WorkerError.Code == "texture-source-construction-requires-game-thread" && Reads == 0 && Cancels == 0);
    NoOutput(*this, WorkerResult);
    Oracle.Check(*this);
    return !HasAnyErrors();
}
#endif

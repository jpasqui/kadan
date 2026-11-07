#include "Misc/AutomationTest.h"
#include "P4MapKitCore/bulk_data.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/Package.h"
#include "UObject/ObjectVersion.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureBulkSummaryTest, "P4MapKit.Quality.TextureBulkSummary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureBulkSummaryTest::RunTest(const FString&)
{
    using namespace P4MapKit;
    FPackageFileSummary Summary;
    Summary.Tag = PACKAGE_FILE_TAG;
    Summary.SetFileVersions(GPackageFileUE4Version, GPackageFileLicenseeUE4Version, true);
    // UE 4.26 has no PKG_Cooked; a cooked package is the one with editor-only data filtered out.
    Summary.PackageFlags = PKG_FilterEditorOnly;
    Summary.FolderName = TEXT("None");
    Summary.Generations.AddZeroed(2);
    Summary.Generations[0].ExportCount = 11;
    Summary.Generations[0].NameCount = 17;
    Summary.Generations[1].ExportCount = 23;
    Summary.Generations[1].NameCount = 31;
    Summary.SavedByEngineVersion.Set(4, 26, 2, 123456, TEXT("native-writer-variable-branch"));
    Summary.CompatibleWithEngineVersion.Set(4, 26, 0, 123000, TEXT("compatible"));
    Summary.ChunkIDs = {3, 9};
    TArray<uint8> Bytes;
    const auto Write = [&]()
    {
        Bytes.Reset();
        FMemoryWriter Writer(Bytes, true);
        Writer << Summary;
        return !Writer.IsError();
    };
    if (!TestTrue(TEXT("Installed engine writes its actual cooked unversioned package summary"), Write()))
        return false;
    Summary.TotalHeaderSize = Bytes.Num();
    Summary.NameOffset = Summary.ImportOffset = Summary.ExportOffset = Summary.SoftPackageReferencesOffset =
        Bytes.Num();
    Summary.BulkDataStartOffset = int64(Bytes.Num()) + 128;
    if (!TestTrue(TEXT("Actual native summary has stable size after its final offsets"),
            Write() && Bytes.Num() == Summary.TotalHeaderSize))
        return false;
    const std::vector<uint8> Header(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    const auto Package = ReadPackageHeader(Header, 256);
    if (!TestTrue(TEXT("Read-only census independently qualifies native serialized bulk offset base"),
            Package.Error.Code.empty() && Package.HasBulkDataStartOffset && Package.BulkDataSummaryError.Code.empty() &&
                Package.BulkDataStartOffset == Summary.BulkDataStartOffset))
        return false;
    FByteBulkContext Context;
    Context.HeaderSize = Package.HeaderSize;
    Context.HasBulkStart = Package.HasBulkDataStartOffset;
    Context.BulkStart = Package.BulkDataStartOffset;
    Context.FileSizes = {{256, 64, -1, -1}};
    FByteBulkHeader Bulk;
    Bulk.Flags = BulkAtEnd | BulkSeparate;
    Bulk.Count = Bulk.Size = 16;
    // This is SaveBulkData's actual legacy convention: sidecar position minus
    // the combined package's recorded start-of-bulk area, commonly negative.
    Bulk.Offset = 32 - Summary.BulkDataStartOffset;
    FByteBulkRange Range;
    FReadError Error;
    TestTrue(TEXT("Native legacy relative sidecar offset resolves to its exact file position"),
        ResolveByteBulk(Bulk, 0, Context, Range, Error) && Range.Source == EBulkSource::Regular && Range.Offset == 32 &&
            Range.Size == 16);
    Bulk.Flags = BulkAtEnd;
    Bulk.Offset = int64(Package.HeaderSize) + 64 - Summary.BulkDataStartOffset;
    Error = {};
    TestTrue(TEXT("Combined archive bulk offsets resolve into the body after the actual header"),
        ResolveByteBulk(Bulk, 0, Context, Range, Error) && Range.Source == EBulkSource::Body && Range.Offset == 64);
    Bulk.Flags = BulkAtEnd | BulkSeparate | BulkNoFixup;
    Bulk.Offset = 32;
    Error = {};
    TestTrue(TEXT("NoOffsetFixUp retains the independently file-relative sidecar position"),
        ResolveByteBulk(Bulk, 0, Context, Range, Error) && Range.Offset == 32);
    Bulk.Offset = 49;
    Error = {};
    TestFalse(TEXT("Qualified base never bypasses the actual sidecar range bound"),
        ResolveByteBulk(Bulk, 0, Context, Range, Error));
    TestEqual(TEXT("Out-of-range bulk remains rejected"), FString(UTF8_TO_TCHAR(Error.Code.c_str())),
        FString(TEXT("bulk-range-out-of-bounds")));
    return !HasAnyErrors();
}
#endif

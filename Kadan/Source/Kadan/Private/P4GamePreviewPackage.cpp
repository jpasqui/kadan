#include "P4GamePreviewPackage.h"
#include "P4MapKitCore/export_package.h"

bool P4ReplaceGamePreviewExports(
    TArray<uint8>& Header, TArray<uint8>& Body, const TMap<int32, TArray<uint8>>& Replacements, FString& Error)
{
    using namespace P4MapKit;
    using namespace P4MapKit::Export;
    using namespace P4MapKit::Export::PackageRewriteDetail;
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    constexpr int64 Maximum = 64 * 1024 * 1024;
    if (Header.Num() < 24 || Body.Num() < 4 || Header.Num() > Maximum || Body.Num() > Maximum)
        return Fail(TEXT("Preview package exceeds the bounded export editor."));
    const std::vector<uint8> OldHeader(Header.GetData(), Header.GetData() + Header.Num());
    const std::vector<uint8> OldBody(Body.GetData(), Body.GetData() + Body.Num());
    // This also validates the complete summary, table separation, contiguous
    // bodies, tag and bulk boundary while preserving every original byte.
    RewrittenPackage Qualified;
    std::string Why;
    if (!RewriteCookedNames(OldHeader, OldBody, {}, Qualified, Why))
    {
        Error = TEXT("Preview export rewrite refused: ") + FString(UTF8_TO_TCHAR(Why.c_str()));
        return false;
    }
    const auto Before = ReadPackageHeader(OldHeader, OldBody.size());
    for (const auto& Pair : Replacements)
        if (Pair.Key < 0 || std::size_t(Pair.Key) >= Before.Exports.size() || Pair.Value.Num() > Maximum)
            return Fail(TEXT("Preview export replacement index or size is invalid."));
    if (Replacements.Num() == 0)
    {
        Error.Reset();
        return true;
    }

    FByteReader Summary(OldHeader, 20);
    std::size_t Count = 0, Declared = 0;
    std::string Text;
    if (!Summary.Count(Count, 1000) || !Summary.Skip(Count * 20) || !Summary.Count(Declared, OldHeader.size()) ||
        !Summary.String(Text))
        return Fail(TEXT("Preview package summary prefix could not be read."));
    const auto FlagsAt = Summary.Tell();
    if (!Summary.Skip(72) || !Summary.Count(Count, 100) || !Summary.Skip(Count * 8))
        return Fail(TEXT("Preview package generation records could not be read."));
    for (int Index = 0; Index < 2; ++Index)
        if (!Summary.Skip(10) || !Summary.String(Text))
            return Fail(TEXT("Preview package engine version could not be read."));
    uint32 Compression = 0;
    if (!Summary.U32(Compression) || Compression || !Summary.Count(Count, 0) || !Summary.Skip(4) ||
        !Summary.Count(Count, 100))
        return Fail(TEXT("Preview package compression is unsupported."));
    for (std::size_t Index = 0; Index < Count; ++Index)
        if (!Summary.String(Text))
            return Fail(TEXT("Preview package additional names could not be read."));
    if (!Summary.Skip(4))
        return Fail(TEXT("Preview package bulk boundary is missing."));
    const auto BulkAt = Summary.Tell();
    uint64 TableAt = 0;
    if (!Scalar(OldHeader, FlagsAt + 24, TableAt))
        return Fail(TEXT("Preview package export table is missing."));

    TArray<int32> Order;
    int64 NewSize = 4;
    for (std::size_t Index = 0; Index < Before.Exports.size(); ++Index)
    {
        Order.Add(int32(Index));
        const auto* Replacement = Replacements.Find(int32(Index));
        NewSize += Replacement ? Replacement->Num() : Before.Exports[Index].Size;
    }
    if (NewSize > Maximum)
        return Fail(TEXT("Preview package replacement body exceeds its size bound."));
    Order.Sort([&](int32 A, int32 B) { return Before.Exports[A].Offset < Before.Exports[B].Offset; });
    std::vector<uint8> NewHeader = OldHeader;
    TArray<uint8> NewBody;
    NewBody.Reserve(int32(NewSize));
    for (const int32 Index : Order)
    {
        const auto& Export = Before.Exports[Index];
        const auto* Replacement = Replacements.Find(Index);
        const auto Offset = NewBody.Num();
        if (Replacement)
            NewBody.Append(*Replacement);
        else if (Export.Size)
            NewBody.Append(Body.GetData() + Export.Offset, int32(Export.Size));
        const auto EntryAt = std::size_t(TableAt) + std::size_t(Index) * 104;
        Put(NewHeader, EntryAt + 28, NewBody.Num() - Offset, 8);
        Put(NewHeader, EntryAt + 36, NewHeader.size() + Offset, 8);
    }
    Put(NewHeader, BulkAt, NewHeader.size() + NewBody.Num(), 8);
    NewBody.Append(OldBody.data() + OldBody.size() - 4, 4);
    const auto After = ReadPackageHeader(NewHeader, NewBody.Num());
    if (!After.Error.Code.empty() || After.Exports.size() != Before.Exports.size() || After.Names != Before.Names ||
        After.Imports.size() != Before.Imports.size())
        return Fail(TEXT("Preview package export rewrite failed readback."));
    // Read back every payload, including all untouched exports. Header writes
    // are restricted to the two serial fields and the bulk boundary above.
    for (std::size_t Index = 0; Index < Before.Exports.size(); ++Index)
    {
        const auto& A = Before.Exports[Index];
        const auto& B = After.Exports[Index];
        const auto* Replacement = Replacements.Find(int32(Index));
        const auto* Expected = Replacement ? Replacement->GetData() : OldBody.data() + A.Offset;
        const auto Size = Replacement ? std::size_t(Replacement->Num()) : A.Size;
        if (B.Size != Size || (Size && FMemory::Memcmp(Expected, NewBody.GetData() + B.Offset, Size)))
            return Fail(TEXT("Preview package payload differs after export rewrite."));
    }
    Header.Reset(int32(NewHeader.size()));
    Header.Append(NewHeader.data(), int32(NewHeader.size()));
    Body = MoveTemp(NewBody);
    Error.Reset();
    return true;
}

bool P4SetGamePreviewPackageName(
    TArray<uint8>& Header, const TArray<uint8>& Body, const FString& Package, FString& Error)
{
    using namespace P4MapKit;
    using namespace P4MapKit::Export;
    using namespace P4MapKit::Export::PackageRewriteDetail;
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (Header.Num() < 24 || Body.Num() < 4 || Package.Len() > 1024)
        return Fail(TEXT("Preview package name or summary is invalid."));
    const std::string NewName(TCHAR_TO_UTF8(*Package));
    if (!AsciiName(NewName))
        return Fail(TEXT("Preview package name is not bounded ASCII."));
    const std::vector<uint8> Old(Header.GetData(), Header.GetData() + Header.Num());
    const std::vector<uint8> Payload(Body.GetData(), Body.GetData() + Body.Num());
    RewrittenPackage Qualified;
    std::string Why;
    if (!RewriteCookedNames(Old, Payload, {}, Qualified, Why))
    {
        Error = TEXT("Preview package name rewrite refused: ") + FString(UTF8_TO_TCHAR(Why.c_str()));
        return false;
    }
    const auto Before = ReadPackageHeader(Old, Payload.size());
    FByteReader Summary(Old, 20);
    std::size_t Count = 0, Size = 0;
    std::string Text;
    if (!Summary.Count(Count, 1000) || !Summary.Skip(Count * 20))
        return Fail(TEXT("Preview custom versions are invalid."));
    const auto SizeAt = Summary.Tell(), NameAt = SizeAt + 4;
    if (!Summary.Count(Size, Old.size()) || !Summary.String(Text))
        return Fail(TEXT("Preview package name is unreadable."));
    const auto FlagsAt = Summary.Tell();
    if (!Summary.Skip(72) || !Summary.Count(Count, 100) || !Summary.Skip(Count * 8))
        return Fail(TEXT("Preview generations are invalid."));
    for (int32 I = 0; I < 2; ++I)
        if (!Summary.Skip(10) || !Summary.String(Text))
            return Fail(TEXT("Preview engine versions are invalid."));
    uint32 Compression = 0;
    if (!Summary.U32(Compression) || Compression || !Summary.Count(Count, 0) || !Summary.Skip(4) ||
        !Summary.Count(Count, 100))
        return Fail(TEXT("Preview compressed package rename is unsupported."));
    for (std::size_t I = 0; I < Count; ++I)
        if (!Summary.String(Text))
            return Fail(TEXT("Preview additional package name is invalid."));
    const auto RegistryAt = Summary.Tell(), BulkAt = RegistryAt + 4;
    if (!Summary.Skip(12))
        return Fail(TEXT("Preview bulk boundary is invalid."));
    const auto WorldTileAt = Summary.Tell();
    if (!Summary.Skip(4) || !Summary.Count(Count, 100) || !Summary.Skip(Count * 4))
        return Fail(TEXT("Preview chunk IDs are invalid."));
    const auto PreloadAt = Summary.Tell();
    if (!Summary.Skip(8))
        return Fail(TEXT("Preview preload records are invalid."));

    std::vector<uint8> Name;
    String(Name, NewName);
    const int64 Delta = int64(Name.size()) - int64(FlagsAt - NameAt);
    const int64 NewSize = int64(Old.size()) + Delta;
    if (NewSize <= 0 || NewSize > 64 * 1024 * 1024)
        return Fail(TEXT("Preview renamed header exceeds its bound."));
    std::vector<uint8> Changed;
    Changed.reserve(std::size_t(NewSize));
    Changed.insert(Changed.end(), Old.begin(), Old.begin() + NameAt);
    Changed.insert(Changed.end(), Name.begin(), Name.end());
    Changed.insert(Changed.end(), Old.begin() + FlagsAt, Old.end());
    const auto Moved = [&](std::size_t At) { return std::size_t(int64(At) + (At >= FlagsAt ? Delta : 0)); };
    for (const auto At : {FlagsAt + 8, FlagsAt + 16, FlagsAt + 24, FlagsAt + 32, FlagsAt + 36, FlagsAt + 44,
             FlagsAt + 48, FlagsAt + 52, RegistryAt, WorldTileAt, PreloadAt + 4})
    {
        uint64 Value = 0;
        if (!Scalar(Old, At, Value))
            return Fail(TEXT("Preview summary offset could not be read."));
        if (Value)
            Put(Changed, Moved(At), uint64(int64(Value) + Delta));
    }
    Put(Changed, SizeAt, NewSize);
    uint64 ExportTable = 0, OldBulk = 0;
    if (!Scalar(Old, FlagsAt + 24, ExportTable) || !Scalar(Old, BulkAt, OldBulk, 8))
        return Fail(TEXT("Preview export offsets could not be read."));
    for (std::size_t I = 0; I < Before.Exports.size(); ++I)
        Put(Changed, Moved(std::size_t(ExportTable) + I * 104 + 36), uint64(NewSize) + Before.Exports[I].Offset, 8);
    Put(Changed, Moved(BulkAt), uint64(int64(OldBulk) + Delta), 8);
    const FGuid Guid = FGuid::NewGuid();
    const uint32 Words[] = {Guid.A, Guid.B, Guid.C, Guid.D};
    for (int32 I = 0; I < 4; ++I)
        Put(Changed, Moved(FlagsAt + 56 + I * 4), Words[I]);
    const auto After = ReadPackageHeader(Changed, Payload.size());
    if (!After.Error.Code.empty() || After.Package != NewName || After.Names != Before.Names ||
        After.Imports.size() != Before.Imports.size() || After.Exports.size() != Before.Exports.size())
        return Fail(TEXT("Preview renamed package failed header readback."));
    for (std::size_t I = 0; I < Before.Exports.size(); ++I)
        if (Before.Exports[I].Offset != After.Exports[I].Offset || Before.Exports[I].Size != After.Exports[I].Size)
            return Fail(TEXT("Preview package rename changed a relative body span."));
    RewrittenPackage Check;
    if (!RewriteCookedNames(Changed, Payload, {}, Check, Why))
        return Fail(TEXT("Preview renamed package failed full layout readback."));
    Header.Reset(int32(Changed.size()));
    Header.Append(Changed.data(), int32(Changed.size()));
    Error.Reset();
    return true;
}

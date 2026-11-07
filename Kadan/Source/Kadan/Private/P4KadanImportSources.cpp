#include "P4KadanImportSources.h"
#include "P4GamePaths.h"
#include "P4KadanImport.h"
#include "P4BoundedSourceRead.h"
#include "P4NativeHash.h"
#include "P4KadanSourceStamp.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END

namespace
{
FP4KadanImportSourceScope* Active = nullptr;
int64 Generation = 1;
bool Canonical(const FString& Package)
{
    return Package.Len() <= 4096 && P4IsValidContentPackage(Package);
}
bool ReadCurrent(const FString& Package, const FString& HeaderPath, const FString& BodyPath, FP4KadanSourceBinding& Out,
    FString& Error)
{
    Out = FP4KadanSourceBinding();
    Error.Reset();
    if (!Canonical(Package) || HeaderPath.IsEmpty() || BodyPath.IsEmpty() ||
        !FPaths::GetExtension(HeaderPath).Equals(TEXT("uasset"), ESearchCase::IgnoreCase) ||
        !FPaths::IsSamePath(FPaths::ChangeExtension(HeaderPath, TEXT("uexp")), BodyPath))
    {
        Error = TEXT("Class source requires a canonical package and its registered header/body pair");
        return false;
    }
    FP4KadanSourceBinding Read;
    if (!P4ReadBoundedSourceFile(HeaderPath, 64 * 1024 * 1024, Read.Header, Error) ||
        !P4ReadBoundedSourceFile(BodyPath, 64 * 1024 * 1024, Read.Body, Error))
        return false;
    if (!Read.Header.Num() || !Read.Body.Num())
    {
        Error = TEXT("Registered class source is empty");
        return false;
    }
    const auto Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Read.Header.GetData(), Read.Header.GetData() + Read.Header.Num()),
        std::vector<uint8>(Read.Body.GetData(), Read.Body.GetData() + Read.Body.Num()));
    if (!Inventory.Error.Code.empty())
    {
        Error = TEXT("Registered class source inventory or export bounds are invalid: ") +
            FString(UTF8_TO_TCHAR(Inventory.Error.Code.c_str()));
        return false;
    }
    if (!Package.Equals(FString(UTF8_TO_TCHAR(Inventory.Package.c_str())), ESearchCase::CaseSensitive))
    {
        Error = TEXT("Registered class source canonical identity differs");
        return false;
    }
    if (!P4HashSourceBuffers(
            Read.Header.GetData(), Read.Header.Num(), Read.Body.GetData(), Read.Body.Num(), Read.SourceDigest))
    {
        Error = TEXT("Registered class source could not be hashed");
        return false;
    }
    Read.Package = Package;
    Read.HeaderPath = HeaderPath;
    Read.BodyPath = BodyPath;
    Out = MoveTemp(Read);
    return true;
}
// A source pair already read whole keeps its bytes while both files keep their stamps. Reading, parsing and
// hashing the pair again at every check was a quarter of an import, and opening each file to ask for its
// identity was most of what was left.
struct FSeenSource
{
    FP4KadanSourceStamp Header, Body;
    FString Digest;
    int64 Checked = 0;
};
TMap<FString, FSeenSource> Seen;
// Bound sources read whole in the running import, kept for the rest of it (Kadan's folders only gain files while
// an import runs). The oldest go first once the kept bytes pass the budget.
struct FBoundRead
{
    FP4KadanSourceBinding Binding;
    int64 Import = 0, Kept = 0;
    // Digests of the kept bytes by kind (P4KadanBoundSourceDigest), made once per kept read.
    TMap<FString, FString> Derived;
};
TMap<FString, FBoundRead> BoundReads;
// The keys in the order they were kept, each with the number it was kept as: a key kept again later has a newer
// number, so its older place in the line is passed over. The line is read from Oldest on and shortened now and then,
// so letting a read go costs nothing however many are kept.
TArray<TPair<FString, int64>> BoundOrder;
int32 BoundOldest = 0;
int64 BoundBytes = 0, BoundKept = 0;
constexpr int64 BoundBudget = 256ll * 1024 * 1024;
void KeepBoundRead(const FString& Key, const FP4KadanSourceBinding& Binding, int64 Import)
{
    if (const FBoundRead* Old = BoundReads.Find(Key))
        BoundBytes -= Old->Binding.Header.Num() + Old->Binding.Body.Num();
    BoundReads.Add(Key, FBoundRead{Binding, Import, ++BoundKept, {}});
    BoundOrder.Emplace(Key, BoundKept);
    BoundBytes += Binding.Header.Num() + Binding.Body.Num();
    while (BoundBytes > BoundBudget && BoundOldest < BoundOrder.Num() - 1)
    {
        const TPair<FString, int64>& Next = BoundOrder[BoundOldest++];
        const FBoundRead* Gone = BoundReads.Find(Next.Key);
        if (!Gone || Gone->Kept != Next.Value)
            continue;
        BoundBytes -= Gone->Binding.Header.Num() + Gone->Binding.Body.Num();
        BoundReads.Remove(Next.Key);
    }
    if (BoundOldest > 4096 && BoundOldest * 2 > BoundOrder.Num())
    {
        BoundOrder.RemoveAt(0, BoundOldest, false);
        BoundOldest = 0;
    }
}
} //namespace

FP4KadanImportSourceScope::FP4KadanImportSourceScope(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources)
{
    check(IsInGameThread());
    if (!Active)
        ++Generation;
    Previous = Active;
    Active = this;
    AddSources(Sources);
}
FP4KadanImportSourceScope::~FP4KadanImportSourceScope()
{
    check(IsInGameThread());
    check(Active == this);
    Active = Previous;
    if (!Active)
    {
        ++Generation;
        BoundReads.Empty();
        BoundOrder.Empty();
        BoundOldest = 0;
        BoundBytes = 0;
    }
}
int64 P4KadanImportGeneration()
{
    check(IsInGameThread());
    return Active ? Generation : 0;
}
void FP4KadanImportSourceScope::AddSources(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources)
{
    check(IsInGameThread());
    // A material import binds its family's rows again for each instance, so what a row binds is kept with the
    // row's own words: a row kept under the same address with other words is worked out again.
    struct FMade
    {
        FString Package, Header, Body;
        bool Valid = false;
        FName Key;
        FPaths Value;
    };
    static TMap<const FP4KadanSourceAsset*, FMade> Made;
    if (Made.Num() > 200000)
        Made.Reset();
    for (const auto& Source : Sources)
    {
        if (!Source.IsValid())
            continue;
        FMade* Row = Made.Find(Source.Get());
        if (!Row || !Row->Package.Equals(Source->Package, ESearchCase::CaseSensitive) ||
            !Row->Header.Equals(Source->Header, ESearchCase::CaseSensitive) ||
            !Row->Body.Equals(Source->Body, ESearchCase::CaseSensitive))
        {
            Row = &Made.Add(Source.Get(), FMade{Source->Package, Source->Header, Source->Body});
            Row->Valid = Canonical(Source->Package);
            if (Row->Valid)
            {
                Row->Key = FName(*Source->Package);
                Row->Value = FPaths{Source->Package, ::FPaths::ConvertRelativePathToFull(Source->Header),
                    ::FPaths::ConvertRelativePathToFull(Source->Body)};
                // Keep missing paths bound, so read failure cannot select another file.
                if (Source->Header.IsEmpty())
                    Row->Value.Header.Reset();
                if (Source->Body.IsEmpty())
                    Row->Value.Body.Reset();
            }
        }
        if (!Row->Valid)
            continue;
        const FName Key = Row->Key;
        if (Ambiguous.Contains(Key))
            continue;
        const FPaths& Value = Row->Value;
        if (const FPaths* Prior = Paths.Find(Key))
        {
            if (!Prior->Package.Equals(Value.Package, ESearchCase::CaseSensitive) ||
                !::FPaths::IsSamePath(Prior->Header, Value.Header) || !::FPaths::IsSamePath(Prior->Body, Value.Body))
            {
                Paths.Remove(Key);
                Ambiguous.Add(Key);
            }
        }
        else
            Paths.Add(Key, Value);
    }
}
bool P4KadanHasBoundSource(const FString& Package)
{
    check(IsInGameThread());
    return Active && (Active->Paths.Contains(FName(*Package)) || Active->Ambiguous.Contains(FName(*Package)));
}
bool P4KadanReadBoundSource(const FString& Package, FP4KadanSourceBinding& Out, FString& Error)
{
    Out = FP4KadanSourceBinding();
    const FP4KadanSourceBinding* Kept = FP4KadanImportSourceScope::Bound(Package, Error);
    if (!Kept)
        return false;
    Out = *Kept;
    return true;
}
const FP4KadanSourceBinding* FP4KadanImportSourceScope::Bound(const FString& Package, FString& Error)
{
    check(IsInGameThread());
    Error.Reset();
    if (!Active || !Canonical(Package))
    {
        Error = TEXT("No current import source binding for this class package");
        return nullptr;
    }
    const FName Key(*Package);
    if (Active->Ambiguous.Contains(Key))
    {
        Error = TEXT("Current import source binding is ambiguous");
        return nullptr;
    }
    const auto* Found = Active->Paths.Find(Key);
    if (!Found)
    {
        Error = TEXT("No current import source binding for this class package");
        return nullptr;
    }
    if (!Found->Package.Equals(Package, ESearchCase::CaseSensitive))
    {
        Error = TEXT("Current import source binding canonical spelling differs");
        return nullptr;
    }
    const FString ReadKey = Found->Header + TEXT("|") + Found->Body;
    const int64 Import = P4KadanImportGeneration();
    if (const FBoundRead* Kept = BoundReads.Find(ReadKey))
        if (Kept->Import == Import && Kept->Binding.Package.Equals(Package, ESearchCase::CaseSensitive))
            return &Kept->Binding;
    FP4KadanSourceBinding Read;
    if (!ReadCurrent(Package, Found->Header, Found->Body, Read, Error))
        return nullptr;
    // The newest kept read is never let go, so the one just kept is there.
    KeepBoundRead(ReadKey, Read, Import);
    return &BoundReads.FindChecked(ReadKey).Binding;
}
namespace
{
// True when the files a snapshot was read from still hold the bytes its digest names.
bool FilesCurrent(const FP4KadanSourceBinding& Source, FString& Error)
{
    FP4KadanSourceBinding Now;
    const FString Key = Source.HeaderPath + TEXT("|") + Source.BodyPath;
    const int64 Import = P4KadanImportGeneration();
    if (FSeenSource* Prior = Import ? Seen.Find(Key) : nullptr)
        if (Prior->Checked == Import && Prior->Digest.Equals(Source.SourceDigest, ESearchCase::CaseSensitive))
            return true;
    FP4KadanSourceStamp Header, Body;
    const bool Known = P4KadanSourceStampOf(Source.HeaderPath, Header) && P4KadanSourceStampOf(Source.BodyPath, Body);
    if (FSeenSource* Prior = Known ? Seen.Find(Key) : nullptr)
        if (Prior->Digest.Equals(Source.SourceDigest, ESearchCase::CaseSensitive) && Prior->Header == Header &&
            Prior->Body == Body)
        {
            Prior->Checked = Import;
            return true;
        }
    if (!ReadCurrent(Source.Package, Source.HeaderPath, Source.BodyPath, Now, Error))
        return false;
    if (!Source.SourceDigest.Equals(Now.SourceDigest, ESearchCase::CaseSensitive))
    {
        Error = TEXT("Current import class source changed after comparison");
        return false;
    }
    // Kept only when neither file changed while it was read.
    FP4KadanSourceStamp HeaderAfter, BodyAfter;
    if (Known && P4KadanSourceStampOf(Source.HeaderPath, HeaderAfter) &&
        P4KadanSourceStampOf(Source.BodyPath, BodyAfter) && Header == HeaderAfter && Body == BodyAfter)
        Seen.Add(Key, FSeenSource{Header, Body, Now.SourceDigest, Import});
    return true;
}
} // namespace
bool P4KadanSourceBindingCurrent(const FP4KadanSourceBinding& Source, FString& Error)
{
    check(IsInGameThread());
    Error.Reset();
    FString BufferDigest;
    if (Source.SourceDigest.Len() != 64 || !Source.Header.Num() || !Source.Body.Num() ||
        !P4HashSourceBuffers(
            Source.Header.GetData(), Source.Header.Num(), Source.Body.GetData(), Source.Body.Num(), BufferDigest) ||
        !BufferDigest.Equals(Source.SourceDigest, ESearchCase::CaseSensitive))
    {
        Error = TEXT("Current import class source snapshot is incomplete or changed");
        return false;
    }
    return FilesCurrent(Source, Error);
}
bool P4KadanBoundSourceDigest(const FString& Package, const FString& Kind,
    TFunctionRef<FString(const FP4KadanSourceBinding&)> Make, FString& Out, FString& Error)
{
    check(IsInGameThread());
    Out.Reset();
    // The kept bytes are the import's own copy, which no caller holds, so they are not hashed again to show
    // they are still the bytes read: only the files are checked.
    const FP4KadanSourceBinding* Kept = FP4KadanImportSourceScope::Bound(Package, Error);
    if (!Kept || !FilesCurrent(*Kept, Error))
        return false;
    FBoundRead& Read = BoundReads.FindChecked(Kept->HeaderPath + TEXT("|") + Kept->BodyPath);
    if (const FString* Made = Read.Derived.Find(Kind))
    {
        Out = *Made;
        return true;
    }
    Out = Make(Read.Binding);
    if (Out.IsEmpty())
    {
        Error = TEXT("Current import source digest could not be made");
        return false;
    }
    Read.Derived.Add(Kind, Out);
    return true;
}

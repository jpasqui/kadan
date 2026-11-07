#include "P4OriginalWorldSource.h"
#include "P4GamePaths.h"
#include "P4BoundedSourceRead.h"
#include "P4RegistryInputLease.h"
#include "P4NativeHash.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END

struct FP4OriginalWorldSource::FImpl
{
    FString Package, HeaderPath, BodyPath, Digest;
    TArray<uint8> Header, Body;
    P4RegistryLease::FInput HeaderLease, BodyLease;
};
FP4OriginalWorldSource::FP4OriginalWorldSource(TUniquePtr<FImpl>&& Value) : Impl(MoveTemp(Value))
{}
FP4OriginalWorldSource::~FP4OriginalWorldSource() = default;
bool FP4OriginalWorldSource::Current(FString& Error) const
{
    if (!Impl || !Impl->HeaderLease.Current() || !Impl->BodyLease.Current())
    {
        Error = TEXT("original-world-source-selected-pair-changed");
        return false;
    }
    return true;
}
const FString& FP4OriginalWorldSource::Package() const
{
    return Impl->Package;
}
const FString& FP4OriginalWorldSource::HeaderPath() const
{
    return Impl->HeaderPath;
}
const FString& FP4OriginalWorldSource::BodyPath() const
{
    return Impl->BodyPath;
}
const FString& FP4OriginalWorldSource::SourceDigest() const
{
    return Impl->Digest;
}
const TArray<uint8>& FP4OriginalWorldSource::Header() const
{
    return Impl->Header;
}
const TArray<uint8>& FP4OriginalWorldSource::Body() const
{
    return Impl->Body;
}
bool P4ReadOriginalWorldSource(const FString& Header, const FString& Body, const FString& ExpectedPackage,
    FP4OriginalWorldSourcePtr& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (Header.IsEmpty() || (!ExpectedPackage.IsEmpty() && !P4IsValidContentPackage(ExpectedPackage)) ||
        !FPaths::GetExtension(Header).Equals(TEXT("umap"), ESearchCase::CaseSensitive))
    {
        Error = TEXT("original-world-source-canonical-map-pair-required");
        return false;
    }
    auto Value = MakeUnique<FP4OriginalWorldSource::FImpl>();
    Value->HeaderPath = FPaths::ConvertRelativePathToFull(Header);
    Value->BodyPath = FPaths::ConvertRelativePathToFull(
        Body.IsEmpty() ? FPaths::ChangeExtension(Header, TEXT("uexp")) : Body);
    FPaths::NormalizeFilename(Value->HeaderPath);
    FPaths::NormalizeFilename(Value->BodyPath);
    if (!FPaths::IsSamePath(FPaths::ChangeExtension(Value->HeaderPath, TEXT("uexp")), Value->BodyPath))
    {
        Error = TEXT("original-world-source-body-is-not-selected-header-pair");
        return false;
    }
#if PLATFORM_WINDOWS
    if (!Value->HeaderLease.Open(std::wstring(*Value->HeaderPath)) ||
        !Value->BodyLease.Open(std::wstring(*Value->BodyPath)))
    {
        Error = TEXT("original-world-source-cannot-hold-selected-pair");
        return false;
    }
#else
    Error = TEXT("original-world-source-immutable-custody-unavailable");
    return false;
#endif
    if (!P4ReadBoundedSourceFile(Value->HeaderPath, 64 * 1024 * 1024, Value->Header, Error) ||
        !P4ReadBoundedSourceFile(Value->BodyPath, 64 * 1024 * 1024, Value->Body, Error) || !Value->Header.Num() ||
        !Value->Body.Num() || uint64(Value->Header.Num()) != Value->HeaderLease.Size() ||
        uint64(Value->Body.Num()) != Value->BodyLease.Size() ||
        !P4HashSourceBuffers(
            Value->Header.GetData(), Value->Header.Num(), Value->Body.GetData(), Value->Body.Num(), Value->Digest))
        return false;
    const auto Inventory = P4MapKit::ReadPackageHeader(
        std::vector<uint8>(Value->Header.GetData(), Value->Header.GetData() + Value->Header.Num()), Value->Body.Num());
    const FString ActualPackage = UTF8_TO_TCHAR(Inventory.Package.c_str());
    if (!Inventory.Error.Code.empty() || !P4IsValidContentPackage(ActualPackage) ||
        (!ExpectedPackage.IsEmpty() && !P4ExactNativeString(ActualPackage, ExpectedPackage)))
    {
        Error = TEXT("original-world-source-package-or-inventory-differs");
        return false;
    }
    int32 Roots = 0;
    for (const auto& Export : Inventory.Exports)
        if (Export.Outer == 0 && Export.Name == std::string(TCHAR_TO_UTF8(*FPackageName::GetShortName(ActualPackage))))
        {
            std::string Class;
            if (!Inventory.Reference(Export.Class, Class) || Class != "/Script/Engine.World")
            {
                Error = TEXT("original-world-source-root-is-not-world");
                return false;
            }
            ++Roots;
        }
    if (Roots != 1)
    {
        Error = TEXT("original-world-source-world-root-ambiguous");
        return false;
    }
    Value->Package = ActualPackage;
    FP4OriginalWorldSourcePtr Source = MakeShareable(new FP4OriginalWorldSource(MoveTemp(Value)));
    if (!Source->Current(Error))
        return false;
    Out = MoveTemp(Source);
    return true;
}

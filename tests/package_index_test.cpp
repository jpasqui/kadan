// The UI census must agree with the payload reader while loading only package headers.
#include "core/package_reader.h"
#include <iostream>
#include <cstdlib>
static int Checks = 0;
static void Check(bool Pass)
{
    ++Checks;
    if (!Pass) { std::cerr << "package index check failed: " << Checks << '\n'; std::exit(1); }
}
namespace P4MapKit
{
static FPackageInventory CheckedReadPackage(const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body)
{
    const auto Full = ReadPackage(Header, Body);
    const auto Index = ReadPackageHeader(Header, Body.size());
    Check(Full.Error.Code == Index.Error.Code && Full.Error.Offset == Index.Error.Offset);
    Check(Full.Package == Index.Package && Full.Names == Index.Names && Full.Flags == Index.Flags);
    Check(Full.Imports.size() == Index.Imports.size() && Full.Exports.size() == Index.Exports.size());
    Check(Full.HeaderSpans.size() == Index.HeaderSpans.size() && Full.BodySpans.size() == Index.BodySpans.size());
    for (std::size_t At = 0; At < Full.Imports.size(); ++At)
    {
        std::string A, B;
        Check(Full.Reference(-1 - std::int32_t(At), A) == Index.Reference(-1 - std::int32_t(At), B) && A == B);
    }
    for (std::size_t At = 0; At < Full.Exports.size(); ++At)
    {
        const auto& A = Full.Exports[At]; const auto& B = Index.Exports[At];
        Check(A.Offset == B.Offset && A.Size == B.Size && A.Name == B.Name && A.Class == B.Class);
    }
    if (Full.Error.Code.empty())
    {
        // The census can account for large actual companion files without allocating those files.
        const auto Large = ReadPackageHeader(Header, 512u * 1024u * 1024u);
        Check(Large.Error.Code.empty() && Large.Exports.size() == Full.Exports.size());
        static bool CheckedSignedSpans = false;
        if (!CheckedSignedSpans)
        {
            CheckedSignedSpans = true;
            Check(ReadPackageHeader(Header, std::numeric_limits<std::size_t>::max()).Error.Code == "package-body-size-out-of-range");
            for (const auto& Span : Full.HeaderSpans)
            {
                if (Span.Kind != "exports") continue;
                for (const std::size_t Field : {std::size_t(28), std::size_t(36)})
                {
                    auto Negative = Header;
                    for (std::size_t Byte = 0; Byte < 8; ++Byte) Negative[Span.Offset + Field + Byte] = 0xff;
                    Check(ReadPackageHeader(Negative, std::size_t(std::numeric_limits<std::int64_t>::max())).Error.Code == "export-payload-bounds");
                }
            }
        }
    }
    return Full;
}
}
#define ReadPackage CheckedReadPackage
#include "package_reader_test.h"
#undef ReadPackage
int main()
{
    CheckPackageReaders();
    CheckPropertyReaders();
    std::vector<std::uint8_t> Oversized(64u * 1024u * 1024u + 1u);
    const auto Header = P4MapKit::ReadPackageHeader(Oversized, 0);
    Check(Header.Error.Code == "package-streaming-required" && Header.Error.Offset == 0);
    const auto Body = P4MapKit::ReadPackage({}, Oversized);
    Check(Body.Error.Code == "package-streaming-required" && Body.Error.Offset == 0);
    std::cout << Checks << " package census checks passed\n";
}

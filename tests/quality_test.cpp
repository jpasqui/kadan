#include "core/quality.h"
#include <cstdlib>
#include <iostream>
#include <random>

static std::size_t Checks = 0;
static void CheckImpl(bool Value, int Line, const char* Expression)
{
    ++Checks;
    if (!Value) { std::cerr << "quality_test.cpp:" << Line << ": " << Expression << '\n'; std::exit(1); }
}
#define Check(Value) CheckImpl((Value), __LINE__, #Value)
#include "package_reader_test.h"
#include "property_decoder_test.h"
#include "bulk_data_test.h"
#include "texture_payload_test.h"
#include "texture_pixels_test.h"
#include "texture_profiles_test.h"
#include "static_mesh_render_test.h"
#include "level_native_test.h"
int main()
{
    using namespace P4MapKit;
    CheckPackageReaders();
    CheckByteBulkReaders();
    CheckTexturePayloadReaders();
    CheckTexturePixels();
    CheckTextureProfiles();
    CheckStaticMeshRenderReaders();
    CheckLevelNativeReaders();
    CheckPropertyReaders();
    CheckValueDecoder();
    std::uint64_t Bits = 17;
    for (const std::string Text : {"", "-", "+1", " 1", "1 ", "1.0", "1e3", "x", "18446744073709551616", "-1"})
    { Check(!ParseInteger(Text, true, Bits)); Check(Bits == 17); }
    Check(!ParseInteger("9223372036854775808", false, Bits));
    Check(!ParseInteger("-", false, Bits));
    Check(!ParseInteger("-9223372036854775809", false, Bits));
    Check(ParseInteger("-9223372036854775808", false, Bits)); Check(Bits == (std::uint64_t(1) << 63));
    Check(ParseInteger("9223372036854775807", false, Bits)); Check(Bits == (std::uint64_t(1) << 63) - 1);
    Check(ParseInteger("18446744073709551615", true, Bits)); Check(Bits == UINT64_MAX);
    Check(ParseInteger("-42", false, Bits)); Check(Bits == std::uint64_t(0) - 42);
    Check(ParseInteger("-0", false, Bits)); Check(Bits == 0);
    std::mt19937_64 Random(0x50344d);
    for (std::size_t Index = 0; Index < 10000; ++Index)
    {
        const std::uint64_t Number = Random();
        Check(ParseInteger(std::to_string(Number), true, Bits)); Check(Bits == Number);
        Check(!ParseInteger(std::to_string(Number) + "x", true, Bits));
    }
    for (const char Bad : {'/', ':', '`', 'g', 'A'}) Check(!ValidDigest(std::string(64, Bad)));
    Check(!ValidDigest("")); Check(ValidDigest(std::string(64, '0'))); Check(ValidDigest(std::string(64, '9')));
    Check(ValidDigest(std::string(64, 'a'))); Check(ValidDigest(std::string(64, 'f')));
    Check(CompleteSpans(4, {{0, 1}, {1, 3}})); Check(CompleteSpans(0, {}));
    Check(!CompleteSpans(4, {})); Check(!CompleteSpans(4, {{0, 0}}));
    Check(!CompleteSpans(4, {{1, 3}})); Check(!CompleteSpans(4, {{0, 2}, {1, 2}}));
    Check(!CompleteSpans(4, {{0, 5}}));
    Check(!CompleteSpans(4, {{0, 4}, {4, 1}}));
    Check(!CompleteSpans(4, {{0, 4}, {5, 1}}));
    Check(!CompleteSpans(4, {{0, std::numeric_limits<std::size_t>::max()}}));
    std::cout << Checks << " native checks passed\n";
}

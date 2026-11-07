// Compiled mask coverage (material_source_compiled_coverage.h) on the two NPC hair owners that hold it, and the
// certificate of what was checked for them: the seals of their package, shader map, settings and every retained
// program, which production code no longer holds. A change to the game's files fails here, not in recovery.
//
//     material_compiled_coverage_test <shader archive> <extracted game root>
#include "../core/material_source_owner.h"
#include "../core/material_source_import.h"
#include "../core/native_custom_backend_archive.h"
#include "../core/custom_material_hash.h"
#include <fstream>
#include <iostream>
#include <tuple>
namespace M = P4MapKit::Material;
namespace C = P4MapKit::CustomMaterial;

static unsigned Checks = 0;

static void Check(bool value, const char* what)
{
    ++Checks;
    if (!value)
        throw std::runtime_error(std::string("compiled coverage check failed: ") + what);
}

template <class F>
static void Refuses(F f, const char* what)
{
    bool refused = false;
    try
    {
        f();
    }
    catch (const M::Refused& error)
    {
        refused = error.name == "compiled mask coverage" || error.name == "material source archive association";
    }
    Check(refused, what);
}

static C::Bytes File(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("explicit source missing: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0 || std::uint64_t(size) > (1ull << 31))
        throw std::runtime_error("source bound: " + path);
    input.seekg(0);
    C::Bytes bytes(std::size_t(size), 0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("source read: " + path);
    return bytes;
}

// What was checked for one owner when its coverage was first certified.
struct Seal
{
    const char* Package;
    const char* Frozen;
    const char* Payloads;
    const char* Body;
};

static const Seal Seals[] = {
    {"/Game/Carrot4/MasterMaterial/chr/MI_chr_Npc_Hair_Opaque_00_DN_Dither",
     "a1996e751d801200d2ef74922c764d73474b8eae56c1f37bdecbf353f8c2c986",
     "7e68908edd5f135f7792250efee55bcecd74be9dd92db0b1045d644bc8265d9c",
     "10b2560f37e48a6c36c2412efa809e7adc7b4f7bb2cb911dd4357c0aa8458b13"},
    {"/Game/Carrot4/MasterMaterial/chr/MI_chr_Npc_Hair_Opaque_00_D_Dither",
     "f76c004ef8397b389d3dd9b0f99c0eb8bf6af6cd30725153c2c1112283741644",
     "db437a919f971ed00cf80553da2e833a28039a44e59341692db18a747f57f7f6",
     "cb8093398da396a2aa8fda974481554c1b9679047c20e5cd1b7a9fa039d98081"},
};

// The certificate's seals of one owner, as first recorded: package body, shader map, cooked settings, and every
// retained stage's (frequency, type, factory, resource, permutation, payload) row with the stage counts.
static void CheckSeal(const Seal& seal, const M::MaterialOriginalOwnerSource& owner,
                      const M::MaterialOriginalArchiveAccess& access)
{
    Check(!owner.User.Chain.empty() &&
              owner.User.Chain.front().Package == "/Game/Carrot4/MasterMaterial/chr/M_chr_Npc_Hair_Opaque_00_Master",
          "hair coverage family identity");
    Check(M::PayloadDigest(*owner.Asset.Body) == seal.Body, "package body seal");
    Check(M::PayloadDigest(owner.Map.Frozen) == seal.Frozen, "shader map seal");
    Check(M::PayloadDigest(C::ByteText(owner.User.Settings.Dump())) ==
              "c6e015feaa343a8cc80c26d3198cbf14ffc6f1b15a35c4e3b1f3fb4f8529ddbf",
          "cooked settings and clip seal");
    const auto indices = access.MapIndices(owner.Map.ResourceHash);
    std::vector<std::tuple<unsigned, std::uint64_t, std::uint64_t, std::int32_t, std::int32_t, std::string>> rows;
    std::map<unsigned, unsigned> counts;
    std::set<std::uint64_t> factories;
    for (const auto& ref : owner.References)
    {
        const auto index = indices.at(std::size_t(ref.ResourceIndex));
        rows.emplace_back(ref.Frequency, ref.TypeHash, ref.VertexFactoryHash, ref.ResourceIndex, ref.Permutation,
                          M::PayloadDigest(access.Payload(index)));
        ++counts[ref.Frequency];
        factories.insert(ref.VertexFactoryHash);
    }
    Check(counts == std::map<unsigned, unsigned>{{0, 20}, {3, 16}, {4, 2}}, "retained stage counts");
    Check(factories == std::set<std::uint64_t>{P4MapKit::EngineHash::LocalVertexFactory,
                                               P4MapKit::EngineHash::GpuSkinVertexFactory},
          "retained factories");
    std::sort(rows.begin(), rows.end());
    std::ostringstream text;
    for (const auto& row : rows)
        text << std::dec << std::get<0>(row) << ':' << std::hex << std::get<1>(row) << ':' << std::get<2>(row) << ':'
             << std::dec << std::get<3>(row) << ':' << std::get<4>(row) << ':' << std::get<5>(row) << '\n';
    Check(M::PayloadDigest(C::ByteText(text.str())) == seal.Payloads, "retained program seal");
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("explicit archive and extracted game source root required");
        const auto bytes = File(argv[1]);
        C::NativeBackend::ShaderArchive archive(bytes);
        const std::string root = argv[2];
        M::MaterialOriginalArchiveAccess access;
        access.MapIndices = [&](const std::array<std::uint8_t, 20>& hash)
        {
            return archive.Map(C::Bytes(hash.begin(), hash.end()));
        };
        access.Payload = [&](std::uint32_t index)
        {
            return archive.Payload(index);
        };
        access.Frequency = [&](std::uint32_t index)
        {
            return std::uint8_t(archive.Frequency(index));
        };
        M::MaterialSourceAssetResolver resolve = [&](const std::string& package, M::MaterialSourceAsset& out)
        {
            const auto stem = root + "/Carrot4/Content/" + package.substr(6);
            const M::MaterialOriginalImportRawAsset raw{std::make_shared<const C::Bytes>(File(stem + ".uasset")),
                                                        std::make_shared<const C::Bytes>(File(stem + ".uexp"))};
            out = M::ReadOriginalMaterialImportCustody(raw, package).Asset;
            return true;
        };
        for (const auto& seal : Seals)
        {
            M::MaterialSourceAsset asset;
            resolve(seal.Package, asset);
            const auto owner = M::ReadOriginalMaterialOwnerSource(asset, resolve);
            CheckSeal(seal, owner, access);
            Check(M::CertifyOriginalCompiledCoverage(owner, access), "the certificate holds");
            auto unmasked = owner;
            unmasked.User.Cooked.BlendMode = "BLEND_Opaque";
            Check(!M::CertifyOriginalCompiledCoverage(unmasked, access), "an opaque owner is outside it");
            auto unlit = owner;
            unlit.User.Cooked.ShadingModel = "MSM_Unlit";
            Check(!M::CertifyOriginalCompiledCoverage(unlit, access), "an unlit owner is outside it");
            auto wrongStage = access;
            wrongStage.Frequency = [](std::uint32_t)
            {
                return std::uint8_t(5);
            };
            Refuses([&] { M::CertifyOriginalCompiledCoverage(owner, wrongStage); }, "a stage mismatch refuses");
            auto corrupt = access;
            corrupt.Payload = [&](std::uint32_t index)
            {
                auto result = archive.Payload(index);
                result.at(0) ^= 1;
                return result;
            };
            Refuses([&] { M::CertifyOriginalCompiledCoverage(owner, corrupt); }, "an unreadable program refuses");
        }
        M::MaterialRetainedStageProgram program;
        program.Text = "!!NVfp5.0\nTEMP R0;\nKIL R0.x;\nEND\n";
        Check(M::MaterialProgramLimitsCoverage(program), "a discard limits coverage");
        program.Text = "!!NVvp5.0\nMOV.F result.clip[0], R0.x;\nEND\n";
        Check(M::MaterialProgramLimitsCoverage(program), "a clip distance limits coverage");
        program.Text = "!!NVfp5.0\nMOV.F result.samplemask.x, R0.x;\nEND\n";
        Check(M::MaterialProgramLimitsCoverage(program), "a sample mask limits coverage");
        program.Text = "!!NVfp5.0\nMOV.F result_color0, {0, 0, 0, 0}.x;\nEND\n";
        Check(!M::MaterialProgramLimitsCoverage(program), "a constant colour does not");
        program.Outputs = {"out_ClipDistance0"};
        Check(M::MaterialProgramLimitsCoverage(program), "a declared clip distance output limits coverage");
        std::cout << Checks << " compiled coverage checks passed; raw authored mask remains unidentified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}

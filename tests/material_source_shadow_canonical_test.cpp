// Actual-source host controls for the canonical fully-rough family (material_source_shadow_canonical.h). The
// production rule admits a fully rough lit owner whose base-pass programs read no material data; this test holds the
// certificate of what was checked for the five M_chr_Shadow_Master owners: every program's payload and instruction
// seals and its carrier, normal and fog lines, which production code no longer holds. Inputs stay in their existing
// corpus locations; no converted packages, archives, caches or renderer outputs are copied.
//
//     material_source_shadow_canonical_test <shader archive> <extracted game root> <canonical shadow record>
#include "../core/material_masters.h"
#include "../core/native_custom_backend_archive.h"
#include <fstream>
#include <iostream>
namespace M = P4MapKit::Material;
namespace C = P4MapKit::CustomMaterial;

static int checks = 0;

static void Check(bool value, const char* reason)
{
    ++checks;
    if (!value)
        throw std::runtime_error(reason);
}

template <class F>
static void Refuses(F action, const char* reason)
{
    bool held = false;
    try
    {
        action();
    }
    catch (const M::Refused&)
    {
        held = true;
    }
    Check(held, reason);
}

static C::Bytes File(const std::string& path, std::uint64_t limit)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("source unavailable: " + path);
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size <= 0 || std::uint64_t(size) > limit)
        throw std::runtime_error("source read bound: " + path);
    in.seekg(0);
    C::Bytes bytes(std::size_t(size), 0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("source read failed");
    return bytes;
}

// What was checked for each program of the five owners when the family was first certified.
struct Seal
{
    const char* Payload;
    const char* Instructions;
    std::vector<std::size_t> Carriers;
    std::size_t Normal, Fog;
    int FogVarying;
    std::uint64_t TypeHash;
};

static const std::vector<Seal>& Seals()
{
    static const std::vector<Seal> seals = {{"1aa48c11dc7d34a797ba54a42bbecd5540c089e87f5a21b3b634a1baecf4ebac",
                                             "37483854096f062eb02f2ebbc0779b75146082687429f17d9ffe60b5de0830a5",
                                             {204, 399, 618},
                                             48,
                                             678,
                                             2,
                                             0x18a82ebde5d3d122ull},
                                            {"f3cc699c390a207d83dbfc7c7b942087929635bd1f4531c4d8b1414a150459ad",
                                             "84ebc9ad0e06ccce542efc8e984b707656bd7507e563fa0393a38650b22a676f",
                                             {151, 343, 562},
                                             48,
                                             622,
                                             3,
                                             0x666403914542b619ull},
                                            {"8589f56b07a1770403fb2bebf8606d69769427efaf058d0f6813462246bcd596",
                                             "b032d00aa28cca02423e43c09e6f57bc3157a193772f100aab13a0989be1ba4a",
                                             {259, 478},
                                             29,
                                             537,
                                             2,
                                             0x4507f03a199c6526ull},
                                            {"16696054146458c365473cb1fd3bcfdb9c6bb4d2bd2ed034704e548f89f0ea65",
                                             "b4abee898fba53e009f6598f17d56fd38b068d65b2a146aa3b1695c61ba1704f",
                                             {87, 283, 502},
                                             27,
                                             562,
                                             2,
                                             0xa8d53bcb8fca168dull},
                                            {"8548b2ae30809814c09cc5adace48c57d1f32ee073d79931fe2e32e64013fa23",
                                             "4d353eddca7effb63c1a812c7293b586b61b059218a3f2590b5f9a9bfb24bca0",
                                             {87, 283, 502},
                                             27,
                                             562,
                                             3,
                                             0xa8d53bcb8fca168dull},
                                            {"8f253c4bfed12e4bb5c3c3205d327d6cbb4849c91826a61b4bb9cb3a59d45c7e",
                                             "554b3fc80d793e63c30329be2a0f88496596a57c2d3bbc408de2408ccd2bc938",
                                             {204, 399, 618},
                                             48,
                                             678,
                                             3,
                                             0x18a82ebde5d3d122ull},
                                            {"a289bf803a36f3dfdcadbd885700ae6546a41c495cfc57e7e889b8faac9286f4",
                                             "2079cbb881cc9bbbd820cf2b94feb7ef3648b479a4e91de9dfdf3b42cf69a33b",
                                             {259, 478},
                                             29,
                                             537,
                                             3,
                                             0x4507f03a199c6526ull},
                                            {"92c55b7538c996d9dde0a40ea99dbc62b72864e14aeb709407ab051bc872ba1a",
                                             "6246242d70cb0e9ca1a186fb9de8c108a6a80dc25d21ca265d391bcca0ba61df",
                                             {151, 343, 562},
                                             48,
                                             622,
                                             2,
                                             0x666403914542b619ull}};
    return seals;
}

// The mask programs of the masked owners, by payload.
static const std::set<std::string>& MaskSeals()
{
    static const std::set<std::string> masks = {"23b889f848870f942f8be8be3e6b5eb1150b7715993d416b6ce179dcb2e45968",
                                                "c5431739e199de8015a2bd1c753c8bc21be199db76e4f1c015e3875209a42180",
                                                "b624d8f206778ce329ee30ca29058e5a6badb39e9cb86908990cebe2f27f1ce0",
                                                "b82118db044a64635b42d589cde6f1cfdd588830469973368adf15f4a83418ef"};
    return masks;
}

static bool SealBuffers(const P4MapKit::FMaterialShaderReference& ref)
{
    return ref.NamedBuffers.empty() &&
           ref.UniformBufferHashes == std::map<std::uint16_t, std::uint64_t>{
                                          {0, P4MapKit::EngineHash::ViewUniformBuffer}, {1, 0x60929d69929b2b59ull}};
}

// The certificate of one program: its payload, header, bindings, instructions, carriers and terminals as recorded.
static void CertifySeal(const M::MaterialPreparedProgram& program, const C::Bytes& payload)
{
    const auto digest = M::PayloadDigest(payload);
    const auto found =
        std::find_if(Seals().begin(), Seals().end(), [&](const Seal& seal) { return digest == seal.Payload; });
    if (found == Seals().end())
        throw M::Refused("original Shadow binary/header seal changed", "material Shadow binary seal");
    const auto factory = M::MaterialFactoryName(program.Reference.VertexFactoryHash);
    if (!M::IsMember(factory, {"FLocalVertexFactory", "TGPUSkinVertexFactoryDefault"}) ||
        program.Reference.TypeHash != found->TypeHash || !SealBuffers(program.Reference) ||
        program.BaseBinding.Buffers != std::map<int, std::string>{{0, "View"}, {1, "Primitive"}} ||
        program.BaseBinding.Varyings !=
            std::map<std::string, int>{{"in_texcoord11", 1}, {"in_texcoord7", found->FogVarying}} ||
        !program.BaseBinding.TextureParameters.empty() || !program.BaseBinding.TextureEntries.empty() ||
        !program.BaseBinding.Samplers.empty() || !program.SamplerParameters.empty())
        throw M::Refused("original Shadow native binding metadata changed", "material Shadow binding seal");
    if (C::ShaderHeader(program.Header) != C::ShaderHeader(C::ReadPixelBindings(payload)))
        throw M::Refused("original Shadow prepared header differs from its binary", "material Shadow header seal");
    const auto normalized = M::NormalizeMaterialTraceDeclarations(program.Lines);
    if (M::ProgramDigest(normalized.Lines) != found->Instructions)
        throw M::Refused("original Shadow complete instruction seal changed", "material Shadow instruction seal");
    const auto code = M::ParseMaterialAnchorInstructions(normalized.Lines);
    if (M::OriginalShadowCanonicalCarriers(code) != found->Carriers)
        throw M::Refused("original Shadow fully-rough carrier sites changed", "material Shadow carrier");
    if (found->Normal >= code.size() || code[found->Normal].op != "MUL" || code[found->Normal].dst != "R11" ||
        code[found->Normal].mask != "xyz" || found->Fog >= code.size() || code[found->Fog].op != "MAD" ||
        code[found->Fog].mask != "xyz" || code[found->Fog].src.size() != 3)
        throw M::Refused("original Shadow geometric normal/fog terminal changed", "material Shadow native terminals");
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error(
                "usage: shadow-canonical-test <archive> <extracted-root> <canonical-shadow-record>");
        const auto archiveBytes = File(argv[1], 1ull << 31);
        C::NativeBackend::ShaderArchive archive(archiveBytes);
        const std::string root = argv[2];
        M::MaterialMasterSources sources;
        sources.Raw = [&](const std::string& package, M::MaterialOriginalImportRawAsset& raw)
        {
            if (package.compare(0, 6, "/Game/") != 0)
                return false;
            const auto stem = root + "/Carrot4/Content/" + package.substr(6);
            std::ifstream exists(stem + ".uasset", std::ios::binary);
            if (!exists)
                return false;
            raw = {std::make_shared<const C::Bytes>(File(stem + ".uasset", 64u * 1024u * 1024u)),
                   std::make_shared<const C::Bytes>(File(stem + ".uexp", 64u * 1024u * 1024u))};
            return true;
        };
        const auto resolver = sources.Resolver();
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
        const std::vector<std::string> instances = {
            "/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master", "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow",
            "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve",
            "/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow",
            "/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow"};
        std::size_t policies = 0;
        const auto scope = M::MaterialDomainSourceScope::ReadOnlyExpressions;
        for (const auto& package : instances)
        {
            M::MaterialSourceAsset asset;
            Check(resolver(package, asset), "selected source exists");
            const auto chain = M::ReadOriginalMaterialParentChain(asset, resolver);
            std::string shaderOwner;
            for (auto record = chain.rbegin(); record != chain.rend() && shaderOwner.empty(); ++record)
            {
                bool own = record->Class == "Material";
                if (!own)
                    if (const auto flag = record->Properties.Field("bHasStaticPermutationResource"))
                        Check(P4MapKit::MaterialBool(*flag, own), "static permutation source flag invalid");
                if (own)
                    shaderOwner = record->Package;
            }
            Check(!shaderOwner.empty() && resolver(shaderOwner, asset), "actual inherited shader owner exists");
            const auto owner = M::ReadOriginalMaterialOwnerSource(asset, resolver);
            Check(M::OriginalShadowCanonicalOwner(owner, access, scope), "the production rule admits the owner");
            const auto refs = M::OriginalShadowCanonicalReferences(owner);
            Check(refs.size() == 8, "all original base policies/factories retained");
            const auto indices = access.MapIndices(owner.Map.ResourceHash);
            for (const auto& ref : refs)
            {
                const auto payload = access.Payload(indices.at(std::size_t(ref.ResourceIndex)));
                const auto program = M::PrepareOriginalMaterialArchiveProgram(owner, ref, access, scope).Program;
                CertifySeal(program, payload);
                Check(M::OriginalShadowCanonicalProgram(program), "the production rule admits the program");
                ++policies;
                // Every positive is paired with exact program, binary/header, uniform/factory and varying identity
                // tampering controls of the certificate, and material-data controls of the production rule.
                auto changed = program;
                changed.Lines[0] = "MOV.F R99.x, {0}";
                Refuses([&] { CertifySeal(changed, payload); }, "changed program accepted");
                auto binary = payload;
                binary[0] ^= 1;
                Refuses([&] { CertifySeal(program, binary); }, "changed binary/header accepted");
                changed = program;
                changed.Header.layout_hashes[0] ^= 1;
                Refuses([&] { CertifySeal(changed, payload); }, "changed prepared header accepted");
                changed = program;
                changed.BaseBinding.Buffers[0] = "Material";
                Refuses([&] { CertifySeal(changed, payload); }, "changed binding accepted");
                Check(!M::OriginalShadowCanonicalProgram(changed), "a program reading Material is admitted");
                changed = program;
                changed.BaseBinding.TextureParameters[0] = "BaseTexture";
                Check(!M::OriginalShadowCanonicalProgram(changed), "a program sampling a texture is admitted");
                changed = program;
                changed.BaseBinding.Varyings["in_texcoord11"] = 7;
                Refuses([&] { CertifySeal(changed, payload); }, "changed varying accepted");
                changed = program;
                changed.Reference.NamedBuffers[1] = "Material";
                Refuses([&] { CertifySeal(changed, payload); }, "changed reference metadata accepted");
                changed = program;
                changed.Reference.UniformBufferHashes[0] ^= 1;
                Refuses([&] { CertifySeal(changed, payload); }, "changed original uniform hash accepted");
                changed = program;
                changed.Reference.TypeHash ^= 1;
                Refuses([&] { CertifySeal(changed, payload); }, "changed original shader type accepted");
            }
            if (owner.User.Cooked.BlendMode == "BLEND_Masked")
            {
                const auto depth =
                    M::SelectOriginalMaterialDepthProgram(owner, M::SelectOriginalMaterialPrimaryProgram(owner));
                Check(MaskSeals().count(
                          M::PayloadDigest(access.Payload(indices.at(std::size_t(depth.ResourceIndex))))) != 0,
                      "original Shadow mask binary seal changed");
            }
            auto changed = owner;
            changed.User.Settings["render_settings"]["bFullyRough"] = false;
            Refuses([&] { M::RecoverOriginalShadowCanonicalExpressionGraph(changed, access); },
                    "changed fully-rough setting accepted");
            Check(!M::OriginalShadowCanonicalOwner(changed, access, scope), "a not fully rough owner is admitted");
            changed = owner;
            changed.User.Settings["shading_model"] = "MSM_Unlit";
            Refuses([&] { M::RecoverOriginalShadowCanonicalExpressionGraph(changed, access); },
                    "unlit substitution accepted");
            const auto result = M::RecoverOriginalMaterialSourceExpressionGraph(owner, access, scope);
            Check(!result.ProductionAdmitted && !result.RendererProven && !result.CompilerProven &&
                      !result.FamilyComplete,
                  "compiled carrier promoted to whole-renderer proof");
            Check(result.Primary.Material.Graph.At("representation").Text() ==
                      "canonical compiled fully-rough lighting; authored raw pins unidentified",
                  "authored factorization lost");
            Check(result.Primary.Material.Graph.At("render_settings").At("bUsedWithSkeletalMesh").boolean,
                  "actual original usage lost");
            std::cout << package
                      << " source-policies=8 graph-nodes=" << result.Primary.Material.Graph.At("nodes").object.size()
                      << '\n';
        }
        Check(policies == 40, "all 5 source owners/policies were not checked");
        const auto family = M::RecoverMaterialMaster("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master", instances,
                                                     sources, access);
        Check(family.HasGraph && family.Refused.empty() && family.RefusedMerge.empty() && family.Users.size() == 5,
              "source family not fully covered");
        const auto canonicalBytes = File(argv[3], 64u * 1024u * 1024u);
        const auto records = M::Json::Parse(std::string(canonicalBytes.begin(), canonicalBytes.end()));
        Check(records.At("package").Text() == "/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master",
              "historical record has another identity");
        const auto& expected = records.At("graph");
        M::MaterialMasterFingerprint oldGraph{expected, nullptr, {}}, newGraph{family.Merge.Graph, nullptr, {}};
        Check(expected.At("outputs").object.size() == family.Merge.Graph.At("outputs").object.size(),
              "historical output inventory changed");
        for (const auto& output : expected.At("outputs").object)
            Check(oldGraph.Of(output.second) == newGraph.Of(family.Merge.Graph.At("outputs").At(output.first)),
                  "source family differs from historical canonical graph");
        std::cout << checks << " checks passed; 5 owners/40 policies, full source/historical canonical graph parity\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

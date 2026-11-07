// Whole-game agreement of the material source producers' admission rules. Every owner of every master in the
// extracted game's material index is routed twice: by the production producer table, and by the same table with
// the retired master-name rules put back. Both must route every owner the same way. The retired rules live here
// only; they name game assets, which production code no longer does. Reads game files; writes nothing.
//
//     material_source_gates_test <shader archive> <extracted game root> <material index json> [master filter] [--full]
//         [--shard=K/N]
//
// By default the rules are evaluated without running the producers; the completion rules that read a result then see
// an empty graph. --full runs the production table on every owner (as long as the whole-game golden) so they see the
// real result. --shard=K/N takes every Nth master from the Kth, for runs in parallel.
// The material index is private/material-re/exactness/instances.json; masters and their instances are grouped as
// native_material_masters_parity.py groups them for the whole-game golden.
#include "../core/material_masters.h"
#include "../core/native_custom_backend_archive.h"
#include <fstream>
#include <iostream>
namespace M = P4MapKit::Material;
namespace C = P4MapKit::CustomMaterial;
using D = M::MaterialSourceDispatch;
using Admission = bool (*)(const D&);

static C::Bytes File(const std::string& path, std::uint64_t bound)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("file unavailable: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || std::uint64_t(size) > bound)
        throw std::runtime_error("file outside its bound: " + path);
    input.seekg(0);
    C::Bytes bytes(std::size_t(size), 0);
    if (size && !input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("file unreadable: " + path);
    return bytes;
}

static bool RootIn(const D& d, std::initializer_list<const char*> masters)
{
    if (d.Owner.User.Chain.empty())
        return false;
    for (const auto* master : masters)
        if (d.Owner.User.Chain.front().Package == master)
            return true;
    return false;
}

// The retired rules, by the producer they admitted for.
static const std::map<std::string, Admission>& RetiredRules()
{
    static const std::map<std::string, Admission> rules = {
        {"original canonical Shadow",
         [](const D& d)
         {
             const auto& package = d.Owner.Asset.Package.Package;
             return RootIn(d, {"/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master"}) &&
                    (package == "/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master" ||
                     package == "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow" ||
                     package == "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve" ||
                     package == "/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow" ||
                     package == "/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow");
         }},
        {"compiled mask coverage",
         [](const D& d)
         {
             const auto& package = d.Owner.Asset.Package.Package;
             return package == "/Game/Carrot4/MasterMaterial/chr/MI_chr_Npc_Hair_Opaque_00_DN_Dither" ||
                    package == "/Game/Carrot4/MasterMaterial/chr/MI_chr_Npc_Hair_Opaque_00_D_Dither";
         }},
        {"sky disk scene rewrites",
         [](const D& d) { return RootIn(d, {"/Game/Carrot4/MasterMaterial/stg/Sky/M_Sky_Panning_Clouds2_Master"}); }},
        {"anisotropy",
         [](const D& d)
         {
             return RootIn(d, {"/Game/Carrot4/MasterMaterial/obj/M_obj_Terrain_Anisotropy_Opaque_00_Master",
                               "/Game/Carrot4/MasterMaterial/obj/Otakara/M_obj_Otakara_Anisotropy_Opaque_00_Master"}) &&
                    !d.Result.Primary.Material.Graph.At("outputs").Has("Anisotropy") &&
                    !d.Result.Primary.Material.Graph.At("outputs").Has("Tangent");
         }},
        {"Ice reflection",
         [](const D& d) { return RootIn(d, {"/Game/Carrot4/MasterMaterial/eft/M_eft_Ice_als_Master"}); }},
        {"original-DefaultLit-particle authored reflection",
         [](const D& d)
         {
             return M::MaterialSourceParticleSurface(d, "MSM_DefaultLit") &&
                    RootIn(d, {"/Game/Carrot4/MasterMaterial/eft/M_eft_Swamp00_alcfr_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Honey00_alidpfrf_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Swamp01_alcfr_Master"});
         }},
        {"original-Black-fog-only",
         [](const D& d)
         {
             return RootIn(d, {"/Game/Carrot4/MasterMaterial/eft/M_eft_Black_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Shadow00_o_Master"});
         }},
        {"original-Subsurface-particle",
         [](const D& d)
         {
             return M::MaterialSourceParticleSurface(d, "MSM_Subsurface") &&
                    RootIn(d, {"/Game/Carrot4/MasterMaterial/eft/M_eft_Ice_o_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Ice_als_Master"});
         }},
        {"original-TwoSidedFoliage-particle",
         [](const D& d)
         {
             return M::MaterialSourceParticleSurface(d, "MSM_TwoSidedFoliage") &&
                    RootIn(d, {"/Game/Carrot4/MasterMaterial/eft/M_eft_Dust_ac_TSF_VPVND_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Plants_alnsfr_Master"});
         }},
        {"original-folded-lighting",
         [](const D& d)
         {
             return RootIn(d, {"/Game/Carrot4/MasterMaterial/eft/M_eft_Water00_alcfr_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Water01_alcfr_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Water01_alcfr_lod_Master",
                               "/Game/Carrot4/MasterMaterial/eft/M_eft_Water01_alscfr_Master",
                               "/Game/Carrot4/MasterMaterial/obj/M_obj_Bubble_Frozen_Translucent_00_Master",
                               "/Game/Carrot4/MasterMaterial/obj/M_obj_Bubble_Translucent_00_Master"});
         }},
    };
    return rules;
}

// Owners a data rule admits that its retired name rule did not, by producer: each is a behavior change, accepted
// only with its evidence. The route test fails on any other difference.
static const std::map<std::string, std::string>& KnownAdditions()
{
    // original-Black-fog-only: the same fog-only program and settings as the two retired masters. The generic owner
    // graph already gave each the fog-only producer's EmissiveColor topology and settings (whole-game golden).
    static const std::map<std::string, std::string> additions = {
        {"/Game/Carrot4/MasterMaterial/Demo/M_demo_Black_Master", "original-Black-fog-only"},
        {"/Game/Carrot4/MasterMaterial/chr/M_chr_Black_Master", "original-Black-fog-only"},
        {"/Game/Carrot4/MasterMaterial/obj/M_obj_Black_Master", "original-Black-fog-only"},
        {"/Game/Carrot4/MasterMaterial/obj/M_obj_Shadow_Master", "original-Black-fog-only"},
        {"/Game/Carrot4/MasterMaterial/obj/Otakara/M_obj_Otakara_Shadow_Master", "original-Black-fog-only"},
        {"/Game/Carrot4/MasterMaterial/stg/M_stg_Black_Master", "original-Black-fog-only"},
        {"/Game/Carrot4/MasterMaterial/stg/M_stg_Shadow_Master", "original-Black-fog-only"},
        {"/Game/Carrot4/MasterMaterial/Unique/Black/M_Black_Master", "original-Black-fog-only"},
        // fog wrapper cut: unlit opaque programs whose standard vertex fog adds to the clamped colour. Each recovers
        // to the same bytes as through the unlit scene reading (whole-game golden).
        {"/Game/Carrot4/MasterMaterial/Collision/M_Collision_Wireframe_Master", "unlit fog wrapper cut"},
        {"/Game/Carrot4/MasterMaterial/Demo/M_demo_Opaque_Unlit_00_Master", "unlit fog wrapper cut"},
        {"/Game/Carrot4/MasterMaterial/UI/Surface/M_ui_Title_Movie_Opaque_Unlit_00_Master", "unlit fog wrapper cut"},
        {"/Game/Carrot4/MasterMaterial/stg/M_stg_Opaque_Unlit_00_Master", "unlit fog wrapper cut"},
        {"/Game/Carrot4/MasterMaterial/stg/Sky/M_Sky_Panning_Clouds2_Master", "unlit fog wrapper cut"},
        {"/Game/Carrot4/Terrain/Sky/Area003/MI_stg_Area003_Day_Sky_Panning_Clouds2", "unlit fog wrapper cut"},
        {"/Game/Carrot4/Terrain/Sky/Area004/MI_stg_Area004_Day_Sky_Panning_Clouds2", "unlit fog wrapper cut"},
        {"/Game/Carrot4/Terrain/Sky/Area006/MI_stg_Area006_Day_Sky_Panning_Clouds2", "unlit fog wrapper cut"},
        {"/Game/Carrot4/Terrain/Sky/Area_Common/MI_stg_Area_Common_Day_Sky_Panning_Clouds2", "unlit fog wrapper cut"},
        {"/Game/Carrot4/Terrain/Sky/Area_Common/MI_stg_Area_Common_Night_Sky_Panning_Clouds2", "unlit fog wrapper cut"},
        {"/Game/Carrot4/Terrain/Sky/Ending_Area500/MI_stg_Ending_Area500_Day_Sky_Panning_Clouds2",
         "unlit fog wrapper cut"},
        // original-TwoSidedFoliage-particle: a two-sided foliage particle surface. Both owners recover to the same
        // bytes as before (whole-game golden).
        {"/Game/Carrot4/MasterMaterial/eft/M_eft_Buddy_Eye_Master", "original-TwoSidedFoliage-particle"},
        {"/Game/Carrot4/Effects/Buddy/Models/MI_eft_Buddy_Eye00", "original-TwoSidedFoliage-particle"},
        // original-Subsurface-particle: a subsurface particle surface. Its BaseColor changes and now matches the Python
        // reference decompiler (the generic owner graph did not); in-game check pending.
        {"/Game/Carrot4/MasterMaterial/eft/M_eft_Kohaku_o_Master", "original-Subsurface-particle"},
    };
    return additions;
}

// True when the two routes differ only by a known addition: the data route runs the added producer where the name
// route ran another producer, and every other step is the same.
static bool KnownAddition(const std::string& package, const std::vector<std::string>& before,
                          const std::vector<std::string>& now)
{
    const auto known = KnownAdditions().find(package);
    if (known == KnownAdditions().end() || before.size() != now.size())
        return false;
    std::size_t changed = 0;
    for (std::size_t i = 0; i < now.size(); ++i)
        if (now[i] != before[i])
        {
            if (now[i] != known->second)
                return false;
            ++changed;
        }
    return changed == 1;
}

// Both routes of one owner while the production table really runs (the full mode): before each step, which steps each
// table admits there. Rules that read the result (the completion steps) see the real result. A step that refuses ends
// both routes with the refusal.
static void FullRoutes(const std::vector<M::MaterialSourceProducer<D>>& production,
                       const std::vector<M::MaterialSourceProducer<D>>& retired, D& d, std::vector<std::string>& now,
                       std::vector<std::string>& before)
{
    bool producedNow = false, producedBefore = false;
    const auto admits =
        [&d](const M::MaterialSourceProducer<D>& producer, bool produced, std::vector<std::string>& route)
    {
        if (producer.Stage == M::MaterialSourceStage::Produce && produced)
            return false;
        try
        {
            if (!producer.Admits(d))
                return false;
        }
        catch (const std::exception& error)
        {
            route.push_back(std::string(producer.Name) + " threw: " + error.what());
            return false;
        }
        route.push_back(producer.Name);
        return true;
    };
    for (std::size_t i = 0; i < production.size(); ++i)
    {
        const bool run = admits(production[i], producedNow, now);
        if (admits(retired[i], producedBefore, before) && retired[i].Stage == M::MaterialSourceStage::Produce)
            producedBefore = true;
        if (!run)
            continue;
        if (production[i].Stage == M::MaterialSourceStage::Produce)
            producedNow = true;
        try
        {
            production[i].Run(d);
        }
        catch (const std::exception& error)
        {
            now.push_back(std::string("refused: ") + error.what());
            before.push_back(std::string("refused: ") + error.what());
            return;
        }
    }
}

// The retired rules of the unlit program table (material_source_unlit_producers.h).
using U = M::MaterialUnlitSceneDispatch;
static const std::map<std::string, bool (*)(const U&)>& RetiredUnlitRules()
{
    static const std::map<std::string, bool (*)(const U&)> rules = {
        {"fog wrapper cut",
         [](const U& d)
         {
             return !d.User.Chain.empty() &&
                    d.User.Chain.front().Package == "/Game/Carrot4/MasterMaterial/stg/M_stg_Black_Soft_Opaque_Master";
         }},
        {"capture off",
         [](const U& d)
         {
             return d.Program.Lines.size() == 75 &&
                    M::ProgramDigest(d.Program.Lines) ==
                        "2e96f4db54ed718a466bf851c92d85dd5f6b7773fae77094f8986b953bfe718b";
         }},
    };
    return rules;
}

// The producers a table admits for one owner, in the order RunMaterialSourceProducers runs them.
template <class Dispatch>
static std::vector<std::string> Route(const std::vector<M::MaterialSourceProducer<Dispatch>>& table, const Dispatch& d)
{
    std::vector<std::string> out;
    bool produced = false;
    for (const auto& producer : table)
    {
        if (producer.Stage == M::MaterialSourceStage::Produce && produced)
            continue;
        bool admitted = false;
        try
        {
            admitted = producer.Admits(d);
        }
        catch (const std::exception& error)
        {
            out.push_back(std::string(producer.Name) + " threw: " + error.what());
            continue;
        }
        if (!admitted)
            continue;
        out.push_back(producer.Name);
        if (producer.Stage == M::MaterialSourceStage::Produce)
            produced = true;
    }
    return out;
}

static std::string Joined(const std::vector<std::string>& names)
{
    std::string out;
    for (const auto& name : names)
        out += (out.empty() ? "" : ", ") + name;
    return out;
}

// {master: its instances} as native_material_masters_parity.game_families groups the material index.
static std::map<std::string, std::vector<std::string>> GameFamilies(const M::Json& index)
{
    std::map<std::string, std::string> cased;
    for (const auto& item : index.object)
    {
        std::string lower = item.first;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        cased[lower] = item.first;
    }
    const auto parent = [&](const std::string& package)
    {
        const auto found = index.object.find(package);
        if (found == index.object.end() || found->second.Has("error") || !found->second.Has("parent") ||
            found->second.At("parent").kind != M::Json::String || found->second.At("parent").string.empty())
            return std::string();
        std::string lower = found->second.At("parent").string;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        const auto spelled = cased.find(lower);
        return spelled == cased.end() ? found->second.At("parent").string : spelled->second;
    };
    std::map<std::string, std::vector<std::string>> families;
    for (const auto& item : index.object)
        if (!item.second.Has("error") && parent(item.first).empty())
            families[item.first];
    for (const auto& item : index.object)
    {
        if (item.second.Has("error") || parent(item.first).empty())
            continue;
        std::string root = item.first;
        std::set<std::string> seen;
        while (!parent(root).empty() && seen.insert(root).second)
            root = parent(root);
        const auto family = families.find(root);
        if (family != families.end())
            family->second.push_back(item.first);
    }
    return families;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 4)
            throw std::runtime_error("shader archive, extracted game root and material index required");
        const auto bytes = File(argv[1], 1ull << 31);
        C::NativeBackend::ShaderArchive shaders(bytes);
        const std::string root = argv[2];
        std::string filter;
        bool full = false;
        int shard = 0, shards = 1;
        for (int i = 4; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--full")
                full = true;
            else if (arg.compare(0, 8, "--shard=") == 0)
            {
                shard = std::stoi(arg.substr(8));
                shards = std::stoi(arg.substr(arg.find('/') + 1));
            }
            else
                filter = arg;
        }
        const auto indexBytes = File(argv[3], 1ull << 28);
        const auto families = GameFamilies(M::Json::Parse(std::string(indexBytes.begin(), indexBytes.end())));
        M::MaterialOriginalArchiveAccess archive;
        archive.MapIndices = [&](const std::array<std::uint8_t, 20>& hash)
        {
            return shaders.Map(C::Bytes(hash.begin(), hash.end()));
        };
        archive.Payload = [&](std::uint32_t index)
        {
            return shaders.Payload(index);
        };
        archive.Frequency = [&](std::uint32_t index)
        {
            return std::uint8_t(shaders.Frequency(index));
        };
        const auto& production = M::MaterialSourceProducers();
        auto retired = production;
        std::set<std::string> replaced;
        // Rules that read only the recovered graph mean nothing without --full: both tables keep the production rule.
        const std::set<std::string> resultRules = {"Ice reflection"};
        for (auto& producer : retired)
        {
            const auto rule = RetiredRules().find(producer.Name);
            if (rule == RetiredRules().end())
                continue;
            if (!full && resultRules.count(producer.Name))
            {
                replaced.insert(producer.Name);
                continue;
            }
            producer.Admits = rule->second;
            replaced.insert(producer.Name);
        }
        if (replaced.size() != RetiredRules().size())
            throw std::runtime_error("a retired rule names no producer of the production table");
        auto retiredUnlit = M::MaterialUnlitSceneProducers();
        for (auto& producer : retiredUnlit)
        {
            const auto rule = RetiredUnlitRules().find(producer.Name);
            if (rule == RetiredUnlitRules().end())
                continue;
            producer.Admits = rule->second;
            replaced.insert(producer.Name);
        }
        if (replaced.size() != RetiredRules().size() + RetiredUnlitRules().size())
            throw std::runtime_error("a retired rule names no producer of the unlit program table");
        const std::function<void(M::SymbolicArena&, M::BindingContext&)> binding;
        std::map<std::string, std::pair<int, int>> admitted;
        int masters = 0, owners = 0, unread = 0, accepted = 0, disagreements = 0;
        int ordinal = -1;
        for (const auto& family : families)
        {
            if (!filter.empty() && family.first.find(filter) == std::string::npos)
                continue;
            if (++ordinal % shards != shard)
                continue;
            ++masters;
            M::MaterialMasterSources sources;
            sources.Raw = [&root](const std::string& package, M::MaterialOriginalImportRawAsset& out)
            {
                if (package.compare(0, 6, "/Game/") != 0)
                    return false;
                const auto stem = root + "/Carrot4/Content/" + package.substr(6);
                std::ifstream exists(stem + ".uasset", std::ios::binary);
                if (!exists)
                    return false;
                out = {std::make_shared<const C::Bytes>(File(stem + ".uasset", 64u * 1024u * 1024u)),
                       std::make_shared<const C::Bytes>(File(stem + ".uexp", 64u * 1024u * 1024u))};
                return true;
            };
            const auto resolve = sources.Resolver();
            // The owners RecoverMaterialMaster recovers: the master, and every instance that owns its shaders.
            std::vector<std::string> ownerPackages{family.first};
            for (const auto& package : family.second)
            {
                try
                {
                    M::MaterialSourceAsset asset;
                    if (!resolve(M::MaterialSourcePackage(package), asset))
                        continue;
                    const auto chain = M::ReadOriginalMaterialParentChain(asset, resolve);
                    if (chain.empty() || chain.back().Package != asset.Package.Package)
                        continue;
                    bool own = chain.back().Class == "Material";
                    if (!own)
                        if (const auto flag = chain.back().Properties.Field("bHasStaticPermutationResource"))
                            P4MapKit::MaterialBool(*flag, own);
                    if (own)
                        ownerPackages.push_back(asset.Package.Package);
                }
                catch (const std::exception&)
                {
                }
            }
            for (const auto& package : ownerPackages)
            {
                M::MaterialOriginalOwnerSource owner;
                P4MapKit::FMaterialShaderReference ordinary;
                try
                {
                    M::MaterialSourceAsset asset;
                    if (!resolve(M::MaterialSourcePackage(package), asset))
                        throw std::runtime_error("unresolved");
                    owner = M::ReadOriginalMaterialOwnerSource(asset, resolve, sources.Collections(asset));
                    ordinary = M::SelectOriginalMaterialPrimaryProgram(owner);
                }
                catch (const std::exception&)
                {
                    ++unread;
                    continue;
                }
                ++owners;
                const M::MaterialSurfaceRewrites rewrites;
                D d{owner,
                    archive,
                    M::MaterialDomainSourceScope::ReadOnlyExpressions,
                    binding,
                    ordinary,
                    rewrites,
                    {},
                    M::Json::MakeObject()};
                std::vector<std::string> now, before;
                if (full)
                    FullRoutes(production, retired, d, now, before);
                else
                {
                    d.Result.Primary.Material.Graph =
                        M::Object({{"nodes", M::Json::MakeObject()}, {"outputs", M::Json::MakeObject()}});
                    now = Route(production, d);
                    before = Route(retired, d);
                }
                for (const auto& name : replaced)
                {
                    if (std::find(before.begin(), before.end(), name) != before.end())
                        ++admitted[name].first;
                    if (std::find(now.begin(), now.end(), name) != now.end())
                        ++admitted[name].second;
                }
                // The unlit program table, for an unlit surface's program of record.
                const auto& cooked = owner.User.Cooked;
                if (!full && cooked.Domain == "MD_Surface" && cooked.ShadingModel == "MSM_Unlit" &&
                    M::IsMember(cooked.BlendMode, {"BLEND_Opaque", "BLEND_Translucent", "BLEND_Additive"}))
                {
                    try
                    {
                        const auto program =
                            M::PrepareOriginalMaterialArchiveProgram(owner, ordinary, archive,
                                                                     M::MaterialDomainSourceScope::ReadOnlyExpressions)
                                .Program;
                        const M::MaterialRecoveryPlan plan;
                        const U unlit{owner.User, plan, program, {}};
                        for (const auto& name : Route(M::MaterialUnlitSceneProducers(), unlit))
                            now.push_back("unlit " + name);
                        for (const auto& name : Route(retiredUnlit, unlit))
                            before.push_back("unlit " + name);
                    }
                    catch (const std::exception& error)
                    {
                        now.push_back(std::string("unlit program unreadable: ") + error.what());
                        before.push_back(std::string("unlit program unreadable: ") + error.what());
                    }
                }
                for (const auto& name : replaced)
                {
                    if (std::find(before.begin(), before.end(), "unlit " + name) != before.end())
                        ++admitted[name].first;
                    if (std::find(now.begin(), now.end(), "unlit " + name) != now.end())
                        ++admitted[name].second;
                }
                if (now == before)
                    continue;
                if (KnownAddition(package, before, now))
                {
                    ++accepted;
                    continue;
                }
                ++disagreements;
                std::cout << "differs: " << package << "\n    name rules: " << Joined(before)
                          << "\n    data rules: " << Joined(now) << "\n";
            }
        }
        for (const auto& item : admitted)
            std::cout << item.first << ": name rules admit " << item.second.first << ", data rules admit "
                      << item.second.second << "\n";
        std::cout << masters << " masters, " << owners << " owners routed, " << unread << " owners unreadable, "
                  << accepted << " known additions, " << disagreements << " routed differently\n";
        return disagreements ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}

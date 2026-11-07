// One graph per master material, with its static switches (tools/navigation/material_masters.py).
//
// The game compiles a master again for every combination of static switch values its instances use.
// Each material with shaders of its own (the master, and every instance with a static permutation) is
// recovered from its own shaders, then the graphs are put back together: nodes that are the same become
// one, and where a material value differs a StaticSwitchParameter picks it, named after a switch whose
// values tell the graphs apart. A switch's default is the opposite of the one value instances set it to;
// a switch set both ways defaults to the first value (on before off) under which no two graphs contradict.
//
// The merged graph is checked against every graph it was made from: with each owner's switch values it
// has to give that owner's own formula for every pin the owner's shaders compute. An owner that changes
// what the whole graph is compiled under (sidedness, shading model, a blend mode other than opaque or
// masked) does not shape the graph and is refused by name when the graph set its way is not its own.
// Nothing here is evaluated numerically; what is not equal is refused.
#pragma once
#include "material_source_import.h"
#include "material_camera_fade.h"
#include "material_game_reads.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iomanip>
#include <set>
#include "material_masters_merge.h"
#include "material_masters_variants.h"
namespace P4MapKit
{
namespace Material
{
// The switch values an instance chain sets, nearest first, as material_exactness.Game.merged(p, 'static').
inline std::map<std::string, MaterialMasterStatic>
MaterialMasterChainStatics(const std::vector<FMaterialCookedRecord>& rootFirst)
{
    std::map<std::string, MaterialMasterStatic> out;
    for (auto record = rootFirst.rbegin(); record != rootFirst.rend(); ++record)
    {
        const auto statics = record->Properties.Field("StaticParameters");
        if (!statics)
            continue;
        if (statics->Kind != FDecodedValue::EKind::Object)
            throw Refused("static parameter set is not typed object", "master static parameters");
        for (const auto* field : {"StaticSwitchParameters", "StaticComponentMaskParameters"})
        {
            const auto rows = statics->Field(field);
            if (!rows)
                continue;
            if (rows->Kind != FDecodedValue::EKind::Array || rows->Items.size() > 4096)
                throw Refused("static parameter list is outside its bound", "master static parameters");
            for (const auto& row : rows->Items)
            {
                const auto name = OriginalMaterialImportParameterName(row);
                if (out.count(name))
                    continue;
                MaterialMasterStatic value;
                if (std::string(field) == "StaticComponentMaskParameters")
                    value.Mask = true;
                else if (const auto flag = row.Field("Value"))
                {
                    if (!MaterialBool(*flag, value.Value))
                        throw Refused("static switch value is not boolean", "master static parameters");
                }
                out[name] = value;
            }
        }
    }
    return out;
}

// Asset access for one recovery: every package read once, from the creator's game files.
struct MaterialMasterSources
{
    MaterialOriginalImportRawResolver Raw;
    std::map<std::string, MaterialSourceAsset> Assets;
    // An owner graph already recovered from exactly these inputs (the owner, its collections and the
    // caller's archive), or false. The caller proves the inputs are the same; nothing here trusts a name.
    std::function<bool(const MaterialSourceAsset&, const std::vector<MaterialSourceAsset>&, Json&)> KnownOwnerGraph;
    MaterialSourceAssetResolver Resolver()
    {
        return [this](const std::string& object, MaterialSourceAsset& value)
        {
            const auto package = MaterialSourcePackage(object), key = PropertyIdentity(package);
            auto found = Assets.find(key);
            if (found == Assets.end())
            {
                MaterialOriginalImportRawAsset raw;
                if (!Raw || !Raw(package, raw))
                    return false;
                found = Assets.emplace(key, ReadOriginalMaterialImportCustody(raw, package).Asset).first;
            }
            value = found->second;
            return true;
        };
    }
    // The parameter collections a material chain names (the cached collection references, not path spelling).
    std::vector<MaterialSourceAsset> Collections(const MaterialSourceAsset& asset)
    {
        const auto resolve = Resolver();
        std::set<std::string> references, found;
        std::vector<MaterialSourceAsset> out;
        for (const auto& record : ReadOriginalMaterialParentChain(asset, resolve))
            OriginalMaterialImportReferences(record.Properties, references);
        for (const auto& path : references)
        {
            MaterialSourceAsset candidate;
            if (!resolve(MaterialSourcePackage(path), candidate))
                continue;
            std::string type;
            FDecodedValue properties;
            if (MaterialSourceReadObject(candidate, {"MaterialParameterCollection"}, type, properties) &&
                found.insert(PropertyIdentity(candidate.Package.Package)).second)
                out.push_back(candidate);
        }
        return out;
    }
};

inline Json RecoverMaterialMasterOwnerGraph(MaterialMasterSources& sources, const MaterialSourceAsset& asset,
                                            const MaterialOriginalArchiveAccess& archive)
{
    const auto collections = sources.Collections(asset);
    Json graph;
    const bool known = sources.KnownOwnerGraph && sources.KnownOwnerGraph(asset, collections, graph);
    const auto owner = ReadOriginalMaterialOwnerSource(asset, sources.Resolver(), collections);
    if (!known)
        graph =
            RecoverOriginalMaterialSourceExpressionGraph(owner, archive, MaterialDomainSourceScope::ReadOnlyExpressions)
                .Primary.Material.Graph;
    return RecoverMaterialCameraFadeGraph(owner, archive, graph);
}

// The graph with its static switches set to values: every link through a switch goes to the side the value
// picks. Nodes are kept; only links change.
inline Json MaterialMasterResolveSwitches(Json graph, const std::map<std::string, bool>& values)
{
    auto& nodes = graph["nodes"];
    const auto through = [&](Json link)
    {
        for (std::size_t step = 0; step < 4096; ++step)
        {
            MaterialMasterCheckLink(link);
            const auto& node = MaterialMasterNode(graph, link.array[0].Text());
            if (node.At("type").Text() != "StaticSwitchParameter" || link.array.size() != 2 ||
                link.array[1].kind != Json::String || !link.array[1].string.empty())
                return link;
            const auto name = node.At("props").At("ParameterName").Text();
            const auto value = values.find(name);
            if (value == values.end())
                throw Refused("merged graph switch has no value for an owner: " + name, "master merge check");
            const char* side = value->second ? "A" : "B";
            if (!node.At("inputs").Has(side))
                throw Refused("merged switch lacks a side", "master merge check");
            link = node.At("inputs").At(side);
        }
        throw Refused("switches nest deeper than the check follows", "master merge check");
    };
    for (auto& item : nodes.object)
        if (item.second.Has("inputs"))
            for (auto& input : item.second["inputs"].object)
                input.second = through(input.second);
    for (auto& pin : graph["outputs"].object)
        pin.second = through(pin.second);
    return graph;
}
// True when a one-sided owner of a two-sided master draws what the master's graph, set as the owner sets it,
// draws on the faces a one-sided material has: Unreal compiles a one-sided material's TwoSidedSign as 1
// (MaterialTemplate.ush: the sign is -1 only on a two-sided material's back faces), so its shader holds the
// two-sided formulas with the sign taken as 1 (material_two_sided_variants.py, read the other way).
inline bool MaterialMasterOneSidedSame(const Json& merged, const std::map<std::string, bool>& values, const Json& owner)
{
    const auto resolved = MaterialMasterResolveSwitches(merged, values);
    if (!MaterialMasterHasSign(resolved))
        return false;
    if (MaterialMasterTruthy(owner, "two_sided") || MaterialMasterTruthy(resolved, "two_sided"))
        return false;
    MaterialMasterSignature front{resolved, true, {}}, own{owner, false, {}};
    std::set<std::string> pins;
    for (const auto& pin : resolved.At("outputs").object)
        if (MaterialMasterPinApplies(owner, pin.first))
            pins.insert(pin.first);
    std::set<std::string> owned;
    for (const auto& pin : owner.At("outputs").object)
        if (MaterialMasterPinApplies(owner, pin.first))
            owned.insert(pin.first);
    if (pins != owned)
        return false;
    for (const auto& pin : pins)
        if (front.Of(resolved.At("outputs").At(pin)) != own.Of(owner.At("outputs").At(pin)))
            return false;
    return true;
}

// What an unlit additive material draws, in the form the decompiler reads it from the shader. Unreal folds
// the opacity into the color (UE 4.26 BasePassPixelShader.usf, MATERIALBLENDING_ADDITIVE: Color * Opacity,
// with the unlit Color = max(EmissiveColor, 0) and the Opacity saturated), so an additive owner's own shader
// holds one EmissiveColor and no Opacity. A merged graph set to additive is compared in that same form.
inline Json MaterialMasterAdditiveOutput(Json graph, const Json& owner)
{
    const auto text = [](const Json& from, const char* key)
    {
        return from.Has(key) && from.At(key).kind == Json::String ? from.At(key).string : std::string();
    };
    if (text(graph, "blend_mode") != "BLEND_Additive" || text(graph, "shading_model") != "MSM_Unlit" ||
        text(owner, "blend_mode") != "BLEND_Additive" || text(owner, "shading_model") != "MSM_Unlit" ||
        text(graph, "domain") != "MD_Surface" || text(owner, "domain") != "MD_Surface" || !graph.Has("outputs") ||
        !owner.Has("outputs"))
        return graph;
    auto& outputs = graph["outputs"];
    const auto& own = owner.At("outputs");
    if (!outputs.Has("EmissiveColor") || !outputs.Has("Opacity") || !own.Has("EmissiveColor") || own.Has("Opacity"))
        return graph;
    auto& nodes = graph["nodes"];
    const std::string zero = "MasterAdditiveFold_Zero", max = "MasterAdditiveFold_Max",
                      sat = "MasterAdditiveFold_Saturate", mul = "MasterAdditiveFold_Multiply";
    for (const auto& id : {zero, max, sat, mul})
        if (nodes.Has(id))
            throw Refused("additive fold node name collision", "master merge check");
    nodes[zero] = Object({{"type", "Constant"}, {"inputs", Json::MakeObject()}, {"props", Object({{"R", 0.0}})}});
    nodes[max] = Object({{"type", "Max"},
                         {"inputs", Object({{"A", outputs.At("EmissiveColor")}, {"B", Array({Json(zero), Json("")})}})},
                         {"props", Json::MakeObject()}});
    nodes[sat] = Object(
        {{"type", "Saturate"}, {"inputs", Object({{"Input", outputs.At("Opacity")}})}, {"props", Json::MakeObject()}});
    nodes[mul] = Object({{"type", "Multiply"},
                         {"inputs", Object({{"A", Array({Json(max), Json("")})}, {"B", Array({Json(sat), Json("")})}})},
                         {"props", Json::MakeObject()}});
    outputs.object.erase("Opacity");
    outputs["EmissiveColor"] = Array({Json(mul), Json("")});
    return graph;
}

// The inverse of that fold for an unlit additive owner of a master that is not additive: its one folded
// EmissiveColor, Multiply(Max(X, 0), Saturate(Y)), is the EmissiveColor X and Opacity Y that Unreal folds
// that way when the material is additive. Given in that form the owner shapes the master's graph as its
// translucent siblings do (its switches become the graph's), and its blend mode stays its own override.
// False, with the graph unchanged, when the folded color does not have exactly that form.
inline bool MaterialMasterUnfoldAdditive(Json& graph)
{
    const auto text = [](const Json& from, const char* key)
    {
        return from.Has(key) && from.At(key).kind == Json::String ? from.At(key).string : std::string();
    };
    if (text(graph, "blend_mode") != "BLEND_Additive" || text(graph, "shading_model") != "MSM_Unlit" ||
        text(graph, "domain") != "MD_Surface" || !graph.Has("outputs") || !graph.Has("nodes"))
        return false;
    const auto& outputs = graph.At("outputs");
    if (!outputs.Has("EmissiveColor") || outputs.Has("Opacity"))
        return false;
    const auto& nodes = graph.At("nodes");
    const auto plain = [](const Json& link)
    {
        return link.kind == Json::Array && link.array.size() == 2 && link.array[0].kind == Json::String &&
               link.array[1].kind == Json::String && link.array[1].string.empty();
    };
    const auto node = [&](const Json& link) -> const Json*
    {
        if (!plain(link) || !nodes.Has(link.array[0].string))
            return nullptr;
        return &nodes.At(link.array[0].string);
    };
    const auto kind = [](const Json* n)
    {
        return n && n->Has("type") && n->At("type").kind == Json::String ? n->At("type").string : std::string();
    };
    const auto inputs = [](const Json* n)
    {
        return n->Has("inputs") ? n->At("inputs") : Json::MakeObject();
    };
    const auto bare = [](const Json* n)
    {
        return !n->Has("props") || n->At("props").kind != Json::Object || n->At("props").object.empty();
    };
    const Json* product = node(outputs.At("EmissiveColor"));
    if (kind(product) != "Multiply" || !bare(product))
        return false;
    const auto factors = inputs(product);
    if (factors.object.size() != 2 || !factors.Has("A") || !factors.Has("B"))
        return false;
    for (const char* first : {"A", "B"})
    {
        const char* second = std::string(first) == "A" ? "B" : "A";
        const Json* max = node(factors.At(first));
        const Json* saturate = node(factors.At(second));
        if (kind(max) != "Max" || !bare(max) || kind(saturate) != "Saturate" || !bare(saturate))
            continue;
        const auto sides = inputs(max), of = inputs(saturate);
        if (sides.object.size() != 2 || !sides.Has("A") || !sides.Has("B") || of.object.size() != 1 || !of.Has("Input"))
            continue;
        const Json* zero = node(sides.At("B"));
        if (kind(zero) != "Constant" || !zero->Has("props") || !zero->At("props").Has("R") ||
            zero->At("props").At("R").kind != Json::Number || zero->At("props").At("R").number != 0.0 ||
            zero->At("props").object.size() != 1)
            continue;
        const Json color = sides.At("A"), opacity = of.At("Input");
        graph["outputs"]["EmissiveColor"] = color;
        graph["outputs"]["Opacity"] = opacity;
        return true;
    }
    return false;
}

// material_masters.recover for one master and the instances of it given, without the numeric proof: the
// merged graph is instead checked to give every owner's own recovered formulas.
inline MaterialMasterFamily RecoverMaterialMaster(const std::string& masterPackage,
                                                  const std::vector<std::string>& instancePackages,
                                                  MaterialMasterSources& sources,
                                                  const MaterialOriginalArchiveAccess& archive)
{
    MaterialMasterFamily out;
    const auto resolve = sources.Resolver();
    const auto refuse = [&out](const std::string& package, const std::string& text, const std::string& name)
    {
        out.Refused[package] = text;
        out.Refusal[package] = name;
    };
    MaterialSourceAsset masterAsset;
    if (!resolve(MaterialSourcePackage(masterPackage), masterAsset))
    {
        refuse(masterPackage, "the master's package is not among the game files: " + masterPackage,
               "a master whose package is missing");
        return out;
    }
    out.Master = masterAsset.Package.Package;
    out.Members.push_back(out.Master);
    out.OwnerOf[out.Master] = out.Master;
    std::map<std::string, std::map<std::string, MaterialMasterStatic>> statics;
    std::map<std::string, std::string> byIdentity{{PropertyIdentity(out.Master), out.Master}};
    std::set<std::string> instances;
    for (const auto& package : instancePackages)
        instances.insert(MaterialSourcePackage(package));
    for (const auto& package : instances)
    {
        try
        {
            MaterialSourceAsset asset;
            if (!resolve(package, asset))
                throw Refused("the instance's package is not among the game files",
                              "an instance whose package is missing");
            const auto chain = ReadOriginalMaterialParentChain(asset, resolve);
            if (chain.empty() || PropertyIdentity(chain.front().Package) != PropertyIdentity(out.Master))
                continue;
            const auto member = asset.Package.Package;
            if (PropertyIdentity(member) == PropertyIdentity(out.Master))
                continue;
            out.Members.push_back(member);
            std::string owner;
            for (auto record = chain.rbegin(); record != chain.rend() && owner.empty(); ++record)
            {
                bool own = record->Class == "Material";
                if (!own)
                    if (const auto flag = record->Properties.Field("bHasStaticPermutationResource"))
                        if (!MaterialBool(*flag, own))
                            throw Refused("static permutation flag is not boolean", "master owner");
                if (own)
                    owner = record->Package;
            }
            out.OwnerOf[member] = owner;
            byIdentity.emplace(PropertyIdentity(owner), owner);
            if (owner == member)
                statics[member] = MaterialMasterChainStatics(chain);
        }
        catch (const Material::Refused& error)
        {
            refuse(package, error.what(), error.name);
        }
    }
    std::vector<std::string> ownerPackages{out.Master};
    {
        std::set<std::string> others;
        for (const auto& item : out.OwnerOf)
            if (item.second == item.first && item.first != out.Master)
                others.insert(item.first);
        ownerPackages.insert(ownerPackages.end(), others.begin(), others.end());
    }
    for (const auto& package : ownerPackages)
    {
        try
        {
            MaterialSourceAsset asset;
            if (!resolve(package, asset))
                throw Refused("owner package is unavailable", "material source owner");
            MaterialMasterOwner owner;
            owner.Package = package;
            owner.Graph = RecoverMaterialMasterOwnerGraph(sources, asset, archive);
            if (package != out.Master)
                owner.Statics = statics[package];
            out.Recovered.push_back(owner);
        }
        catch (const Material::Refused& error)
        {
            refuse(package, error.what(), error.name);
        }
        catch (const std::exception& error)
        {
            refuse(package, std::string("an error that is not a refusal yet: ") + error.what(),
                   "an error that is not a refusal yet");
        }
    }
    if (out.Recovered.empty() || out.Recovered.front().Package != out.Master)
        return out;
    // Additive owners are unfolded before the two-sided preparation, so that they are prepared as their
    // translucent siblings are.
    std::set<std::string> unfolded;
    auto recovered = out.Recovered;
    if (recovered.front().Graph.At("blend_mode").Text() != "BLEND_Additive")
        for (std::size_t i = 1; i < recovered.size(); ++i)
            if (MaterialMasterUnfoldAdditive(recovered[i].Graph))
                unfolded.insert(recovered[i].Package);
    auto prepared = PrepareMaterialMasterTwoSidedVariants(recovered, unfolded);
    auto owners = prepared.Owners;
    out.TwoSidedSources = prepared.Sources;
    // An instance that changes what the whole graph is compiled under is the same graph compiled another
    // way. It does not shape the master's graph; it is checked against it like every owner.
    std::map<std::string, std::string> apart;
    const auto separate = [&]()
    {
        apart.clear();
        const auto& masterGraph = owners.front().Graph;
        for (std::size_t i = 1; i < owners.size(); ++i)
        {
            owners[i].Shapes = true;
            const auto& graph = owners[i].Graph;
            std::vector<std::string> changed;
            for (const auto* key : {"two_sided", "shading_model"})
            {
                const Json x = graph.Has(key) ? graph.At(key) : Json(),
                           y = masterGraph.Has(key) ? masterGraph.At(key) : Json();
                if (x.Dump() != y.Dump() &&
                    !(std::string(key) == "two_sided" && prepared.Included.count(owners[i].Package)))
                    changed.push_back(key);
            }
            const std::string mode = graph.At("blend_mode").Text(), base = masterGraph.At("blend_mode").Text();
            const auto opaqueOrMasked = [](const std::string& m)
            {
                return m == "BLEND_Opaque" || m == "BLEND_Masked";
            };
            if (mode != base && !(opaqueOrMasked(mode) && opaqueOrMasked(base)) && !unfolded.count(owners[i].Package))
                changed.push_back("blend_mode");
            if (!changed.empty())
            {
                std::string list;
                for (const auto& key : changed)
                    list += (list.empty() ? "" : " and ") + key;
                apart[owners[i].Package] = list;
                owners[i].Shapes = false;
            }
        }
    };
    separate();
    try
    {
        out.Merge = MergeMaterialMasterGraphs(owners);
    }
    catch (const Material::Refused& error)
    {
        // Unfolded additive owners whose graphs do not merge with the others stay apart instead, in the form
        // their own shaders hold, and are checked in it like any other owner that changes its blend mode.
        if (unfolded.empty())
        {
            out.RefusedMerge = error.what();
            out.RefusalMerge = error.name;
            return out;
        }
        unfolded.clear();
        prepared = PrepareMaterialMasterTwoSidedVariants(out.Recovered);
        owners = prepared.Owners;
        out.TwoSidedSources = prepared.Sources;
        separate();
        try
        {
            out.Merge = MergeMaterialMasterGraphs(owners);
        }
        catch (const Material::Refused& again)
        {
            out.RefusedMerge = again.what();
            out.RefusalMerge = again.name;
            return out;
        }
    }
    // The merged graph, with an owner's switches and settings, has to be that owner's own graph.
    std::set<std::string> kept;
    for (const auto& owner : owners)
    {
        std::map<std::string, bool> values = out.Merge.Defaults;
        for (const auto& item : MaterialMasterSwitches(owner.Statics))
            if (values.count(item.first))
                values[item.first] = item.second;
        auto graph = out.Merge.Graph;
        if (out.Merge.Overrides.Has(owner.Package))
            for (const auto& setting : out.Merge.Overrides.At(owner.Package).object)
                graph[setting.first] = setting.second;
        graph = MaterialMasterAdditiveOutput(std::move(graph), owner.Graph);
        MaterialMasterFingerprint merged{graph, &values, {}}, own{owner.Graph, nullptr, {}};
        std::string differs;
        for (const auto* key : {"domain", "blend_mode", "two_sided", "shading_model", "clip", "tangent_space_normal"})
        {
            const Json x = graph.Has(key) ? graph.At(key) : Json(),
                       y = owner.Graph.Has(key) ? owner.Graph.At(key) : Json();
            if (x.Dump() != y.Dump() && differs.empty())
                differs = std::string("the setting ") + key;
        }
        std::set<std::string> pins;
        for (const auto& pin : graph.At("outputs").object)
            pins.insert(pin.first);
        for (const auto& pin : owner.Graph.At("outputs").object)
            pins.insert(pin.first);
        // A pin a graph leaves out is what the pin is with nothing connected.
        MaterialMasterPool unconnected;
        Json defaultGraph = Json::MakeObject();
        defaultGraph["outputs"] = Json::MakeObject();
        const auto side = [&](MaterialMasterFingerprint& print, const Json& from, const std::string& pin)
        {
            if (from.At("outputs").Has(pin))
                return print.Of(from.At("outputs").At(pin));
            const auto link = unconnected.Default(pin);
            defaultGraph["nodes"] = unconnected.Nodes;
            MaterialMasterFingerprint fixed{defaultGraph, nullptr, {}};
            return fixed.Of(link);
        };
        try
        {
            for (const auto& pin : pins)
            {
                if (!differs.empty())
                    break;
                if (!MaterialMasterPinApplies(owner.Graph, pin))
                    continue;
                if (side(merged, graph, pin) != side(own, owner.Graph, pin))
                    differs = "the pin " + pin;
            }
        }
        catch (const Material::Refused& error)
        {
            if (differs.empty())
                differs = error.what();
        }
        if (!differs.empty() && apart.count(owner.Package) && MaterialMasterOneSidedSame(graph, values, owner.Graph))
            differs.clear();
        if (differs.empty())
        {
            kept.insert(owner.Package);
            continue;
        }
        const auto found = apart.find(owner.Package);
        if (found != apart.end())
            refuse(owner.Package,
                   "it changes " + found->second + ", and the master's graph set that way is not its shaders (" +
                       differs + " differs)",
                   "an instance that changes " + found->second + ", which the master's graph does not follow");
        else if (owner.Package == out.Master)
        {
            out.RefusedMerge = "the merged graph does not give the master's own graph: " + differs;
            out.RefusalMerge = "a merged graph that differs from an owner";
            return out;
        }
        else
            refuse(owner.Package, "the merged graph does not give its own graph: " + differs,
                   "a merged graph that differs from an owner");
    }
    for (const auto& owner : owners)
        if (kept.count(owner.Package))
            out.Owners.push_back(owner.Package);
    for (const auto& member : out.Members)
    {
        const auto owner = out.OwnerOf.find(member);
        if (owner != out.OwnerOf.end() && kept.count(owner->second))
            out.Users.push_back(member);
    }
    // A scene read a stock material of the master's kind cannot make is named as a Kadan game node.
    // A translucent master's opaque instance compiles the same node graph under its own blend mode.
    // Such a family needs the explicit game scene-read preview nodes for every retained owner.
    bool anyOpaqueOrMaskedOwner = false;
    for (const auto& owner : owners)
        if (kept.count(owner.Package) && !MaterialStockSceneReads(owner.Graph))
            anyOpaqueOrMaskedOwner = true;
    out.Merge.Graph = KadanGameReads(out.Merge.Graph, anyOpaqueOrMaskedOwner);
    out.HasGraph = true;
    return out;
}

// One material instance as the plugin makes it (NativeBuildMaterialInstance), or why it is refused: its
// master has no graph, the shaders that draw it are refused, or it sets something with no form here.
inline Json MaterialMasterInstance(const MaterialMasterFamily& family, const std::string& instance,
                                   MaterialMasterSources& sources)
{
    const auto master = MaterialMasterLeaf(family.Master);
    if (!family.HasGraph)
        throw Refused("its master " + master + " has no graph", "an instance of a master with no graph");
    const auto owner = family.OwnerOf.find(instance);
    if (owner == family.OwnerOf.end())
    {
        const auto why = family.Refused.find(instance);
        if (why != family.Refused.end())
            throw Refused(why->second, family.Refusal.at(instance));
        throw Refused("it is not an instance of " + master + " among the instances given",
                      "an instance outside its master's family");
    }
    if (std::find(family.Owners.begin(), family.Owners.end(), owner->second) == family.Owners.end())
    {
        const auto why = family.Refused.find(owner->second);
        throw Refused("the shaders that draw it are refused: " +
                          (why != family.Refused.end() ? why->second : owner->second),
                      "an instance whose shaders are refused");
    }
    MaterialSourceAsset asset;
    if (!sources.Resolver()(instance, asset))
        throw Refused("the instance's package is not among the game files", "an instance whose package is missing");
    const auto chain = ReadOriginalMaterialParentChain(asset, sources.Resolver());
    if (chain.empty() || chain.back().Class != "MaterialInstanceConstant")
        throw Refused("the package holds no material instance", "material source import instance");
    return ProjectOriginalMaterialInstanceForImport(chain.back());
}
}
} // P4MapKit::Material

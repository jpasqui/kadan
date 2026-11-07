// Independent structural expression readback comparison. Exact reflected CDO
// values must come from the engine; no missing node/property default is guessed.
#pragma once
#include "material_source_import.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
inline Json OriginalImportFloat32Json(const Json& value)
{
    if (value.kind == Json::Number)
    {
        const float number = float(value.number);
        if (!std::isfinite(number))
            throw Refused("readback numeric property is nonfinite", "material source import readback numeric");
        return Object(
            {{"float32_bits",
              CustomMaterial::Hex(CustomMaterial::Bytes{
                  std::uint8_t(MaterialFloatBits(number)), std::uint8_t(MaterialFloatBits(number) >> 8),
                  std::uint8_t(MaterialFloatBits(number) >> 16), std::uint8_t(MaterialFloatBits(number) >> 24)})}});
    }
    auto out = value;
    if (value.kind == Json::Array)
        for (auto& child : out.array)
            child = OriginalImportFloat32Json(child);
    if (value.kind == Json::Object)
        for (auto& child : out.object)
            child.second = OriginalImportFloat32Json(child.second);
    return out;
}
inline std::string OriginalImportCanonicalHash(const Json& value)
{
    return TextDigest(value.Dump());
}
struct MaterialOriginalImportReadbackComparison
{
    bool Equal = false;
    Json Expected, Actual;
    std::string ExpectedSha256, ActualSha256;
};
class OriginalImportGraphCanonicalizer
{
    const Json& Graph;
    const Json& Defaults;
    const Json& CollectionWidths;
    const Json& ObjectIdentities;
    bool InputIsReadback;
    std::map<std::string, std::vector<Json>> Memo;
    std::set<std::string> Active;
    std::size_t Steps = 0;
    using Components = std::vector<Json>;
    [[noreturn]] static void Fail(const std::string& detail)
    {
        throw Refused(detail, "material source import graph readback");
    }
    static Json Vector(const Components& components)
    {
        return Json(components);
    }
    static int Channel(char c)
    {
        if (c == 'R' || c == 'r' || c == 'x' || c == 'X')
            return 0;
        if (c == 'G' || c == 'g' || c == 'y' || c == 'Y')
            return 1;
        if (c == 'B' || c == 'b' || c == 'z' || c == 'Z')
            return 2;
        if (c == 'A' || c == 'a' || c == 'w' || c == 'W')
            return 3;
        Fail("readback channel is not RGBA/xyzw");
    }
    Components Link(const Json& link)
    {
        if (link.kind != Json::Array || link.array.size() < 2 || link.array.size() > 3 ||
            link.array[0].kind != Json::String || link.array[1].kind != Json::String)
            Fail("readback expression edge is malformed");
        auto result = Node(link.array[0].Text());
        std::string pinMask;
        if (link.array.size() == 3)
        {
            const auto& index = link.array[2];
            if (index.kind != Json::Number || !std::isfinite(index.number) || index.number < 0 || index.number > 64 ||
                index.number != std::floor(index.number))
                Fail("readback output selector is not bounded integer");
            if (index.number != 0)
            {
                const auto& type = Graph.At("nodes").At(link.array[0].Text()).At("type").Text();
                if (!Defaults.Has("outputs") || !Defaults.At("outputs").Has(type))
                    Fail("engine output descriptors absent for nonzero selector");
                const auto& outputs = Defaults.At("outputs").At(type);
                if (outputs.kind != Json::Array || std::size_t(index.number) >= outputs.array.size())
                    Fail("nonzero selector exceeds actual engine outputs");
                pinMask = outputs.array[std::size_t(index.number)].At("mask").Text();
                if (pinMask.empty() || pinMask.size() > 4)
                    Fail("nonzero output has no qualified static mask");
                Components selected;
                for (const auto c : pinMask)
                {
                    const auto lane = std::size_t(Channel(c));
                    if (lane >= result.size())
                        Fail("engine output mask exceeds original expression width");
                    selected.push_back(result[lane]);
                }
                result = std::move(selected);
            }
        }
        const auto& mask = link.array[1].string;
        if (mask.empty() || (InputIsReadback && !pinMask.empty() && pinMask == mask))
            return result;
        if (mask.size() > 4)
            Fail("readback channel width exceeds four");
        Components selected;
        for (const auto c : mask)
        {
            const auto index = std::size_t(Channel(c));
            if (index >= result.size())
            {
                if (result.size() == 1 && index == 0)
                    selected.push_back(result[0]);
                else
                    Fail("readback channel exceeds original expression width");
            }
            else
                selected.push_back(result[index]);
        }
        return selected;
    }
    Components Node(const std::string& id)
    {
        const auto memo = Memo.find(id);
        if (memo != Memo.end())
            return memo->second;
        if (++Steps > 100000 || Active.size() > 512 || !Active.insert(id).second)
            Fail("readback graph is cyclic or outside its bound");
        const auto& node = Graph.At("nodes").At(id);
        const auto type = node.At("type").Text();
        if (!Defaults.At("nodes").Has(type))
            Fail("engine CDO defaults absent for " + type);
        if (node.At("props").kind != Json::Object || node.At("inputs").kind != Json::Object ||
            Defaults.At("nodes").At(type).kind != Json::Object)
            Fail("readback node/default table is malformed");
        auto props = Defaults.At("nodes").At(type);
        for (const auto& field : node.At("props").object)
            props[field.first] = field.second;
        std::map<std::string, Components> inputs;
        for (const auto& field : node.At("inputs").object)
            inputs[field.first] = Link(field.second);
        Components result;
        if (type == "ComponentMask")
        {
            if (inputs.size() != 1 || !inputs.count("Input"))
                Fail("ComponentMask lowering lacks exact Input");
            for (const auto* key : {"R", "G", "B", "A"})
            {
                if (!props.Has(key) || props.At(key).kind != Json::Boolean)
                    Fail("ComponentMask CDO channel is unqualified");
                if (props.At(key).boolean)
                {
                    const auto index = std::size_t(Channel(key[0]));
                    if (index >= inputs.at("Input").size())
                        Fail("ComponentMask lowering exceeds source width");
                    result.push_back(inputs.at("Input")[index]);
                }
            }
            if (result.empty())
                Fail("empty ComponentMask lowering");
        }
        else if (type == "AppendVector")
        {
            if (inputs.size() != 2 || !inputs.count("A") || !inputs.count("B"))
                Fail("AppendVector lowering lacks both exact inputs");
            result = inputs.at("A");
            result.insert(result.end(), inputs.at("B").begin(), inputs.at("B").end());
            if (result.size() > 4)
                Fail("AppendVector lowering exceeds four channels");
        }
        else
        {
            std::size_t width = 0;
            if (IsMember(type, {"Constant", "ScalarParameter", "DotProduct"}))
                width = 1;
            else if (type == "Constant2Vector" || type == "TextureCoordinate")
                width = 2;
            else if (IsMember(type, {"Constant3Vector", "CameraVectorWS", "PixelNormalWS"}))
                width = 3;
            else if (IsMember(type, {"Constant4Vector", "VectorParameter"}) ||
                     type.compare(0, 13, "TextureSample") == 0)
                width = 4;
            else if (type == "CollectionParameter")
            {
                if (!props.Has("Collection") || !props.Has("ParameterName"))
                    Fail("collection parameter identity absent");
                const auto key = PropertyIdentity(MaterialSourcePackage(props.At("Collection").Text())) + "|" +
                                 PropertyIdentity(props.At("ParameterName").Text());
                if (!CollectionWidths.Has(key))
                    Fail("actual collection parameter width is unqualified");
                const auto n = CollectionWidths.At(key).Numeric();
                if (n != 1 && n != 4)
                    Fail("actual collection parameter width is unsupported");
                width = std::size_t(n);
            }
            else if (IsMember(type, {"Add", "Subtract", "Multiply", "Divide", "Power", "If", "OneMinus", "Saturate"}))
            {
                width = 1;
                for (const auto& input : inputs)
                    width = std::max(width, input.second.size());
            }
            else
                Fail("node type has no independently qualified readback width: " + type);
            Json canonicalInputs = Json::MakeObject();
            for (const auto& field : inputs)
                canonicalInputs[field.first] = Vector(field.second);
            // Paths are UObject/FName identities in this comparison. Printable
            // case changes do not confer a different referenced source object.
            for (const auto* key : {"Texture", "Collection"})
                if (props.Has(key) && props.At(key).kind == Json::String && !props.At(key).Text().empty())
                {
                    const auto path = props.At(key).Text(), package = PropertyIdentity(MaterialSourcePackage(path));
                    if (ObjectIdentities.Has(package))
                    {
                        const auto actual = ObjectIdentities.At(package).Text();
                        if (path.find('.') != std::string::npos && PropertyIdentity(path) != actual)
                            Fail("graph dependency object disagrees with its actual raw export");
                        props[key] = actual;
                    }
                    else
                    {
                        if (path.find('.') == std::string::npos)
                            Fail("package-only graph dependency has no actual export identity");
                        props[key] = PropertyIdentity(path);
                    }
                }
            const auto hash = OriginalImportCanonicalHash(
                Object({{"type", type}, {"props", OriginalImportFloat32Json(props)}, {"inputs", canonicalInputs}}));
            for (std::size_t lane = 0; lane < width; ++lane)
                result.push_back(Object({{"node", hash}, {"lane", int(lane)}}));
        }
        Active.erase(id);
        Memo.emplace(id, result);
        return result;
    }

public:
    OriginalImportGraphCanonicalizer(const Json& graph, const Json& defaults, const Json& collections,
                                     const Json& identities, bool readback = false)
        : Graph(graph), Defaults(defaults), CollectionWidths(collections), ObjectIdentities(identities),
          InputIsReadback(readback)
    {
    }
    Json Canonical()
    {
        if (Graph.At("nodes").kind != Json::Object || Graph.At("nodes").object.size() > 100000 ||
            Graph.At("outputs").kind != Json::Object || Defaults.At("root").kind != Json::Object)
            Fail("graph readback boundary is malformed");
        Json settings = Defaults.At("root");
        for (const auto* key : {"domain", "blend_mode", "shading_model", "two_sided", "tangent_space_normal", "clip"})
            if (Graph.Has(key))
                settings[key] = Graph.At(key);
        auto outputs = Json::MakeObject();
        for (const auto& field : Graph.At("outputs").object)
            outputs[field.first] = Vector(Link(field.second));
        std::vector<std::string> nodeHashes;
        for (const auto& node : Graph.At("nodes").object)
        {
            const auto type = node.second.At("type").Text();
            if (type == "ComponentMask" || type == "AppendVector")
                continue;
            const auto value = Node(node.first);
            if (value.empty() || !value[0].Has("node"))
                Fail("ordinary node lost its structural identity");
            nodeHashes.push_back(value[0].At("node").Text());
        }
        std::sort(nodeHashes.begin(), nodeHashes.end());
        auto inventory = Json::MakeArray();
        for (const auto& hash : nodeHashes)
            inventory.array.push_back(hash);
        auto canonical =
            Object({{"settings", OriginalImportFloat32Json(settings)}, {"outputs", outputs}, {"nodes", inventory}});
        if (Graph.Has("render_settings"))
            canonical["render_settings"] = OriginalImportFloat32Json(Graph.At("render_settings"));
        return canonical;
    }
};
inline Json OriginalMaterialImportCollectionWidths(const MaterialOriginalSourceImport& source)
{
    Json widths = Json::MakeObject();
    for (const auto& c : source.MasterOwner.User.Collections)
        if (c.Available)
        {
            for (const auto& name : c.Scalars)
                widths[PropertyIdentity(c.Package) + "|" + PropertyIdentity(name)] = 1;
            for (const auto& name : c.Vectors)
                widths[PropertyIdentity(c.Package) + "|" + PropertyIdentity(name)] = 4;
        }
    return widths;
}
inline Json OriginalMaterialImportObjectIdentities(const MaterialOriginalSourceImport& source)
{
    auto result = Json::MakeObject();
    for (const auto& entry : source.ResolvedSources)
    {
        const auto& asset = entry.second.Asset;
        std::string type;
        FDecodedValue props;
        if (!MaterialSourceReadObject(asset,
                                      {"MaterialParameterCollection", "Texture2D", "TextureCube", "MediaTexture",
                                       "TextureRenderTarget2D", "Texture2DArray", "VolumeTexture"},
                                      type, props))
            continue;
        std::string root;
        for (const auto& record : asset.Package.Exports)
        {
            std::string name;
            if (!asset.Package.Reference(record.Class, name))
                throw Refused("dependency class reference is invalid", "material source import dependency identity");
            if (name.substr(name.find_last_of('.') == std::string::npos ? 0 : name.find_last_of('.') + 1) == type)
            {
                if (!root.empty())
                    throw Refused("dependency export identity is ambiguous",
                                  "material source import dependency identity");
                root = record.Name;
            }
        }
        if (root.empty())
            throw Refused("dependency has no actual export identity", "material source import dependency identity");
        result[PropertyIdentity(asset.Package.Package)] = PropertyIdentity(asset.Package.Package + "." + root);
    }
    return result;
}
inline MaterialOriginalImportReadbackComparison
CompareOriginalMaterialImportGraph(const Json& expected, const Json& actual, const Json& engineClassDefaults,
                                   const Json& actualCollectionWidths,
                                   const Json& actualObjectIdentities = Json::MakeObject())
{
    // Require a CDO value for every explicit property which the other graph
    // omits. This avoids accepting an unsayable default as an empty dictionary.
    std::map<std::string, std::set<std::string>> required;
    for (const auto* graph : {&expected, &actual})
        for (const auto& node : graph->At("nodes").object)
            for (const auto& p : node.second.At("props").object)
                if (p.first != "Inputs")
                    required[node.second.At("type").Text()].insert(p.first);
    for (const auto& type : required)
        for (const auto& property : type.second)
            if (!engineClassDefaults.At("nodes").Has(type.first) ||
                !engineClassDefaults.At("nodes").At(type.first).Has(property))
                throw Refused("engine CDO property is not independently readable: " + type.first + "." + property,
                              "material source import graph readback default");
    MaterialOriginalImportReadbackComparison result;
    result.Expected =
        OriginalImportGraphCanonicalizer(expected, engineClassDefaults, actualCollectionWidths, actualObjectIdentities)
            .Canonical();
    result.Actual = OriginalImportGraphCanonicalizer(actual, engineClassDefaults, actualCollectionWidths,
                                                     actualObjectIdentities, true)
                        .Canonical();
    result.ExpectedSha256 = OriginalImportCanonicalHash(result.Expected);
    result.ActualSha256 = OriginalImportCanonicalHash(result.Actual);
    result.Equal = result.Expected.Dump() == result.Actual.Dump();
    return result;
}
inline Json CanonicalOriginalMaterialInstanceImport(const Json& instance)
{
    if (instance.kind != Json::Object)
        throw Refused("instance readback is not object", "material source import instance readback");
    const std::set<std::string> allowed = {"parent",   "scalars",   "vectors",       "textures",
                                           "switches", "overrides", "game_overrides"};
    for (const auto& field : instance.object)
        if (!allowed.count(field.first))
            throw Refused("instance readback contains unsupported field: " + field.first,
                          "material source import instance readback");
    Json out = Object({{"parent", PropertyIdentity(instance.At("parent").Text())}});
    for (const auto* key : {"scalars", "vectors", "textures", "switches", "overrides"})
    {
        auto table = instance.Has(key) ? instance.At(key) : Json::MakeObject();
        if (table.kind != Json::Object)
            throw Refused("instance readback table is malformed", "material source import instance readback");
        if (std::string(key) == "scalars" || std::string(key) == "vectors")
            table = OriginalImportFloat32Json(table);
        else if (std::string(key) == "textures")
            for (auto& value : table.object)
            {
                if (value.second.kind != Json::Null)
                    value.second = PropertyIdentity(value.second.Text());
            }
        else if (std::string(key) == "overrides" && table.Has("clip"))
            table["clip"] = OriginalImportFloat32Json(table.At("clip"));
        out[key] = table;
    }
    if (instance.Has("game_overrides"))
        out["game_overrides"] = instance.At("game_overrides");
    return out;
}
inline bool CompareOriginalMaterialInstanceImport(const Json& expected, const Json& actual)
{
    return CanonicalOriginalMaterialInstanceImport(expected).Dump() ==
           CanonicalOriginalMaterialInstanceImport(actual).Dump();
}
}
} // P4MapKit::Material

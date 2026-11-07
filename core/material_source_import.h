// Raw-source preparation for original editor imports. Engine construction,
// independent live readback and save custody are separate boundaries.
#pragma once
#include "material_source_pipeline.h"
#include "custom_material_hash.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalImportRawAsset
{
    std::shared_ptr<const CustomMaterial::Bytes> Header, Body;
};
using MaterialOriginalImportRawResolver = std::function<bool(const std::string&, MaterialOriginalImportRawAsset&)>;
struct MaterialOriginalImportCustody
{
    MaterialOriginalImportRawAsset Raw;
    MaterialSourceAsset Asset;
    std::string HeaderSha256, BodySha256, SourceSha256;
    const char* DigestKind = "header-body-sha256-v1";
};
inline MaterialOriginalImportCustody ReadOriginalMaterialImportCustody(const MaterialOriginalImportRawAsset& raw,
                                                                       const std::string& expectedPackage = {})
{
    constexpr std::size_t limit = 64u * 1024u * 1024u;
    if (!raw.Header || !raw.Body || raw.Header->empty() || raw.Header->size() > limit || raw.Body->empty() ||
        raw.Body->size() > limit)
        throw Refused("original import raw header/body is unavailable or outside its bound",
                      "material source import raw bytes");
    MaterialOriginalImportCustody result;
    result.Raw = raw;
    result.Asset = {ReadPackage(*raw.Header, *raw.Body), raw.Body};
    if (!result.Asset.Package.Error.Code.empty())
        throw Refused(result.Asset.Package.Error.Code, "material source import package");
    MaterialSourcePackage(result.Asset.Package.Package);
    if (!expectedPackage.empty() &&
        PropertyIdentity(result.Asset.Package.Package) != PropertyIdentity(MaterialSourcePackage(expectedPackage)))
        throw Refused("resolved raw package disagrees with the actual parent/dependency identity",
                      "material source import package identity");
    result.HeaderSha256 = PayloadDigest(*raw.Header);
    result.BodySha256 = PayloadDigest(*raw.Body);
    CustomMaterial::Bytes joined;
    joined.reserve(raw.Header->size() + raw.Body->size());
    joined.insert(joined.end(), raw.Header->begin(), raw.Header->end());
    joined.insert(joined.end(), raw.Body->begin(), raw.Body->end());
    result.SourceSha256 = PayloadDigest(joined);
    return result;
}
inline Json OriginalMaterialImportFloat(const FDecodedValue& value)
{
    float number = 0;
    if (!MaterialNumber(value, number))
        throw Refused("original parameter is not a finite float32", "material source import parameter value");
    return Json(double(number));
}
inline std::string OriginalMaterialImportParameterName(const FDecodedValue& row)
{
    if (row.Kind != FDecodedValue::EKind::Object)
        throw Refused("parameter row is not typed object", "material source import parameter info");
    const auto info = row.Field("ParameterInfo");
    if (!info || info->Kind != FDecodedValue::EKind::Object)
        throw Refused("parameter has no typed identity", "material source import parameter info");
    const auto name = info->Field("Name");
    if (!name || name->Kind != FDecodedValue::EKind::Name || name->Text.empty() || name->Text == "None" ||
        !JsonDetail::ValidUtf8(name->Text))
        throw Refused("parameter has no bounded FName", "material source import parameter info");
    if (const auto association = info->Field("Association"))
    {
        std::string word;
        if (!MaterialEnum(*association, word) || word != "GlobalParameter")
            throw Refused("layered parameter cannot be imported by plain-name interface",
                          "material source import parameter association");
    }
    if (const auto index = info->Field("Index"))
        if (index->Kind != FDecodedValue::EKind::Number || index->Text != "-1")
            throw Refused("parameter index is not global", "material source import parameter association");
    return name->Text;
}
inline Json OriginalMaterialImportGameValue(const FDecodedValue* value)
{
    if (!value)
        return Object({{"type", "omitted-cooked-value"}});
    bool b = false;
    if (MaterialBool(*value, b))
        return Object({{"type", "bool"}, {"value", b}});
    if (value->Kind == FDecodedValue::EKind::Number)
    {
        if (value->Type != "FloatProperty" && value->Type != "DoubleProperty" &&
            value->Text.find_first_of(".eE") == std::string::npos)
        {
            std::int64_t number = 0;
            std::istringstream input(value->Text);
            input >> number;
            if (input.fail() || !input.eof() || number < -9007199254740991ll || number > 9007199254740991ll)
                throw Refused("game integer cannot be transported exactly by importer JSON",
                              "material source import game override");
            return Object({{"type", "integer"}, {"value", double(number)}});
        }
        const auto number = OriginalMaterialImportFloat(*value);
        const auto bits = MaterialFloatBits(float(number.Numeric()));
        CustomMaterial::Bytes bytes;
        for (int i = 0; i < 4; ++i)
            bytes.push_back(std::uint8_t(bits >> (8 * i)));
        return Object({{"type", "float32"}, {"value", number}, {"bits", CustomMaterial::Hex(bytes)}});
    }
    if ((value->Kind == FDecodedValue::EKind::Name || value->Kind == FDecodedValue::EKind::Enum ||
         value->Kind == FDecodedValue::EKind::String))
        return Object({{"type", "name"}, {"value", value->Text}});
    throw Refused("game override is not an exact supported scalar", "material source import game override");
}
// Explicit values from ONE MIC only. Effective inherited values and inactive
// copied base/cache fields remain in its separate exact typed source witness.
inline Json ProjectOriginalMaterialInstanceForImport(const FMaterialCookedRecord& record)
{
    if (record.Class != "MaterialInstanceConstant" || record.Properties.Kind != FDecodedValue::EKind::Object)
        throw Refused("instance projection requires an original typed MIC", "material source import instance");
    const auto& props = record.Properties;
    const auto parent = props.Field("Parent");
    if (!parent ||
        !(parent->Kind == FDecodedValue::EKind::HardReference || parent->Kind == FDecodedValue::EKind::SoftPath) ||
        parent->Text.empty())
        throw Refused("instance has no actual parent reference", "material source import parent");
    MaterialSourcePackage(parent->Text);
    Json out = Object({{"parent", parent->Text},
                       {"scalars", Json::MakeObject()},
                       {"vectors", Json::MakeObject()},
                       {"textures", Json::MakeObject()},
                       {"switches", Json::MakeObject()},
                       {"overrides", Json::MakeObject()}});
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{{"ScalarParameterValues", "scalars"},
                                                                             {"VectorParameterValues", "vectors"},
                                                                             {"TextureParameterValues", "textures"}})
    {
        const auto rows = props.Field(pair.first);
        if (!rows)
            continue;
        if (rows->Kind != FDecodedValue::EKind::Array || rows->Items.size() > 4096)
            throw Refused("instance parameter array exceeds its typed bound", "material source import parameters");
        std::set<std::string> names;
        for (const auto& row : rows->Items)
        {
            const auto name = OriginalMaterialImportParameterName(row);
            if (!names.insert(PropertyIdentity(name)).second)
                throw Refused("duplicate instance parameter identity", "material source import duplicate parameter");
            const auto value = row.Field("ParameterValue");
            if (!value)
                throw Refused("parameter has no local value", "material source import parameter value");
            if (pair.second == "scalars")
                out[pair.second][name] = OriginalMaterialImportFloat(*value);
            else if (pair.second == "vectors")
            {
                if (value->Kind != FDecodedValue::EKind::Object)
                    throw Refused("vector parameter is not a typed color", "material source import parameter value");
                Json color = Json::MakeArray();
                for (const auto* channel : {"R", "G", "B", "A"})
                {
                    const auto component = value->Field(channel);
                    if (!component)
                        throw Refused("vector color component is absent", "material source import parameter value");
                    color.array.push_back(OriginalMaterialImportFloat(*component));
                }
                out[pair.second][name] = color;
            }
            else if (value->Kind == FDecodedValue::EKind::HardReference && !value->ReferenceIndex &&
                     value->Text.empty())
                out[pair.second][name] = Json();
            else
            {
                if (!(value->Kind == FDecodedValue::EKind::HardReference ||
                      value->Kind == FDecodedValue::EKind::SoftPath) ||
                    value->Text.empty() ||
                    (value->Kind == FDecodedValue::EKind::HardReference && !value->ReferenceIndex))
                    throw Refused("unsupported texture parameter source reference",
                                  "material source import texture parameter");
                MaterialSourcePackage(value->Text);
                out[pair.second][name] = value->Text;
            }
        }
    }
    if (const auto font = props.Field("FontParameterValues"))
        if (font->Kind != FDecodedValue::EKind::Array || !font->Items.empty())
            throw Refused("font parameters have no original import producer", "material source import font parameter");
    if (const auto statics = props.Field("StaticParameters"))
    {
        if (statics->Kind != FDecodedValue::EKind::Object)
            throw Refused("static parameter set is not typed object", "material source import static parameters");
        for (const auto& field : statics->Fields)
        {
            if (field.second.Kind != FDecodedValue::EKind::Array || field.second.Items.size() > 4096)
                throw Refused("static parameter list is outside its bound", "material source import static parameters");
            std::set<std::string> names;
            for (const auto& row : field.second.Items)
            {
                const auto flag = row.Field("bOverride");
                bool enabled = false;
                if (flag && !MaterialBool(*flag, enabled))
                    throw Refused("static override flag is not boolean", "material source import static parameters");
                if (!enabled)
                    continue;
                if (field.first != "StaticSwitchParameters")
                    throw Refused("active static parameter has no producer: " + field.first,
                                  "material source import static parameters");
                const auto name = OriginalMaterialImportParameterName(row);
                if (!names.insert(PropertyIdentity(name)).second)
                    throw Refused("duplicate static parameter", "material source import duplicate parameter");
                const auto value = row.Field("Value");
                bool b = false;
                if (!value || !MaterialBool(*value, b))
                    throw Refused("static switch value is absent/not boolean",
                                  "material source import static parameters");
                out["switches"][name] = b;
            }
        }
    }
    if (const auto base = props.Field("BasePropertyOverrides"))
    {
        if (base->Kind != FDecodedValue::EKind::Object)
            throw Refused("base override source is not typed object", "material source import base overrides");
        const std::map<std::string, std::pair<std::string, std::string>> ordinary = {
            {"bOverride_TwoSided", {"TwoSided", "two_sided"}},
            {"bOverride_BlendMode", {"BlendMode", "blend_mode"}},
            {"bOverride_ShadingModel", {"ShadingModel", "shading_model"}},
            {"bOverride_OpacityMaskClipValue", {"OpacityMaskClipValue", "clip"}}};
        for (const auto& field : base->Fields)
        {
            if (field.first.compare(0, 10, "bOverride_"))
                continue;
            bool enabled = false;
            if (!MaterialBool(field.second, enabled))
                throw Refused("base override flag is not boolean", "material source import base overrides");
            if (!enabled)
                continue;
            const auto known = ordinary.find(field.first);
            // A cook writes a member of the overrides only where it differs from FMaterialInstanceBasePropertyOverrides'
            // default, so an enabled override with no value overrides with that default: not two-sided, BLEND_Opaque,
            // MSM_DefaultLit, a clip value of .333333f.
            if (known != ordinary.end())
            {
                const auto value = base->Field(known->second.first);
                if (!value)
                {
                    if (known->second.second == "two_sided")
                        out["overrides"]["two_sided"] = false;
                    else if (known->second.second == "clip")
                        out["overrides"]["clip"] = double(.333333f);
                    else
                        out["overrides"][known->second.second] =
                            std::string(known->second.second == "blend_mode" ? "BLEND_Opaque" : "MSM_DefaultLit");
                    continue;
                }
                if (known->second.second == "two_sided")
                {
                    bool b = false;
                    if (!MaterialBool(*value, b))
                        throw Refused("two-sided override is not boolean", "material source import base overrides");
                    out["overrides"][known->second.second] = b;
                }
                else if (known->second.second == "clip")
                    out["overrides"]["clip"] = OriginalMaterialImportFloat(*value);
                else
                {
                    std::string word;
                    if (!MaterialEnum(*value, word))
                        throw Refused("ordinary enum override is not named", "material source import base overrides");
                    out["overrides"][known->second.second] = word;
                }
                continue;
            }
            auto key = field.first.substr(10);
            if (key.compare(0, 3, "bC4") == 0)
                key = key.substr(1);
            if (key.compare(0, 2, "C4"))
                throw Refused("active unknown base override has no native producer: " + field.first,
                              "material source import base overrides");
            const auto plain = base->Field(key), boolean = base->Field("b" + key);
            if (plain && boolean &&
                OriginalMaterialImportGameValue(plain).Dump() != OriginalMaterialImportGameValue(boolean).Dump())
                throw Refused("C4 override aliases disagree", "material source import game override");
            const auto value = plain ? plain : boolean;
            const auto property = plain ? key : boolean ? "b" + key : std::string();
            if (!out.Has("game_overrides"))
                out["game_overrides"] = Json::MakeObject();
            if (out["game_overrides"].Has(key))
                throw Refused("duplicate C4 override flag aliases", "material source import game override");
            out["game_overrides"][key] = Object({{"flag", field.first},
                                                 {"enabled", true},
                                                 {"value_present", value != nullptr},
                                                 {"value_property", value ? Json(property) : Json()},
                                                 {"value", OriginalMaterialImportGameValue(value)}});
        }
    }
    return out;
}
inline Json OriginalMaterialImportTypedWitness(const FDecodedValue& value, std::size_t depth = 0)
{
    if (depth > 128)
        throw Refused("typed import witness exceeds depth bound", "material source import witness");
    auto fields = Json::MakeArray(), items = Json::MakeArray();
    for (const auto& field : value.Fields)
        fields.array.push_back(Array({field.first, OriginalMaterialImportTypedWitness(field.second, depth + 1)}));
    for (const auto& item : value.Items)
        items.array.push_back(OriginalMaterialImportTypedWitness(item, depth + 1));
    return Object({{"kind", int(value.Kind)},
                   {"text", value.Text},
                   {"type", value.Type},
                   {"enum_type", value.EnumType},
                   {"name_base", value.NameBase},
                   {"name_index", value.NameIndex},
                   {"name_number", value.NameNumber},
                   {"reference_index", value.ReferenceIndex},
                   {"asset_path", value.AssetPath},
                   {"sub_path", value.SubPath},
                   {"enum_numeric", value.EnumNumeric},
                   {"fields", fields},
                   {"items", items}});
}
inline void OriginalMaterialImportUnreadFields(const FDecodedValue& value, std::vector<std::string>& paths,
                                               const std::string& path = {}, std::size_t depth = 0)
{
    if (depth > 128)
        throw Refused("native field witness exceeds depth bound", "material source import witness");
    if (value.Field("__native_bytes_not_read"))
        paths.push_back(path);
    for (const auto& field : value.Fields)
        OriginalMaterialImportUnreadFields(field.second, paths, path.empty() ? field.first : path + "." + field.first,
                                           depth + 1);
    for (std::size_t i = 0; i < value.Items.size(); ++i)
        OriginalMaterialImportUnreadFields(value.Items[i], paths, path + "[" + std::to_string(i) + "]", depth + 1);
}
struct MaterialOriginalImportNode
{
    FMaterialCookedRecord Witness;
    MaterialOriginalImportCustody Custody;
    Json Instance = Json();
    std::string TypedWitnessSha256;
    std::vector<std::string> UnreadNativeFields;
};
struct MaterialOriginalSourceImport : MaterialEvidence
{
    std::vector<MaterialOriginalImportNode> RootFirstChain;
    std::map<std::string, MaterialOriginalImportCustody> ResolvedSources;
    MaterialOriginalOwnerSource MasterOwner, SelectedOwner;
    MaterialOriginalOwnerRecovery MasterRecovery;
    std::vector<std::string> DependencyObjects, WitnessDependencyObjects, MissingAssets;
};
inline void OriginalMaterialImportReferences(const FDecodedValue& value, std::set<std::string>& paths,
                                             std::size_t depth = 0)
{
    if (depth > 128)
        throw Refused("source dependency tree exceeds its recursion bound", "material source import dependencies");
    if ((value.Kind == FDecodedValue::EKind::HardReference || value.Kind == FDecodedValue::EKind::SoftPath) &&
        !value.Text.empty() && value.Text[0] == '/' &&
        !(value.Kind == FDecodedValue::EKind::HardReference && !value.ReferenceIndex))
    {
        if (value.Text.compare(0, 8, "/Script/"))
        {
            MaterialSourcePackage(value.Text);
            paths.insert(value.Text);
            if (paths.size() > 4096)
                throw Refused("source dependency set exceeds its bound", "material source import dependencies");
        }
    }
    for (const auto& child : value.Fields)
        OriginalMaterialImportReferences(child.second, paths, depth + 1);
    for (const auto& child : value.Items)
        OriginalMaterialImportReferences(child, paths, depth + 1);
}
inline MaterialOriginalSourceImport
PrepareOriginalMaterialSourceImport(const MaterialOriginalImportRawAsset& selected,
                                    const MaterialOriginalImportRawResolver& rawResolver,
                                    const MaterialOriginalArchiveAccess& archive,
                                    const std::vector<MaterialOriginalImportRawAsset>& collectionInputs = {},
                                    MaterialDomainSourceScope scope = MaterialDomainSourceScope::ReadOnlyExpressions)
{
    MaterialOriginalSourceImport out;
    auto first = ReadOriginalMaterialImportCustody(selected);
    const auto selectedPackage = first.Asset.Package.Package;
    out.ResolvedSources.emplace(PropertyIdentity(selectedPackage), std::move(first));
    const MaterialSourceAssetResolver resolve = [&](const std::string& object, MaterialSourceAsset& value)
    {
        const auto package = MaterialSourcePackage(object), key = PropertyIdentity(package);
        auto found = out.ResolvedSources.find(key);
        if (found == out.ResolvedSources.end())
        {
            MaterialOriginalImportRawAsset raw;
            if (!rawResolver || !rawResolver(package, raw))
                return false;
            auto custody = ReadOriginalMaterialImportCustody(raw, package);
            found = out.ResolvedSources.emplace(key, std::move(custody)).first;
        }
        value = found->second.Asset;
        return true;
    };
    std::vector<MaterialSourceAsset> collections;
    for (const auto& raw : collectionInputs)
    {
        auto custody = ReadOriginalMaterialImportCustody(raw);
        const auto key = PropertyIdentity(custody.Asset.Package.Package);
        auto found = out.ResolvedSources.find(key);
        if (found != out.ResolvedSources.end())
        {
            if (found->second.SourceSha256 != custody.SourceSha256)
                throw Refused("collection and chain sources conflict", "material source import custody");
        }
        else
            found = out.ResolvedSources.emplace(key, std::move(custody)).first;
        collections.push_back(found->second.Asset);
    }
    // Resolve the actual cached collection object references before binding
    // shader-map GUIDs. Neither path spelling nor list position assigns buffers.
    const auto previewChain =
        ReadOriginalMaterialParentChain(out.ResolvedSources.at(PropertyIdentity(selectedPackage)).Asset, resolve);
    std::set<std::string> witnessReferences, collectionPackages;
    for (const auto& asset : collections)
        collectionPackages.insert(PropertyIdentity(asset.Package.Package));
    for (const auto& record : previewChain)
        OriginalMaterialImportReferences(record.Properties, witnessReferences);
    for (const auto& path : witnessReferences)
    {
        MaterialSourceAsset asset;
        if (!resolve(MaterialSourcePackage(path), asset))
            continue;
        std::string type;
        FDecodedValue properties;
        if (MaterialSourceReadObject(asset, {"MaterialParameterCollection"}, type, properties) &&
            collectionPackages.insert(PropertyIdentity(asset.Package.Package)).second)
            collections.push_back(std::move(asset));
    }
    out.SelectedOwner = ReadOriginalMaterialOwnerSource(out.ResolvedSources.at(PropertyIdentity(selectedPackage)).Asset,
                                                        resolve, collections);
    for (const auto& record : out.SelectedOwner.User.Chain)
    {
        const auto found = out.ResolvedSources.find(PropertyIdentity(record.Package));
        if (found == out.ResolvedSources.end())
            throw Refused("source chain node lost its raw custody", "material source import custody");
        std::string type;
        FDecodedValue properties;
        if (!MaterialSourceReadObject(found->second.Asset, {"Material", "MaterialInstanceConstant"}, type,
                                      properties) ||
            type != record.Class)
            throw Refused("source chain root is absent/ambiguous", "material source import owner");
        MaterialOriginalImportNode node;
        node.Witness = record;
        node.Custody = found->second;
        node.TypedWitnessSha256 = TextDigest(OriginalMaterialImportTypedWitness(record.Properties).Dump());
        OriginalMaterialImportUnreadFields(record.Properties, node.UnreadNativeFields);
        if (record.Class == "MaterialInstanceConstant")
            node.Instance = ProjectOriginalMaterialInstanceForImport(record);
        out.RootFirstChain.push_back(std::move(node));
    }
    if (out.RootFirstChain.empty() || out.RootFirstChain.front().Witness.Class != "Material")
        throw Refused("raw import chain has no material master", "material source import master");
    out.MasterOwner = ReadOriginalMaterialOwnerSource(out.RootFirstChain.front().Custody.Asset, resolve, collections);
    out.MasterRecovery = RecoverOriginalMaterialSourceExpressionGraph(out.MasterOwner, archive, scope);
    std::set<std::string> liveReferences;
    for (const auto& item : out.MasterRecovery.Primary.Material.Graph.At("nodes").object)
        if (item.second.Has("props"))
            for (const auto* key : {"Texture", "Collection"})
                if (item.second.At("props").Has(key))
                {
                    const auto& value = item.second.At("props").At(key);
                    if (value.kind == Json::String && !value.Text().empty() && value.Text()[0] == '/')
                    {
                        MaterialSourcePackage(value.Text());
                        liveReferences.insert(value.Text());
                    }
                }
    for (const auto& node : out.RootFirstChain)
        if (node.Instance.kind == Json::Object)
            for (const auto& texture : node.Instance.At("textures").object)
                if (texture.second.kind != Json::Null)
                    liveReferences.insert(texture.second.Text());
    for (const auto& path : witnessReferences)
    {
        const auto package = MaterialSourcePackage(path);
        bool isChain = false;
        for (const auto& node : out.RootFirstChain)
            if (PropertyIdentity(node.Witness.Package) == PropertyIdentity(package))
                isChain = true;
        if (!isChain)
            out.WitnessDependencyObjects.push_back(path);
    }
    for (const auto& path : liveReferences)
    {
        out.DependencyObjects.push_back(path);
        MaterialSourceAsset dependency;
        if (!resolve(MaterialSourcePackage(path), dependency))
            out.MissingAssets.push_back(MaterialSourcePackage(path));
    }
    std::sort(out.MissingAssets.begin(), out.MissingAssets.end());
    out.MissingAssets.erase(std::unique(out.MissingAssets.begin(), out.MissingAssets.end()), out.MissingAssets.end());
    return out;
}
}
} // P4MapKit::Material

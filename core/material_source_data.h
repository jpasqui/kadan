// Original material user data is decoded from actual selected assets. A resolver
// supplies additional user assets; it never supplies recovered graphs or plans.
#pragma once
#include "material_cooked.h"
#include "material_prepare.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialSourceAsset
{
    FPackageInventory Package;
    std::shared_ptr<const std::vector<std::uint8_t>> Body;
};
using MaterialSourceAssetResolver = std::function<bool(const std::string&, MaterialSourceAsset&)>;
struct MaterialOriginalUserData
{
    std::vector<FMaterialCookedRecord> Chain;
    FMaterialCookedSettings Cooked;
    Json Settings = Json::MakeObject(), Defaults = Json::MakeObject();
    std::vector<CollectionBinding> Collections;
    std::vector<std::string> MissingAssets;
};
inline std::string MaterialSourcePackage(const std::string& object)
{
    if (object.empty() || object[0] != '/' || object.size() > 1024 || !JsonDetail::ValidUtf8(object) ||
        object.find('\\') != std::string::npos || object.find(':') != std::string::npos)
        throw Refused("material source reference is not a bounded object path", "material source asset reference");
    for (const auto c : object)
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            throw Refused("material source reference contains a control byte", "material source asset reference");
    const auto dot = object.find('.');
    const auto package = object.substr(0, dot);
    if (package.size() < 2 || package.back() == '/' || package.find("//") != std::string::npos)
        throw Refused("material source reference has an invalid package segment", "material source asset reference");
    std::size_t first = 1;
    while (first < package.size())
    {
        const auto stop = package.find('/', first);
        const auto segment = package.substr(first, stop == std::string::npos ? std::string::npos : stop - first);
        if (segment == "." || segment == "..")
            throw Refused("material source reference traverses a package path", "material source asset reference");
        if (stop == std::string::npos)
            break;
        first = stop + 1;
    }
    if (dot != std::string::npos && (dot + 1 == object.size() || object.find('/', dot) != std::string::npos ||
                                     object.find('.', dot + 1) != std::string::npos))
        throw Refused("material source object suffix is invalid", "material source asset reference");
    return package;
}
inline Json MaterialSourceTypedJson(const FDecodedValue& value)
{
    std::ostringstream out;
    WriteDecodedJson(value, out);
    return Json::Parse(out.str());
}
inline float MaterialSourceFloat(std::uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    if (!std::isfinite(value))
        throw Refused("original uniform default is nonfinite", "material source uniform default");
    return value;
}
// Same shortest significant-digit round trip as material_cooked.short. The
// cooked float32 bits remain in Cooked; only the editable graph literal changes.
inline double MaterialSourceGraphFloat(float value)
{
    if (!std::isfinite(value))
        throw Refused("graph setting is nonfinite", "material source graph setting");
    std::uint32_t expected;
    std::memcpy(&expected, &value, 4);
    for (int digits = 1; digits <= 9; ++digits)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << std::setprecision(digits) << std::defaultfloat << double(value);
        std::istringstream input(text.str());
        input.imbue(std::locale::classic());
        double candidate = 0;
        input >> candidate;
        if (input.fail() || !input.eof())
            continue;
        const float actual = float(candidate);
        std::uint32_t bits;
        std::memcpy(&bits, &actual, 4);
        if (bits == expected)
            return candidate;
    }
    return double(value);
}
inline void MaterialSourceInsert(Json& table, const std::string& name, const Json& value, const char* category)
{
    if (name.empty() || !JsonDetail::ValidUtf8(name))
        throw Refused("material source parameter has no valid identity", category);
    if (table.Has(name) && table.At(name).Dump() != value.Dump())
        throw Refused("material source parameter has conflicting defaults: " + name, category);
    table[name] = value;
}
inline bool MaterialSourceReadObject(const MaterialSourceAsset& asset, const std::set<std::string>& classes,
                                     std::string& className, FDecodedValue& properties)
{
    if (!asset.Body || !asset.Package.Error.Code.empty())
        throw Refused("material source package is unavailable or invalid", "material source asset");
    std::size_t matches = 0;
    for (const auto& record : asset.Package.Exports)
    {
        std::string type;
        if (!asset.Package.Reference(record.Class, type))
            throw Refused("material source class reference is invalid", "material source asset");
        type = type.substr(type.find_last_of('.') == std::string::npos ? 0 : type.find_last_of('.') + 1);
        if (!classes.count(type))
            continue;
        if (++matches != 1)
            throw Refused("material source has ambiguous root objects", "material source asset");
        if (record.Offset > asset.Body->size() || record.Size > asset.Body->size() - record.Offset)
            throw Refused("material source export is outside its body", "material source asset");
        FPropertyDecoder decoder(asset.Package, *asset.Body);
        auto at = record.Offset;
        if (!decoder.TypedProperties(at, record.Offset + record.Size, properties))
            throw Refused(decoder.Error.Code, "material source properties");
        className = type;
    }
    return matches == 1;
}
inline Json MaterialSourceSettings(const FMaterialCookedSettings& cooked)
{
    auto settings = Object({{"domain", cooked.Domain},
                            {"blend_mode", cooked.BlendMode},
                            {"shading_model", cooked.ShadingModel},
                            {"two_sided", cooked.TwoSided},
                            {"clip", MaterialSourceGraphFloat(cooked.Clip)},
                            {"tangent_space_normal", true}});
    if (!cooked.RenderSettings.empty() || !cooked.Usage.empty())
    {
        settings["render_settings"] = Json::MakeObject();
        for (const auto& field : cooked.RenderSettings)
            settings["render_settings"][field.first] = MaterialSourceTypedJson(field.second);
        // These are the master's actual cooked true flags, already validated by
        // ReadMaterialCookedSettings. Preserve them before editor compilation;
        // inferring usage from whichever meshes load first regenerates StateId
        // and can select DefaultMaterial until the new vertex factory compiles.
        for (const auto& flag : cooked.Usage)
            settings["render_settings"][flag] = true;
    }
    return settings;
}
inline std::vector<FMaterialCookedRecord> ReadOriginalMaterialParentChain(const MaterialSourceAsset& selected,
                                                                          const MaterialSourceAssetResolver& resolve)
{
    std::vector<FMaterialCookedRecord> leafFirst;
    std::set<std::string> seen;
    MaterialSourceAsset current = selected;
    for (std::size_t depth = 0; depth < 1024; ++depth)
    {
        FMaterialCookedRecord record;
        FReadError error;
        if (!current.Body || !ReadMaterialCookedRecord(current.Package, *current.Body, record, error))
            throw Refused(error.Code.empty() ? "material body unavailable" : error.Code,
                          "material source parent chain");
        if (!seen.insert(PropertyIdentity(record.Package)).second)
            throw Refused("original material parent chain contains a cycle", "material source parent chain");
        leafFirst.push_back(record);
        if (record.Class == "Material")
        {
            std::reverse(leafFirst.begin(), leafFirst.end());
            return leafFirst;
        }
        const auto parent = record.Properties.Field("Parent");
        if (!parent ||
            (parent->Kind != FDecodedValue::EKind::HardReference && parent->Kind != FDecodedValue::EKind::SoftPath) ||
            parent->Text.empty())
            throw Refused("material instance has no checked parent reference", "material source parent chain");
        const auto package = MaterialSourcePackage(parent->Text);
        if (!resolve || !resolve(package, current))
            throw Refused("original material parent asset is unavailable: " + package, "material source parent asset");
    }
    throw Refused("original material parent chain exceeds its depth bound", "material source parent chain");
}
inline MaterialOriginalUserData
ReadOriginalMaterialUserData(const MaterialSourceAsset& selected, const FMaterialShaderMapEnvelope& map,
                             const MaterialSourceAssetResolver& resolve,
                             const std::vector<MaterialSourceAsset>& collectionAssets = {})
{
    MaterialOriginalUserData result;
    result.Chain = ReadOriginalMaterialParentChain(selected, resolve);
    FReadError error;
    if (!ReadMaterialCookedSettings(result.Chain, result.Cooked, error))
        throw Refused(error.Code, "material source cooked settings");
    result.Settings = MaterialSourceSettings(result.Cooked);
    result.Defaults = Object({{"scalars", Json::MakeObject()},
                              {"vectors", Json::MakeObject()},
                              {"textures", Json::MakeObject()},
                              {"sampler_types", Json::MakeObject()}});
    for (const auto& scalar : map.Uniforms.Scalars)
        if (!scalar.NameIsNone)
            MaterialSourceInsert(result.Defaults["scalars"], scalar.Name,
                                 double(MaterialSourceFloat(scalar.DefaultBits[0])),
                                 "material source uniform identity");
    for (const auto& vector : map.Uniforms.Vectors)
        if (!vector.NameIsNone)
        {
            auto value = Json::MakeArray();
            for (const auto bits : vector.DefaultBits)
                value.array.push_back(double(MaterialSourceFloat(bits)));
            MaterialSourceInsert(result.Defaults["vectors"], vector.Name, value, "material source uniform identity");
        }
    const auto cached = result.Chain.front().Properties.Field("CachedExpressionData");
    const auto parameters = cached ? cached->Field("Parameters") : nullptr;
    if (parameters)
    {
        const auto entries = parameters->Field("RuntimeEntries[2]");
        const auto infos = entries ? entries->Field("ParameterInfos") : nullptr;
        const auto values = parameters->Field("TextureValues");
        if (infos || values)
        {
            if (!infos || !values || infos->Kind != FDecodedValue::EKind::Array ||
                values->Kind != FDecodedValue::EKind::Array || infos->Items.size() != values->Items.size() ||
                infos->Items.size() > 1000000)
                throw Refused("cooked master texture parameter arrays disagree", "material source texture defaults");
            for (std::size_t i = 0; i < infos->Items.size(); ++i)
            {
                const auto name = infos->Items[i].Field("Name");
                const auto& value = values->Items[i];
                if (!name || name->Text.empty() || name->Text == "None" || value.Kind == FDecodedValue::EKind::Null ||
                    (value.ReferenceIndex == 0 && value.Kind == FDecodedValue::EKind::HardReference))
                    continue;
                if (value.Kind != FDecodedValue::EKind::HardReference && value.Kind != FDecodedValue::EKind::SoftPath)
                    throw Refused("master texture parameter is not an asset reference",
                                  "material source texture defaults");
                const auto package = MaterialSourcePackage(value.Text);
                MaterialSourceInsert(result.Defaults["textures"], name->Text, value.Text,
                                     "material source texture defaults");
                MaterialSourceAsset texture;
                std::string type;
                FDecodedValue props;
                if (resolve && resolve(package, texture) &&
                    MaterialSourceReadObject(texture,
                                             {"Texture2D", "MediaTexture", "TextureRenderTarget2D", "TextureCube",
                                              "Texture2DArray", "VolumeTexture"},
                                             type, props))
                {
                    std::string samplerType;
                    if (!ReadMaterialSamplerType(type, props, samplerType, error))
                        throw Refused(error.Code, "material source sampler type");
                    MaterialSourceInsert(result.Defaults["sampler_types"], name->Text, samplerType,
                                         "material source sampler type");
                }
                else
                {
                    result.Defaults["sampler_types"][name->Text] = Json();
                    result.MissingAssets.push_back(package);
                }
            }
        }
    }
    // Collection assets are matched by actual decoded StateId; list position
    // and filename are never used to assign a material collection buffer.
    std::map<std::array<std::uint8_t, 16>, CollectionBinding> collections;
    for (const auto& asset : collectionAssets)
    {
        std::string type;
        FDecodedValue props;
        if (!MaterialSourceReadObject(asset, {"MaterialParameterCollection"}, type, props))
            throw Refused("provided collection asset has no parameter collection", "material source collection asset");
        const auto guid = props.Field("StateId");
        if (!guid || guid->Kind != FDecodedValue::EKind::Object)
            throw Refused("collection has no typed StateId", "material source collection identity");
        std::array<std::uint8_t, 16> bytes{{}};
        for (std::size_t i = 0; i < 4; ++i)
        {
            const auto word = guid->Field(std::string(1, char('A' + i)));
            if (!word || word->Kind != FDecodedValue::EKind::Number)
                throw Refused("collection StateId word is missing", "material source collection identity");
            std::int64_t signedWord = 0;
            std::istringstream input(word->Text);
            input >> signedWord;
            if (input.fail() || !input.eof() || signedWord < -2147483648ll || signedWord > 4294967295ll)
                throw Refused("collection StateId word is invalid", "material source collection identity");
            const auto n = std::uint32_t(signedWord);
            for (std::size_t k = 0; k < 4; ++k)
                bytes[i * 4 + k] = std::uint8_t(n >> (8 * k));
        }
        CollectionBinding collection;
        collection.Package = asset.Package.Package;
        for (const auto& field : {std::make_pair("ScalarParameters", &collection.Scalars),
                                  std::make_pair("VectorParameters", &collection.Vectors)})
        {
            const auto rows = props.Field(field.first);
            if (!rows)
                continue;
            if (rows->Kind != FDecodedValue::EKind::Array || rows->Items.size() > 1000000)
                throw Refused("collection parameter array is invalid", "material source collection parameters");
            for (const auto& row : rows->Items)
            {
                const auto name = row.Field("ParameterName");
                if (!name || name->Text.empty())
                    throw Refused("collection parameter has no identity", "material source collection parameters");
                field.second->push_back(name->Text);
            }
        }
        if (!collections.emplace(bytes, std::move(collection)).second)
            throw Refused("collection StateId is ambiguous", "material source collection identity");
    }
    for (const auto& guid : map.Uniforms.Collections)
    {
        const auto known = collections.find(guid);
        if (known == collections.end())
        {
            CollectionBinding missing;
            missing.Available = false;
            result.Collections.push_back(missing);
        }
        else
            result.Collections.push_back(known->second);
    }
    return result;
}
}
} // P4MapKit::Material

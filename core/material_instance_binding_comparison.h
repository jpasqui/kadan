// Read-only comparison projection for engine-rebound original MIC parameter GUIDs.
// Parent frame custody is established by the native adapter, never by this predicate.
#pragma once
#include "property_decoder.h"
#include "custom_material_hash.h"
#include <map>
#include <set>
#include <stdexcept>
#include "material_source_common.h"

namespace P4MapKit
{
namespace Material
{
struct MaterialInstanceParentBinding
{
    std::string Group, NameBase;
    std::int32_t NameNumber = 0;
    FDecodedValue Info, Guid;
};
namespace InstanceBindingComparison
{
using Value = FDecodedValue;
inline const Value& Need(const Value& v, const char* key)
{
    const auto* p = v.Field(key);
    if (!p)
        throw std::runtime_error(std::string("MIC binding missing ") + key);
    return *p;
}
inline Value& Need(Value& v, const char* key)
{
    for (auto& p : v.Fields)
        if (p.first == key)
            return p.second;
    throw std::runtime_error(std::string("MIC binding missing ") + key);
}
inline void Guid(const Value& value)
{
    if (value.Kind != Value::EKind::Object || value.Fields.size() != 4)
        throw std::runtime_error("MIC binding GUID shape differs");
    for (const char* key : {"A", "B", "C", "D"})
    {
        const auto& part = Need(value, key);
        if (part.Kind != Value::EKind::Number || part.Text.empty())
            throw std::runtime_error("MIC binding GUID word differs");
        std::size_t used = 0;
        const auto number = std::stoll(part.Text, &used);
        if (used != part.Text.size() || number < INT32_MIN || number > INT32_MAX || std::to_string(number) != part.Text)
            throw std::runtime_error("MIC binding GUID word is not signed32");
    }
}
inline std::string Key(const std::string& group, const Value& info)
{
    return group + "\n" + DecodedJson(info);
}
inline void Info(const Value& info)
{
    if (info.Kind != Value::EKind::Object || info.Fields.size() != 3)
        throw std::runtime_error("MIC parameter identity shape differs");
    const auto& name = Need(info, "Name");
    const auto& association = Need(info, "Association");
    const auto& index = Need(info, "Index");
    // Current recovered parent construction supports global parameters only.
    if (name.Kind != Value::EKind::String || name.Text.empty() || association.Kind != Value::EKind::String ||
        association.Text != "GlobalParameter" || index.Kind != Value::EKind::Number || index.Text != "-1")
        throw std::runtime_error("MIC parameter name/association/layer is unsupported");
}
inline Value Read(const std::string& text)
{
    Value result;
    std::size_t at = 0;
    if (text.size() > 4u * 1024u * 1024u || !ReadDecodedJson(text, at, result) || at != text.size() ||
        DecodedJson(result) != text)
        throw std::runtime_error("MIC comparison line is not exact decoded JSON");
    return result;
}
}

// Only three parameter-property lines may change, and only their ExpressionGUID leaves.
// Every other byte/line remains compared by the existing object-lines digest. Output is
// transactional: false leaves it untouched, including failures after an earlier GUID.
inline bool ProjectOriginalMaterialInstanceBindingLines(
    const std::string& package, const std::string& sourceSha256, const std::vector<std::uint8_t>& header,
    const std::vector<std::uint8_t>& body, const std::string& parentObject,
    const std::vector<MaterialInstanceParentBinding>& parentFrame, const std::vector<std::string>& lines,
    std::vector<std::string>& output, std::size_t& changed, std::string& error)
{
    namespace B = InstanceBindingComparison;
    using V = FDecodedValue;
    error.clear();
    changed = 0;
    try
    {
        if (header.empty() || body.empty() || header.size() > 64u * 1024u * 1024u || body.size() > 64u * 1024u * 1024u)
            throw std::runtime_error("MIC source byte bound");
        auto bytes = header;
        bytes.insert(bytes.end(), body.begin(), body.end());
        if (sourceSha256.size() != 64 || PayloadDigest(bytes) != sourceSha256)
            throw std::runtime_error("MIC original source SHA differs");
        auto inventory = ReadPackage(header, body);
        if (!inventory.Error.Code.empty() || inventory.Package != package || inventory.Exports.size() != 1 ||
            inventory.Exports[0].Outer)
            throw std::runtime_error("MIC original package/owner differs");
        const auto& root = inventory.Exports[0];
        std::string owner, klass;
        if (!inventory.Reference(1, owner) || !inventory.Reference(root.Class, klass) ||
            klass != "/Script/Engine.MaterialInstanceConstant")
            throw std::runtime_error("MIC original class differs");
        V source;
        FPropertyDecoder decoder(inventory, body);
        auto at = root.Offset;
        if (!decoder.TypedProperties(at, root.Offset + root.Size, source, 0, "MaterialInstanceConstant"))
            throw std::runtime_error("MIC original parameter decode failed: " + decoder.Error.Code);
        const auto* sourceParent = source.Field("Parent");
        if (!sourceParent || sourceParent->Kind != V::EKind::HardReference || !sourceParent->ReferenceIndex ||
            sourceParent->Text != parentObject)
            throw std::runtime_error("MIC source parent owner differs");
        std::map<std::string, const MaterialInstanceParentBinding*> frames;
        for (const auto& frame : parentFrame)
        {
            if (frame.Group != "ScalarParameterValues" && frame.Group != "VectorParameterValues" &&
                frame.Group != "TextureParameterValues")
                throw std::runtime_error("MIC parent parameter type differs");
            B::Info(frame.Info);
            B::Guid(frame.Guid);
            if (!frames.emplace(B::Key(frame.Group, frame.Info), &frame).second)
                throw std::runtime_error("MIC parent parameter identities are ambiguous");
        }
        auto result = lines;
        std::size_t replacements = 0;
        const std::string prefix = owner + " | ";
        if (result.size() > 100000)
            throw std::runtime_error("MIC comparison line bound");
        std::map<std::string, std::size_t> properties;
        bool classSeen = false;
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const auto& line = result[i];
            if (line.compare(0, prefix.size(), prefix) != 0)
                throw std::runtime_error("MIC comparison contains another owner");
            const auto keyAt = line.find(" = ", prefix.size());
            if (keyAt != std::string::npos)
            {
                if (!properties.emplace(line.substr(prefix.size(), keyAt - prefix.size()), i).second)
                    throw std::runtime_error("MIC comparison property repeats");
            }
            else if (line.compare(prefix.size(), 6, "class ") == 0)
            {
                const std::string expected = "class /Script/Engine.MaterialInstanceConstant, kind ";
                if (classSeen || line.compare(prefix.size(), expected.size(), expected) != 0)
                    throw std::runtime_error("MIC comparison class differs");
                classSeen = true;
            }
        }
        if (!classSeen)
            throw std::runtime_error("MIC comparison class is absent");
        const auto parentLine = properties.find("parent");
        if (parentLine == properties.end() ||
            result[parentLine->second] != prefix + "parent = " + DecodedJson(LegacyDecodedValue(*sourceParent)))
            throw std::runtime_error("MIC current parent differs");
        for (const char* group : {"ScalarParameterValues", "VectorParameterValues", "TextureParameterValues"})
        {
            const auto* raw = source.Field(group);
            const auto found = properties.find(PropertyIdentity(group));
            if (!raw && found == properties.end())
                continue;
            if (!raw || found == properties.end() || raw->Kind != V::EKind::Array)
                throw std::runtime_error("MIC parameter group shape differs");
            const auto linePrefix = prefix + PropertyIdentity(group) + " = ";
            auto current = B::Read(result[found->second].substr(linePrefix.size()));
            if (current.Kind != V::EKind::Array || current.Items.size() != raw->Items.size())
                throw std::runtime_error("MIC parameter row count differs");
            std::set<std::string> identities;
            for (std::size_t i = 0; i < current.Items.size(); ++i)
            {
                auto& row = current.Items[i];
                const auto& original = raw->Items[i];
                const auto sourceInfo = LegacyDecodedValue(B::Need(original, "ParameterInfo"));
                const auto& info = B::Need(row, "ParameterInfo");
                B::Info(info);
                B::Info(sourceInfo);
                const auto key = B::Key(group, info);
                if (DecodedJson(info) != DecodedJson(sourceInfo) || !identities.insert(key).second ||
                    DecodedJson(B::Need(row, "ParameterValue")) !=
                        DecodedJson(LegacyDecodedValue(B::Need(original, "ParameterValue"))))
                    throw std::runtime_error("MIC parameter name/value/association/layer differs");
                auto& guid = B::Need(row, "ExpressionGUID");
                const auto& sourceGuid = B::Need(original, "ExpressionGUID");
                B::Guid(guid);
                B::Guid(sourceGuid);
                const auto frame = frames.find(key);
                if (frame != frames.end())
                {
                    const auto& sourceName = B::Need(B::Need(original, "ParameterInfo"), "Name");
                    if (sourceName.Kind != V::EKind::Name || sourceName.NameBase != frame->second->NameBase ||
                        sourceName.NameNumber != frame->second->NameNumber)
                        throw std::runtime_error("MIC parameter native FName identity differs");
                }
                if (DecodedJson(guid) == DecodedJson(sourceGuid))
                    continue;
                if (frame == frames.end() || DecodedJson(guid) != DecodedJson(frame->second->Guid))
                    throw std::runtime_error("MIC current GUID differs from recovered parent binding");
                guid = sourceGuid;
                ++replacements;
            }
            result[found->second] = linePrefix + DecodedJson(current);
        }
        output = std::move(result);
        changed = replacements;
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}
}
}

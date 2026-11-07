// Raw texture and collection metadata needed to interpret compiled uniforms.
// This is not shader liveness, executable dependencies, graph proof or import
// authority. Only a later complete graph discovery can request an actual asset.
#pragma once
#include "material_used_resources.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialBindingMetadataRequest
{
    std::string Owner, OwnerSourceSha256, Package, Object, NativeClass;
    std::vector<FMaterialUsedResourceReference> References;
};
inline std::vector<MaterialBindingMetadataRequest>
DiscoverOriginalMaterialBindingMetadata(const std::string& ownerPackage, const MaterialOriginalImportRawResolver& raw)
{
    MaterialUsedResourceDetail::FReader reader(raw);
    const auto* selected = reader.Read(ownerPackage);
    if (!selected)
        throw Refused("binding metadata owner source is missing", "material binding metadata owner");
    MaterialOriginalUserData user;
    user.Chain = ReadOriginalMaterialParentChain(selected->Asset, reader.Resolver());
    const auto references = MaterialUsedResourceDetail::ChainReferences(reader, user.Chain);
    const auto& asset = selected->Asset;
    std::size_t index = asset.Package.Exports.size();
    for (std::size_t i = 0; i < asset.Package.Exports.size(); ++i)
    {
        std::string type;
        if (!asset.Package.Reference(asset.Package.Exports[i].Class, type))
            throw Refused("binding metadata owner class is invalid", "material binding metadata owner");
        if (type != "/Script/Engine.Material" && type != "/Script/Engine.MaterialInstanceConstant")
            continue;
        if (index != asset.Package.Exports.size())
            throw Refused("binding metadata owner is ambiguous", "material binding metadata owner");
        index = i;
    }
    if (index == asset.Package.Exports.size())
        throw Refused("binding metadata owner has no material", "material binding metadata owner");
    FMaterialResourceBoundary boundary;
    FMaterialShaderMapEnvelope map;
    FReadError error;
    if (!DiscoverMaterialResourceBoundary(asset.Package, *asset.Body, index, boundary, error) ||
        !ReadMaterialShaderMapEnvelope(*asset.Body, boundary.ResourceStart, boundary.ExportEnd, map, error))
        throw Refused(error.Code, "material binding metadata shader map");
    const std::vector<FDecodedValue>* table = nullptr;
    std::map<std::string, MaterialBindingMetadataRequest> result;
    // A collection's StateId and parameter layout are prerequisites for reading a
    // MaterialCollection buffer. Acquire only actual cached typed references;
    // neither this list nor an imported native collection proves shader use.
    for (const auto& record : user.Chain)
    {
        const auto* cached = record.Properties.Field("CachedExpressionData");
        const auto* infos = cached ? cached->Field("ParameterCollectionInfos") : nullptr;
        if (!infos)
            continue;
        if (infos->Kind != FDecodedValue::EKind::Array || infos->Items.size() > 65536)
            throw Refused("cached collection metadata is not a bounded array", "material binding metadata collection");
        for (std::size_t i = 0; i < infos->Items.size(); ++i)
        {
            const auto* value = infos->Items[i].Field("ParameterCollection");
            if (!value)
                throw Refused("cached collection metadata has no typed reference",
                              "material binding metadata collection");
            if (value->Kind == FDecodedValue::EKind::HardReference && !value->ReferenceIndex)
                continue;
            if (value->Kind != FDecodedValue::EKind::HardReference || value->ReferenceIndex >= 0)
                throw Refused("cached collection metadata is not an external hard reference",
                              "material binding metadata collection");
            const auto package = MaterialSourcePackage(value->Text);
            const auto property =
                "CachedExpressionData.ParameterCollectionInfos[" + std::to_string(i) + "].ParameterCollection";
            std::vector<FMaterialUsedResourceReference> matches;
            for (const auto& reference : references)
                if (reference.SourcePackage == record.Package && reference.Property == property)
                {
                    if (reference.Object != value->Text || reference.ImportIndex != value->ReferenceIndex ||
                        reference.NativeClass != "/Script/Engine.MaterialParameterCollection")
                        throw Refused("cached collection metadata differs from its actual typed import",
                                      "material binding metadata collection");
                    matches.push_back(reference);
                }
            if (matches.size() != 1)
                throw Refused("cached collection metadata has no unique actual typed import",
                              "material binding metadata collection");
            if (const auto* available = reader.Read(package))
            {
                FDecodedValue properties;
                if (MaterialUsedResourceDetail::ActualObjectClass(*available, value->Text, properties) !=
                    "/Script/Engine.MaterialParameterCollection")
                    throw Refused("available collection metadata has a different native class",
                                  "material binding metadata collection");
                continue;
            }
            auto& request = result[package];
            if (!request.Object.empty() &&
                (request.Object != value->Text || request.NativeClass != "/Script/Engine.MaterialParameterCollection"))
                throw Refused("cached collection metadata identities conflict", "material binding metadata collection");
            request.Owner = ownerPackage;
            request.OwnerSourceSha256 = selected->SourceSha256;
            request.Package = package;
            request.Object = value->Text;
            request.NativeClass = "/Script/Engine.MaterialParameterCollection";
            request.References.insert(request.References.end(), matches.begin(), matches.end());
        }
    }
    for (const auto& uniform : map.Uniforms.Textures)
    {
        if (!MaterialFixedAnonymous(uniform) || uniform.TextureType != 0)
            continue;
        if (!table)
            table = &OriginalMaterialFixedReferences(user);
        if (uniform.TextureIndex < 0 || std::size_t(uniform.TextureIndex) >= table->size())
            throw Refused("binding metadata fixed index is outside its actual table",
                          "material binding metadata index");
        const auto& value = table->at(std::size_t(uniform.TextureIndex));
        const auto package = OriginalMaterialFixedPackage(value);
        if (reader.Read(package))
            continue; // Already available metadata stays in its existing read-only scope.
        auto& request = result[package];
        request.Owner = ownerPackage;
        request.OwnerSourceSha256 = selected->SourceSha256;
        request.Package = package;
        request.Object = value.Text;
        for (const auto& reference : references)
            if (reference.Object == value.Text)
            {
                if (!IsMember(reference.NativeClass,
                              {"/Script/Engine.Texture2D", "/Script/Engine.TextureRenderTarget2D",
                               "/Script/MediaAssets.MediaTexture"}))
                    throw Refused("fixed Standard2D metadata has a different native import class",
                                  "material binding metadata class");
                if (!request.NativeClass.empty() && request.NativeClass != reference.NativeClass)
                    throw Refused("binding metadata source classes conflict", "material binding metadata class");
                request.NativeClass = reference.NativeClass;
                request.References.push_back(reference);
            }
        if (request.References.empty())
            throw Refused("fixed metadata has no actual typed hard source import",
                          "material binding metadata reference");
    }
    std::vector<MaterialBindingMetadataRequest> out;
    for (auto& item : result)
        out.push_back(std::move(item.second));
    return out;
}
}
} // P4MapKit::Material

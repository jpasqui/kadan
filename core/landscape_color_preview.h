// Read-only base-color previews from an inline Landscape shader owner. This
// deliberately grants no material import/export or whole-shader admission.
#pragma once
#include "material_resource_boundary.h"
#include "material_source_data.h"
#include "material_domain_prepare.h"
#include "material_decompile.h"
#include "material_anchor.h"
#include "material_program.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct InlineLandscapeColorPreview
{
    std::string Object, Parent;
    std::array<std::uint8_t, 20> ResourceHash{{}};
    std::uint64_t ShaderType = 0;
    Json Graph;
};
struct LandscapePreviewArchiveAccess
{
    std::function<std::vector<std::uint32_t>(const std::array<std::uint8_t, 20>&)> MapIndices;
    std::function<std::vector<std::uint8_t>(std::uint32_t)> Payload;
    std::function<std::uint8_t(std::uint32_t)> Frequency;
};
inline std::string LandscapePreviewObjectPath(const FPackageInventory& package, const std::string& relative)
{
    const auto world = package.Package.substr(package.Package.find_last_of('/') + 1);
    const auto prefix = world + ".PersistentLevel.";
    if (relative.compare(0, prefix.size(), prefix) != 0)
        throw Refused("inline object is outside the source world's persistent level", "landscape color preview");
    return package.Package + "." + world + ":" + relative.substr(world.size() + 1);
}
inline InlineLandscapeColorPreview RecoverInlineLandscapeColorPreview(const FPackageInventory& package,
                                                                      const std::vector<std::uint8_t>& body,
                                                                      std::size_t index,
                                                                      const MaterialSourceAssetResolver& resolve,
                                                                      const LandscapePreviewArchiveAccess& archive,
                                                                      const std::string& weightmap)
{
    if (!package.Error.Code.empty() || index >= package.Exports.size())
        throw Refused("inline landscape owner is absent", "landscape color preview");
    const auto& item = package.Exports[index];
    std::string cls;
    if (item.Offset > body.size() || item.Size > body.size() - item.Offset || !archive.MapIndices || !archive.Payload ||
        !archive.Frequency)
        throw Refused("inline landscape source bounds or archive access are invalid", "landscape color preview");
    if (!package.Reference(item.Class, cls) || cls != "/Script/Landscape.LandscapeMaterialInstanceConstant")
        throw Refused("inline shader owner is not the native Landscape MIC class", "landscape color preview");
    std::size_t end = item.Offset;
    FPropertyDecoder decoder(package, body);
    FDecodedValue props;
    if (!decoder.TypedProperties(end, item.Offset + item.Size, props, 0, "LandscapeMaterialInstanceConstant"))
        throw Refused(decoder.Error.Code, "landscape color preview");
    const auto parent = props.Field("Parent");
    if (!parent || parent->Kind != FDecodedValue::EKind::HardReference || parent->ReferenceIndex >= 0)
        throw Refused("inline shader owner has no external original parent", "landscape color preview");
    MaterialSourceAsset parentAsset;
    if (!resolve(MaterialSourcePackage(parent->Text), parentAsset))
        throw Refused("inline landscape parent source is unavailable", "landscape color preview");
    bool sourceWeightmap = false;
    for (std::size_t i = 0; i < package.Exports.size(); ++i)
    {
        std::string path, type;
        if (package.Reference(std::int32_t(i + 1), path) && package.Reference(package.Exports[i].Class, type) &&
            type == "/Script/Engine.Texture2D" && LandscapePreviewObjectPath(package, path) == weightmap)
            sourceWeightmap = true;
    }
    if (!sourceWeightmap)
        throw Refused("the preview default is not a texture owned by this source world", "landscape color preview");
    // The ordinary material boundary reader intentionally excludes Landscape
    // subclasses. Apply its same unique structural-envelope bounds here, without
    // changing its importer contract or claiming the preceding native tail.
    FMaterialShaderMapEnvelope map;
    std::size_t candidates = 0, found = 0;
    if (item.Size > 64u * 1024u * 1024u)
        throw Refused("inline landscape scan exceeds its budget", "landscape color preview");
    for (std::size_t at = end; at + 20 <= item.Offset + item.Size; ++at)
    {
        if (body[at] != 1 || body[at + 1] || body[at + 2] || body[at + 3])
            continue;
        if (++candidates > 4096)
            throw Refused("inline landscape candidate limit", "landscape color preview");
        FMaterialShaderMapEnvelope candidate;
        FReadError error;
        std::vector<FMaterialShaderReference> refs;
        if (!ReadMaterialShaderMapEnvelope(body, at, item.Offset + item.Size, candidate, error) ||
            !ReadMaterialShaderReferences(candidate, refs, error))
            continue;
        if (++found != 1)
            throw Refused("ambiguous inline landscape shader envelope", "landscape color preview");
        map = std::move(candidate);
    }
    if (found != 1)
        throw Refused("inline landscape has no unique compiled shader owner", "landscape color preview");
    std::vector<FMaterialShaderReference> refs;
    FReadError error;
    if (!ReadMaterialShaderReferences(map, refs, error))
        throw Refused(error.Code, "landscape color preview");
    const FMaterialShaderReference* selected = nullptr;
    for (const auto& ref : refs)
        if (ref.Frequency == 3 && ref.VertexFactoryHash == 0xb2d60d07a558b78bull && ref.ResourceIndex == 0)
        {
            if (selected)
                throw Refused("the source base-pass shader association is ambiguous", "landscape color preview");
            selected = &ref;
        }
    // Resource 0 is the base-pass pixel shader only when it was compiled first. Otherwise take one of the base-pass
    // pixel shader types the Landscape factory has (they differ in light map policy, never in base color), in this
    // order: one of each type is present at most.
    for (const std::uint64_t type : {0xa8d53bcb8fca168dull, 0x666403914542b619ull})
        for (const auto& ref : refs)
            if (!selected && ref.Frequency == 3 && ref.VertexFactoryHash == 0xb2d60d07a558b78bull &&
                ref.TypeHash == type)
                selected = &ref;
    if (!selected)
    {
        std::string present;
        for (const auto& ref : refs)
            if (ref.Frequency == 3 && present.size() < 600)
            {
                char text[64];
                std::snprintf(text, sizeof(text), " %016llx/%016llx/%d", static_cast<unsigned long long>(ref.TypeHash),
                              static_cast<unsigned long long>(ref.VertexFactoryHash), int(ref.ResourceIndex));
                present += text;
            }
        throw Refused("the bounded source has no qualified Landscape base-pass pixel shader (pixel shaders by type/vertex "
                      "factory/resource:" + (present.empty() ? std::string(" none") : present) + ")",
                      "landscape color preview");
    }
    const auto indices = archive.MapIndices(map.ResourceHash);
    if (indices.empty())
        throw Refused("the inline shader is absent from the original archive", "landscape color preview");
    const auto shader = ArchiveIndexOf(indices, *selected, "the inline shader is absent from the original archive",
                                       "landscape color preview");
    const auto original = PrepareOriginalMaterialDomainPixelProgram(map, *selected, archive.Payload(shader),
                                                                    archive.Frequency(shader),
                                                                    Object({{"domain", "MD_Surface"}}),
                                                                    MaterialDomainSourceScope::ReadOnlyExpressions);
    const auto user = ReadOriginalMaterialUserData(parentAsset, map, resolve);
    MaterialRecoveryPlan plan;
    plan.Lines = NormalizeMaterialTraceDeclarations(original.Program.Lines).Lines;
    plan.Stop = plan.Lines.size();
    plan.Binding = original.Program.BaseBinding;
    plan.Settings = user.Settings;
    plan.Defaults = user.Defaults;
    plan.Defaults["textures"]["Weightmap0"] = weightmap;
    plan.Defaults["sampler_types"]["Weightmap0"] = "SAMPLERTYPE_LinearColor";
    plan.ConfigureBinding = [](SymbolicArena& arena, BindingContext& binding)
    {
        const auto uv = [&](int index, int component) { return source(arena, "TexCoord", index, component); };
        // UE4 LandscapeVertexFactory: weight UV = TexCoord3, height UV =
        // TexCoord5. These are a preview input contract, not a new factory
        // qualification for creator/export shaders.
        binding.Interpolants["in_texcoord1"] = {{uv(3, 0), uv(3, 1), uv(5, 0), uv(5, 1)}};
        binding.Interpolants["in_texcoord7"] = {{0., 0., 0., 1.}};
        binding.Interpolants["in_texcoord9"] = {{source(arena, "TranslatedWorldPositionNoOffsets", Value::null(), 0),
                                                 source(arena, "TranslatedWorldPositionNoOffsets", Value::null(), 1),
                                                 source(arena, "TranslatedWorldPositionNoOffsets", Value::null(), 2),
                                                 1.}};
    };
    const auto anchors = FindMaterialAnchors(ParseMaterialAnchorInstructions(plan.Lines));
    InlineLandscapeColorPreview result;
    package.Reference(std::int32_t(index + 1), result.Object);
    result.Object = LandscapePreviewObjectPath(package, result.Object);
    result.Parent = parent->Text;
    result.ResourceHash = map.ResourceHash;
    result.ShaderType = selected->TypeHash;
    result.Graph = RecoverMaterialExpressionGraph(PlanHeldMaterialOutputs(plan, anchors, {"BaseColor"})).Graph;
    return result;
}
} // namespace Material
} // namespace P4MapKit

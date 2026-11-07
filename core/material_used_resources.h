// Typed prerequisites from an internally recovered original owner graph. This
// is discovery, never renderer admission or permission to overwrite an asset.
#pragma once
#include "material_source_import.h"
#include "material_source_common.h"

namespace P4MapKit
{
namespace Material
{
enum class EMaterialUsedResourceKind
{
    Texture,
    Collection
};
enum class EMaterialUsedResourceStatus
{
    Complete,
    Incomplete
};
struct FMaterialUsedResourceReference
{
    std::string SourcePackage, SourceSha256, Property, Object, NativeClass;
    std::int32_t ImportIndex = 0;
};
struct FMaterialUsedResourceUse
{
    std::string Node, NodeType, Parameter;
    std::vector<std::size_t> CollectionUniforms;
    std::vector<std::string> CollectionStateIds;
};
struct FMaterialUsedResourceRequest
{
    EMaterialUsedResourceKind Kind = EMaterialUsedResourceKind::Texture;
    std::string Package, Object, NativeClass, SourceSha256, SamplerType;
    std::string Owner, OwnerSourceSha256, ResourceHash, GraphSha256;
    bool SourceAvailable = false;
    std::vector<FMaterialUsedResourceReference> References;
    std::vector<FMaterialUsedResourceUse> Uses;
};
struct FMaterialUsedResourceSource
{
    std::string Package, HeaderSha256, BodySha256, SourceSha256;
};
struct FMaterialUsedResourceProgram
{
    std::uint32_t ArchiveIndex = 0;
    std::uint8_t Frequency = 0;
    std::string PayloadSha256;
};
struct FMaterialUsedResourceDiscovery
{
    EMaterialUsedResourceStatus Status = EMaterialUsedResourceStatus::Incomplete;
    std::string Owner, OwnerSourceSha256, ResourceHash, GraphSha256, RefusalCategory, Refusal;
    std::size_t GraphNodes = 0, ReachableNodes = 0;
    std::vector<FMaterialUsedResourceSource> Sources;
    std::vector<FMaterialUsedResourceProgram> ArchiveReads;
    FMaterialShaderReference PrimaryReference;
    std::vector<FMaterialUsedResourceRequest> Requests;
    // Cached defaults absent from this owner's reachable graph request set.
    // Not a claim of whole-family/artist-graph nonuse or MissingAssets==0.
    std::vector<std::string> CachedMissingNotRequested;
    bool RendererProven = false, ProductionAdmitted = false;
    // The recovered owner graph and the parameter collections it was recovered with. A family
    // recovery of the same owner from the same reads and archive reuses it instead of recovering again.
    Json Graph;
    std::vector<std::string> CollectionPackages;
};
namespace MaterialUsedResourceDetail
{
inline bool TextureClass(const std::string& Class)
{
    return IsMember(Class, {"/Script/Engine.Texture2D", "/Script/Engine.TextureCube", "/Script/Engine.Texture2DArray",
                            "/Script/Engine.VolumeTexture", "/Script/Engine.TextureRenderTarget2D",
                            "/Script/MediaAssets.MediaTexture"});
}
inline std::string ObjectPackage(const std::string& Object)
{
    const auto Package = MaterialSourcePackage(Object);
    if (Package.compare(0, 6, "/Game/") || Object.find_first_of(" \t\r\n'\"<>|?*") != std::string::npos)
        throw Refused("used resource has no canonical original game identity", "material used resource identity");
    return Package;
}
struct FReader
{
    MaterialOriginalImportRawResolver Raw;
    std::map<std::string, MaterialOriginalImportCustody> Known;
    std::set<std::string> Missing;
    std::size_t Bytes = 0;
    explicit FReader(MaterialOriginalImportRawResolver In) : Raw(std::move(In))
    {
    }
    const MaterialOriginalImportCustody* Read(const std::string& Package)
    {
        if (Package != MaterialSourcePackage(Package))
            throw Refused("source resolver requires exact package identity", "material used source identity");
        const auto Prior = Known.find(Package);
        if (Prior != Known.end())
            return &Prior->second;
        if (Missing.count(Package))
            return nullptr;
        MaterialOriginalImportRawAsset Input;
        if (!Raw || !Raw(Package, Input))
        {
            Missing.insert(Package);
            return nullptr;
        }
        auto Source = ReadOriginalMaterialImportCustody(Input, Package);
        if (Source.Asset.Package.Package != Package)
            throw Refused("source resolver returned a case alias", "material used source identity");
        const auto Size = Input.Header->size() + Input.Body->size();
        if (Size > 128u * 1024u * 1024u - Bytes || Known.size() >= 4096)
            throw Refused("used resource source set exceeds its bound", "material used source bound");
        Bytes += Size;
        return &Known.emplace(Package, std::move(Source)).first->second;
    }
    MaterialSourceAssetResolver Resolver()
    {
        return [this](const std::string& Package, MaterialSourceAsset& Out)
        {
            const auto* Source = Read(MaterialSourcePackage(Package));
            if (!Source)
                return false;
            Out = Source->Asset;
            return true;
        };
    }
};
inline void References(const MaterialOriginalImportCustody& Source, const FDecodedValue& Value,
                       std::vector<FMaterialUsedResourceReference>& Out, const std::string& Path = {},
                       std::size_t Depth = 0)
{
    if (Depth > 128 || Out.size() >= 65536)
        throw Refused("source reference traversal exceeds its bound", "material used resource reference bound");
    if (Value.Kind == FDecodedValue::EKind::HardReference && Value.ReferenceIndex)
    {
        if (Value.ReferenceIndex < 0)
        {
            const auto Index = std::size_t(-std::int64_t(Value.ReferenceIndex) - 1);
            if (Index >= Source.Asset.Package.Imports.size())
                throw Refused("source import index is invalid", "material used resource reference");
            const auto& Import = Source.Asset.Package.Imports[Index];
            const auto Class = Import.ClassPackage + "." + Import.Class;
            std::string Object;
            if (!Source.Asset.Package.Reference(Value.ReferenceIndex, Object) || Object != Value.Text)
                throw Refused("decoded reference differs from source import", "material used resource reference");
            if (TextureClass(Class) || Class == "/Script/Engine.MaterialParameterCollection")
                Out.push_back(
                    {Source.Asset.Package.Package, Source.SourceSha256, Path, Object, Class, Value.ReferenceIndex});
        }
    }
    for (const auto& Field : Value.Fields)
        References(Source, Field.second, Out, Path.empty() ? Field.first : Path + "." + Field.first, Depth + 1);
    for (std::size_t Index = 0; Index < Value.Items.size(); ++Index)
        References(Source, Value.Items[Index], Out, Path + "[" + std::to_string(Index) + "]", Depth + 1);
}
inline std::vector<FMaterialUsedResourceReference> ChainReferences(FReader& Reader,
                                                                   const std::vector<FMaterialCookedRecord>& Chain)
{
    std::vector<FMaterialUsedResourceReference> Result;
    for (const auto& Record : Chain)
    {
        const auto* Source = Reader.Read(Record.Package);
        if (!Source)
            throw Refused("actual material ancestor source is missing", "material used source ancestor");
        std::size_t Roots = 0;
        for (const auto& Export : Source->Asset.Package.Exports)
        {
            std::string Class;
            if (!Source->Asset.Package.Reference(Export.Class, Class))
                throw Refused("ancestor export class is invalid", "material used source ancestor");
            const auto Short =
                Class.substr(Class.find_last_of('.') == std::string::npos ? 0 : Class.find_last_of('.') + 1);
            if (Short != "Material" && Short != "MaterialInstanceConstant")
                continue;
            if (++Roots != 1 || Export.Outer || Class != "/Script/Engine." + Record.Class)
                throw Refused("ancestor material root is ambiguous or class-mismatched",
                              "material used source ancestor");
        }
        if (Roots != 1)
            throw Refused("ancestor material root is missing", "material used source ancestor");
        References(*Source, Record.Properties, Result);
    }
    return Result;
}
inline std::string ActualObjectClass(const MaterialOriginalImportCustody& Source, const std::string& Object,
                                     FDecodedValue& Properties)
{
    std::string Found;
    std::size_t Count = 0;
    const auto& Package = Source.Asset.Package;
    for (std::size_t Index = 0; Index < Package.Exports.size(); ++Index)
    {
        const auto& Export = Package.Exports[Index];
        if (Export.Outer)
            continue;
        std::string Path, Class;
        if (!Package.Reference(std::int32_t(Index + 1), Path) || !Package.Reference(Export.Class, Class))
            throw Refused("resource export reference is invalid", "material used resource export");
        if (Package.Package + "." + Path != Object)
            continue;
        if (++Count != 1)
            throw Refused("resource object is ambiguous", "material used resource export");
        auto At = Export.Offset;
        FPropertyDecoder Decoder(Package, *Source.Asset.Body);
        if (!Decoder.TypedProperties(At, Export.Offset + Export.Size, Properties))
            throw Refused(Decoder.Error.Code, "material used resource properties");
        Found = Class;
    }
    if (Count != 1)
        throw Refused("actual resource object does not match the source reference", "material used resource export");
    return Found;
}
inline std::set<std::string> Reachable(const Json& Graph)
{
    ValidateGraph(Graph);
    std::set<std::string> Seen;
    std::vector<std::string> Pending;
    for (const auto& Output : Graph.At("outputs").object)
        Pending.push_back(Output.second.array.at(0).Text());
    while (!Pending.empty())
    {
        const auto Node = Pending.back();
        Pending.pop_back();
        if (!Seen.insert(Node).second)
            continue;
        if (Seen.size() > 100000)
            throw Refused("reachable graph exceeds discovery bound", "material used graph bound");
        for (const auto& Input : Graph.At("nodes").At(Node).At("inputs").object)
            Pending.push_back(Input.second.array.at(0).Text());
    }
    return Seen;
}
inline std::vector<FMaterialUsedResourceRequest>
GraphRequests(const Json& Graph, const MaterialOriginalOwnerSource& Owner,
              const std::vector<FMaterialUsedResourceReference>& References, FReader& Reader,
              std::size_t& ReachableNodes)
{
    const auto Nodes = Reachable(Graph);
    ReachableNodes = Nodes.size();
    std::map<std::string, FMaterialUsedResourceRequest> Result;
    for (const auto& Name : Nodes)
    {
        const auto& Node = Graph.At("nodes").At(Name);
        const auto Type = Node.At("type").Text();
        const auto& Props = Node.At("props");
        const bool Collection = Type == "CollectionParameter";
        const bool Texture =
            IsMember(Type, {"TextureSample", "TextureSampleParameter2D", "TextureSampleParameterCube",
                            "TextureSampleParameterSubUV", "TextureObject", "TextureObjectParameter", "ParticleSubUV"});
        if (!Collection && !Texture)
            continue;
        const char* Field = Collection ? "Collection" : "Texture";
        if (!Props.Has(Field) || Props.At(Field).kind != Json::String || Props.At(Field).Text().empty())
            throw Refused("used graph node has no concrete original resource", "material used graph resource");
        const auto Selection = Props.At(Field).Text();
        const bool Full = Selection.find('.') != std::string::npos;
        const bool EngineTexture = Texture && !MaterialSourcePackage(Selection).compare(0, 8, "/Engine/") &&
                                   Selection.find_first_of(" \t\r\n'\"<>|?*") == std::string::npos;
        const auto Package = EngineTexture ? MaterialSourcePackage(Selection) : ObjectPackage(Selection);
        std::vector<FMaterialUsedResourceReference> Matches;
        for (const auto& Reference : References)
        {
            if (MaterialSourcePackage(Reference.Object) != Package || (Full && Reference.Object != Selection))
                continue;
            if (Collection ? (Reference.NativeClass != "/Script/Engine.MaterialParameterCollection")
                           : !TextureClass(Reference.NativeClass))
                continue;
            if (!Matches.empty() &&
                (Matches.front().Object != Reference.Object || Matches.front().NativeClass != Reference.NativeClass))
                throw Refused("used resource references disagree about object/class",
                              "material used resource ambiguous");
            Matches.push_back(Reference);
        }
        if (Matches.empty())
            throw Refused("used graph resource has no actual typed hard source reference",
                          "material used resource unbound");
        const auto& Identity = Matches.front();
        if (Type == "TextureSampleParameterCube" && Identity.NativeClass != "/Script/Engine.TextureCube")
            throw Refused("cube graph node disagrees with actual resource class", "material used resource class");
        if ((Type == "TextureSampleParameter2D" || Type == "TextureSampleParameterSubUV" || Type == "ParticleSubUV") &&
            !IsMember(Identity.NativeClass, {"/Script/Engine.Texture2D", "/Script/Engine.TextureRenderTarget2D",
                                             "/Script/MediaAssets.MediaTexture"}))
            throw Refused("2D graph node disagrees with actual resource class", "material used resource class");
        // Engine textures, including the cave sky's default cube, use the editor's asset and need no
        // game import. They must first pass the same typed-reference and consumer-class checks.
        if (EngineTexture)
            continue;
        FMaterialUsedResourceUse Use;
        Use.Node = Name;
        Use.NodeType = Type;
        if (Props.Has("ParameterName"))
            Use.Parameter = Props.At("ParameterName").Text();
        if (Collection)
        {
            for (std::size_t Index = 0; Index < Owner.User.Collections.size(); ++Index)
            {
                const auto& Bound = Owner.User.Collections[Index];
                if (!Bound.Available || Bound.Package != Package)
                    continue;
                if (Index >= Owner.Map.Uniforms.Collections.size() ||
                    (std::find(Bound.Scalars.begin(), Bound.Scalars.end(), Use.Parameter) == Bound.Scalars.end() &&
                     std::find(Bound.Vectors.begin(), Bound.Vectors.end(), Use.Parameter) == Bound.Vectors.end()))
                    throw Refused("collection parameter has no actual bound source word",
                                  "material used collection binding");
                Use.CollectionUniforms.push_back(Index);
                const auto& Guid = Owner.Map.Uniforms.Collections[Index];
                Use.CollectionStateIds.push_back(CustomMaterial::Hex(CustomMaterial::Bytes(Guid.begin(), Guid.end())));
            }
            if (Use.CollectionUniforms.empty())
                throw Refused("used collection lacks a source StateId binding", "material used collection binding");
        }
        auto Known = Result.find(Package);
        if (Known == Result.end())
        {
            FMaterialUsedResourceRequest Request;
            Request.Kind = Collection ? EMaterialUsedResourceKind::Collection : EMaterialUsedResourceKind::Texture;
            Request.Package = Package;
            Request.Object = Identity.Object;
            Request.NativeClass = Identity.NativeClass;
            Request.References = Matches;
            if (const auto* Actual = Reader.Read(Package))
            {
                FDecodedValue Properties;
                const auto Class = ActualObjectClass(*Actual, Request.Object, Properties);
                if (Class != Request.NativeClass)
                    throw Refused("resource source class differs from actual material import",
                                  "material used resource class");
                Request.SourceAvailable = true;
                Request.SourceSha256 = Actual->SourceSha256;
                if (Texture)
                {
                    FReadError Error;
                    const auto Short = Class.substr(Class.find_last_of('.') + 1);
                    if (!ReadMaterialSamplerType(Short, Properties, Request.SamplerType, Error))
                        throw Refused(Error.Code, "material used resource sampler");
                }
            }
            Known = Result.emplace(Package, std::move(Request)).first;
        }
        if (Known->second.Object != Identity.Object || Known->second.NativeClass != Identity.NativeClass ||
            Known->second.Kind !=
                (Collection ? EMaterialUsedResourceKind::Collection : EMaterialUsedResourceKind::Texture))
            throw Refused("one used package has conflicting resource roles", "material used resource conflict");
        if (Texture && Known->second.SourceAvailable && Props.Has("SamplerType") &&
            Props.At("SamplerType").Text() != Known->second.SamplerType)
            throw Refused("graph sampler differs from source texture properties", "material used resource sampler");
        Known->second.Uses.push_back(std::move(Use));
    }
    std::vector<FMaterialUsedResourceRequest> Requests;
    for (auto& Pair : Result)
        Requests.push_back(std::move(Pair.second));
    return Requests;
}
}

// Caller supplies exact immutable current source buffers and existing real
// archive access. An unavailable anonymous fixed texture or MPC can refuse
// BEFORE a graph exists: that is Incomplete, with no invented resource/value.
// Complete means only this internally recovered graph's resources were typed;
// requests are not acquisition authority, shader-ready graphs or edit admission.
inline FMaterialUsedResourceDiscovery
DiscoverOriginalMaterialUsedResources(const std::string& OwnerPackage, const MaterialOriginalImportRawResolver& Raw,
                                      const MaterialOriginalArchiveAccess& Archive)
{
    FMaterialUsedResourceDiscovery Result;
    Result.Owner = OwnerPackage;
    try
    {
        MaterialUsedResourceDetail::FReader Reader(Raw);
        const auto* Selected = Reader.Read(OwnerPackage);
        if (!Selected)
            throw Refused("original shader owner source is missing", "material used source owner");
        Result.OwnerSourceSha256 = Selected->SourceSha256;
        const auto Resolve = Reader.Resolver();
        const auto Chain = ReadOriginalMaterialParentChain(Selected->Asset, Resolve);
        const auto References = MaterialUsedResourceDetail::ChainReferences(Reader, Chain);
        std::vector<MaterialSourceAsset> Collections;
        std::set<std::string> CollectionPackages;
        // Only source-typed MPC references are considered here. No resolve-all
        // hard-reference scan or speculative filename/class matching.
        for (const auto& Reference : References)
            if (Reference.NativeClass == "/Script/Engine.MaterialParameterCollection")
            {
                const auto Package = MaterialSourcePackage(Reference.Object);
                if (CollectionPackages.insert(Package).second)
                    if (const auto* Source = Reader.Read(Package))
                        Collections.push_back(Source->Asset);
            }
        const auto Owner = ReadOriginalMaterialOwnerSource(Selected->Asset, Resolve, Collections);
        Result.ResourceHash =
            CustomMaterial::Hex(CustomMaterial::Bytes(Owner.Map.ResourceHash.begin(), Owner.Map.ResourceHash.end()));
        if (!Archive.MapIndices || !Archive.Payload || !Archive.Frequency)
            throw Refused("actual shader archive access is incomplete", "material used resource archive");
        const auto ResourceIndices = Archive.MapIndices(Owner.Map.ResourceHash);
        std::map<std::uint32_t, FMaterialUsedResourceProgram> Programs;
        auto Tracked = Archive;
        Tracked.MapIndices = [&](const std::array<std::uint8_t, 20>& Hash)
        {
            if (Hash != Owner.Map.ResourceHash)
                throw Refused("graph requested an unrelated archive resource map", "material used resource archive");
            const auto Indices = Archive.MapIndices(Hash);
            if (Indices != ResourceIndices)
                throw Refused("archive resource indices changed during graph discovery",
                              "material used resource archive");
            return Indices;
        };
        Tracked.Payload = [&](std::uint32_t Index)
        {
            if (std::find(ResourceIndices.begin(), ResourceIndices.end(), Index) == ResourceIndices.end())
                throw Refused("graph payload is outside the source resource map", "material used resource archive");
            auto Payload = Archive.Payload(Index);
            const auto Hash = PayloadDigest(Payload);
            const auto Frequency = Archive.Frequency(Index);
            const auto Prior = Programs.find(Index);
            if (Prior != Programs.end() &&
                (Prior->second.PayloadSha256 != Hash || Prior->second.Frequency != Frequency))
                throw Refused("archive payload changed within graph discovery", "material used resource archive");
            Programs[Index] = {Index, Frequency, Hash};
            return Payload;
        };
        const auto Recovered = RecoverOriginalMaterialSourceExpressionGraph(
            Owner, Tracked, MaterialDomainSourceScope::ReadOnlyExpressions);
        const auto& Graph = Recovered.Primary.Material.Graph;
        Result.Graph = Graph;
        for (const auto& Collection : Collections)
            Result.CollectionPackages.push_back(Collection.Package.Package);
        Result.PrimaryReference = Recovered.Primary.Source.Program.Reference;
        Result.GraphSha256 = TextDigest(Graph.Dump());
        Result.GraphNodes = Graph.At("nodes").object.size();
        Result.Requests =
            MaterialUsedResourceDetail::GraphRequests(Graph, Owner, References, Reader, Result.ReachableNodes);
        for (auto& Request : Result.Requests)
        {
            Request.Owner = OwnerPackage;
            Request.OwnerSourceSha256 = Result.OwnerSourceSha256;
            Request.ResourceHash = Result.ResourceHash;
            Request.GraphSha256 = Result.GraphSha256;
        }
        std::set<std::string> Requested;
        for (const auto& Request : Result.Requests)
            Requested.insert(Request.Package);
        for (const auto& Missing : Owner.User.MissingAssets)
            if (!Requested.count(Missing))
                Result.CachedMissingNotRequested.push_back(Missing);
        std::sort(Result.CachedMissingNotRequested.begin(), Result.CachedMissingNotRequested.end());
        Result.CachedMissingNotRequested.erase(
            std::unique(Result.CachedMissingNotRequested.begin(), Result.CachedMissingNotRequested.end()),
            Result.CachedMissingNotRequested.end());
        for (const auto& Source : Reader.Known)
            Result.Sources.push_back(
                {Source.first, Source.second.HeaderSha256, Source.second.BodySha256, Source.second.SourceSha256});
        for (const auto& Program : Programs)
            Result.ArchiveReads.push_back(Program.second);
        Result.Status = EMaterialUsedResourceStatus::Complete;
    }
    catch (const Refused& Failure)
    {
        Result.Status = EMaterialUsedResourceStatus::Incomplete;
        Result.RefusalCategory = Failure.name;
        Result.Refusal = Failure.what();
        Result.Requests.clear();
        Result.Graph = Json();
        Result.CollectionPackages.clear();
    }
    return Result;
}
}
}

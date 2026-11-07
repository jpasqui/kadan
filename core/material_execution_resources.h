// Initial material prerequisites from authored values and typed collection
// imports. Shader-USED edges are a separate all-owner discovery.
#pragma once
#include "material_source_import.h"
namespace P4MapKit
{
namespace Material
{
enum class EMaterialExecutionResourceKind
{
    Parent,
    TextureOverride,
    ParameterCollection
};
struct MaterialExecutionUnsupported : Refused
{
    MaterialExecutionUnsupported(const std::string& Why, const std::string& Category) : Refused(Why, Category)
    {
    }
};
struct FMaterialExecutionResource
{
    EMaterialExecutionResourceKind Kind = EMaterialExecutionResourceKind::Parent;
    std::string Package, Object, NativeClass, Parameter, Property;
    std::int32_t ImportIndex = 0;
};
struct FMaterialExecutionSource
{
    std::string Package, Object, NativeClass, HeaderSha256, BodySha256, SourceSha256;
    std::vector<FMaterialExecutionResource> Resources;
};
namespace MaterialExecutionResourceDetail
{
inline FMaterialExecutionResource Hard(const MaterialOriginalImportCustody& Source, const FDecodedValue& Value,
                                       EMaterialExecutionResourceKind Kind, const std::string& Property,
                                       const std::string& Parameter = {})
{
    if (Value.Kind != FDecodedValue::EKind::HardReference || Value.ReferenceIndex >= 0)
        throw MaterialExecutionUnsupported("execution edge is not an actual external hard source import",
                                           "material execution hard reference");
    const auto Index = std::size_t(-std::int64_t(Value.ReferenceIndex) - 1);
    if (Index >= Source.Asset.Package.Imports.size())
        throw Refused("execution edge import is out of range", "material execution import");
    std::string Object;
    if (!Source.Asset.Package.Reference(Value.ReferenceIndex, Object) || Object != Value.Text)
        throw Refused("execution edge differs from actual source import", "material execution import");
    const auto& Import = Source.Asset.Package.Imports[Index];
    const auto Class = Import.ClassPackage + "." + Import.Class;
    const auto Package = MaterialSourcePackage(Object);
    if (Package.compare(0, 6, "/Game/") || Object.find('.') == std::string::npos ||
        Package == Source.Asset.Package.Package)
        throw MaterialExecutionUnsupported("execution edge has no distinct canonical game object",
                                           "material execution identity");
    const bool Valid =
        Kind == EMaterialExecutionResourceKind::ParameterCollection
            ? Class == "/Script/Engine.MaterialParameterCollection"
        : Kind == EMaterialExecutionResourceKind::Parent
            ? IsMember(Class, {"/Script/Engine.Material", "/Script/Engine.MaterialInstanceConstant"})
            : IsMember(Class, {"/Script/Engine.Texture2D", "/Script/Engine.TextureCube",
                               "/Script/Engine.Texture2DArray", "/Script/Engine.VolumeTexture",
                               "/Script/Engine.TextureRenderTarget2D", "/Script/MediaAssets.MediaTexture"});
    if (!Valid)
        throw MaterialExecutionUnsupported("execution edge actual import class is unsupported",
                                           "material execution class");
    return {Kind, Package, Object, Class, Parameter, Property, Value.ReferenceIndex};
}
inline std::vector<FMaterialExecutionResource> Instance(const MaterialOriginalImportCustody& Source,
                                                        const FMaterialCookedRecord& Record)
{
    // Reuse the actual importer's typed local-texture projection, restricted to
    // these execution fields. Unrelated scalar/render overrides may require
    // constructor defaults and remain the later importer's responsibility.
    // This is not whole-MIC reconstruction/admission or metadata deletion.
    FMaterialCookedRecord Local;
    Local.Package = Record.Package;
    Local.Class = Record.Class;
    Local.Properties.Kind = FDecodedValue::EKind::Object;
    std::size_t Parents = 0, Textures = 0;
    for (const auto& Field : Record.Properties.Fields)
        if (Field.first == "Parent" || Field.first == "TextureParameterValues")
        {
            if (Field.first == "Parent")
                ++Parents;
            else
                ++Textures;
            Local.Properties.Fields.push_back(Field);
        }
    if (!Parents)
        throw MaterialExecutionUnsupported("MIC source has no authored Parent", "material execution parent");
    if (Parents != 1 || Textures > 1)
        throw Refused("execution source fields are ambiguous", "material execution source fields");
    Json Projected;
    try
    {
        Projected = ProjectOriginalMaterialInstanceForImport(Local);
    }
    catch (const Refused& Error)
    {
        throw MaterialExecutionUnsupported(Error.what(), Error.name);
    }
    const auto* Parent = Record.Properties.Field("Parent");
    if (!Parent)
        throw Refused("MIC source has no Parent", "material execution parent");
    std::vector<FMaterialExecutionResource> Result{
        Hard(Source, *Parent, EMaterialExecutionResourceKind::Parent, "Parent")};
    if (Projected.At("parent").Text() != Result.front().Object)
        throw Refused("projected parent differs from typed source", "material execution parent");
    std::size_t NullTextures = 0;
    if (const auto* Rows = Record.Properties.Field("TextureParameterValues"))
        for (std::size_t Index = 0; Index < Rows->Items.size(); ++Index)
        {
            const auto& Row = Rows->Items[Index];
            const auto Name = OriginalMaterialImportParameterName(Row);
            const auto* Value = Row.Field("ParameterValue");
            if (!Value)
                throw Refused("texture override has no authored value", "material execution texture override");
            if (Value->Kind == FDecodedValue::EKind::HardReference && !Value->ReferenceIndex && Value->Text.empty())
            {
                if (!Projected.At("textures").Has(Name) || Projected.At("textures").At(Name).kind != Json::Null)
                    throw Refused("null texture override differs from typed source",
                                  "material execution texture override");
                ++NullTextures;
                continue; // An explicit null is retained in the recipe; it requests no asset.
            }
            auto Edge = Hard(Source, *Value, EMaterialExecutionResourceKind::TextureOverride,
                             "TextureParameterValues[" + std::to_string(Index) + "].ParameterValue", Name);
            if (!Projected.At("textures").Has(Name) || Projected.At("textures").At(Name).Text() != Edge.Object)
                throw Refused("projected texture override differs from typed source",
                              "material execution texture override");
            Result.push_back(std::move(Edge));
        }
    if (Result.size() + NullTextures != Projected.At("textures").object.size() + 1)
        throw Refused("authored texture override coverage differs", "material execution texture override");
    return Result;
}
inline void Collections(const MaterialOriginalImportCustody& Source, std::vector<FMaterialExecutionResource>& Out)
{
    // Collection layouts are needed to interpret the shader before shader-used resources can
    // be discovered. Bootstrap only actual typed hard imports, without adding cached textures
    // or functions to the execution graph or guessing familiar collection names.
    std::set<std::string> Seen;
    for (std::size_t Index = 0; Index < Source.Asset.Package.Imports.size(); ++Index)
    {
        const auto& Import = Source.Asset.Package.Imports[Index];
        if (Import.ClassPackage != "/Script/Engine" || Import.Class != "MaterialParameterCollection")
            continue;
        FDecodedValue Value;
        Value.Kind = FDecodedValue::EKind::HardReference;
        Value.ReferenceIndex = -std::int32_t(Index) - 1;
        if (!Source.Asset.Package.Reference(Value.ReferenceIndex, Value.Text))
            throw Refused("collection prerequisite has an invalid hard import", "material execution collection");
        auto Edge = Hard(Source, Value, EMaterialExecutionResourceKind::ParameterCollection,
                         "Imports[" + std::to_string(Index) + "]");
        if (Seen.insert(Edge.Object).second)
            Out.push_back(std::move(Edge));
    }
}
}
// Unreal names ignore case, and the game ships packages whose object differs from the package's leaf in case
// only (MI_chr_Nightkochappy_00_M00 holds MI_chr_NightKochappy_00_M00). The object keeps its own spelling.
inline bool SameObjectName(const std::string& A, const std::string& B)
{
    if (A.size() != B.size())
        return false;
    for (std::size_t I = 0; I < A.size(); ++I)
    {
        char X = A[I], Y = B[I];
        if (X >= 'A' && X <= 'Z')
            X = char(X - 'A' + 'a');
        if (Y >= 'A' && Y <= 'Z')
            Y = char(Y - 'A' + 'a');
        if (X != Y)
            return false;
    }
    return true;
}
inline FMaterialExecutionSource ReadOriginalMaterialExecutionResources(const MaterialOriginalImportRawAsset& Raw,
                                                                       const std::string& Expected)
{
    auto Source = ReadOriginalMaterialImportCustody(Raw, Expected);
    const auto& Package = Source.Asset.Package;
    if (Package.Package != Expected || Expected != MaterialSourcePackage(Expected) || Expected.compare(0, 6, "/Game/"))
        throw Refused("execution source package is not exact canonical game identity",
                      "material execution source identity");
    const auto Leaf = Expected.substr(Expected.find_last_of('/') + 1);
    std::size_t Roots = 0;
    std::string RootClass, RootObject;
    for (std::size_t Index = 0; Index < Package.Exports.size(); ++Index)
    {
        const auto& Export = Package.Exports[Index];
        std::string Class, Object;
        if (!Package.Reference(Export.Class, Class))
            throw Refused("execution source export class is invalid", "material execution source root");
        const auto Short = Class.substr(Class.find_last_of('.') == std::string::npos ? 0 : Class.find_last_of('.') + 1);
        if (Short != "Material" && Short != "MaterialInstanceConstant")
            continue;
        if (++Roots != 1 || Export.Outer || Class != "/Script/Engine." + Short ||
            !Package.Reference(std::int32_t(Index + 1), Object) || !SameObjectName(Object, Leaf))
            throw Refused("execution source material root is ambiguous/noncanonical", "material execution source root");
        RootClass = Class;
        RootObject = Expected + "." + Object;
    }
    if (Roots != 1)
        throw Refused("execution source has no canonical Material/MIC root", "material execution source root");
    FMaterialCookedRecord Record;
    FReadError Error;
    if (!ReadMaterialCookedRecord(Package, *Raw.Body, Record, Error))
        throw Refused(Error.Code, "material execution properties");
    FMaterialExecutionSource Result;
    Result.Package = Expected;
    Result.Object = RootObject;
    Result.NativeClass = RootClass;
    Result.HeaderSha256 = Source.HeaderSha256;
    Result.BodySha256 = Source.BodySha256;
    Result.SourceSha256 = Source.SourceSha256;
    if (Record.Class == "MaterialInstanceConstant")
        Result.Resources = MaterialExecutionResourceDetail::Instance(Source, Record);
    MaterialExecutionResourceDetail::Collections(Source, Result.Resources);
    return Result;
}
}
}

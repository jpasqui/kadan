// Native cooked mesh observations from held bytes. No source, creator, physics,
// installed-default, dependency or editable-asset admission is conferred here.
#pragma once
#include "property_decoder.h"
#include "static_mesh_render.h"

namespace P4MapKit
{
struct FCookedStaticMeshMaterialFacts
{
    FDecodedValue MaterialInterface, MaterialSlotName, ImportedMaterialSlotName, NativeMaterialSlotName;
    bool UVInitialized = false, UVOverride = false;
    std::array<float, 4> UVDensities{};
};
struct FCookedStaticMeshFacts
{
    std::string Package, ExportObject, BodyObject;
    std::int32_t ExportIndex = 0, BodyIndex = 0;
    FPackageExport MeshExport, BodyExport;
    FPropertyInventory MeshPropertyTags, BodyPropertyTags;
    FDecodedValue MeshProperties, BodyProperties;
    FStaticMeshRenderPayload Render;
    std::vector<FCookedStaticMeshMaterialFacts> Materials;
    // Exact bytes only: the native BodySetup/PhysX tail is not interpreted by
    // this reader. The physics reader must use the original inventory/identity.
    std::size_t BodyNativeOffset = 0;
    std::vector<std::uint8_t> BodyNativeTail;
};
namespace StaticMeshFactsDetail
{
inline bool Reject(FReadError& Error, std::size_t At, const char* Code)
{ Error = {At, Code}; return false; }
inline bool PackageName(const std::string& Name)
{
    if (Name.size() > 4096 || Name.size() < 7 || Name.front() != '/' ||
        Name.back() == '/' || Name.find("//") != std::string::npos) return false;
    for (const auto C : Name)
        if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
              (C >= '0' && C <= '9') || C == '_' || C == '/')) return false;
    return Name.compare(0, 6, "/Game/") == 0 || Name.compare(0, 8, "/Engine/") == 0;
}
inline bool ScalarFloat(const FDecodedValue* Value, float Expected)
{
    if (!Value || Value->Kind != FDecodedValue::EKind::Number || Value->Type != "FloatProperty") return false;
    std::istringstream Token(Value->Text); Token.imbue(std::locale::classic());
    double Number = 0; Token >> Number; const float Single = float(Number);
    return Token && Token.peek() == std::char_traits<char>::eof() && std::isfinite(Single) &&
        std::memcmp(&Single, &Expected, sizeof(float)) == 0;
}
inline const FPropertyEnvelope* Tag(const FPropertyInventory& Tags, const std::string& Name)
{
    const FPropertyEnvelope* Found = nullptr;
    for (const auto& Entry : Tags.Properties) if (Entry.Name == Name && !Entry.ArrayIndex)
    { if (Found) return nullptr; Found = &Entry; }
    return Found;
}
inline bool PlainStruct(const FPropertyEnvelope& Tag, const std::string& Name)
{
    if (Tag.Type != "StructProperty" || Tag.Detail != std::vector<std::string>{Name} || Tag.HasGuid || Tag.ArrayIndex) return false;
    for (const auto Word : Tag.StructGuid) if (Word) return false;
    return true;
}
inline bool Materials(const FPackageInventory& Inventory, const std::vector<std::uint8_t>& Body,
    const FPropertyInventory& Tags, const FDecodedValue& Properties, const FStaticMeshRenderPayload& Render,
    std::vector<FCookedStaticMeshMaterialFacts>& Out)
{
    using Kind = FDecodedValue::EKind;
    const auto* Values = Properties.Field("StaticMaterials");
    const auto* MaterialTag = Tag(Tags, "StaticMaterials");
    if (!MaterialTag || MaterialTag->Type != "ArrayProperty" || MaterialTag->Detail != std::vector<std::string>{"StructProperty"} ||
        MaterialTag->HasGuid || !Values || Values->Kind != Kind::Array ||
        Values->Items.size() != Render.Materials.size()) return false;
    FByteReader Descriptor(Body, MaterialTag->PayloadOffset, MaterialTag->PayloadOffset + MaterialTag->Size);
    std::size_t Count = 0; FPropertyEnvelope Inner;
    if (!Descriptor.Count(Count, 4096) || !ReadPropertyEnvelope(Descriptor, Inventory.Names, Body.size(), Inner) ||
        Count != Values->Items.size() || Descriptor.Tell() != Descriptor.Limit() ||
        Inner.Name != "StaticMaterials" || !PlainStruct(Inner, "StaticMaterial")) return false;
    std::size_t Position = Inner.PayloadOffset;
    // The contract's terminal native material array has fixed-width entries:
    // object index, FName, two int32 booleans and four binary32 densities.
    // The render reader already proves exact exhaustion of this native tail.
    const std::size_t NativeBytes = 4 + Render.Materials.size() * 36;
    if (Render.End < NativeBytes || Render.End > Body.size()) return false;
    FByteReader NativeNames(Body, Render.End - NativeBytes, Render.End);
    std::size_t NativeCount = 0;
    if (!NativeNames.Count(NativeCount, 4096) || NativeCount != Count) return false;
    for (std::size_t Index = 0; Index < Values->Items.size(); ++Index)
    {
        const auto EntryTags = ReadPropertyEnvelopes(Body, Inventory.Names, Position, Inner.PayloadOffset + Inner.Size);
        const auto* UVTag = Tag(EntryTags, "UVChannelData");
        if (!EntryTags.Error.Code.empty() || EntryTags.Properties.size() != 4 || !UVTag ||
            !PlainStruct(*UVTag, "MeshUVChannelInfo")) return false;
        const auto& Value = Values->Items[Index]; const auto& Native = Render.Materials[Index];
        const auto* Interface = Value.Field("MaterialInterface");
        const auto* Slot = Value.Field("MaterialSlotName");
        const auto* Imported = Value.Field("ImportedMaterialSlotName");
        const auto* UV = Value.Field("UVChannelData");
        if (Value.Kind != Kind::Object || Value.Fields.size() != 4 || !Interface || !Slot || !Imported || !UV ||
            Interface->Kind != Kind::HardReference || Interface->Type != "ObjectProperty" ||
            Interface->ReferenceIndex != Native.ObjectIndex || Slot->Kind != Kind::Name ||
            Slot->Type != "NameProperty" || Slot->Text != Native.Slot || Imported->Kind != Kind::Name ||
            Imported->Type != "NameProperty" || UV->Kind != Kind::Object || UV->Fields.size() != 6) return false;
        std::int32_t NativeObject = 0, NameIndex = 0, NameNumber = 0;
        if (!NativeNames.I32(NativeObject) || !NativeNames.I32(NameIndex) || !NativeNames.I32(NameNumber) ||
            !NativeNames.Skip(24) || NativeObject != Interface->ReferenceIndex || NameIndex < 0 || NameNumber < 0 ||
            std::size_t(NameIndex) >= Inventory.Names.size() || Inventory.Names[std::size_t(NameIndex)] != Slot->NameBase ||
            NameNumber != Slot->NameNumber) return false;
        const auto* Initialized = UV->Field("bInitialized"); const auto* Override = UV->Field("bOverrideDensities");
        if (!Initialized || !Override || Initialized->Kind != Kind::Boolean || Override->Kind != Kind::Boolean ||
            Initialized->Type != "BoolProperty" || Override->Type != "BoolProperty" ||
            Initialized->Text != (Native.UVInitialized ? "true" : "false") ||
            Override->Text != (Native.UVOverride ? "true" : "false")) return false;
        for (std::size_t Channel = 0; Channel < 4; ++Channel)
            if (!ScalarFloat(UV->Field("LocalUVDensities" + (Channel ? "[" + std::to_string(Channel) + "]" : "")),
                Native.UVDensities[Channel])) return false;
        FCookedStaticMeshMaterialFacts Material;
        Material.MaterialInterface = *Interface; Material.MaterialSlotName = *Slot;
        Material.NativeMaterialSlotName = FDecodedValue::Atom(Kind::Name, Native.Slot);
        Material.NativeMaterialSlotName.NameIndex = NameIndex; Material.NativeMaterialSlotName.NameNumber = NameNumber;
        Material.NativeMaterialSlotName.NameBase = Inventory.Names[std::size_t(NameIndex)];
        Material.ImportedMaterialSlotName = *Imported; Material.UVInitialized = Native.UVInitialized;
        Material.UVOverride = Native.UVOverride; Material.UVDensities = Native.UVDensities;
        Out.push_back(std::move(Material));
        Position = EntryTags.End;
    }
    return Position == Inner.PayloadOffset + Inner.Size && NativeNames.Tell() == NativeNames.Limit();
}
}

// Inventory must have been decoded from the same held header/body pair by a
// qualified header profile. CanonicalPackage supplies an explicit owner when
// retoc stores None; its text does not prove original or creator custody.
// Bulk context/profile qualification and immutable sidecar ownership remain
// with the caller. This reader accepts only the existing retoc UE4.26 layout.
// All failures, including cancellation and late body/property failure, retain Out.
inline bool ReadCookedStaticMeshFacts(const FPackageInventory& Inventory,
    const std::vector<std::uint8_t>& Body, const std::string& CanonicalPackage,
    const std::string& ExportObject, const FByteBulkContext& Context,
    const FStaticMeshBulkRead& ReadAt, FCookedStaticMeshFacts& Out, FReadError& Error,
    const FStaticMeshRenderLimits& Limits = {}, const std::function<bool()>& Cancel = {})
{
    using namespace StaticMeshFactsDetail;
    Error = {};
    if (!Inventory.Error.Code.empty()) { Error = Inventory.Error; return false; }
    if (!PackageName(CanonicalPackage) ||
        (!Inventory.Package.empty() && Inventory.Package != "None" && Inventory.Package != CanonicalPackage))
        return Reject(Error, 0, "static-mesh-facts-package-identity");
    if (Body.size() > Limits.InputBytes || Inventory.HeaderSize != Context.HeaderSize ||
        Context.FileSizes[0] != std::int64_t(Body.size()) || Inventory.Exports.size() > 100000)
        return Reject(Error, 0, "static-mesh-facts-input-context");
    if (Cancel && Cancel()) return Reject(Error, 0, "static-mesh-cancelled");
    FCookedStaticMeshFacts Pending; Pending.Package = CanonicalPackage;
    for (std::size_t Index = 0; Index < Inventory.Exports.size(); ++Index)
    {
        const auto& Export = Inventory.Exports[Index]; std::string Object, Class;
        if (!Inventory.Reference(std::int32_t(Index + 1), Object) || !Inventory.Reference(Export.Class, Class))
            return Reject(Error, Export.Offset, "static-mesh-facts-export-reference");
        if (CanonicalPackage + "." + Object != ExportObject) continue;
        if (Pending.ExportIndex || Export.Outer || Class != "/Script/Engine.StaticMesh")
            return Reject(Error, Export.Offset, "static-mesh-facts-export-identity");
        Pending.ExportIndex = std::int32_t(Index + 1); Pending.ExportObject = ExportObject; Pending.MeshExport = Export;
    }
    if (!Pending.ExportIndex) return Reject(Error, 0, "static-mesh-facts-export-missing");
    const auto& Mesh = Pending.MeshExport;
    if (Mesh.Offset > Body.size() || Mesh.Size > Body.size() - Mesh.Offset)
        return Reject(Error, Mesh.Offset, "static-mesh-facts-export-range");
    FPropertyDecoder Decoder(Inventory, Body); std::size_t Position = Mesh.Offset;
    Pending.MeshPropertyTags = ReadPropertyEnvelopes(Body, Inventory.Names, Mesh.Offset, Mesh.Offset + Mesh.Size);
    if (!Pending.MeshPropertyTags.Error.Code.empty()) { Error = Pending.MeshPropertyTags.Error; return false; }
    if (!Decoder.TypedProperties(Position, Mesh.Offset + Mesh.Size, Pending.MeshProperties, 0, "StaticMesh"))
    { Error = Decoder.Error; return false; }
    if (!ReadCookedStaticMeshRender(Body, Position, Mesh.Offset + Mesh.Size, Inventory.Names,
        EStaticMeshRenderContract::RetocUE426, Context, ReadAt, Pending.Render, Error, Limits, Cancel)) return false;
    if (!Materials(Inventory, Body, Pending.MeshPropertyTags, Pending.MeshProperties, Pending.Render, Pending.Materials))
        return Reject(Error, Mesh.Offset, "static-mesh-facts-material-disagreement");
    Pending.BodyIndex = Pending.Render.BodySetupIndex;
    if (Pending.BodyIndex < 0 || std::size_t(Pending.BodyIndex) > Inventory.Exports.size())
        return Reject(Error, Position, "static-mesh-facts-body-reference");
    if (Pending.BodyIndex)
    {
        const auto& Setup = Inventory.Exports[std::size_t(Pending.BodyIndex - 1)]; std::string Class, Object;
        if (Setup.Outer != Pending.ExportIndex || !Inventory.Reference(Setup.Class, Class) ||
            Class != "/Script/Engine.BodySetup" || !Inventory.Reference(Pending.BodyIndex, Object))
            return Reject(Error, Setup.Offset, "static-mesh-facts-body-identity");
        if (Setup.Offset > Body.size() || Setup.Size > Body.size() - Setup.Offset)
            return Reject(Error, Setup.Offset, "static-mesh-facts-body-range");
        Pending.BodyObject = CanonicalPackage + "." + Object; Pending.BodyExport = Setup;
        Pending.BodyPropertyTags = ReadPropertyEnvelopes(Body, Inventory.Names, Setup.Offset, Setup.Offset + Setup.Size);
        if (!Pending.BodyPropertyTags.Error.Code.empty()) { Error = Pending.BodyPropertyTags.Error; return false; }
        FPropertyDecoder BodyDecoder(Inventory, Body); std::size_t Tail = Setup.Offset;
        if (!BodyDecoder.TypedProperties(Tail, Setup.Offset + Setup.Size, Pending.BodyProperties, 0, "BodySetup"))
        { Error = BodyDecoder.Error; return false; }
        Pending.BodyNativeOffset = Tail;
        Pending.BodyNativeTail.assign(Body.begin() + Tail, Body.begin() + Setup.Offset + Setup.Size);
    }
    if (Cancel && Cancel()) return Reject(Error, Position, "static-mesh-cancelled");
    Out = std::move(Pending); return true;
}
}

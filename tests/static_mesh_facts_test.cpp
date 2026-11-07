#include "core/static_mesh_facts.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <fstream>
static std::size_t Checks = 0;
static void CheckImpl(bool Value, int Line, const char* Expression)
{
    ++Checks;
    if (!Value) { std::cerr << "static_mesh_facts_test.cpp:" << Line << ": " << Expression << '\n'; std::exit(1); }
}
#define Check(Value) CheckImpl((Value), __LINE__, #Value)
#include "package_reader_test.h"
#include "property_decoder_test.h"
#include "static_mesh_render_test.h"

struct FFactsFixture : FValueFixture
{
    std::size_t RenderStart = 0, BodyTail = 0, RenderMaterialName = 0;
    FFactsFixture()
    {
        Package.Package = "None"; Package.HeaderSize = 128;
        Package.Names = {"None", "SlotA", "SlotB"};
        Package.Imports = {{"", "", "/Script/Engine", 0}, {"", "", "StaticMesh", -1},
            {"", "", "BodySetup", -1}, {"", "", "Unused", -1},
            {"", "", "Material", -6}, {"", "", "/Game/Material", 0},
            {"", "", "NavCollision", -1}, {"", "", "StaticMeshSocket", -1}};
        std::vector<std::uint8_t> Materials;
        for (unsigned Index = 0; Index < 2; ++Index)
        {
            Tag(Materials, "ObjectProperty", Integer(std::uint32_t(-5), 4), {}, "MaterialInterface");
            std::vector<std::uint8_t> Slot; Name(Slot, Index ? "SlotB" : "SlotA"); P4Patch(Slot, 4, Index);
            Tag(Materials, "NameProperty", Slot, {}, "MaterialSlotName");
            Tag(Materials, "NameProperty", Slot, {}, "ImportedMaterialSlotName");
            std::vector<std::uint8_t> UV;
            Tag(UV, "BoolProperty", {}, {}, "bInitialized", true);
            Tag(UV, "BoolProperty", {}, {}, "bOverrideDensities", false);
            for (unsigned Channel = 0; Channel < 4; ++Channel)
            {
                const auto Start = UV.size(); std::vector<std::uint8_t> Float; P4MeshFloat(Float, float(Channel + 1));
                Tag(UV, "FloatProperty", Float, {}, "LocalUVDensities"); P4Patch(UV, Start + 20, Channel);
            }
            Name(UV, "None"); Tag(Materials, "StructProperty", UV, {"MeshUVChannelInfo"}, "UVChannelData");
            Name(Materials, "None");
        }
        std::vector<std::uint8_t> Array; P4Put(Array, 2);
        Tag(Array, "StructProperty", Materials, {"StaticMaterial"}, "StaticMaterials");
        Tag(Body, "ArrayProperty", Array, {"StructProperty"}, "StaticMaterials"); Name(Body, "None");
        RenderStart = Body.size(); const auto Render = P4StaticMeshFixture();
        RenderMaterialName = RenderStart + Render.MaterialName;
        Body.insert(Body.end(), Render.Bytes.begin(), Render.Bytes.end());
        Package.Exports.resize(4);
        auto& Mesh = Package.Exports[0]; Mesh.Name = "Mesh"; Mesh.Class = -2; Mesh.IsAsset = 1; Mesh.Size = Body.size();
        auto& Setup = Package.Exports[1]; Setup.Name = "BodySetup"; Setup.Class = -3; Setup.Outer = 1; Setup.Offset = Body.size();
        Tag(Body, "BoolProperty", {}, {}, "bGenerateMirroredCollision", true); Name(Body, "None");
        BodyTail = Body.size(); Body.insert(Body.end(), {1, 2, 3, 4}); Setup.Size = Body.size() - Setup.Offset;
        Package.Exports[2].Name = "NavCollision"; Package.Exports[2].Class = -7; Package.Exports[2].Outer = 1;
        Package.Exports[3].Name = "Socket"; Package.Exports[3].Class = -8; Package.Exports[3].Outer = 1;
    }
    bool Read(P4MapKit::FCookedStaticMeshFacts& Out, P4MapKit::FReadError& Error,
        const std::string& Owner = "/Game/Creator/Mesh", const std::function<bool()>& Cancel = {}) const
    {
        P4MapKit::FByteBulkContext Context; Context.HeaderSize = Package.HeaderSize;
        Context.FileSizes[0] = std::int64_t(Body.size());
        return P4MapKit::ReadCookedStaticMeshFacts(Package, Body, Owner, Owner + ".Mesh", Context, {}, Out, Error, {}, Cancel);
    }
};
static bool FileBytes(const std::string& Path, std::vector<std::uint8_t>& Bytes, std::size_t Limit)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    if (!Input) return false;
    const auto Size = Input.tellg();
    if (Size < 0 || std::uint64_t(Size) > Limit) return false;
    Bytes.resize(std::size_t(Size)); Input.seekg(0);
    return Bytes.empty() || bool(Input.read(reinterpret_cast<char*>(Bytes.data()), std::streamsize(Bytes.size())));
}
int main(int Argc, char** Argv)
{
    using namespace P4MapKit;
    if (Argc == 4)
    {
        const std::string HeaderFile = Argv[1]; const auto Dot = HeaderFile.rfind('.');
        if (Dot == std::string::npos) return 2;
        const auto Stem = HeaderFile.substr(0, Dot);
        std::vector<std::uint8_t> Header, Body; std::array<std::vector<std::uint8_t>, 3> Bulk;
        if (!FileBytes(HeaderFile, Header, 64u * 1024u * 1024u) ||
            !FileBytes(Stem + ".uexp", Body, 64u * 1024u * 1024u)) return 2;
        const auto Inventory = ReadPackage(Header, Body); FByteBulkContext Context;
        Context.HeaderSize = Inventory.HeaderSize; Context.FileSizes[0] = std::int64_t(Body.size());
        const std::array<const char*, 3> Suffix{{".ubulk", ".uptnl", ".m.ubulk"}};
        for (std::size_t Index = 0; Index < Bulk.size(); ++Index)
            if (FileBytes(Stem + Suffix[Index], Bulk[Index], 256u * 1024u * 1024u))
                Context.FileSizes[Index + 1] = std::int64_t(Bulk[Index].size());
        const auto ReadAt = [&](EBulkSource Source, std::uint64_t Offset, std::uint8_t* To, std::size_t Count)
        {
            const int Index = Source == EBulkSource::Regular ? 0 : Source == EBulkSource::Optional ? 1 : Source == EBulkSource::Mapped ? 2 : -1;
            if (Index < 0 || Context.FileSizes[std::size_t(Index + 1)] < 0) return false;
            const auto& Bytes = Bulk[std::size_t(Index)];
            if (Offset > Bytes.size() || Count > Bytes.size() - std::size_t(Offset)) return false;
            if (Count) std::memcpy(To, Bytes.data() + std::size_t(Offset), Count);
            return true;
        };
        FCookedStaticMeshFacts Facts; FReadError Error;
        const bool Valid = ReadCookedStaticMeshFacts(Inventory, Body, Argv[2], Argv[3], Context, ReadAt, Facts, Error);
        std::cout << "{\"valid\":" << (Valid ? "true" : "false") << ",\"error\":\"" << Error.Code <<
            "\",\"offset\":" << Error.Offset << ",\"lods\":" << Facts.Render.LODs.size() <<
            ",\"materials\":" << Facts.Materials.size() << ",\"body_tail_bytes\":" << Facts.BodyNativeTail.size() << "}\n";
        return Valid ? 0 : 1;
    }
    if (Argc != 1) return 2;
    // Independent encoders exercise the underlying readers as well as their
    // fact composition; no Unreal, creator/source authenticity or PhysX claim.
    CheckPackageReaders(); CheckPropertyReaders(); CheckValueDecoder(); CheckStaticMeshRenderReaders();
    FFactsFixture Fixture; FCookedStaticMeshFacts Facts; FReadError Error;
    Check(Fixture.Read(Facts, Error)); Check(Error.Code.empty());
    Check(Facts.Package == "/Game/Creator/Mesh" && Facts.ExportObject == "/Game/Creator/Mesh.Mesh");
    Check(Facts.BodyObject == "/Game/Creator/Mesh.Mesh.BodySetup" && Facts.BodyIndex == 2);
    Check(Facts.BodyNativeOffset == Fixture.BodyTail && Facts.BodyNativeTail == std::vector<std::uint8_t>({1, 2, 3, 4}));
    Check(Facts.BodyProperties.Field("bGenerateMirroredCollision")->Kind == FDecodedValue::EKind::Boolean);
    Check(Facts.Materials.size() == 2 && Facts.Materials[1].MaterialSlotName.NameBase == "SlotB");
    Check(Facts.Materials[1].MaterialSlotName.NameNumber == 1 && Facts.Materials[1].MaterialSlotName.Text == "SlotB_0");
    Check(Facts.Materials[1].NativeMaterialSlotName.NameBase == "SlotB" && Facts.Materials[1].NativeMaterialSlotName.NameNumber == 1);
    Check(Facts.Materials[0].MaterialInterface.Text == "/Game/Material.Material");
    Check(Facts.Render.LODs[0].Buffers.PositionValues[2][1] == 1.f);
    Check(!Facts.Render.EditableAssetQualified && !Facts.Render.DependenciesQualified && !Facts.Render.OriginalDefaultsQualified);
    const auto Refuse = [&](const FFactsFixture& Bad, const std::string& Owner = "/Game/Creator/Mesh")
    {
        FCookedStaticMeshFacts Retained; Retained.ExportObject = "prior"; Retained.BodyNativeTail = {99};
        FReadError Why; Check(!Bad.Read(Retained, Why, Owner)); Check(!Why.Code.empty());
        Check(Retained.ExportObject == "prior" && Retained.BodyNativeTail == std::vector<std::uint8_t>{99});
    };
    auto Bad = Fixture; Bad.Package.Package = "/Game/Other/Mesh"; Refuse(Bad);
    for (const std::string Owner : {"/Game/../Mesh", "/Game//Mesh", "/Game/Mesh.Asset", "Game/Mesh", "/Game/Mesh/"}) Refuse(Fixture, Owner);
    Bad = Fixture; Bad.Package.Exports[1].Outer = 0; Refuse(Bad);
    Bad = Fixture; Bad.Package.Exports[1].Class = -2; Refuse(Bad);
    Bad = Fixture; Bad.Package.Exports[0].Size = Fixture.RenderStart + 4; Refuse(Bad);
    Bad = Fixture; Bad.Package.Exports[1].Size = std::numeric_limits<std::size_t>::max(); Refuse(Bad);
    Bad = Fixture; P4Patch(Bad.Body, Bad.RenderMaterialName, 0); Refuse(Bad);
    // Literal and numbered names can print identically. Their native archive
    // base/number identities must still agree with the tagged material name.
    Bad = Fixture; Bad.Package.Names.push_back("SlotB_0");
    P4Patch(Bad.Body, Bad.RenderMaterialName + 36, Bad.Package.Names.size() - 1);
    P4Patch(Bad.Body, Bad.RenderMaterialName + 40, 0); Refuse(Bad);
    const auto* MaterialTag = StaticMeshFactsDetail::Tag(Facts.MeshPropertyTags, "StaticMaterials"); Check(MaterialTag);
    FByteReader MaterialDescriptor(Fixture.Body, MaterialTag->PayloadOffset, MaterialTag->PayloadOffset + MaterialTag->Size);
    std::size_t MaterialCount = 0; FPropertyEnvelope Inner;
    Check(MaterialDescriptor.Count(MaterialCount)); Check(ReadPropertyEnvelope(MaterialDescriptor, Fixture.Package.Names, Fixture.Body.size(), Inner));
    const auto ElementTags = ReadPropertyEnvelopes(Fixture.Body, Fixture.Package.Names, Inner.PayloadOffset, Inner.PayloadOffset + Inner.Size);
    const auto* UVTag = StaticMeshFactsDetail::Tag(ElementTags, "UVChannelData"); Check(UVTag);
    const auto UVTags = ReadPropertyEnvelopes(Fixture.Body, Fixture.Package.Names, UVTag->PayloadOffset, UVTag->PayloadOffset + UVTag->Size);
    const auto* DensityTag = StaticMeshFactsDetail::Tag(UVTags, "LocalUVDensities"); Check(DensityTag);
    Bad = Fixture; P4Patch(Bad.Body, DensityTag->PayloadOffset, 0x80000000u);
    P4Patch(Bad.Body, Bad.RenderMaterialName + 16, 0x80000000u);
    FCookedStaticMeshFacts NegativeZero; Check(Bad.Read(NegativeZero, Error)); Check(std::signbit(NegativeZero.Materials[0].UVDensities[0]));
    P4Patch(Bad.Body, DensityTag->PayloadOffset, 0); Refuse(Bad);
    Bad = Fixture; Bad.Body[Fixture.BodyTail - 1] = 0xff; Refuse(Bad);
    for (std::size_t Size = 0; Size < Fixture.BodyTail; ++Size)
    { Bad = Fixture; Bad.Body.resize(Size); Refuse(Bad); }
    FCookedStaticMeshFacts Retained; Retained.Package = "prior";
    Check(!Fixture.Read(Retained, Error, "/Game/Creator/Mesh", []() { return true; })); Check(Retained.Package == "prior");
    int Calls = 0;
    Check(!Fixture.Read(Retained, Error, "/Game/Creator/Mesh", [&]() { return ++Calls > 2; })); Check(Retained.Package == "prior");
    std::cout << Checks << " native mesh fact checks passed\n";
}

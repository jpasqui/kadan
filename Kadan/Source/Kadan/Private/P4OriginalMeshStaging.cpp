#include "P4OriginalMeshStaging.h"
#include "P4GamePaths.h"
#include "P4StaticMeshSockets.h"
#include "P4AuthoringRows.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/property_decoder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PackedNormal.h"
#include "Containers/StringConv.h"
#include <sstream>
#include <locale>

namespace
{
bool OriginalMeshReject(FString& Error, const TCHAR* Reason)
{
    Error = Reason;
    return false;
}
bool OriginalMeshContinue(const TFunction<bool()>& Cancel, FString& Error)
{
    return !Cancel || !Cancel() || OriginalMeshReject(Error, TEXT("original-mesh-cancelled"));
}
bool OriginalMeshText(const std::string& Source, FString& Out)
{
    if (Source.size() > 65536 || Source.find('\0') != std::string::npos)
        return false;
    FUTF8ToTCHAR Decode(Source.data(), int32(Source.size()));
    Out = FString(Decode.Length(), Decode.Get());
    FTCHARToUTF8 Encode(*Out, Out.Len());
    return Encode.Length() == int32(Source.size()) &&
        (!Encode.Length() || FMemory::Memcmp(Encode.Get(), Source.data(), Source.size()) == 0);
}
bool OriginalMeshCoreError(FString& Error, const P4MapKit::FReadError& Native)
{
    FString Code;
    if (!OriginalMeshText(Native.Code, Code))
        return OriginalMeshReject(Error, TEXT("original-mesh-error-text-invalid"));
    Error = Code + FString::Printf(TEXT("@%llu"), uint64(Native.Offset));
    return false;
}
// One of the mesh's files, read whole. A file that is not there, or is larger than Limit, is refused.
bool OriginalMeshLoad(
    const FString& Path, int64 Limit, std::vector<std::uint8_t>& Bytes, FString& Error, const TFunction<bool()>& Cancel)
{
    TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
    const int64 Size = Handle ? Handle->Size() : -1;
    if (Size < 0)
        return OriginalMeshReject(Error, TEXT("original-mesh-source-unreadable"));
    if (Size > Limit)
        return OriginalMeshReject(Error, TEXT("original-mesh-source-over-budget"));
    Bytes.resize(size_t(Size));
    for (int64 At = 0; At < Size;)
    {
        const int64 Count = FMath::Min<int64>(1024 * 1024, Size - At);
        if (!OriginalMeshContinue(Cancel, Error))
            return false;
        if (!Handle->Read(Bytes.data() + size_t(At), Count))
            return OriginalMeshReject(Error, TEXT("original-mesh-source-unreadable"));
        At += Count;
    }
    return Handle->Size() == Size || OriginalMeshReject(Error, TEXT("original-mesh-source-size-changed"));
}
bool OriginalMeshName(const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body,
    const P4MapKit::FPropertyEnvelope& Tag, const P4MapKit::FDecodedValue& Decoded, FP4OriginalMeshName& Out,
    FString& Error)
{
    if (Tag.Type != "NameProperty" || Tag.ArrayIndex || Tag.Size != 8 || Tag.HasGuid ||
        Decoded.Kind != P4MapKit::FDecodedValue::EKind::String)
        return OriginalMeshReject(Error, TEXT("original-mesh-name-field-schema"));
    P4MapKit::FByteReader R(Body, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
    if (!R.I32(Out.SourceIndex) || !R.I32(Out.SourceNumber))
        return OriginalMeshCoreError(Error, R.Error);
    if (Out.SourceIndex < 0 || size_t(Out.SourceIndex) >= Package.Names.size() || Out.SourceNumber < 0 ||
        !OriginalMeshText(Package.Names[size_t(Out.SourceIndex)], Out.SourceBase) ||
        !OriginalMeshText(Decoded.Text, Out.Display) || Out.SourceBase.Len() >= NAME_SIZE ||
        Out.Display.Len() >= NAME_SIZE)
        return OriginalMeshReject(Error, TEXT("original-mesh-name-source-identity-invalid"));
    const FName Exact(*Out.SourceBase, Out.SourceNumber, FNAME_Add, false);
    const FName Staging(*Out.Display);
    if (Exact.GetPlainNameString() != Out.SourceBase || Exact.GetNumber() != Out.SourceNumber ||
        Exact.ToString() != Out.Display || Staging.GetPlainNameString() != Out.SourceBase ||
        Staging.GetNumber() != Out.SourceNumber || Staging.ToString() != Out.Display)
        return OriginalMeshReject(Error, TEXT("original-mesh-name-native-representation-loss"));
    return true;
}
const P4MapKit::FPropertyEnvelope* OriginalMeshField(
    const P4MapKit::FPropertyInventory& Props, const char* Name, size_t Index = 0)
{
    const P4MapKit::FPropertyEnvelope* Found = nullptr;
    for (const auto& T : Props.Properties)
        if (T.Name == Name && T.ArrayIndex == Index)
        {
            if (Found)
                return nullptr;
            Found = &T;
        }
    return Found;
}
bool OriginalMeshKnownNativeStruct(const P4MapKit::FPropertyEnvelope& Tag)
{
    for (uint32 Word : Tag.StructGuid)
        if (Word)
            return false;
    return !Tag.HasGuid;
}
bool OriginalMeshUV(const std::vector<std::uint8_t>& Body, const P4MapKit::FPackageInventory& Package,
    const P4MapKit::FPropertyEnvelope& Tag, const P4MapKit::FDecodedValue& Decoded,
    const P4MapKit::FStaticMeshMaterial& Native, FP4OriginalMeshMaterial& Out, FString& Error)
{
    using Kind = P4MapKit::FDecodedValue::EKind;
    if (Tag.Type != "StructProperty" || Tag.Detail != std::vector<std::string>{"MeshUVChannelInfo"} || Tag.ArrayIndex ||
        !OriginalMeshKnownNativeStruct(Tag) || Decoded.Kind != Kind::Object || Decoded.Fields.size() != 6)
        return OriginalMeshReject(Error, TEXT("original-mesh-uv-struct-schema"));
    const auto Tags = P4MapKit::ReadPropertyEnvelopes(
        Body, Package.Names, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
    if (!Tags.Error.Code.empty())
        return OriginalMeshCoreError(Error, Tags.Error);
    if (Tags.End != Tag.PayloadOffset + Tag.Size || Tags.Properties.size() != 6)
        return OriginalMeshReject(Error, TEXT("original-mesh-uv-member-extent"));
    for (const char* Name : {"bInitialized", "bOverrideDensities"})
    {
        const auto* T = OriginalMeshField(Tags, Name);
        const auto* Value = Decoded.Field(Name);
        if (!T || !Value || T->Type != "BoolProperty" || T->Size || T->HasGuid || Value->Kind != Kind::Boolean ||
            Value->Text != (T->BoolValue ? "true" : "false"))
            return OriginalMeshReject(Error, TEXT("original-mesh-uv-bool-source-disagreement"));
        if (std::string(Name) == "bInitialized")
            Out.UVInitialized = T->BoolValue;
        else
            Out.UVOverride = T->BoolValue;
    }
    for (size_t Index = 0; Index < 4; ++Index)
    {
        const auto* T = OriginalMeshField(Tags, "LocalUVDensities", Index);
        const auto* Value = Decoded.Field("LocalUVDensities" + (Index ? "[" + std::to_string(Index) + "]" : ""));
        if (!T || !Value || T->Type != "FloatProperty" || T->Size != 4 || T->HasGuid || Value->Kind != Kind::Number)
            return OriginalMeshReject(Error, TEXT("original-mesh-uv-density-schema"));
        P4MapKit::FByteReader R(Body, T->PayloadOffset, T->PayloadOffset + 4);
        std::uint32_t Bits = 0;
        if (!R.U32(Bits))
            return OriginalMeshCoreError(Error, R.Error);
        FMemory::Memcpy(&Out.UVDensities[Index], &Bits, 4);
        std::istringstream Token(Value->Text);
        Token.imbue(std::locale::classic());
        double Number = 0;
        Token >> Number;
        const float Single = float(Number);
        if (!Token || Token.peek() != std::char_traits<char>::eof() || !FMath::IsFinite(Single) ||
            FMemory::Memcmp(&Single, &Out.UVDensities[Index], 4) ||
            FMemory::Memcmp(&Native.UVDensities[Index], &Single, 4))
            return OriginalMeshReject(Error, TEXT("original-mesh-uv-density-bit-disagreement"));
    }
    return (Out.UVInitialized == Native.UVInitialized && Out.UVOverride == Native.UVOverride) ||
        OriginalMeshReject(Error, TEXT("original-mesh-uv-native-bool-disagreement"));
}
bool OriginalMeshMaterials(const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body,
    const P4MapKit::FPropertyInventory& Props, FP4OriginalMeshSource& Out, FString& Error,
    const TFunction<bool()>& Cancel)
{
    size_t MatchingTags = 0;
    for (const auto& T : Props.Properties)
    {
        FString Name;
        if (!OriginalMeshText(T.Name, Name))
            return OriginalMeshReject(Error, TEXT("original-mesh-property-name-invalid"));
        if (Name.Equals(TEXT("StaticMaterials"), ESearchCase::IgnoreCase))
        {
            if (++MatchingTags != 1 || T.Name != "StaticMaterials" || T.ArrayIndex)
                return OriginalMeshReject(Error, TEXT("original-mesh-static-materials-duplicate-or-name-identity"));
        }
    }
    const auto* Tag = OriginalMeshField(Props, "StaticMaterials");
    if (!Tag || Tag->Type != "ArrayProperty" || Tag->Detail != std::vector<std::string>{"StructProperty"} ||
        Tag->HasGuid)
        return OriginalMeshReject(Error, TEXT("original-mesh-static-materials-source-missing-or-schema"));
    std::vector<P4MapKit::FMapStructCodec> Codecs;
    P4MapKit::FPropertyDecoder Decoder(Package, Body, 1000000, 64 * 1024 * 1024, Codecs);
    P4MapKit::FDecodedValue Values;
    if (!Decoder.Property(*Tag, Values, 0, "StaticMesh"))
        return OriginalMeshCoreError(Error, Decoder.Error);
    if (!OriginalMeshContinue(Cancel, Error))
        return false;
    if (Values.Kind != P4MapKit::FDecodedValue::EKind::Array || Values.Items.size() != Out.Render.Materials.size())
        return OriginalMeshReject(Error, TEXT("original-mesh-decoded-material-count"));
    P4MapKit::FByteReader Array(Body, Tag->PayloadOffset, Tag->PayloadOffset + Tag->Size);
    size_t Count = 0;
    P4MapKit::FPropertyEnvelope Inner;
    if (!Array.Count(Count, 4096) || !P4MapKit::ReadPropertyEnvelope(Array, Package.Names, Body.size(), Inner))
        return OriginalMeshCoreError(Error, Array.Error);
    if (Inner.Name != Tag->Name || Inner.Type != "StructProperty" ||
        Inner.Detail != std::vector<std::string>{"StaticMaterial"} || Inner.ArrayIndex ||
        !OriginalMeshKnownNativeStruct(Inner) || Array.Tell() != Array.Limit() || Count != Values.Items.size())
        return OriginalMeshReject(Error, TEXT("original-mesh-material-array-descriptor"));
    size_t Position = Inner.PayloadOffset;
    const size_t End = Position + Inner.Size;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        if (!OriginalMeshContinue(Cancel, Error))
            return false;
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(Body, Package.Names, Position, End);
        if (!Tags.Error.Code.empty())
            return OriginalMeshCoreError(Error, Tags.Error);
        const auto& D = Values.Items[Index];
        const auto& N = Out.Render.Materials[Index];
        if (Tags.Properties.size() != 4 || D.Kind != P4MapKit::FDecodedValue::EKind::Object || D.Fields.size() != 4)
            return OriginalMeshReject(Error, TEXT("original-mesh-material-fields-unrepresented"));
        const auto* Obj = OriginalMeshField(Tags, "MaterialInterface");
        const auto* Slot = OriginalMeshField(Tags, "MaterialSlotName");
        const auto* Imported = OriginalMeshField(Tags, "ImportedMaterialSlotName");
        const auto* UV = OriginalMeshField(Tags, "UVChannelData");
        const auto* DV = D.Field("MaterialInterface");
        const auto* DS = D.Field("MaterialSlotName");
        const auto* DI = D.Field("ImportedMaterialSlotName");
        const auto* DU = D.Field("UVChannelData");
        if (!Obj || !Slot || !Imported || !UV || !DV || !DS || !DI || !DU || Obj->Type != "ObjectProperty" ||
            Obj->Size != 4 || Obj->HasGuid)
            return OriginalMeshReject(Error, TEXT("original-mesh-material-source-field-missing"));
        FP4OriginalMeshMaterial M;
        M.TaggedStart = Position;
        M.TaggedEnd = Tags.End;
        P4MapKit::FByteReader Ref(Body, Obj->PayloadOffset, Obj->PayloadOffset + 4);
        if (!Ref.I32(M.Slot.ObjectIndex))
            return OriginalMeshCoreError(Error, Ref.Error);
        std::string Path;
        if (M.Slot.ObjectIndex != N.ObjectIndex || !Package.Reference(M.Slot.ObjectIndex, Path) ||
            (M.Slot.ObjectIndex ? DV->Kind != P4MapKit::FDecodedValue::EKind::String || DV->Text != Path
                                : DV->Kind != P4MapKit::FDecodedValue::EKind::Null) ||
            !OriginalMeshText(Path, M.ObjectPath))
            return OriginalMeshReject(Error, TEXT("original-mesh-material-object-identity-disagreement"));
        if (!OriginalMeshName(Package, Body, *Slot, *DS, M.NativeName, Error) ||
            !OriginalMeshName(Package, Body, *Imported, *DI, M.ImportedName, Error) ||
            !OriginalMeshUV(Body, Package, *UV, *DU, N, M, Error))
            return false;
        if (DS->Text != N.Slot)
            return OriginalMeshReject(Error, TEXT("original-mesh-material-native-slot-disagreement"));
        M.Slot.NativeSlot = M.NativeName.Display;
        M.Slot.ImportedSlot = M.ImportedName.Display;
        Out.Materials.Add(MoveTemp(M));
        Position = Tags.End;
    }
    if (Position != End)
        return OriginalMeshReject(Error, TEXT("original-mesh-unread-material-elements"));
    Out.MaterialsValidated = true;
    return true;
}
} //namespace

bool P4DecodeOriginalMesh(const FString& HeaderFile, const FString& BodyFile, const FString& ExportObject,
    FP4OriginalMeshSource& Out, FString& Error, const TFunction<bool()>& Cancel)
{
    Error.Reset();
    if (!IsInGameThread())
        return OriginalMeshReject(Error, TEXT("original-mesh-editor-thread-required"));
    if (!OriginalMeshContinue(Cancel, Error))
        return false;
    FP4OriginalMeshSource Pending;
    // The mesh's files, each read whole and once: the header, the body, and the bulk files beside them.
    // Everything is decoded from these bytes, so a file that changes afterwards changes nothing here.
    std::vector<std::uint8_t> Header, Body, Bulk[3];
    bool Present[3] = {false, false, false};
    if (!OriginalMeshLoad(HeaderFile, 64ll * 1024 * 1024, Header, Error, Cancel) ||
        !OriginalMeshLoad(BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile,
            64ll * 1024 * 1024, Body, Error, Cancel))
        return false;
    P4MapKit::FByteBulkContext Context;
    Context.FileSizes[0] = std::int64_t(Body.size());
    uint64 TotalSourceBytes = uint64(Header.size()) + uint64(Body.size());
    const TCHAR* const Kinds[3] = {TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")};
    for (int32 Kind = 0; Kind < 3; ++Kind)
    {
        const FString Path = FPaths::ChangeExtension(HeaderFile, Kinds[Kind]);
        if (!IFileManager::Get().FileExists(*Path))
            continue;
        if (!OriginalMeshLoad(Path, 256ll * 1024 * 1024, Bulk[Kind], Error, Cancel))
            return false;
        TotalSourceBytes += uint64(Bulk[Kind].size());
        if (TotalSourceBytes > 512ull * 1024ull * 1024ull)
            return OriginalMeshReject(Error, TEXT("original-mesh-total-source-budget"));
        Present[Kind] = true;
        Context.FileSizes[Kind + 1] = std::int64_t(Bulk[Kind].size());
    }
    if (!P4HashSourceBuffers(Header.data(), int64(Header.size()), Body.data(), int64(Body.size()), Pending.Digest))
        return OriginalMeshReject(Error, TEXT("original-mesh-source-digest-unavailable"));
    const auto Package = P4MapKit::ReadPackage(Header, Body);
    if (!Package.Error.Code.empty())
        return OriginalMeshCoreError(Error, Package.Error);
    if (!OriginalMeshText(Package.Package, Pending.Package))
        return OriginalMeshReject(Error, TEXT("original-mesh-package-name-invalid"));
    const FString Wanted = ExportObject.IsEmpty() ? P4ObjectPathOf(Pending.Package) : ExportObject;
    const P4MapKit::FPackageExport* Chosen = nullptr;
    for (size_t Index = 0; Index < Package.Exports.size(); ++Index)
    {
        const auto& X = Package.Exports[Index];
        std::string Identity, Class;
        FString Full;
        if (!Package.Reference(int32(Index + 1), Identity) || !OriginalMeshText(Identity, Full))
            return OriginalMeshReject(Error, TEXT("original-mesh-export-identity-invalid"));
        Full = Pending.Package + TEXT(".") + Full;
        if (Full != Wanted)
            continue;
        if (Chosen || !Package.Reference(X.Class, Class) || Class != "/Script/Engine.StaticMesh")
            return OriginalMeshReject(Error, TEXT("original-mesh-export-class-or-identity"));
        Chosen = &X;
        Pending.ExportObject = Full;
    }
    if (!Chosen)
        return OriginalMeshReject(Error, TEXT("original-mesh-export-object-not-found"));
    const auto Props = P4MapKit::ReadPropertyEnvelopes(
        Body, Package.Names, Chosen->Offset, Chosen->Offset + Chosen->Size);
    if (!Props.Error.Code.empty())
        return OriginalMeshCoreError(Error, Props.Error);
    Context.HeaderSize = Package.HeaderSize;
    const auto Read = [&](P4MapKit::EBulkSource Kind, std::uint64_t Offset, std::uint8_t* To, size_t Count)
    {
        const int32 Which = Kind == P4MapKit::EBulkSource::Regular ? 0
            : Kind == P4MapKit::EBulkSource::Optional              ? 1
            : Kind == P4MapKit::EBulkSource::Mapped                ? 2
                                                                   : -1;
        if (Which < 0 || !Present[Which] || (Cancel && Cancel()))
            return false;
        const std::vector<std::uint8_t>& Bytes = Bulk[Which];
        if (Offset > Bytes.size() || Count > Bytes.size() - Offset)
            return false;
        if (Count)
            FMemory::Memcpy(To, Bytes.data() + size_t(Offset), Count);
        return true;
    };
    P4MapKit::FReadError RenderError;
    if (!P4MapKit::ReadCookedStaticMeshRender(Body, Props.End, Chosen->Offset + Chosen->Size, Package.Names,
            P4MapKit::EStaticMeshRenderContract::RetocUE426, Context, Read, Pending.Render, RenderError, {},
            [&]() { return Cancel && Cancel(); }))
        return OriginalMeshCoreError(Error, RenderError);
    Pending.RenderDecoded = true;
    if (!OriginalMeshMaterials(Package, Body, Props, Pending, Error, Cancel))
        return false;
    for (const auto& T : Props.Properties)
        if (T.Name != "StaticMaterials")
        {
            FString Name;
            if (!OriginalMeshText(T.Name, Name))
                return OriginalMeshReject(Error, TEXT("original-mesh-property-name-invalid"));
            if (T.ArrayIndex)
                Name += FString::Printf(TEXT("[%llu]"), uint64(T.ArrayIndex));
            Pending.OtherTaggedProperties.Add(Name);
        }
    {
        // What cannot be decoded here does not refuse the source: it is said, and an import refuses it.
        FString Issue;
        if (!P4InspectObjectProperties(Package, *Chosen, Props, Body, UStaticMesh::StaticClass(), TEXT("StaticMesh"),
                TSet<FString>{TEXT("StaticMaterials"), TEXT("Sockets")}, Pending.MeshProperties, Issue, {}, true))
            Pending.PropertyIssue = Issue;
        Pending.Sockets = int32(Pending.Render.SocketIndices.size());
        if (!P4ReadOriginalMeshSockets(Package, Body, int32(Chosen - Package.Exports.data()) + 1,
                Pending.Render.SocketIndices, Pending.SocketProperties, Issue))
            Pending.PropertyIssue = Issue;
        Pending.HasNavCollision = Pending.Render.NavCollisionIndex != 0;
        const std::int32_t SetupIndex = Pending.Render.BodySetupIndex;
        if (SetupIndex < 0 || size_t(SetupIndex) > Package.Exports.size())
            Pending.PropertyIssue = TEXT("BodySetup: not an export of the package");
        else if (SetupIndex > 0)
        {
            const P4MapKit::FPackageExport& Setup = Package.Exports[size_t(SetupIndex - 1)];
            std::string SetupClass;
            const auto SetupProps = P4MapKit::ReadPropertyEnvelopes(
                Body, Package.Names, Setup.Offset, Setup.Offset + Setup.Size);
            if (!Package.Reference(Setup.Class, SetupClass) || SetupClass != "/Script/Engine.BodySetup")
                Pending.PropertyIssue = TEXT("BodySetup: not a body setup");
            else if (!SetupProps.Error.Code.empty())
                Pending.PropertyIssue = TEXT("BodySetup: properties unreadable");
            else if (!P4InspectObjectProperties(Package, Setup, SetupProps, Body, UBodySetup::StaticClass(),
                         TEXT("BodySetup"), TSet<FString>(), Pending.BodyProperties, Issue))
                Pending.PropertyIssue = Issue;
            else
                Pending.HasBodySetup = true;
        }
    }
    if (!OriginalMeshContinue(Cancel, Error))
        return false;
    Out = MoveTemp(Pending);
    return true;
}

namespace
{
// Canonical little-endian readback bytes, independent of host layout/padding.
// The bounded buffer avoids one SHA callback per scalar on large meshes.
class FOriginalMeshDigestWriter
{
public:
    explicit FOriginalMeshDigestWriter(const TFunction<bool(const uint8*, int32)>& InUpdate) : Update(InUpdate) {}
    bool Byte(uint8 Value)
    {
        if (Count == int32(sizeof(Bytes)) && !Flush())
            return false;
        Bytes[Count++] = Value;
        return true;
    }
    bool Word(uint16 Value) { return Byte(uint8(Value)) && Byte(uint8(Value >> 8)); }
    bool Integer(uint32 Value) { return Word(uint16(Value)) && Word(uint16(Value >> 16)); }
    bool Single(float Value)
    {
        uint32 Bits = 0;
        FMemory::Memcpy(&Bits, &Value, 4);
        return Integer(Bits);
    }
    bool Flush()
    {
        if (Count && !Update(Bytes, Count))
            return false;
        Count = 0;
        return true;
    }

private:
    const TFunction<bool(const uint8*, int32)>& Update;
    uint8 Bytes[16384];
    int32 Count = 0;
};
struct FOriginalMeshReadbackHashes
{
    FString Positions, Indices, UV, Colors, Tangents;
};
bool OriginalMeshReadbackHashes(const FP4PrivateMeshLODStage& Stage, const P4MapKit::FStaticMeshBuffers& Original,
    FOriginalMeshReadbackHashes& Out, FString& Error)
{
    if (!Stage.Description)
        return OriginalMeshReject(Error, TEXT("original-mesh-readback-description-absent"));
    const FMeshDescription& D = *Stage.Description;
    const FStaticMeshConstAttributes A(D);
    TMap<FVertexID, uint32> SourceVertex;
    TArray<FVertexInstanceID> Instance;
    Instance.Init(FVertexInstanceID(INDEX_NONE), Stage.Vertices.Num());
    for (int32 V = 0; V < Stage.Vertices.Num(); ++V)
        SourceVertex.Add(Stage.Vertices[V], uint32(V));
    for (const FPolygonID Polygon : Stage.Polygons)
    {
        const auto Corners = D.GetPolygonVertexInstances(Polygon);
        for (const FVertexInstanceID Corner : Corners)
        {
            const uint32* V = SourceVertex.Find(D.GetVertexInstanceVertex(Corner));
            if (!V)
                return OriginalMeshReject(Error, TEXT("original-mesh-readback-vertex-not-in-source"));
            if (Instance[*V] == FVertexInstanceID(INDEX_NONE))
                Instance[*V] = Corner;
        }
    }
    for (int32 V = 0; V < Instance.Num(); ++V)
    {
        if (Instance[V] == FVertexInstanceID(INDEX_NONE))
            Instance[V] = Stage.UnusedInstances[V];
        if (!D.VertexInstances().IsValid(Instance[V]) || D.GetVertexInstanceVertex(Instance[V]) != Stage.Vertices[V])
            return OriginalMeshReject(Error, TEXT("original-mesh-readback-source-attribute-instance-absent"));
    }
    FOriginalMeshReadbackHashes Pending;
    const bool Good = P4HashByteStream(
                          [&](const TFunction<bool(const uint8*, int32)>& Update)
                          {
                              FOriginalMeshDigestWriter W(Update);
                              for (const FVertexID V : Stage.Vertices)
                              {
                                  const FVector P = A.GetVertexPositions()[V];
                                  if (!W.Single(P.X) || !W.Single(P.Y) || !W.Single(P.Z))
                                      return false;
                              }
                              return W.Flush();
                          },
                          Pending.Positions) &&
        P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Update)
            {
                FOriginalMeshDigestWriter W(Update);
                for (const FPolygonID P : Stage.Polygons)
                    for (const FVertexInstanceID I : D.GetPolygonVertexInstances(P))
                    {
                        const uint32* V = SourceVertex.Find(D.GetVertexInstanceVertex(I));
                        if (!V || !W.Integer(*V))
                            return false;
                    }
                return W.Flush();
            },
            Pending.Indices) &&
        P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Update)
            {
                FOriginalMeshDigestWriter W(Update);
                const auto UV = A.GetVertexInstanceUVs();
                for (int32 U = 0; U < UV.GetNumIndices(); ++U)
                    for (const FVertexInstanceID I : Instance)
                    {
                        const FVector2D P = UV.Get(I, U);
                        if (!W.Single(P.X) || !W.Single(P.Y))
                            return false;
                    }
                return W.Flush();
            },
            Pending.UV) &&
        P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Update)
            {
                FOriginalMeshDigestWriter W(Update);
                for (const FVertexInstanceID I : Instance)
                {
                    const FVector4 C = A.GetVertexInstanceColors()[I];
                    const FColor P = FLinearColor(C.X, C.Y, C.Z, C.W).ToFColor(true);
                    if (!W.Byte(P.R) || !W.Byte(P.G) || !W.Byte(P.B) || !W.Byte(P.A))
                        return false;
                }
                return W.Flush();
            },
            Pending.Colors) &&
        P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Update)
            {
                FOriginalMeshDigestWriter W(Update);
                for (const FVertexInstanceID I : Instance)
                {
                    const FVector X = A.GetVertexInstanceTangents()[I], Z = A.GetVertexInstanceNormals()[I];
                    const float Sign = A.GetVertexInstanceBinormalSigns()[I];
                    if (Original.HighPrecisionTangents)
                    {
                        const FPackedRGBA16N PX(X), PZ(FVector4(Z, Sign));
                        const int16 Values[] = {PX.X, PX.Y, PX.Z, PX.W, PZ.X, PZ.Y, PZ.Z, PZ.W};
                        for (int16 Value : Values)
                            if (!W.Word(uint16(Value)))
                                return false;
                    }
                    else
                    {
                        const FPackedNormal PX(X), PZ(FVector4(Z, Sign));
                        const int16 Values[] = {PX.Vector.X, PX.Vector.Y, PX.Vector.Z, PX.Vector.W, PZ.Vector.X,
                            PZ.Vector.Y, PZ.Vector.Z, PZ.Vector.W};
                        for (int16 Value : Values)
                            if (!W.Word(uint16(Value)))
                                return false;
                    }
                }
                return W.Flush();
            },
            Pending.Tangents);
    if (!Good)
        return OriginalMeshReject(Error, TEXT("original-mesh-readback-digest-failed"));
    Out = MoveTemp(Pending);
    return true;
}
FString OriginalMeshDensityBits(const std::array<float, 4>& Values)
{
    FString Result;
    for (float Value : Values)
    {
        uint32 Bits = 0;
        FMemory::Memcpy(&Bits, &Value, 4);
        for (int32 Byte = 0; Byte < 4; ++Byte)
            Result += FString::Printf(TEXT("%02x"), (Bits >> (Byte * 8)) & 255u);
    }
    return Result;
}
TArray<TSharedPtr<FJsonValue>> OriginalMeshGaps(const FP4OriginalMeshSource& Source, const FP4MeshStageReport& Report)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    const TArray<FString> Baseline = {TEXT("Original native class/defaults and tagged asset settings are unqualified"),
        TEXT("Material/texture dependency objects are unresolved; source identities and UV metadata are retained only"),
        TEXT("BodySetup/NavCollision/sockets and native object dependencies are unqualified"),
        TEXT("Source GUIDs/bounds/screen sizes/LOD errors/build history are not applied to an editable asset"),
        TEXT(
            "Section flags and auxiliary buffers/samplers/distance-field/occluder data are not represented by MeshDescription"),
        TEXT("Rebuilt render precision/topology and save/restart/cook roundtrip are unqualified")};
    for (const FString& Gap : Baseline)
        Values.Add(MakeShared<FJsonValueString>(Gap));
    for (const FString& Gap : Report.UnrepresentedFields)
        Values.Add(MakeShared<FJsonValueString>(Gap));
    for (const FString& Name : Source.OtherTaggedProperties)
        Values.Add(MakeShared<FJsonValueString>(TEXT("Unapplied original tagged property: ") + Name));
    return Values;
}
} //namespace

FString P4StageOriginalMesh(const FString& HeaderFile, const FString& BodyFile, const FString& ExportObject)
{
    FP4OriginalMeshSource Source;
    FP4PrivateMeshStage Stage;
    FP4MeshStageReport Report;
    FString Error;
    TArray<FOriginalMeshReadbackHashes> Hashes;
    bool Valid = P4DecodeOriginalMesh(HeaderFile, BodyFile, ExportObject, Source, Error);
    if (Valid)
    {
        TArray<FP4ExactMeshMaterialSlot> Slots;
        for (const auto& M : Source.Materials)
            Slots.Add(M.Slot);
        Valid = P4StagePrivateMeshDescriptions(Source.Render, Slots, Stage, Report, Error);
        if (Valid)
            Valid = P4VerifyPrivateMeshDescriptions(Source.Render, Slots, Stage, Report, Error);
        if (Valid)
            for (int32 L = 0; L < Stage.LODs.Num(); ++L)
            {
                FOriginalMeshReadbackHashes H;
                if (!OriginalMeshReadbackHashes(Stage.LODs[L], Source.Render.LODs[size_t(L)].Buffers, H, Error))
                {
                    Valid = false;
                    break;
                }
                Hashes.Add(MoveTemp(H));
            }
    }
    if (!Valid)
        Hashes.Reset();
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Valid);
    Result->SetStringField(TEXT("error"), Error);
    Result->SetStringField(TEXT("source_sha256"), Source.Digest);
    Result->SetStringField(TEXT("export_object"), Source.ExportObject.IsEmpty() ? ExportObject : Source.ExportObject);
    Result->SetStringField(TEXT("package"), Source.Package);
    Result->SetNumberField(TEXT("native_start"), double(Source.Render.Start));
    Result->SetNumberField(TEXT("native_end"), double(Source.Render.End));
    Result->SetBoolField(TEXT("materials_validated"), Source.MaterialsValidated);
    Result->SetBoolField(TEXT("render_payload_decoded"), Source.RenderDecoded);
    Result->SetBoolField(TEXT("geometry_attributes_verified"), Valid && Report.GeometryAttributesVerified);
    Result->SetBoolField(TEXT("private_staging_qualified"), Valid && Report.GeometryAttributesVerified);
    Result->SetBoolField(TEXT("editable_import_qualified"), false);
    Result->SetBoolField(TEXT("editable_asset_qualified"), false);
    Result->SetBoolField(TEXT("original_defaults_qualified"), false);
    Result->SetBoolField(TEXT("dependencies_qualified"), false);
    Result->SetBoolField(TEXT("full_asset_qualified"), false);
    Result->SetArrayField(TEXT("unrepresented_fields"), OriginalMeshGaps(Source, Report));
    TArray<TSharedPtr<FJsonValue>> Materials;
    for (int32 Index = 0; Index < Source.Materials.Num(); ++Index)
    {
        const auto& M = Source.Materials[Index];
        auto J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("ordinal"), Index);
        J->SetNumberField(TEXT("native_object_index"), M.Slot.ObjectIndex);
        J->SetStringField(TEXT("native_object_path"), M.ObjectPath);
        J->SetStringField(TEXT("native_slot_name"), M.NativeName.Display);
        J->SetStringField(TEXT("imported_slot_name"), M.ImportedName.Display);
        J->SetNumberField(TEXT("slot_name_index"), M.NativeName.SourceIndex);
        J->SetNumberField(TEXT("slot_name_number"), M.NativeName.SourceNumber);
        J->SetNumberField(TEXT("imported_name_index"), M.ImportedName.SourceIndex);
        J->SetNumberField(TEXT("imported_name_number"), M.ImportedName.SourceNumber);
        J->SetStringField(TEXT("slot_name_base"), M.NativeName.SourceBase);
        J->SetStringField(TEXT("imported_name_base"), M.ImportedName.SourceBase);
        J->SetBoolField(TEXT("uv_initialized"), M.UVInitialized);
        J->SetBoolField(TEXT("uv_override"), M.UVOverride);
        J->SetStringField(TEXT("uv_density_float32_bits"), OriginalMeshDensityBits(M.UVDensities));
        J->SetNumberField(TEXT("tagged_start"), double(M.TaggedStart));
        J->SetNumberField(TEXT("tagged_end"), double(M.TaggedEnd));
        Materials.Add(MakeShared<FJsonValueObject>(J));
    }
    Result->SetArrayField(TEXT("materials"), Materials);
    TArray<TSharedPtr<FJsonValue>> LODs;
    for (size_t Index = 0; Index < Source.Render.LODs.size(); ++Index)
    {
        const auto& L = Source.Render.LODs[Index];
        const auto& B = L.Buffers;
        auto J = MakeShared<FJsonObject>();
        TSet<uint32> Used;
        for (uint32 V : B.Main.Indices)
            Used.Add(V);
        J->SetNumberField(TEXT("lod_index"), double(Index));
        J->SetNumberField(TEXT("vertices"), B.Vertices);
        J->SetNumberField(TEXT("corners"), double(B.Main.Indices.size()));
        J->SetNumberField(TEXT("triangles"), double(B.Main.Indices.size() / 3));
        J->SetNumberField(TEXT("sections"), double(L.Sections.size()));
        J->SetNumberField(TEXT("unused_vertices"), double(B.Vertices) - Used.Num());
        J->SetNumberField(TEXT("uv_channels"), B.TexCoords);
        J->SetBoolField(TEXT("color_present"), !B.ColorRGBA.empty());
        if (Valid && Index < size_t(Hashes.Num()))
        {
            const auto& H = Hashes[int32(Index)];
            J->SetStringField(TEXT("positions_sha256"), H.Positions);
            J->SetStringField(TEXT("indices_sha256"), H.Indices);
            J->SetStringField(TEXT("uv_sha256"), H.UV);
            J->SetStringField(TEXT("colors_sha256"), H.Colors);
            J->SetStringField(TEXT("packed_tangents_sha256"), H.Tangents);
        }
        LODs.Add(MakeShared<FJsonValueObject>(J));
    }
    Result->SetArrayField(TEXT("lods"), LODs);
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

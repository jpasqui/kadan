#include "P4KadanReimportAsset.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4StaticMeshContent.h"
#include "P4OriginalMeshImport.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "P4CreatorSource.h"
#include "P4StandInFill.h"
#include "P4StaticMeshBuild.h"
#include "P4MeshSourceReconstruction.h"
#include "P4CollisionSettings.h"
#include "P4OriginalMeshStaging.h"
#include "P4TextureProperties.h"
#include "P4NativeHash.h"
#include "P4PackageSave.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "PhysicsEngine/BodySetup.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "NavCollision.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4StaticMeshContent
{
const TCHAR* const BaselineKey = TEXT("P4NativeMeshBaseline");
const TCHAR* const BaselineOwner = TEXT("p4mapkit-original-mesh-v1");
// What the game's files are converted with writes for a reference to a package the game's containers do
// not hold. The game's own collision meshes name such a material: the game draws nothing with it.
const TCHAR* const Unshipped = TEXT("/Engine/UnknownPackage.UnknownExport");

bool WriteAll(const TFunction<bool(const uint8*, int32)>& Write, const uint8* Bytes, int64 Count)
{
    const int64 Step = 64 * 1024; // the largest piece the hash takes at once
    for (int64 At = 0; At < Count; At += Step)
        if (!Write(Bytes + At, int32(FMath::Min(Step, Count - At))))
            return false;
    return true;
}
bool WriteCount(const TFunction<bool(const uint8*, int32)>& Write, int64 Count)
{
    return Write(reinterpret_cast<const uint8*>(&Count), int32(sizeof Count));
}

// Every setting an object saves but the named ones, by name and exact value, with names and object
// references as text.
void Settings(UObject* Object, const TSet<FName>& Left, FArchive& Writer)
{
    FString Class = Object->GetClass()->GetPathName();
    Writer << Class;
    for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || Left.Contains(Field->GetFName()))
            continue;
        FString Name = Field->GetName();
        Writer << Name;
        for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
            Field->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(),
                Field->ContainerPtrToValuePtr<void>(Object, Index), nullptr);
    }
}
// The mesh's own objects (collision, navigation, sockets) are said by what they hold, not by their
// names. The lighting id is renewed by every build, the import record is the engine's.
const TSet<FName>& MeshLeft()
{
    static const TSet<FName> Names{TEXT("AssetImportData"), TEXT("AssetUserData"), TEXT("LightingGuid"),
        TEXT("BodySetup"), TEXT("NavCollision"), TEXT("Sockets"), TEXT("StaticMaterials")};
    return Names;
}
const TSet<FName>& BodyLeft()
{
    static const TSet<FName> Names{TEXT("BodySetupGuid")};
    return Names;
}

// Every corner of every triangle of one LOD's source geometry, in the description's own order.
void Geometry(const FMeshDescription& D, TArray<uint8>& Out)
{
    FMemoryWriter Writer(Out, true);
    const FStaticMeshConstAttributes A(D);
    const auto Positions = A.GetVertexPositions();
    const auto Normals = A.GetVertexInstanceNormals();
    const auto Tangents = A.GetVertexInstanceTangents();
    const auto Signs = A.GetVertexInstanceBinormalSigns();
    const auto Colors = A.GetVertexInstanceColors();
    const auto UVs = A.GetVertexInstanceUVs();
    const auto Slots = A.GetPolygonGroupMaterialSlotNames();
    int32 Channels = UVs.GetNumIndices();
    Writer << Channels;
    for (const FPolygonID Polygon : D.Polygons().GetElementIDs())
    {
        FString Slot = Slots[D.GetPolygonPolygonGroup(Polygon)].ToString();
        Writer << Slot;
        for (const FVertexInstanceID Corner : D.GetPolygonVertexInstances(Polygon))
        {
            FVector Position = Positions[D.GetVertexInstanceVertex(Corner)], Normal = Normals[Corner],
                    Tangent = Tangents[Corner];
            float Sign = Signs[Corner];
            FVector4 Color = Colors[Corner];
            Writer << Position << Normal << Tangent << Sign << Color;
            for (int32 Channel = 0; Channel < Channels; ++Channel)
            {
                FVector2D UV = UVs.Get(Corner, Channel);
                Writer << UV;
            }
        }
    }
}

struct FBaseline
{
    FString Package, Class, Source, State, Equal;
    float LargestUVGap = 0;
};
FString BaselineText(const FBaseline& Value)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("owner"), BaselineOwner);
    Json->SetStringField(TEXT("package"), Value.Package);
    Json->SetStringField(TEXT("cls"), Value.Class);
    Json->SetStringField(TEXT("source_sha256"), Value.Source);
    Json->SetStringField(TEXT("state"), Value.State);
    Json->SetStringField(TEXT("equal"), Value.Equal);
    Json->SetNumberField(TEXT("largest_uv_gap"), double(Value.LargestUVGap));
    FString Out = P4CondensedJson(Json);
    return Out;
}
bool ReadBaseline(const UStaticMesh* Mesh, FBaseline& Out)
{
    const FString Record = Mesh->GetOutermost()->GetMetaData()->GetValue(Mesh, BaselineKey);
    TSharedPtr<FJsonObject> Json;
    FString Owner;
    double Format = 0, Gap = 0;
    if (Record.IsEmpty() || !P4ParseJson(Record, Json))
        return false;
    if (!Json->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Json->TryGetStringField(TEXT("owner"), Owner) || !P4SameText(Owner, BaselineOwner) ||
        !Json->TryGetStringField(TEXT("package"), Out.Package) || !Json->TryGetStringField(TEXT("cls"), Out.Class) ||
        !Json->TryGetStringField(TEXT("source_sha256"), Out.Source) ||
        !Json->TryGetStringField(TEXT("state"), Out.State) || !Json->TryGetStringField(TEXT("equal"), Out.Equal) ||
        !Json->TryGetNumberField(TEXT("largest_uv_gap"), Gap))
        return false;
    Out.LargestUVGap = float(Gap);
    return P4IsSha256Hex(Out.Source) && P4IsSha256Hex(Out.State) &&
        P4SameText(Out.Package, Mesh->GetOutermost()->GetName()) &&
        P4SameText(Out.Class, Mesh->GetClass()->GetPathName());
}
void BaselineFields(const FBaseline& Value, FJsonObject& Json)
{
    Json.SetStringField(TEXT("source_sha256"), Value.Source);
    Json.SetStringField(TEXT("imported_state"), Value.State);
    Json.SetStringField(TEXT("equal"), Value.Equal);
    Json.SetNumberField(TEXT("largest_uv_gap"), double(Value.LargestUVGap));
}
FString Finish(const TSharedRef<FJsonObject>& Json, const FString& Error)
{
    Json->SetBoolField(TEXT("valid"), Error.IsEmpty());
    Json->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Json);
    return Out;
}

FString State(UStaticMesh* Mesh)
{
    if (!IsInGameThread() || !IsValid(Mesh) || Mesh->GetNumSourceModels() == 0)
        return FString();
    TArray<uint8> Saved;
    {
        FMemoryWriter Memory(Saved, true);
        FObjectAndNameAsStringProxyArchive Writer(Memory, false);
        Settings(Mesh, MeshLeft(), Writer);
        int32 Slots = Mesh->StaticMaterials.Num();
        Writer << Slots;
        for (FStaticMaterial& Slot : Mesh->StaticMaterials)
        {
            FString Material = Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : FString();
            FString Name = Slot.MaterialSlotName.ToString(), Imported = Slot.ImportedMaterialSlotName.ToString();
            bool Set = Slot.UVChannelData.bOverrideDensities;
            Writer << Material << Name << Imported << Set;
            // A slot's texture densities are the engine's own measurement of the geometry unless a
            // person set them.
            if (Set)
                for (float& Density : Slot.UVChannelData.LocalUVDensities)
                    Writer << Density;
        }
        bool Collides = Mesh->BodySetup != nullptr;
        Writer << Collides;
        if (Collides)
            Settings(Mesh->BodySetup, BodyLeft(), Writer);
        bool Navigated = Mesh->NavCollision != nullptr;
        Writer << Navigated;
        int32 Sockets = Mesh->Sockets.Num();
        Writer << Sockets;
        for (UStaticMeshSocket* Socket : Mesh->Sockets)
            if (Socket)
                Settings(Socket, TSet<FName>(), Writer);
        if (Writer.IsError() || Memory.IsError())
            return FString();
    }
    FString Digest;
    const bool Good = P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Write)
        {
            if (!WriteCount(Write, Saved.Num()) || !WriteAll(Write, Saved.GetData(), Saved.Num()))
                return false;
            for (int32 LOD = 0; LOD < Mesh->GetNumSourceModels(); ++LOD)
            {
                // A LOD the engine makes by reducing another has no geometry of its own.
                TArray<uint8> Bytes;
                if (const FMeshDescription* Description = Mesh->GetMeshDescription(LOD))
                    Geometry(*Description, Bytes);
                if (!WriteCount(Write, Bytes.Num()) || !WriteAll(Write, Bytes.GetData(), Bytes.Num()))
                    return false;
            }
            return true;
        },
        Digest);
    return Good ? Digest : FString();
}

FString Status(UStaticMesh* Mesh)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("original"), false);
    Json->SetBoolField(TEXT("unchanged"), false);
    if (!IsInGameThread())
        return Finish(Json, TEXT("mesh-status-requires-editor-thread"));
    if (!IsValid(Mesh))
        return Finish(Json, TEXT("mesh-status-needs-a-mesh"));
    Json->SetStringField(TEXT("asset"), Mesh->GetPathName());
    FBaseline Baseline;
    if (ReadBaseline(Mesh, Baseline))
        BaselineFields(Baseline, *Json);
    const FP4OriginalStatus Status = P4QueryOriginalAsset(Mesh);
    P4OriginalStatusFields(Status, *Json);
    return Finish(Json, Status.Error);
}

FP4OriginalMeshImportResult Import(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    FP4OriginalMeshImportResult Result;
    const auto Refuse = [&](EP4OriginalMeshImportError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Refuse(EP4OriginalMeshImportError::EditorThreadRequired, TEXT("mesh-import-requires-editor-thread"));
    FP4OriginalMeshSource Source;
    FString Error, Name;
    // The mesh of the package: the one named after it.
    if (!P4DecodeOriginalMesh(HeaderFile, BodyFile, FString(), Source, Error))
        return Refuse(EP4OriginalMeshImportError::DecodeFailed, Error);
    const FString SourceSha256 = Source.Digest;
    const FString Package = Source.Package;
    Result.Package = Package;
    Result.AssetPath = Source.ExportObject;
    // Unreal names ignore case, and the game ships meshes whose object differs from the package's name in case
    // only (SM_eft_Geyser_spiral00 holds SM_eft_Geyser_Spiral00). The mesh keeps the game's own spelling.
    if (!P4IsValidContentPackage(Package) ||
        !Source.ExportObject.Equals(P4ObjectPathOf(Package), ESearchCase::IgnoreCase) ||
        !Source.ExportObject.StartsWith(Package + TEXT("."), ESearchCase::CaseSensitive))
        return Refuse(EP4OriginalMeshImportError::InvalidGamePath, TEXT("mesh-import-game-path-unusable"));
    Name = Source.ExportObject.Mid(Package.Len() + 1);
    // Nothing the game's file holds may be dropped: what is not brought over yet refuses the mesh.
    if (Source.SocketProperties.Num() != Source.Sockets)
        return Refuse(EP4OriginalMeshImportError::SocketsUnsupported,
            TEXT("mesh-import-sockets-unqualified: ") + Source.PropertyIssue);
    if (!Source.PropertyIssue.IsEmpty())
        return Refuse(EP4OriginalMeshImportError::PropertiesUndecoded,
            TEXT("mesh-import-properties-undecoded: ") + Source.PropertyIssue);
    if (!Source.MeshProperties.NativePropertiesRepresentable)
        return Refuse(EP4OriginalMeshImportError::PropertyUnrepresentable,
            TEXT("mesh-import-property-not-representable: ") +
                (Source.MeshProperties.NativePropertyIssues.Num() ? Source.MeshProperties.NativePropertyIssues[0]
                                                                  : FString()));
    if (Source.HasBodySetup && !Source.BodyProperties.NativePropertiesRepresentable)
        return Refuse(EP4OriginalMeshImportError::CollisionUnrepresentable,
            TEXT("mesh-import-collision-property-not-representable: ") +
                (Source.BodyProperties.NativePropertyIssues.Num() ? Source.BodyProperties.NativePropertyIssues[0]
                                                                  : FString()));
    int32 EmptySlots = 0;
    for (const FP4OriginalMeshMaterial& Material : Source.Materials)
    {
        if (P4SameText(Material.ObjectPath, Unshipped))
        {
            ++EmptySlots;
            continue;
        }
        if (!Material.ObjectPath.IsEmpty() &&
            !StaticLoadObject(
                UMaterialInterface::StaticClass(), nullptr, *Material.ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
            return Refuse(EP4OriginalMeshImportError::MissingMaterial,
                TEXT("mesh-import-material-not-in-the-project: ") + Material.ObjectPath);
    }
    // Slots that name a material the game does not ship stay empty, as they are in the game.
    Result.MaterialsChecked = true;
    Result.UnshippedMaterialSlots = EmptySlots;

    const bool OnDisk = !P4KadanOwnsReimportSlot(Package) && FPackageName::DoesPackageExist(Package);
    UObject* Found = FindObject<UObject>(nullptr, *Source.ExportObject);
    if (Found && !IsValid(Found))
        Found = nullptr;
    if (!Found && OnDisk)
        Found = StaticLoadObject(
            UObject::StaticClass(), nullptr, *Source.ExportObject, nullptr, LOAD_NoWarn | LOAD_Quiet);
    UStaticMesh* Mesh = Cast<UStaticMesh>(Found);
    UStaticMesh* Placeholder = nullptr;
    const bool StandIn = Found &&
        FString(Found->GetOutermost()->GetMetaData()->GetValue(Found, TEXT("P4BlueprintStandIn"))) == TEXT("1");
    TUniquePtr<FP4StandInFill> FillGuard;
    if (StandIn)
    {
        if (!Mesh || Mesh->GetClass() != UStaticMesh::StaticClass())
            return Refuse(
                EP4OriginalMeshImportError::PlaceholderClassMismatch, TEXT("mesh-import-placeholder-class-differs"));
        if (!P4IsVerifiedStandIn(Mesh))
            return Refuse(
                EP4OriginalMeshImportError::EditedPlaceholder, TEXT("mesh-import-would-replace-an-edited-placeholder"));
        if (!SaveToDisk)
            return Refuse(EP4OriginalMeshImportError::SaveRequired, TEXT("mesh-import-placeholder-fill-requires-save"));
        Placeholder = Mesh;
    }
    FBaseline Baseline;
    if (Found && !StandIn)
    {
        if (!Mesh || !ReadBaseline(Mesh, Baseline))
            return Refuse(
                EP4OriginalMeshImportError::OccupiedDestination, TEXT("mesh-import-destination-holds-another-asset"));
        const FP4OriginalStatus Status = P4QueryOriginalAsset(Mesh);
        FString MigrationError;
        const bool Untouched = Status.IsUnchanged() ||
            ((Status.State == EP4OriginalState::NeedsMigration || Status.State == EP4OriginalState::Recorded) &&
                P4MigrateOriginalRecord(
                    Mesh, [&]() { return P4SameText(State(Mesh), Baseline.State); }, MigrationError));
        if (!Untouched)
            return Refuse(Status.State == EP4OriginalState::NeedsMigration
                    ? EP4OriginalMeshImportError::RecordValidationRequired
                    : EP4OriginalMeshImportError::EditedOriginal,
                Status.State == EP4OriginalState::NeedsMigration
                    ? TEXT("mesh-import-record-needs-validation: ") + MigrationError
                    : FString(TEXT("mesh-import-would-replace-an-edited-mesh")));
        if (!P4SameText(Baseline.Source, SourceSha256))
            return Refuse(EP4OriginalMeshImportError::EarlierImportReplacementUnsupported,
                TEXT("mesh-import-replacing-an-earlier-import-is-not-done-yet"));
    }
    else
    {
        UPackage* Loaded = FindPackage(nullptr, *Package);
        TArray<UObject*> Inside;
        if (Loaded)
            GetObjectsWithOuter(Loaded, Inside, false);
        Inside.RemoveAll([](const UObject* Object) { return !IsValid(Object) || Object->IsA<UMetaData>(); });
        if (!StandIn && (OnDisk || Inside.Num() != 0))
            return Refuse(
                EP4OriginalMeshImportError::OccupiedDestination, TEXT("mesh-import-destination-holds-another-asset"));

        // Built away from Content, given the game's settings, and only then moved to its place.
        FP4MeshBuildReport Build;
        Mesh = P4BuildOriginalMesh(Source, GetTransientPackage(), NAME_None, RF_Transient, Build, Error);
        if (!Mesh)
            return Refuse(EP4OriginalMeshImportError::BuildFailed, Error);
        TStrongObjectPtr<UStaticMesh> Guard(Mesh);
        const auto Fail = [&](const FString& Why,
                              EP4OriginalMeshImportError Code = EP4OriginalMeshImportError::PropertyPreparationFailed)
        {
            if (Mesh != Placeholder)
                Mesh->MarkPendingKill();
            return Refuse(Code, Why);
        };
        // Qualified by actual source import/save/cold reload/repeated builds.
        // Every reconstructed asset must still compare EXACTLY with its own
        // source; the legacy weld/tangent allowances cannot admit this path.
        if (Build.SourceReconstructed && !Build.Equal)
            return Fail(TEXT("mesh-import-reconstructed-source-differs-from-game"),
                EP4OriginalMeshImportError::EngineBuildDiffers);
        if (!Build.EqualWithinEngineWeld && !Build.EqualWithinEngineTangents)
            return Fail(
                TEXT("mesh-import-engine-build-differs-from-game"), EP4OriginalMeshImportError::EngineBuildDiffers);
        Build.MissingMaterials.RemoveAll([](const FString& Path) { return P4SameText(Path, Unshipped); });
        if (Build.MissingMaterials.Num())
            return Fail(TEXT("mesh-import-material-not-in-the-project: ") + Build.MissingMaterials[0],
                EP4OriginalMeshImportError::MissingMaterial);
        FString Canonical;
        if (!P4PrepareObjectProperties(Mesh, Source.MeshProperties, Canonical, Error))
            return Fail(Error);
        // Original reflected settings include the stock build-state flag. Restore
        // the supported editor reconstruction state before applying body settings.
        if (Build.SourceReconstructed &&
            (!P4RecordMeshSourceReconstruction(Mesh, Source, Error) || !P4RestoreMeshSourceReconstruction(Mesh, Error)))
            return Fail(Error);
        if (Source.HasBodySetup)
        {
            // The game's collision settings on top of the class's own: what its file does not say is the
            // class default.
            UBodySetup* Scratch = NewObject<UBodySetup>(GetTransientPackage(), NAME_None, RF_Transient);
            TStrongObjectPtr<UBodySetup> ScratchGuard(Scratch);
            if (!P4PrepareObjectProperties(Scratch, Source.BodyProperties, Canonical, Error))
            {
                Scratch->MarkPendingKill();
                return Fail(Error);
            }
            Mesh->CreateBodySetup();
            for (TFieldIterator<FProperty> Field(UBodySetup::StaticClass()); Field; ++Field)
            {
                if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) ||
                    BodyLeft().Contains(Field->GetFName()))
                    continue;
                Field->CopyCompleteValue(
                    Field->ContainerPtrToValuePtr<void>(Mesh->BodySetup), Field->ContainerPtrToValuePtr<void>(Scratch));
            }
            Scratch->MarkPendingKill();
            // The profile and the channels the game's file names have to be the project's too, or the
            // engine rewrites them when the mesh loads and the next save loses them.
            const FString Lost = P4CollisionProjectIssue(Mesh->BodySetup->DefaultInstance, Mesh->BodySetup);
            if (!Lost.IsEmpty())
                return Fail(TEXT("mesh-import-") + Lost, EP4OriginalMeshImportError::CollisionProfileMismatch);
        }
        else
            Mesh->BodySetup = nullptr;
        if (Source.HasNavCollision)
            Mesh->CreateNavCollision(true);
        else
            Mesh->MarkAsNotHavingNavigationData();
        if (StandIn)
        {
            UStaticMesh* Donor = Mesh;
            const FString DonorState = State(Donor);
            if (DonorState.IsEmpty())
                return Fail(TEXT("mesh-import-state-unreadable"), EP4OriginalMeshImportError::StateUnreadable);
            FillGuard = MakeUnique<FP4StandInFill>(Placeholder);
            if (!FillGuard->IsReady())
                return Fail(
                    TEXT("mesh-import-placeholder-backup-unreadable"), EP4OriginalMeshImportError::BackupUnreadable);
            FP4MeshBuildReport Filled;
            Mesh = Placeholder;
            if (!P4BuildOriginalMesh(
                    Source, Mesh->GetOuter(), Mesh->GetFName(), Mesh->GetFlags(), Filled, Error, Mesh) ||
                (Filled.SourceReconstructed ? !Filled.Equal : !Filled.EqualWithinEngineWeld))
                return Fail(Error.IsEmpty() ? FString(TEXT("mesh-import-engine-fill-differs-from-game")) : Error,
                    EP4OriginalMeshImportError::FillDiffers);
            for (TFieldIterator<FProperty> Field(UStaticMesh::StaticClass()); Field; ++Field)
            {
                if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) ||
                    MeshLeft().Contains(Field->GetFName()))
                    continue;
                Field->CopyCompleteValue(
                    Field->ContainerPtrToValuePtr<void>(Mesh), Field->ContainerPtrToValuePtr<void>(Donor));
            }
            Mesh->BodySetup = Donor->BodySetup
                ? DuplicateObject<UBodySetup>(
                      Donor->BodySetup, Mesh, MakeUniqueObjectName(Mesh, UBodySetup::StaticClass(), TEXT("BodySetup")))
                : nullptr;
            if (Source.HasNavCollision)
                Mesh->CreateNavCollision(true);
            else
                Mesh->MarkAsNotHavingNavigationData();
            if (!P4SameText(State(Mesh), DonorState))
                return Fail(TEXT("mesh-import-placeholder-fill-differs-from-built-mesh"),
                    EP4OriginalMeshImportError::FillDiffers);
            Donor->MarkPendingKill();
        }
        else if (!Mesh->Rename(*Name, CreatePackage(*Package),
                     REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
            return Fail(TEXT("mesh-import-could-not-be-put-in-its-place"), EP4OriginalMeshImportError::RenameFailed);
        Mesh->ClearFlags(RF_Transient);
        Mesh->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        if (Build.SourceReconstructed)
        {
            // Bind new/fill metadata to the FINAL target identity. Never copy a
            // transient donor's path-bound record onto the published asset.
            if (!P4RecordMeshSourceReconstruction(Mesh, Source, Error) ||
                !P4RestoreMeshSourceReconstruction(Mesh, Error))
                return Fail(Error);
            if (Source.HasNavCollision)
                Mesh->CreateNavCollision(true);
            else
                Mesh->MarkAsNotHavingNavigationData();
            if (!Source.HasBodySetup)
                Mesh->BodySetup = nullptr;
            FP4MeshBuildReport Final;
            if (!P4CompareOriginalMeshBuild(Source, Mesh, Final, Error) || !Final.Equal)
                return Fail(Error.IsEmpty() ? FString(TEXT("mesh-import-published-source-differs-from-game")) : Error,
                    EP4OriginalMeshImportError::EngineBuildDiffers);
        }
        Baseline.Package = Package;
        Baseline.Class = Mesh->GetClass()->GetPathName();
        Baseline.Source = SourceSha256;
        Baseline.Equal = Build.Equal      ? TEXT("exact")
            : Build.EqualWithinEngineWeld ? TEXT("uv-weld")
                                          : TEXT("engine-tangent-frames");
        Baseline.LargestUVGap = Build.LargestUVGap;
        Baseline.State = State(Mesh);
        if (Baseline.State.IsEmpty())
            return Fail(TEXT("mesh-import-state-unreadable"), EP4OriginalMeshImportError::StateUnreadable);
        Mesh->GetOutermost()->GetMetaData()->SetValue(Mesh, BaselineKey, *BaselineText(Baseline));
        // What the game's engine has on a mesh and this one does not (ShadowCapsuleData, for one) is kept
        // in the mesh's record with the game's value, and the game keeps its own mesh.
        TArray<FP4OriginalValueNote> Notes;
        for (const FString& Line : Source.MeshProperties.NotHeld)
        {
            FP4OriginalValueNote Note;
            Note.Kind = TEXT("unsupported-value");
            Note.Action = TEXT("kept-for-reference");
            Note.Object = Mesh->GetPathName();
            Line.Split(TEXT(" = "), &Note.Property, &Note.GameValue);
            Notes.Add(MoveTemp(Note));
        }
        if (!P4MarkLegacyOriginalAsset(Mesh, Notes, Error))
            return Fail(Error, EP4OriginalMeshImportError::RecordFailed);
        if (StandIn)
            Mesh->GetOutermost()->GetMetaData()->RemoveValue(Mesh, FName(TEXT("P4BlueprintStandIn")));
        else
            FAssetRegistryModule::AssetCreated(Mesh);
        Mesh->MarkPackageDirty();
        Result.Created = !StandIn;
        Result.FilledStandIn = StandIn;
        Result.BuildComplete = true;
        Result.Lods = Build.LODs.Num();
    }
    TStrongObjectPtr<UStaticMesh> Kept(Mesh);
    // Source body settings and final identity/records are now established. An
    // ordinary Build's earlier default-body nav cache must not survive them.
    P4RefreshOriginalMeshNavigation(Mesh);
    Result.BaselineRead = true;
    Result.SourceDigest = Baseline.Source;
    Result.ImportedState = Baseline.State;
    Result.Equality = Baseline.Equal;
    Result.LargestUVGap = Baseline.LargestUVGap;
    Result.State = Baseline.State;
    FString FillSourceError;
    if (FillGuard && !P4PrepareVerifiedGameFillSource(Mesh, *FillGuard, FillSourceError))
        return Refuse(EP4OriginalMeshImportError::RecordFailed, FillSourceError);
    if (SaveToDisk && (StandIn || Result.Created || Mesh->GetOutermost()->IsDirty() || !OnDisk))
    {
        FString SaveError;
        if (!P4SaveAssetPackage(Mesh, [&]() { return P4SameText(State(Mesh), Baseline.State); }, SaveError))
            return Refuse(EP4OriginalMeshImportError::SaveFailed, TEXT("mesh-import-save-refused: ") + SaveError);
        Result.Saved = true;
    }
    if (FillGuard)
        FillGuard->Commit();
    Result.Asset = Mesh;
    return Result;
}
} //namespace P4StaticMeshContent

FString P4StaticMeshState(UStaticMesh* Mesh)
{
    return P4StaticMeshContent::State(Mesh);
}
FString P4OriginalMeshStatus(UStaticMesh* Mesh)
{
    return P4StaticMeshContent::Status(Mesh);
}
FString P4ImportOriginalMesh(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    return P4OriginalMeshImportJson(P4StaticMeshContent::Import(HeaderFile, BodyFile, SaveToDisk));
}

void P4StaticMeshSaveReferences(UStaticMesh* Mesh, TArray<UObject*>& Out)
{
    for (const FStaticMaterial& Slot : Mesh->StaticMaterials)
        if (Slot.MaterialInterface)
            Out.AddUnique(Slot.MaterialInterface);
}

FP4OriginalMeshImportResult P4ImportOriginalMeshNative(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    return P4StaticMeshContent::Import(HeaderFile, BodyFile, SaveToDisk);
}

FP4OriginalMeshImportResult P4ValidateReconstructedMeshImport(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    P4ImportPublication::FImportScope Import;
    return P4StaticMeshContent::Import(HeaderFile, BodyFile, SaveToDisk);
}

bool P4OriginalMeshBaselineUnchanged(UStaticMesh* Mesh)
{
    P4StaticMeshContent::FBaseline Baseline;
    return Mesh && P4StaticMeshContent::ReadBaseline(Mesh, Baseline) &&
        P4SameText(P4StaticMeshContent::State(Mesh), Baseline.State);
}

bool P4RefreshOriginalMeshNavigation(UStaticMesh* Mesh)
{
    using namespace P4StaticMeshContent;
    // The load delegate also sees foreign meshes. Avoid creating metadata or
    // touching derived data unless both independent original records agree.
    if (!IsInGameThread() || !IsValid(Mesh) || P4HasCreatorSource(Mesh) ||
        !FindObjectFast<UMetaData>(Mesh->GetOutermost(), FName(NAME_PackageMetaData)) || !Mesh->BodySetup)
        return false;
    UNavCollision* Collision = Cast<UNavCollision>(Mesh->NavCollision);
    if (!Collision)
        return false;
    static TSet<UStaticMesh*> Refreshing;
    if (Refreshing.Contains(Mesh))
        return false;
    Refreshing.Add(Mesh);
    struct FScope
    {
        UStaticMesh* Mesh;
        TSet<UStaticMesh*>& Set;
        ~FScope() { Set.Remove(Mesh); }
    } Scope{Mesh, Refreshing};
    FBaseline Baseline;
    FP4OriginalRecord Record;
    FString Error;
    if (!ReadBaseline(Mesh, Baseline) || !P4ReadOriginalRecord(Mesh, Record, Error) ||
        Record.Kind != TEXT("static-mesh") || !P4SameText(Record.SourceDigest, Baseline.Source) ||
        !P4SameText(Record.ComparisonDigest, Baseline.State) || !P4SameText(State(Mesh), Baseline.State))
        return false;
    // Setup's same-GUID/HasConvexGeometry shortcuts can retain an empty cache.
    // GatherCollision reads actual native physics and recomputes that flag.
    Mesh->BodySetup->CreatePhysicsMeshes();
    Collision->GatherCollision();
    return true;
}

TMap<FString, FString> P4StaticMeshStateParts(UStaticMesh* Mesh)
{
    using namespace P4StaticMeshContent;
    TMap<FString, FString> Out;
    if (!IsInGameThread() || !IsValid(Mesh))
        return Out;
    const auto Part = [&](const FString& Label, const TArray<uint8>& Bytes, const FString& Value = FString())
    {
        FString Digest;
        if (P4HashByteStream([&](const TFunction<bool(const uint8*, int32)>& Write)
                { return WriteAll(Write, Bytes.GetData(), Bytes.Num()); }, Digest))
            Out.Add(Label, Digest + TEXT(" | ") + Value.Left(512));
    };
    const auto Fields = [&](const FString& Prefix, UObject* Object, const TSet<FName>& Excluded)
    {
        if (!Object)
        {
            Out.Add(Prefix + TEXT(".present"), TEXT("false"));
            return;
        }
        Out.Add(Prefix + TEXT(".present"), TEXT("true"));
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
        {
            if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || Excluded.Contains(Field->GetFName()))
                continue;
            TArray<uint8> Bytes;
            FMemoryWriter Memory(Bytes, true);
            FObjectAndNameAsStringProxyArchive Writer(Memory, false);
            FString Text;
            for (int32 I = 0; I < Field->ArrayDim; ++I)
            {
                void* Value = Field->ContainerPtrToValuePtr<void>(Object, I);
                Field->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(), Value, nullptr);
                if (I == 0)
                    Field->ExportTextItem(Text, Value, nullptr, Object, PPF_None);
            }
            if (!Writer.IsError())
                Part(Prefix + TEXT(".") + Field->GetName(), Bytes, Text);
        }
    };
    Fields(TEXT("mesh"), Mesh, MeshLeft());
    Fields(TEXT("body"), Mesh->BodySetup, BodyLeft());
    Out.Add(TEXT("nav.present"), Mesh->NavCollision ? TEXT("true") : TEXT("false"));
    Out.Add(TEXT("socket.count"), LexToString(Mesh->Sockets.Num()));
    TArray<uint8> Materials;
    FMemoryWriter MaterialWriter(Materials, true);
    for (const auto& Slot : Mesh->StaticMaterials)
    {
        FString Material = Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : FString();
        FString Name = Slot.MaterialSlotName.ToString(), Imported = Slot.ImportedMaterialSlotName.ToString();
        bool Set = Slot.UVChannelData.bOverrideDensities;
        MaterialWriter << Material << Name << Imported << Set;
        if (Set)
            for (float Density : Slot.UVChannelData.LocalUVDensities)
                MaterialWriter << Density;
    }
    Part(TEXT("materials"), Materials);
    for (int32 L = 0; L < Mesh->GetNumSourceModels(); ++L)
    {
        TArray<uint8> Bytes;
        if (const FMeshDescription* D = Mesh->GetMeshDescription(L))
            Geometry(*D, Bytes);
        Part(FString::Printf(TEXT("geometry.%d"), L), Bytes);
    }
    return Out;
}

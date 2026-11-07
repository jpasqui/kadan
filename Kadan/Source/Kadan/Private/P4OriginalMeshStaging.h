#pragma once
#include "P4NumericContract.h"
#include "P4StaticMeshStaging.h"
#include "P4TextureProperties.h"

// The index in the file's own name table is kept beside the name. UE global name-table
// indices are unrelated; representability checks compare base/number/display.
struct FP4OriginalMeshName
{
    int32 SourceIndex = 0, SourceNumber = 0;
    FString SourceBase, Display;
};
struct FP4OriginalMeshMaterial
{
    FP4ExactMeshMaterialSlot Slot;
    FP4OriginalMeshName NativeName, ImportedName;
    FString ObjectPath;
    bool UVInitialized = false, UVOverride = false;
    std::array<float, 4> UVDensities{};
    uint64 TaggedStart = 0, TaggedEnd = 0;
};
struct FP4OriginalMeshSocket
{
    FString ObjectName;
    FP4TexturePropertyDocument Properties;
};
struct FP4OriginalMeshSource
{
    // Digest is of the header and the body as they were read: what is decoded is what was hashed.
    FString Digest, Package, ExportObject;
    P4MapKit::FStaticMeshRenderPayload Render;
    TArray<FP4OriginalMeshMaterial> Materials;
    TArray<FString> OtherTaggedProperties;
    // The mesh's tagged properties besides its material slots, and its body setup's when it has one,
    // decoded against the installed classes. What the file does not say is the class default.
    FP4TexturePropertyDocument MeshProperties, BodyProperties;
    bool HasBodySetup = false, HasNavCollision = false;
    int32 Sockets = 0;
    TArray<FP4OriginalMeshSocket> SocketProperties;
    // Why the two documents could not be made, when they could not.
    FString PropertyIssue;
    bool MaterialsValidated = false, RenderDecoded = false;
};

// Reads one of the game's cooked static meshes from its files: the header, the body (the header's name
// with .uexp when BodyFile is empty) and whichever bulk files lie beside the header. Each file is read
// whole, once, and everything is decoded from what was read. ExportObject is the mesh's whole path
// (package, a dot, its name). When it is empty the mesh is the one named after its package.
KADAN_API bool P4DecodeOriginalMesh(const FString& HeaderFile, const FString& BodyFile,
    const FString& ExportObject, FP4OriginalMeshSource& Out, FString& Error,
    const TFunction<bool()>& Cancel = {});
// Reflected wrapper is root-owned. JSON reports source decode evidence even
// when private geometry staging refuses. Every full-asset qualification is false.
KADAN_API FString P4StageOriginalMesh(const FString& HeaderFile, const FString& BodyFile,
    const FString& ExportObject);

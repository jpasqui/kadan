// Cooking a package of the export for the game, and reading back what a cooked package names.

#include "P4ExportInternal.h"
#include "Misc/ScopeExit.h"
#include "Engine/VolumeTexture.h"

namespace
{
bool SplitWritten(const FString& Base, FString& Error, const FString& Extension = TEXT(".uasset"))
{
    if (IFileManager::Get().FileSize(*(Base + Extension)) <= 0 ||
        IFileManager::Get().FileSize(*(Base + TEXT(".uexp"))) <= 0)
    {
        Error = TEXT("The cook did not write split files for ") + FPaths::GetCleanFilename(Base);
        return false;
    }
    return true;
}

// A cook of a mesh serializes the platform's render data, which for this editor's own platform is
// the editor's render data, and the save changes some of its fields. They are put back afterwards.
struct FKeepMeshRender
{
    UStaticMesh* Mesh;
    FStaticMeshRenderData* Render;
    int32 InlinedLods = 0, FirstLod = 0;
    using FSectionSamplers = decltype(FStaticMeshLODResources::AreaWeightedSectionSamplers);
    TArray<FSectionSamplers> Values;
    struct FFlags
    {
        bool Depth, Reversed, ReversedDepth, Color, Adjacency, Wireframe, Inlined, Optional;
        uint32 DepthTriangles, Size;
        FAdditionalStaticMeshIndexBuffers* Additional;
    };
    TArray<FFlags> Flags;
    explicit FKeepMeshRender(UStaticMesh* InMesh) : Mesh(InMesh), Render(InMesh->RenderData.Get())
    {
        if (!Render)
            return;
        InlinedLods = Render->NumInlinedLODs;
        FirstLod = Render->CurrentFirstLODIdx;
        for (const auto& Lod : Render->LODResources)
        {
            Values.Add(Lod.AreaWeightedSectionSamplers);
            Flags.Add({bool(Lod.bHasDepthOnlyIndices), bool(Lod.bHasReversedIndices),
                bool(Lod.bHasReversedDepthOnlyIndices), bool(Lod.bHasColorVertexData), bool(Lod.bHasAdjacencyInfo),
                bool(Lod.bHasWireframeIndices), bool(Lod.bBuffersInlined), bool(Lod.bIsOptionalLOD),
                Lod.DepthOnlyNumTriangles, Lod.BuffersSize, Lod.AdditionalIndexBuffers});
        }
    }
    void Restore()
    {
        if (!Render || Mesh->RenderData.Get() != Render || Render->LODResources.Num() != Values.Num())
        {
            Render = nullptr;
            return;
        }
        Render->NumInlinedLODs = InlinedLods;
        Render->CurrentFirstLODIdx = FirstLod;
        for (int32 I = 0; I < Values.Num(); ++I)
        {
            auto& Lod = Render->LODResources[I];
            const auto& F = Flags[I];
            Lod.AreaWeightedSectionSamplers = Values[I];
            Lod.bHasDepthOnlyIndices = F.Depth;
            Lod.bHasReversedIndices = F.Reversed;
            Lod.bHasReversedDepthOnlyIndices = F.ReversedDepth;
            Lod.bHasColorVertexData = F.Color;
            Lod.bHasAdjacencyInfo = F.Adjacency;
            Lod.DepthOnlyNumTriangles = F.DepthTriangles;
            Lod.bHasWireframeIndices = F.Wireframe;
            Lod.bBuffersInlined = F.Inlined;
            Lod.bIsOptionalLOD = F.Optional;
            Lod.BuffersSize = F.Size;
            if (!F.Additional && Lod.AdditionalIndexBuffers)
            {
                delete Lod.AdditionalIndexBuffers;
                Lod.AdditionalIndexBuffers = nullptr;
            }
        }
        Render = nullptr;
    }
    ~FKeepMeshRender() { Restore(); }
};

// The editor cooks collision for Windows. The game reads the same bytes under its own format name.
bool RelabelCollision(const FString& Base, FString& Error)
{
    std::vector<std::uint8_t> Header, Body;
    if (!P4LoadFileBytes(Base + TEXT(".uasset"), Header) || !P4LoadFileBytes(Base + TEXT(".uexp"), Body))
    {
        Error = TEXT("The cooked mesh could not be read back");
        return false;
    }
    P4MapKit::Export::RewrittenPackage Rewritten;
    std::string Why;
    if (!P4MapKit::Export::RelabelPhysXCook(Header, Body, Rewritten, Why))
    {
        Error = TEXT("The cooked mesh collision could not be prepared for the game (") +
            FString(UTF8_TO_TCHAR(Why.c_str())) + TEXT(")");
        return false;
    }
    if (!Rewritten.Changed)
        return true;
    TArray<uint8> Bytes;
    Bytes.Append(Rewritten.Header.data(), int32(Rewritten.Header.size()));
    if (!FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT(".uasset"))))
    {
        Error = TEXT("The cooked mesh header could not be written");
        return false;
    }
    return true;
}
} // namespace

namespace P4Export
{
FString CookedBase(const FString& Stage, const FString& Package)
{
    return Stage / P4ContainerRelativePath(Package);
}

bool NoLinks(const FString& Path, FString& Error)
{
#if PLATFORM_WINDOWS
    FString At = FPaths::ConvertRelativePathToFull(Path);
    while (!At.IsEmpty())
    {
        const DWORD Attributes = GetFileAttributesW(*At);
        if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            Error = TEXT("Export paths cannot contain filesystem links");
            return false;
        }
        const FString Parent = FPaths::GetPath(At);
        if (Parent == At)
            break;
        At = Parent;
    }
    TArray<FString> Pending;
    Pending.Add(Path);
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        if (Pending.Num() > 100000)
        {
            Error = TEXT("Export tree exceeds inspection limit");
            return false;
        }
        TArray<FString> Children;
        IFileManager::Get().FindFiles(Children, *(Pending[Index] / TEXT("*")), true, true);
        for (const FString& Child : Children)
        {
            const FString Full = Pending[Index] / Child;
            const DWORD Attributes = GetFileAttributesW(*Full);
            if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                Error = TEXT("Export tree contains a filesystem link");
                return false;
            }
            if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY))
                Pending.Add(Full);
        }
    }
    return true;
#else
    Error = TEXT("Export publication requires the supported Windows editor filesystem boundary");
    return false;
#endif
}
bool SafeIdLeaf(const FString& Id)
{
    FString Base;
    Id.Split(TEXT("."), &Base, nullptr);
    if (Base.IsEmpty())
        Base = Id;
    Base = Base.ToUpper();
    return Base != TEXT("CON") && Base != TEXT("PRN") && Base != TEXT("AUX") && Base != TEXT("NUL") &&
        !(Base.Len() == 4 && (Base.StartsWith(TEXT("COM")) || Base.StartsWith(TEXT("LPT"))) && Base[3] >= TEXT('1') &&
            Base[3] <= TEXT('9'));
}

ITargetPlatform* CookTarget()
{
    return GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
}

// A package's data in a separate bulk file (a texture's streamed mips) is cooked with its offset in that file and
// the flag that says so, as the game's own cook writes it. Without a save context the editor writes an offset
// the loader fixes up from the package's file summary, which the game's I/O store loader has none of: loading
// such a mip read from a null summary, and the game stopped.
bool CookTexture(UTexture* Texture, const FString& Stage, FString& Error)
{
    ITargetPlatform* Target = CookTarget();
    if (!Target || !GEditor)
    {
        Error = TEXT("Native cook platform is unavailable");
        return false;
    }
    UPackage* Package = Texture->GetOutermost();
    const FString Base = CookedBase(Stage, Package->GetName());
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true))
    {
        Error = TEXT("Cannot prepare cooked package directory");
        return false;
    }
    // Stock 4.26 makes a volume texture of a block-compressed format only on a platform that supports compressed volume
    // textures (UVolumeTexture::ShaderPlatformSupportsCompression); the game's does not, makes none, and the texture
    // samples black. A volume texture is cooked uncompressed; the creator's asset gets its setting back after. The
    // change goes through the editor's edit notifications: the cook target shares the editor's derived data.
    const bool bUncompress = Texture->IsA<UVolumeTexture>() && !Texture->CompressionNone;
    const bool bWasDirty = Texture->GetOutermost()->IsDirty();
    auto SetCompressionNone = [Texture](bool bNone)
    {
        Texture->PreEditChange(nullptr);
        Texture->CompressionNone = bNone;
        Texture->PostEditChange();
    };
    if (bUncompress)
        SetCompressionNone(true);
    ON_SCOPE_EXIT
    {
        if (bUncompress)
        {
            SetCompressionNone(false);
            Texture->GetOutermost()->SetDirtyFlag(bWasDirty);
        }
    };
    Texture->BeginCacheForCookedPlatformData(Target);
    Texture->FinishCachePlatformData();
    const uint32 Flags = Package->GetPackageFlags();
    const uint64 FileSize = Package->FileSize;
    const bool Dirty = Package->IsDirty();
    if (!Target->HasEditorOnlyData())
        Package->SetPackageFlags(PKG_FilterEditorOnly);
    FSavePackageContext GameBulkOffsets(nullptr, nullptr, false);
    const bool Saved = GEditor
                           ->Save(Package, Texture, RF_Public | RF_Standalone, *(Base + TEXT(".uasset")), GError,
                               nullptr, false, true,
                               SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_NoError | SAVE_Async, Target,
                               FDateTime::MinValue(), false, nullptr, &GameBulkOffsets)
                           .Result == ESavePackageResult::Success;
    Package->SetPackageFlagsTo(Flags);
    Package->FileSize = FileSize;
    Package->SetDirtyFlag(Dirty);
    UPackage::WaitForAsyncFileWrites();
    if (!Saved)
    {
        Error = TEXT("Native texture cook failed");
        return false;
    }
    return SplitWritten(Base, Error);
}

bool CookObject(UObject* Asset, const FString& Stage, FString& Error)
{
    ITargetPlatform* Target = CookTarget();
    if (!Target || !GEditor)
    {
        Error = TEXT("Native cook platform is unavailable");
        return false;
    }
    TStrongObjectPtr<UObject> Held(Asset);
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
        if (P4IsGameReplacement(Blueprint))
        {
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
            if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
            {
                Error = TEXT("Compile this Blueprint and fix its errors before exporting.");
                return false;
            }
        }
    const bool IsMap = Asset->IsA<UWorld>();
    const FString Extension = IsMap ? TEXT(".umap") : TEXT(".uasset");
    if (UWorld* World = Cast<UWorld>(Asset))
    {
        if (!World->PersistentLevel)
        {
            Error = TEXT("Your level has no persistent level.");
            return false;
        }
        if (ULevelScriptBlueprint* Script = World->PersistentLevel->GetLevelScriptBlueprint(true))
        {
            FKismetEditorUtilities::CompileBlueprint(Script);
            if (Script->Status == BS_Error || !Script->GeneratedClass)
            {
                Error = TEXT("Compile your level script and fix its errors before saving the mod.");
                return false;
            }
        }
    }
    UPackage* Package = Asset->GetOutermost();
    P4RefreshRecoveredFoliageDefaults();
    P4RefreshRecoveredParticleSystemDefaults();
    const FString Base = CookedBase(Stage, Package->GetName());
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true))
    {
        Error = TEXT("Cannot prepare cooked package directory");
        return false;
    }
    UStaticMesh* Mesh = Cast<UStaticMesh>(Asset);
    // Navigation collision keeps a cooked copy that a collision edit does not always discard. The cook
    // makes it again from the mesh's current collision.
    if (Mesh)
        if (UNavCollision* Navigation = Cast<UNavCollision>(Mesh->NavCollision))
            Navigation->CookedFormatData.FlushData();
    TUniquePtr<FKeepMeshRender> Render(Mesh ? new FKeepMeshRender(Mesh) : nullptr);
    bool Saved = false;
    {
        FP4LightCookDefaults LightDefaults(Asset);
        FP4DiagnosticCookPackageGuard Guard(Package);
        if (!Guard.IsReady())
        {
            Error = Guard.GetErrorDetail();
            return false;
        }
        if (!Target->HasEditorOnlyData())
            Package->SetPackageFlags(PKG_FilterEditorOnly);
        FSavePackageContext GameBulkOffsets(nullptr, nullptr, false);
        Saved = GEditor
                    ->Save(Package, Asset, IsMap ? RF_Standalone : RF_Public | RF_Standalone, *(Base + Extension),
                        GError, nullptr, false, true,
                        SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_NoError | SAVE_Async, Target,
                        FDateTime::MinValue(), false, nullptr, &GameBulkOffsets)
                    .Result == ESavePackageResult::Success;
        if (!Guard.AwaitAndRestore(Error))
            return false;
    }
    if (Render)
        Render->Restore();
    if (!Saved)
    {
        Error = TEXT("The cook of ") + Package->GetName() + TEXT(" failed");
        return false;
    }
    if (!SplitWritten(Base, Error, Extension))
        return false;
    if (Mesh && !RelabelCollision(Base, Error))
        return false;
    std::vector<uint8> Header, Body;
    const int64 BodySize = IFileManager::Get().FileSize(*(Base + TEXT(".uexp")));
    if (BodySize < 0 || !P4LoadFileBytes(Base + Extension, Header))
    {
        Error = TEXT("The cooked package could not be read back.");
        return false;
    }
    const auto Inventory = P4MapKit::ReadPackageHeader(Header, std::size_t(BodySize));
    if (!Inventory.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Inventory.Error.Code.c_str());
        return false;
    }
    if (std::find(Inventory.Names.begin(), Inventory.Names.end(), "P4RecoveredFoliageType") == Inventory.Names.end() &&
        std::find(Inventory.Names.begin(), Inventory.Names.end(), "P4RecoveredParticleSystem") ==
            Inventory.Names.end() &&
        !P4PackageHasStockEngineFields(Package) && !P4PackageHasGameStockTags(Package) &&
        !P4PackageHasGameNativeTails(Package) && !P4PackageHasGameMaterialModes(Package))
        return true;
    if (!P4LoadFileBytes(Base + TEXT(".uexp"), Body))
    {
        Error = TEXT("The cooked foliage references could not be read back.");
        return false;
    }
    const auto Before = Header, BeforeBody = Body;
    if (!P4TranslateRecoveredFoliageCook(Header, Body, Error))
        return false;
    if (!P4TranslateRecoveredParticleCook(Header, Body, Error))
        return false;
    if (!P4TranslateStockEngineFieldCook(Package, Header, Body, Error))
        return false;
    if (!P4TranslateGameStockTagCook(Package, Header, Body, Error))
        return false;
    if (!P4TranslateGameNativeTailCook(Package, Header, Body, Error))
        return false;
    if (!P4TranslateGameMaterialModeCook(Package, Header, Body, Error))
        return false;
    if (Body != BeforeBody)
    {
        TArray<uint8> Bytes;
        Bytes.Append(Body.data(), int32(Body.size()));
        if (!FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT(".uexp"))))
        {
            Error = TEXT("The cooked game engine fields could not be written.");
            return false;
        }
    }
    if (Header != Before)
    {
        TArray<uint8> Bytes;
        Bytes.Append(Header.data(), int32(Header.size()));
        if (!FFileHelper::SaveArrayToFile(Bytes, *(Base + Extension)))
        {
            Error = TEXT("The cooked foliage reference identities could not be written.");
            return false;
        }
    }
    return true;
}

// The packages a cooked package names, from its own header.
bool PackagesNamed(const FString& Header, TArray<FString>& Out, FString& Error)
{
    std::vector<std::uint8_t> Bytes;
    const int64 Body = IFileManager::Get().FileSize(*FPaths::ChangeExtension(Header, TEXT("uexp")));
    if (Body < 0 || !P4LoadFileBytes(Header, Bytes))
    {
        Error = TEXT("A cooked package could not be read back");
        return false;
    }
    const P4MapKit::FPackageInventory Inventory = P4MapKit::ReadPackageHeader(Bytes, std::size_t(Body));
    if (!Inventory.Error.Code.empty())
    {
        Error = TEXT("A cooked package could not be read back (") +
            FString(UTF8_TO_TCHAR(Inventory.Error.Code.c_str())) + TEXT(")");
        return false;
    }
    for (const P4MapKit::FPackageImport& Import : Inventory.Imports)
    {
        if (Import.Outer != 0 || Import.Class != "Package")
            continue;
        const FString Name = UTF8_TO_TCHAR(Import.Name.c_str());
        if (P4IsContentPackage(Name))
            Out.AddUnique(Name);
    }
    return true;
}

FString MeshIssue(UStaticMesh* Mesh)
{
    if (Mesh->GetClass() != UStaticMesh::StaticClass())
        return TEXT("Only plain static meshes can be exported.");
    if (!Mesh->RenderData || !Mesh->RenderData->LODResources.Num())
        return TEXT("Build your mesh before exporting it.");
    if (Mesh->BodySetup)
    {
        FBodyInstance Instance = Mesh->BodySetup->DefaultInstance;
        if (!P4CollisionProjectIssue(Instance, Mesh->BodySetup).IsEmpty())
            return TEXT(
                "Your mesh uses a collision setting this project does not have. Choose one of the project's collision presets.");
    }
    return FString();
}
} // namespace P4Export

bool P4CookGameAssetWhole(UObject* Asset, const FString& Stage, FString& Base, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Asset))
    {
        Error = TEXT("A game asset is cooked whole from a loaded asset on the editor thread.");
        return false;
    }
    const FString Package = Asset->GetOutermost()->GetName();
    Base = P4Export::CookedBase(Stage, Package);
    if (UWorld* World = Cast<UWorld>(Asset))
    {
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true))
        {
            Error = TEXT("Cannot prepare cooked package directory");
            return false;
        }
        return P4CookWholeOriginalLevel(World, Base, Error);
    }
    if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
        return P4Export::CookTexture(Texture, Stage, Error);
    return P4Export::CookObject(Asset, Stage, Error);
}

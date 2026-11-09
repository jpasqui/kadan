#include "P4GameReplacement.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4AuthoringRows.h"
#include "P4CreatorSource.h"
#include "P4KadanGameContent.h"
#include "P4KadanReadOnly.h"
#include "P4KadanAssetPolicy.h"
#include "P4OriginalRecord.h"
#include "AssetImportTask.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/ReferencerFinder.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
const TCHAR* const RecordKey = TEXT("P4KadanReplacesGame");
const TCHAR* const SourceKey = TEXT("P4KadanCreatorSource");
// What Kadan's import keeps with a game asset to say where it came from. A replacement keeps these as
// history in its own record, so the asset no longer says it is the game's.
const TCHAR* const GameKeys[] = {TEXT("P4OriginalRecord"), TEXT("P4NativeTextureBaseline"),
    TEXT("P4NativeMeshBaseline"), TEXT("P4NativeObjectBaseline"), TEXT("P4NativeObjectValuesNotHeld"),
    TEXT("P4NativeLevelBaseline"), TEXT("P4NativeLevelImported"), TEXT("P4NativeLevelLighting"),
    TEXT("P4NativeLevelRunLines"), TEXT("P4GameRows"), TEXT("P4KadanCopiedGameAsset"), TEXT("P4BlueprintStandIn")};
// The packages being replaced right now. A package name, because a texture is made again in place.
TSet<FName> Replacing;
const TCHAR* const BlueprintEditKey = TEXT("P4KadanEditedGameBlueprint");
const TCHAR* const InstanceEditKey = TEXT("P4KadanEditedGameMaterialInstance");

bool InstanceReplacement(const UObject* Asset, FP4GameReplacement* Out)
{
    using namespace P4Kadan;
    if (GameAssetShipping(EGameAssetFamily::MaterialInstance) != EGameAssetShipping::WholePackage ||
        !IsRecoveredGameMaterialInstance(Asset))
    {
        return false;
    }
    const FString Package = Asset->GetOutermost()->GetName();
    if (!P4IsContentPackageIgnoringCase(Package) || P4IsModFolderPackage(Package) ||
        FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, InstanceEditKey)) != Package)
    {
        return false;
    }
    if (Out)
    {
        Out->Package = Package;
        Out->Class = Asset->GetClass()->GetPathName();
        Out->Kind = TEXT("edited-game-material-instance");
    }
    return true;
}

bool BlueprintReplacement(const UObject* Asset, FP4GameReplacement* Out)
{
    using namespace P4Kadan;
    if (GameAssetShipping(GameAssetFamily(Asset)) != EGameAssetShipping::WholePackage ||
        !IsRecoveredGameActorBlueprint(Asset))
        return false;
    const UBlueprint* Blueprint = CastChecked<UBlueprint>(Asset);
    const FString Package = Asset->GetOutermost()->GetName();
    if (!P4IsContentPackage(Package) || P4IsModFolderPackage(Package) || !Blueprint->GeneratedClass ||
        Blueprint->GeneratedClass->ClassGeneratedBy != Blueprint ||
        Blueprint->GeneratedClass->GetOutermost() != Asset->GetOutermost() ||
        Blueprint->GeneratedClass->GetName() != Asset->GetName() + TEXT("_C") ||
        FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, BlueprintEditKey)) != Package)
        return false;
    const UClass* Native = Blueprint->ParentClass;
    while (Native && !Native->HasAnyClassFlags(CLASS_Native))
        Native = Native->GetSuperClass();
    if (!Native)
        return false;
    if (Out)
    {
        Out->Package = Package;
        Out->Class = Asset->GetClass()->GetPathName();
        Out->Kind = TEXT("edited-game-actor-blueprint");
        Out->GeneratedClass = Blueprint->GeneratedClass->GetPathName();
        Out->NativeParent = Native->GetPathName();
    }
    return true;
}

UMetaData* LoadedMetadata(const UObject* Asset)
{
#if WITH_EDITORONLY_DATA
    // Asset-registry tag gathering asks during a save: read the package's metadata only when it is there.
    UMetaData* Meta = Asset->GetOutermost()->MetaData;
    return Meta && !Meta->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) ? Meta : nullptr;
#else
    return nullptr;
#endif
}

TSharedPtr<FJsonObject> ReadRecord(const UObject* Asset)
{
    UMetaData* Meta = LoadedMetadata(Asset);
    if (!Meta || !Meta->HasValue(Asset, RecordKey))
        return nullptr;
    TSharedPtr<FJsonObject> Json;
    if (!P4ParseJson(Meta->GetValue(Asset, RecordKey), Json))
        return nullptr;
    double Format = 0;
    if (!Json->TryGetNumberField(TEXT("format"), Format) || Format != 1)
        return nullptr;
    return Json;
}

FString Compact(const TSharedRef<FJsonObject>& Json)
{
    FString Text = P4CondensedJson(Json);
    return Text;
}

bool IsImage(const FString& Extension)
{
    for (const TCHAR* One :
        {TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("tga"), TEXT("bmp"), TEXT("psd"), TEXT("exr")})
        if (Extension == One)
            return true;
    return false;
}

// The image decodes with the editor's texture factory, before the game's texture is touched.
bool Decodes(const FString& File, const FString& Extension, FString& Error)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *File) || !Bytes.Num())
    {
        Error = TEXT("Kadan cannot read your image file.");
        return false;
    }
    TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
    Factory->bCreateMaterial = false;
    TStrongObjectPtr<UPackage> Scratch(
        CreatePackage(*(TEXT("/Temp/KadanReplacement/") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    const uint8* Start = Bytes.GetData();
    UTexture2D* Decoded = Cast<UTexture2D>(Factory->FactoryCreateBinary(UTexture2D::StaticClass(), Scratch.Get(),
        TEXT("Decoded"), RF_Transient, nullptr, *Extension, Start, Bytes.GetData() + Bytes.Num(), GWarn));
    const bool Ok = Decoded && Decoded->Source.IsValid() && Decoded->Source.GetSizeX() > 0 &&
        Decoded->Source.GetSizeY() > 0;
    if (Decoded)
    {
        Decoded->ClearFlags(RF_Public | RF_Standalone);
        Decoded->MarkPendingKill();
    }
    Scratch->MarkPendingKill();
    if (!Ok)
        Error = TEXT("The editor cannot read your image. Use a PNG, JPEG, TGA, BMP, PSD or EXR file.");
    return Ok;
}

// The record that the asset replaces the game's, with what Kadan kept of the game asset as history. The
// keys that say it is the game's leave the asset.
void WriteRecord(UObject* Asset, const FString& Package, const FString& File, const FString& GameDigest,
    const TSharedRef<FJsonObject>& History)
{
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    for (const TCHAR* Key : GameKeys)
        Meta->RemoveValue(Asset, Key);
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("package"), Package);
    Json->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Json->SetStringField(TEXT("file"), File);
    Json->SetStringField(TEXT("game_digest"), GameDigest);
    Json->SetObjectField(TEXT("history"), History);
    Meta->SetValue(Asset, RecordKey, *Compact(Json));
    // The same diagnostic of the import the creator-source hook writes, naming the asset where it is now.
    TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
    Source->SetNumberField(TEXT("format"), 1);
    Source->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
    Source->SetStringField(TEXT("asset"), Asset->GetPathName());
    Source->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Source->SetStringField(TEXT("kind"), TEXT("creator-file"));
    Source->SetStringField(
        TEXT("source_mode"), Asset->IsA<UStaticMesh>() ? TEXT("fbx-factory") : TEXT("texture-factory"));
    Meta->SetValue(Asset, SourceKey, *Compact(Source));
    Asset->MarkPackageDirty();
}

struct FReplacing
{
    FName Package;
    explicit FReplacing(FName InPackage) : Package(InPackage) { Replacing.Add(Package); }
    ~FReplacing() { Replacing.Remove(Package); }
};

UObject* ReplaceTexture(UTexture2D* Game, const FString& File, FString& Error)
{
    UPackage* Package = Game->GetOutermost();
    const FName Name = Game->GetFName();
    TStrongObjectPtr<UTextureFactory> Factory(NewObject<UTextureFactory>());
    Factory->bCreateMaterial = false;
    // Automated, over the existing texture, keeping its settings (compression, color space, group, filter).
    Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
    Factory->AssetImportTask->bAutomated = true;
    Factory->AssetImportTask->bReplaceExisting = true;
    Factory->AssetImportTask->bReplaceExistingSettings = false;
    bool Cancelled = false;
    UObject* Made = Factory->ImportObject(UTexture2D::StaticClass(), Package, Name,
        RF_Public | RF_Standalone | RF_Transactional, File, nullptr, Cancelled);
    Factory->CleanUp();
    UTexture2D* Texture = Cast<UTexture2D>(Made);
    if (Cancelled || !Texture || Texture->GetOutermost() != Package || Texture->GetFName() != Name ||
        !P4HasCreatorSource(Texture))
    {
        Error = TEXT("The editor's import of your image did not complete.");
        return nullptr;
    }
    return Texture;
}

// References in the editor (level components, the game's Blueprints, selections) move to the new mesh. A
// component draws and collides with it again. The package metadata and the undo history are left alone.
void MoveReferences(UObject* Old, UObject* New)
{
    TMap<UObject*, UObject*> Map;
    Map.Add(Old, New);
    for (UObject* User : FReferencerFinder::GetAllReferencers(
             TArray<UObject*>{Old}, nullptr, EReferencerFinderFlags::SkipInnerReferences))
    {
        if (!IsValid(User) || User == Old || User == New || User->IsIn(Old) || User->IsA<UMetaData>() ||
            User->IsA<UTransactor>())
            continue;
        FArchiveReplaceObjectRef<UObject> Replace(User, Map, false, true, true);
        if (UActorComponent* Component = Cast<UActorComponent>(User))
            if (Component->IsRegistered())
                Component->ReregisterComponent();
    }
}

UObject* ReplaceMesh(UStaticMesh* Game, const FString& File, FString& Error)
{
    UPackage* Package = Game->GetOutermost();
    const FString Name = Game->GetName();
    // A fresh import beside the game, so the factory makes a whole new mesh of the file and nothing of the
    // game mesh's geometry or collision is kept. It moves to the game's path once it is complete.
    UPackage* Scratch = CreatePackage(
        *(TEXT("/Temp/KadanReplacement/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) / Name));
    TStrongObjectPtr<UFbxFactory> Factory(NewObject<UFbxFactory>());
    Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
    Factory->AssetImportTask->bAutomated = true;
    Factory->SetDetectImportTypeOnImport(false);
    Factory->ImportUI->MeshTypeToImport = FBXIT_StaticMesh;
    Factory->ImportUI->bAutomatedImportShouldDetectType = false;
    Factory->ImportUI->bImportMaterials = false;
    Factory->ImportUI->bImportTextures = false;
    Factory->ImportUI->bImportAnimations = false;
    Factory->ImportUI->StaticMeshImportData->bCombineMeshes = true;
    // The file's painted colors, which the game mesh's materials read as masks. Unreal's default ignores them.
    Factory->ImportUI->StaticMeshImportData->VertexColorImportOption = EVertexColorImportOption::Replace;
    // Collision shapes in the file (UCX_ and the like) are used, and a mesh without any gets collision made from its shape.
    Factory->ImportUI->StaticMeshImportData->bAutoGenerateCollision = true;
    bool Cancelled = false;
    UObject* Made = Factory->ImportObject(UStaticMesh::StaticClass(), Scratch, FName(*Name),
        RF_Public | RF_Standalone | RF_Transactional, File, nullptr, Cancelled);
    Factory->CleanUp();
    UStaticMesh* Mesh = Cast<UStaticMesh>(Made);
    if (Cancelled || !Mesh || Mesh->GetClass() != UStaticMesh::StaticClass() || Mesh->GetOutermost() != Scratch ||
        !P4HasCreatorSource(Mesh) || !Mesh->RenderData || !Mesh->RenderData->LODResources.Num())
    {
        if (Made)
        {
            Made->ClearFlags(RF_Public | RF_Standalone);
            Made->MarkPendingKill();
        }
        Scratch->SetDirtyFlag(false);
        Scratch->MarkPendingKill();
        Error = TEXT("The editor's import of your mesh did not complete. Use an FBX or OBJ file with one static mesh.");
        return nullptr;
    }
    // What the game's mesh is set to keeps: the materials of its slots, its sockets, how it collides and
    // whether Pikmin navigation uses it. The shapes are the file's.
    for (int32 Slot = 0; Slot < Mesh->StaticMaterials.Num() && Slot < Game->StaticMaterials.Num(); ++Slot)
    {
        Mesh->StaticMaterials[Slot].MaterialInterface = Game->StaticMaterials[Slot].MaterialInterface;
        Mesh->StaticMaterials[Slot].MaterialSlotName = Game->StaticMaterials[Slot].MaterialSlotName;
    }
    for (UStaticMeshSocket* Socket : Game->Sockets)
        if (Socket && !Mesh->FindSocket(Socket->SocketName))
            Mesh->AddSocket(DuplicateObject<UStaticMeshSocket>(Socket, Mesh));
    Mesh->bHasNavigationData = Game->bHasNavigationData;
    if (!Mesh->BodySetup)
        Mesh->CreateBodySetup();
    if (Game->BodySetup)
        Mesh->BodySetup->CopyBodySetupProperty(Game->BodySetup);
    Mesh->PostEditChange();
    Mesh->BodySetup->InvalidatePhysicsData();
    Mesh->BodySetup->CreatePhysicsMeshes();

    if (UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
        Editors->CloseAllEditorsForAsset(Game);
    // The game's mesh leaves its package (its loader is let go of, so nothing loads it from the old file again)
    // and the new mesh takes its name.
    Game->Rename(
        *MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), FName(*(Name + TEXT("_Replaced"))))
            .ToString(),
        GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
    Game->ClearFlags(RF_Public | RF_Standalone);
    Mesh->Rename(*Name, Package, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
    Mesh->SetFlags(RF_Public | RF_Standalone);
    Scratch->SetDirtyFlag(false);
    Scratch->MarkPendingKill();
    MoveReferences(Game, Mesh);
    Game->MarkPendingKill();
    Package->GetMetaData()->RemoveMetaDataOutsidePackage();
    return Mesh;
}
} //namespace

bool P4IsGameReplacement(const UObject* Asset, FP4GameReplacement* Out)
{
    if (Out)
        *Out = FP4GameReplacement();
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    if (BlueprintReplacement(Asset, Out))
        return true;
    if (InstanceReplacement(Asset, Out))
    {
        return true;
    }
    if (Asset->GetClass() != UTexture2D::StaticClass() && Asset->GetClass() != UStaticMesh::StaticClass())
        return false;
    const TSharedPtr<FJsonObject> Json = ReadRecord(Asset);
    FP4GameReplacement Value;
    if (!Json.IsValid() || !Json->TryGetStringField(TEXT("package"), Value.Package) ||
        !Json->TryGetStringField(TEXT("class"), Value.Class) || !Json->TryGetStringField(TEXT("file"), Value.File) ||
        !Json->TryGetStringField(TEXT("game_digest"), Value.GameDigest))
        return false;
    // Only at the game asset's own path, made from the creator's file by the editor's factory.
    if (Value.Package != Asset->GetOutermost()->GetName() || Value.Class != Asset->GetClass()->GetPathName() ||
        Value.File.IsEmpty() || !P4HasCreatorSource(Asset))
        return false;
    if (Out)
        *Out = MoveTemp(Value);
    return true;
}

void P4RecordGameBlueprintEdit(UObject* Object)
{
    using namespace P4Kadan;
    if (!IsInGameThread() || !IsValid(Object) ||
        GameAssetShipping(EGameAssetFamily::ActorBlueprint) != EGameAssetShipping::WholePackage)
        return;
    UPackage* Package = Object->GetOutermost();
    UBlueprint* Blueprint = FindObject<UBlueprint>(Package, *FPackageName::GetLongPackageAssetName(Package->GetName()));
    if (!IsRecoveredGameActorBlueprint(Blueprint) || !GetAssetPolicy(Blueprint).AllowEdit)
        return;
    Package->GetMetaData()->SetValue(Blueprint, BlueprintEditKey, *Package->GetName());
}

void P4RecordGameMaterialInstanceEdit(UObject* Object)
{
    using namespace P4Kadan;
    if (!IsInGameThread() || !IsValid(Object) ||
        GameAssetShipping(EGameAssetFamily::MaterialInstance) != EGameAssetShipping::WholePackage)
    {
        return;
    }
    UPackage* Package = Object->GetOutermost();
    auto* Instance = FindObject<UMaterialInstanceConstant>(
        Package, *FPackageName::GetLongPackageAssetName(Package->GetName()));
    if (IsRecoveredGameMaterialInstance(Instance) && GetAssetPolicy(Instance).AllowEdit)
    {
        Package->GetMetaData()->SetValue(Instance, InstanceEditKey, *Package->GetName());
    }
}

bool P4IsGameReplacementPackage(const FString& Package, FP4GameReplacement* Out)
{
    if (Out)
        *Out = FP4GameReplacement();
    if (!IsInGameThread() || !FPackageName::IsValidLongPackageName(Package))
        return false;
    const FString Path = P4ObjectPathOf(Package);
    UObject* Asset = FindObject<UObject>(nullptr, *Path);
    if (!Asset && FPackageName::DoesPackageExist(Package))
        Asset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    return Asset && P4GameReplacementShips(Asset, Out);
}

bool P4GameReplacementShips(UObject* Asset, FP4GameReplacement* Out)
{
    if (!P4IsGameReplacement(Asset, Out))
    {
        return false;
    }
    if (!P4QueryOriginalAsset(Asset).IsOriginal())
    {
        return true;
    }
    const FP4OriginalDiagnostic Compared = P4DiagnoseOriginalAsset(Asset);
    if (Asset->GetOutermost()->IsDirty() || Compared.Comparison != EP4OriginalComparison::Equal)
    {
        return true;
    }
    if (Out)
    {
        *Out = FP4GameReplacement();
    }
    return false;
}

bool P4CanReplaceGameAsset(UObject* Asset, FString* Why)
{
    if (Why)
        Why->Reset();
    const auto No = [&](const TCHAR* Reason)
    {
        if (Why)
            *Why = Reason;
        return false;
    };
    if (!IsInGameThread() || !IsValid(Asset))
        return No(TEXT("Select a texture or a static mesh of the game's."));
    if (Asset->GetClass() != UTexture2D::StaticClass() && Asset->GetClass() != UStaticMesh::StaticClass())
        return No(TEXT("Only the game's textures and static meshes can be replaced with your own file."));
    const FString Package = Asset->GetOutermost()->GetName();
    if (!P4IsContentPackage(Package) || P4IsModFolderPackage(Package))
        return No(TEXT("Only an asset at one of the game's paths can be replaced with your own file."));
    if (P4IsGameReplacement(Asset))
        return true;
    if (P4IsCreatorAsset(Asset))
        return No(TEXT("This asset is already yours. Reimport your file into it instead."));
    return true;
}

bool P4IsReplacingGameAsset(const UObject* Asset)
{
    return IsValid(Asset) && Replacing.Contains(Asset->GetOutermost()->GetFName());
}

UObject* P4ReplaceGameAsset(UObject* Asset, const FString& InFile, FString& Error)
{
    Error.Reset();
    if (!GEditor)
    {
        Error = TEXT("Replacing a game asset needs the editor.");
        return nullptr;
    }
    if (!P4CanReplaceGameAsset(Asset, &Error))
        return nullptr;
    FString File = FPaths::ConvertRelativePathToFull(InFile);
    FPaths::NormalizeFilename(File);
    const FString Extension = FPaths::GetExtension(File).ToLower();
    const bool Texture = Asset->IsA<UTexture2D>();
    if (IFileManager::Get().FileSize(*File) <= 0)
    {
        Error = TEXT("Choose a file that exists and is not empty.");
        return nullptr;
    }
    if (Texture ? !IsImage(Extension) : (Extension != TEXT("fbx") && Extension != TEXT("obj")))
    {
        Error = Texture ? TEXT("Choose an image file (PNG, JPEG, TGA, BMP, PSD or EXR) for a texture.")
                        : TEXT("Choose an FBX or OBJ file for a static mesh.");
        return nullptr;
    }
    if (Texture && !Decodes(File, Extension, Error))
        return nullptr;
    UPackage* Package = Asset->GetOutermost();
    const FString PackageName = Package->GetName();
    // A replacement made again keeps what its first replacement kept of the game asset.
    TSharedRef<FJsonObject> History = MakeShared<FJsonObject>();
    FString GameDigest;
    FP4GameReplacement Previous;
    if (P4IsGameReplacement(Asset, &Previous))
    {
        GameDigest = Previous.GameDigest;
        const TSharedPtr<FJsonObject> Record = ReadRecord(Asset);
        const TSharedPtr<FJsonObject>* Kept = nullptr;
        if (Record.IsValid() && Record->TryGetObjectField(TEXT("history"), Kept) && Kept && Kept->IsValid())
            History = Kept->ToSharedRef();
    }
    else
    {
        GameDigest = P4GameContentDigest(Asset);
        if (UMetaData* Meta = LoadedMetadata(Asset))
            for (const TCHAR* Key : GameKeys)
                if (Meta->HasValue(Asset, Key))
                    History->SetStringField(Key, Meta->GetValue(Asset, Key));
    }
    UObject* Made = nullptr;
    {
        // The package is writable while the import runs. The guards lock it again when this fails.
        FReplacing Scope(Package->GetFName());
        P4Kadan::UpdateReadOnlyAsset(Asset);
        Made = Texture ? ReplaceTexture(CastChecked<UTexture2D>(Asset), File, Error)
                       : ReplaceMesh(CastChecked<UStaticMesh>(Asset), File, Error);
        if (Made)
            WriteRecord(Made, PackageName, File, GameDigest, History);
    }
    if (!Made)
    {
        if (IsValid(Asset))
            P4Kadan::UpdateReadOnlyAsset(Asset);
        return nullptr;
    }
    // The asset registry keeps the package's entry: it is not a new package (AssetCreated would mark it as one
    // that was never saved), and its tags are read again when it is saved.
    P4Kadan::UpdateReadOnlyAsset(Made);
    if (!P4IsGameReplacement(Made))
    {
        Error = TEXT("Kadan could not record that your file replaces the game's asset.");
        return nullptr;
    }
    return Made;
}

UObject* UP4AuthoringTools::NativeReplaceGameAsset(UObject* Asset, const FString& File, FString& Error)
{
    return P4ReplaceGameAsset(Asset, File, Error);
}

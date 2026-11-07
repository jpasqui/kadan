// Native editor tools. Original content uses recovered game schemas.
#pragma once
#include "P4NumericContract.h"
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/AssetUserData.h"
#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/TargetPoint.h"
#include "Factories/Factory.h"
#include "P4AuthoringRows.generated.h"
class USkeletalMesh;
class UTexture2D;
class UTexture;
class UMaterial;
class UMaterialInstanceConstant;
class UStaticMesh;
class UMaterialInterface;

// A standard Content Browser creation factory for the game's UObject count asset.
UCLASS()
class KADAN_API UP4ActorCountsFactory : public UFactory
{
    GENERATED_BODY()
public:
    UP4ActorCountsFactory();
    virtual UClass* ResolveSupportedClass() override;
    virtual FText GetDisplayName() const override;
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context,
        FFeedbackContext* Warn) override;
};

UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Pikmin 4 Material"))
class KADAN_API UP4MaterialExportData : public UAssetUserData
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pikmin 4|Shader",
        meta = (FilePathFilter = "glsl;frag;hlsl;cg",
            ToolTip =
                "Leave empty to export the authored material graph. A file overrides the graph for this material; instances inherit it unless they specify their own file."))
    FFilePath ShaderFile;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pikmin 4|Provenance")
    FString Representation = TEXT("Authored");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pikmin 4|Provenance")
    FString OriginalMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pikmin 4|Provenance")
    FString ReconstructionNotes;
    virtual bool IsEditorOnly() const override { return true; }
};

UCLASS(meta = (DisplayName = "Pikmin 4 Gameplay Handle"))
class KADAN_API AP4GameplayHandle : public ATargetPoint
{
    GENERATED_BODY()
public:
    AP4GameplayHandle() { bIsEditorOnlyActor = true; }
    UPROPERTY(EditAnywhere, Category = "Pikmin 4|Spawn") bool OverrideSpawnCount = false;
    UPROPERTY(EditAnywhere, Category = "Pikmin 4|Spawn",
        meta = (ClampMin = "1", ClampMax = "1000", EditCondition = "OverrideSpawnCount"))
    int32 SpawnCount = 1;
    UPROPERTY(EditAnywhere, Category = "Pikmin 4|Spawn") bool OverrideSpawnRadius = false;
    UPROPERTY(EditAnywhere, Category = "Pikmin 4|Spawn",
        meta = (ClampMin = "0", ClampMax = "100000", Units = "cm", EditCondition = "OverrideSpawnRadius"))
    float SpawnRadius = 300.0f;
    UPROPERTY(EditAnywhere, Category = "Pikmin 4|Persistence") bool OverrideSavePersistence = false;
    UPROPERTY(EditAnywhere, Category = "Pikmin 4|Persistence", meta = (EditCondition = "OverrideSavePersistence"))
    bool SaveActorState = true;
};

UCLASS()
class KADAN_API UP4AuthoringTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Transitional JSON transport; discovery and policy are implemented in C++.
    /** Private acceptance diagnostic for the actual loaded kit DLLs and their native module manifests. */

    /** Private acceptance diagnostic for the six actual loaded original runtime identity modules. */

    /** Observe the current skeletal placeholder native model/render shape without allocating or initializing it. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Diagnostics")
    static FString NativeSkinnedPlaceholderShape(UObject* Asset);

    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativePackageInventory(
        const FString& HeaderFile, const FString& BodyFile = TEXT(""), bool DecodeTexturePixels = false);

    /** Read original texture groups and lexical profile settings without applying defaults or publishing assets. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeTextureProfileInventory(
        const FString& CaptureFile, const TArray<FString>& ConfigFiles, const TArray<FString>& ProfileSections);

    /** Decode exact original table and scene values without creating assets. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeDecodeOriginalPackage(const FString& HeaderFile, const FString& BodyFile = TEXT(""));
    /** Read one of the game's cooked static meshes (the .uasset, with its .uexp and bulk files next to it) into private MeshDescriptions and verify them against the file; does not publish an editable asset. ExportObject is the mesh's whole path, or empty for the mesh named after its package. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeStageOriginalMesh(
        const FString& HeaderFile, const FString& BodyFile = TEXT(""), const FString& ExportObject = TEXT(""));
    /** Build an ordinary static mesh from one of the game's cooked static meshes with the engine's mesh builder and compare the result with the game's render data. The mesh is discarded; nothing is added to Content. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeBuildOriginalMesh(
        const FString& HeaderFile, const FString& BodyFile = TEXT(""), const FString& ExportObject = TEXT(""));
    /** Make an ordinary texture from one of the game's cooked textures (the .uasset, with its .uexp and .ubulk next to it), let the engine build it and compare it with the game's. The texture is discarded; nothing is added to Content. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeBuildOriginalTexture(const FString& HeaderFile, const FString& BodyFile = TEXT(""));
    /** Import one of the game's cooked textures as an ordinary texture at its game path in Content, and record what it was imported as. A texture that was edited since its import is never replaced. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeImportOriginalTexture(
        const FString& HeaderFile, const FString& BodyFile = TEXT(""), bool SaveToDisk = false);
    /** Whether a texture was imported from the game by the kit, and whether it is still as imported. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeOriginalTextureStatus(UTexture* Texture);
    /** Make a material at a path in Content from a node graph in the kit's JSON (the form the material decompiler writes), and read its graph back. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeBuildMaterialGraph(const FString& GraphJson, const FString& Package, bool SaveToDisk = false);
    /** A material's node graph and settings as the kit's JSON, or an empty string when it holds something that form cannot say. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeMaterialGraphJson(UMaterial* Material);
    /** Make a material instance at a path in Content from the kit's JSON: its parent, the scalar, vector and texture parameters it sets, its static switches and what it overrides of the parent's settings. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeBuildMaterialInstance(
        const FString& InstanceJson, const FString& Package, bool SaveToDisk = false);
    /** A material instance's parent and what it sets, as the kit's JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeMaterialInstanceJson(UMaterialInstanceConstant* Instance);
    /** Import one of the game's cooked levels as an ordinary Unreal level at its game path in Content, compared with the game's file by saving it the way a cook does. A level that holds anything not brought over yet is refused whole, by name. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeImportOriginalLevel(
        const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk = false);
    /** Import one of the game's maps with everything it names, through the Kadan panel's importer: the game folder is read, its materials are recovered from the game's own files, the map's packages are planned in the order they need each other, and each is imported and saved. What the project has already is left as it is. Returns the import report (Saved/Kadan/import-result.json) as JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeKadanImportMap(const FString& GameFolder, const FString& MapPackage);

    UFUNCTION(BlueprintCallable, Category = "Kadan")
    static FString NativeSetupKadanProject(const FString& GamePaks, const FString& Repak);

    UFUNCTION(BlueprintCallable, Category = "Kadan")
    static bool NativeKadanProjectReady();

    /** The Content Browser's Replace with my own file action, for repeatable editor workflows. Leaves the replacement unsaved. */
    UFUNCTION(BlueprintCallable, Category = "Kadan")
    static UObject* NativeReplaceGameAsset(UObject* Asset, const FString& File, FString& Error);
    /** Import with the panel's game-backed fallback policy, using original containers and both tools to identify references lost during package conversion. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeKadanImportMapFromContainers(const FString& GameFolder, const FString& MapPackage,
        const FString& GamePaks, const FString& Retoc, const FString& Repak = TEXT(""));
    /** Whether a level was imported from the game by the kit, and whether it is still as imported. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeOriginalLevelStatus(UWorld* World);
    /** What an edit to a level imported from the game amounts to: actors of the game's taken out, moved or changed otherwise, and actors added. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeOriginalLevelChanges(UWorld* World);
    /** Write the collision of an actor that navigation takes into account, as the engine's own navigation build takes it, to a file the kit's navigation bake reads. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Export")
    static FString NativeWriteActorNavigationCollision(AActor* Actor, const FString& File);
    /** Take out the foliage of a level that stands inside a sphere, the way the foliage mode's eraser does. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Edit")
    static FString NativeEraseFoliage(UWorld* World, FVector Center, float Radius);
    /** Add one piece of foliage of the kind a component of the level's foliage actor draws, the way the foliage mode's brush adds one. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Edit")
    static FString NativeAddFoliage(
        UWorld* World, UInstancedStaticMeshComponent* Kind, FVector Location, FRotator Rotation, FVector Scale);
    /** Whether a Blueprint class of the project says what the game's cooked file of the class says (defaults, component templates, construction script nodes; not code). Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeCompareOriginalClass(const FString& HeaderFile, const FString& BodyFile);
    /** Save the mod's identity through the same action as the Kadan panel. UpdateExisting allows editing an existing identity. Returns JSON with valid, error and the saved identity. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Export")
    static FString NativeSaveMod(
        const FString& ModId, const FString& Name, const FString& Version, bool UpdateExisting = false);
    /** The Kadan panel's export of a mod of this project, for scripts. ModId names a persisted mod. Roots names packages or folders (commas between). MapPackage is the game's persistent map when game levels are edited. GameFiles is the folder the game's packages were extracted to. Returns JSON: valid, error, published, directory, packages, levels, files, refused, told. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Export")
    static FString NativeExportMod(const FString& ModId, const FString& Roots, const FString& MapPackage,
        const FString& GamePaks, const FString& OutputDirectory, const FString& WorkDirectory, const FString& Retoc,
        const FString& Repak, const FString& GameFiles);
    /** Put a map's bake setup into a level, for a lighting build to light the level with. Setup is JSON as the map's recovered lighting has it: sun, sky (with the game's cubemap or a capture of the map's sky sphere), importance_volumes, lights, game_sun and lightmass. Everything put in has a label that begins with P4Bake. The answer names what was put in and says what the sky light takes its light from (sky_source), so that a stand-in can be told. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Export")
    static FString NativeLightingBakeSetup(UWorld* World, const FString& Setup);
    /** After the lighting build: take the bake setup's lights out of the level again, and with them what the build made for the level as a whole (its volumetric lightmap), which a level loaded into a map would give the whole map in place of the map's own. What the build made for each mesh stays. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Export")
    static FString NativeLightingBakeClear(UWorld* World);
    /** What a level says, one line for each thing, as a cook's save of it holds it. A level's state is the digest of this text. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeLevelContent(UWorld* World);
    /** Import one of the game's cooked assets that is properties only (a data asset, a curve, a set of environment values, a table with its rows) at its game path, compared with the game's file by saving it the way a cook does. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeImportOriginalAsset(
        const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk = false);
    /** Whether an asset was imported from the game that way, and whether it is still as imported. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeOriginalAssetStatus(UObject* Asset);
    /** Cheap persisted provenance status for every imported kind. Does not save or cook. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeOriginalImportStatus(UObject* Asset);
    /** Explicitly compare current content with its imported baseline. May cook; never call for polling. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeOriginalImportDiagnostic(UObject* Asset);
    /** Explicitly compare an old import baseline. Never writes the asset. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeMigrateOriginalImportRecord(UObject* Asset);
    /** Import one of the game's cooked static meshes (the .uasset, with its .uexp and bulk files next to it) as an ordinary static mesh at its game path in Content, with the game's material slots, lightmap settings and collision settings, and record what it was imported as. Its materials have to be in the project. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeImportOriginalMesh(
        const FString& HeaderFile, const FString& BodyFile = TEXT(""), bool SaveToDisk = false);
    /** Whether a static mesh was imported from the game by the kit, and whether it is still as imported. Returns JSON. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeOriginalMeshStatus(UStaticMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativeProjectInventory();
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativeWorldExportPlan(const FString& WorldPackage);
    /** Inspect actual streaming Worlds without copying or changing their settings. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativeWorldStreamingGraph(UWorld* World, const FString& TargetPackage);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static UP4MaterialExportData* MaterialExportSettings(UMaterialInterface* Material, bool Create = false);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool SetMaterialProvenance(UMaterialInterface* Material, const FString& Representation,
        const FString& OriginalMaterial, const FString& Notes);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString TableJson(UDataTable* Table);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool ImportTableJson(UDataTable* Table, const FString& Json);
    /** Resolve serialized field identities, validate exact values and hard references without changing rows. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeCanonicalTableRows(UDataTable* Table, const FString& Json);
    /** Compare every serialized field to independent native row readback; no table mutation. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeTableReadback(UDataTable* Table, const FString& SourceJson);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativeTableSchema(UDataTable* Table);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativeTableRowsJson(UDataTable* Table);
    /** Plan reimport from qualified native tables without modifying any input. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeMergeTableRows(UDataTable* Current, UDataTable* Previous, UDataTable* Source);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativeAssetSchema(UObject* Asset);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativeAssetValuesJson(UObject* Asset);
    /** Native schema/value checks; source references may be discovered before their targets exist. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeCanonicalAssetValues(UObject* Asset, const FString& Json);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeAssetReferences(UObject* Asset, const FString& Json);
    /** Independently compare actual properties against serialized overrides. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static FString NativeAssetReadback(UObject* Asset, const FString& SourceJson);
    /** Stage typed writes and verify before publishing into the target. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4|Import")
    static bool NativeApplyAssetValues(UObject* Asset, const FString& Json);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static UObject* NewNativeAsset(UClass* Class);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static UObject* CreateNativeAsset(UClass* Class, const FString& Package);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static int32 EnableOriginalAssetDetails();
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool CopyNativeAssetValues(UObject* Target, UObject* Source);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static UDataTable* NewNativeTable(UScriptStruct* RowStruct);
    // Read serialized live references, including Blueprint CDO/template edits and soft paths.
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static TArray<FString> NativeAssetDependencies(UObject* Asset);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString NativePlacementInfo(AActor* Actor);
    // The native Place Actors panel uses the recovered classes and editor Blueprints themselves.
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static int32 RefreshPlaceActors();
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static TArray<FString> PlaceActorPaths(FName Category);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool SetPropertyText(UObject* Object, FName Property, const FString& Text);
    /** Strict editor edit with readonly checks, transaction and property-chain notification. Failed parsing leaves the target unchanged. */
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool SetEditablePropertyText(UObject* Object, FName Property, const FString& Text);
    // Integer values are written as decimal JSON tokens, including uint64 save identities.
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString PropertyJson(UObject* Object, FName Property);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool EnsureActorRoot(AActor* Actor);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool RefreshActorDefaults(AActor* DefaultActor);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static FString StreamingState(UWorld* World);
    // Remap only the temporary world's level references; never edit source worlds.
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool RemapStreaming(UWorld* World, const FString& MappingJson, bool ForBake, bool MarkDirty = true);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool LoadExportLevels(UWorld* World);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool SetExportLevel(UWorld* World, const FString& Package);
    UFUNCTION(BlueprintCallable, Category = "Pikmin 4")
    static bool SaveExportWorld(UWorld* World, const FString& Package);
};

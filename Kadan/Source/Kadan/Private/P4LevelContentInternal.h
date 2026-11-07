// Internal to the level content files (P4Level*.cpp, P4OriginalClassCompare.cpp, P4OriginalObjectImport.cpp,
// P4GameBackedLevelImport.cpp, P4MaterialInstanceCopyBoundary.cpp): what a level or asset file says once
// read, the keys an imported level keeps its records under, and the functions the files share.
#pragma once

#include "P4KadanReimportAsset.h"
#include "P4Common.h"
#include "P4RecoveredPostProcessCook.h"
#include "P4StockEngineFieldContent.h"
#include "P4StockTags.h"
#include "P4GameNativeTails.h"
#include "P4LegacyVectorFieldCompatibility.h"
#include "P4MapKitCore/blueprint_class.h"
#include "P4OriginalAssetImport.h"
#include "P4BlueprintImport.h"
#include "P4BlueprintSubobjectStorage.h"
#include "P4AuthoringRows.h"
#include "P4OriginalLevelImport.h"
#include "P4OriginalClassComparison.h"
#include "P4OriginalClassValueNotes.h"
#include "P4StrictNativeClassSources.h"
#include "P4BoundedSourceRead.h"
#include "P4OriginalWorldSource.h"
#include "P4InheritedTemplateAliases.h"
#include "P4RedundantMaterialOverrides.h"
#include "P4OriginalClassSourceComparison.h"
#include "P4DecodedPropertyWriter.h"
#include "P4RecoveredPostProcessStorage.h"
#include "P4RecoveredPostProcessValues.h"
#include "P4DiagnosticCookPackageGuard.h"
#include "P4CookedTableRowObservation.h"
#include "P4LitActorFacts.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4LevelContent.h"
#include "P4LevelImportWorld.h"
#include "P4OriginalComponentReferences.h"
#include "P4OriginalEngineFields.h"
#include "P4KadanImportSources.h"
#include "P4MaterialCollectionSource.h"
#include "P4MaterialInstanceComparison.h"
#include "P4KadanImport.h"
#include "P4LevelChanges.h"
#include "P4GameLighting.h"
#include "P4LevelExport.h"
#include "P4PlacementExport.h"
#include "P4ActorCollision.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
#include "P4OriginalImportSave.h"
#include "P4DroppedVisibilityIds.h"
#if P4_KADAN_TEST_SEAMS
#include "HAL/PlatformFilemanager.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectHash.h"
#endif
#include "Interfaces/Interface_AssetUserData.h"
#include "P4StandInFill.h"
#include "UObject/StrongObjectPtr.h"
#include "Materials/Material.h"
#include "MaterialShaderQualitySettings.h"
#include "Materials/MaterialParameterCollection.h"
#include "P4NativeHash.h"
#include "P4PackageSave.h"
#include "PackageTools.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectHash.h"
#include "P4OriginalDecode.h"
#include "P4ComparisonNames.h"
#include "P4SkinnedContent.h"
#include "P4PhysicsContent.h"
#include "P4FoliageContent.h"
#include "P4RecoveredFoliageType.h"
#include "P4RecoveredLightCook.h"
#include "P4RecoveredParticleSystem.h"
#include "P4OriginalParticlePreflight.h"
#include "P4ParticleDistributionDefaults.h"
#include "Particles/ParticleModuleRequired.h"
#include "P4MapKitCore/level_native.h"
#include "P4MapKitCore/component_tail_relocation.h"
#include "P4MapKitCore/physics_asset_native.h"
#include "P4MapKitCore/blueprint_empty_level.h"
#include "P4MapKitCore/blueprint_compiler_frame.h"
#include "P4MapKitCore/property_decoder.h"
#include "P4MapKitCore/table_row_comparison.h"
#include "P4TextureBuild.h"
#include "UObject/LazyObjectPtr.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Engine/Texture2D.h"
#include "VectorField/VectorFieldStatic.h"
#include "LandscapeComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeProxy.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Engine.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LevelStreaming.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "UObject/StructOnScope.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/ChildActorComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "FoliageType.h"
#include "Model.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Components/BrushComponent.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavMesh/RecastNavMeshGenerator.h"
#include "NavModifierVolume.h"
#include "Lightmass/PrecomputedVisibilityVolume.h"
#include "Engine/CullDistanceVolume.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "BSPOps.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Internationalization/LocKeyFuncs.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "RHI.h"
#include "Misc/ConfigCacheIni.h"

namespace P4LevelContent
{
using P4MapKit::FDecodedValue;
const TCHAR* const GameBackedKey = TEXT("P4GameBackedLevelCustody");
// The flags a save keeps of an object (RF_Load, less the class default's). Each imported object has the game's own, as
// the game's editor held it after loading the file: what that editor did not make undoable stays so, and the reverse.
constexpr EObjectFlags SavedFlags = RF_Public | RF_Standalone | RF_Transactional | RF_ArchetypeObject |
    RF_DefaultSubObject | RF_TextExportTransient | RF_InheritableComponentTemplate | RF_DuplicateTransient |
    RF_NonPIEDuplicateTransient;
const TCHAR* const BaselineKey = TEXT("P4NativeLevelBaseline");
const TCHAR* const BaselineOwner = TEXT("p4mapkit-original-level-v1");
// What the game's files are converted with writes for a reference to a package the game's containers do
// not hold. In the game such a reference is to nothing.
const char* const Unshipped = "/Engine/UnknownPackage.UnknownExport";
// What the level said when it was imported, line by line, packed. An edit is told against it.
const TCHAR* const ImportedKey = TEXT("P4NativeLevelImported");
// What a lighting build gave the game's level: the level's lighting data, and for each component the
// ids of its levels of detail in it. Kept packed, for an actor carried into a level of a mod's own.
const TCHAR* const LightingKey = TEXT("P4NativeLevelLighting");
// The identity and order of the tags of the game's post-process components, never a value, bound to the
// source digest of the game's file. A cook of the level merges its recovered post-process settings in
// that order when it is not given the game's file.
const TCHAR* const PostProcessOrderKey = TEXT("P4NativeLevelPostProcessOrder");
// The actors of which a construction script run did not keep all the game's file says, one to a line.
const TCHAR* const ActorsNotKeptKey = TEXT("P4NativeLevelActorsNotKept");
// What a construction script run changed in what the level says, packed, each line with a mark before
// it: '-' what the game's file says and the run did not keep, '+' what the level says after the run
// and the game's file does not, 'g' what the game's own code made that the editor does not hold (the
// terrain tags of a collision component, the distance a cull distance volume gave a component).
const TCHAR* const RunLinesKey = TEXT("P4NativeLevelRunLines");
// The values of the game's file that this engine has no place for, one to a line, kept with the level.
const TCHAR* const NotHeldKey = TEXT("P4NativeLevelValuesNotHeld");
// The classes a level's objects are of that do not say what the game's files of the classes say, one
// package to a line. An actor of such a class cannot be carried into a mod: see FLentTemplates.
const TCHAR* const ClassesNotTheGamesKey = TEXT("P4NativeLevelClassesNotTheGames");

inline FString Text(const std::string& In)
{
    return FString(UTF8_TO_TCHAR(In.c_str()));
}

// One object of a level file.
struct FNode
{
    FString Name, Class, Path;
    int32 Outer = 0, ClassIndex = 0;
    // What the object is made from: the object of its class, or of its owner's class, whose values it
    // has wherever the file says none.
    int32 Template = 0;
    uint32 Flags = 0;
    FDecodedValue Values;
    P4MapKit::FPropertyInventory Properties;
    std::size_t TailStart = 0, TailEnd = 0;
    // Why the object's properties could not be read, when they could not.
    FString Undecoded;
    // Nothing follows the properties but the flag every object has (a class default object has not
    // even that).
    bool PlainTail = false;
    bool ParticleRequired = false;
    // What follows is read whole by its own reader (a vector field's source, the game's foliage grid
    // cells) and said in Native.
    bool NativeTail = false;
    // What follows is what a static mesh component holds for each level of detail: how many, and the
    // ids a lighting build gave them. -1 when it is not that.
    int32 Lods = -1;
    TArray<FString> LightingIds;
    // What follows is the engine's own to make, when this says what it is: a build's output that the
    // level only carries.
    FString EngineTail;
    // The id the object carries for the lazy references that name it (UObject's PossiblySerializeObjectGuid),
    // as 32 hexadecimal digits. Empty when it carries none.
    FString ObjectGuid;
    // A texture inside an actor whose native data is its cooked image (a landscape's height maps): the
    // import gives it its source image from that data.
    bool CookedTexture = false;
    // What follows is the native data of a skeleton, a skinned mesh or an animation sequence, read: what
    // it says, line by line, and what of it a cook works out for itself. SkinnedIssue says why it was
    // not read, when it is of such a class and was not.
    bool Skinned = false;
    bool Physics = false;
    P4MapKit::FPhysicsAssetNative PhysicsData;
    bool PhysicsBody = false;
    P4MapKit::FPhysicsBodyNative PhysicsBodyData;
    TArray<FString> Native, NativeMade;
    FString SkinnedIssue;
};
// The rows of a table, in the table's order: each row's name, and every member of the row.
struct FTableRows
{
    TArray<FString> Names;
    TArray<FDecodedValue> Rows;
    // Optional comparison identity; Rows remains the established importer tree.
    TArray<FDecodedValue> TypedComparisonRows;
};
// A level file read: every object with its properties decoded, and the native data of the level's own
// objects. Indices are the file's: 1 is its first object, 0 is none.
struct FFile
{
    std::vector<std::uint8_t> Header, Body;
    P4MapKit::FPackageInventory Package;
    FString PackageName;
    FString SourceRoot;
    // Development-only diagnostic staging can construct in a distinct unsaved package while
    // retaining the authoritative source package identity used by all readers/comparisons.
    FString ConstructionPackage;
    // Import identities verified against the original Zen package, for imports retoc could not name.
    std::map<std::int32_t, std::string> RecoveredImports;
    TArray<FNode> Nodes;
    int32 World = 0, Level = 0, Model = 0, Settings = 0, ScriptClass = 0, ScriptDefault = 0, ScriptActor = 0;
    // The level's actors besides its world settings and its level script, in the level's order.
    TArray<int32> Actors;
    // Explicit projection-only source omissions. Empty for every strict complete import.
    TSet<int32> ProjectionExcluded;
    bool UnsupportedScript = false;
    // The shapes of the level's volumes: the BSP model of each brush actor, by the model's index.
    TMap<int32, P4MapKit::FModelNative> Brushes;
    // The instances of each instanced mesh component, and for each foliage actor which component draws
    // which kind of foliage.
    TMap<int32, P4MapKit::FInstancedMeshNative> Instanced;
    TMap<int32, P4MapKit::FFoliageActorNative> Foliage;
    // The rows of each table the file holds, by the table's index.
    TMap<int32, FTableRows> Tables;
    P4MapKit::FWorldNative WorldNative;
    P4MapKit::FLevelNative LevelNative;
    P4MapKit::FModelNative ModelNative;
    P4MapKit::FGeneratedClassNative ClassNative;
    bool ProvenEmptyBeginPlay = false;
    TSet<int32> EmptyLevelFunctions;
    bool Export(int32 Index) const { return Index > 0 && Index <= Nodes.Num(); }
    const FNode& Node(int32 Index) const { return Nodes[Index - 1]; }
    FString Ref(int32 Index) const
    {
        if (Index == 0)
            return TEXT("none");
        if (Export(Index))
            return Node(Index).Path;
        std::string Path;
        return Package.Reference(Index, Path) ? Text(Path) : FString(TEXT("unreadable"));
    }
    // Whether an object sits somewhere inside another.
    bool Below(int32 Index, int32 Ancestor) const
    {
        for (int32 Guard = 0; Export(Index) && Guard < 64; ++Guard)
        {
            Index = Node(Index).Outer;
            if (Index == Ancestor)
                return true;
        }
        return false;
    }
    // The actor an object is or sits inside. 0 when it is in none.
    int32 ActorOf(int32 Index) const
    {
        for (int32 Guard = 0; Export(Index) && Guard < 64; ++Guard)
        {
            if (Actors.Contains(Index))
                return Index;
            Index = Node(Index).Outer;
        }
        return 0;
    }
    bool InActor(int32 Index) const { return ActorOf(Index) != 0; }
    // Whether an object is, or sits inside, an actor that a component of another actor makes (a child
    // actor). Such an actor says which component made it.
    bool InChildActor(int32 Index) const
    {
        const int32 Actor = ActorOf(Index);
        return Actor != 0 && Node(Actor).Values.Field("ParentComponent") != nullptr;
    }
};

// How a line says what an object is made from.
const TCHAR* const MadeFrom = TEXT("made from ");

struct FBaseline
{
    FString Package, Source, State;
    // An asset kept so that others can refer to it, without the native data the game's file has:
    // how many bytes of that there are. Such an asset is never to go into a mod.
    int32 ReferenceOnlyBytes = 0;
    // And how many values of it this engine has no place for.
    int32 ReferenceOnlyValues = 0;
};

// Sets values as the game's file says them, on top of what the object has from its class: a struct
// keeps the members the file does not say, an array is what the file says it is.
struct FWriter : FP4DecodedPropertyWriter
{
    FWriter(const FFile& File, const TMap<int32, UObject*>& Made, bool IsFinal)
        : FP4DecodedPropertyWriter(
              [&File, &Made](const FString& Path) -> UObject*
              {
                  for (const auto& Pair : Made)
                      if (P4SameText(File.Node(Pair.Key).Path, Path))
                          return Pair.Value;
                  return nullptr;
              },
              IsFinal)
    {}
};

inline bool Digest(const TArray<FString>& Content, FString& Out)
{
    return P4HashUtf8(FString::Join(Content, TEXT("\n")), Out);
}

// Class templates of the project given, for a while, what the game's files of the classes say, and
// their own values back when the lending ends. The functions are in P4LevelActorExport.cpp.
struct FLentTemplates : public FGCObject
{
    struct FOne
    {
        TWeakObjectPtr<UObject> Object;
        void* Before = nullptr;
    };
    TArray<FOne> Kept;
    TArray<FString> Classes, Problems;
    // What the kept values name. A kept copy is plain memory the garbage collector does not walk, and a
    // level save collects garbage while the game's values are lent: an object only the copy names (an
    // instanced subobject such as a CakEventHandler template's Unit_N, or an entry of a transient list)
    // would be freed then, and giving the copy back would write a pointer to freed memory into the
    // template. So everything the object names when it is kept is held until the lending ends.
    TArray<UObject*> Held;

    FLentTemplates() = default;
    FLentTemplates(const FLentTemplates&) = delete;
    FLentTemplates& operator=(const FLentTemplates&) = delete;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObjects(Held); }
    virtual FString GetReferencerName() const override { return TEXT("P4 lent class templates"); }

    void* Keep(UObject* Object);
    static void GiveBack(FOne& One);
    // Gives the class in a package what the game's file of the class says. Root is the folder the game's
    // /Game/ packages are under.
    void Lend(const FString& Package, const FString& Root, const FP4KadanSourceBinding* ProvenSource = nullptr);
    void Return();
    ~FLentTemplates() { Return(); }
};

// P4LevelContentApi.cpp
FString Finish(const TSharedRef<FJsonObject>& Json, const FString& Error);

// P4LevelFileRead.cpp
bool ReadClassComponentTail(FNode& Node, const UStruct* Schema, const std::vector<uint8>& Body);
bool ReadNodes(FFile& File, const FString& PackageName, FString& Error);
bool ReadTables(FFile& File, bool Refuse, FString& Error, bool TypedValues = false);
bool Read(FFile& File, const FString& PackageName, FString& Error, bool AllowUnsupportedScript = false);
FName VariableKind(const std::string& Type);
FString Refusal(const FFile& File);

// P4LevelLines.cpp
bool CallsItself(const FDecodedValue& Value, const FString& Path);
void Lines(const FFile& File, TArray<FString>& Out, TArray<FString>& EngineMade, const TSet<int32>* Included = nullptr,
    bool FoldNames = false);

// P4LevelCook.cpp
bool Cooked(UObject* World, bool Map, FFile& Out, FString& Error, const FString& KeepAt = FString(),
    const FFile* SourceOrder = nullptr);
TArray<FString> OriginalLightingLines(const FFile& Game);
FString Pack(const TArray<FString>& Content);
bool Unpack(const FString& Text, TArray<FString>& Content);

// P4LevelEditorFixups.cpp
void WithoutEditorNavigation(UWorld* World, TArray<FString>& Content, TArray<FString>* Made = nullptr);
void WithEditorDroppedStreams(UWorld* World, TArray<FString>& Content, TArray<FString>* Dropped = nullptr);
void WithEditorNavLinkIds(UWorld* World, TArray<FString>& Content);
void WithEditorLandscapeMaterials(UWorld* World, TArray<FString>& Content);
void WithoutAddedNativeTransformTracking(UWorld* World, TArray<FString>& Content);
void WithInheritedAnimationClasses(UWorld* World, TArray<FString>& Content);
void WithInheritedRuleTableDefaults(UWorld* World, TArray<FString>& Content);

// P4LevelBaseline.cpp
FString State(UWorld* World);
FString BaselineText(const FBaseline& Value);
bool ReadBaseline(UWorld* World, FBaseline& Out);
bool ReadVisibilityBaseline(UWorld* World, TArray<FString>& Lines);
bool KeptPostProcessOrder(UObject* Object, FString& Json, FString& Source, FString& Error);
bool HashGameBackedSourceFiles(const FString& StoredHeader, const FString& StoredBody, FString& Digest);
bool ReadGameBackedCustody(UWorld* World, TSet<FName>& Omitted, FString& Source, FString& Error,
    const FP4OriginalWorldSource* SelectedSource = nullptr);
bool RetargetGameBackedSource(UWorld* World, const FP4OriginalWorldSource& Selected, FString& Error);
FString Status(UWorld* World);
FString OwnerOf(const FString& Line, const FString& Level);
FP4LevelChangesResult AnalyzeChanges(UWorld* World);
FString Changes(UWorld* World);

// P4LevelActorExport.cpp
FString NavigationCollision(AActor* Actor, const FString& File);
FString EraseFoliage(UWorld* World, const FVector& Center, float Radius);
FString AddFoliage(UWorld* World, UInstancedStaticMeshComponent* Kind, const FVector& Location,
    const FRotator& Rotation, const FVector& Scale);

// P4LevelApplyValues.cpp
KADAN_API UObject* ResolveOriginalExternalTemplate(const FString& Reference);
UObject* DeclaredTemplate(
    const FFile& File, int32 Index, const TMap<int32, UObject*>& Made, UObject* Fallback, FString& Error);
void Reset(const FProperty* Property, void* Live, const void* Archetype, int32 Depth = 0,
    UObject* InstanceOwner = nullptr, const UObject* TemplateOwner = nullptr);
bool Apply(const FFile& File, int32 Index, UObject* Live, const TMap<int32, UObject*>& Made, bool Final, FString& Error,
    TArray<FString>* NotHeld = nullptr, TArray<FString>* NotShipped = nullptr, bool MissingAsNotHeld = false);

// A delegate's bindings as a file says them, each binding's object found by its path.
bool GiveDelegate(const FProperty* Property, UObject* Live, int32 Place, const FDecodedValue& Said,
    TFunctionRef<UObject*(const FString&)> Resolve);
bool PutValue(UObject* Target, const FProperty* Property, int32 Place, const FDecodedValue& Value, const FString& Where,
    FString& Error);
bool SameInEffect(const FString& Root, FString From, const FString& Wanted, TMap<FString, FString>& Known);
bool P4SameDecodedValue(const FDecodedValue& A, const FDecodedValue& B);
void RedundantUnshippedNulls(
    FFile& Compared, const FFile& Mine, const FString& Root, TArray<FString>& NotShipped, UObject* LiveOwner = nullptr);
void Without(FFile& File, const TArray<FString>& NotHeld);

// P4LevelBuild.cpp
void Discard(UWorld* World);
bool ExistingStorageSubobject(UObject* Object, UObject* Outer, UClass* Declared, const FString& Name,
    uint32 SourceFlags, UObject* SourceTemplate);
UWorld* Build(const FFile& File, TArray<FString>& NotHeld, TArray<FString>& NotShipped, TArray<FString>& NotInGame,
    FString& Error);

// P4OriginalClassCompare.cpp
bool ReadClassSource(const FString& Package, const FString& Root, FP4KadanSourceBinding& Source, FString& Error);
TArray<FString> ClassesThatDiffer(const FFile& File, const FString& Root, int32& Asked,
    TArray<FString>* Differing = nullptr, TSet<FName>* Unproven = nullptr,
    TMap<FString, FP4KadanSourceBinding>* Sources = nullptr);
void Unheld(const UStruct* Type, FDecodedValue& Value, int32& Count, int32 Depth = 0,
    TArray<FP4OriginalClassValueNotHeld>* Details = nullptr, const FString& Object = FString(),
    const FString& Prefix = FString(), const FP4RecoveredPostProcessStorage* LogicalSettings = nullptr,
    FString* StorageError = nullptr, const UObject* StockOwner = nullptr);
FP4OriginalClassComparisonResult CompareClassNative(const FString& HeaderFile, const FString& BodyFile,
    const FString& OwnerPackage = FString(), const TArray<uint8>* SourceHeader = nullptr,
    const TArray<uint8>* SourceBody = nullptr);

// P4LevelImport.cpp
TArray<FP4OriginalValueNote> ValueNotes(const TArray<FString>& NotHeld, const TArray<FString>& NotShipped);
FP4OriginalLevelImportResult ImportNative(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk, bool OwnsReimportSlot = false);

// P4GameBackedLevelImport.cpp
FP4OriginalLevelImportResult ImportGameBacked(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk,
    const TSet<FName>& Unavailable, const FString& Reason, bool TryActors,
    const FString& DiagnosticDestination = FString(), const TSet<FString>& OmitActors = TSet<FString>(),
    bool ReimportSlot = false);
} // namespace P4LevelContent

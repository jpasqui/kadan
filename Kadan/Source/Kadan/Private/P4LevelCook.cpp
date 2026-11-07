// A level or an asset saved the way a cook saves it and read back, and the packed line records kept
// with an imported level.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4LightCookDefaults.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "P4MapKitCore/export_package.h"
#include "P4MapKitCore/cooked_name_sort.h"
#include "P4OriginalNavigationMesh.h"

namespace P4LevelContent
{
struct FIntrinsicCookSlots
{
    FString Class, Property;
    int32 Count = 0;
    bool DefaultEmpty = false;
    // Whether the object's template has nothing in the array but this editor's own data either.
    bool TemplateEmpty = false;
    TArray<int32> Slots;
};

bool IntrinsicUserData(const UObject* Data, const UObject* Owner)
{
    return IsValid(Data) &&
        (Data->GetClass() == UP4CreatorSourceData::StaticClass() ||
            Data->GetClass() == UP4StockEngineFields::StaticClass()) &&
        Data->GetOuter() == Owner && Data->IsEditorOnly();
}

TMap<FString, FIntrinsicCookSlots> CaptureIntrinsicCookSlots(UPackage* Package)
{
    TMap<FString, FIntrinsicCookSlots> Captured;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Owner : Objects)
    {
        if (!IsValid(Owner) || !Cast<IInterface_AssetUserData>(Owner))
            continue;
        const FArrayProperty* Array = FindFProperty<FArrayProperty>(Owner->GetClass(), TEXT("AssetUserData"));
        const FObjectPropertyBase* Object = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
        if (!Array || !Object || Array->ArrayDim != 1)
            continue;
        FScriptArrayHelper Values(Array, Array->ContainerPtrToValuePtr<void>(Owner));
        FIntrinsicCookSlots Snapshot;
        Snapshot.Class = Owner->GetClass()->GetPathName();
        Snapshot.Property = Array->GetName();
        Snapshot.Count = Values.Num();
        for (int32 Slot = 0; Slot < Values.Num(); ++Slot)
            if (IntrinsicUserData(Object->GetObjectPropertyValue(Values.GetRawPtr(Slot)), Owner))
                Snapshot.Slots.Add(Slot);
        if (Snapshot.Slots.Num() == 0)
            continue;
        if (UObject* Default = Owner->GetClass()->GetDefaultObject(false))
            Snapshot.DefaultEmpty = FScriptArrayHelper(Array, Array->ContainerPtrToValuePtr<void>(Default)).Num() == 0;
        if (UObject* Template = Owner->GetArchetype())
        {
            FScriptArrayHelper Kept(Array, Array->ContainerPtrToValuePtr<void>(Template));
            Snapshot.TemplateEmpty = true;
            for (int32 Slot = 0; Slot < Kept.Num(); ++Slot)
                Snapshot.TemplateEmpty &= IntrinsicUserData(Object->GetObjectPropertyValue(Kept.GetRawPtr(Slot)), Template);
        }
        // Package export paths are built from actual outer identities, with a dot between
        // each object. This is also correct for subobjects whose UI path uses a colon.
        FString Path;
        for (UObject* Part = Owner; Part && Part != Package; Part = Part->GetOuter())
            Path = Part->GetName() + (Path.IsEmpty() ? FString() : TEXT(".") + Path);
        Captured.Add(Path, MoveTemp(Snapshot));
    }
    return Captured;
}

// The save strips this editor's own data from an object's user data and writes its place as null. Where that was all
// the array held and the template holds nothing else either, the game's file has no array there: the cook leaves it
// out of the written package too.
bool StripIntrinsicCookSlots(std::vector<uint8>& Header, std::vector<uint8>& Body,
    const TMap<FString, FIntrinsicCookSlots>& Captured, FString& Error)
{
    if (!Captured.Num())
        return true;
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = Text(Native.Error.Code);
        return false;
    }
    std::map<std::size_t, std::vector<uint8>> Replacements;
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
    {
        std::string Path;
        const FIntrinsicCookSlots* Snapshot =
            Native.Reference(int32(At + 1), Path) ? Captured.Find(Text(Path)) : nullptr;
        if (!Snapshot || !Snapshot->TemplateEmpty || Snapshot->Slots.Num() != Snapshot->Count)
            continue;
        const auto& Export = Native.Exports[At];
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(Body, Native.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Tags.Error.Code.empty())
            continue;
        for (const auto& Tag : Tags.Properties)
        {
            if (Text(Tag.Name) != Snapshot->Property || Tag.Type != "ArrayProperty" ||
                Tag.Size != std::size_t(4 + 4 * Snapshot->Count))
                continue;
            bool Null = std::int32_t(Body[Tag.PayloadOffset] | Body[Tag.PayloadOffset + 1] << 8 |
                            Body[Tag.PayloadOffset + 2] << 16 | Body[Tag.PayloadOffset + 3] << 24) == Snapshot->Count;
            for (std::size_t Byte = 4; Null && Byte < Tag.Size; ++Byte)
                Null = Body[Tag.PayloadOffset + Byte] == 0;
            if (!Null)
                break;
            std::vector<uint8> Without(Body.begin() + Export.Offset, Body.begin() + Tag.TagOffset);
            Without.insert(
                Without.end(), Body.begin() + Tag.PayloadOffset + Tag.Size, Body.begin() + Export.Offset + Export.Size);
            Replacements.emplace(At, std::move(Without));
            break;
        }
    }
    if (Replacements.empty())
        return true;
    if (!P4ValidateStockEngineCookRelocation(Header, Body, Error))
        return false;
    P4MapKit::Export::RewrittenPackage Candidate;
    std::string Why;
    if (!P4MapKit::Export::RebuildCookedExports(Header, Body, Native.Names, Replacements, Candidate, Why))
    {
        Error = TEXT("intrinsic-cook-user-data-not-left-out: ") + Text(Why);
        return false;
    }
    Header = std::move(Candidate.Header);
    Body = std::move(Candidate.Body);
    return true;
}

bool ExcludeIntrinsicCookSlots(FFile& File, const TMap<FString, FIntrinsicCookSlots>& Captured, FString& Error)
{
    for (FNode& Node : File.Nodes)
    {
        const FIntrinsicCookSlots* Snapshot = Captured.Find(Node.Path);
        if (!Snapshot || Node.Class != Snapshot->Class)
            continue;
        auto Field = std::find_if(Node.Values.Fields.begin(), Node.Values.Fields.end(),
            [&](const std::pair<std::string, FDecodedValue>& Value)
            { return Text(Value.first) == Snapshot->Property; });
        // Some native properties are editor-only themselves, so no runtime array is written.
        if (Field == Node.Values.Fields.end())
            continue;
        if (Field->second.Kind != FDecodedValue::EKind::Array ||
            Field->second.Items.size() != std::size_t(Snapshot->Count))
        {
            Error = TEXT("intrinsic-cook-user-data-array-changed: ") + Node.Path;
            return false;
        }
        for (const int32 Slot : Snapshot->Slots)
            if (Slot < 0 || Slot >= Snapshot->Count ||
                Field->second.Items[std::size_t(Slot)].Kind != FDecodedValue::EKind::Null)
            {
                Error = TEXT("intrinsic-cook-user-data-slot-not-stripped: ") + Node.Path;
                return false;
            }
        // The save strips the known object and writes its pointer as null without compacting
        // the array. Remove only those exact, pre-cook proven slots from this parsed receipt.
        // Every unrelated null, object and array position stays meaningful. No live data changes.
        for (int32 At = Snapshot->Slots.Num(); At-- > 0;)
            Field->second.Items.erase(Field->second.Items.begin() + Snapshot->Slots[At]);
        if (Field->second.Items.empty() && Snapshot->DefaultEmpty)
            Node.Values.Fields.erase(Field);
    }
    return true;
}

// A level or an asset saved the way a cook saves it, read back, with the files thrown away. KeepAt,
// when given, is where a copy of the two files is kept: a path without its ending.
bool Cooked(UObject* World, bool Map, FFile& Out, FString& Error, const FString& KeepAt, const FFile* SourceOrder)
{
    FP4OriginalComponentReferenceScope ComponentReferences(Map ? Cast<UWorld>(World) : nullptr);
    const TCHAR* const Head = Map ? TEXT(".umap") : TEXT(".uasset");
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!Target || !GEditor)
    {
        Error = TEXT("level-compare-no-cook-platform");
        return false;
    }
    // Material serialization asks for each cook shader platform's quality settings. Create them
    // before SavePackage enters its serialization scope, where finding or creating UObjects is illegal.
    TArray<FName> ShaderFormats;
    Target->GetAllTargetedShaderFormats(ShaderFormats);
    for (const FName Format : ShaderFormats)
        UMaterialShaderQualitySettings::Get()->GetShaderPlatformQualitySettings(Format);
    UPackage* Package = World->GetOutermost();
    P4RefreshRecoveredFoliageDefaults();
    P4RefreshRecoveredParticleSystemDefaults();
    const TMap<FString, FIntrinsicCookSlots> IntrinsicSlots = !Target->HasEditorOnlyData()
        ? CaptureIntrinsicCookSlots(Package)
        : TMap<FString, FIntrinsicCookSlots>();
    // One save of the package the way a cook saves it, with the cook's own translations after it.
    const auto CookOnce = [&](std::vector<uint8>& OutHeader, std::vector<uint8>& OutBody) -> bool
    {
        FP4DiagnosticCookPackageGuard Source(Package);
        if (!Source.IsReady())
        {
            Error = Source.GetErrorDetail();
            return false;
        }
        const FString Base = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/LevelCompare")) /
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        // A cook marks a package as one without editor data before it saves it, and the save leaves that out.
        if (!Target->HasEditorOnlyData())
            Package->SetPackageFlags(PKG_FilterEditorOnly);
        // A cook saves a level from its world, an asset from the asset, and a Blueprint's package (whose
        // asset is the editor's alone) from everything public in it.
        UObject* From = World->IsA<UBlueprint>() ? nullptr : World;
        // The comparison reads what the package says, not the shaders a cook puts in a material, which are the
        // cook platform's and never the game's. Saving a material for a cook compiles its shaders for every shader
        // format the Windows target lists, which held an import minutes on each material. That list is empty while
        // this package is saved and given back at once, and the materials' cook caches are cleared before and after,
        // so a real cook later (a mod's export) still compiles and keeps their shaders.
        const TCHAR* WindowsTarget = TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings");
        TArray<FString> TargetedRHIs;
        GConfig->GetArray(WindowsTarget, TEXT("TargetedRHIs"), TargetedRHIs, GEngineIni);
        const auto ClearMaterialCooks = [Package, Target]
        {
            TArray<UObject*> Inside;
            GetObjectsWithOuter(Package, Inside, true);
            for (UObject* Object : Inside)
                if (UMaterialInterface* Material = Cast<UMaterialInterface>(Object))
                    Material->ClearCachedCookedPlatformData(Target);
        };
        ClearMaterialCooks();
        // The editor saves a world that has no physics scene by taking its components out, making a scene, and
        // registering them again with every actor's construction script run (UEditorEngine's
        // InitializePhysicsSceneForSaveIfNecessary, and the same again after the save). A level made by an
        // import is such a world, and the save would then hold what that run makes, not the level made: a
        // component the game's construction script made is gone, and a child actor is made again from its
        // template. A scene is made here instead, for the save only, with the components as they are.
        // PreSaveRoot's registration branch is suppressed for this synchronous serialization;
        // the original registration state is restored afterwards.
        UWorld* const SavedWorld = Map ? Cast<UWorld>(World) : nullptr;
        ULevel* const SavedLevel = SavedWorld ? SavedWorld->PersistentLevel : nullptr;
        const bool NoScene = SavedLevel && !SavedWorld->GetPhysicsScene() &&
            !(SavedLevel->OwningWorld && SavedLevel->OwningWorld->GetPhysicsScene());
        if (NoScene)
            SavedWorld->CreatePhysicsScene();
        const bool Unregistered = SavedLevel && !SavedLevel->bAreComponentsCurrentlyRegistered;
        // PreSaveRoot uses this runtime flag solely to decide whether to register
        // and later clear the level. Even UpdateLevelComponents(false) runs native
        // OnRegister side effects; ClearLevelComponents destroys child actors.
        // Serialization needs the existing objects, not a registration round trip.
        if (Unregistered)
            SavedLevel->bAreComponentsCurrentlyRegistered = true;
        ON_SCOPE_EXIT
        {
            if (Unregistered)
                SavedLevel->bAreComponentsCurrentlyRegistered = false;
            if (NoScene)
                SavedWorld->SetPhysicsScene(nullptr);
        };
        GConfig->SetArray(WindowsTarget, TEXT("TargetedRHIs"), TArray<FString>(), GEngineIni);
        bool Saved = false;
        {
            // The level's lights keep their inverse-square falloff, which the game's defaults would otherwise turn off.
            FP4LightCookDefaults LightDefaults(Package);
            ON_SCOPE_EXIT
            {
                GConfig->SetArray(WindowsTarget, TEXT("TargetedRHIs"), TargetedRHIs, GEngineIni);
            };
            // A cook runs as a commandlet, where the editor's save of a world does not sort the level's actors (nor
            // shrink its model and collect garbage first); the game's files keep their actors in the order their
            // editor had them. The editor's save does the same for an autosave. The places removed actors left empty
            // are taken out, as that sort would.
            if (SavedLevel)
                SavedLevel->Actors.RemoveAll([](const AActor* Actor) { return !Actor || Actor->IsPendingKill(); });
            Saved = GEditor->SavePackage(Package, From, From ? RF_Standalone : RF_Public, *(Base + Head), GError, nullptr,
                false, true,
                SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_NoError | SAVE_Async |
                    (Map ? SAVE_FromAutosave : SAVE_None),
                Target, FDateTime::MinValue(), false);
        }
        ClearMaterialCooks();
        // A cook writes a package as two files, the header and the objects, and it writes them from another
        // thread.
        const bool Restored = Source.AwaitAndRestore(Error);
        TArray<uint8> Header, Body;
        const bool Split = IFileManager::Get().FileExists(*(Base + TEXT(".uexp")));
        const bool Loaded = Saved && Split && FFileHelper::LoadFileToArray(Header, *(Base + Head), FILEREAD_Silent) &&
            FFileHelper::LoadFileToArray(Body, *(Base + TEXT(".uexp")), FILEREAD_Silent);
        // The files are thrown away. They are left where they are when the environment asks for it, to look at.
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_KEEP_LEVEL_COMPARE")).IsEmpty())
            for (const TCHAR* Suffix : {Head, TEXT(".uexp"), TEXT(".ubulk")})
                IFileManager::Get().Delete(*(Base + Suffix), false, true, true);
        if (!Restored)
            return false;
        if (!Loaded)
        {
            Error = Saved && !Split ? TEXT("level-compare-project-does-not-cook-split-files")
                                    : TEXT("level-compare-cook-failed");
            return false;
        }
        std::vector<uint8> TranslatedHeader(Header.GetData(), Header.GetData() + Header.Num()),
            TranslatedBody(Body.GetData(), Body.GetData() + Body.Num());
        if (!P4TranslateRecoveredFoliageCook(TranslatedHeader, TranslatedBody, Error))
            return false;
        if (!P4TranslateRecoveredLightCook(TranslatedHeader, TranslatedBody, Error))
            return false;
        if (!P4TranslateRecoveredParticleCook(TranslatedHeader, TranslatedBody, Error))
            return false;
        bool NeedsPostProcessMerge = false;
        if (!P4RecoveredPostProcessCookHasFork(TranslatedHeader, TranslatedBody, NeedsPostProcessMerge, Error))
            return false;
        // Without the game's file, a level's merge takes the order its import kept.
        const bool UseKept = NeedsPostProcessMerge && !SourceOrder && Map;
        FString KeptOrder, KeptSource;
        if (UseKept && !KeptPostProcessOrder(World, KeptOrder, KeptSource, Error))
            return false;
        bool RelocationSafe = false;
        if (NeedsPostProcessMerge && SourceOrder && !Map && World->IsA<UBlueprint>())
        {
            // Changing a tagged struct's byte length moves following exports. Only a
            // fully covered class closure is admitted: ordinary decoded UObject tags
            // with a plain identity tail, or completely decoded UClass/UFunction code.
            FFile NativeLayout;
            NativeLayout.Header = TranslatedHeader;
            NativeLayout.Body = TranslatedBody;
            if (!ReadNodes(NativeLayout, Package->GetName(), Error))
                return false;
            TSet<int32> Code;
            try
            {
                const auto Structure = P4MapKit::Blueprint::ReadClassStructure(NativeLayout.Package, NativeLayout.Body);
                if (Structure["kind"].S() != "Blueprint")
                {
                    Error = TEXT("postprocess-cook-relocation-requires-covered-Blueprint");
                    return false;
                }
                Code.Add(int32(Structure["class"]["index"].U()));
                for (const auto& Name : Structure["functions"].Keys())
                    Code.Add(int32(Structure["functions"][Name]["index"].U()));
            }
            catch (const std::exception& Why)
            {
                Error = TEXT("postprocess-cook-native-code-relocation-unqualified: ") + Text(Why.what());
                return false;
            }
            for (int32 Index = 0; Index < NativeLayout.Nodes.Num(); ++Index)
            {
                const FNode& Node = NativeLayout.Nodes[Index];
                const bool CompleteCode = Code.Contains(Index + 1) &&
                    (Node.Class == TEXT("/Script/Engine.BlueprintGeneratedClass") ||
                        Node.Class == TEXT("/Script/CoreUObject.Function"));
                // Qualification is separate from decoded engine/build payloads: only
                // exact stock GUID-only or empty-LOD forms contain no relocatable offsets.
                const bool CompleteFixedComponent = !Node.PlainTail && !CompleteCode &&
                    P4MapKit::ReadPointerFreeComponentTail(
                        TCHAR_TO_UTF8(*Node.Class), NativeLayout.Body, Node.TailStart, Node.TailEnd)
                        .Error.Code.empty();
                if (!Node.Undecoded.IsEmpty() || (!Node.PlainTail && !CompleteCode && !CompleteFixedComponent))
                {
                    Error = TEXT("postprocess-cook-native-tail-relocation-unqualified: ") + Node.Path;
                    return false;
                }
            }
            RelocationSafe = true;
        }
        else if (NeedsPostProcessMerge && (SourceOrder || UseKept) && Map)
        {
            // A level: the merged settings change the byte length of the post-process component, which
            // moves the objects after it in the file. Each object that moves has to hold nothing that says
            // where in the file it is: only the flag every object has, a lighting build's id, a navigation
            // build's mesh (its size is told relative to itself), the levels of detail of a static mesh
            // component, or the world's and the level's lists of objects. Anything else (cooked collision,
            // a brush's model, instances, foliage) refuses the merge, as before.
            FFile NativeLayout;
            NativeLayout.Header = TranslatedHeader;
            NativeLayout.Body = TranslatedBody;
            if (!Read(NativeLayout, Package->GetName(), Error))
                return false;
            std::size_t First = SIZE_MAX;
            for (int32 Index = 1; Index <= NativeLayout.Nodes.Num(); ++Index)
            {
                const FNode& Node = NativeLayout.Nodes[Index - 1];
                if (!P4SameText(Node.Class, TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent")))
                    continue;
                if (!Node.Undecoded.IsEmpty() || !Node.PlainTail)
                {
                    Error = TEXT("postprocess-cook-native-tail-relocation-unqualified: ") + Node.Path;
                    return false;
                }
                First = FMath::Min(First, std::size_t(NativeLayout.Package.Exports[Index - 1].Offset));
            }
            for (int32 Index = 1; Index <= NativeLayout.Nodes.Num(); ++Index)
            {
                const FNode& Node = NativeLayout.Nodes[Index - 1];
                if (std::size_t(NativeLayout.Package.Exports[Index - 1].Offset) <= First)
                    continue;
                const bool Moves = Node.Undecoded.IsEmpty() &&
                    (Node.PlainTail || Index == NativeLayout.World || Index == NativeLayout.Level ||
                        Node.EngineTail == TEXT("the id of a lighting build") ||
                        Node.EngineTail == TEXT("the navigation mesh of a navigation build") ||
                        (Node.Lods >= 0 && !NativeLayout.Instanced.Contains(Index)));
                if (!Moves)
                {
                    Error = TEXT("postprocess-cook-native-tail-relocation-unqualified: ") + Node.Path;
                    return false;
                }
            }
            RelocationSafe = true;
        }
        if (UseKept ? !P4TranslateRecoveredPostProcessCookWithOrder(
                          TranslatedHeader, TranslatedBody, KeptOrder, KeptSource, RelocationSafe, Error)
                    : !P4TranslateRecoveredPostProcessCook(TranslatedHeader, TranslatedBody,
                          SourceOrder ? &SourceOrder->Header : nullptr, SourceOrder ? &SourceOrder->Body : nullptr,
                          RelocationSafe, Error))
            return false;
        // The editor cooks collision for Windows; the game reads the same bytes under its own format name.
        {
            P4MapKit::Export::RewrittenPackage Relabelled;
            std::string Why;
            if (!P4MapKit::Export::RelabelPhysXCook(TranslatedHeader, TranslatedBody, Relabelled, Why))
            {
                Error = TEXT("level-cook-collision-format-not-relabelled: ") + Text(Why);
                return false;
            }
            TranslatedHeader = std::move(Relabelled.Header);
        }
        // A relabel (the merge's, the collision format's) writes the names again and so changes the header's size,
        // which the collision payloads' places in the file count in.
        if (!P4PlacePackageInlinePhysicsBulk(TranslatedHeader, TranslatedBody, Error))
            return false;
        // A cook saves every navigation tile a level's meshes hold; the editor's save, only those its navigation bounds
        // cover.
        if (Map && !P4CookNavigationMeshesAsCommandlet(Package, TranslatedHeader, TranslatedBody, Error))
            return false;
        if (!P4TranslateStockEngineFieldCook(Package, TranslatedHeader, TranslatedBody, Error))
            return false;
        if (!P4TranslateGameStockTagCook(Package, TranslatedHeader, TranslatedBody, Error))
            return false;
        if (!P4TranslateGameNativeTailCook(Package, TranslatedHeader, TranslatedBody, Error))
            return false;
        if (Map && !StripIntrinsicCookSlots(TranslatedHeader, TranslatedBody, IntrinsicSlots, Error))
            return false;
        OutHeader = std::move(TranslatedHeader);
        OutBody = std::move(TranslatedBody);
        return true;
    };
    std::vector<uint8> TranslatedHeader, TranslatedBody;
    if (!CookOnce(TranslatedHeader, TranslatedBody))
        return false;
    // The save writes the name table right after its summary and every name in its own order. A translation that adds
    // a name writes the table again after the header and the name after the others (cooked_name_sort.h). The table is
    // put back in its place, and the level is saved once more with one object renamed to a name that comes before every
    // other: the two saves give where each name is referred to, and the names are put in the save's order. The object
    // renamed is the only one of its class in the package, so that the save keeps its objects in the same order. Where
    // that cannot be told exactly, the package keeps the names as the translations left them.
    const auto Place = [](std::vector<uint8>& PlacedHeader, const std::vector<uint8>& PlacedBody, FString& Why)
    {
        P4MapKit::Export::RewrittenPackage Placed;
        std::string Problem;
        if (!P4MapKit::Export::PlaceNamesAfterSummary(PlacedHeader, PlacedBody, Placed, Problem))
        {
            Why = Text(Problem);
            return false;
        }
        PlacedHeader = std::move(Placed.Header);
        return true;
    };
    FString Why;
    if (Map && Place(TranslatedHeader, TranslatedBody, Why) && !P4MapKit::Export::CookedNamesSorted(TranslatedHeader))
    {
        UWorld* Level = Cast<UWorld>(World);
        TArray<UObject*> Inside;
        GetObjectsWithOuter(Package, Inside, true);
        const auto Alone = [&Inside](const UObject* Object)
        {
            int32 Same = 0;
            for (const UObject* Other : Inside)
                Same += Other->GetClass() == Object->GetClass();
            return Same == 1;
        };
        UObject* Renamed = nullptr;
        if (Level && Level->PersistentLevel)
            for (UObject* Candidate : {static_cast<UObject*>(Level->PersistentLevel->Model),
                     static_cast<UObject*>(Level->PersistentLevel->GetWorldSettings(false))})
                if (!Renamed && IsValid(Candidate) && Alone(Candidate))
                    Renamed = Candidate;
        const TCHAR* const Probe = TEXT("*P4NameProbe");
        if (!Renamed || StaticFindObjectFast(nullptr, Renamed->GetOuter(), FName(Probe)))
            Why = TEXT("no object of a class of its own to rename");
        else
        {
            const FString Was = Renamed->GetName();
            const ERenameFlags Quietly =
                REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders | REN_DoNotDirty;
            std::vector<uint8> ProbeHeader, ProbeBody;
            bool Probed = false;
            if (Renamed->Rename(Probe, nullptr, Quietly))
            {
                Probed = CookOnce(ProbeHeader, ProbeBody);
                Renamed->Rename(*Was, nullptr, Quietly);
            }
            if (!Probed)
                Why = Error.IsEmpty() ? FString(TEXT("the probe's object was not renamed")) : Error;
            else if (Place(ProbeHeader, ProbeBody, Why))
            {
                P4MapKit::Export::RewrittenPackage Sorted;
                std::string Problem;
                if (P4MapKit::Export::SortCookedNames(TranslatedHeader, TranslatedBody, ProbeHeader, ProbeBody,
                        TCHAR_TO_UTF8(Probe), Sorted, Problem))
                {
                    TranslatedHeader = std::move(Sorted.Header);
                    TranslatedBody = std::move(Sorted.Body);
                }
                else
                    Why = Text(Problem);
            }
            Error.Reset();
        }
    }
    if (!Why.IsEmpty())
        UE_LOG(LogTemp, Display, TEXT("%s keeps names in the order its translations left them: %s"), *Package->GetName(),
            *Why);
    // Placing and sorting the names changes the header's size too.
    if (!P4PlacePackageInlinePhysicsBulk(TranslatedHeader, TranslatedBody, Error))
        return false;
    TArray<uint8> Header, Body;
    Header.Reset();
    Header.Append(TranslatedHeader.data(), int32(TranslatedHeader.size()));
    Body.Reset();
    Body.Append(TranslatedBody.data(), int32(TranslatedBody.size()));
    if (!KeepAt.IsEmpty() &&
        (!FFileHelper::SaveArrayToFile(Header, *(KeepAt + Head)) ||
            !FFileHelper::SaveArrayToFile(Body, *(KeepAt + TEXT(".uexp")))))
    {
        Error = TEXT("level-compare-copy-not-written");
        return false;
    }
    Out.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
    Out.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    const bool Parsed = Map ? Read(Out, Package->GetName(), Error)
                            : ReadNodes(Out, Package->GetName(), Error) && ReadTables(Out, false, Error);
    return Parsed && ExcludeIntrinsicCookSlots(Out, IntrinsicSlots, Error);
}

TArray<FString> OriginalLightingLines(const FFile& Game)
{
    TArray<FString> Lighting;
    const FDecodedValue* Registry = Game.Node(Game.Level).Values.Field("MapBuildData");
    Lighting.Add(TEXT("registry\t") +
        (Registry && Registry->Kind == FDecodedValue::EKind::String ? Text(Registry->Text) : FString()));
    for (const FNode& Node : Game.Nodes)
        if (Node.LightingIds.Num())
            Lighting.Add(Node.Path + TEXT("\t") + FString::Join(Node.LightingIds, TEXT(",")));
    for (const FNode& Node : Game.Nodes)
    {
        // Preserve only an actual complete serialized Engine Guid, exactly as in
        // strict import. Omitted actors remain original game content; this record
        // supplies source lighting identities, never editable actor admission.
        const FDecodedValue* Value = Node.Values.Field("LightGuid");
        if (!Value || Value->Kind != FDecodedValue::EKind::Object || Value->Fields.size() != 4)
            continue;
        uint32 Words[4];
        int32 Count = 0;
        for (const char* Name : {"A", "B", "C", "D"})
        {
            const FDecodedValue* Part = Value->Field(Name);
            if (!Part || Part->Kind != FDecodedValue::EKind::Number || Part->Text.empty() || Part->Text.size() > 11)
                break;
            const FString Token = Text(Part->Text);
            const int64 Number = FCString::Strtoi64(*Token, nullptr, 10);
            if (Number < MIN_int32 || Number > MAX_int32 || FString::Printf(TEXT("%lld"), Number) != Token)
                break;
            Words[Count++] = uint32(int32(Number));
        }
        if (Count == 4)
            Lighting.Add(TEXT("light\t") + Node.Path + TEXT("\t") +
                FGuid(Words[0], Words[1], Words[2], Words[3]).ToString(EGuidFormats::Digits));
    }
    return Lighting;
}

// Lines as one short text, and back: the length of the text in bytes, then the text packed.
FString Pack(const TArray<FString>& Content)
{
    const FTCHARToUTF8 Plain(*FString::Join(Content, TEXT("\n")));
    if (!Plain.Length())
        return TEXT("0:");
    TArray<uint8> Packed;
    int32 Size = FCompression::CompressMemoryBound(NAME_Zlib, Plain.Length());
    Packed.SetNumUninitialized(Size);
    if (!FCompression::CompressMemory(NAME_Zlib, Packed.GetData(), Size, Plain.Get(), Plain.Length()))
        return FString();
    Packed.SetNum(Size);
    return FString::Printf(TEXT("%d:"), Plain.Length()) + FBase64::Encode(Packed);
}

bool Unpack(const FString& Text, TArray<FString>& Content)
{
    int32 Colon = INDEX_NONE;
    TArray<uint8> Packed, Plain;
    if (!Text.FindChar(TEXT(':'), Colon))
        return false;
    const int32 Size = FCString::Atoi(*Text.Left(Colon));
    if (Size < 0 || Size > 512 * 1024 * 1024 || !FBase64::Decode(Text.Mid(Colon + 1), Packed))
        return false;
    Plain.SetNumZeroed(Size + 1);
    if (Size && !FCompression::UncompressMemory(NAME_Zlib, Plain.GetData(), Size, Packed.GetData(), Packed.Num()))
        return false;
    Content.Reset();
    if (!Size)
        return true;
    const FUTF8ToTCHAR Wide(reinterpret_cast<const ANSICHAR*>(Plain.GetData()), Size);
    FString(Wide.Length(), Wide.Get()).ParseIntoArray(Content, TEXT("\n"), false);
    return true;
}
} // namespace P4LevelContent

FP4CookedTableRowObservation P4ObserveCookedTableRowsNative(UDataTable* Table)
{
    FP4CookedTableRowObservation Result;
    const auto Refuse = [&](EP4CookedTableRowObservationError Error, const FString& Detail)
    {
        Result.Error = Error;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Refuse(
            EP4CookedTableRowObservationError::WrongThread, TEXT("table-row-observation-requires-game-thread"));
    if (!IsValid(Table) || !Table->RowStruct)
        return Refuse(EP4CookedTableRowObservationError::InvalidTable, TEXT("table-row-observation-invalid-table"));
    TStrongObjectPtr<UDataTable> Held(Table);
    Result.Package = Table->GetOutermost()->GetName();
    Result.AssetPath = Table->GetPathName();
    P4LevelContent::FFile File;
    FString Error;
    if (!P4LevelContent::Cooked(Table, false, File, Error))
        return Refuse(EP4CookedTableRowObservationError::CookFailed, Error);
    if (!P4LevelContent::ReadTables(File, true, Error, true))
        return Refuse(EP4CookedTableRowObservationError::RowsUnreadable, Error);
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
    {
        const P4LevelContent::FNode& Node = File.Node(Index);
        if (Node.Outer != 0 || !Node.Name.Equals(Table->GetName(), ESearchCase::CaseSensitive) ||
            !Node.Class.Equals(TEXT("/Script/Engine.DataTable"), ESearchCase::CaseSensitive))
            continue;
        const P4LevelContent::FTableRows* Rows = File.Tables.Find(Index);
        const P4MapKit::FDecodedValue* Struct = Node.Values.Field("RowStruct");
        if (!Rows || !Struct || Struct->Kind != P4MapKit::FDecodedValue::EKind::String ||
            Rows->Names.Num() != Rows->Rows.Num())
            return Refuse(
                EP4CookedTableRowObservationError::RowsUnreadable, TEXT("table-row-observation-cooked-rows-not-read"));
        Result.RowStruct = UTF8_TO_TCHAR(Struct->Text.c_str());
        Result.RowNames = Rows->Names;
        Result.Rows = Rows->Rows;
        return Result;
    }
    return Refuse(
        EP4CookedTableRowObservationError::TableNotFound, TEXT("table-row-observation-cooked-table-not-found"));
}

// Only complete native layouts whose file positions are either absent or handled by the
// texture relocator may move when the stored engine fields enter the cooked tag stream.
bool P4ValidateStockEngineCookRelocation(
    const std::vector<uint8>& Header, const std::vector<uint8>& Body, FString& Error)
{
    using namespace P4LevelContent;
    FFile File;
    File.Header = Header;
    File.Body = Body;
    if (!ReadNodes(File, FString(), Error))
        return false;
    bool Map = false;
    for (const FNode& Node : File.Nodes)
        Map = Map || Node.Class == TEXT("/Script/Engine.World");
    if (Map)
    {
        File = FFile();
        File.Header = Header;
        File.Body = Body;
        if (!Read(File, FString(), Error))
            return false;
    }
    TSet<int32> Code;
    for (const FNode& Node : File.Nodes)
        if (Node.Class == TEXT("/Script/Engine.BlueprintGeneratedClass"))
        {
            try
            {
                const auto Structure = P4MapKit::Blueprint::ReadClassStructure(File.Package, File.Body);
                if (Structure["kind"].S() != "Blueprint")
                    break;
                Code.Add(int32(Structure["class"]["index"].U()));
                for (const auto& Name : Structure["functions"].Keys())
                    Code.Add(int32(Structure["functions"][Name]["index"].U()));
            }
            catch (const std::exception& Why)
            {
                Error = TEXT("stock-engine-field-cook-code-unreadable: ") + Text(Why.what());
                return false;
            }
            break;
        }
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
    {
        const FNode& Node = File.Node(Index);
        if (!Node.Undecoded.IsEmpty())
        {
            Error = TEXT("stock-engine-field-cook-properties-unreadable: ") + Node.Path;
            return false;
        }
        // Skeletons, skinned meshes and sequences are read whole by the skinned reader: their arrays are
        // written inline in one piece and a stream outside the file refuses, so they hold no file positions.
        // A particle module's required data is its cutout geometry, read whole (P4OriginalParticleCutout).
        // Static foliage contains only identity and package indices, read in full by Read().
        // Appending imports preserves those indices; it has no absolute file offsets to relocate.
        if (Node.PlainTail || Code.Contains(Index) || Node.Lods >= 0 || File.Instanced.Contains(Index) ||
            File.Brushes.Contains(Index) || File.Foliage.Contains(Index) || Node.Physics || Node.PhysicsBody ||
            Node.Skinned || Node.ParticleRequired || Index == File.World || Index == File.Level ||
            Node.Class == TEXT("/Script/Engine.Texture2D"))
            continue;
        // A sequence the skinned reader does not decode (curves, another codec) still holds no file position: its
        // cooked data is written inline (FCompressedAnimSequence::SerializeCompressedData), and the bulk form is
        // only for a target with memory-mapped animation, which the export's WindowsNoEditor cook is not.
        if (Node.Class == TEXT("/Script/Engine.AnimSequence"))
            continue;
        if (Node.Class == TEXT("/Script/Landscape.LandscapeMaterialInstanceConstant"))
        {
            // UMaterialInstance::Serialize writes its resource count after UObject identity.
            // Nonempty resources are a FMaterialResourceMemoryWriter archive: a local name
            // table, relative resource locations and one length-prefixed memory buffer.
            // Its Tell/Seek and FNames are relative to that buffer, not the enclosing package
            // (Material.cpp SerializeInlineShaderMaps; MaterialShared.cpp SerializeToParentArchive).
            P4MapKit::FByteReader Reader(Body, Node.TailStart, Node.TailEnd);
            std::size_t Resources = 0;
            bool Safe = P4MapKit::LevelNative::ObjectId(Reader) && Reader.Count(Resources, 64);
            if (Safe && Resources)
            {
                std::size_t Names = 0, Locations = 0, Bytes = 0;
                std::string Name;
                Safe = Reader.Count(Names, 1000000);
                for (std::size_t At = 0; Safe && At < Names; ++At)
                    Safe = Reader.String(Name) && Reader.Skip(4);
                Safe = Safe && Reader.Count(Locations, 64) && Locations == Resources;
                std::vector<uint32> Offsets;
                for (std::size_t At = 0; Safe && At < Locations; ++At)
                {
                    uint32 Offset = 0;
                    uint64 Feature = 0, Quality = 0;
                    Safe = Reader.U32(Offset) && Reader.Unsigned(1, Feature) && Reader.Unsigned(1, Quality) &&
                        Feature < uint64(ERHIFeatureLevel::Num) && Quality <= uint64(EMaterialQualityLevel::Num) &&
                        (At ? Offset > Offsets.back() : Offset == 0);
                    if (Safe)
                        Offsets.push_back(Offset);
                }
                Safe = Safe && Reader.Count(Bytes, 64 * 1024 * 1024) && Bytes == Reader.Limit() - Reader.Tell();
                for (uint32 Offset : Offsets)
                    Safe = Safe && Offset < Bytes;
                if (Safe)
                    Safe = Reader.Skip(Bytes);
            }
            if (Safe && Reader.Tell() == Node.TailEnd)
                continue;
        }
        if (Node.Class == TEXT("/Script/Engine.Model"))
        {
            const auto Model = P4MapKit::ReadModelNative(Body, Node.TailStart, Node.TailEnd);
            if (Model.Error.Code.empty() && Model.End == Node.TailEnd)
                continue;
        }
        if (Node.EngineTail == TEXT("the id of a lighting build") ||
            Node.EngineTail == TEXT("the navigation mesh of a navigation build") ||
            Node.EngineTail == TEXT("a landscape component's grass data, none, and the flags of a cook") ||
            Node.EngineTail == TEXT("the collision a cook makes from a landscape's heights"))
            continue;
        if (Node.Class == TEXT("/Script/Engine.BodySetup"))
        {
            P4MapKit::FByteBulkContext Context;
            Context.HeaderSize = File.Package.HeaderSize;
            Context.FileSizes[0] = int64(Body.size());
            const auto Bulk = P4MapKit::ReadInlinePhysicsBodyBulk(
                Body, File.Package.Names, Node.TailStart, Node.TailEnd, Context);
            if (Bulk.Error.Code.empty())
                continue; // Its absolute bulk headers are rebased by the package relocator.
        }
        Error = TEXT("stock-engine-field-cook-native-tail-relocation-unqualified: ") + Node.Path + TEXT(" (") +
            Node.Class + TEXT(")");
        return false;
    }
    Error.Reset();
    return true;
}

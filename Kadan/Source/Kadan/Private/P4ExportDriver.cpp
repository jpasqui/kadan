// The mod export: one run of FP4ExportRun, its steps in order up to the cook of the creator's packages.

#include "P4ExportInternal.h"
#include "UObject/UObjectHash.h"
#include "P4GameBuild.h"
#include "P4KadanImport.h"
#include "P4KadanSourceCatalog.h"
#include "P4GameMaterialUsage.h"
#include "P4NativeConstructionPreview.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"

using namespace P4Export;

bool P4ExportMod(const FP4ExportModRequest& Request, FP4ExportModResult& Out, FString& Error)
{
    // The export reads the map as it is, without what the editor previews of the game's native classes.
    FP4SuspendNativePreviews Previews;
    FP4ExportRun Run(Request, Out, Error);
    return Run.Execute();
}

FP4ExportRun::FP4ExportRun(const FP4ExportModRequest& InRequest, FP4ExportModResult& InOut, FString& InError)
    : Request(InRequest), Out(InOut), Error(InError)
{}

bool FP4ExportRun::Execute()
{
    return Review() && ReadDeclarations() && ReadLevelEdits() && BuildPlacementLists() &&
        PreflightCreatorReferences() && PrepareWorkFolders() && StageMaterials() && BuildNavigation() &&
        ChooseLightingMap() && TellGameMaterialUsage() && ExportAddedLevels() && BuildLighting() && CookPlacementLists() && CookWholeLevels() &&
        CookNativeTables() && CookOwnPackages() &&
        WriteManifest() && PackContainer() && WritePreparedFiles() && Publish();
}

bool FP4ExportRun::Fail(const FString& Problem)
{
    Error = Problem;
    Result.Refused.Add(Problem);
    Out = MoveTemp(Result);
    return false;
}

bool FP4ExportRun::Review()
{
    if (!IsInGameThread() || !GEditor)
        return Fail(TEXT("Mod export requires the editor thread"));
    if (!P4ReportExportStage(EP4ExportStage::Review, Request.Mod.Id, Error))
    {
        return Fail(Error);
    }
    if (!P4ModDerivedNames(Request.Mod, Names, Error) || !SafeIdLeaf(Request.Mod.Id))
        return Fail(Error.IsEmpty() ? TEXT("Mod ID is a reserved output filename") : Error);
    if (Request.PreparePreviewPackages && !Request.PreviewOnly)
        return Fail(TEXT("Private preview package preparation cannot run in an ordinary mod export."));
    if (Request.PreviewOnly &&
        (!Request.PreviewWorldObjectPath.StartsWith(Names.PackageRoot + TEXT("/"), ESearchCase::CaseSensitive) ||
            (!Request.PreparePreviewPackages && !FindObject<UWorld>(nullptr, *Request.PreviewWorldObjectPath))))
        return Fail(TEXT("Private preview export requires its generated world in this preview namespace."));
    if (Request.ProjectDirectory.IsEmpty() || Request.OutputDirectory.IsEmpty() || Request.WorkDirectory.IsEmpty())
        return Fail(TEXT("Project, output and private work directories are required"));
    FP4ModDescriptor Persisted;
    if (!P4ReadModDescriptor(Request.ProjectDirectory, Request.Mod.Id, Persisted, Error) ||
        Persisted.Name != Request.Mod.Name || Persisted.Version != Request.Mod.Version)
        return Fail(TEXT("Export needs the current persisted project mod descriptor"));
    const FString Unsupported = P4GameBuildUnsupported(Request.GamePaksDirectory);
    if (!Unsupported.IsEmpty())
        return Fail(Unsupported);
    if (!P4ReadExportGameInventory(Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error))
        return Fail(Error);
    if (!P4PreviewExportSelection(Request.Mod, Request.Roots, Inventory, Request.OtherPackageClaims, Preview, Error))
        return Fail(Error);
    if (Preview.Refused.Num())
    {
        Result.Refused = Preview.Refused;
        Out = MoveTemp(Result);
        Error = TEXT("Selection has refused packages");
        return false;
    }
    if (!Preview.CookCandidates.Num() && !Preview.GameEditCandidates.Num() && !Preview.TableEditCandidates.Num())
    {
        if (!Inventory.Contains(FName(*Request.MapPackage)))
            return Fail(TEXT("Unchanged levels must target an actual original game map"));
        if (!P4ValidateUnchangedMapSelection(
                Request.Mod, Preview, LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Request.MapPackage)), Error))
            return Fail(Error);
    }
    return true;
}

bool FP4ExportRun::ReadDeclarations()
{
    TSet<FString> ContentNames;
    for (const FString& Package : Preview.CookCandidates)
    {
        const UP4AddedContent* Definition = Cast<UP4AddedContent>(
            LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package)));
        if (!Definition)
            continue;
        TSharedPtr<FJsonObject> Declaration;
        if (!P4AddedContentDeclaration(Definition, Names.PackageRoot, Declaration, Error))
            return Fail(Error);
        const TCHAR* Group = Definition->Kind == EP4AddedContentKind::Treasure ? TEXT("treasures") : TEXT("creatures");
        if (!Content->HasField(Group))
            Content->SetObjectField(Group, MakeShared<FJsonObject>());
        auto Declarations = Content->GetObjectField(Group);
        if (ContentNames.Contains(Definition->ShortName))
            return Fail(TEXT("Two declarations use the same new kind name."));
        ContentNames.Add(Definition->ShortName);
        Declarations->SetObjectField(Definition->ShortName, Declaration);
        if (ContentClasses.Contains(Declaration->GetStringField(TEXT("class"))))
            return Fail(TEXT("Give each new kind a Blueprint child class of its own."));
        ContentClasses.Add(Declaration->GetStringField(TEXT("class")));
        ContentDefinitions.Add(Package);
    }
    for (const FString& Package : Preview.TableEditCandidates)
    {
        UDataTable* Table = LoadObject<UDataTable>(nullptr, *P4ObjectPathOf(Package));
        TSharedPtr<FJsonObject> Changes;
        if (!P4ExportTableChanges(Table, Request.Mod.Id, Changes, Error))
            return Fail(Package + TEXT(" cannot be exported. ") + Error);
        if (Changes->Values.Num())
            Tables->SetObjectField(P4ObjectPathOf(Package), Changes);
    }
    return true;
}

bool FP4ExportRun::ReadLevelEdits()
{
    if (!P4GroupExportMaps(Preview.GameEditCandidates, Inventory, Request.MapPackage, Groups, Error))
        return Fail(Error);
    Maps.resize(Groups.Num());
    // The rows of the game's placement lists of each edited map, read from the game's files once per map.
    TMap<FString, TArray<FP4GamePlacementRow>> GameRows;
    for (int32 Index = 0; Index < Groups.Num(); ++Index)
    {
        Maps[Index].Package = TCHAR_TO_UTF8(*Groups[Index].Map);
        for (const FString& Package : Groups[Index].Levels)
        {
            UWorld* World = LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Package));
            FLevelEdit Edit;
            Edit.MapIndex = Index;
            if (Request.WholeLevels || Request.Native)
            {
                if (!World)
                    return Fail(TEXT("Selected original World could not be loaded"));
                // Nothing is translated, and what is walked on can be anything in the level.
                Edit.World = World;
                Edit.Package = Package;
                Edit.Level = Package;
                // Without Tsuchi nothing swaps a map's navigation: the level keeps the navigation it holds.
                Edit.Navigation = !Request.Native;
                Edit.NavigationWhy = TEXT("The whole level");
                // The creator's assets the level names (an added cube's mesh, the materials set on it), so they
                // are staged and cooked as an added actor's would be.
                TArray<UObject*> Objects, Referenced;
                GetObjectsWithPackage(World->GetOutermost(), Objects, true);
                FReferenceFinder Finder(Referenced, nullptr, false, true, false, true);
                for (UObject* Object : Objects)
                    Finder.FindReferences(Object);
                for (UObject* Object : Referenced)
                {
                    const FString Referred = Object ? Object->GetOutermost()->GetName() : FString();
                    if (!Referred.IsEmpty() && Referred != Package && !Inventory.Contains(FName(*Referred)) &&
                        P4IsContentPackage(Referred) && FPackageName::DoesPackageExist(Referred))
                        Edit.References.AddUnique(Referred);
                }
                WholeLevels.AddUnique(Package);
                Edits.Add(MoveTemp(Edit));
                continue;
            }
            FString ListFolder = Request.ExtractedGameDirectory.IsEmpty()
                ? FString()
                : P4GamePlacementFolder(Request.ExtractedGameDirectory, Package);
            // A game folder without the map's lists (one imported from the game's containers): the lists the import
            // converted from them into this project.
            if (ListFolder.IsEmpty())
                ListFolder = P4GamePlacementFolder(P4KadanCatalogGameFolder(), Package);
            if (!ListFolder.IsEmpty() && !GameRows.Contains(ListFolder))
            {
                TArray<FP4GamePlacementRow> Rows;
                if (!P4ReadGamePlacementLists(ListFolder, Rows, Error))
                    return Fail(TEXT("The game's placement lists of ") + Groups[Index].Map +
                        TEXT(" could not be read (") + Error + TEXT(")."));
                GameRows.Add(ListFolder, MoveTemp(Rows));
            }
            if (!World ||
                !LevelChanges(World, P4AnalyzeOriginalLevelChanges(World), Maps[Index], Edit, Error, true,
                    GameRows.Find(ListFolder)))
                return Fail(Error.IsEmpty() ? TEXT("Selected original World could not be loaded") : Error);
            Result.GameRowEdits.Append(Edit.RowEdits);
            Edits.Add(MoveTemp(Edit));
        }
    }
    return true;
}

bool FP4ExportRun::BuildPlacementLists()
{
    for (int32 Index = 0; Index < Groups.Num(); ++Index)
    {
        PlacementLists.Add(MakeUnique<FP4PlacementList>());
        FP4PlacementList& Placements = *PlacementLists.Last();
        TArray<AActor*> Placed;
        for (const FLevelEdit& Edit : Edits)
            if (Edit.MapIndex == Index)
                Placed.Append(Edit.Placed);
        if (Placed.Num())
        {
            const FString PlacementRoot = Groups.Num() == 1
                ? Names.PackageRoot
                : Names.PackageRoot / (TEXT("Maps/Map") + FString::FromInt(Index + 1));
            if (!P4BuildPlacementList(Request.Mod.Id, PlacementRoot, Groups[Index].Map, Placed, Placements, Error))
                return Fail(Error);
            // A row names a class of the game's, which the player's game supplies. No class is shipped.
            for (const FP4PlacementRow& Row : Placements.Rows)
            {
                const FString Package = FPackageName::ObjectPathToPackageName(Row.Class);
                if (!Inventory.Contains(FName(*Package)) && !ContentClasses.Contains(Row.Class))
                    return Fail(Row.Actor +
                        TEXT(" uses a new class without a treasure or creature declaration in this selection."));
            }
            Maps[Index].Placements.push_back(TCHAR_TO_UTF8(*Placements.Package));
        }
    }
    return true;
}

bool FP4ExportRun::PreflightCreatorReferences()
{
    Mod.Format = Request.Mod.Format;
    Mod.Id = TCHAR_TO_UTF8(*Request.Mod.Id);
    Mod.Name = TCHAR_TO_UTF8(*Request.Mod.Name);
    Mod.Version = TCHAR_TO_UTF8(*Request.Mod.Version);
    // Refuse unsupported game-ready conversions before cooking or touching a previous output.
    FP4CreatorReferenceInputs CreatorReferences;
    CreatorReferences.Packages = Preview.CookCandidates;
    for (const FString& Definition : ContentDefinitions)
        CreatorReferences.Packages.Remove(Definition);
    for (const FString& Class : ContentClasses)
        CreatorReferences.Packages.AddUnique(FPackageName::ObjectPathToPackageName(Class));
    for (const FLevelEdit& Edit : Edits)
    {
        CreatorReferences.Packages.Append(Edit.References);
        CreatorReferences.AssetValues.Append(Edit.AssetValues);
        for (const FString& Name : Edit.Added)
        {
            AActor* Actor = FindObject<AActor>(Edit.World->PersistentLevel, *Name);
            if (!Actor)
                return Fail(TEXT("An added actor disappeared before export: ") + Name);
            CreatorReferences.AddedActors.Add(Actor);
        }
    }
    if (!P4PreflightCreatorReferences(
            Request.Mod, CreatorReferences, &Inventory, Request.OtherPackageClaims, CustomMaterials, Own, Error))
        return Fail(Error);
    GatherHold.Emplace();
    for (const FString& Package : Own)
        if (P4ExportAssetAvailability(LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package)), Request.Mod).Route ==
                EP4ExportAssetRoute::CreatorLevel &&
            !Request.PreviewOnly && !Inventory.Contains(FName(*Request.MapPackage)))
            return Fail(TEXT("Choose a game map to load your level and its level script."));
    return true;
}

bool FP4ExportRun::PrepareWorkFolders()
{
    Target = FPaths::ConvertRelativePathToFull(Request.OutputDirectory / Request.Mod.Id);
    Working = P4ExportWorkingDirectory(Request.OutputDirectory, Request.WorkDirectory, Request.Mod.Id,
        {FPaths::ProjectSavedDir(), FPaths::ProjectContentDir()});
    if (Working.IsEmpty())
    {
        return Fail(TEXT("Kadan could not choose a private work folder on the output volume."));
    }
    if (Target.Equals(Working, ESearchCase::IgnoreCase) || FPaths::IsUnderDirectory(Working, Target) ||
        FPaths::IsUnderDirectory(Target, Working))
        return Fail(TEXT("Private export work and published output directories must be independent"));
    // Unreal mounts Saved and Content and scans files that appear there. Cooked and read-back packages in
    // either would be taken for project assets, and a folder name that cannot be a package name stops the editor.
    for (const FString& Mounted : {FPaths::ProjectSavedDir(), FPaths::ProjectContentDir()})
        if (FPaths::IsUnderDirectory(Working, FPaths::ConvertRelativePathToFull(Mounted)))
            return Fail(TEXT("Kadan's export work folder has to be outside the project's Saved and Content folders."));
    if (!NoLinks(Target, Error) || !NoLinks(Working, Error))
        return Fail(Error);
    if (!IFileManager::Get().MakeDirectory(*Request.OutputDirectory, true) ||
        !IFileManager::Get().DirectoryExists(*Request.OutputDirectory) ||
        !IFileManager::Get().MakeDirectory(*Working, true) || !IFileManager::Get().DirectoryExists(*Working))
        return Fail(TEXT("Kadan could not prepare the export work folders."));
    const FString Lock = Target + TEXT(".lock");
    Guard.Reset(IFileManager::Get().CreateFileWriter(*Lock, FILEWRITE_NoReplaceExisting));
    if (!Guard)
        return Fail(TEXT("Another export holds this mod's output lock"));
    Release.Emplace(Guard, Lock);
    // Exports of one mod to other destinations, or from another project, can choose the same work folder. Hold it
    // too, so no other export resets the prepared and cooked files this one is using.
    const FString WorkLock = Working + TEXT(".lock");
    WorkGuard.Reset(IFileManager::Get().CreateFileWriter(*WorkLock, FILEWRITE_NoReplaceExisting));
    if (!WorkGuard)
        return Fail(
            TEXT("Another export of this mod is using its work folder. Wait for it to finish, then export again."));
    WorkRelease.Emplace(WorkGuard, WorkLock);
    Prepared = Working / TEXT("prepared");
    Stage = Working / TEXT("cooked");
    // A directory marker keeps one mod's reusable work folder from being used by another.
    const FString Marker = Working / TEXT("identity.txt");
    FString Owner;
    if (IFileManager::Get().FileExists(*Marker))
    {
        if (!FFileHelper::LoadFileToString(Owner, *Marker) || !P4ExactNativeString(Owner, Request.Mod.Id))
            return Fail(TEXT("Export work directory belongs to another identity"));
    }
    else
    {
        TArray<FString> Existing;
        IFileManager::Get().FindFiles(Existing, *(Working / TEXT("*")), true, true);
        if (Existing.Num() ||
            !FFileHelper::SaveStringToFile(Request.Mod.Id, *Marker, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            return Fail(TEXT("Export work directory is not empty or cannot be claimed"));
    }
    for (const FString& Directory : {Prepared, Stage})
    {
        if (!NoLinks(Directory, Error) ||
            (IFileManager::Get().DirectoryExists(*Directory) &&
                !IFileManager::Get().DeleteDirectory(*Directory, false, true)) ||
            !IFileManager::Get().MakeDirectory(*Directory, true))
            return Fail(Error.IsEmpty() ? TEXT("Cannot reset this export's prepared directories") : Error);
    }
    return true;
}

bool FP4ExportRun::StageMaterials()
{
    // Actors added to a game level go into a level of the mod's own. The game level itself is never cooked.
    if (!Request.ExtractedGameDirectory.IsEmpty())
        GameFiles = IFileManager::Get().DirectoryExists(
                        *(Request.ExtractedGameDirectory / TEXT(P4_GAME_CONTENT_FOLDER "/" P4_GAME_PROJECT)))
            ? Request.ExtractedGameDirectory / TEXT(P4_GAME_CONTENT_FOLDER)
            : Request.ExtractedGameDirectory;
    // Through UE's shaders the cook compiles for the game too (GLSL_SWITCH), which UE's workers cannot.
    if (P4UseSwitchShaderRoute())
        SwitchInProcess.Emplace();
    Materials.Emplace();
    {
        // Maps are compiled for the shader format of the build the mod is exported against.
        FP4GameBuildInfo Build;
        FString BuildError;
        if (!P4ResolveGameBuild(Request.GamePaksDirectory, Build, BuildError))
            return Fail(BuildError);
        Materials->SetShaderFormat(UTF8_TO_TCHAR(Build.Family->ShaderFormat));
    }
    TArray<UObject*> Assets;
    for (const FString& Package : Own)
        Assets.Add(LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package)));
    if (!Materials->Prepare(Names.PackageRoot, Assets, Error))
        return Fail(Error);
    for (UObject* Asset : Assets)
        if (Asset)
            Result.RuntimePaths.Add(Asset->GetPathName(), Materials->RuntimePath(Asset->GetPathName()));
    for (int32 Index = 0; Index < Own.Num(); ++Index)
    {
        UObject* Cooked = Materials->CookAsset(Assets[Index]);
        if (Cooked != Assets[Index])
            Own[Index] = Cooked->GetOutermost()->GetName();
    }
    // Typed actor asset references name the generated runtime material, rather than
    // the editable graph that is intentionally absent from the cooked container.
    for (const FLevelEdit& Edit : Edits)
        for (const FAssetValueReference& Reference : Edit.AssetValues)
        {
            const FString Runtime = Materials->RuntimePath(Reference.Target);
            if (Runtime == Reference.Target)
                continue;
            auto& Values =
                Maps[Edit.MapIndex].Changes.at(TCHAR_TO_UTF8(*Edit.Level)).Set.at(TCHAR_TO_UTF8(*Reference.Actor));
            if (!P4RemapActorAssetValue(Values, Reference, Runtime, Error))
                return Fail(Error);
        }
    for (const FString& Package : Own)
        if (P4ExportAssetAvailability(LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package)), Request.Mod).Route ==
            EP4ExportAssetRoute::CreatorSound)
            SoundSources.Add(Package);
    Known = TSet<FString>(Own);
    for (const auto& List : PlacementLists)
        if (List->Asset.IsValid())
        {
            Known.Add(List->Package);
            Known.Add(List->ListPackage);
        }
    return true;
}

bool FP4ExportRun::Follow(const FString& From, const FString& Header)
{
    TArray<FString> Named;
    if (!PackagesNamed(Header, Named, Error))
        return false;
    for (const FString& Package : Named)
    {
        if (SoundSources.Contains(Package))
        {
            Error = From + TEXT(" refers to the source sound ") + Package +
                TEXT(". Play your sound bank through Tsuchi instead.");
            return false;
        }
        if (Known.Contains(Package))
            continue;
        // A game asset the creator replaced with their own file is cooked, wherever it is named from (K9).
        const bool Replacement = P4IsGameReplacementPackage(Package);
        if (!Replacement && Inventory.Contains(FName(*Package)))
            continue;
        if (!Replacement && !P4IsContentPackage(Package))
        {
            Error = From + TEXT(" uses ") + Package + TEXT(", which is not a package of the project's Content.");
            return false;
        }
        Known.Add(Package);
        Own.Add(Package);
    }
    return true;
}

bool FP4ExportRun::BuildNavigation()
{
    // An edit that changes where Pikmin can walk gets the map's navigation built again in the editor and
    // converted into the game's navigation file, which the mod names for the map.
    // Each edited map whose walkable ground changed gets its own navigation file.
    Navigations.SetNum(Groups.Num());
    for (const FLevelEdit& Edit : Edits)
    {
        if (!Edit.Navigation)
            continue;
        FMapNavigationEdit& Each = Navigations[Edit.MapIndex];
        if (Each.Why.IsEmpty())
        {
            Each.Why = Edit.NavigationWhy;
            Each.Level = Edit.Level;
        }
    }
    for (int32 Index = 0; Index < Groups.Num(); ++Index)
    {
        FMapNavigationEdit& Each = Navigations[Index];
        if (Each.Why.IsEmpty())
            continue;
        if (!P4ReportExportStage(EP4ExportStage::Navigation, Groups[Index].Map, Error))
        {
            return Fail(Error);
        }
        // A game folder that holds only the game's containers: the map is converted from them, as import does.
        FP4KadanImportSettings Containers;
        Containers.GameFolder = Request.ExtractedGameDirectory;
        Containers.GamePaks = Request.GamePaksDirectory;
        Containers.Retoc = Request.Tools.Retoc;
        if (!P4BuildMapNavigation(Groups[Index].Map, GameFiles, Each.Navigation, Error,
                Containers.GamePaks.IsEmpty() || Containers.Retoc.IsEmpty() ? nullptr : &Containers))
            return Fail(Error);
        Each.Built = true;
        Maps[Index].Navigation = !Each.Navigation.Kept;
    }
    return true;
}

bool FP4ExportRun::ChooseLightingMap()
{
    for (const FLevelEdit& Edit : Edits)
    {
        if (!Edit.RebuildLighting)
            continue;
        if (LightingMap != INDEX_NONE && LightingMap != Edit.MapIndex)
            return Fail(TEXT(
                "Lighting can be rebuilt for one edited map per export. Export the other map's lit changes in another mod."));
        LightingMap = Edit.MapIndex;
        NeedsLighting = true;
    }
    return true;
}

bool FP4ExportRun::ExportAddedLevels()
{
    // Actors added to a game level go into a level of the mod's own. The added static meshes that do not move
    // and are tied to no other actor go into a second level of the mod's own when their map is the one open in
    // the editor. That level is held in the editor only, lit inside the open map by the same lighting build that
    // rebuilds the game's lighting when an edit needs it, cooked with its lighting data and thrown away.
    UWorld* OpenWorld = GEditor->GetEditorWorldContext().World();
    const FString OpenMap = OpenWorld ? OpenWorld->GetOutermost()->GetName() : FString();
    for (FLevelEdit& Edit : Edits)
    {
        if (!Edit.Added.Num())
            continue;
        const FString Base = Names.PackageRoot + TEXT("/Maps/") + P4ContentRelativePath(Edit.Package);
        const FString ModLevel = Base + TEXT("_Added");
        const bool Bakeable = Groups[Edit.MapIndex].Map == OpenMap;
        TArray<FString> Leave;
        for (const AActor* Actor : Edit.Placed)
            Leave.Add(Actor->GetName());
        TFunction<bool(UWorld*, FString&)> PrepareCook;
        if (Materials->HasMaterials())
            PrepareCook = [this](UWorld* World, FString& Problem) { return Materials->Remap(World, Problem, false); };
        if (!P4ReportExportStage(EP4ExportStage::Cooking, Edit.Level, Error))
        {
            return Fail(Error);
        }
        // The game draws a mesh with baked lighting only with a material whose own master allows that, and draws the
        // default material otherwise. Without baked lighting the same mesh draws with its material.
        FP4KadanImportSettings Containers;
        Containers.GameFolder = Request.ExtractedGameDirectory;
        Containers.GamePaks = Request.GamePaksDirectory;
        Containers.Retoc = Request.Tools.Retoc;
        FP4GameMaterialUsage GameUsage(Containers);
        const auto WhyNotBaked = [&](const AActor* Actor) -> FString
        {
            const AStaticMeshActor* Mesh = Cast<AStaticMeshActor>(Actor);
            const UStaticMeshComponent* Part = Mesh ? Mesh->GetStaticMeshComponent() : nullptr;
            for (int32 Slot = 0; Part && Slot < Part->GetNumMaterials(); ++Slot)
            {
                UMaterialInterface* Material = Part->GetMaterial(Slot);
                FString Unread;
                if (Material && !GameUsage.Allows(Material, TEXT("bUsedWithStaticLighting"), Unread))
                    return FString::Printf(TEXT("The game does not draw its material %s with baked lighting."),
                               *Material->GetName()) + (Unread.IsEmpty() ? FString() : TEXT(" ") + Unread);
            }
            return FString();
        };
        const FP4LevelExportResult Exported = P4ExportOriginalLevelActors(Edit.World, ModLevel, Stage, {}, GameFiles,
            Bakeable ? Base + TEXT("_Lit") : FString(), Leave, PrepareCook,
            Containers.GamePaks.IsEmpty() || Containers.Retoc.IsEmpty() ? TFunction<FString(const AActor*)>() : WhyNotBaked);
        if (!Exported.LitLevel.IsEmpty())
        {
            FP4AddedLitLevel Lit;
            if (UPackage* Package = FindPackage(nullptr, *Exported.LitLevel))
                Lit.World = UWorld::FindWorldInPackage(Package);
            for (const FString& Name : Exported.LitActors)
                if (AActor* Source = FindObject<AActor>(Edit.World->PersistentLevel, *Name))
                    Lit.Sources.Add(Source);
            if (Exported.IsValid() && (!Lit.World || Lit.Sources.Num() != Exported.LitActors.Num()))
            {
                LitLevels.Add(Lit);
                return Fail(TEXT("The level of the static meshes added to ") + Edit.Level +
                    TEXT(" to be lit could not be made."));
            }
            LitLevels.Add(Lit);
            LitMaps.Add(Edit.MapIndex);
        }
        if (!Exported.IsValid())
            return Fail(TEXT("The actors added to ") + Edit.Level +
                TEXT(" could not be put into a level of this mod (") + Exported.ErrorDetail + TEXT(")."));
        // A reference between an added actor and one of the game's would take a copy of the game's actor.
        if (Exported.PulledActors.Num())
            return Fail(FString::Printf(
                TEXT(
                    "An actor added to %s refers to %s of the game's level, or is referred to by it, which cannot be exported yet."),
                *Edit.Level, *FString::Join(Exported.PulledActors, TEXT(", "))));
        // Every added actor without baked lighting is named with the reason.
        for (const FP4LevelUnlitActor& Unlit : Exported.UnlitActors)
        {
            Result.UnbakedActors.Add(Unlit.Actor);
            Result.Told.Add(
                FString::Printf(TEXT("%s is exported without baked lighting. %s"), *Unlit.Actor, *Unlit.Reason));
        }
        if (!Bakeable)
        {
            Result.UnbakedActors.Append(Edit.Added);
            Result.Told.Add(FString::Printf(
                TEXT(
                    "The actors added to %s are exported without baked lighting, because %s is not the map open in the editor. Open it and export again to bake them."),
                *Edit.Level, *FPackageName::GetShortName(Groups[Edit.MapIndex].Map)));
        }
        if (!Exported.LevelMade)
            continue;
        Maps[Edit.MapIndex].Levels.push_back(TCHAR_TO_UTF8(*ModLevel));
        Result.Levels.Add(ModLevel);
        if (!Follow(ModLevel, CookedBase(Stage, ModLevel) + TEXT(".umap")))
            return Fail(Error);
    }
    return true;
}

bool FP4ExportRun::BuildLighting()
{
    if (NeedsLighting || LitLevels.Num())
    {
        // One build lights the added levels and, when an edit needs it, the game's levels too.
        FP4RebuiltGameLighting Lighting;
        const int32 MapIndex = NeedsLighting ? LightingMap : LitMaps[0];
        UWorld* Persistent = LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Groups[MapIndex].Map));
        if (!P4ReportExportStage(EP4ExportStage::Lighting, Groups[MapIndex].Map, Error))
        {
            return Fail(Error);
        }
        if (!P4BuildEditedMapLighting(Persistent, Inventory, NeedsLighting, LitLevels, Lighting, Error))
            return Fail(Error);
        Result.Told.Add(Lighting.LightmassSettingsFrom.IsEmpty()
                ? FString(TEXT(
                      "The map's environment has no Lightmass settings, so its lighting was built with the engine's own and the game's ambient occlusion."))
                : TEXT("The lighting was built with the game's own Lightmass settings from ") +
                    FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(Lighting.LightmassSettingsFrom)) +
                    TEXT("."));
        for (UMapBuildDataRegistry* Registry : Lighting.Registries)
        {
            const FString Package = Registry->GetOutermost()->GetName();
            if (!Inventory.Contains(FName(*Package)) || !Package.EndsWith(TEXT("_BuiltData")))
                return Fail(TEXT("The rebuilt lighting does not name a game lighting package."));
            if (!CookObject(Registry, Stage, Error))
                return Fail(Error);
            Result.RebuiltLighting.AddUnique(Package);
            Known.Add(Package);
            if (!Follow(Package, CookedBase(Stage, Package) + TEXT(".uasset")))
                return Fail(Error);
        }
        // Each added level is cooked with its own lighting data, which it names.
        for (int32 Index = 0; Index < LitLevels.Num(); ++Index)
        {
            FP4AddedLitLevel& Lit = LitLevels[Index];
            const FString Level = Lit.World->GetOutermost()->GetName(), Data = Lit.Registry->GetOutermost()->GetName();
            if (Inventory.Contains(FName(*Data)) ||
                !Data.StartsWith(Names.PackageRoot + TEXT("/"), ESearchCase::CaseSensitive))
                return Fail(TEXT("The lighting data of your added meshes is not in this mod's folder."));
            P4PrepareAddedLightingForCook(Lit.Registry);
            if (!CookObject(Lit.Registry, Stage, Error))
                return Fail(Error);
            Known.Add(Data);
            Known.Add(Level);
            if (Materials->HasMaterials() && !Materials->Remap(Lit.World, Error, false))
                return Fail(Error);
            if (!CookObject(Lit.World, Stage, Error))
                return Fail(Error);
            if (!Follow(Data, CookedBase(Stage, Data) + TEXT(".uasset")) ||
                !Follow(Level, CookedBase(Stage, Level) + TEXT(".umap")))
                return Fail(Error);
            Maps[LitMaps[Index]].Levels.push_back(TCHAR_TO_UTF8(*Level));
            Result.Levels.Add(Level);
            Result.BakedLighting.Add(Data);
            Result.BakedActors.Append(Lit.LitActors);
            Result.BakedVolumesTakenOut += Lit.VolumeTakenOut ? 1 : 0;
        }
        for (FP4AddedLitLevel& Lit : LitLevels)
            P4DiscardAddedLitLevel(Lit);
        LitLevels.Reset();
    }
    return true;
}

bool FP4ExportRun::CookPlacementLists()
{
    // The placement list and the list asset holding it, cooked into the mod's container.
    for (const auto& List : PlacementLists)
    {
        const FP4PlacementList& Placements = *List;
        if (!Placements.Asset.IsValid())
            continue;
        for (UObject* Asset : {Placements.Asset.Get(), Placements.ListAsset.Get()})
        {
            const FString Package = Asset->GetOutermost()->GetName();
            if (!CookObject(Asset, Stage, Error) || !Follow(Package, CookedBase(Stage, Package) + TEXT(".uasset")))
                return Fail(Error);
        }
        Result.Placements.Add(Placements.Package);
        if (!Result.PlacementMark)
            Result.PlacementMark = Placements.MarkId;
        Result.PlacementRows.Append(Placements.Rows);
    }
    return true;
}

// What the editor draws and the game does not: the editor turns a usage on as soon as a material is put on a mesh, and
// the game checks its own cooked master. Only what the creator added or changed is looked at.
bool FP4ExportRun::TellGameMaterialUsage()
{
    FP4KadanImportSettings Containers;
    Containers.GameFolder = Request.ExtractedGameDirectory;
    Containers.GamePaks = Request.GamePaksDirectory;
    Containers.Retoc = Request.Tools.Retoc;
    if (Containers.GamePaks.IsEmpty() || Containers.Retoc.IsEmpty())
        return true;
    FP4GameMaterialUsage GameUsage(Containers);
    for (const FLevelEdit& Edit : Edits)
    {
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(Edit.World);
        for (const FString& Name : Changes.Added)
            GameUsage.WhyDefaultMaterial(FindObject<AActor>(Edit.World->PersistentLevel, *Name), false, Result.Told);
        // A game actor's static mesh keeps the lighting the game baked for it.
        for (const FP4LevelActorChanges& Changed : Changes.OtherChanged)
            GameUsage.WhyDefaultMaterial(FindObject<AActor>(Edit.World->PersistentLevel, *Changed.Actor), true, Result.Told);
    }
    return true;
}

bool FP4ExportRun::CookNativeTables()
{
    if (!Request.Native)
        return true;
    TArray<FString> Changed;
    Tables->Values.GetKeys(Changed);
    for (const FString& Path : Changed)
    {
        UObject* Table = LoadObject<UObject>(nullptr, *Path);
        FString Base;
        if (!Table || !P4CookGameAssetWhole(Table, Stage, Base, Error) || !Follow(Table->GetOutermost()->GetName(), Base + TEXT(".uasset")))
            return Fail(Error.IsEmpty() ? TEXT("An edited table could not be cooked whole: ") + Path : Error);
        const FString Package = Table->GetOutermost()->GetName();
        Result.Replacements.Add(Package);
        Result.ReplacementFiles.Add(FString());
        Result.ReplacementKinds.Add(TEXT("edited-game-table"));
        Result.ReplacementClasses.Add(Table->GetClass()->GetPathName());
        Result.ReplacementGeneratedClasses.Add(FString());
        Result.ReplacementNativeParents.Add(FString());
        Result.Told.Add(FString::Printf(
            TEXT("%s is shipped whole at the game's path. Another mod that edits this table replaces it."),
            *FPackageName::GetShortName(Package)));
    }
    Tables->Values.Empty();
    return true;
}

bool FP4ExportRun::CookWholeLevels()
{
    for (const FString& Package : WholeLevels)
    {
        if (!P4ReportExportStage(EP4ExportStage::Cooking, Package, Error))
            return Fail(Error);
        UWorld* World = LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Package));
        if (!World || !Materials->Remap(World, Error, false))
            return Fail(Error.IsEmpty() ? TEXT("Selected original World could not be loaded") : Error);
        const FString Base = CookedBase(Stage, Package);
        if (!P4CookWholeOriginalLevel(World, Base, Error) || !Follow(Package, Base + TEXT(".umap")))
            return Fail(Error);
        Result.Replacements.Add(Package);
        Result.ReplacementFiles.Add(FString());
        Result.ReplacementKinds.Add(TEXT("edited-game-level"));
        Result.ReplacementClasses.Add(World->GetClass()->GetPathName());
        Result.ReplacementGeneratedClasses.Add(FString());
        Result.ReplacementNativeParents.Add(FString());
        Result.Told.Add(FString::Printf(
            TEXT("%s is shipped whole at the game's path. Another mod that edits this level replaces it."),
            *FPackageName::GetShortName(Package)));
    }
    return true;
}

bool FP4ExportRun::CookOwnPackages()
{
    // The creator's own packages: the selection's and every one a cooked package names, followed until
    // everything named is the game's or cooked here.
    // A level of the creator's own joins the map the export names, or the only edited map.
    auto CreatorLevelMap = [&]() -> P4MapKit::Export::MapEdit&
    {
        const std::string Package = Request.MapPackage.IsEmpty() && Maps.size() == 1
            ? Maps[0].Package
            : std::string(TCHAR_TO_UTF8(*Request.MapPackage));
        for (auto& Each : Maps)
            if (Each.Package == Package)
                return Each;
        Maps.emplace_back();
        Maps.back().Package = Package;
        return Maps.back();
    };
    TArray<FString> CreatorLevels;
    for (int32 Index = 0; Index < Own.Num(); ++Index)
    {
        const FString Package = Own[Index];
        if (!P4ReportExportStage(EP4ExportStage::Cooking, Package, Error))
        {
            return Fail(Error);
        }
        UObject* Asset = LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package));
        FP4ExportAvailability Availability;
        if (Materials->IsRuntimeAsset(Asset))
            Availability.Route = EP4ExportAssetRoute::CreatorMaterialInstance;
        else
            Availability = AssetAvailability(Asset, Request.Mod, CustomMaterials);
        if (!Materials->IsRuntimeAsset(Asset) && !Materials->Remap(Asset, Error))
            return Fail(Error);
        bool Cooked = false;
        switch (Availability.Route)
        {
            case EP4ExportAssetRoute::CreatorSound:
                if (!SoundSources.Contains(Package))
                    return Fail(Package +
                        TEXT(
                            " is a source sound used by a cooked package. Play the sound bank through Tsuchi instead."));
                Sounds.Add(CastChecked<USoundWave>(Asset));
                continue;
            case EP4ExportAssetRoute::CreatorTexture:
                Cooked = CookTexture(CastChecked<UTexture2D>(Asset), Stage, Error);
                Textures.Add(CastChecked<UTexture2D>(Asset));
                break;
            case EP4ExportAssetRoute::CreatorStaticMesh:
            case EP4ExportAssetRoute::CreatorBlueprint:
            case EP4ExportAssetRoute::GameActorBlueprint:
            case EP4ExportAssetRoute::CreatorSkinned:
            case EP4ExportAssetRoute::CreatorMaterialInstance:
                // A runtime copy of a creator material is cooked with no shader map of the editor's, then Kadan's own goes in.
                if (Materials->IsRuntimeAsset(Asset))
                    Materials->BeginCook(Asset, CookTarget());
                Cooked = CookObject(Asset, Stage, Error) &&
                    (!Materials->IsRuntimeAsset(Asset) ||
                        Materials->WriteCookedBlock(Asset, CookedBase(Stage, Package), Error));
                break;
            case EP4ExportAssetRoute::CreatorLighting:
                // Save the registry with its native light GUID/channel records and
                // light/shadow textures in the same package, rather than reimporting it.
                Cooked = CookObject(Asset, Stage, Error);
                if (Cooked)
                    Result.BakedLighting.AddUnique(Package);
                break;
            case EP4ExportAssetRoute::PreviewObject:
                // Derived light/shadow textures can be subclasses or share a
                // registry package. Preserve native cooking without claiming the
                // ordinary single-export Texture2D readback contract for them.
                if (P4IsGamePreviewDerivedCopy(Asset) && Asset->IsA<UTexture2D>())
                    Cooked = CookTexture(CastChecked<UTexture2D>(Asset), Stage, Error);
                else
                    Cooked = CookObject(Asset, Stage, Error);
                break;
            case EP4ExportAssetRoute::CreatorLevel:
                Cooked = CookObject(Asset, Stage, Error);
                if (Cooked)
                {
                    CreatorLevels.AddUnique(Package);
                    Result.Levels.AddUnique(Package);
                }
                break;
            default:
                return Fail(Package + TEXT(" cannot be exported. ") + Availability.Reason);
        }
        if (!Cooked ||
            !Follow(Package,
                CookedBase(Stage, Package) +
                    (Availability.Route == EP4ExportAssetRoute::CreatorLevel ? TEXT(".umap") : TEXT(".uasset"))))
            return Fail(Error);
        FP4GameReplacement Replaced;
        if (P4IsGameReplacement(Asset, &Replaced) && !Result.Replacements.Contains(Package))
        {
            Result.Replacements.Add(Package);
            Result.ReplacementFiles.Add(FPaths::GetCleanFilename(Replaced.File));
            Result.ReplacementKinds.Add(Replaced.Kind);
            Result.ReplacementClasses.Add(Replaced.Class);
            Result.ReplacementGeneratedClasses.Add(Replaced.GeneratedClass);
            Result.ReplacementNativeParents.Add(Replaced.NativeParent);
            Result.Told.Add(
                FString::Printf(TEXT("%s is shipped whole at the game's path."), *FPackageName::GetShortName(Package)));
        }
    }
    if (Request.PreviewOnly)
        return true;
    TArray<FString> Roots;
    if (!CreatorRootLevels(CreatorLevels, Roots))
        return false;
    for (const FString& Level : Roots)
        CreatorLevelMap().Levels.push_back(TCHAR_TO_UTF8(*Level));
    return true;
}

bool FP4ExportRun::CreatorRootLevels(const TArray<FString>& Levels, TArray<FString>& Roots)
{
    // A level of the creator's that another of them streams is loaded by that level, as the engine does, so
    // only the levels nothing streams are the map's. Tsuchi refuses a level that comes twice.
    TSet<FString> Streamed;
    for (const FString& Package : Levels)
    {
        const UWorld* World = LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!World)
            return Fail(Package + TEXT(" could not be loaded again after it was cooked."));
        for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
            if (Streaming)
                Streamed.Add(Streaming->GetWorldAssetPackageName());
    }
    for (const FString& Package : Levels)
        if (!Streamed.Contains(Package))
            Roots.Add(Package);
    if (Levels.Num() && !Roots.Num())
        return Fail(TEXT("Your levels stream each other in a loop, so none of them can be the one the map loads."));
    return true;
}

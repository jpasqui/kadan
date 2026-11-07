// Importing a game level: read, build, cook and compare with the game's file.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4OriginalBuiltLighting.h"
#include "P4OriginalNavigationMesh.h"

UWorld* P4CreateOriginalImportWorld(FName Name, UPackage* Package)
{
    if (!Package || !IsInGameThread())
        return nullptr;
    // UE4.26's CreateWorld always creates a physics scene, even for Inactive worlds. Use the same
    // engine initialization path with only transient collision processing left out. Applying the
    // source's scale/BodyInstance properties must not update PhysX bodies made from partial data.
    if (Package != GetTransientPackage())
        Package->ThisContainsMap();
    const FName WorldName = Name.IsNone() ? FName(TEXT("Untitled")) : Name;
    // Source/dependency reads may load a package between preflight and construction. NewObject
    // must never recycle an existing world and leave its old actors under a newly initialized level.
    if (StaticFindObjectFast(nullptr, Package, WorldName))
        return nullptr;
    UWorld* World = NewObject<UWorld>(Package, WorldName);
    World->SetFlags(RF_Transactional);
    World->WorldType = EWorldType::Inactive;
    World->FeatureLevel = GMaxRHIFeatureLevel;
    World->InitializeNewWorld(UWorld::InitializationValues()
            .ShouldSimulatePhysics(false)
            .CreatePhysicsScene(false)
            .EnableTraceCollision(false)
            .CreateNavigation(false)
            .CreateAISystem(false));
    Package->SetDirtyFlag(false);
    return World;
}

namespace P4LevelContent
{
TArray<FP4OriginalValueNote> ValueNotes(const TArray<FString>& NotHeld, const TArray<FString>& NotShipped)
{
    TArray<FP4OriginalValueNote> Out;
    const auto Add = [&](const FString& Line, const TCHAR* Kind, const TCHAR* Action)
    {
        FP4OriginalValueNote N;
        N.Kind = Kind;
        N.Action = Action;
        int32 Equals = Line.Find(TEXT(" = "), ESearchCase::CaseSensitive);
        FString Where = Equals == INDEX_NONE ? Line : Line.Left(Equals);
        N.GameValue = Equals == INDEX_NONE ? TEXT("/Engine/UnknownPackage.UnknownExport") : Line.Mid(Equals + 3);
        int32 Space = INDEX_NONE;
        if (Where.FindChar(TEXT(' '), Space))
        {
            N.Object = Where.Left(Space);
            N.Property = Where.Mid(Space + 1);
        }
        else
            N.Object = Where;
        Out.Add(N);
    };
    for (const FString& Line : NotHeld)
        Add(Line, TEXT("unsupported-value"), TEXT("kept-for-reference"));
    for (const FString& Line : NotShipped)
        Add(Line, TEXT("unshipped-reference"), TEXT("set-null"));
    return Out;
}

FP4OriginalLevelImportResult ImportNative(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk, bool OwnsReimportSlot)
{
    FP4OriginalLevelImportResult Result;
    const auto Refuse = [&](EP4OriginalLevelImportError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Refuse(EP4OriginalLevelImportError::EditorThreadRequired, TEXT("level-import-requires-editor-thread"));
    FFile Game;
    FString Error;
    FBaseline Baseline;
    {
        TArray<uint8> Header, Body;
        if (!FFileHelper::LoadFileToArray(Header, *HeaderFile, FILEREAD_Silent) ||
            !FFileHelper::LoadFileToArray(Body,
                *(BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile),
                FILEREAD_Silent) ||
            !P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Baseline.Source))
            return Refuse(EP4OriginalLevelImportError::SourceUnreadable, TEXT("level-import-source-unreadable"));
        Game.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
        Game.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    }
    if (!Read(Game, FString(), Error))
        return Refuse(Game.UnsupportedScript ? EP4OriginalLevelImportError::UnsupportedContent
                                             : EP4OriginalLevelImportError::DecodeFailed,
            Error);
    const FString Package = Game.PackageName, Short = FPackageName::GetLongPackageAssetName(Game.PackageName);
    Result.Package = Package;
    Result.AssetPath = Package + TEXT(".") + Short;
    Result.Objects = Game.Nodes.Num();
    Result.IdentityKnown = true;
    if (!P4IsValidContentPackage(Package) || !P4SameText(Game.Node(Game.World).Name, Short))
        return Refuse(EP4OriginalLevelImportError::InvalidGamePath, TEXT("level-import-game-path-unusable"));
    // Nothing the game's file holds may be dropped: what is not brought over yet refuses the level.
    const FString Why = Refusal(Game);
    if (!Why.IsEmpty())
        return Refuse(EP4OriginalLevelImportError::UnsupportedContent, Why);
    Baseline.Package = Package;

    const bool OnDisk = !OwnsReimportSlot && FPackageName::DoesPackageExist(Package);
    UPackage* Loaded = FindPackage(nullptr, *Package);
    if (!Loaded && OnDisk)
        Loaded = LoadPackage(nullptr, *Package, LOAD_NoWarn | LOAD_Quiet);
    UWorld* World = Loaded ? UWorld::FindWorldInPackage(Loaded) : nullptr;
    if (World && !IsValid(World))
        World = nullptr;
    if (World)
    {
        if (World->GetOutermost()->GetMetaData()->HasValue(World, GameBackedKey))
            return Refuse(EP4OriginalLevelImportError::EarlierImportReplacementUnsupported,
                TEXT("level-import-complete-native-source-required; an existing partial layer was preserved"));
        FBaseline Earlier;
        if (!ReadBaseline(World, Earlier))
            return Refuse(
                EP4OriginalLevelImportError::OccupiedDestination, TEXT("level-import-destination-holds-another-asset"));
        const FP4OriginalStatus Status = P4QueryOriginalAsset(World);
        FString MigrationError;
        const bool Untouched = Status.IsUnchanged() ||
            ((Status.State == EP4OriginalState::NeedsMigration || Status.State == EP4OriginalState::Recorded) &&
                P4MigrateOriginalRecord(
                    World, [&]() { return P4SameText(State(World), Earlier.State); }, MigrationError));
        if (!Untouched)
            return Refuse(Status.State == EP4OriginalState::NeedsMigration
                    ? EP4OriginalLevelImportError::RecordValidationRequired
                    : EP4OriginalLevelImportError::EditedOriginal,
                Status.State == EP4OriginalState::NeedsMigration
                    ? TEXT("level-import-record-needs-validation: ") + MigrationError
                    : FString(TEXT("level-import-would-replace-an-edited-level")));
        if (!P4SameText(Earlier.Source, Baseline.Source))
            return Refuse(EP4OriginalLevelImportError::EarlierImportReplacementUnsupported,
                TEXT("level-import-replacing-an-earlier-import-is-not-done-yet"));
        Baseline = Earlier;
    }
    else
    {
        TArray<UObject*> Inside;
        if (Loaded)
            GetObjectsWithOuter(Loaded, Inside, false);
        Inside.RemoveAll([](const UObject* Object) { return !IsValid(Object) || Object->IsA<UMetaData>(); });
        if (OnDisk || Inside.Num() != 0)
            return Refuse(
                EP4OriginalLevelImportError::OccupiedDestination, TEXT("level-import-destination-holds-another-asset"));
        TArray<FString> NotHeld, NotShipped, NotInGame, ActorsNotKept, RunLines;
        // The folder the game's packages are under, from where the level's own file is.
        FString Root = HeaderFile;
        FPaths::NormalizeFilename(Root);
        {
            const FString Tail = P4ContentRelativePath(Package) + TEXT(".umap");
            Root = Root.EndsWith(Tail, ESearchCase::CaseSensitive) ? Root.LeftChop(Tail.Len()) : FString();
        }
        // A level says only what differs from its classes. Which of them do not say what the game's
        // own files of the classes say is asked first, and told. Those are given what the game's files
        // say while the level is made and compared.
        Game.SourceRoot = Root;
        int32 Asked = 0;
        TArray<FString> Differing;
        FLentTemplates Lent;
        const TArray<FString> ClassLines = ClassesThatDiffer(Game, Root, Asked, &Differing);
        {
            // A class after the class it comes from.
            const auto Depth = [](const FString& Name)
            {
                int32 Count = 0;
                const UClass* Class = LoadObject<UClass>(
                    nullptr, *P4ClassPathOf(Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
                for (; Class; Class = Class->GetSuperClass())
                    ++Count;
                return Count;
            };
            TArray<FString> Ordered = Differing;
            Ordered.StableSort([&](const FString& A, const FString& B) { return Depth(A) < Depth(B); });
            for (const FString& Name : Ordered)
                Lent.Lend(Name, Root);
            TArray<FString> Told, Given, NotGiven;
            for (const FString& Line : ClassLines)
                Told.Add(Line);
            for (const FString& Name : Lent.Classes)
                Given.Add(Name);
            for (const FString& Line : Lent.Problems)
                NotGiven.Add(Line);
            Result.ClassesCompared = Asked;
            Result.ClassesKnown = true;
            Result.DifferingClasses = Told;
            Result.LentClasses = Given;
            Result.ClassProblems = NotGiven;
        }
        World = Build(Game, NotHeld, NotShipped, NotInGame, Error);
        if (!World)
            return Refuse(EP4OriginalLevelImportError::BuildFailed, Error);
        FP4OriginalComponentReferenceScope ImportComponentReferences(World);
        {
            TArray<FString> Told, Extra;
            for (const FString& Value : NotShipped)
                Told.Add(Value);
            Result.UnshippedReferences = Told;
            Result.BuildDetailsKnown = true;
            for (const FString& Value : NotInGame)
                Extra.Add(Value);
            Result.ProjectOnlyObjects = Extra;
        }
        // What was made is saved the way a cook saves it and compared with the game's file.
        FFile Mine;
        TArray<FString> GameSays, GameEngine, MineSays, MineEngine;
        if (!Cooked(World, true, Mine, Error, FString(), &Game))
        {
            Discard(World);
            return Refuse(EP4OriginalLevelImportError::CookFailed, Error);
        }
        // Values this engine has no place for are not in the level. They are kept beside it, in the
        // level's own record, and told. Everything else the game's file says is compared.
        {
            FFile Compared = Game;
            Without(Compared, NotHeld);
            RedundantUnshippedNulls(Compared, Mine, Root, NotShipped, World);
            Result.UnshippedReferences = NotShipped;
            Lines(Compared, GameSays, GameEngine);
            Lines(Mine, MineSays, MineEngine);
            TArray<FString> Told;
            for (const FString& Value : NotHeld)
                Told.Add(Value);
            Result.ValuesNotHeld = Told;
            Result.ValuesKnown = true;
        }
        TArray<FString> MineAll = MineSays;
        // Preserve the recorded state format. Only the cross-engine comparison
        // folds object paths and typed FNames; serialized strings stay exact.
        {
            FFile Compared = Game;
            Without(Compared, NotHeld);
            RedundantUnshippedNulls(Compared, Mine, Root, NotShipped, World);
            GameSays.Reset();
            GameEngine.Reset();
            MineSays.Reset();
            MineEngine.Reset();
            Lines(Compared, GameSays, GameEngine, nullptr, true);
            Lines(Mine, MineSays, MineEngine, nullptr, true);
        }
        // An object the game makes from an empty template of its own class, and the project from the
        // parent class's template, is made from the same in effect. The game's own files say so.
        {
            const FString Mark = FString(TEXT(" | ")) + MadeFrom;
            TMap<FString, FString> Froms, Known;
            int32 Proven = 0;
            for (const FString& Line : MineSays)
            {
                const int32 At = Line.Find(Mark, ESearchCase::CaseSensitive);
                if (At != INDEX_NONE)
                    Froms.Add(Line.Left(At), Line.Mid(At + Mark.Len()));
            }
            for (FString& Line : GameSays)
            {
                const int32 At = Line.Find(Mark, ESearchCase::CaseSensitive);
                const FString* Other = At != INDEX_NONE ? Froms.Find(Line.Left(At)) : nullptr;
                if (!Other || P4SameText(*Other, Line.Mid(At + Mark.Len())) ||
                    !SameInEffect(Root, Line.Mid(At + Mark.Len()), *Other, Known))
                    continue;
                Line = Line.Left(At) + Mark + *Other;
                ++Proven;
            }
            Result.EquivalentParentTemplates = Proven;
            Result.TemplatesKnown = true;
        }
        TSet<FString, FLocKeySetFuncs> InGame(GameSays), InMine(MineSays);
        TArray<FString> OnlyGame, OnlyMine;
        int32 Differences = 0;
        FString First;
        for (const FString& Line : GameSays)
            if (!InMine.Contains(Line))
            {
                if (First.IsEmpty())
                    First = TEXT("the game's file says ") + Line;
                if (OnlyGame.Num() < 400)
                    OnlyGame.Add(Line);
                ++Differences;
            }
        for (const FString& Line : MineSays)
            if (!InGame.Contains(Line))
            {
                if (First.IsEmpty())
                    First = TEXT("the level made says ") + Line;
                if (OnlyMine.Num() < 400)
                    OnlyMine.Add(Line);
                ++Differences;
            }
        if (Differences)
        {
            Result.Differences = Differences;
            Result.DifferencesKnown = true;
            Result.OnlyGameSays = OnlyGame;
            Result.OnlyLevelSays = OnlyMine;
#if P4_KADAN_TEST_SEAMS
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REFERENCE_DIAGNOSTIC")) == TEXT("1"))
            {
                TArray<TSharedPtr<FJsonValue>> References;
                for (const FNode& Node : Game.Nodes)
                {
                    UObject* Live = StaticFindObject(UObject::StaticClass(), World->GetOutermost(), *Node.Path);
                    if (!Live)
                        continue;
                    for (const auto& Field : Node.Values.Fields)
                    {
                        const FObjectProperty* Property = FindFProperty<FObjectProperty>(
                            Live->GetClass(), *Text(Field.first));
                        if (!Property || Field.second.Kind != FDecodedValue::EKind::String ||
                            Field.second.Text.empty() || Field.second.Text[0] == '/' || References.Num() >= 128)
                            continue;
                        const FString Prefix = Node.Path.ToLower() + TEXT(" | ") + Text(Field.first).ToLower() +
                            TEXT(" = ");
                        if (!OnlyGame.ContainsByPredicate([&](const FString& Line)
                                { return Line.StartsWith(Prefix, ESearchCase::CaseSensitive); }))
                            continue;
                        const auto Reference = [&](const UObject* Owner)
                        {
                            const UObject* Value = Owner ? Property->GetObjectPropertyValue_InContainer(Owner)
                                                         : nullptr;
                            return Value ? Value->GetPathName() : TEXT("None");
                        };
                        auto Row = MakeShared<FJsonObject>();
                        Row->SetStringField(TEXT("object"), Live->GetPathName());
                        Row->SetStringField(TEXT("property"), Property->GetName());
                        Row->SetStringField(TEXT("source_value"), Text(Field.second.Text));
                        Row->SetStringField(TEXT("live_value"), Reference(Live));
                        Row->SetStringField(TEXT("archetype_value"), Reference(Live->GetArchetype()));
                        Row->SetStringField(
                            TEXT("class_default_value"), Reference(Live->GetClass()->GetDefaultObject()));
                        Row->SetStringField(
                            TEXT("flags"), FString::Printf(TEXT("%016llx"), Property->GetPropertyFlags()));
                        Row->SetBoolField(TEXT("identical_to_archetype"),
                            Property->Identical_InContainer(Live, Live->GetArchetype()));
                        References.Add(MakeShared<FJsonValueObject>(Row));
                    }
                }
                auto Report = MakeShared<FJsonObject>();
                Report->SetArrayField(TEXT("references"), References);
                FString Json;
                FJsonSerializer::Serialize(Report, TJsonWriterFactory<TCHAR>::Create(&Json));
                const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StrictNative");
                IFileManager::Get().MakeDirectory(*Folder, true);
                FFileHelper::SaveStringToFile(Json, *(Folder / (Short + TEXT(".references.json"))),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            }
#endif
            Discard(World);
            return Refuse(
                EP4OriginalLevelImportError::CookDiffers, TEXT("level-import-cook-differs-from-game: ") + First);
        }
        // What the engine makes for itself is not compared, and is told.
        TArray<FString> Told;
        TSet<FString, FLocKeySetFuncs> MineHas(MineEngine);
        for (const FString& Line : GameEngine)
            if (!MineHas.Contains(Line))
                Told.Add(Line);
        Result.EngineMadeNotCompared = Told;
        Result.ComparedKnown = true;
        Result.Compared = GameSays.Num();
        // The classes get back what they had. From here on the level says for itself what the game's
        // classes gave its objects, so what it says is read again.
        if (Lent.Kept.Num())
        {
            Lent.Return();
            FFile Again;
            TArray<FString> AgainEngine;
            if (!Cooked(World, true, Again, Error, FString(), &Game))
            {
                Discard(World);
                return Refuse(EP4OriginalLevelImportError::CookFailed, Error);
            }
            MineAll.Reset();
            Lines(Again, MineAll, AgainEngine);
        }
        // The engine runs an actor's construction script again when the level loads and when the actor
        // is moved, and such a run keeps on a Blueprint's components only what can be edited there.
        // What the game's own code puts there besides (which component is the mesh, what a script
        // changed) this editor cannot keep. So the scripts are run now, the level is taken as it is
        // after that, and what the run did not keep is told.
        {
            TArray<AActor*> All = World->PersistentLevel->Actors;
            for (AActor* Actor : All)
                if (Actor && !Actor->IsPendingKill() && !Actor->IsChildActor())
                    Actor->RerunConstructionScripts();
            // The game's built lighting as the game's editor held the level after loading it.
            World->GetOutermost()->GetMetaData()->SetValue(World, LightingKey, *Pack(OriginalLightingLines(Game)));
            P4HoldOriginalBuiltLighting(World);
            // And its navigation build's meshes.
            P4HoldOriginalNavigationMeshes(World, Game);
            FFile Settled;
            TArray<FString> SettledSays, SettledEngine;
            if (!Cooked(World, true, Settled, Error, FString(), &Game))
            {
                Discard(World);
                return Refuse(EP4OriginalLevelImportError::CookFailed, Error);
            }
            Lines(Settled, SettledSays, SettledEngine);
            TSet<FString, FLocKeySetFuncs> InSettled(SettledSays);
            TArray<FString> NotKept, Added;
            int32 NotKeptCount = 0, AddedCount = 0;
            TSet<FString, FLocKeySetFuncs> InAll(MineAll);
            for (const FString& Line : MineAll)
                if (!InSettled.Contains(Line))
                {
                    if (NotKept.Num() < 80)
                        NotKept.Add(Line);
                    ++NotKeptCount;
                    const FString Owner = OwnerOf(Line, Game.Node(Game.Level).Path);
                    if (!Owner.IsEmpty())
                        ActorsNotKept.AddUnique(Owner);
                    RunLines.Add(TEXT("-") + Line);
                }
            for (const FString& Line : SettledSays)
                if (!InAll.Contains(Line))
                {
                    if (Added.Num() < 80)
                        Added.Add(Line);
                    ++AddedCount;
                    RunLines.Add(TEXT("+") + Line);
                }
            // What the game's own code made, as the game's file says it.
            for (const FString& Line : GameEngine)
            {
                const int32 Bar = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
                const FString Said = Bar == INDEX_NONE ? FString() : Line.Mid(Bar + 3);
                if (Said.StartsWith(TEXT("c4terraintagcontainer = "), ESearchCase::CaseSensitive) ||
                    Said.StartsWith(TEXT("cachedmaxdrawdistance = "), ESearchCase::CaseSensitive))
                    RunLines.Add(TEXT("g") + Line);
            }
            Result.NotKeptCount = NotKeptCount;
            Result.ConstructionKnown = true;
            Result.NotKept = NotKept;
            Result.AddedCount = AddedCount;
            Result.Added = Added;
            MineSays = MoveTemp(SettledSays);
        }
        // The game reads a gameplay object's options from its row of the map's placement lists, not from the
        // level. Each object is given what its row holds, and that is part of the level as imported, so an
        // object nobody changes exports nothing.
        {
            const FString Lists = P4GamePlacementFolder(Root, Package);
            TArray<FP4GamePlacementRow> Rows;
            FString ListError;
            if (!Lists.IsEmpty() && !P4ReadGamePlacementLists(Lists, Rows, ListError))
                Result.PlacementNotHeld.Add(TEXT("The map's placement lists could not be read: ") + ListError);
            if (Rows.Num())
            {
                const FP4PlacementOptionsReport Placed = P4ApplyGamePlacementOptions(World, Rows);
                Result.PlacementKnown = true;
                Result.PlacementRows = Placed.Rows;
                Result.PlacementValues = Placed.Values;
                Result.PlacementWithoutRow = Placed.WithoutRow;
                Result.PlacementNotHeld.Append(Placed.NotHeld);
                for (const auto& Pair : Placed.NotRead)
                    Result.PlacementNotRead.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key, Pair.Value));
                Result.PlacementNotRead.Sort();
                if (Placed.Changed.Num())
                {
                    // The level runs the objects' construction scripts again when it loads. What a run keeps is
                    // what the level holds.
                    for (AActor* Actor : Placed.Changed)
                        if (IsValid(Actor))
                            Actor->RerunConstructionScripts();
                    for (AActor* Actor : Placed.Changed)
                        if (const FP4GamePlacementRow* Row = IsValid(Actor) ? P4GamePlacementRowOf(Actor, Rows)
                                                                            : nullptr)
                            for (const FString& Line : P4PlacementOptionDifferences(Actor, *Row))
                                if (!Line.Contains(TEXT("not read yet")) && !Line.Contains(TEXT("read in part")))
                                    Result.PlacementNotHeld.AddUnique(Actor->GetName() + TEXT(" ") + Line);
                    FFile Given;
                    TArray<FString> GivenEngine;
                    if (!Cooked(World, true, Given, Error, FString(), &Game))
                    {
                        Discard(World);
                        return Refuse(EP4OriginalLevelImportError::CookFailed, Error);
                    }
                    MineSays.Reset();
                    Lines(Given, MineSays, GivenEngine);
                }
            }
        }
        if (!Digest(MineSays, Baseline.State))
        {
            Discard(World);
            return Refuse(EP4OriginalLevelImportError::StateUnreadable, TEXT("level-import-state-unreadable"));
        }
        World->GetOutermost()->GetMetaData()->SetValue(World, BaselineKey, *BaselineText(Baseline));
        World->GetOutermost()->GetMetaData()->SetValue(World, ImportedKey, *Pack(MineSays));
        {
            // The order of the game's post-process tags, read from the game's file, for cooks of the level
            // that are not given that file.
            FString Order, OrderError;
            if (!P4RecoveredPostProcessSourceOrder(Game.Header, Game.Body, Baseline.Source, Order, OrderError))
            {
                Discard(World);
                return Refuse(EP4OriginalLevelImportError::RecordFailed, OrderError);
            }
            if (Order.IsEmpty())
                World->GetOutermost()->GetMetaData()->RemoveValue(World, PostProcessOrderKey);
            else
                World->GetOutermost()->GetMetaData()->SetValue(World, PostProcessOrderKey, *Order);
        }
        {
            ActorsNotKept.Sort();
            World->GetOutermost()->GetMetaData()->SetValue(
                World, ActorsNotKeptKey, *FString::Join(ActorsNotKept, TEXT("\n")));
            World->GetOutermost()->GetMetaData()->SetValue(World, RunLinesKey, *Pack(RunLines));
        }
        if (NotHeld.Num())
            World->GetOutermost()->GetMetaData()->SetValue(World, NotHeldKey, *FString::Join(NotHeld, TEXT("\n")));
        if (Differing.Num())
            World->GetOutermost()->GetMetaData()->SetValue(
                World, ClassesNotTheGamesKey, *FString::Join(Differing, TEXT("\n")));
        TArray<FP4OriginalValueNote> Notes = ValueNotes(NotHeld, NotShipped);
        if (Game.ProvenEmptyBeginPlay)
        {
            FP4OriginalValueNote Frame;
            Frame.Kind = TEXT("compiler-frame");
            Frame.Object = Game.Node(Game.ScriptDefault).Path;
            Frame.Property = TEXT("UberGraphFrame");
            Frame.Action = TEXT("rebuilt-by-engine");
            for (const auto& Tag : Game.Node(Game.ScriptDefault).Properties.Properties)
                if (Tag.Name == "UberGraphFrame")
                {
                    for (std::size_t Byte = 0; Byte < Tag.Size; ++Byte)
                        Frame.GameValue += FString::Printf(TEXT("%02x"), Game.Body[Tag.PayloadOffset + Byte]);
                }
            Notes.Add(MoveTemp(Frame));
        }
        for (const FString& Line : RunLines)
        {
            FP4OriginalValueNote N;
            N.Kind = TEXT("construction-script");
            N.Object = World->GetPathName();
            N.GameValue = Line.Mid(1);
            N.Action = Line.StartsWith(TEXT("-")) ? TEXT("dropped")
                : Line.StartsWith(TEXT("+"))      ? TEXT("engine-added")
                                                  : TEXT("game-code-only");
            Notes.Add(N);
        }
        if (!P4MarkLegacyOriginalAsset(World, Notes, Error))
        {
            Discard(World);
            return Refuse(EP4OriginalLevelImportError::RecordFailed, Error);
        }
        FAssetRegistryModule::AssetCreated(World);
        World->MarkPackageDirty();
        Result.Created = true;
    }
    Result.SourceDigest = Baseline.Source;
    Result.ImportedState = Baseline.State;
    Result.State = Baseline.State;
    Result.BaselineKnown = true;
    if (SaveToDisk && (Result.Created || World->GetOutermost()->IsDirty() || !OnDisk))
    {
        FString SaveError;
        if (!P4OwnStockEngineFields(World->GetOutermost(), SaveError))
        {
            if (Result.Created)
                Discard(World);
            return Refuse(EP4OriginalLevelImportError::SaveFailed, TEXT("level-import-save-refused: ") + SaveError);
        }
        if (!P4SaveAssetPackage(World, [&]() { return P4SameText(State(World), Baseline.State); }, SaveError))
        {
            // A level this import made is taken out again, like every other refusal's, so that nothing
            // of it stays for the garbage collector to find half made.
            if (Result.Created)
                Discard(World);
            return Refuse(EP4OriginalLevelImportError::SaveFailed, TEXT("level-import-save-refused: ") + SaveError);
        }
        Result.Saved = true;
    }
    Result.Asset = World;
    return Result;
}
} // namespace P4LevelContent

FString P4ImportOriginalLevel(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    return P4OriginalLevelImportJson(P4LevelContent::ImportNative(HeaderFile, BodyFile, SaveToDisk));
}

FP4OriginalLevelImportResult P4ImportOriginalLevelNative(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    return P4LevelContent::ImportNative(HeaderFile, BodyFile, SaveToDisk);
}

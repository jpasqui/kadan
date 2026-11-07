// The game-backed level import: an editor projection of a game level whose runtime content stays
// the game's own layers.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4OriginalBuiltLighting.h"
#include "P4OriginalNavigationMesh.h"

namespace P4LevelContent
{
// The game-backed import is a distinct editor projection,
// not a complete source import; original game layers remain the runtime content.
bool ProjectionFinite(const FDecodedValue& Value)
{
    if (Value.Kind == FDecodedValue::EKind::Number && !FMath::IsFinite(FCString::Atod(*Text(Value.Text))))
        return false;
    for (const auto& Field : Value.Fields)
        if (!ProjectionFinite(Field.second))
            return false;
    for (const auto& Item : Value.Items)
        if (!ProjectionFinite(Item))
            return false;
    return true;
}

KADAN_API TSet<int32> ProjectionUnsafeAttachmentNodes(const TMap<int32, int32>& Parents)
{
    TSet<int32> Unsafe, Finished;
    for (const auto& Edge : Parents)
    {
        TArray<int32> Walk;
        TSet<int32> Seen;
        int32 At = Edge.Key;
        while (Parents.Contains(At) && !Finished.Contains(At) && !Seen.Contains(At))
        {
            Seen.Add(At);
            Walk.Add(At);
            At = Parents[At];
        }
        const bool Cyclic = Seen.Contains(At) || Unsafe.Contains(At);
        for (const int32 Node : Walk)
        {
            Finished.Add(Node);
            if (Cyclic)
                Unsafe.Add(Node);
        }
    }
    return Unsafe;
}

KADAN_API bool ProjectionClassHasProvenDefaults(const UClass* Class, const TSet<FName>& Declined)
{
    if (!Class)
        return false;
    for (const UClass* Parent = Class; Parent; Parent = Parent->GetSuperClass())
        if (Declined.Contains(Parent->GetOutermost()->GetFName()))
            return false;
    return true;
}

KADAN_API bool ProjectionFamilyEqual(
    const TArray<FString>& Before, const TArray<FString>& After, const FString& Level, const FString& Name)
{
    TSet<FString, FLocKeySetFuncs> First, Second;
    for (const FString& Line : Before)
        if (OwnerOf(Line, Level) == Name)
            First.Add(Line);
    for (const FString& Line : After)
        if (OwnerOf(Line, Level) == Name)
            Second.Add(Line);
    return First.Num() != 0 && First.Difference(Second).Num() == 0 && Second.Difference(First).Num() == 0;
}

KADAN_API bool ProjectionRetireUnexpectedActors(UWorld* World, const TSet<FName>& Admitted, FString& Error)
{
    ULevel* Level = World ? World->PersistentLevel : nullptr;
    // An initialized inactive world is reused by the editor without InitWorld's brush repair.
    // Map_Load unconditionally reads GetDefaultBrush()->Brush, and GetDefaultBrush requires
    // the engine builder brush at Actors[1]. This infrastructure is absent from source actors
    // and SetNotForClientOrServer does not make ABrush::IsEditorOnly return true.
    ABrush* Builder = Level && Level->Actors.Num() > 1 ? Cast<ABrush>(Level->Actors[1]) : nullptr;
    if (!IsValid(Builder) || !Builder->IsStaticBrush() || Builder->BrushType != Brush_Default ||
        !Builder->GetBrushComponent() || !Builder->Brush || Builder->GetBrushComponent()->Brush != Builder->Brush)
    {
        Error = TEXT("game-backed-editor-builder-brush-unavailable");
        return false;
    }
    for (AActor* Actor : TArray<AActor*>(Level->Actors))
        if (Actor && !Actor->IsPendingKill() && Actor != Level->GetWorldSettings() && Actor != Builder &&
            !Actor->IsEditorOnly() && !Admitted.Contains(Actor->GetFName()))
            World->DestroyActor(Actor);
    if (Level->Actors.Num() < 2 || Level->Actors[1] != Builder || !IsValid(Builder))
    {
        Error = TEXT("game-backed-editor-builder-brush-retired");
        return false;
    }
    return true;
}

KADAN_API bool ProjectionNavigationIsGameBacked(const UClass* Class)
{
    // Editor NavigationSystem removes source navigation actors whose agent profile
    // differs from this SDK project during ordinary load. Stock game navigation is
    // preserved by overlay export, so these actual native actor families never enter
    // an editable projection baseline in the first place.
    return Class && Class->IsChildOf(ANavigationData::StaticClass());
}

KADAN_API bool ProjectionNavigationBoundsClass(const UClass* Class)
{
    return Class && Class->IsChildOf(ANavMeshBoundsVolume::StaticClass());
}

KADAN_API bool ProjectionSupportedBrushClass(const UClass* Class)
{
    // These native map volumes use the same source polygon reconstruction as
    // navigation bounds. Admission still requires a complete finite model and
    // exact actor-family comparison; no default or fitted brush is substituted.
    return ProjectionNavigationBoundsClass(Class) || Class == ANavModifierVolume::StaticClass() ||
        Class == APrecomputedVisibilityVolume::StaticClass() || Class == ACullDistanceVolume::StaticClass() ||
        Class == ALightmassImportanceVolume::StaticClass();
}

bool ProjectionNavigationBoundsShape(const P4MapKit::FModelNative& Shape)
{
    // Admit the existing source polygon reconstruction only when every value it
    // sends into BSP/brush collision is finite and every polygon index is bounded.
    // No fitted box or default shape substitutes for an unread source volume.
    if (!Shape.Error.Code.empty() || Shape.BufferVertices || Shape.Nodes.empty() ||
        Shape.Nodes.size() != Shape.Surfaces.size() || Shape.Nodes.size() > 4096 || Shape.Points.size() > 4096 ||
        Shape.Vectors.size() > 4096 || Shape.Vertices.size() > 16384)
        return false;
    for (const float Value : Shape.Bounds)
        if (!FMath::IsFinite(Value))
            return false;
    for (const auto& Point : Shape.Points)
        for (const float Value : Point)
            if (!FMath::IsFinite(Value))
                return false;
    for (const auto& Vector : Shape.Vectors)
        for (const float Value : Vector)
            if (!FMath::IsFinite(Value))
                return false;
    const auto In = [](int32 Index, std::size_t Count) { return Index >= 0 && std::size_t(Index) < Count; };
    for (const auto& Polygon : Shape.Nodes)
    {
        if (!In(Polygon.Surface, Shape.Surfaces.size()) || Polygon.Vertices < 3 || Polygon.VertexPool < 0 ||
            std::size_t(Polygon.VertexPool) + Polygon.Vertices > Shape.Vertices.size())
            return false;
        for (const float Value : Polygon.Plane)
            if (!FMath::IsFinite(Value))
                return false;
        const auto& Surface = Shape.Surfaces[Polygon.Surface];
        if (Surface.Material != 0 || !In(Surface.Base, Shape.Points.size()) ||
            !In(Surface.Normal, Shape.Vectors.size()) || !In(Surface.TextureU, Shape.Vectors.size()) ||
            !In(Surface.TextureV, Shape.Vectors.size()) || !FMath::IsFinite(Surface.LightMapScale))
            return false;
        for (const float Value : Surface.Plane)
            if (!FMath::IsFinite(Value))
                return false;
        if (FVector(Surface.Plane[0], Surface.Plane[1], Surface.Plane[2]).IsNearlyZero())
            return false;
        for (int32 Corner = 0; Corner < Polygon.Vertices; ++Corner)
            if (!In(Shape.Vertices[Polygon.VertexPool + Corner].Point, Shape.Points.size()))
                return false;
    }
    return true;
}

KADAN_API bool ProjectionConstructorChildrenSafe(UClass* Root, TFunctionRef<bool(UClass*)> Supported, FString& Error)
{
    TSet<UClass*> Visiting, Finished;
    int32 Asked = 0;
    TFunction<bool(UClass*)> Check = [&](UClass* Class)
    {
        if (!Class || !Class->IsChildOf(AActor::StaticClass()) ||
            (Class->IsChildOf(AVolume::StaticClass()) && !ProjectionSupportedBrushClass(Class)) ||
            Class->IsChildOf(AInstancedFoliageActor::StaticClass()) || !Supported(Class))
        {
            Error = TEXT("game-backed-constructor-child-class-unavailable");
            return false;
        }
        if (Finished.Contains(Class))
            return true;
        if (Visiting.Contains(Class) || Visiting.Num() >= 32 || ++Asked > 4096)
        {
            Error = TEXT("game-backed-constructor-child-class-cycle-or-limit");
            return false;
        }
        Visiting.Add(Class);
        TSet<UClass*> Children;
        if (AActor* Default = Class->GetDefaultObject<AActor>())
        {
            TInlineComponentArray<UChildActorComponent*> Components(Default);
            for (UChildActorComponent* Component : Components)
                if (Component && Component->GetChildActorClass())
                    Children.Add(Component->GetChildActorClass());
        }
        UBlueprintGeneratedClass* Actual = Cast<UBlueprintGeneratedClass>(Class);
        for (UClass* Parent = Class; Parent; Parent = Parent->GetSuperClass())
            if (UBlueprintGeneratedClass* Blueprint = Cast<UBlueprintGeneratedClass>(Parent))
                if (Blueprint->SimpleConstructionScript)
                    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
                        if (Node)
                            if (UChildActorComponent* Component = Cast<UChildActorComponent>(
                                    Node->GetActualComponentTemplate(Actual)))
                                if (Component->GetChildActorClass())
                                    Children.Add(Component->GetChildActorClass());
        for (UClass* Child : Children)
            if (!Check(Child))
                return false;
        Visiting.Remove(Class);
        Finished.Add(Class);
        return true;
    };
    return Check(Root);
}

struct FProjectionRefs
{
    const FFile& Game;
    const TSet<FName>& Unavailable;
    const TSet<int32>& Omitted;
    const TMap<FString, int32>& Paths;
    bool Check(const FProperty* Property, const FDecodedValue& Data, int32 Depth = 0) const
    {
        if (!Property || Depth > 16)
            return Depth <= 16;
        if (CastField<FSoftObjectProperty>(Property))
            return true;
        if (CastField<FObjectPropertyBase>(Property))
        {
            if (Data.Kind != FDecodedValue::EKind::String)
                return Data.Kind == FDecodedValue::EKind::Null;
            const FString Path = Text(Data.Text);
            if (const int32* Index = Paths.Find(Path))
                return !Omitted.Contains(Game.ActorOf(*Index));
            if (!Path.StartsWith(TEXT("/")))
                return false;
            if (Unavailable.Contains(FName(*FPackageName::ObjectPathToPackageName(Path))))
                return false;
            UObject* Target = StaticLoadObject(
                UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
            return Target &&
                FString(Target->GetOutermost()->GetMetaData()->GetValue(Target, TEXT("P4BlueprintStandIn"))) !=
                TEXT("1");
        }
        if (CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property))
        {
            for (const auto& Entry : Data.Items)
                if (const auto* Target = Entry.Field("object"))
                    if (const int32* Index = Paths.Find(Text(Target->Text)))
                        if (Omitted.Contains(Game.ActorOf(*Index)))
                            return false;
            return true;
        }
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            const FString Kind = Struct->Struct->GetPathName();
            if (Kind.Contains(TEXT("SoftObjectPath")) || Kind.Contains(TEXT("SoftClassPath")))
                return true;
            for (const auto& Field : Data.Fields)
                if (!Check(Struct->Struct->FindPropertyByName(FName(*Text(Field.first))), Field.second, Depth + 1))
                    return false;
        }
        else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            for (const auto& Item : Data.Items)
                if (!Check(Array->Inner, Item, Depth + 1))
                    return false;
        }
        else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            for (const auto& Item : Data.Items)
                if (!Check(Set->ElementProp, Item, Depth + 1))
                    return false;
        }
        else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
            if (const auto* Pairs = Data.Field("__native_map_pairs"))
                for (const auto& Pair : Pairs->Items)
                    if (Pair.Items.size() != 2 || !Check(Map->KeyProp, Pair.Items[0], Depth + 1) ||
                        !Check(Map->ValueProp, Pair.Items[1], Depth + 1))
                        return false;
        return true;
    }
};

FP4OriginalLevelImportResult ResumeOriginalLevel(UWorld* World, const FString& HeaderFile, const FString& BodyFile)
{
    FP4OriginalLevelImportResult Result;
    FString Error;
    const auto Fail = [&](EP4OriginalLevelImportError Code, const FString& Why)
    {
        Result.Error = Code;
        Result.ErrorDetail = Why;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(EP4OriginalLevelImportError::EditorThreadRequired, TEXT("level-resume-editor-thread-required"));
    if (!IsValid(World) || !World->PersistentLevel)
        return Fail(EP4OriginalLevelImportError::OccupiedDestination, TEXT("level-resume-original-world-required"));
    FP4OriginalWorldSourcePtr SelectedSource;
    if (!P4ReadOriginalWorldSource(HeaderFile, BodyFile, World->GetOutermost()->GetName(), SelectedSource, Error))
        return Fail(EP4OriginalLevelImportError::SourceUnreadable, Error);
    const TArray<uint8>& Header = SelectedSource->Header();
    const TArray<uint8>& Body = SelectedSource->Body();
    Result.SourceDigest = SelectedSource->SourceDigest();
    const auto Source = P4MapKit::ReadPackageHeader(
        std::vector<uint8>(Header.GetData(), Header.GetData() + Header.Num()), Body.Num());
    Result.Package = World->GetOutermost()->GetName();
    Result.AssetPath = World->GetPathName();
    if (!Source.Error.Code.empty())
        return Fail(EP4OriginalLevelImportError::DecodeFailed, TEXT("level-resume-source-header-unreadable"));
    if (!P4SameText(Text(Source.Package), Result.Package))
        return Fail(EP4OriginalLevelImportError::InvalidGamePath, TEXT("level-resume-source-package-differs"));
    int32 Roots = 0;
    for (const auto& Export : Source.Exports)
        if (Export.Outer == 0 && P4SameText(Text(Export.Name), World->GetName()))
        {
            std::string Class;
            if (!Source.Reference(Export.Class, Class) || !P4SameText(Text(Class), TEXT("/Script/Engine.World")))
                return Fail(
                    EP4OriginalLevelImportError::InvalidGamePath, TEXT("level-resume-source-root-is-not-world"));
            ++Roots;
        }
    if (Roots != 1)
        return Fail(EP4OriginalLevelImportError::InvalidGamePath, TEXT("level-resume-source-world-identity-ambiguous"));
    FBaseline Baseline;
    FP4OriginalRecord Record;
    TArray<FString> Imported;
    FString ImportedDigest;
    if (!ReadBaseline(World, Baseline) || !P4ReadOriginalRecord(World, Record, Error) || Record.Kind != TEXT("level") ||
        !P4SameText(Record.Package, Result.Package) || !P4SameText(Record.Class, World->GetClass()->GetPathName()) ||
        Record.SourceDigestKind != TEXT("header-body-sha256-v1") ||
        !P4SameText(Record.SourceDigest, Result.SourceDigest) || !P4SameText(Baseline.Source, Result.SourceDigest) ||
        !P4SameText(Baseline.State, Record.ComparisonDigest) ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported) ||
        !Digest(Imported, ImportedDigest) || !P4SameText(ImportedDigest, Baseline.State) ||
        (Record.ComparisonSchema != TEXT("level-lines-v1") &&
            Record.ComparisonSchema != TEXT("game-backed-level-lines-v1")))
        return Fail(EP4OriginalLevelImportError::RecordValidationRequired,
            TEXT("level-resume-original-source-or-baseline-unqualified: ") + Error);
    if (Record.ComparisonSchema == TEXT("game-backed-level-lines-v1"))
    {
        TSet<FName> Omitted;
        FString CustodySource;
        if (!ReadGameBackedCustody(World, Omitted, CustodySource, Error, SelectedSource.Get()))
            return Fail(EP4OriginalLevelImportError::RecordValidationRequired, Error);
        if (!RetargetGameBackedSource(World, *SelectedSource, Error))
            return Fail(EP4OriginalLevelImportError::RecordValidationRequired, Error);
        Result.GameBacked = true;
        Result.GameBackedReason = TEXT("Existing game-backed layer and edits preserved");
        for (const FName Name : Omitted)
            Result.UneditableOriginalActors.Add(Name.ToString());
        TSharedPtr<FJsonObject> Custody;
        if (FJsonSerializer::Deserialize(
                TJsonReaderFactory<TCHAR>::Create(World->GetOutermost()->GetMetaData()->GetValue(World, GameBackedKey)),
                Custody) &&
            Custody.IsValid())
        {
            Result.OriginalActorCount = Custody->GetArrayField(TEXT("actors")).Num();
            Result.EditableOriginalActorCount = Result.OriginalActorCount - Omitted.Num();
            Result.ActorCountsKnown = true;
        }
    }
    else if (Record.Fidelity != TEXT("exact"))
        return Fail(
            EP4OriginalLevelImportError::RecordValidationRequired, TEXT("level-resume-original-fidelity-unqualified"));
    Result.Asset = World;
    Result.IdentityKnown = Result.BaselineKnown = true;
    Result.ImportedState = Baseline.State;
    Result.Objects = Source.Exports.size();
    return Result;
}

FP4OriginalLevelImportResult ImportGameBacked(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk,
    const TSet<FName>& Unavailable, const FString& Reason, bool TryActors, const FString& DiagnosticDestination,
    const TSet<FString>& OmitActors, bool ReimportSlot)
{
    FP4OriginalLevelImportResult Result;
    Result.GameBacked = true;
    Result.GameBackedReason = Reason;
    const auto Fail = [&](EP4OriginalLevelImportError Code, const FString& Why)
    {
        Result.Error = Code;
        Result.ErrorDetail = Why;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(EP4OriginalLevelImportError::EditorThreadRequired, TEXT("game-backed-editor-thread-required"));
    P4ImportPublication::FImportScope Import;
    FFile Game;
    FBaseline Baseline;
    FString Error;
    FP4OriginalWorldSourcePtr HeldSource;
    if (!P4ReadOriginalWorldSource(HeaderFile, BodyFile, FString(), HeldSource, Error))
        return Fail(EP4OriginalLevelImportError::SourceUnreadable, Error);
    const TArray<uint8>& Header = HeldSource->Header();
    const TArray<uint8>& Body = HeldSource->Body();
    Baseline.Source = HeldSource->SourceDigest();
    Game.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
    Game.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    if (!Read(Game, FString(), Error, true))
        return Fail(EP4OriginalLevelImportError::DecodeFailed, Error);
    if (Game.UnsupportedScript)
    {
        TryActors = false;
        Result.GameBackedReason += TEXT("; original level script contains an unsupported variable type: ") +
            Text(Game.ClassNative.UndecodedType);
    }
    const FString Package = Game.PackageName, Short = FPackageName::GetShortName(Package), SourceHash = Baseline.Source;
    if (!P4IsValidContentPackageIgnoringCase(Package) || !P4SameText(Game.Node(Game.World).Name, Short))
        return Fail(EP4OriginalLevelImportError::InvalidGamePath, TEXT("game-backed-source-identity-unusable"));
#if P4_KADAN_TEST_SEAMS
    const bool Diagnostic = !DiagnosticDestination.IsEmpty();
    if (Diagnostic &&
        (SaveToDisk || !DiagnosticDestination.StartsWith(TEXT("/Temp/P4OriginalFamilyProof_")) ||
            !FPackageName::IsValidLongPackageName(DiagnosticDestination, true) ||
            FPackageName::GetShortName(DiagnosticDestination) != Short))
        return Fail(
            EP4OriginalLevelImportError::InvalidGamePath, TEXT("game-backed-family-diagnostic-destination-unusable"));
#else
    // Family diagnostics are a development test seam only.
    if (!DiagnosticDestination.IsEmpty())
        return Fail(EP4OriginalLevelImportError::InvalidGamePath, TEXT("game-backed-family-diagnostic-unavailable"));
    const bool Diagnostic = false;
#endif
    if (ReimportSlot && (SaveToDisk || Diagnostic))
        return Fail(EP4OriginalLevelImportError::InvalidGamePath,
            TEXT("game-backed-reimport-slot-is-private-unsaved-staging-only"));
    if (Diagnostic)
        Game.ConstructionPackage = DiagnosticDestination;
    Baseline.Package = Package;
    Result.Package = Package;
    Result.AssetPath = Package + TEXT(".") + Short;
    Result.IdentityKnown = true;
    Result.Objects = Game.Nodes.Num();
    TArray<int32> SourceActors = Game.Actors;
    if (Game.ScriptActor)
        SourceActors.AddUnique(Game.ScriptActor);
    Result.ActorCountsKnown = true;
    Result.OriginalActorCount = SourceActors.Num();
    UPackage* Existing = FindPackage(nullptr, *(Diagnostic ? DiagnosticDestination : Package));
    if (!Diagnostic && !ReimportSlot && !Existing && FPackageName::DoesPackageExist(Package))
        Existing = LoadPackage(nullptr, *Package, LOAD_NoWarn | LOAD_Quiet);
    UWorld* World = Existing ? UWorld::FindWorldInPackage(Existing) : nullptr;
    if (Existing)
    {
        if (Diagnostic)
            return Fail(EP4OriginalLevelImportError::OccupiedDestination,
                TEXT("game-backed-family-diagnostic-destination-occupied"));
        TSet<FName> Names;
        FString Source;
        if (!World || !ReadBaseline(World, Baseline) ||
            !ReadGameBackedCustody(World, Names, Source, Error, HeldSource.Get()) || !P4SameText(Source, SourceHash))
            return Fail(EP4OriginalLevelImportError::OccupiedDestination,
                TEXT("game-backed-mode-does-not-replace-an-existing-full-or-edited-layer"));
        if (!RetargetGameBackedSource(World, *HeldSource, Error))
            return Fail(EP4OriginalLevelImportError::RecordValidationRequired, Error);
        for (const FName Name : Names)
            Result.UneditableOriginalActors.Add(Name.ToString());
        Result.EditableOriginalActorCount = Result.OriginalActorCount - Names.Num();
        Result.Asset = World;
        Result.SourceDigest = SourceHash;
        Result.ImportedState = Baseline.State;
        Result.State = State(World);
        Result.BaselineKnown = true;
        return Result; // Preserve existing projection edits; never replay or replace them.
    }
    FString Root = HeaderFile;
    FPaths::NormalizeFilename(Root);
    const FString Tail = P4ContentRelativePath(Package) + TEXT(".umap");
    Game.SourceRoot = Root.EndsWith(Tail) ? Root.LeftChop(Tail.Len()) : FString();
    TSet<int32> Omitted;
    TMap<FString, int32> Paths;
    TSet<FName> Declined = Unavailable;
    if (Game.ScriptActor)
        Omitted.Add(Game.ScriptActor);
    for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
        Paths.Add(Game.Node(Index).Path, Index);
    const auto SupportedClass = [&](UClass* Class)
    {
        if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || ProjectionNavigationIsGameBacked(Class))
            return false;
        for (UClass* Parent = Class; Parent; Parent = Parent->GetSuperClass())
            if (Parent->ClassGeneratedBy)
            {
                FP4OriginalRecord Record;
                FString Why;
                if (P4BlueprintReconstructionIncomplete(Parent) ||
                    Unavailable.Contains(Parent->GetOutermost()->GetFName()) ||
                    !P4ReadOriginalRecord(Parent->ClassGeneratedBy, Record, Why) || Record.Kind != TEXT("blueprint") ||
                    Record.Fidelity != TEXT("recovered"))
                    return false;
            }
        return true;
    };
    // Both ends of every source child link must be present, reciprocal and acyclic before a
    // parent constructor/SCS is allowed to create children. Outer ancestry alone misses them.
    TMap<int32, int32> ChildParents;
    for (const int32 Child : Game.Actors)
        if (const FDecodedValue* Parent = Game.Node(Child).Values.Field("ParentComponent"))
        {
            const int32* Component = Parent->Kind == FDecodedValue::EKind::String ? Paths.Find(Text(Parent->Text))
                                                                                  : nullptr;
            const int32 Owner = Component ? Game.ActorOf(*Component) : 0;
            const FDecodedValue* Link = Component ? Game.Node(*Component).Values.Field("ChildActor") : nullptr;
            if (!Owner || Owner == Child || !Link || Link->Kind != FDecodedValue::EKind::String ||
                Text(Link->Text) != Game.Node(Child).Path)
            {
                Omitted.Add(Child);
                if (Owner)
                    Omitted.Add(Owner);
            }
            else
                ChildParents.Add(Child, Owner);
        }
    for (const int32 Actor : ProjectionUnsafeAttachmentNodes(ChildParents))
        Omitted.Add(Actor);
    for (const int32 Actor : Game.Actors)
    {
        if (!TryActors || Actor == Game.ScriptActor)
        {
            Omitted.Add(Actor);
            continue;
        }
        // Actors the caller found the editor changes when the map opens stay the game's by name.
        if (OmitActors.Contains(Game.Node(Actor).Name))
        {
            Omitted.Add(Actor);
            continue;
        }
        UClass* Class = LoadObject<UClass>(nullptr, *Game.Node(Actor).Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (ProjectionNavigationIsGameBacked(Class) && Result.ClassProblems.Num() < 64)
            Result.ClassProblems.Add(Game.Node(Actor).Name +
                TEXT(
                    ": original NavigationData is retained in the game; the editor's agents do not preserve its actor across loading"));
        const bool Bounds = ProjectionSupportedBrushClass(Class);
        bool BoundsShape = !Bounds;
        int32 BoundsModels = 0;
        if (Bounds)
            for (const auto& Shape : Game.Brushes)
                if (Game.ActorOf(Shape.Key) == Actor)
                {
                    ++BoundsModels;
                    BoundsShape = ProjectionNavigationBoundsShape(Shape.Value);
                }
        if (Bounds)
            BoundsShape = BoundsShape && BoundsModels == 1;
        bool Safe = SupportedClass(Class) && Class->IsChildOf(AActor::StaticClass()) &&
            (!Class->IsChildOf(AVolume::StaticClass()) || Bounds) && BoundsShape &&
            !Class->IsChildOf(AInstancedFoliageActor::StaticClass());
        FString ChildError;
        if (Safe)
            Safe = ProjectionConstructorChildrenSafe(Class, SupportedClass, ChildError);
        for (int32 Index = 1; Safe && Index <= Game.Nodes.Num(); ++Index)
            if (Game.ActorOf(Index) == Actor)
            {
                const FNode& Node = Game.Node(Index);
                UClass* Kind = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
                Safe = Node.Undecoded.IsEmpty() && SupportedClass(Kind) && ProjectionFinite(Node.Values) &&
                    (!Game.Brushes.Contains(Index) || Bounds) && !Game.Instanced.Contains(Index);
                if (Safe && Kind->IsChildOf(UChildActorComponent::StaticClass()))
                {
                    const FDecodedValue* Child = Node.Values.Field("ChildActor");
                    const int32* ChildIndex = Child && Child->Kind == FDecodedValue::EKind::String
                        ? Paths.Find(Text(Child->Text))
                        : nullptr;
                    Safe = ChildIndex && Game.Actors.Contains(*ChildIndex) &&
                        ChildParents.FindRef(*ChildIndex) == Actor;
                    if (Safe)
                    {
                        UClass* ChildClass = LoadObject<UClass>(
                            nullptr, *Game.Node(*ChildIndex).Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
                        Safe = ProjectionConstructorChildrenSafe(ChildClass, SupportedClass, ChildError);
                    }
                }
                if (Safe && Node.Template < 0)
                {
                    const FString Path = Game.Ref(Node.Template);
                    Safe = !Unavailable.Contains(FName(*FPackageName::ObjectPathToPackageName(Path))) &&
                        ResolveOriginalExternalTemplate(Path) != nullptr;
                }
            }
        if (!Safe)
        {
            Omitted.Add(Actor);
            if (Result.ClassProblems.Num() < 64 && !ChildError.IsEmpty())
                Result.ClassProblems.Add(Game.Node(Actor).Name + TEXT(": ") + ChildError);
        }
    }
    TMap<int32, int32> AttachmentParents;
    for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
        if (Game.ActorOf(Index) && !Omitted.Contains(Game.ActorOf(Index)))
        {
            const UClass* Kind = FindObject<UClass>(nullptr, *Game.Node(Index).Class);
            const auto* Parent = Game.Node(Index).Values.Field("AttachParent");
            if (Kind && Kind->IsChildOf(USceneComponent::StaticClass()) && Parent &&
                Parent->Kind == FDecodedValue::EKind::String)
                if (const int32* Target = Paths.Find(Text(Parent->Text)))
                    AttachmentParents.Add(Index, *Target);
        }
    for (const int32 Node : ProjectionUnsafeAttachmentNodes(AttachmentParents))
        Omitted.Add(Game.ActorOf(Node));
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        FProjectionRefs Refs{Game, Declined, Omitted, Paths};
        for (const int32 Actor : Game.Actors)
            if (!Omitted.Contains(Actor))
                for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
                    if (Game.ActorOf(Index) == Actor)
                    {
                        const UClass* Class = LoadObject<UClass>(
                            nullptr, *Game.Node(Index).Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
                        bool Safe = true;
                        for (const auto& Field : Game.Node(Index).Values.Fields)
                            if (!Refs.Check(Class->FindPropertyByName(FName(*Text(Field.first))), Field.second))
                            {
                                Safe = false;
                                break;
                            }
                        if (!Safe)
                        {
                            Omitted.Add(Actor);
                            Changed = true;
                            break;
                        }
                    }
    }
    FFile Projection = Game;
    Projection.Actors.RemoveAll([&](int32 Actor) { return Omitted.Contains(Actor); });
    Projection.ScriptActor = Projection.ScriptClass = Projection.ScriptDefault = 0;
    Projection.ProvenEmptyBeginPlay = false;
    Projection.Brushes.Reset();
    Projection.Instanced.Reset();
    Projection.Foliage.Reset();
    for (const auto& Shape : Game.Brushes)
    {
        const int32 Actor = Game.ActorOf(Shape.Key);
        if (Projection.Actors.Contains(Actor) &&
            ProjectionSupportedBrushClass(FindObject<UClass>(nullptr, *Game.Node(Actor).Class)))
            Projection.Brushes.Add(Shape.Key, Shape.Value);
    }
    for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
        if (Omitted.Contains(Game.ActorOf(Index)) || Index == Game.ScriptClass || Index == Game.ScriptDefault ||
            Index == Game.ScriptActor || (Game.ScriptClass && Game.Below(Index, Game.ScriptClass)))
            Projection.ProjectionExcluded.Add(Index);
    TArray<FString> NotHeld, NotShipped, Extra;
    // Original actor deltas require the same source default lending used by complete import.
    FLentTemplates Lent;
    int32 Asked = 0;
    TArray<FString> Differing;
    TMap<FString, FP4KadanSourceBinding> ClassSources;
    if (Projection.Actors.Num())
    {
        TSet<FName> UnprovenDefaults;
        const TArray<FString> ClassEvidence = ClassesThatDiffer(
            Projection, Game.SourceRoot, Asked, &Differing, &UnprovenDefaults, &ClassSources);
        Declined.Append(UnprovenDefaults);
        Result.ClassProblems.Append(ClassEvidence);
        const auto Depth = [](const FString& Name)
        {
            int32 Count = 0;
            const UClass* Class = LoadObject<UClass>(nullptr, *P4ClassPathOf(Name));
            for (; Class; Class = Class->GetSuperClass())
                ++Count;
            return Count;
        };
        Differing.StableSort([&](const FString& A, const FString& B) { return Depth(A) < Depth(B); });
        for (const FString& Name : Differing)
            Lent.Lend(Name, Game.SourceRoot, ClassSources.Find(Name));
        Result.ClassesKnown = true;
        Result.ClassesCompared = Asked;
        Result.DifferingClasses = Differing;
        Result.LentClasses = Lent.Classes;
        Result.ClassProblems.Append(Lent.Problems);
        for (const FString& Name : Differing)
            if (!Lent.Classes.Contains(Name))
                Declined.Add(FName(*Name));
        // Lending itself does not establish equality. Re-cook every lent class from the same
        // pinned source buffers before any actor may rely on those defaults.
        for (const FString& Name : Lent.Classes)
        {
            const FP4KadanSourceBinding* Source = ClassSources.Find(Name);
            const auto Proven = Source
                ? CompareClassNative(Source->HeaderPath, Source->BodyPath, Name, &Source->Header, &Source->Body)
                : FP4OriginalClassComparisonResult();
            FString SourceError;
            if (!Source || !Proven.IsValid() || !Proven.DefaultsReadable || !Proven.Same ||
                !P4KadanSourceBindingCurrent(*Source, SourceError))
            {
                Declined.Add(FName(*Name));
                Result.ClassProblems.Add(Name + TEXT(": source defaults remain unproven after lending: ") +
                    SourceError +
                    (Proven.ErrorDetail.IsEmpty() ? (Proven.OnlyGameSays.Num() ? Proven.OnlyGameSays[0].Left(300)
                                                                               : TEXT("class comparison differs"))
                                                  : Proven.ErrorDetail));
            }
        }
        // A missing source default disqualifies only families that need that class/template.
        // Unrelated native actors retain their own source-fidelity qualification below.
        for (const int32 Actor : Projection.Actors)
            for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
                if (Game.ActorOf(Index) == Actor)
                {
                    const FNode& Node = Game.Node(Index);
                    UClass* Class = FindObject<UClass>(nullptr, *Node.Class);
                    bool Safe = ProjectionClassHasProvenDefaults(Class, Declined);
                    if (Node.Template < 0 &&
                        Declined.Contains(FName(*FPackageName::ObjectPathToPackageName(Game.Ref(Node.Template)))))
                        Safe = false;
                    if (!Safe)
                    {
                        Omitted.Add(Actor);
                        break;
                    }
                }
        for (bool Changed = true; Changed;)
        {
            Changed = false;
            FProjectionRefs Refs{Game, Declined, Omitted, Paths};
            for (const int32 Actor : Projection.Actors)
                if (!Omitted.Contains(Actor))
                    for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
                        if (Game.ActorOf(Index) == Actor)
                        {
                            UClass* Class = FindObject<UClass>(nullptr, *Game.Node(Index).Class);
                            bool Safe = true;
                            for (const auto& Field : Game.Node(Index).Values.Fields)
                                if (!Refs.Check(Class ? Class->FindPropertyByName(FName(*Text(Field.first))) : nullptr,
                                        Field.second))
                                {
                                    Safe = false;
                                    break;
                                }
                            if (!Safe)
                            {
                                Omitted.Add(Actor);
                                Changed = true;
                                break;
                            }
                        }
        }
        Projection.Actors.RemoveAll([&](int32 Actor) { return Omitted.Contains(Actor); });
        for (auto It = Projection.Brushes.CreateIterator(); It; ++It)
            if (Omitted.Contains(Game.ActorOf(It.Key())))
                It.RemoveCurrent();
        for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
            if (Omitted.Contains(Game.ActorOf(Index)))
                Projection.ProjectionExcluded.Add(Index);
    }
    // A global native-world refusal has no actor-local pruning interpretation. A projection
    // without any of the layer's actors still opens and exports: every original actor stays
    // in the game (best effort), and the import says so.
    const FString GlobalRefusal = Refusal(Projection);
    if (GlobalRefusal.IsEmpty())
        World = Build(Projection, NotHeld, NotShipped, Extra, Error);
    else
        Error = GlobalRefusal;
    if (!World)
    {
        if (!Error.IsEmpty())
            Result.GameBackedReason += TEXT("; projection construction: ") + Error;
        return Fail(EP4OriginalLevelImportError::UnsupportedContent,
            TEXT("game-backed-layer-not-built: ") + Result.GameBackedReason);
    }
    TStrongObjectPtr<UWorld> Hold(World);
    const auto TrackingEvidence = [&](const TCHAR* Phase)
    {
        if (!Diagnostic)
            return;
        int32 Count = 0;
        for (const FNode& Node : Game.Nodes)
        {
            const FDecodedValue* List = Node.Values.Field("UCSModifiedProperties");
            bool Terrain = false;
            if (List && List->Kind == FDecodedValue::EKind::Array)
                for (const auto& Item : List->Items)
                    if (const FDecodedValue* Name = Item.Field("MemberName"))
                        Terrain |= Name->Text == "C4TerrainTagContainer";
            if (!Terrain || Count >= 6)
                continue;
            UActorComponent* Component = FindObject<UActorComponent>(World->GetOutermost(), *Node.Path);
            if (!Component)
                continue;
            ++Count;
            TSet<const FProperty*> Modified;
            Component->GetUCSModifiedProperties(Modified);
            TArray<FString> Names;
            for (const FProperty* Property : Modified)
                Names.Add(Property ? Property->GetPathName() : TEXT("unresolved-member"));
            const FProperty* Field = Component->GetClass()->FindPropertyByName(TEXT("C4TerrainTagContainer"));
            const UObject* Template = Component->GetArchetype();
            const bool Equal = Field && Template &&
                Field->Identical(Field->ContainerPtrToValuePtr<void>(Component),
                    Field->ContainerPtrToValuePtr<void>(Template), PPF_DeepCompareInstances);
            Result.ClassProblems.Add(FString::Printf(
                TEXT(
                    "terrain tracking %s %s | creation %d | modified %s | field %s flags %016llx | equals archetype %d"),
                Phase, *Node.Path, int32(Component->CreationMethod), *FString::Join(Names, TEXT(",")),
                Field ? *Field->GetPathName() : TEXT("missing"), Field ? Field->GetPropertyFlags() : 0ull,
                int32(Equal)));
        }
    };
    TrackingEvidence(TEXT("constructed"));
    // Every editable source actor family must equal the actual game's semantic lines. Failed
    // families remain in the game, never accepted by a newly computed editor-state baseline.
    FFile ProvenCook;
    TArray<FString> SourceLines, ProjectedLines, EngineLines;
    if (!Cooked(World, true, ProvenCook, Error))
    {
        Hold.Reset();
        Discard(World);
        return Fail(EP4OriginalLevelImportError::CookFailed, Error);
    }
    TrackingEvidence(TEXT("after-cook"));
    FFile Compared = Game;
    Without(Compared, NotHeld);
    RedundantUnshippedNulls(Compared, ProvenCook, Game.SourceRoot, NotShipped, World);
    Lines(Compared, SourceLines, EngineLines, nullptr, true);
    Lines(ProvenCook, ProjectedLines, EngineLines, nullptr, true);
    const FString SourceLevel = Game.Node(Game.Level).Path.ToLower();
    for (const int32 Index : Projection.Actors)
        if (!Omitted.Contains(Index))
        {
            const FString Name = Game.Node(Index).Name.ToLower();
            if (!ProjectionFamilyEqual(SourceLines, ProjectedLines, SourceLevel, Name))
            {
                Omitted.Add(Index);
                FString Missing, ExtraLine;
                const TSet<FString, FLocKeySetFuncs> Have(ProjectedLines), Wanted(SourceLines);
                for (const FString& Line : SourceLines)
                    if (OwnerOf(Line, SourceLevel) == Name && !Have.Contains(Line))
                    {
                        Missing = Line;
                        break;
                    }
                for (const FString& Line : ProjectedLines)
                    if (OwnerOf(Line, SourceLevel) == Name && !Wanted.Contains(Line))
                    {
                        ExtraLine = Line;
                        break;
                    }
                if (Result.ClassProblems.Num() < 64)
                    Result.ClassProblems.Add(Game.Node(Index).Name + TEXT(": exact source family differs; source ") +
                        Missing + TEXT("; editor ") + ExtraLine);
            }
        }
    // Return ordinary editor defaults before replaying construction, just as the complete
    // importer does. Comparing source deltas directly after Return would confuse a default
    // difference with an instance change, so retain the source-proven instance's editor cook.
    Lent.Return();
    bool HasAdmittedActor = false;
    for (const int32 Index : Projection.Actors)
        HasAdmittedActor |= !Omitted.Contains(Index);
    if (HasAdmittedActor)
    {
        FFile BeforeRun, AfterRun;
        TArray<FString> BeforeRunLines, AfterRunLines;
        if (!Cooked(World, true, BeforeRun, Error))
        {
            Hold.Reset();
            Discard(World);
            return Fail(EP4OriginalLevelImportError::CookFailed, Error);
        }
        Lines(BeforeRun, BeforeRunLines, EngineLines, nullptr, true);
        for (const int32 Index : Projection.Actors)
            if (!Omitted.Contains(Index))
            {
                AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Game.Node(Index).Name);
                if (!IsValid(Actor))
                {
                    Omitted.Add(Index);
                    continue;
                }
                // Parent replay recreates its children. They remain in both exact cooked family
                // comparisons; replaying (or rejecting) each child independently destroys that proof.
                if (Actor->IsChildActor())
                    continue;
                Actor->RerunConstructionScripts();
            }
        // The game's built lighting as the game's editor held the level after loading it.
        World->GetOutermost()->GetMetaData()->SetValue(World, LightingKey, *Pack(OriginalLightingLines(Game)));
        P4HoldOriginalBuiltLighting(World);
        // And its navigation build's meshes.
        P4HoldOriginalNavigationMeshes(World, Game);
        if (!Cooked(World, true, AfterRun, Error))
        {
            Hold.Reset();
            Discard(World);
            return Fail(EP4OriginalLevelImportError::CookFailed, Error);
        }
        Lines(AfterRun, AfterRunLines, EngineLines, nullptr, true);
        for (const int32 Index : Projection.Actors)
            if (!Omitted.Contains(Index) &&
                !ProjectionFamilyEqual(BeforeRunLines, AfterRunLines, SourceLevel, Game.Node(Index).Name.ToLower()))
            {
                Omitted.Add(Index);
                Result.ClassProblems.Add(Game.Node(Index).Name +
                    TEXT(
                        ": construction replay changed the source-proven instance state; original game actor preserved"));
                Result.ClassesKnown = true;
            }
    }
    // A failed family's hard-reference dependents are omitted too before collecting the baseline.
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        FProjectionRefs Refs{Game, Declined, Omitted, Paths};
        for (const int32 Actor : Projection.Actors)
            if (!Omitted.Contains(Actor))
                for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
                    if (Game.ActorOf(Index) == Actor)
                        for (const auto& Field : Game.Node(Index).Values.Fields)
                        {
                            UClass* Class = LoadObject<UClass>(nullptr, *Game.Node(Index).Class);
                            if (!Refs.Check(Class->FindPropertyByName(FName(*Text(Field.first))), Field.second))
                            {
                                Omitted.Add(Actor);
                                Changed = true;
                                break;
                            }
                        }
    }
    // A constructor-created actor outside the admitted source set is never a creator addition.
    TSet<FName> Admitted;
    for (const int32 Index : Projection.Actors)
        if (!Omitted.Contains(Index))
            Admitted.Add(FName(*Game.Node(Index).Name));
    if (!Admitted.Num() && SourceActors.Num())
        Result.GameBackedReason += TEXT("; every original actor of this layer stays in the game");
    if (!ProjectionRetireUnexpectedActors(World, Admitted, Error))
    {
        Hold.Reset();
        Discard(World);
        return Fail(EP4OriginalLevelImportError::BuildFailed, Error);
    }
    for (const auto& Entry : ClassSources)
        if (!P4KadanSourceBindingCurrent(Entry.Value, Error))
        {
            Hold.Reset();
            Discard(World);
            return Fail(EP4OriginalLevelImportError::SourceUnreadable, Entry.Key + TEXT(": ") + Error);
        }
    if (Diagnostic)
    {
        // Diagnostic worlds carry no original marker/baseline and can never be published as a
        // repaired source map. The caller receives only source+settled-proven families to inspect.
        FString CurrentSource;
        if (!HashGameBackedSourceFiles(HeaderFile,
                BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile, CurrentSource) ||
            !P4SameText(CurrentSource, SourceHash))
        {
            Hold.Reset();
            Discard(World);
            return Fail(
                EP4OriginalLevelImportError::SourceUnreadable, TEXT("game-backed-family-diagnostic-source-changed"));
        }
        for (const int32 Index : SourceActors)
            if (Omitted.Contains(Index))
                Result.UneditableOriginalActors.Add(Game.Node(Index).Name);
        Result.EditableOriginalActorCount = Result.OriginalActorCount - Result.UneditableOriginalActors.Num();
        Result.Asset = World;
        Result.AssetPath = World->GetPathName();
        Result.SourceDigest = SourceHash;
        return Result;
    }
    // Capture a baseline only for families that passed source fidelity and settled replay.
    FFile Cook;
    TArray<FString> Content, Engine;
    if (!Cooked(World, true, Cook, Error))
    {
        Hold.Reset();
        Discard(World);
        return Fail(EP4OriginalLevelImportError::CookFailed, Error);
    }
    Lines(Cook, Content, Engine);
    if (!Digest(Content, Baseline.State))
    {
        Hold.Reset();
        Discard(World);
        return Fail(EP4OriginalLevelImportError::StateUnreadable, TEXT("game-backed-editor-baseline-unreadable"));
    }
    auto Meta = World->GetOutermost()->GetMetaData();
    Meta->SetValue(World, BaselineKey, *BaselineText(Baseline));
    Meta->SetValue(World, ImportedKey, *Pack(Content));
    auto Custody = MakeShared<FJsonObject>();
    Custody->SetStringField(TEXT("format"), TEXT("p4-game-backed-level"));
    Custody->SetNumberField(TEXT("version"), 1);
    FString SourceHeader = FPaths::ConvertRelativePathToFull(HeaderFile);
    FString SourceBody = FPaths::ConvertRelativePathToFull(
        BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile);
    FPaths::NormalizeFilename(SourceHeader);
    FPaths::NormalizeFilename(SourceBody);
    Custody->SetStringField(TEXT("source_header"), P4PortableSourceCustodyPath(SourceHeader));
    Custody->SetStringField(TEXT("source_body"), P4PortableSourceCustodyPath(SourceBody));
    FString CurrentSource;
    if (!HashGameBackedSourceFiles(SourceHeader, SourceBody, CurrentSource) || !P4SameText(CurrentSource, SourceHash))
    {
        Hold.Reset();
        Discard(World);
        return Fail(EP4OriginalLevelImportError::SourceUnreadable,
            TEXT("game-backed-original-source-changed-before-recording"));
    }
    Custody->SetStringField(TEXT("package"), Package);
    Custody->SetStringField(TEXT("source_sha256"), SourceHash);
    Custody->SetStringField(TEXT("reason"), Result.GameBackedReason);
    TArray<TSharedPtr<FJsonValue>> Actors;
    for (const int32 Index : SourceActors)
    {
        const FNode& Node = Game.Node(Index);
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Node.Name);
        Row->SetStringField(TEXT("class"), Node.Class);
        Row->SetNumberField(TEXT("source_export"), Index);
        Row->SetBoolField(TEXT("editable"), !Omitted.Contains(Index));
        Actors.Add(MakeShared<FJsonValueObject>(Row));
        if (Omitted.Contains(Index))
            Result.UneditableOriginalActors.Add(Node.Name);
    }
    Custody->SetArrayField(TEXT("actors"), Actors);
    FString CustodyText;
    Result.EditableOriginalActorCount = Result.OriginalActorCount - Result.UneditableOriginalActors.Num();
    FJsonSerializer::Serialize(
        Custody, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&CustodyText));
    Meta->SetValue(World, GameBackedKey, *CustodyText);
    FP4OriginalRecord Record;
    Record.Package = Package;
    Record.Class = World->GetClass()->GetPathName();
    Record.Kind = TEXT("level");
    Record.SourceDigest = SourceHash;
    Record.SourceDigestKind = TEXT("header-body-sha256-v1");
    Record.ComparisonDigest = Baseline.State;
    Record.ComparisonSchema = TEXT("game-backed-level-lines-v1");
    Record.Fidelity = TEXT("game-backed-overlay");
    FP4OriginalValueNote CustodyNote;
    CustodyNote.Kind = TEXT("game-backed-custody");
    CustodyNote.Object = Result.AssetPath;
    if (!P4HashUtf8(CustodyText, CustodyNote.GameValue))
    {
        Hold.Reset();
        Discard(World);
        return Fail(EP4OriginalLevelImportError::RecordFailed, TEXT("game-backed-custody-digest-unreadable"));
    }
    CustodyNote.Action = TEXT("preserve-uneditable-originals");
    Record.Values.Add(CustodyNote);
    if (!P4MarkOriginalAsset(World, Record, Error))
    {
        Hold.Reset();
        Discard(World);
        return Fail(EP4OriginalLevelImportError::RecordFailed, Error);
    }
    FAssetRegistryModule::AssetCreated(World);
    World->MarkPackageDirty();
    Result.Asset = World;
    Result.Created = true;
    Result.SourceDigest = SourceHash;
    Result.ImportedState = Result.State = Baseline.State;
    Result.BaselineKnown = true;
    if (SaveToDisk)
    {
        if (!P4SaveAssetPackage(World, [&]() { return P4SameText(State(World), Baseline.State); }, Error))
        {
            Result.Asset = nullptr;
            Result.Created = false;
            Hold.Reset();
            Discard(World);
            return Fail(EP4OriginalLevelImportError::SaveFailed, Error);
        }
        Result.Saved = true;
    }
    return Result;
}
} // namespace P4LevelContent

FP4OriginalLevelImportResult P4ResumeOriginalLevelNative(
    UWorld* World, const FString& HeaderFile, const FString& BodyFile)
{
    return P4LevelContent::ResumeOriginalLevel(World, HeaderFile, BodyFile);
}

FP4OriginalLevelImportResult P4ImportGameBackedLevelNative(const FString& HeaderFile, const FString& BodyFile,
    bool SaveToDisk, const TSet<FName>& UnavailablePackages, const FString& Reason, bool TrySupportedActors,
    const TSet<FString>& OmitActors)
{
    return P4LevelContent::ImportGameBacked(
        HeaderFile, BodyFile, SaveToDisk, UnavailablePackages, Reason, TrySupportedActors, FString(), OmitActors);
}

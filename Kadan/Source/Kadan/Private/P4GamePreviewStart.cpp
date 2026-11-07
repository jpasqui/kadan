#include "P4GamePreviewStart.h"
#include "P4GamePaths.h"
#include "P4CreatorSource.h"
#include "P4LevelChanges.h"
#include "P4LevelContent.h"
#include "P4GamePreviewWorld.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/PlayerStartPIE.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "LevelEditorViewport.h"
#include "UObject/Package.h"

namespace
{
bool SameLines(const TArray<FString>& A, const TArray<FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    TArray<FString> Left = A, Right = B;
    const auto Less = [](const FString& X, const FString& Y) { return X.Compare(Y, ESearchCase::CaseSensitive) < 0; };
    Left.Sort(Less);
    Right.Sort(Less);
    for (int32 I = 0; I < Left.Num(); ++I)
        if (!Left[I].Equals(Right[I], ESearchCase::CaseSensitive))
            return false;
    return true;
}

bool SameLines(const FP4LevelLineChanges& A, const FP4LevelLineChanges& B)
{
    return SameLines(A.Before, B.Before) && SameLines(A.After, B.After);
}

bool SourceWorlds(UWorld* World, TArray<UWorld*>& Sources, FString& Error)
{
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("Preview changes need an editor level on the editor thread.");
        return false;
    }
    Sources.Add(World);
    for (ULevel* Level : World->GetLevels())
    {
        if (!IsValid(Level) || Level->HasAnyFlags(RF_Transient))
            continue;
        UWorld* Source = Level->GetTypedOuter<UWorld>();
        if (!Source || Source->PersistentLevel != Level || Source->GetOutermost() != Level->GetOutermost())
        {
            Error = TEXT("The preview cannot verify loaded level ") + Level->GetPathName() + TEXT(".");
            return false;
        }
        Sources.AddUnique(Source);
    }
    return true;
}

// Query pawn-blocking collision, rather than rendered triangles or visibility
// collision: a decorative mesh with NoCollision cannot be a landing surface.
// Preview copies must never make an otherwise empty authored level pass.
FCollisionQueryParams StartQuery(UWorld* World)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(KadanPreviewStart), false);
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        const ULevel* Level = Actor->GetLevel();
        if (Actor->bIsEditorPreviewActor || Actor->HasAnyFlags(RF_Transient) ||
            (Level && (Level->HasAnyFlags(RF_Transient) || !Level->bIsVisible)))
            Query.AddIgnoredActor(Actor);
    }
    return Query;
}

bool FloorBelow(UWorld* World, const FVector& At, const FCollisionQueryParams& Query, FHitResult& Hit, FString& Why)
{
    // A ceiling above the marker must not become its landing floor. A one
    // centimetre tolerance admits a marker placed exactly on the floor;
    // clearance below a partly embedded capsule is checked separately.
    const FVector Top = At + FVector(0, 0, 1.f);
    const FVector Bottom = At - FVector(0, 0, 20000.f);
    if (!World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Pawn, Query) || !Hit.bBlockingHit ||
        Hit.bStartPenetrating)
    {
        Why = TEXT("there is no pawn-blocking floor below it");
        return false;
    }
    if (Hit.ImpactNormal.Z < 0.71f)
    {
        Why = TEXT("the surface below it is too steep to stand on");
        return false;
    }
    return true;
}

bool ClearStart(UWorld* World, const FVector& At, float Yaw, bool bCamera, float Radius, float HalfHeight,
    const FCollisionQueryParams& Query, FP4GamePreviewStartResult& Out, FString& Why)
{
    if (At.ContainsNaN() || !FMath::IsFinite(Yaw))
    {
        Why = TEXT("its transform is not finite");
        return false;
    }
    FHitResult Hit;
    if (!FloorBelow(World, At, Query, Hit, Why))
        return false;
    FVector Centre = At;
    // Preserve an intentional elevated Player Start, as native Play does.
    // Only a camera fallback is brought down to the floor before Play.
    const float MinimumZ = Hit.ImpactPoint.Z + HalfHeight + 2.f;
    Centre.Z = bCamera ? MinimumZ : FMath::Max(Centre.Z, MinimumZ);
    if (World->OverlapBlockingTestByChannel(
            Centre, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Query))
    {
        Why = TEXT("the player clearance intersects blocking collision");
        return false;
    }
    Out.World = World;
    Out.Transform = FTransform(FRotator(0, Yaw, 0), Centre, FVector::OneVector);
    Out.FloorPoint = Hit.ImpactPoint;
    Out.FloorNormal = Hit.ImpactNormal;
    Out.HasFloor = true;
    return true;
}

FLevelEditorViewportClient* CameraFor(UWorld* World)
{
    if (!GEditor)
        return nullptr;
    FLevelEditorViewportClient* Fallback = nullptr;
    FViewport* Active = GEditor->GetActiveViewport();
    for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
    {
        if (!Client || Client->GetWorld() != World || !Client->IsPerspective())
            continue;
        if (Client->Viewport == Active)
            return Client;
        if (!Fallback)
            Fallback = Client;
    }
    return Fallback;
}

bool CanRecreateAdded(UWorld* PreviewWorld, AActor* Actor, const TSet<FName>& Imported, FString& Why)
{
    if (!Actor || Actor->bIsEditorPreviewActor || Actor->HasAnyFlags(RF_Transient))
    {
        Why = TEXT("the added actor is missing or is a temporary preview object");
        return false;
    }
    // Mirror::Attach currently reseeds the previous current level against
    // the root world's imported actor names. Do not admit other sublevels
    // until that mirror path has a corresponding per-level baseline.
    if (!PreviewWorld || Actor->GetLevel() != PreviewWorld->PersistentLevel ||
        PreviewWorld->GetCurrentLevel() != PreviewWorld->PersistentLevel)
    {
        Why = TEXT("live additions must be in the current persistent level");
        return false;
    }
    if (!Imported.Num() || Imported.Contains(Actor->GetFName()))
    {
        Why = TEXT("the live mirror cannot prove this is a new actor outside the imported actor set");
        return false;
    }
    if (const AStaticMeshActor* Static = Cast<AStaticMeshActor>(Actor))
    {
        const UStaticMeshComponent* Mesh = Static->GetStaticMeshComponent();
        if (Mesh && Mesh->GetStaticMesh())
            return true;
        Why = TEXT("the static-mesh actor has no mesh assigned");
        return false;
    }
    const UClass* Class = Actor->GetClass();
    for (const UClass* Each = Class; Each; Each = Each->GetSuperClass())
        if (P4IsGameClass(Each))
            return true;
    if (Cast<UBlueprintGeneratedClass>(Class) && Class->ClassGeneratedBy &&
        Class->ClassGeneratedBy->IsA<UBlueprint>() && P4IsContentPackageIgnoringCase(Class->GetOutermost()->GetName()))
        return true;
    Why = TEXT("its actor class is not supported by the live mirror");
    return false;
}

bool OnlyPreviewDelta(UWorld* SourceWorld, UWorld* PreviewWorld, const FP4LevelChangesResult& Changes,
    bool bAllowLiveAdded, const TSet<FName>& Imported, FString& Error)
{
    Error.Reset();
    if (!Changes.IsValid())
    {
        Error = TEXT("The preview could not verify this level's unsaved changes: ") + P4LevelChangesErrorText(Changes);
        return false;
    }
    if (Changes.Removed.Num() || Changes.LevelChanges.Before.Num() || Changes.LevelChanges.After.Num())
    {
        Error = TEXT("This level has removed actors or level changes that need an exported map refresh before Play.");
        return false;
    }
    const auto IsStart = [&](const FString& Name)
    {
        AActor* Actor = SourceWorld && SourceWorld->PersistentLevel
            ? FindObject<AActor>(SourceWorld->PersistentLevel, *Name)
            : nullptr;
        if (Actor && Actor->IsA<APlayerStart>())
            return true;
        Error = TEXT("The unsaved change to ") + Name +
            TEXT(
                " needs an exported map refresh before Play. Only Player Start edits can currently bypass that refresh.");
        return false;
    };
    for (const FString& Name : Changes.Added)
    {
        AActor* Actor = SourceWorld && SourceWorld->PersistentLevel
            ? FindObject<AActor>(SourceWorld->PersistentLevel, *Name)
            : nullptr;
        if (Actor && Actor->IsA<APlayerStart>())
            continue;
        FString Why;
        if (bAllowLiveAdded && CanRecreateAdded(PreviewWorld, Actor, Imported, Why))
            continue;
        Error = TEXT("The unsaved addition ") + Name + TEXT(" needs Save and Export before Play: ") +
            (bAllowLiveAdded ? Why : FString(TEXT("only Player Start additions are admitted"))) + TEXT(".");
        return false;
    }
    for (const FP4LevelActorMove& Move : Changes.Moved)
        if (!IsStart(Move.Actor))
            return false;
    for (const FP4LevelActorChanges& Change : Changes.OtherChanged)
        if (!IsStart(Change.Actor))
            return false;
    return true;
}

bool OnlyDirtyPreviewChanges(
    UWorld* World, bool bAllowLiveAdded, FString& Error, const FP4GamePreviewBaseline* Baseline = nullptr)
{
    Error.Reset();
    TArray<UWorld*> Sources;
    if (!SourceWorlds(World, Sources, Error))
        return false;
    if (Baseline && Baseline->Root.IsValid() && !Baseline->IsFor(World))
    {
        Error = TEXT(
            "The loaded source levels changed since the clean preview snapshot. Save and reopen the map before Play.");
        return false;
    }
    const TSet<FName> Imported = bAllowLiveAdded ? P4ImportedLevelActors(World) : TSet<FName>();
    for (UWorld* Source : Sources)
    {
        if (!Source->GetOutermost()->IsDirty())
            continue;
        FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(Source);
        if (Baseline && Baseline->Root.IsValid())
        {
            const FP4GamePreviewBaseline::FSource* Before = Baseline->Sources.FindByPredicate(
                [Source](const FP4GamePreviewBaseline::FSource& Entry) { return Entry.World.Get() == Source; });
            TSet<FString> Starts;
            for (AActor* Actor : Source->PersistentLevel->Actors)
                if (IsValid(Actor) && Actor->IsA<APlayerStart>())
                    Starts.Add(Actor->GetName());
            if (!Before || !P4GamePreviewDeltaSinceBaseline(Before->Changes, Changes, Error, Starts))
            {
                Error = Source->GetOutermost()->GetName() + TEXT(": ") + Error;
                return false;
            }
        }
        if (!OnlyPreviewDelta(Source, World, Changes, bAllowLiveAdded, Imported, Error))
        {
            Error = Source->GetOutermost()->GetName() + TEXT(": ") + Error;
            return false;
        }
    }
    return true;
}
} //namespace

FString FP4GamePreviewStartResult::FingerprintText() const
{
    const FQuat Q = Transform.GetRotation();
    const FVector P = Transform.GetLocation(), S = Transform.GetScale3D();
    const float Values[] = {Q.X, Q.Y, Q.Z, Q.W, P.X, P.Y, P.Z, S.X, S.Y, S.Z, FloorPoint.X, FloorPoint.Y, FloorPoint.Z,
        FloorNormal.X, FloorNormal.Y, FloorNormal.Z};
    FString Evidence = HasFloor ? TEXT("preview-start/1|floor|") : TEXT("preview-start/1|no-floor|");
    for (float Value : Values)
    {
        // Encode every float bit, avoiding ToString's rounded decimal precision.
        // Signed zero is equivalent for a transform or contact normal.
        const float Canonical = Value == 0.f ? 0.f : Value;
        uint32 Bits = 0;
        FMemory::Memcpy(&Bits, &Canonical, sizeof(Bits));
        Evidence += FString::Printf(TEXT("%08x"), Bits);
    }
    return Evidence;
}

void FP4GamePreviewBaseline::Reset()
{
    Root.Reset();
    Sources.Reset();
}

bool FP4GamePreviewBaseline::IsFor(UWorld* World) const
{
    if (!World || Root.Get() != World)
        return false;
    TArray<UWorld*> Current;
    FString Error;
    if (!SourceWorlds(World, Current, Error) || Current.Num() != Sources.Num())
        return false;
    for (UWorld* Source : Current)
    {
        const FSource* Entry = Sources.FindByPredicate(
            [Source](const FSource& Item) { return Item.World.Get() == Source; });
        if (!Entry || Entry->Level.Get() != Source->PersistentLevel ||
            Entry->Changes.Package != Source->GetOutermost()->GetName())
            return false;
    }
    return true;
}

bool P4CaptureGamePreviewCleanBaseline(UWorld* World, FP4GamePreviewBaseline& Out, FString& Error)
{
    Error.Reset();
    TArray<UWorld*> Sources;
    if (!SourceWorlds(World, Sources, Error))
        return false;
    if (World->WorldType != EWorldType::Editor || P4IsGamePreviewScratchWorld(World))
    {
        Error = TEXT("Only an imported editor world can provide a clean preview snapshot.");
        return false;
    }
    for (UWorld* Source : Sources)
        if (Source->GetOutermost()->IsDirty())
        {
            Error = TEXT("A preview snapshot cannot be captured from unsaved source changes.");
            return false;
        }
    FP4GamePreviewBaseline Snapshot;
    Snapshot.Root = World;
    for (UWorld* Source : Sources)
    {
        FP4GamePreviewBaseline::FSource Entry;
        Entry.World = Source;
        Entry.Level = Source->PersistentLevel;
        Entry.Changes = P4AnalyzeOriginalLevelChanges(Source);
        if (!Entry.Changes.IsValid())
        {
            Error = TEXT("The clean preview snapshot could not read ") + Source->GetOutermost()->GetName() +
                TEXT(": ") + P4LevelChangesErrorText(Entry.Changes);
            return false;
        }
        Snapshot.Sources.Add(MoveTemp(Entry));
    }
    for (UWorld* Source : Sources)
        if (Source->GetOutermost()->IsDirty())
        {
            Error = TEXT("The source changed while its clean preview snapshot was being captured.");
            return false;
        }
    Out = MoveTemp(Snapshot);
    return true;
}

bool P4GamePreviewDeltaSinceBaseline(const FP4LevelChangesResult& Baseline, FP4LevelChangesResult& Current,
    FString& Error, const TSet<FString>& MutableStarts)
{
    Error.Reset();
    if (!Current.IsValid())
    {
        Error = TEXT("The preview source comparison failed: ") + P4LevelChangesErrorText(Current);
        return false;
    }
    if (!Baseline.IsValid() || Baseline.Package != Current.Package || Baseline.Level != Current.Level ||
        Baseline.ComparisonSchema != Current.ComparisonSchema)
    {
        Error = TEXT("The preview source comparison no longer matches its clean snapshot.");
        return false;
    }
    if (!SameLines(Baseline.LevelChanges, Current.LevelChanges))
    {
        Error = TEXT("The level's structure or build data changed since clean Play. Save and Export before Play.");
        return false;
    }
    Current.LevelChanges = FP4LevelLineChanges();
    for (const FString& Removed : Baseline.Removed)
    {
        const FP4RemovedActor* Was = Baseline.RemovedActors.Find(Removed);
        const FP4RemovedActor* Now = Current.RemovedActors.Find(Removed);
        if (!Current.Removed.Contains(Removed) || !Was || !Now || Was->Class != Now->Class ||
            Was->PlacementNumber != Now->PlacementNumber)
        {
            Error = TEXT("The original actor set changed since clean Play. Save and Export before Play.");
            return false;
        }
        Current.Removed.Remove(Removed);
        Current.RemovedActors.Remove(Removed);
    }
    for (const FP4LevelActorMove& Was : Baseline.Moved)
    {
        const int32 Index = Current.Moved.IndexOfByPredicate(
            [&Was](const FP4LevelActorMove& Now) { return Now.Actor == Was.Actor; });
        const bool Same = Index != INDEX_NONE && Current.Moved[Index].Location == Was.Location &&
            Current.Moved[Index].Rotation == Was.Rotation && Current.Moved[Index].Scale == Was.Scale;
        if (!Same && !MutableStarts.Contains(Was.Actor))
        {
            Error = TEXT("The original transform of ") + Was.Actor +
                TEXT(" changed since clean Play. Save and Export before Play.");
            return false;
        }
        if (Same)
            Current.Moved.RemoveAt(Index);
    }
    for (const FP4LevelActorChanges& Was : Baseline.OtherChanged)
    {
        const int32 Index = Current.OtherChanged.IndexOfByPredicate(
            [&Was](const FP4LevelActorChanges& Now) { return Now.Actor == Was.Actor; });
        const bool Same = Index != INDEX_NONE && SameLines(Was.Lines, Current.OtherChanged[Index].Lines);
        if (!Same && !MutableStarts.Contains(Was.Actor))
        {
            Error = TEXT("The original properties of ") + Was.Actor +
                TEXT(" changed since clean Play. Save and Export before Play.");
            return false;
        }
        if (Same)
            Current.OtherChanged.RemoveAt(Index);
    }
    return true;
}

bool P4GamePreviewOnlyStartChanges(UWorld* World, FString& Error)
{
    return OnlyDirtyPreviewChanges(World, false, Error);
}

bool P4GamePreviewOnlyLiveChanges(UWorld* World, FString& Error, const FP4GamePreviewBaseline* Baseline)
{
    return OnlyDirtyPreviewChanges(World, true, Error, Baseline);
}

bool P4GamePreviewStart(UWorld* World, bool bScratchWorld, FTransform& OutTransform, FString& Source, FString& Error,
    float CapsuleRadius, float CapsuleHalfHeight)
{
    FP4GamePreviewStartResult Selected;
    const bool Resolved = P4GamePreviewStart(
        World, bScratchWorld, Selected, Source, Error, CapsuleRadius, CapsuleHalfHeight);
    OutTransform = Selected.Transform;
    return Resolved;
}

bool P4GamePreviewStart(UWorld* World, bool bScratchWorld, FP4GamePreviewStartResult& Out, FString& Source,
    FString& Error, float CapsuleRadius, float CapsuleHalfHeight)
{
    Out = FP4GamePreviewStartResult();
    Source.Reset();
    Error.Reset();
    if (!World || !FMath::IsFinite(CapsuleRadius) || !FMath::IsFinite(CapsuleHalfHeight) || CapsuleRadius <= 0 ||
        CapsuleHalfHeight < CapsuleRadius)
    {
        Error = TEXT("The preview needs an editor level and valid player clearance.");
        return false;
    }
    const FCollisionQueryParams Query = StartQuery(World);
    struct FCandidate
    {
        FP4GamePreviewStartResult Result;
        FString Name;
    };
    TArray<FCandidate> Clear;
    int32 Starts = 0;
    FString FirstFailure;
    for (TActorIterator<APlayerStart> It(World); It; ++It)
    {
        APlayerStart* Start = *It;
        const ULevel* Level = Start->GetLevel();
        if (Start->bIsEditorPreviewActor || Start->HasAnyFlags(RF_Transient) ||
            (Level && (Level->HasAnyFlags(RF_Transient) || !Level->bIsVisible)))
            continue;
        ++Starts;
        FCandidate Candidate;
        Candidate.Name = Start->GetActorLabel();
        FString Why;
        if (!ClearStart(World, Start->GetActorLocation(), Start->GetActorRotation().Yaw, false, CapsuleRadius,
                CapsuleHalfHeight, Query, Candidate.Result, Why))
        {
            if (FirstFailure.IsEmpty())
                FirstFailure = Candidate.Name + TEXT(": ") + Why + TEXT(".");
            continue;
        }
        // This is the same priority used by GameModeBase::ChoosePlayerStart.
        // Editor selection alone does not choose a start in native UE4.
        if (Start->IsA<APlayerStartPIE>())
        {
            Out = Candidate.Result;
            Source = TEXT("Player Start: ") + Candidate.Name;
            return true;
        }
        Clear.Add(MoveTemp(Candidate));
    }
    if (Clear.Num())
    {
        const FCandidate& Chosen = Clear[FMath::RandRange(0, Clear.Num() - 1)];
        Out = Chosen.Result;
        Source = TEXT("Player Start: ") + Chosen.Name;
        return true;
    }
    if (Starts)
    {
        Error = TEXT("No Player Start has a safe landing. ") + FirstFailure;
        return false;
    }
    if (!bScratchWorld)
        return false;
    FLevelEditorViewportClient* Camera = CameraFor(World);
    if (!Camera)
    {
        Error = TEXT("Place a Player Start above a collision floor, or open a perspective viewport before Play.");
        return false;
    }
    FString Why;
    if (!ClearStart(World, Camera->GetViewLocation(), Camera->GetViewRotation().Yaw, true, CapsuleRadius,
            CapsuleHalfHeight, Query, Out, Why))
    {
        Error = TEXT("The editor camera cannot provide a safe preview start: ") + Why +
            TEXT(". Place a Player Start above a collision floor.");
        return false;
    }
    Source = TEXT("Editor camera");
    return true;
}

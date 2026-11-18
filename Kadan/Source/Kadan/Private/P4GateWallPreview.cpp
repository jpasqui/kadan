// The gate wall preview: what the game's wall build does when a gate begins play, drawn in the editor.
#include "P4GateWallPreview.h"
#include "P4NativeConstructionPreview.h"
#include "P4MapKitCore/gate_wall.h"
#include "AI/NavigationSystemBase.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorUndoClient.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

namespace
{
namespace G = P4MapKit::GateWall;

// What the map holds of a component the preview moves, hides or stretches.
struct FOwn
{
    FVector Location = FVector::ZeroVector, Scale = FVector::OneVector, Extent = FVector::ZeroVector;
    bool bVisible = true;
};

struct FGate
{
    // By component name: construction makes a gate's components again, and the map's values stay the ones read
    // before the preview first changed them.
    TMap<FName, FOwn> Own;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> Pieces;
    bool bShown = false;
    bool bExchange = false;
};

TMap<TWeakObjectPtr<AActor>, FGate>& Gates()
{
    static TMap<TWeakObjectPtr<AActor>, FGate> Held;
    return Held;
}

FDelegateHandle PreChangeHandle, ChangedHandle, BeginMoveHandle, EndMoveHandle, MovedHandle, AddedHandle;

// An undo or redo puts the gates' components back as the transaction recorded them: they are drawn again after.
class FGateUndo final : public FEditorUndoClient
{
public:
    void PostUndo(bool) override
    {
        Again();
    }
    void PostRedo(bool) override
    {
        Again();
    }

private:
    static void Again();
};
TUniquePtr<FGateUndo> Undo;

bool DerivesNative(const UClass* Class, const TCHAR* Name)
{
    for (; Class; Class = Class->GetSuperClass())
        if (Class->HasAnyClassFlags(CLASS_Native) && Class->GetName() == Name)
            return true;
    return false;
}

template <class TProperty> TProperty* Field(const AActor* Actor, const TCHAR* Name)
{
    return FindFProperty<TProperty>(Actor->GetClass(), Name);
}

int32 Int(const AActor* Actor, const TCHAR* Name)
{
    const FIntProperty* Property = Field<FIntProperty>(Actor, Name);
    return Property ? Property->GetPropertyValue_InContainer(Actor) : 0;
}

float Float(const AActor* Actor, const TCHAR* Name)
{
    const FFloatProperty* Property = Field<FFloatProperty>(Actor, Name);
    return Property ? Property->GetPropertyValue_InContainer(Actor) : 0.f;
}

bool Bool(const AActor* Actor, const TCHAR* Name)
{
    const FBoolProperty* Property = Field<FBoolProperty>(Actor, Name);
    return Property && Property->GetPropertyValue_InContainer(Actor);
}

// A mesh list of the gate's class, as the game reads it: a TArray of static meshes.
TArray<UStaticMesh*> Meshes(const AActor* Actor, const TCHAR* Name)
{
    TArray<UStaticMesh*> Out;
    const FArrayProperty* Property = Field<FArrayProperty>(Actor, Name);
    const FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
    if (!Inner)
        return Out;
    FScriptArrayHelper Items(Property, Property->ContainerPtrToValuePtr<void>(Actor));
    for (int32 Index = 0; Index < Items.Num(); ++Index)
        Out.Add(Cast<UStaticMesh>(Inner->GetObjectPropertyValue(Items.GetRawPtr(Index))));
    return Out;
}

bool Tagged(const UActorComponent* Component, const TCHAR* Tag)
{
    return Component && Component->ComponentHasTag(FName(Tag));
}

bool OwnPiece(const FGate& State, const UActorComponent* Component)
{
    return State.Pieces.ContainsByPredicate(
        [&](const TWeakObjectPtr<UStaticMeshComponent>& Piece) { return Piece.Get() == Component; });
}

// The map's values of a component, read the first time the preview touches it.
void Keep(FGate& State, USceneComponent* Component)
{
    if (State.Own.Contains(Component->GetFName()))
        return;
    FOwn& Own = State.Own.Add(Component->GetFName());
    Own.Location = Component->GetRelativeLocation();
    Own.Scale = Component->GetRelativeScale3D();
    Own.bVisible = Component->IsVisible();
    if (const UBoxComponent* Box = Cast<UBoxComponent>(Component))
        Own.Extent = Box->GetUnscaledBoxExtent();
}

void Moved(USceneComponent* Component)
{
    Component->MarkRenderStateDirty();
    FNavigationSystem::UpdateComponentData(*Component);
}

void Place(FGate& State, USceneComponent* Component, const FVector& Location)
{
    Keep(State, Component);
    if (!Component->GetRelativeLocation().Equals(Location, 0.001f))
        Component->SetRelativeLocation(Location);
    Moved(Component);
}

void Stretch(FGate& State, UBoxComponent* Box, const FVector& Location, const FVector& Extent)
{
    Keep(State, Box);
    Box->SetBoxExtent(Extent, false);
    Place(State, Box, Location);
}

void DropPieces(FGate& State)
{
    for (const TWeakObjectPtr<UStaticMeshComponent>& Piece : State.Pieces)
        if (UStaticMeshComponent* Held = Piece.Get())
            Held->DestroyComponent();
    State.Pieces.Reset();
}

// The map's own values again, and no pieces of the preview's.
void Restore(AActor* Gate, FGate& State)
{
    DropPieces(State);
    TInlineComponentArray<USceneComponent*> Parts(Gate);
    for (USceneComponent* Part : Parts)
    {
        const FOwn* Own = Part ? State.Own.Find(Part->GetFName()) : nullptr;
        if (!Own)
            continue;
        if (UBoxComponent* Box = Cast<UBoxComponent>(Part))
            Box->SetBoxExtent(Own->Extent, false);
        Part->SetRelativeScale3D(Own->Scale);
        Part->SetRelativeLocation(Own->Location);
        Part->SetVisibility(Own->bVisible);
        Moved(Part);
    }
    State.bShown = false;
}

bool Show(AActor* Gate, FGate& State);

// Saves and exports take the preview out and put it back through this.
void Exchange(const TWeakObjectPtr<AActor>& Weak)
{
    AActor* Gate = Weak.Get();
    FGate* State = Gate ? Gates().Find(Weak) : nullptr;
    if (!State)
        return;
    if (P4NativePreviewsSuspended())
        Restore(Gate, *State);
    else
        Show(Gate, *State);
}

UStaticMeshComponent* Template(AActor* Gate)
{
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Gate);
    for (UStaticMeshComponent* Mesh : Meshes)
        if (Mesh && Mesh->GetName().Contains(TEXT("Mesh_Inside_00"), ESearchCase::IgnoreCase))
            return Mesh;
    for (UStaticMeshComponent* Mesh : Meshes)
        if (Tagged(Mesh, TEXT("PostureMesh")))
            return Mesh;
    return nullptr;
}

// A piece as the game adds one: a mesh component of the template's kind with the template's game options and
// materials, the bucket's last mesh and the piece's tags, held by no save (transient) and made again by
// construction (UserConstructionScript). The engine's own fields of the template (its attachments among them) are
// not copied.
UStaticMeshComponent* AddPiece(AActor* Gate, UStaticMeshComponent* Template, USceneComponent* Parent,
    UStaticMesh* Mesh, const G::Piece& Spec, const FVector& Location)
{
    UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Gate, Template->GetClass(),
        MakeUniqueObjectName(Gate, Template->GetClass(), TEXT("KadanGateWallPiece")),
        RF_Transient | RF_TextExportTransient | RF_DuplicateTransient | RF_NonPIEDuplicateTransient);
    for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
    {
        const UClass* Owner = It->GetOwnerClass();
        if (!Owner || UStaticMeshComponent::StaticClass()->IsChildOf(Owner) ||
            It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
            continue;
        It->CopyCompleteValue_InContainer(Piece, Template);
    }
    Piece->OverrideMaterials = Template->OverrideMaterials;
    Piece->bReceivesDecals = Template->bReceivesDecals;
    Piece->CastShadow = Template->CastShadow;
    Piece->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    Piece->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    Piece->bIsEditorOnly = true;
    Piece->ComponentTags.Reset();
    for (const std::string& Tag : Spec.Tags)
        Piece->ComponentTags.Add(FName(UTF8_TO_TCHAR(Tag.c_str())));
    Piece->SetMobility(EComponentMobility::Movable);
    Piece->SetCanEverAffectNavigation(false);
    Piece->SetStaticMesh(Mesh);
    Piece->SetRelativeTransform(FTransform(FQuat::Identity, Location, FVector::OneVector));
    Piece->SetVisibility(true);
    Piece->SetupAttachment(Parent);
    Piece->RegisterComponent();
    return Piece;
}

bool Show(AActor* Gate, FGate& State)
{
    const bool bAddWall = Bool(Gate, TEXT("bAddWall"));
    UStaticMeshComponent* Inside = Template(Gate);
    if (!bAddWall || !Inside)
    {
        Restore(Gate, State);
        return false;
    }
    G::Options In;
    In.AddWallNum = Int(Gate, TEXT("AddWallNum"));
    In.WallWidth = Float(Gate, TEXT("WallWidth"));
    In.OutSideLWidth = Float(Gate, TEXT("OutSideLWidth"));
    In.OutSideRWidth = Float(Gate, TEXT("OutSideRWidth"));
    In.PillarWidth = Float(Gate, TEXT("PillarWidth"));
    In.UseHigherNavBox = Bool(Gate, TEXT("bUseHigherNavBox"));
    const TArray<UStaticMesh*> Ground = Meshes(Gate, TEXT("GroundMeshList"));
    In.GroundMeshes = Ground.Num();
    const G::Layout Wall = G::Lay(In);
    const TArray<UStaticMesh*> Lists[3] = {
        Meshes(Gate, TEXT("AddWallMesh05")), Meshes(Gate, TEXT("AddWallMesh02")), Meshes(Gate, TEXT("AddWallMesh01"))};

    DropPieces(State);
    TInlineComponentArray<USceneComponent*> Parts(Gate);
    // The physics component's place, before the preview moves it: the inside collision follows it.
    USceneComponent* Physics = nullptr;
    for (USceneComponent* Part : Parts)
        if (Tagged(Part, TEXT("Physics")))
        {
            Physics = Part;
            break;
        }
    const FOwn* PhysicsOwn = Physics ? State.Own.Find(Physics->GetFName()) : nullptr;
    const float PhysicsX = PhysicsOwn ? PhysicsOwn->Location.X : Physics ? Physics->GetRelativeLocation().X : 0.f;
    const float InsideX = PhysicsX + Wall.Middle;
    USceneComponent* FarSide = nullptr;
    USceneComponent* InsideCollision = nullptr;
    for (USceneComponent* Part : Parts)
    {
        if (!Part || OwnPiece(State, Part))
            continue;
        const FVector At = State.Own.Contains(Part->GetFName()) ? State.Own[Part->GetFName()].Location
                                                                : Part->GetRelativeLocation();
        if (Tagged(Part, TEXT("OutSideR")) || Tagged(Part, TEXT("GroundOutSideR")))
        {
            Place(State, Part, FVector(Wall.OutSideRX, At.Y, At.Z));
            if (Tagged(Part, TEXT("OutSideR")))
                FarSide = Part;
        }
        else if (Tagged(Part, TEXT("PillarL")))
            Place(State, Part, FVector(Wall.PillarLX, At.Y, At.Z));
        else if (Tagged(Part, TEXT("PillarR")))
            Place(State, Part, FVector(Wall.PillarRX, At.Y, At.Z));
        else if (Part == Physics)
            Place(State, Part, FVector(InsideX, At.Y, At.Z));
        else if (Tagged(Part, TEXT("AddWall")) || Tagged(Part, TEXT("AddGround")))
        {
            // The pieces the map's level holds give way to the preview's.
            Keep(State, Part);
            Part->SetVisibility(false);
        }
        if (UBoxComponent* Box = Cast<UBoxComponent>(Part))
        {
            if (Tagged(Box, TEXT("Nav_Box")))
                Stretch(State, Box, FVector(Wall.NavBoxLocation.X, Wall.NavBoxLocation.Y, Wall.NavBoxLocation.Z),
                    FVector(Wall.NavBoxExtent.X, Wall.NavBoxExtent.Y, Wall.NavBoxExtent.Z));
            else if (Tagged(Box, TEXT("LaterTaskTrigger")))
                Stretch(State, Box,
                    FVector(Wall.LaterTaskLocation.X, Wall.LaterTaskLocation.Y, Wall.LaterTaskLocation.Z),
                    FVector(Wall.LaterTaskExtent.X, Wall.LaterTaskExtent.Y, Wall.LaterTaskExtent.Z));
        }
        if (Tagged(Part, TEXT("Col_Inside")))
        {
            Keep(State, Part);
            const FOwn& Own = State.Own[Part->GetFName()];
            Part->SetRelativeScale3D(FVector(Wall.InsideScaleX, 1.f, 1.f));
            Place(State, Part, FVector(InsideX, Own.Location.Y, Own.Location.Z));
            InsideCollision = Part;
        }
    }
    // The ground collision takes the inside one's scale and place, the far side's collision the far side's place.
    for (USceneComponent* Part : Parts)
    {
        if (!Part || OwnPiece(State, Part))
            continue;
        if (InsideCollision && Tagged(Part, TEXT("Col_Ground")))
        {
            Keep(State, Part);
            Part->SetRelativeScale3D(InsideCollision->GetRelativeScale3D());
            Place(State, Part, InsideCollision->GetRelativeLocation());
        }
        else if (FarSide && Tagged(Part, TEXT("Col_OutsideR")))
            Place(State, Part, FarSide->GetRelativeLocation());
    }

    const FOwn* InsideOwn = State.Own.Find(Inside->GetFName());
    const FVector InsideAt = InsideOwn ? InsideOwn->Location : Inside->GetRelativeLocation();
    USceneComponent* Root = Gate->GetRootComponent();
    for (const G::Piece& Spec : Wall.Pieces)
    {
        UStaticMesh* Mesh = nullptr;
        if (Spec.Ground)
            Mesh = Ground.IsValidIndex(Spec.Bucket) ? Ground[Spec.Bucket] : nullptr;
        else if (Lists[Spec.Bucket].Num())
            Mesh = Lists[Spec.Bucket].Last();
        if (Spec.Ground)
            State.Pieces.Add(AddPiece(
                Gate, Inside, Root, Mesh, Spec, FVector(InsideAt.X + Spec.X, InsideAt.Y, InsideAt.Z)));
        else
            State.Pieces.Add(AddPiece(Gate, Inside, Inside, Mesh, Spec, FVector(Spec.X, 0.f, 0.f)));
    }
    State.bShown = true;
    return true;
}
} // namespace

void FGateUndo::Again()
{
    if (GEditor)
        P4ApplyGateWallPreviews(GEditor->GetEditorWorldContext().World());
}

bool P4IsGateWall(const AActor* Actor)
{
    return Actor && DerivesNative(Actor->GetClass(), TEXT("VarGateBaseActor"));
}

bool P4ApplyGateWallPreview(AActor* Gate)
{
    if (!IsValid(Gate) || !P4IsGateWall(Gate) || P4NativePreviewsSuspended() || Gate->IsTemplate())
        return false;
    UWorld* World = Gate->GetWorld();
    if (!World || World->WorldType != EWorldType::Editor)
        return false;
    // A preview is not an edit: the gate's level keeps whether it had unsaved changes.
    UPackage* Package = Gate->GetOutermost();
    const bool WasDirty = Package->IsDirty();
    ON_SCOPE_EXIT
    {
        Package->SetDirtyFlag(WasDirty);
    };
    const TWeakObjectPtr<AActor> Weak(Gate);
    FGate& State = Gates().FindOrAdd(Weak);
    if (!State.bExchange)
    {
        State.bExchange = true;
        P4RememberNativePreviewExchange(Gate, TEXT("GateWall"), [Weak]() { Exchange(Weak); });
    }
    return Show(Gate, State);
}

int32 P4ApplyGateWallPreviews(UWorld* World)
{
    if (!World || World->WorldType != EWorldType::Editor || P4NativePreviewsSuspended())
        return 0;
    int32 Shown = 0;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
                Shown += P4IsGateWall(Actor) && P4ApplyGateWallPreview(Actor);
    return Shown;
}

TArray<UStaticMeshComponent*> P4GateWallPreviewPieces(AActor* Gate)
{
    TArray<UStaticMeshComponent*> Out;
    if (const FGate* State = Gates().Find(TWeakObjectPtr<AActor>(Gate)))
        for (const TWeakObjectPtr<UStaticMeshComponent>& Piece : State->Pieces)
            if (UStaticMeshComponent* Held = Piece.Get())
                Out.Add(Held);
    return Out;
}

void P4StartGateWallPreviews()
{
    const auto GateOf = [](UObject* Object) -> AActor*
    {
        AActor* Actor = Cast<AActor>(Object);
        return P4IsGateWall(Actor) && !Actor->IsTemplate() ? Actor : nullptr;
    };
    // Before an option changes, the gate has the map's values, so the construction that follows and the undo
    // record see the map; after it, the wall is drawn again for the new values.
    if (!PreChangeHandle.IsValid())
        PreChangeHandle = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [GateOf](UObject* Object, const FEditPropertyChain&)
            {
                if (AActor* Gate = GateOf(Object))
                    if (FGate* State = Gates().Find(TWeakObjectPtr<AActor>(Gate)))
                        if (State->bShown && !P4NativePreviewsSuspended())
                            Restore(Gate, *State);
            });
    if (!ChangedHandle.IsValid())
        ChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [GateOf](UObject* Object, FPropertyChangedEvent&)
            {
                if (AActor* Gate = GateOf(Object))
                    P4ApplyGateWallPreview(Gate);
            });
    if (!GEditor)
        return;
    if (!BeginMoveHandle.IsValid())
        BeginMoveHandle = GEditor->OnBeginObjectMovement().AddLambda(
            [GateOf](UObject& Object)
            {
                if (AActor* Gate = GateOf(&Object))
                    if (FGate* State = Gates().Find(TWeakObjectPtr<AActor>(Gate)))
                        if (State->bShown && !P4NativePreviewsSuspended())
                            Restore(Gate, *State);
            });
    if (!EndMoveHandle.IsValid())
        EndMoveHandle = GEditor->OnEndObjectMovement().AddLambda(
            [GateOf](UObject& Object)
            {
                if (AActor* Gate = GateOf(&Object))
                    P4ApplyGateWallPreview(Gate);
            });
    if (!MovedHandle.IsValid())
        MovedHandle = GEditor->OnActorMoved().AddLambda([](AActor* Actor) { P4ApplyGateWallPreview(Actor); });
    if (!AddedHandle.IsValid() && GEngine)
        AddedHandle = GEngine->OnLevelActorAdded().AddLambda([](AActor* Actor) { P4ApplyGateWallPreview(Actor); });
    if (!Undo)
    {
        Undo = MakeUnique<FGateUndo>();
        GEditor->RegisterForUndo(Undo.Get());
    }
}

void P4StopGateWallPreviews()
{
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreChangeHandle);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ChangedHandle);
    PreChangeHandle.Reset();
    ChangedHandle.Reset();
    if (GEditor)
    {
        GEditor->OnBeginObjectMovement().Remove(BeginMoveHandle);
        GEditor->OnEndObjectMovement().Remove(EndMoveHandle);
        GEditor->OnActorMoved().Remove(MovedHandle);
    }
    if (GEngine)
        GEngine->OnLevelActorAdded().Remove(AddedHandle);
    if (Undo && GEditor)
        GEditor->UnregisterForUndo(Undo.Get());
    Undo.Reset();
    BeginMoveHandle.Reset();
    EndMoveHandle.Reset();
    MovedHandle.Reset();
    AddedHandle.Reset();
}

#include "P4GroundTypes.h"
#include "P4GameNames.h"
#include "P4StockEngineFields.h"
#include "LandscapeProxy.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "P4CreatorSource.h"
#include "P4KadanReadOnly.h"
#include "P4OriginalRecord.h"
#include "P4PlacementExport.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
struct FGroundFields
{
    FStructProperty* Container = nullptr;
    FArrayProperty* Tags = nullptr;
    FStructProperty* Row = nullptr;
    FUInt32Property* Value = nullptr;
    explicit FGroundFields(UClass* Class)
    {
        Container = FindFProperty<FStructProperty>(Class, TEXT("C4TerrainTagContainer"));
        Tags = Container ? FindFProperty<FArrayProperty>(Container->Struct, TEXT("TerrainTags")) : nullptr;
        Row = Tags ? CastField<FStructProperty>(Tags->Inner) : nullptr;
        Value = Row ? FindFProperty<FUInt32Property>(Row->Struct, TEXT("TerrainTag")) : nullptr;
    }
    bool IsValid() const { return Container && Tags && Row && Value; }
    void* Array(UObject* Object) const
    {
        return Tags->ContainerPtrToValuePtr<void>(Container->ContainerPtrToValuePtr<void>(Object));
    }
};
FString SurfaceName(FString Slot)
{
    if (Slot.StartsWith(TEXT("MI_Tag_")))
        Slot = Slot.Mid(7);
    TArray<FString> Parts;
    Slot.ParseIntoArray(Parts, TEXT("_"));
    if (Parts.Num() < 2 || Parts[0] != TEXT("G"))
        return FString();
    FString Name;
    for (int32 Index = 0; Index < Parts[1].Len(); ++Index)
    {
        const TCHAR Letter = Parts[1][Index];
        if (Index && FChar::IsUpper(Letter) && FChar::IsLower(Parts[1][Index - 1]))
            Name += TEXT(' ');
        Name += Letter;
    }
    return Name;
}
bool SetTags(UStaticMeshComponent* Component, uint32 Tag)
{
    FGroundFields Fields(Component->GetClass());
    UStaticMesh* Mesh = Component->GetStaticMesh();
    if (!Fields.IsValid() || !Mesh || !Mesh->StaticMaterials.Num())
        return false;
    FScriptArrayHelper Rows(Fields.Tags, Fields.Array(Component));
    Rows.EmptyAndAddValues(Mesh->StaticMaterials.Num());
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
        Fields.Value->SetPropertyValue_InContainer(Rows.GetRawPtr(Index), Tag);
    return true;
}
bool ImportedAsset(UObject* Asset)
{
    if (!Asset || P4HasCreatorSource(Asset))
        return false;
    const auto Status = P4QueryOriginalAsset(Asset);
    return Status.IsOriginal() && Status.Error.IsEmpty() && Status.Record.Package == Asset->GetOutermost()->GetName();
}
bool ImportedComponent(UStaticMeshComponent* Component)
{
    if (const AActor* Owner = Component->GetOwner())
    {
        if (!Owner->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
            return P4Kadan::GameActorOf(Owner) != nullptr;
        UObject* Blueprint = Owner->GetClass()->ClassGeneratedBy;
        return ImportedAsset(Blueprint);
    }
    // Blueprint SCS templates do not yet have an actor owner. Their original asset owns them.
    for (UObject* Outer = Component->GetOuter(); Outer && !Outer->IsA<UPackage>(); Outer = Outer->GetOuter())
    {
        if (UClass* Class = Cast<UClass>(Outer))
            if (Class->ClassGeneratedBy)
                return ImportedAsset(Class->ClassGeneratedBy);
        if (Outer->IsAsset())
            return ImportedAsset(Outer);
    }
    return false;
}
} //namespace

bool P4ReadGroundTags(UStaticMeshComponent* Component, TArray<uint32>& Out)
{
    if (!IsInGameThread() || !IsValid(Component))
        return false;
    const FGroundFields Fields(Component->GetClass());
    if (!Fields.IsValid())
        return false;
    FScriptArrayHelper Rows(Fields.Tags, Fields.Array(Component));
    if (Rows.Num() > 256)
        return false;
    TArray<uint32> Values;
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
        Values.Add(Fields.Value->GetPropertyValue_InContainer(Rows.GetRawPtr(Index)));
    Out = MoveTemp(Values);
    return true;
}

TArray<FP4GroundType> P4ImportedGroundTypes()
{
    struct FChoice
    {
        FP4GroundType Type;
        int32 Count = 0;
    };
    TMap<uint32, TMap<uint32, FChoice>> Surfaces;
    if (!IsInGameThread())
        return {};
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* Component = *It;
        if (!IsValid(Component))
            continue;
        UStaticMesh* Mesh = Component->GetStaticMesh();
        if (!IsValid(Mesh) || !P4HasIntrinsicGameSource(Mesh) || !ImportedComponent(Component))
            continue;
        TArray<uint32> Tags;
        if (!P4ReadGroundTags(Component, Tags) || Tags.Num() != Mesh->StaticMaterials.Num())
            continue;
        for (int32 Index = 0; Index < Tags.Num(); ++Index)
        {
            const uint32 Tag = Tags[Index];
            if ((Tag & 0xc0000000u) != 0x80000000u)
                continue;
            FString Name = SurfaceName(Mesh->StaticMaterials[Index].MaterialSlotName.ToString());
            if (Name.IsEmpty())
                if (UMaterialInterface* Material = Mesh->StaticMaterials[Index].MaterialInterface)
                    Name = SurfaceName(Material->GetName());
            if (Name.IsEmpty())
                continue;
            FChoice& Choice = Surfaces.FindOrAdd(Tag & 0xffc00000u).FindOrAdd(Tag);
            Choice.Type.Name = Name;
            Choice.Type.Tag = Tag;
            ++Choice.Count;
        }
    }
    // Landscapes use the same captured ground tags; their collision material names the surface.
    for (TObjectIterator<ALandscapeProxy> It; It; ++It)
    {
        ALandscapeProxy* Landscape = *It;
        if (!IsValid(Landscape) || !P4Kadan::GameActorOf(Landscape))
            continue;
        FString Name;
        for (const auto& Field : P4GetStockEngineFields(Landscape))
            if (Field.Name == TEXT("C4CollisionMaterial") && Field.Material)
                Name = SurfaceName(Field.Material->GetName());
        if (Name.IsEmpty())
            Name = TEXT("Ground from ") + Landscape->GetActorLabel();
        TInlineComponentArray<ULandscapeHeightfieldCollisionComponent*> Parts(Landscape);
        for (const auto* Part : Parts)
            for (const auto& Field : P4GetStockEngineFields(Part))
                if (Field.Name == TEXT("C4TerrainTagContainer"))
                    for (uint32 Tag : Field.Tags)
                    {
                        if ((Tag & 0xc0000000u) != 0x80000000u)
                            continue;
                        FChoice& Choice = Surfaces.FindOrAdd(Tag & 0xffc00000u).FindOrAdd(Tag);
                        if (Choice.Type.Name.IsEmpty())
                            Choice.Type.Name = Name;
                        Choice.Type.Tag = Tag;
                        ++Choice.Count;
                    }
    }
    TArray<FP4GroundType> Result;
    for (const auto& Surface : Surfaces)
    {
        const FChoice* Best = nullptr;
        for (const auto& Variant : Surface.Value)
            if (!Best || Variant.Value.Count > Best->Count ||
                (Variant.Value.Count == Best->Count && Variant.Value.Type.Tag < Best->Type.Tag))
                Best = &Variant.Value;
        if (Best)
            Result.Add(Best->Type);
    }
    Result.Sort([](const FP4GroundType& A, const FP4GroundType& B) { return A.Name < B.Name; });
    return Result;
}

bool P4SetCreatorGroundType(AActor* Actor, const FP4GroundType& Type, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Actor) || !Actor->GetLevel() ||
        Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || P4IsGameplayActor(Actor))
    {
        Error = TEXT("Select a scenery actor in the editor.");
        return false;
    }
    if ((Type.Tag & 0xc0000000u) != 0x80000000u)
    {
        Error = TEXT("Choose a ground type from the game's collision.");
        return false;
    }
    if (P4Kadan::GameActorOf(Actor))
    {
        Error = TEXT("Use this ground type on scenery you added to the map.");
        return false;
    }
    if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Actor))
    {
        TInlineComponentArray<ULandscapeHeightfieldCollisionComponent*> Collision(Landscape);
        if (!Collision.Num())
        {
            Error = TEXT("Build the landscape's collision before choosing its ground type.");
            return false;
        }
        for (const auto* Part : Collision)
            if (!P4CanStoreStockEngineField(Part, TEXT("C4TerrainTagContainer")))
            {
                Error = TEXT("This landscape collision cannot hold the game's ground tags.");
                return false;
            }
        const FScopedTransaction Transaction(NSLOCTEXT("Kadan", "LandscapeGroundType", "Set landscape ground type"));
        Landscape->Modify();
        for (auto* Part : Collision)
        {
            FP4StockEngineField Field;
            Field.Name = TEXT("C4TerrainTagContainer");
            int32 Count = 1;
            for (const auto& Existing : P4GetStockEngineFields(Part))
                if (Existing.Name == Field.Name)
                    Count = FMath::Max(1, Existing.Tags.Num());
            Field.Tags.Init(Type.Tag, Count);
            if (!P4SetStockEngineField(Part, Field, Error))
                return false;
        }
        Landscape->MarkPackageDirty();
        Error.Reset();
        return true;
    }
    UClass* Class = LoadClass<UStaticMeshComponent>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".StaticCollisionComponent"));
    if (!Class || !FGroundFields(Class).IsValid())
    {
        Error = TEXT("The game's ground collision class is not available. Complete game setup first.");
        return false;
    }
    TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
    TArray<UStaticMeshComponent*> Native, Visual;
    for (UStaticMeshComponent* Part : Parts)
        if (Part && Part->GetStaticMesh())
        {
            if (Part->IsA(Class))
                Native.Add(Part);
            else if (Part->GetClass() == UStaticMeshComponent::StaticClass())
                Visual.Add(Part);
        }
    if (!Native.Num() && !Visual.Num())
    {
        Error = TEXT("The selected actor has no supported static mesh surface.");
        return false;
    }
    FCollisionResponseTemplate Profile;
    if (!UCollisionProfile::Get()->GetProfileTemplate(FName(TEXT("CP_Terrain")), Profile))
    {
        Error = TEXT("This project needs the game's terrain collision profile. Complete game setup first.");
        return false;
    }
    TArray<UStaticMesh*> TriangleSurfaces;
    for (UStaticMeshComponent* Part : Native.Num() ? Native : Visual)
    {
        UStaticMesh* Mesh = Part->GetStaticMesh();
        if (Part->IsA(Class) && Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
        {
            Error = TEXT("Enable collision on the existing terrain component before choosing its ground type.");
            return false;
        }
        if (Mesh->StaticMaterials.Num() < 1 || Mesh->StaticMaterials.Num() > 256)
        {
            Error = TEXT("Give the mesh material slots before choosing its ground type.");
            return false;
        }
        if (!P4HasIntrinsicGameSource(Mesh) && !P4IsCreatorAsset(Mesh))
        {
            Error = TEXT("Import the surface mesh from your own source file first.");
            return false;
        }
        const bool Shapes = Mesh->BodySetup && Mesh->BodySetup->AggGeom.GetElementCount() > 0;
        const bool Triangles = Mesh->ContainsPhysicsTriMeshData(false);
        const bool UsesTriangles = Mesh->BodySetup &&
            Mesh->BodySetup->GetCollisionTraceFlag() == CTF_UseComplexAsSimple;
        if ((UsesTriangles && !Triangles) || (!Shapes && !Triangles))
        {
            Error = TEXT(
                "This mesh has no usable collision surface. Enable collision on its mesh sections or import a collision mesh first.");
            return false;
        }
        if (!Shapes && !UsesTriangles)
        {
            if (!P4IsCreatorAsset(Mesh))
            {
                Error = TEXT(
                    "This game mesh has no simple collision. Use its game collision mesh or import your own surface mesh.");
                return false;
            }
            if (Part->IsSimulatingPhysics())
            {
                Error = TEXT("Turn off Simulate Physics before using this mesh's triangles as terrain collision.");
                return false;
            }
            // The collision change is the mesh's, so it reaches every object using the mesh. A simulating
            // body cannot use triangle collision.
            for (TObjectIterator<UStaticMeshComponent> Other; Other; ++Other)
            {
                if (*Other == Part || !IsValid(*Other) || Other->GetStaticMesh() != Mesh ||
                    !Other->BodyInstance.bSimulatePhysics)
                {
                    continue;
                }
                const AActor* Owner = Other->GetOwner();
                Error = (Owner ? Owner->GetName() : Other->GetName()) +
                    TEXT(
                        " simulates physics with this mesh. Its triangles cannot be terrain collision while another object simulates with it. Use a copy of the mesh for the terrain.");
                return false;
            }
            TriangleSurfaces.AddUnique(Mesh);
        }
    }
    const FScopedTransaction Transaction(NSLOCTEXT("Kadan", "GroundType", "Set game ground type"));
    Actor->Modify();
    for (UStaticMesh* Mesh : TriangleSurfaces)
    {
        // An ordinary file import can have BodySetup but no shapes. Use its
        // actual triangles for static terrain rather than reporting empty collision.
        Mesh->Modify();
        Mesh->CreateBodySetup();
        Mesh->BodySetup->Modify();
        Mesh->BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
        Mesh->BodySetup->InvalidatePhysicsData();
        Mesh->BodySetup->CreatePhysicsMeshes();
        Mesh->CreateNavCollision(true);
        Mesh->MarkPackageDirty();
    }
    if (!Native.Num())
        for (UStaticMeshComponent* Part : Visual)
        {
            const FName Name = MakeUniqueObjectName(Actor, Class, TEXT("GameGroundCollision"));
            auto* Collision = NewObject<UStaticMeshComponent>(Actor, Class, Name, RF_Transactional);
            if (!Collision)
            {
                Error = TEXT("The editor could not create the game's collision component.");
                return false;
            }
            Actor->AddInstanceComponent(Collision);
            Collision->SetMobility(Part->Mobility);
            Collision->SetupAttachment(Part);
            Collision->SetRelativeTransform(FTransform::Identity);
            Collision->SetStaticMesh(Part->GetStaticMesh());
            Collision->SetCollisionProfileName(FName(TEXT("CP_Terrain")));
            // As the old kit's terrain_body_defaults does, materialize the terrain object's
            // WorldStatic type rather than inheriting a different native component default.
            // SetObjectType invalidates the profile even when the type is unchanged.
            if (Collision->GetCollisionObjectType() != ECC_WorldStatic)
                Collision->SetCollisionObjectType(ECC_WorldStatic);
            Collision->SetCanEverAffectNavigation(true);
            Collision->SetVisibility(false);
            Collision->SetHiddenInGame(true);
            Collision->CastShadow = false;
            Part->Modify();
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Native.Add(Collision);
        }
    for (UStaticMeshComponent* Part : Native)
    {
        Part->Modify();
        if (!SetTags(Part, Type.Tag))
        {
            Error = TEXT("The game's ground tags could not be written.");
            return false;
        }
        const bool RefreshPhysics = Part->IsRegistered() && TriangleSurfaces.Contains(Part->GetStaticMesh());
        Part->RegisterComponent();
        if (RefreshPhysics)
            Part->RecreatePhysicsState();
        Part->MarkRenderStateDirty();
    }
    Actor->MarkPackageDirty();
    Error.Reset();
    return true;
}

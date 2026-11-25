// Fillers of placement row blobs (P4PlacementFillers.h): splines, boxes and the rest. Each kind reads what the game's
// serializer reads, from the same component, found the same way; the addresses are of the game's 1.1.1 build.
#include "P4PlacementFillers.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"

namespace P4PlacementDetail
{
namespace
{
void SetVec(P::Value& Out, const FVector& V)
{
    for (int32 I = 0; I < 3; ++I)
        Out.Words[I] = P::Value::FloatBits(V[I]);
}

// The members of a struct value; false with Error when one is not in the layout's type.
bool Members(P::Value& Out, std::initializer_list<const char*> Names, TArray<P::Value*>& Found, FString& Error)
{
    for (const char* Name : Names)
    {
        P::Value* One = Out.Find(Name);
        if (!One)
        {
            Error = FString::Printf(TEXT("the layout's type has no %s"), *Wide(Name));
            return false;
        }
        Found.Add(One);
    }
    return true;
}

UActorComponent* FirstFor(AActor* Actor, const P::Field& Field, FString& Error)
{
    const TArray<UActorComponent*> Parts = ComponentsFor(Actor, Field);
    if (!Parts.Num())
    {
        const FString Class = Field.FillClass.empty() ? FString(TEXT("component")) : Wide(Field.FillClass);
        Error = FString::Printf(TEXT("the actor has no %s%s%s"), *Class, Field.FillTag.empty() ? TEXT("") : TEXT(" tagged "),
            *Wide(Field.FillTag));
        return nullptr;
    }
    return Parts[0];
}

// The points of the actor's first spline component (FindComponentByClass), as the game's spline helper 0x00da2a10
// writes them (the point streamed by 0x00da2f20): per point its index as the input key, then its location, arrive and
// leave tangents and rotation in the spline's own space, its scale and its type. Used by MoveFloorAIComponent
// (0x0146f810), PushGimmickAIComponent (0x01845170), NarrowSpaceTriggerComponent (0x01af7380) and
// RouteSplineComponent (0x01ae7580). An actor without one: the game writes nothing.
bool SplinePoints(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    USplineComponent* Spline = Cast<USplineComponent>(FirstFor(Actor, Field, Error));
    if (!Spline)
    {
        if (Error.IsEmpty())
            Error = TEXT("the component is not a spline");
        return false;
    }
    if (!Field.Of->Item)
    {
        Error = TEXT("not an array");
        return false;
    }
    Out.Items.clear();
    const int32 Count = Spline->GetNumberOfSplinePoints();
    for (int32 Index = 0; Index < Count; ++Index)
    {
        P::Value Point = P::EmptyOf(*Field.Of->Item, P::RowVersion);
        TArray<P::Value*> M;
        if (!Members(Point, {"InputKey", "Position", "ArriveTangent", "LeaveTangent", "Rotation", "Scale", "Type"}, M, Error))
            return false;
        const ESplineCoordinateSpace::Type Local = ESplineCoordinateSpace::Local;
        M[0]->Bits = P::Value::FloatBits(float(Index));
        SetVec(*M[1], Spline->GetLocationAtSplinePoint(Index, Local));
        SetVec(*M[2], Spline->GetArriveTangentAtSplinePoint(Index, Local));
        SetVec(*M[3], Spline->GetLeaveTangentAtSplinePoint(Index, Local));
        const FRotator Rotation = Spline->GetRotationAtSplinePoint(Index, Local);
        M[4]->Words[0] = P::Value::FloatBits(Rotation.Pitch);
        M[4]->Words[1] = P::Value::FloatBits(Rotation.Yaw);
        M[4]->Words[2] = P::Value::FloatBits(Rotation.Roll);
        SetVec(*M[5], Spline->GetScaleAtSplinePoint(Index));
        M[6]->Bits = uint8(Spline->GetSplinePointType(Index));
        Out.Items.push_back(std::move(Point));
    }
    return true;
}

UBoxComponent* TaggedBox(AActor* Actor, const P::Field& Field, FString& Error)
{
    UBoxComponent* Box = Cast<UBoxComponent>(FirstFor(Actor, Field, Error));
    if (!Box && Error.IsEmpty())
        Error = TEXT("the component is not a box");
    return Box;
}

// The unscaled extent of the actor's first box component with the tag (PortalTriggerComponent 0x01afd620: the box
// tagged Trigger). An actor without one: the game writes nothing.
bool TaggedBoxExtent(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    UBoxComponent* Box = TaggedBox(Actor, Field, Error);
    if (!Box)
        return false;
    SetVec(Out, Box->GetUnscaledBoxExtent());
    return true;
}

// The world location of that box (its component to world translation, 0x01afd620).
bool TaggedBoxLocation(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    UBoxComponent* Box = TaggedBox(Actor, Field, Error);
    if (!Box)
        return false;
    SetVec(Out, Box->GetComponentLocation());
    return true;
}

// NavMeshTriggerComponent (0x01af8450): of the actor's box components, one tagged TriggerBox is the trigger box, else
// one tagged NavBox is the nav box; the last of each wins. The unscaled extent of the one of the field's tag, or
// (100, 100, 100) when the actor has none.
bool NavTriggerBoxExtent(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    if (!Actor)
    {
        Error = TEXT("no actor");
        return false;
    }
    const FName Trigger(TEXT("TriggerBox")), Nav(TEXT("NavBox")), Wanted(*Wide(Field.FillTag));
    UBoxComponent* Found = nullptr;
    TInlineComponentArray<UBoxComponent*> Boxes(Actor);
    for (UBoxComponent* Box : Boxes)
    {
        if (!Box)
            continue;
        // A box tagged TriggerBox is the trigger box, whatever else it is tagged.
        const bool IsTrigger = Box->ComponentHasTag(Trigger);
        if ((Wanted == Trigger && IsTrigger) || (Wanted == Nav && !IsTrigger && Box->ComponentHasTag(Nav)))
            Found = Box;
    }
    SetVec(Out, Found ? Found->GetUnscaledBoxExtent() : FVector(100.f));
    return true;
}

// TriggerDoorAIComponent (0x014a68b0): the actor's first box component tagged SearchBox, as its relative transform
// (the rotation from its relative rotation, as GetRelativeTransform makes it, its relative location and scale) and its
// scaled extent. An actor without one: the game writes nothing.
bool RelativeBox(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    UBoxComponent* Box = TaggedBox(Actor, Field, Error);
    if (!Box)
        return false;
    TArray<P::Value*> M;
    if (!Members(Out, {"Rotation", "Translation", "Scale3D", "BoxExtent"}, M, Error))
        return false;
    const FTransform Relative = Box->GetRelativeTransform();
    const FQuat Q = Relative.GetRotation();
    const float Quat[] = {Q.X, Q.Y, Q.Z, Q.W};
    for (int32 I = 0; I < 4; ++I)
        M[0]->Words[I] = P::Value::FloatBits(Quat[I]);
    SetVec(*M[1], Relative.GetTranslation());
    SetVec(*M[2], Relative.GetScale3D());
    SetVec(*M[3], Box->GetScaledBoxExtent());
    return true;
}

// CakTriggerComponent (0x018e87f0): the trigger's shape, from the actor's first component tagged Trigger when it is a
// shape, else its first shape component: 1 a box (its unscaled extent), 2 a capsule (unscaled half height and radius),
// 3 a sphere (unscaled radius), 4 another shape, 0 none. The extents of the other shapes keep the component's start
// values (the field's runtime default). The field says which value by the end of its name.
bool TriggerShape(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    if (!Actor)
    {
        Error = TEXT("no actor");
        return false;
    }
    UShapeComponent* Shape = nullptr;
    TInlineComponentArray<UActorComponent*> Parts(Actor);
    const FName Tag(*Wide(Field.FillTag));
    for (UActorComponent* Part : Parts)
        if (Part && Part->ComponentHasTag(Tag))
        {
            Shape = Cast<UShapeComponent>(Part);
            break;
        }
    if (!Shape)
        Shape = Actor->FindComponentByClass<UShapeComponent>();
    const UBoxComponent* Box = Cast<UBoxComponent>(Shape);
    const UCapsuleComponent* Capsule = Box ? nullptr : Cast<UCapsuleComponent>(Shape);
    const USphereComponent* Sphere = Box || Capsule ? nullptr : Cast<USphereComponent>(Shape);
    const FString Name = Wide(Field.Name);
    const auto Ends = [&](const TCHAR* End) { return Name.EndsWith(End, ESearchCase::CaseSensitive); };
    if (Ends(TEXT("_shape")))
    {
        Out.Bits = !Shape ? 0 : Box ? 1 : Capsule ? 2 : Sphere ? 3 : 4;
        return true;
    }
    float Value = 0.f;
    bool Taken = false;
    if (Box && (Ends(TEXT("_box_extent_x")) || Ends(TEXT("_box_extent_y")) || Ends(TEXT("_box_extent_z"))))
    {
        Value = Box->GetUnscaledBoxExtent()[Ends(TEXT("_x")) ? 0 : Ends(TEXT("_y")) ? 1 : 2];
        Taken = true;
    }
    else if (Capsule && (Ends(TEXT("_capsule_half_height")) || Ends(TEXT("_capsule_radius"))))
    {
        Value = Ends(TEXT("_capsule_radius")) ? Capsule->GetUnscaledCapsuleRadius() : Capsule->GetUnscaledCapsuleHalfHeight();
        Taken = true;
    }
    else if (Sphere && Ends(TEXT("_sphere_radius")))
    {
        Value = Sphere->GetUnscaledSphereRadius();
        Taken = true;
    }
    else if (!Ends(TEXT("_box_extent_x")) && !Ends(TEXT("_box_extent_y")) && !Ends(TEXT("_box_extent_z")) &&
        !Ends(TEXT("_capsule_half_height")) && !Ends(TEXT("_capsule_radius")) && !Ends(TEXT("_sphere_radius")))
    {
        Error = TEXT("not a value of the trigger's shape");
        return false;
    }
    Out.Bits = Taken ? P::Value::FloatBits(Value) : Field.RuntimeDefault;
    return true;
}

// The property on the actor's first component of this class. RouteSplineActor (0x00d0ba30) takes AreaList
// and JumpKeyList from its RouteSplineComponent (0x01ae7580).
bool ComponentProperty(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    UActorComponent* Component = FirstFor(Actor, Field, Error);
    if (!Component)
        return false;
    FString First, Rest;
    if (!Wide(Field.Name).Split(TEXT("."), &First, &Rest))
    {
        Error = TEXT("the field's name is not a path");
        return false;
    }
    FProperty* Leaf = nullptr;
    void* Data = nullptr;
    return Resolve(Component, Rest, Leaf, Data, Error) && ToValue(Leaf, Data, *Field.Of, Out, Error);
}

// A fresh placement starts each crystal at its authored maximum. The blob calls this value Life;
// the editor component's Life is runtime state and can still hold the class's initial value.
bool CrystalLives(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    UActorComponent* Component = FirstFor(Actor, Field, Error);
    if (!Component)
        return false;
    const FArrayProperty* Array = FindFProperty<FArrayProperty>(Component->GetClass(), TEXT("CrystalParameters"));
    const FStructProperty* Crystal = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    const FFloatProperty* MaxLife = Crystal ? FindFProperty<FFloatProperty>(Crystal->Struct, TEXT("MaxLife")) : nullptr;
    if (!MaxLife || !Field.Of->Item)
    {
        Error = TEXT("the crystal component has no authored crystal life array");
        return false;
    }

    FScriptArrayHelper Crystals(Array, Array->ContainerPtrToValuePtr<void>(Component));
    Out.Items.clear();
    for (int32 Index = 0; Index < Crystals.Num(); ++Index)
    {
        P::Value Parameter = P::EmptyOf(*Field.Of->Item, P::RowVersion);
        P::Value* Life = Parameter.Find("Life");
        if (!Life)
        {
            Error = TEXT("the crystal layout has no Life field");
            return false;
        }
        Life->Bits = P::Value::FloatBits(MaxLife->GetPropertyValue_InContainer(Crystals.GetRawPtr(Index)));
        Out.Items.push_back(MoveTemp(Parameter));
    }
    return true;
}

// ExcavationActor (0xd05090): its height map's drawer's TerrainMaterial, by its path name. The drawer is the actor's
// first component of the class. An actor without one: the game writes the actor's own copy, which Kadan does not know.
bool TerrainMaterial(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    UActorComponent* Drawer = FirstFor(Actor, Field, Error);
    if (!Drawer)
        return false;
    FProperty* Leaf = nullptr;
    void* Data = nullptr;
    if (!Resolve(Drawer, TEXT("TerrainMaterial"), Leaf, Data, Error) || !ToValue(Leaf, Data, *Field.Of, Out, Error))
        return false;

    const std::string Material = P::NameUtf8(Out);
    if (Material.empty() || Material == "None")
    {
        const FString Package = Actor->GetClass()->GetOutermost()->GetName();
        const FString Prefix(TEXT("/Game/Carrot4/Placeables/Gimmicks/Excavation/"));
        // All 12 verified rows of these four stock Blueprint classes hold the same editor-only name.
        // The material is absent from the shipped packages, so the editor's reconstructed drawer has no object for it.
        if (Package == Prefix + TEXT("GExcavationM") || Package == Prefix + TEXT("GExcavationOnyon") ||
            Package == Prefix + TEXT("GExcavationS") || Package == Prefix + TEXT("GExcavationUnder"))
        {
            Out = P::NameFromUtf8("/Game/Test/MasterMaterial/Collision/stg/Move_c/MI_Tag_G_Soil_c.MI_Tag_G_Soil_c");
        }
    }
    return true;
}

const FFillerKind Kinds[] = {
    {TEXT("spline_points"), &SplinePoints},
    {TEXT("tagged_box_extent"), &TaggedBoxExtent},
    {TEXT("tagged_box_location"), &TaggedBoxLocation},
    {TEXT("nav_trigger_box_extent"), &NavTriggerBoxExtent},
    {TEXT("relative_box"), &RelativeBox},
    {TEXT("trigger_shape"), &TriggerShape},
    {TEXT("component_property"), &ComponentProperty},
    {TEXT("crystal_lives"), &CrystalLives},
    {TEXT("terrain_material"), &TerrainMaterial},
};
} // namespace

TArrayView<const FFillerKind> ShapeFillers()
{
    return MakeArrayView(Kinds);
}
} // namespace P4PlacementDetail

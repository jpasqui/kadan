#pragma once
#include "CoreMinimal.h"
class AActor;

enum class EP4ActorCollisionError : uint8
{
    None, EditorThreadRequired, ActorRequired, FileRequired, AbsoluteFileRequired,
    GeometryUnreadable, NoCollision, FileWriteFailed
};
struct FP4ActorNavigationGeometry
{
    FString Actor, Component;
    // The editor navigation cache's exact Recast coordinates and triangle order.
    // Keeping their coordinate system explicit avoids an OBJ mirror in native callers.
    TArray<FVector> RecastVertices;
    TArray<int32> RecastTriangles;
};
struct FP4ActorCollisionResult
{
    EP4ActorCollisionError Error = EP4ActorCollisionError::None;
    FString Component, File;
    bool GeometryRead = false;
    int32 Components = 0, Vertices = 0, Triangles = 0;
    TArray<FP4ActorNavigationGeometry> Geometry;
    bool IsValid() const { return Error == EP4ActorCollisionError::None; }
};
FP4ActorCollisionResult P4ExportActorNavigationCollision(AActor* Actor, const FString& File);
FString P4ActorCollisionJson(const FP4ActorCollisionResult& Result);

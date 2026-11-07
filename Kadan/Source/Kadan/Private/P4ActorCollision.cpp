#include "P4ActorCollision.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavMesh/RecastNavMeshGenerator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FP4ActorCollisionResult P4ExportActorNavigationCollision(AActor* Actor, const FString& File)
{
    FP4ActorCollisionResult Result;
    const auto Fail = [&](EP4ActorCollisionError Code, const FString& Component = FString())
    {
        Result.Error = Code;
        Result.Component = Component;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(EP4ActorCollisionError::EditorThreadRequired);
    if (!IsValid(Actor))
        return Fail(EP4ActorCollisionError::ActorRequired);
    if (File.IsEmpty())
        return Fail(EP4ActorCollisionError::FileRequired);
    if (FPaths::IsRelative(File))
        return Fail(EP4ActorCollisionError::AbsoluteFileRequired);
    TArray<FString> Lines;
    Lines.Add(TEXT("# P4_COORDINATES UE4_OBJ_IMPORT, centimeters, Z up, import at origin with rotation 0 and scale 1"));
    int32 Base = 1;
    TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
    for (UPrimitiveComponent* Part : Parts)
    {
        if (!Part->IsNavigationRelevant())
            continue;
        FNavigationRelevantData Data(*Part);
        FRecastNavMeshGenerator::ExportComponentGeometry(Part, Data);
        const int64 Size = Data.CollisionData.Num();
        if (Size == 0)
            continue;
        const int64 Start = sizeof(FRecastGeometryCache);
        FRecastGeometryCache::FHeader Header;
        if (Size < Start)
            return Fail(EP4ActorCollisionError::GeometryUnreadable, Part->GetName());
        FMemory::Memcpy(&Header, Data.CollisionData.GetData(), sizeof(Header));
        const int64 Count = Header.NumVerts, Faces = Header.NumFaces;
        if (Count < 0 || Faces < 0 ||
            Start + Count * 3 * int64(sizeof(float)) + Faces * 3 * int64(sizeof(int32)) != Size)
            return Fail(EP4ActorCollisionError::GeometryUnreadable, Part->GetName());
        const uint8* At = Data.CollisionData.GetData() + Start;
        TArray<float> Points;
        Points.SetNumUninitialized(int32(Count * 3));
        FMemory::Memcpy(Points.GetData(), At, Count * 3 * sizeof(float));
        TArray<int32> Corners;
        Corners.SetNumUninitialized(int32(Faces * 3));
        FMemory::Memcpy(Corners.GetData(), At + Count * 3 * sizeof(float), Faces * 3 * sizeof(int32));
        for (const int32 Corner : Corners)
            if (Corner < 0 || Corner >= Count)
                return Fail(EP4ActorCollisionError::GeometryUnreadable, Part->GetName());
        FP4ActorNavigationGeometry Geometry;
        Geometry.Actor = Actor->GetName();
        Geometry.Component = Part->GetName();
        Geometry.RecastTriangles = Corners;
        for (int32 Point = 0; Point < Count; ++Point)
            Geometry.RecastVertices.Add(FVector(Points[Point * 3], Points[Point * 3 + 1], Points[Point * 3 + 2]));
        Result.Geometry.Add(MoveTemp(Geometry));
        Lines.Add(TEXT("o ") + Actor->GetName() + TEXT("_") + Part->GetName());
        // Recast (-x,-z,y) becomes the existing OBJ contract (-x,z,y),
        // which Unreal's OBJ import mirrors back into Unreal coordinates.
        for (int32 Point = 0; Point < Count; ++Point)
            Lines.Add(FString::Printf(
                TEXT("v %.9g %.9g %.9g"), -Points[Point * 3], Points[Point * 3 + 2], Points[Point * 3 + 1]));
        for (int32 Face = 0; Face < Faces; ++Face)
            Lines.Add(FString::Printf(TEXT("f %d %d %d"), Base + Corners[Face * 3], Base + Corners[Face * 3 + 1],
                Base + Corners[Face * 3 + 2]));
        Base += int32(Count);
        Result.Vertices += int32(Count);
        Result.Triangles += int32(Faces);
        ++Result.Components;
    }
    Result.GeometryRead = true;
    Result.File = File;
    if (!Result.Triangles)
        return Fail(EP4ActorCollisionError::NoCollision);
    if (!FFileHelper::SaveStringArrayToFile(Lines, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return Fail(EP4ActorCollisionError::FileWriteFailed);
    return Result;
}

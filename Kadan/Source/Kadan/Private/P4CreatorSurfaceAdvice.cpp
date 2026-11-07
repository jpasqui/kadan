#include "P4CreatorSurfaceAdvice.h"
#include "P4CreatorSource.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "MaterialShared.h"

FString P4MeshCollisionExplanation(const UStaticMesh* Mesh)
{
    if (!IsInGameThread() || !IsValid(Mesh) || !Mesh->BodySetup)
        return FString();
    if (Mesh->BodySetup->GetCollisionTraceFlag() != CTF_UseComplexAsSimple)
        return FString();
    return Mesh->ComplexCollisionMesh
        ? TEXT("This mesh uses the triangles of its separate Complex Collision Mesh for collision.")
        : TEXT(
              "This mesh uses its own triangles for collision. Complex Collision Mesh can stay empty and Customized Collision can stay off.");
}

TArray<FP4CreatorSurfaceAdvice> P4InspectCreatorSurface(UStaticMesh* Mesh)
{
    TArray<FP4CreatorSurfaceAdvice> Out;
    if (!IsInGameThread() || !IsValid(Mesh) || !P4IsCreatorAsset(Mesh) || Mesh->GetNumSourceModels() < 1)
        return Out;
    const FMeshDescription* Description = Mesh->GetMeshDescription(0);
    if (!Description)
        return Out;
    const FStaticMeshConstAttributes Attributes(*Description);
    const auto Positions = Attributes.GetVertexPositions();
    const auto Normals = Attributes.GetVertexInstanceNormals();
    const auto Tangents = Attributes.GetVertexInstanceTangents();
    const auto Colors = Attributes.GetVertexInstanceColors();
    const auto UVs = Attributes.GetVertexInstanceUVs();
    for (int32 Slot = 0; Slot < Mesh->StaticMaterials.Num(); ++Slot)
    {
        UMaterialInterface* Interface = Mesh->StaticMaterials[Slot].MaterialInterface;
        UMaterial* Material = Interface ? Interface->GetMaterial() : nullptr;
        if (!Material)
            continue;
        FStaticParameterSet Parameters;
        if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Interface))
            Instance->GetStaticParameterValues(Parameters);
        TArray<UMaterialExpression*> Expressions;
        if (!Material->GetAllReferencedExpressions(Expressions, &Parameters))
            continue;
        TSet<int32> Channels;
        bool VertexColors = false, VertexNormals = false;
        for (UMaterialExpression* Expression : Expressions)
        {
            if (const auto* UV = Cast<UMaterialExpressionTextureCoordinate>(Expression))
                Channels.Add(UV->CoordinateIndex);
            if (const auto* Sample = Cast<UMaterialExpressionTextureSample>(Expression))
                if (!Sample->Coordinates.IsConnected())
                    Channels.Add(Sample->ConstCoordinate);
            VertexColors |= Expression && Expression->IsA<UMaterialExpressionVertexColor>();
            VertexNormals |= Expression && Expression->IsA<UMaterialExpressionVertexNormalWS>();
        }
        const bool CaveWall = VertexColors && VertexNormals && P4HasIntrinsicGameSource(Material) &&
            Material->GetOutermost()->GetName().Equals(
                TEXT("/Game/Carrot4/MasterMaterial/stg/M_stg_Cave_Wall_AxisXY_Layer_Opaque_00_Master"),
                ESearchCase::IgnoreCase);
        const auto Tell = [&](const TCHAR* Code, const FString& Message)
        {
            FP4CreatorSurfaceAdvice Row;
            Row.Code = Code;
            Row.Message = TEXT("Material ") + Interface->GetName() + TEXT(" is assigned to this mesh. ") + Message;
            Row.MaterialSlot = Slot;
            Out.Add(MoveTemp(Row));
        };
        TArray<FVertexInstanceID> Corners;
        for (const FPolygonID Polygon : Description->Polygons().GetElementIDs())
        {
            const int32 Group = Description->GetPolygonPolygonGroup(Polygon).GetValue();
            const FMeshSectionInfo Info = Mesh->GetSectionInfoMap().Get(0, Group);
            if (Info.MaterialIndex != Slot)
                continue;
            for (const FVertexInstanceID Corner : Description->GetPolygonVertexInstances(Polygon))
                Corners.Add(Corner);
        }
        if (!Corners.Num())
            continue;
        float MinRed = 1.f, MinAlpha = 1.f;
        bool BadFrames = false;
        for (const FVertexInstanceID Corner : Corners)
        {
            const FVector N = Normals[Corner], T = Tangents[Corner];
            BadFrames |= N.ContainsNaN() || T.ContainsNaN() || N.IsNearlyZero() || T.IsNearlyZero() ||
                FVector::CrossProduct(N, T).IsNearlyZero();
            MinRed = FMath::Min(MinRed, Colors[Corner].X);
            MinAlpha = FMath::Min(MinAlpha, Colors[Corner].W);
        }
        bool OpposedNormals = false;
        TSet<int32> Flat, Stretched;
        for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
        {
            const FPolygonID Polygon = Description->GetTrianglePolygon(Triangle);
            if (Mesh->GetSectionInfoMap().Get(0, Description->GetPolygonPolygonGroup(Polygon).GetValue()).MaterialIndex !=
                Slot)
                continue;
            const auto Vertices = Description->GetTriangleVertexInstances(Triangle);
            for (int32 E = 0; E < 3; ++E)
                OpposedNormals |= FVector::DotProduct(Normals[Vertices[E]].GetSafeNormal(),
                                      Normals[Vertices[(E + 1) % 3]].GetSafeNormal()) < -0.25f;
            for (int32 Channel : Channels)
            {
                if (Channel >= UVs.GetNumIndices())
                    continue;
                FVector2D U[3];
                FVector P[3];
                for (int32 K = 0; K < 3; ++K)
                {
                    U[K] = UVs.Get(Vertices[K], Channel);
                    P[K] = Positions[Description->GetVertexInstanceVertex(Vertices[K])];
                }
                if (FVector::CrossProduct(P[1] - P[0], P[2] - P[0]).SizeSquared() < SMALL_NUMBER)
                    continue;
                if (FMath::Abs(FVector2D::CrossProduct(U[1] - U[0], U[2] - U[0])) < 1.e-10f)
                    Flat.Add(Channel);
                float Smallest = BIG_NUMBER, Largest = 0.f;
                for (int32 E = 0; E < 3; ++E)
                {
                    const float WorldLength = (P[E] - P[(E + 1) % 3]).Size(), UVLength = (U[E] - U[(E + 1) % 3]).Size();
                    if (WorldLength > KINDA_SMALL_NUMBER && UVLength > 1.e-10f)
                    {
                        const float Density = UVLength / WorldLength;
                        Smallest = FMath::Min(Smallest, Density);
                        Largest = FMath::Max(Largest, Density);
                    }
                }
                if (Smallest < BIG_NUMBER && Largest > Smallest * 32.f)
                    Stretched.Add(Channel);
            }
        }
        if (CaveWall)
        {
            Tell(TEXT("cave-wall-inputs"),
                TEXT(
                    "This cave wall material uses world position and vertex normals for projection. Vertex red chooses the wall or ground layer and vertex alpha darkens the color. Paint the red and alpha channels white on a plain wall. UV channel 1 is for baked lighting and does not repair these masks."));
            if (MinAlpha < 0.99f)
                Tell(TEXT("cave-wall-dark-alpha"),
                    TEXT(
                        "Vertex alpha is below white on this mesh. Zero alpha makes black patches. Paint alpha white where you want the full material color."));
            if (MinRed < 0.99f)
                Tell(TEXT("cave-wall-ground-projection"),
                    TEXT(
                        "Vertex red is below white on this mesh. With Use_UVs_Texture_02 off, this selects ground projection and can stretch up a wall. Paint red white on wall faces and keep lower values where you intend the ground layer."));
        }
        else if (VertexColors)
            Tell(TEXT("material-vertex-colors"),
                TEXT(
                    "This material reads painted vertex colors. Check its color masks when reusing it on your own mesh. A mesh without painted colors supplies white."));
        if (BadFrames || OpposedNormals)
            Tell(TEXT("mesh-normal-tangent-frames"),
                TEXT(
                    "This mesh has invalid tangent frames or opposing normals on a triangle. Recalculate its normals and tangents in your modeling tool and reimport it before judging the material."));
        TArray<int32> Ordered = Channels.Array();
        Ordered.Sort();
        for (int32 Channel : Ordered)
        {
            if (Channel >= UVs.GetNumIndices())
                Tell(TEXT("material-missing-uv"),
                    FString::Printf(
                        TEXT(
                            "This material reads UV channel %d, which the mesh lacks. Add that channel in your modeling tool and reimport the mesh."),
                        Channel));
            else if (Flat.Contains(Channel) || Stretched.Contains(Channel))
                Tell(TEXT("material-stretched-uv"),
                    FString::Printf(
                        TEXT(
                            "UV channel %d is collapsed or severely stretched on part of this mesh. Unwrap it at a consistent texture scale and reimport the mesh."),
                        Channel));
        }
    }
    return Out;
}

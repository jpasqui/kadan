#pragma once
// How the game lays out a gate's wall (VarGateBaseActor) from its length option, AddWallNum.
//
// The game builds the wall when the gate's AI component begins play (UVarGateAIComponent's BeginPlay, 0x0185eb30,
// in the 1.1.1 analysis program): the wall build (0x00d10640) adds the wall's pieces and places the far side, the
// pillars and the boxes, and the AI's own pass (0x0185f0a0) stretches the inside collision and moves the far side's
// collision. Both run only for a gate class with bAddWall. Their numbers, in the actor's space:
//
//  - The wall is one unit (the template mesh, Mesh_Inside_00_BR00) and AddWallNum more, at most 26. The added units
//    are pieces of 5, 2 and 1 units: as many of 5 as fit, then of 2, then of 1. A piece of bucket b (0 for 5, 1 for 2,
//    2 for 1), the i-th of its size, carries the tags Wall_b_i, Width_<size> and AddWall and the last mesh of
//    AddWallMesh05, 02 or 01. It hangs on the template at X = W * (units of the buckets before) + W * size * i
//    + W / 2 + W * size / 2, Y and Z 0; W is WallWidth.
//  - For a bucket the gate has a ground mesh of (GroundMeshList), each piece also gets a ground piece with the tags
//    Wall_b_i and AddGround and that mesh, on the gate's root at the template's place plus the same X.
//  - The far side (tags OutSideR, GroundOutSideR) goes to X = R / 2 + L / 2 + W * (AddWallNum + 1), keeping Y and Z;
//    L and R are OutSideLWidth and OutSideRWidth. The pillar tagged PillarL to X = -(L / 2 + P / 2), the one tagged
//    PillarR to X = P / 2 + R + L / 2 + W * (AddWallNum + 1); P is PillarWidth.
//  - The middle of the wall is at C = L / 2 + W * (AddWallNum + 1) / 2. The box tagged Nav_Box goes to (C, 0, 0)
//    with the extent (W / 2 * AddWallNum + 50, 15, 25); with bUseHigherNavBox 100 higher and 100 taller. The box
//    tagged LaterTaskTrigger goes to (C, 0, 0) with the extent (W / 2 * AddWallNum + 50, 35, 80).
//  - The inside collision (tag Col_Inside) is scaled to AddWallNum + 1 along X and goes to X = C plus the gate's
//    physics component's X, keeping Y and Z; the one tagged Col_Ground takes its scale and place. The far side's
//    collision (tag Col_OutsideR) takes the far side mesh's place.
//
// These match every gate of the game's cave levels (the pieces the game's editor saved, for AddWallNum 2 to 15).
// bUseHigherNavBox is used by none of them; its numbers come from the code alone (the up vector, taken as Z).
#include <string>
#include <vector>

namespace P4MapKit
{
namespace GateWall
{

constexpr int MostAddedUnits = 26;
constexpr int Sizes[3] = {5, 2, 1};

struct Options
{
    int AddWallNum = 0;
    float WallWidth = 0, OutSideLWidth = 0, OutSideRWidth = 0, PillarWidth = 0;
    bool UseHigherNavBox = false;
    // How many ground meshes the gate class has (GroundMeshList), one per bucket from the first.
    int GroundMeshes = 0;
};

struct Vec
{
    float X = 0, Y = 0, Z = 0;
};

struct Piece
{
    int Bucket = 0, Index = 0, Size = 0;
    // On the template for a wall piece; on the root, after the template's own place, for a ground piece.
    float X = 0;
    bool Ground = false;
    std::vector<std::string> Tags;
};

struct Layout
{
    int AddWallNum = 0;
    int Counts[3] = {0, 0, 0};
    std::vector<Piece> Pieces;
    float OutSideRX = 0, PillarLX = 0, PillarRX = 0;
    // The middle of the wall along X.
    float Middle = 0;
    Vec NavBoxLocation, NavBoxExtent, LaterTaskLocation, LaterTaskExtent;
    float InsideScaleX = 1;
};

inline Layout Lay(const Options& In)
{
    Layout Out;
    const int Added = In.AddWallNum < 0 ? 0 : In.AddWallNum > MostAddedUnits ? MostAddedUnits : In.AddWallNum;
    Out.AddWallNum = Added;
    const float W = In.WallWidth, L = In.OutSideLWidth, R = In.OutSideRWidth, P = In.PillarWidth;
    int Left = Added;
    for (int Bucket = 0; Bucket < 3; ++Bucket)
    {
        Out.Counts[Bucket] = Left / Sizes[Bucket];
        Left -= Out.Counts[Bucket] * Sizes[Bucket];
    }
    int Before = 0;
    for (int Bucket = 0; Bucket < 3; ++Bucket)
    {
        const int Size = Sizes[Bucket];
        for (int Index = 0; Index < Out.Counts[Bucket]; ++Index)
        {
            const float X = W * float(Before) + W * float(Size * Index) + W * 0.5f + W * float(Size) * 0.5f;
            const std::string Wall = "Wall_" + std::to_string(Bucket) + "_" + std::to_string(Index);
            Piece One;
            One.Bucket = Bucket;
            One.Index = Index;
            One.Size = Size;
            One.X = X;
            One.Tags = {Wall, "Width_" + std::to_string(Size), "AddWall"};
            Out.Pieces.push_back(One);
            if (Bucket < In.GroundMeshes)
            {
                One.Ground = true;
                One.Tags = {Wall, "AddGround"};
                Out.Pieces.push_back(One);
            }
        }
        Before += Size * Out.Counts[Bucket];
    }
    const float Units = float(Added + 1);
    Out.OutSideRX = R * 0.5f + L * 0.5f + W * Units;
    Out.PillarLX = -(L * 0.5f + P * 0.5f);
    Out.PillarRX = P * 0.5f + R + L * 0.5f + W * Units;
    Out.Middle = L * 0.5f + W * 0.5f * Units;
    const float Long = W * 0.5f * float(Added) + 50.f;
    Out.NavBoxLocation = {Out.Middle, 0, In.UseHigherNavBox ? 100.f : 0.f};
    Out.NavBoxExtent = {Long, 15.f, In.UseHigherNavBox ? 125.f : 25.f};
    Out.LaterTaskLocation = {Out.Middle, 0, 0};
    Out.LaterTaskExtent = {Long, 35.f, 80.f};
    Out.InsideScaleX = Units;
    return Out;
}

} // namespace GateWall
} // namespace P4MapKit

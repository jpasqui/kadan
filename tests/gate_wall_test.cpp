// Host test of core/gate_wall.h: the gate walls of the game's cave levels, as the game's editor saved them.
// Each case is a gate class's widths and a length option, with the places the saved pieces, far side and boxes have.
#include "core/gate_wall.h"
#include <cmath>
#include <iostream>

using namespace P4MapKit::GateWall;

static int Failures = 0;
#define CHECK(X)                                                         \
    do                                                                   \
    {                                                                    \
        if (!(X))                                                        \
        {                                                                \
            std::cerr << "Failed at " << __LINE__ << ": " << #X << '\n'; \
            ++Failures;                                                  \
        }                                                                \
    }                                                                    \
    while (false)

static bool Near(float A, float B)
{
    return std::fabs(A - B) < 0.01f;
}

struct Saved
{
    const char* Gate;
    float L, R, P;
    int Added;
    std::vector<std::pair<std::string, float>> Pieces; // Wall tag and X on the template
    float FarSide, Middle, Long;
};

int main()
{
    // Soft walls are 40 wide at each side, electric and ice ones 25; every unit is 25.
    const Saved Gates[] = {
        {"GVarGateSoftNoPillar", 40, 40, 0, 2, {{"Wall_1_0", 37.5f}}, 115, 57.5f, 75},
        {"GVarGateSoftNoPillar", 40, 40, 0, 3, {{"Wall_1_0", 37.5f}, {"Wall_2_0", 75}}, 140, 70, 87.5f},
        {"GVarGateSoftNoPillar", 40, 40, 0, 5, {{"Wall_0_0", 75}}, 190, 95, 112.5f},
        {"GVarGateSoftNoPillar", 40, 40, 0, 6, {{"Wall_0_0", 75}, {"Wall_2_0", 150}}, 215, 107.5f, 125},
        {"GVarGateSoftNoPillar", 40, 40, 0, 7, {{"Wall_0_0", 75}, {"Wall_1_0", 162.5f}}, 240, 120, 137.5f},
        {"GVarGateSoftNoPillar", 40, 40, 0, 8, {{"Wall_0_0", 75}, {"Wall_1_0", 162.5f}, {"Wall_2_0", 200}}, 265, 132.5f,
            150},
        {"GVarGateDenkiNoPillar", 25, 25, 0, 4, {{"Wall_1_0", 37.5f}, {"Wall_1_1", 87.5f}}, 150, 75, 100},
        {"GVarGateIceNoPillar", 25, 25, 0, 7, {{"Wall_0_0", 75}, {"Wall_1_0", 162.5f}}, 225, 112.5f, 137.5f},
        {"GVarGateDenkiNoPillar", 25, 25, 0, 10, {{"Wall_0_0", 75}, {"Wall_0_1", 200}}, 300, 150, 175},
        {"GVarGateDenkiNoPillar", 25, 25, 0, 11, {{"Wall_0_0", 75}, {"Wall_0_1", 200}, {"Wall_2_0", 275}}, 325, 162.5f,
            187.5f},
        {"GVarGateDenkiNoPillar", 25, 25, 0, 15, {{"Wall_0_0", 75}, {"Wall_0_1", 200}, {"Wall_0_2", 325}}, 425, 212.5f,
            237.5f},
        {"GVarGateSoft", 40, 40, 20, 4, {{"Wall_1_0", 37.5f}, {"Wall_1_1", 87.5f}}, 165, 82.5f, 100},
    };
    for (const Saved& Gate : Gates)
    {
        Options In;
        In.AddWallNum = Gate.Added;
        In.WallWidth = 25;
        In.OutSideLWidth = Gate.L;
        In.OutSideRWidth = Gate.R;
        In.PillarWidth = Gate.P;
        const Layout Out = Lay(In);
        bool Same = Out.Pieces.size() == Gate.Pieces.size();
        for (std::size_t I = 0; Same && I < Out.Pieces.size(); ++I)
            Same = !Out.Pieces[I].Ground && Out.Pieces[I].Tags[0] == Gate.Pieces[I].first &&
                Near(Out.Pieces[I].X, Gate.Pieces[I].second) && Out.Pieces[I].Tags.back() == "AddWall" &&
                Out.Pieces[I].Tags[1] == "Width_" + std::to_string(Out.Pieces[I].Size);
        if (!Same)
            std::cerr << Gate.Gate << " " << Gate.Added << ": the pieces differ\n";
        CHECK(Same);
        CHECK(Near(Out.OutSideRX, Gate.FarSide));
        CHECK(Near(Out.Middle, Gate.Middle) && Near(Out.NavBoxLocation.X, Gate.Middle) &&
            Near(Out.LaterTaskLocation.X, Gate.Middle));
        CHECK(Near(Out.NavBoxExtent.X, Gate.Long) && Near(Out.NavBoxExtent.Y, 15) && Near(Out.NavBoxExtent.Z, 25));
        CHECK(Near(Out.LaterTaskExtent.X, Gate.Long) && Near(Out.LaterTaskExtent.Y, 35) &&
            Near(Out.LaterTaskExtent.Z, 80));
        CHECK(Near(Out.NavBoxLocation.Y, 0) && Near(Out.NavBoxLocation.Z, 0));
        CHECK(Near(Out.InsideScaleX, float(Gate.Added + 1)));
    }
    // The pillared soft gate's right pillar is at 195 in the saved level, which a pillar width of 20 gives.
    {
        Options In;
        In.AddWallNum = 4;
        In.WallWidth = 25;
        In.OutSideLWidth = In.OutSideRWidth = 40;
        In.PillarWidth = 20;
        const Layout Out = Lay(In);
        CHECK(Near(Out.PillarRX, 195) && Near(Out.PillarLX, -30));
    }
    // At most 26 more units, none below 0; ground pieces for the buckets the class has a ground mesh of.
    {
        Options In;
        In.WallWidth = 25;
        In.AddWallNum = 40;
        Layout Out = Lay(In);
        CHECK(Out.AddWallNum == 26 && Out.Counts[0] == 5 && Out.Counts[1] == 0 && Out.Counts[2] == 1);
        In.AddWallNum = -3;
        Out = Lay(In);
        CHECK(Out.AddWallNum == 0 && Out.Pieces.empty() && Near(Out.InsideScaleX, 1));
        In.AddWallNum = 8;
        In.GroundMeshes = 2;
        In.UseHigherNavBox = true;
        Out = Lay(In);
        int Ground = 0;
        for (const Piece& One : Out.Pieces)
            if (One.Ground)
            {
                ++Ground;
                CHECK(One.Bucket < 2 && One.Tags.size() == 2 && One.Tags[1] == "AddGround");
            }
        CHECK(Out.Pieces.size() == 5 && Ground == 2);
        CHECK(Near(Out.NavBoxLocation.Z, 100) && Near(Out.NavBoxExtent.Z, 125) && Near(Out.LaterTaskLocation.Z, 0));
    }
    if (Failures)
    {
        std::cout << Failures << " failures\n";
        return 1;
    }
    std::cout << "gate wall checks passed\n";
    return 0;
}

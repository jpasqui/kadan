#pragma once
#include "Commandlets/Commandlet.h"
#include "P4SwitchShadersCommandlet.generated.h"

// Compiles materials for the game's shader platform (SP_SWITCH, Kadan's GLSL_SWITCH format) through UE's own material
// shader pipeline and writes what UE made: each shader's type, vertex factory, permutation and frequency, its
// parameter bindings, and its payload (stage 8.5 of tools/navigation/OWN_SHADER_MAPS.md).
//
//   UE4Editor-Cmd <project> -run=P4SwitchShaders -Materials=/Game/A/M_A+/Game/B/M_B -Out=<folder>
//       [-Graph=<decompiled graph json> -GraphPackage=/Game/Test/M_Name]
//   UE4Editor-Cmd <project> -run=P4SwitchShaders -Graphs=<{name: {graph, usage}} json> -Out=<folder>
UCLASS()
class UP4SwitchShadersCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UP4SwitchShadersCommandlet();
    virtual int32 Main(const FString& Params) override;
};

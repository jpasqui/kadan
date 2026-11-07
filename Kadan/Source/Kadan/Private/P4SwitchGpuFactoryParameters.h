#pragma once
#include "CoreMinimal.h"

class FShader;
class FShaderMapPointerTable;
class FVertexFactoryShaderParameters;
struct FTypeLayoutDesc;

// Stock GPU sprite parameters have no binding for the game's per-particle parameter texture. Install a constructor
// that binds it for Switch jobs, and a memory-image field writer that preserves the stock path on other platforms.
// Fails before installation if this engine's stock prefix or the game's 56-byte frozen layout is not as expected.
bool P4InstallSwitchGpuFactoryParameters(FString& Error);
void P4UninstallSwitchGpuFactoryParameters();
const FTypeLayoutDesc* P4SwitchFactoryParameterLayout(const FShader& Shader, const FShaderMapPointerTable& Table);
bool P4ValidateSwitchGpuFactoryParameters(const FShader& Shader, const FShaderMapPointerTable& Table,
                                        const FVertexFactoryShaderParameters* Parameters, FString& Error);

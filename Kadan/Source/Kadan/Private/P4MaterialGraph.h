#pragma once
#include "CoreMinimal.h"

class UMaterial;

// A material's node graph as JSON, the form the kit's material tools read and write
// (tools/navigation/material_decompile.py, recomp/graphics/nvasm/material_graph_eval.py):
//   {"nodes": {"<id>": {"type": "Multiply", "inputs": {"A": ["<id>", "RGB"]}, "props": {"ConstB": 2}}},
//    "outputs": {"BaseColor": ["<id>", "RGB"]},
//    "format": 2, "domain": "MD_Surface", "blend_mode": "BLEND_Opaque", "two_sided": false, "shading_model": "MSM_DefaultLit",
//    "clip": 0.3333, "tangent_space_normal": true}
// A node's type is its expression class without "MaterialExpression". An input is named as the expression's
// own property is. A link is [node, channels] with channels "" for the node's whole value, and a third
// item, the output pin's index, for a pin that is not a channel of the first.

// Makes a material at Package from a graph. Every node becomes one expression. A link that takes channels
// goes to the expression's own pin for them when it has one, else through a ComponentMask. Refuses a
// node type, a property or an input the engine does not have, and an asset that is not in the project.
// Null with Error set when it could not be made. Nothing is saved.
KADAN_API UMaterial* P4BuildMaterialFromGraph(const FString& GraphJson, const FString& Package, FString& Error);

// The graph of a material as it is in the editor, in the same JSON. Properties are given where they
// differ from the expression's defaults. Empty with Error set when the material holds something the
// format cannot say.
KADAN_API FString P4MaterialGraphJson(UMaterial* Material, FString& Error);

// SHA-256 of graph readback plus persistent reflected material/expression
// values, with binary numbers and stable object/name references. This is a
// native editor comparison, not original cooked bytes. Never compiles/saves or
// updates provenance; an unreadable graph has no comparison state.
KADAN_API FString P4MaterialComparisonState(UMaterial* Material, FString& Error);

// Diagnostic ranges into the very same native comparison byte stream above.
// No alternate serializer, filtering, graph construction or baseline capture.
struct FP4MaterialNativeFieldRange
{
    FString Object, Property;
    int64 Begin=0, End=0;
};
struct FP4MaterialNativeStateFrame
{
    FString Graph;
    TArray<uint8> Bytes;
    TArray<FP4MaterialNativeFieldRange> Fields;
    bool Complete=false;
};
KADAN_API FString P4MaterialComparisonStateAndFrame(UMaterial* Material, FP4MaterialNativeStateFrame& Frame, FString& Error);

// Waits for the existing material's actual current-platform shader resource.
// Requires a real renderer; does not build another asset or save the material.
KADAN_API bool P4WaitForMaterialShaders(UMaterial* Material,TArray<FString>& CompileErrors,FString& Error);

// Builds, optionally saves, and reports as JSON: valid, error, asset, nodes, and the graph read back.
// A real renderer also reports renderer_active, compiled and every compile_errors entry. NullRHI
// builds never qualify as renderer compile evidence. Recovered game masters are read-only references.
// Optional source custody is checked before construction, before save and after its completed write.
// Without bWaitForShaders the material is saved while its shaders still compile, as the editor compiles
// any material, and the report says compile_deferred instead of compiled. When they finish, a graph that
// does not compile is told in the editor and the log (P4WatchMasterCompile).
KADAN_API FString P4BuildMaterialGraphReport(const FString& GraphJson, const FString& Package, bool SaveToDisk,
    const TFunction<bool(FString&)>& SourceCurrent = TFunction<bool(FString&)>(), bool bWaitForShaders = true);

// The assets outside a material's own package that it refers to: what a save of it has to follow.
void P4MaterialSaveReferences(UMaterial* Material, TArray<UObject*>& Out);

// Internal to the material graph: the codec and settings helpers its files and the candidate proof share
// (P4MaterialGraph.cpp, P4MaterialGraphRead.cpp, P4MaterialCandidateSourceProof.cpp).
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"

class FProperty;
class UMaterial;
class UObject;
class UScriptStruct;

namespace P4MaterialGraph
{
inline const TCHAR* const Pins[] = {TEXT("BaseColor"), TEXT("Metallic"), TEXT("Specular"), TEXT("Roughness"),
    TEXT("EmissiveColor"), TEXT("Opacity"), TEXT("OpacityMask"), TEXT("Normal"), TEXT("WorldPositionOffset"),
    TEXT("SubsurfaceColor"), TEXT("AmbientOcclusion"), TEXT("Refraction"), TEXT("PixelDepthOffset"), TEXT("ClearCoat"),
    TEXT("ClearCoatRoughness"), TEXT("Anisotropy"), TEXT("Tangent"), TEXT("CustomizedUV0"), TEXT("CustomizedUV1"),
    TEXT("CustomizedUV2"), TEXT("CustomizedUV3"), TEXT("CustomizedUV4"), TEXT("CustomizedUV5"), TEXT("CustomizedUV6"),
    TEXT("CustomizedUV7")};
inline const FString Prefix = TEXT("MaterialExpression");
// An expression's input is an ExpressionInput. A material's own pins are MaterialInput structs, which in
// the engine's code derive from it and in its reflection data stand on their own.
bool IsInput(const UScriptStruct* Struct);
// An expression's or a material's input of that name, which is a property of it. Null when it has none.
FExpressionInput* InputOf(UObject* Owner, const FString& Name);
bool Set(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& Error);
// False when the property is of a kind the format has no value for.
bool Get(const FProperty* Property, const void* At, TSharedPtr<FJsonValue>& Out);
// The highest texture coordinate the material reads from the mesh's vertices.
int32 HighestTexCoord(const TSharedPtr<FJsonObject>& Nodes, const TSharedPtr<FJsonObject>& Outputs);
// A game material setting stock Unreal 4.26 cannot compile, and the stock value the editor's material holds in
// its place. Null when the game's value is used as it is.
TSharedPtr<FJsonValue> GameRenderStandIn(
    const FString& Key, const TSharedPtr<FJsonValue>& GameValue, int32 HighestCoordinate);
// A render_settings key's property and the object that holds it.
FProperty* RenderSettingProperty(UMaterial* Material, const FString& Key, bool bAdd, UObject*& Owner);
// Sets one render_settings value on a material.
bool SetRenderSetting(UMaterial* Material, const FString& Key, const TSharedPtr<FJsonValue>& Value,
    int32 HighestCoordinate, TMap<FString, TSharedPtr<FJsonValue>>& GameValues, FString& Error);
// The game's values of the settings a material holds as stand-ins, by setting.
TSharedPtr<FJsonObject> GameRenderValues(const UMaterial* Material);
// Node ids in an order that does not depend on how they were written.
void SortIds(TArray<FString>& Ids);
// The graph's material from its JSON (P4MaterialGraph.cpp) and the JSON of a material's graph (P4MaterialGraphRead.cpp).
UMaterial* Build(const FString& GraphJson, const FString& Package, FString& Error);
FString Json(UMaterial* Material, FString& Error);
} // namespace P4MaterialGraph

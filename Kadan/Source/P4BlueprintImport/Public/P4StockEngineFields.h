#pragma once
#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "P4StockEngineFields.generated.h"

class UMaterialInterface;
class FJsonObject;

// Real editor storage for the game's additions to unmodified engine classes.
// The cook adapter writes these typed values under their original field names.
USTRUCT()
struct P4BLUEPRINTIMPORT_API FP4StockEngineField
{
    GENERATED_BODY()
    UPROPERTY() FName Name;
    UPROPERTY() int32 Integer = 0;
    UPROPERTY() FName Choice;
    UPROPERTY() TArray<uint32> Tags;
    UPROPERTY() UMaterialInterface* Material = nullptr;
};

UCLASS(NotBlueprintType, HideDropdown)
class P4BLUEPRINTIMPORT_API UP4StockEngineFields : public UAssetUserData
{
    GENERATED_BODY()
public:
    virtual bool IsEditorOnly() const override { return true; }
    UPROPERTY() TArray<FP4StockEngineField> Fields;
    // A landscape actor uses its existing root component as the data owner.
    UPROPERTY() TArray<FP4StockEngineField> ActorFields;
};

enum class EP4StockEngineField { NotApplicable, Stored, Refused };
P4BLUEPRINTIMPORT_API bool P4CanStoreStockEngineField(const UObject* Target, FName Name);
P4BLUEPRINTIMPORT_API bool P4SetStockEngineField(UObject* Target, const FP4StockEngineField& Field, FString& Error);
P4BLUEPRINTIMPORT_API TArray<FP4StockEngineField> P4GetStockEngineFields(const UObject* Target);
// Explicit import boundary only: native property copying can share a template's private data.
// Give each component its own record before comparison and the ordinary editor save.
P4BLUEPRINTIMPORT_API bool P4OwnStockEngineFields(UPackage* Package, FString& Error);
P4BLUEPRINTIMPORT_API EP4StockEngineField P4ApplyStockEngineFieldPlan(UObject* Target,
    const TSharedPtr<FJsonObject>& Record, FString& Error);

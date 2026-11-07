#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Factories/Factory.h"
#include "P4AddedContent.generated.h"

class AActor;
class UBlueprint;
class UTexture2D;
class FJsonObject;

UENUM()
enum class EP4AddedContentKind : uint8 { Treasure, Creature };

// An editor source declaration. Export writes its creator-set fields into the manifest and never
// cooks this asset. The child's stock parent and donor stay references supplied by the player's game.
UCLASS(BlueprintType, meta=(DisplayName="Treasure or creature"))
class KADAN_API UP4AddedContent : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Identity") EP4AddedContentKind Kind = EP4AddedContentKind::Treasure;
    UPROPERTY(EditAnywhere, Category="Identity", meta=(ToolTip="Give this kind a short lowercase name. Tsuchi assigns its identity from this name and the mod ID.")) FString ShortName;
    UPROPERTY(EditAnywhere, Category="Identity", meta=(ToolTip="Choose the new Blueprint child class you created in this mod.")) TSoftClassPtr<AActor> ActorClass;
    UPROPERTY(EditAnywhere, Category="Identity", meta=(ToolTip="Name the stock treasure or creature row that supplies values you leave unchanged.")) FName From;
    UPROPERTY(EditAnywhere, Category="Text") FString DisplayName;
    UPROPERTY(EditAnywhere, Category="Text", meta=(MultiLine="true")) FString Description;
    UPROPERTY(EditAnywhere, Category="Appearance") TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere, Category="Values", meta=(ToolTip="Leave this at minus one to use the donor value.")) int32 Sparklium = -1;
    UPROPERTY(EditAnywhere, Category="Values", meta=(ToolTip="Leave both carrying counts at minus one to use the donor values.")) int32 MinimumCarriers = -1;
    UPROPERTY(EditAnywhere, Category="Values") int32 MaximumCarriers = -1;
    UPROPERTY(EditAnywhere, Category="Values", meta=(ToolTip="Leave this at minus one to use the donor life.")) float Life = -1.f;
};

UCLASS(HideCategories=Object, CollapseCategories)
class KADAN_API UP4AddedContentFactory : public UFactory
{
    GENERATED_BODY()
public:
    UP4AddedContentFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual bool ShouldShowInNewMenu() const override { return true; }
    virtual FText GetDisplayName() const override;
    virtual uint32 GetMenuCategories() const override;
};

KADAN_API FString P4CreatorBlueprintIssue(const UBlueprint* Blueprint, const FString& PackageRoot);
KADAN_API bool P4AddedContentDeclaration(const UP4AddedContent* Definition, const FString& PackageRoot,
    TSharedPtr<FJsonObject>& Out, FString& Error);

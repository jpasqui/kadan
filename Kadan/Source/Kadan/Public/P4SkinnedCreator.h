#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "P4SkinnedCreator.generated.h"
class USkeletalMesh;
class USkeleton;
class UAnimSequence;
class UFbxImportUI;

// The same native FBX options used by the working Groink root-scale/facing rebake.
KADAN_API void P4ConfigureSkinnedFbx(UFbxImportUI* UI, USkeleton* Skeleton, float Scale, float Yaw, int32 SampleRate = 0);

UCLASS()
class KADAN_API UP4SkinnedCreatorTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Creates new editor assets through Unreal's existing FBX factories. Originals are only read.
    UFUNCTION(BlueprintCallable, Category="Kadan|Creator", meta=(DevelopmentOnly))
    static FString RebakeSkeletalAssets(USkeletalMesh* Mesh, const TArray<UAnimSequence*>& Clips,
        const FString& DestinationFolder, float Scale = 1.f, float Yaw = 0.f);
};

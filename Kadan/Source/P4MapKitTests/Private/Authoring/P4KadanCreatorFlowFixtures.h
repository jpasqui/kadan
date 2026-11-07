#pragma once
#if P4_MAPKIT_TESTS

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Framework/Application/SlateApplication.h"
#include "P4KadanPathPicker.h"
#include "P4KadanExportReceipt.h"
#include "P4CreatorMaterialFactories.h"
#include "P4MaterialGameSettings.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "IMaterialEditor.h"
#include "MaterialEditorModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SSplitter.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Brush.h"
#include "Components/ArrowComponent.h"
#include "HAL/PlatformApplicationMisc.h"

struct FP4KadanTooltipFixture
{
    bool Previous = false;
    FVector2D PreviousPosition;

    FP4KadanTooltipFixture();
    ~FP4KadanTooltipFixture();
};

struct FP4KadanUnavailablePickerFixture
{
    FP4KadanUnavailablePickerFixture();
    ~FP4KadanUnavailablePickerFixture();
};

struct FP4KadanReadOnlyTextureProbe
{
    TStrongObjectPtr<UTexture2D> Texture;
    TEnumAsByte<TextureAddress> Address;
    bool Dirty = false;

    explicit FP4KadanReadOnlyTextureProbe(UTexture2D* Value);
    ~FP4KadanReadOnlyTextureProbe();
};

bool P4KadanResizeDetailsSplit(const TSharedRef<SWidget>& Root,
    const TSharedRef<SWidget>& Target, EOrientation Orientation);

#if PLATFORM_WINDOWS
class FP4KadanReceiptPublicationLock
{
public:
    ~FP4KadanReceiptPublicationLock();
    bool Open(const FString& Directory);
    void Close();

private:
    void* Handle = nullptr;
};
#endif
#endif // P4_MAPKIT_TESTS

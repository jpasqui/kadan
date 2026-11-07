#include "P4CreatorMaterialFactories.h"
#include "P4GamePaths.h"
#include "P4KadanEditor.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
#include "P4OriginalImportSave.h"
#include "P4KadanReadOnly.h"
#include "AssetToolsModule.h"
#include "AssetTypeCategories.h"
#include "ContentBrowserModule.h"
#include "Engine/Texture.h"
#include "Factories/MaterialFactoryNew.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/CoreDelegates.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakFieldPtr.h"

// Internal to the two real constructors, not an API for relabelling arbitrary existing assets.
void P4RecordFactoryCreatedMaterial(UMaterial* Material);
void P4RecordFactoryCreatedInstance(UMaterialInstanceConstant* Instance);

namespace
{
bool Installed = false;
bool OriginalMaterialCreateNew = false, OriginalInstanceCreateNew = false;
bool EngineExiting = false;
TWeakObjectPtr<UMaterialFactoryNew> NativeMaterialDefault;
TWeakObjectPtr<UMaterialInstanceConstantFactoryNew> NativeInstanceDefault;
TWeakFieldPtr<FBoolProperty> NativeCreateNewProperty;
FDelegateHandle EnginePreExit;

void RestoreFactoriesBeforeEngineExit()
{
    EngineExiting = true;
    P4ShutdownCreatorMaterialFactories();
}

bool FreshTarget(UClass* Class, UClass* Expected, UObject* Parent, FName Name)
{
    if (!IsInGameThread() || P4ImportPublication::IsImporting() || Class != Expected || !IsValid(Parent) ||
        !Parent->IsA<UPackage>() || Name.IsNone())
        return false;
    const FString Package = Parent->GetName();
    if (!P4IsValidContentPackageIgnoringCase(Package) || FPackageName::DoesPackageExist(Package) ||
        FindObjectFast<UObject>(Parent, Name))
        return false;
    TArray<UObject*> ExistingObjects;
    GetObjectsWithOuter(Parent, ExistingObjects, false);
    for (UObject* Object : ExistingObjects)
        if (Object && Object->IsAsset())
            return false;
    return true;
}
} //namespace

UP4CreatorMaterialFactory::UP4CreatorMaterialFactory()
{
    SupportedClass = UMaterial::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
FText UP4CreatorMaterialFactory::GetDisplayName() const
{
    return FText::FromString(TEXT("Material"));
}
uint32 UP4CreatorMaterialFactory::GetMenuCategories() const
{
    return EAssetTypeCategories::Basic | EAssetTypeCategories::MaterialsAndTextures;
}
UObject* UP4CreatorMaterialFactory::FactoryCreateNew(
    UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    if (!FreshTarget(Class, UMaterial::StaticClass(), Parent, Name))
        return nullptr;
    // This is the actual empty graph constructor. An optional texture is a reference,
    // matching the native material factory's initial-texture behavior, not copied pixels.
    UMaterial* Material = NewObject<UMaterial>(Parent, Class, Name, Flags);
    // Start new creator graphs with the decal response supported by export.
    Material->MaterialDecalResponse = MDR_ColorRoughness;
    if (InitialTexture)
    {
        UMaterialExpressionTextureSample* Sample = NewObject<UMaterialExpressionTextureSample>(Material);
        Sample->MaterialExpressionEditorX = -250;
        Sample->Texture = InitialTexture;
        Sample->AutoSetSampleType();
        Material->Expressions.Add(Sample);
        FExpressionOutput& Output = Sample->GetOutputs()[0];
        FExpressionInput& Input = Sample->SamplerType == SAMPLERTYPE_Normal
            ? static_cast<FExpressionInput&>(Material->Normal)
            : static_cast<FExpressionInput&>(Material->BaseColor);
        Input.Expression = Sample;
        Input.Mask = Output.Mask;
        Input.MaskR = Output.MaskR;
        Input.MaskG = Output.MaskG;
        Input.MaskB = Output.MaskB;
        Input.MaskA = Output.MaskA;
        Material->PostEditChange();
    }
    P4RecordFactoryCreatedMaterial(Material);
    return Material;
}

UP4CreatorMaterialInstanceFactory::UP4CreatorMaterialInstanceFactory()
{
    SupportedClass = UMaterialInstanceConstant::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
FText UP4CreatorMaterialInstanceFactory::GetDisplayName() const
{
    return FText::FromString(TEXT("Material Instance"));
}
uint32 UP4CreatorMaterialInstanceFactory::GetMenuCategories() const
{
    return EAssetTypeCategories::MaterialsAndTextures;
}
UObject* UP4CreatorMaterialInstanceFactory::FactoryCreateNew(
    UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    if (!FreshTarget(Class, UMaterialInstanceConstant::StaticClass(), Parent, Name))
        return nullptr;
    UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(
        Super::FactoryCreateNew(Class, Parent, Name, Flags, Context, Warn));
    if (Instance && Instance->GetOuter() == Parent && Instance->GetFName() == Name)
        P4RecordFactoryCreatedInstance(Instance);
    return Instance;
}

void P4CreateCreatorMaterialInstances(const TArray<UObject*>& Parents)
{
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TArray<UMaterialInterface*> SupportedParents;
    for (UObject* Object : Parents)
    {
        UMaterialInterface* Parent = Cast<UMaterialInterface>(Object);
        if (!IsValid(Parent))
            continue;
        if (P4HasIntrinsicGameProxy(Parent) || P4HasCopiedGameData(Parent) ||
            FString(Parent->GetOutermost()->GetMetaData()->GetValue(Parent, TEXT("P4BlueprintStandIn"))) == TEXT("1"))
        {
            P4Kadan::Tell(TEXT("A material instance cannot be made from ") + Parent->GetName() +
                TEXT(", because it is a copy of game data or a parent that was not recovered."));
            continue;
        }
        SupportedParents.Add(Parent);
    }
    TArray<UObject*> Made;
    for (UMaterialInterface* Parent : SupportedParents)
    {
        FString Package, Name;
        FString Target = Parent->GetOutermost()->GetName();
        if (!P4IsCreatorAsset(Parent))
        {
            FString Root, Error;
            if (!P4KadanActiveCreatorRoot(Root, Error))
            {
                P4Kadan::Tell(Error);
                continue;
            }
            Target = Root + TEXT("/") + Parent->GetName();
        }
        Tools.CreateUniqueAssetName(Target, TEXT("_Inst"), Package, Name);
        UP4CreatorMaterialInstanceFactory* Factory = NewObject<UP4CreatorMaterialInstanceFactory>();
        Factory->InitialParent = Parent;
        // Match the ordinary native action: one selection starts Browser inline naming;
        // multiple selections create each instance immediately and sync the results.
        if (SupportedParents.Num() == 1)
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
                .Get()
                .CreateNewAsset(
                    Name, FPackageName::GetLongPackagePath(Package), UMaterialInstanceConstant::StaticClass(), Factory);
        else if (UObject* Asset = Tools.CreateAsset(Name, FPackageName::GetLongPackagePath(Package),
                     UMaterialInstanceConstant::StaticClass(), Factory))
            Made.Add(Asset);
    }
    if (Made.Num())
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(Made);
}

void P4CreateCreatorMaterials(const TArray<UObject*>& Textures)
{
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TArray<UTexture*> SupportedTextures;
    for (UObject* Object : Textures)
    {
        UTexture* Texture = Cast<UTexture>(Object);
        if (!IsValid(Texture))
            continue;
        if (P4HasIntrinsicGameProxy(Texture) || P4HasCopiedGameData(Texture) ||
            FString(Texture->GetOutermost()->GetMetaData()->GetValue(Texture, TEXT("P4BlueprintStandIn"))) == TEXT("1"))
        {
            P4Kadan::Tell(TEXT("A material cannot be made from ") + Texture->GetName() +
                TEXT(", because it is a copy of game data or a texture that was not recovered."));
            continue;
        }
        SupportedTextures.Add(Texture);
    }
    TArray<UObject*> Made;
    for (UTexture* Texture : SupportedTextures)
    {
        FString Package, Name;
        FString Target = Texture->GetOutermost()->GetName();
        if (!P4IsCreatorAsset(Texture))
        {
            FString Root, Error;
            if (!P4KadanActiveCreatorRoot(Root, Error))
            {
                P4Kadan::Tell(Error);
                continue;
            }
            Target = Root + TEXT("/") + Texture->GetName();
        }
        Tools.CreateUniqueAssetName(Target, TEXT("_Mat"), Package, Name);
        UP4CreatorMaterialFactory* Factory = NewObject<UP4CreatorMaterialFactory>();
        Factory->InitialTexture = Texture;
        if (SupportedTextures.Num() == 1)
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
                .Get()
                .CreateNewAsset(Name, FPackageName::GetLongPackagePath(Package), UMaterial::StaticClass(), Factory);
        else if (UObject* Asset = Tools.CreateAsset(
                     Name, FPackageName::GetLongPackagePath(Package), UMaterial::StaticClass(), Factory))
            Made.Add(Asset);
    }
    if (Made.Num())
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(Made);
}

void P4InitializeCreatorMaterialFactories()
{
    if (Installed || !IsInGameThread() || EngineExiting || GExitPurge)
        return;
    // Initialize our CDOs while the native defaults are still present. Native menu generation
    // enumerates factory CDOs and calls ShouldShowInNewMenu; it has no per-factory filter hook.
    GetDefault<UP4CreatorMaterialFactory>();
    GetDefault<UP4CreatorMaterialInstanceFactory>();
    UMaterialFactoryNew* Material = GetMutableDefault<UMaterialFactoryNew>();
    UMaterialInstanceConstantFactoryNew* Instance = GetMutableDefault<UMaterialInstanceConstantFactoryNew>();
    // The exact native field is a protected UPROPERTY uint32 bit. Use its reflected
    // boolean API rather than access-control casts or assumptions about memory offsets.
    FBoolProperty* MenuFlag = FindFProperty<FBoolProperty>(UFactory::StaticClass(), TEXT("bCreateNew"));
    if (!ensure(MenuFlag))
        return;
    NativeMaterialDefault = Material;
    NativeInstanceDefault = Instance;
    NativeCreateNewProperty = MenuFlag;
    OriginalMaterialCreateNew = MenuFlag->GetPropertyValue_InContainer(Material);
    OriginalInstanceCreateNew = MenuFlag->GetPropertyValue_InContainer(Instance);
    MenuFlag->SetPropertyValue_InContainer(Material, false);
    MenuFlag->SetPropertyValue_InContainer(Instance, false);
    Installed = true;
    // Module shutdown runs after UObject teardown in the engine exit path. Restore
    // native menus while their existing CDOs and reflected fields are still alive.
    EnginePreExit = FCoreDelegates::OnEnginePreExit.AddStatic(&RestoreFactoriesBeforeEngineExit);
}
void P4ShutdownCreatorMaterialFactories()
{
    if (!IsInGameThread())
        return;
    FCoreDelegates::OnEnginePreExit.Remove(EnginePreExit);
    EnginePreExit.Reset();
    if (!Installed)
        return;
    if (!GExitPurge)
        if (FBoolProperty* MenuFlag = NativeCreateNewProperty.Get())
        {
            // Hot unload also restores the exact CDOs we changed. Never construct a
            // default object or resolve a native class from a late shutdown callback.
            if (UMaterialFactoryNew* Material = NativeMaterialDefault.Get())
                if (!MenuFlag->GetPropertyValue_InContainer(Material))
                    MenuFlag->SetPropertyValue_InContainer(Material, OriginalMaterialCreateNew);
            if (UMaterialInstanceConstantFactoryNew* Instance = NativeInstanceDefault.Get())
                if (!MenuFlag->GetPropertyValue_InContainer(Instance))
                    MenuFlag->SetPropertyValue_InContainer(Instance, OriginalInstanceCreateNew);
        }
    NativeMaterialDefault.Reset();
    NativeInstanceDefault.Reset();
    NativeCreateNewProperty.Reset();
    Installed = false;
}

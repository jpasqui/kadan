#include "P4ProjectInventory.h"
#include "P4GamePaths.h"
#include "P4CreatorSource.h"
#include "P4GameNames.h"
#include "P4AuthoringRows.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace P4ProjectInventory
{
struct FSceneBinding
{
    FString Scene;
    FString World;
    FString Error;
    bool bAuthored = false;
    bool bSavedWorld = false;
};

static bool ValidWorldPackage(const FString& Package)
{
    return P4IsValidContentPackageIgnoringCase(Package) && !Package.Contains(TEXT("."));
}

static FString Serialize(const TSharedRef<FJsonObject>& Object)
{
    FString Result;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Result));
    return Result;
}

static bool SavedWorld(const FString& Package, IAssetRegistry& Registry)
{
    const FString ObjectPath = P4ObjectPathOf(Package);
    const FAssetData Asset = Registry.GetAssetByObjectPath(FName(*ObjectPath));
    return Asset.IsValid() && Asset.AssetClass == UWorld::StaticClass()->GetFName() &&
        FPackageName::DoesPackageExist(Package);
}

static TArray<FSceneBinding> ReadBindings()
{
    TArray<FSceneBinding> Result;
    UClass* SceneClass = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".GameSceneData"));
    // A blank project need not have imported the game kit yet.
    if (!SceneClass)
        return Result;
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> Assets;
    Registry.GetAssetsByClass(SceneClass->GetFName(), Assets, true);
    Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.ObjectPath.LexicalLess(B.ObjectPath); });
    for (const FAssetData& Data : Assets)
    {
        UObject* Scene = Data.GetAsset();
        if (!IsValid(Scene) || !Scene->IsA(SceneClass))
            continue;
        FSceneBinding Binding;
        Binding.Scene = Scene->GetPathName();
        Binding.bAuthored = P4IsCreatorAsset(Scene);
        FSoftObjectProperty* OpenLevel = FindFProperty<FSoftObjectProperty>(Scene->GetClass(), TEXT("OpenLevel"));
        if (!OpenLevel)
            Binding.Error = TEXT("Native scene has no supported OpenLevel property: ") + Binding.Scene;
        else
        {
            const FSoftObjectPath Path = OpenLevel->GetPropertyValue_InContainer(Scene).ToSoftObjectPath();
            if (Path.IsNull())
                continue; // Parent scenes intentionally have no world.
            Binding.World = Path.GetLongPackageName();
            if (!ValidWorldPackage(Binding.World) || !Path.GetSubPathString().IsEmpty() ||
                Path.GetAssetName() != FPackageName::GetLongPackageAssetName(Binding.World))
                Binding.Error = TEXT("Scene OpenLevel must reference its world asset: ") + Binding.Scene;
            else
            {
                Binding.bSavedWorld = SavedWorld(Binding.World, Registry);
                if (!Binding.bSavedWorld)
                    Binding.Error = TEXT("Save or restore the world referenced by ") + Binding.Scene + TEXT(": ") +
                        Binding.World;
            }
        }
        Result.Add(MoveTemp(Binding));
    }
    return Result;
}

FString InventoryJson()
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("format"), 1);
    Root->SetStringField(TEXT("source"), TEXT("native-editor-assets"));
    TArray<TSharedPtr<FJsonValue>> Scenes;
    for (const FSceneBinding& Binding : ReadBindings())
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("scene"), Binding.Scene);
        Row->SetStringField(TEXT("world"), Binding.World);
        Row->SetBoolField(TEXT("authored"), Binding.bAuthored);
        Row->SetBoolField(TEXT("saved_world"), Binding.bSavedWorld);
        Row->SetStringField(TEXT("error"), Binding.Error);
        Scenes.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("scenes"), Scenes);
    return Serialize(Root);
}

FString WorldPlanJson(const FString& WorldPackage)
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("format"), 1);
    Root->SetStringField(TEXT("world"), WorldPackage);
    Root->SetStringField(TEXT("route"), TEXT("unbound"));
    Root->SetStringField(TEXT("scene"), TEXT(""));
    // These are mandatory policies, never optional export switches.
    Root->SetBoolField(TEXT("full_scene"), true);
    Root->SetBoolField(TEXT("strip_game_assets"), true);
    TArray<TSharedPtr<FJsonValue>> Errors;
    if (!ValidWorldPackage(WorldPackage))
        Errors.Add(MakeShared<FJsonValueString>(TEXT("Expected a saved /Game world package")));
    else
    {
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        if (!SavedWorld(WorldPackage, Registry))
            Errors.Add(
                MakeShared<FJsonValueString>(TEXT("Save or restore the level before exporting: ") + WorldPackage));
        TArray<FSceneBinding> Matches;
        for (const FSceneBinding& Binding : ReadBindings())
            if (Binding.World.Equals(WorldPackage, ESearchCase::IgnoreCase))
                Matches.Add(Binding);
        if (Matches.Num() > 1)
            Errors.Add(MakeShared<FJsonValueString>(TEXT("Multiple native scene assets refer to ") + WorldPackage));
        else if (Matches.Num() == 1)
        {
            const FSceneBinding& Binding = Matches[0];
            if (!Binding.Error.IsEmpty())
                Errors.Add(MakeShared<FJsonValueString>(Binding.Error));
            Root->SetStringField(TEXT("scene"), Binding.Scene);
            Root->SetStringField(TEXT("route"), Binding.bAuthored ? TEXT("independent") : TEXT("original"));
        }
    }
    Root->SetBoolField(TEXT("valid"), Errors.Num() == 0);
    Root->SetArrayField(TEXT("errors"), Errors);
    return Serialize(Root);
}
} //namespace P4ProjectInventory

FString UP4AuthoringTools::NativeProjectInventory()
{
    return P4ProjectInventory::InventoryJson();
}

FString UP4AuthoringTools::NativeWorldExportPlan(const FString& WorldPackage)
{
    return P4ProjectInventory::WorldPlanJson(WorldPackage);
}

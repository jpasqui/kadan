#include "P4CreatorSource.h"
#include "AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
const TCHAR* const OldRoot = TEXT("/Game/Carrot4/");

void Compare(FAutomationTestBase& Test, const TCHAR* What, int32 Total, const TArray<FString>& Differ)
{
    Test.AddInfo(FString::Printf(TEXT("%s: %d checked, %d where the rules differ"), What, Total, Differ.Num()));
    for (int32 At = 0; At < Differ.Num() && At < 20; ++At)
        Test.AddInfo(FString(TEXT("  differs: ")) + Differ[At]);
}
} //namespace

// Which assets and classes are the game's: the provenance rules agree with the /Game/Carrot4/ folder test
// they replaced, over every Blueprint, material, texture and scene of the project. Needs a project with
// imported game content.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanGameProvenanceRules, "P4MapKit.Kadan.GameProvenanceRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKadanGameProvenanceRules::RunTest(const FString&)
{
    // The project's assets: the game's own by their import tag, where they were the game's by their folder.
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FName> Classes = {UBlueprint::StaticClass()->GetFName(), UMaterialInterface::StaticClass()->GetFName(),
        UTexture::StaticClass()->GetFName(), FName(TEXT("GameSceneData"))};
    int32 Total = 0, Game = 0, Blueprints = 0;
    TArray<FString> AssetsDiffer, ClassesDiffer;
    for (const FName& Class : Classes)
    {
        TArray<FAssetData> Found;
        Registry.GetAssetsByClass(Class, Found, true);
        for (const FAssetData& Data : Found)
        {
            const FString Package = Data.PackageName.ToString();
            if (!Package.StartsWith(TEXT("/Game/")))
                continue;
            UObject* Asset = Data.GetAsset();
            if (!IsValid(Asset))
                continue;
            ++Total;
            const bool Old = Package.StartsWith(OldRoot, ESearchCase::IgnoreCase);
            const bool New = !P4IsCreatorAsset(Asset);
            Game += New ? 1 : 0;
            if (Old != New)
                AssetsDiffer.Add(Package + (New ? TEXT(" (now the game's)") : TEXT(" (now the creator's)")));
            const UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
            if (!Blueprint || !Blueprint->GeneratedClass)
                continue;
            ++Blueprints;
            const bool OldClass = Blueprint->GeneratedClass->GetOutermost()->GetName().StartsWith(OldRoot);
            if (OldClass != P4IsGameClass(Blueprint->GeneratedClass))
                ClassesDiffer.Add(Blueprint->GeneratedClass->GetPathName());
        }
    }
    Compare(*this, TEXT("Project assets"), Total, AssetsDiffer);
    Compare(*this, TEXT("Blueprint classes"), Blueprints, ClassesDiffer);
    AddInfo(FString::Printf(TEXT("%d of the project's assets are the game's"), Game));
    TestTrue(TEXT("The project has imported game content"), Game > 0);
    TestEqual(TEXT("Both rules say the same of every asset"), AssetsDiffer.Num(), 0);
    TestEqual(TEXT("Both rules say the same of every Blueprint class"), ClassesDiffer.Num(), 0);
    return !HasAnyErrors();
}
#endif

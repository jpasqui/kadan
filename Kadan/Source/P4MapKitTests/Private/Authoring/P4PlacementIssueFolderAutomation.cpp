#if P4_MAPKIT_TESTS
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "P4GameNames.h"
#include "P4PlacementExport.h"
#include "UObject/Package.h"

// A creator's gameplay class gets the same verdict from the placement check wherever Kadan keeps its mod's
// packages: in the mod's folder under /Game/Mods, and in the live preview's copy of it under the preview's
// folder (the path the preview gives a copied creator package: its preview mod's folder, then the package's
// path under /Game). A class lying directly in either folder, outside a mod's own, is refused in both.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PlacementIssueModFolders, "P4MapKit.Export.PlacementIssueModFolders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PlacementIssueModFolders::RunTest(const FString&)
{
    UClass* GActor = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".GActor"));
    if (!TestNotNull(TEXT("The game's gameplay actor class is in the project"), GActor))
        return false;
    const FString Run = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    TArray<UBlueprint*> Made;
    ON_SCOPE_EXIT
    {
        for (UBlueprint* Blueprint : Made)
        {
            UPackage* Package = Blueprint->GetOutermost();
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
            Package->SetDirtyFlag(false);
            Package->Rename(
                *(TEXT("/Temp/P4PlacementIssueModFolders_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr,
                REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
            Package->MarkPendingKill();
        }
    };
    const auto Make = [&](UClass* Parent, const FString& Package, bool WithComponent) -> UBlueprint*
    {
        const FString Name = FPackageName::GetLongPackageAssetName(Package);
        UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*Package), FName(*Name),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (!Blueprint)
            return nullptr;
        Made.Add(Blueprint);
        if (WithComponent)
        {
            USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(
                USceneComponent::StaticClass(), TEXT("AddedComponent"));
            Blueprint->SimpleConstructionScript->AddNode(Node);
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        return Blueprint;
    };
    // A stand-in for one of the game's gameplay Blueprints: a content package outside every mod's folder.
    UBlueprint* GameParent = Make(GActor, TEXT("/Game/P4PlacementIssueTest_") + Run + TEXT("/GGameParent"), false);
    if (!TestTrue(TEXT("The stand-in game Blueprint compiles"), GameParent && GameParent->GeneratedClass))
        return false;
    const FString ModPath = TEXT("Mods/issue") + Run.Left(8);
    const FString Folders[] = {TEXT("/Game/") + ModPath, TEXT(P4_PREVIEW_PACKAGE_ROOT) + Run + TEXT("/") + ModPath};
    const auto Verdict = [&](const FString& Package, bool WithComponent, FString& Out)
    {
        UBlueprint* Blueprint = Make(GameParent->GeneratedClass, Package, WithComponent);
        if (!Blueprint || !Blueprint->GeneratedClass)
            return false;
        Out = P4PlacementIssue(Blueprint->GeneratedClass->GetDefaultObject<AActor>());
        return true;
    };
    FString Edited[2], Plain[2], Loose[2];
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (!TestTrue(TEXT("The creator classes compile: ") + Folders[Index],
                Verdict(Folders[Index] + TEXT("/GEdited"), true, Edited[Index]) &&
                    Verdict(Folders[Index] + TEXT("/GPlain"), false, Plain[Index])))
            return false;
    }
    const FString LooseFolders[] = {TEXT(P4_MODS_FOLDER), TEXT(P4_PREVIEW_FOLDER)};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (!TestTrue(TEXT("A loose creator class compiles in ") + LooseFolders[Index],
                Verdict(LooseFolders[Index] + TEXT("/GLoose") + Run.Left(8), false, Loose[Index])))
            return false;
    }
    TestTrue(TEXT("In its mod's folder, a gameplay class with an added component is refused by the creator check: ") +
            Edited[0],
        Edited[0].Contains(TEXT("Added components")));
    TestEqual(TEXT("The preview's copy of it gets the same verdict"), Edited[1], Edited[0]);
    TestEqual(TEXT("A data-only gameplay class in its mod's folder is accepted"), Plain[0], FString());
    TestEqual(TEXT("The preview's copy of it is accepted too"), Plain[1], Plain[0]);
    TestEqual(TEXT("A gameplay class directly in the folder of all mods is kept to a mod's folder"), Loose[0],
        FString(TEXT("Keep the new gameplay class inside its mod's folder.")));
    TestEqual(TEXT("One directly in the preview's folder gets the same verdict"), Loose[1], Loose[0]);
    return !HasAnyErrors();
}
#endif

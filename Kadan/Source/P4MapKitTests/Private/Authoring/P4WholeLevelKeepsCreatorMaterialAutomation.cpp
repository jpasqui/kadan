#if P4_MAPKIT_TESTS
// A game level that ships whole keeps the creator's material on its meshes after the export. The cook of a level that
// ships whole uses runtime copies of the creator's material graphs and releases them when the export ends; the open
// level has the creator's own material again, and the unsaved state it had. A movable mesh is used so the export
// builds no lighting. Runs in a project with an imported map (P4_WHOLE_LEVEL_MAP, Cave000 by default).
#include "P4ExportDriverTestShared.h"
#include "P4CreatorMaterialFactories.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WholeLevelKeepsCreatorMaterialTest, "P4MapKit.Export.WholeLevelKeepsCreatorMaterial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WholeLevelKeepsCreatorMaterialTest::RunTest(const FString&)
{
    FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_WHOLE_LEVEL_MAP"));
    if (Map.IsEmpty())
        Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    if (!FPackageName::DoesPackageExist(Map))
    {
        AddWarning(TEXT("P4MapKit test not run. ") + Map + TEXT(" is not imported in this project."));
        return true;
    }
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(*this, TEXT("kadan.whole.material"), TEXT("Whole Level Material"), Request, Names))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The map opens"), World))
        return false;
    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);

    // The creator's mesh, and a material graph of the creator's own set on it in the level.
    P4ExportDriverTest::FOwn Own;
    if (!P4ExportDriverTest::MakeOwn(*this, Names.PackageRoot / (TEXT("WholeLevelCube_") + Suffix), Own))
        return false;
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Graph(Cast<UMaterial>(Tools.CreateAsset(
        TEXT("M_WholeLevel_") + Suffix, Names.PackageRoot, UMaterial::StaticClass(), Factory.Get())));
    if (!TestNotNull(TEXT("Kadan's material action made the graph"), Graph.Get()))
        return false;
    Graph->BlendMode = BLEND_Opaque;
    Graph->SetShadingModel(MSM_Unlit);
    UMaterialExpressionConstant3Vector* Color = NewObject<UMaterialExpressionConstant3Vector>(Graph.Get());
    Color->Constant = FLinearColor(1.f, 0.f, 1.f);
    Graph->Expressions.Add(Color);
    Graph->EmissiveColor.Expression = Color;
    Graph->PostEditChange();

    FActorSpawnParameters Spawn;
    Spawn.Name = FName(*(TEXT("P4WholeLevelMaterial_") + Suffix));
    Spawn.OverrideLevel = World->PersistentLevel;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AStaticMeshActor* Added =
        World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("The mesh is placed in the level"), Added))
        return false;
    ON_SCOPE_EXIT
    {
        if (IsValid(Added))
            World->DestroyActor(Added);
        UEditorLoadingAndSavingUtils::SaveMap(World, Map);
    };
    UStaticMeshComponent* Part = Added->GetStaticMeshComponent();
    // Movable: the mesh has no baked lighting, so the export does not build the map's lighting.
    Part->SetMobility(EComponentMobility::Movable);
    Part->SetStaticMesh(Own.Mesh.Get());
    Part->SetMaterial(0, Graph.Get());
    TArray<UPackage*> Saves = {Graph->GetOutermost()};
    for (UObject* Made : Own.Made)
        if (Made)
            Saves.AddUnique(Made->GetOutermost());
    if (!TestTrue(TEXT("The creator's assets are saved"), UEditorLoadingAndSavingUtils::SavePackages(Saves, false)) ||
        !TestTrue(TEXT("The level is saved with the mesh"), UEditorLoadingAndSavingUtils::SaveMap(World, Map)))
        return false;
    UPackage* const LevelPackage = World->PersistentLevel->GetOutermost();
    const bool DirtyBefore = LevelPackage->IsDirty();

    Request.MapPackage = Map;
    Request.Roots.Add({EP4ExportRootKind::Package, Map});
    Request.WholeLevels = true;
    FP4ExportModResult Result;
    FString Error;
    const bool Exported = P4ExportMod(Request, Result, Error);
    if (!TestTrue(TEXT("The level ships whole: ") + Error, Exported))
        for (const FString& Why : Result.Refused)
            AddError(Why);
    if (Exported)
        TestTrue(TEXT("The edited level is in the mod whole, at its game path"), Result.Replacements.Contains(Map));

    TestTrue(TEXT("After the export the level's mesh uses the creator's own material"),
        Part->GetMaterial(0) == Graph.Get());
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("It still does once garbage is collected"),
        IsValid(Added) && Added->GetStaticMeshComponent()->GetMaterial(0) == Graph.Get());
    TestEqual(TEXT("The level's unsaved state is as it was before the export"), LevelPackage->IsDirty(), DirtyBefore);
    return !HasAnyErrors();
}
#endif

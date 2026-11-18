#if P4_MAPKIT_TESTS
#include "P4CreatorSource.h"
#include "P4CreatorSurfaceAdvice.h"
#include "AssetExportTask.h"
#include "AssetImportTask.h"
#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Exporters/Exporter.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace P4ExportDriverTest
{
UObject* ImportOwn(FAutomationTestBase&, const FString&, const FString&, bool);
}

namespace P4CreatorMeshFileColorsTest
{
// The FBX import of File into Package, as the import dialog starts it (the factory's own options) or with a task's
// explicit options.
UStaticMesh* Import(const FString& File, const FString& Package, UFbxImportUI* TaskOptions)
{
    TStrongObjectPtr<UFbxFactory> Factory(NewObject<UFbxFactory>());
    Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
    Factory->AssetImportTask->bAutomated = true;
    Factory->AssetImportTask->Options = TaskOptions;
    Factory->SetDetectImportTypeOnImport(false);
    for (UFbxImportUI* UI : {Factory->ImportUI, TaskOptions})
        if (UI)
        {
            UI->MeshTypeToImport = FBXIT_StaticMesh;
            UI->bAutomatedImportShouldDetectType = false;
            UI->bImportMaterials = false;
            UI->bImportTextures = false;
            UI->StaticMeshImportData->bAutoGenerateCollision = false;
        }
    bool Cancelled = false;
    UObject* Made = Factory->ImportObject(UStaticMesh::StaticClass(), CreatePackage(*Package),
        FName(*FPackageName::GetShortName(Package)), RF_Public | RF_Standalone | RF_Transactional, File, nullptr, Cancelled);
    if (Made)
        FAssetRegistryModule::AssetCreated(Made);
    Factory->CleanUp();
    return Cast<UStaticMesh>(Made);
}
float LowestRed(UStaticMesh* Mesh)
{
    float Red = 9.f;
    if (const FMeshDescription* Description = Mesh->GetMeshDescription(0))
    {
        const auto Colors = FStaticMeshConstAttributes(*Description).GetVertexInstanceColors();
        for (const FVertexInstanceID Corner : Description->VertexInstances().GetElementIDs())
            Red = FMath::Min(Red, Colors[Corner].X);
    }
    return Red;
}
}

// A tester's cave room from Blender lost its painted blend and fade masks: Unreal's FBX import starts with Vertex Color
// Import Option Ignore, which makes every vertex of a new mesh white. That is Unreal's own choice and Kadan leaves it:
// with Replace the file's colors are kept, and a mesh imported without them says so when its material reads them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorMeshFileColorsTest, "P4MapKit.Kadan.CreatorMeshKeepsFileColors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorMeshFileColorsTest::RunTest(const FString&)
{
    using namespace P4CreatorMeshFileColorsTest;
    const FString Root = TEXT("/Game/Mods/CreatorMeshFileColorsTest");
    TArray<UStaticMesh*> Made;
    ON_SCOPE_EXIT
    {
        for (UStaticMesh* Mesh : Made)
            if (IsValid(Mesh))
            {
                FAssetRegistryModule::AssetDeleted(Mesh);
                Mesh->ClearFlags(RF_Public | RF_Standalone);
                Mesh->MarkPendingKill();
                Mesh->GetOutermost()->SetDirtyFlag(false);
            }
    };
    // A painted FBX: a cube whose corners run from black to white, written by Unreal's own FBX exporter.
    UStaticMesh* Painted = Cast<UStaticMesh>(P4ExportDriverTest::ImportOwn(*this, Root, TEXT("PaintedSource"), true));
    if (!TestNotNull(TEXT("The source cube imports"), Painted))
        return false;
    Made.Add(Painted);
    {
        FMeshDescription* Description = Painted->GetMeshDescription(0);
        if (!TestNotNull(TEXT("The source cube has its mesh description"), Description))
            return false;
        FStaticMeshAttributes Attributes(*Description);
        int32 Index = 0;
        for (const FVertexInstanceID Corner : Description->VertexInstances().GetElementIDs())
        {
            const float Value = float(Index++ % 4) / 3.f;
            Attributes.GetVertexInstanceColors()[Corner] = FVector4(Value, 1.f, 1.f, 1.f - Value);
        }
        Painted->CommitMeshDescription(0);
        Painted->Build(true);
    }
    const FString Folder =
        FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/CreatorMeshFileColorsTest"));
    IFileManager::Get().MakeDirectory(*Folder, true);
    const FString File = Folder / TEXT("Painted.fbx");
    IFileManager::Get().Delete(*File, false, true, true);
    auto* Export = NewObject<UAssetExportTask>();
    Export->Object = Painted;
    Export->Exporter = UExporter::FindExporter(Painted, TEXT("FBX"));
    Export->Filename = File;
    Export->bAutomated = true;
    Export->bPrompt = false;
    Export->bReplaceIdentical = true;
    if (!TestTrue(TEXT("Unreal's FBX exporter writes the painted cube"),
            Export->Exporter && UExporter::RunAssetExportTask(Export) && IFileManager::Get().FileSize(*File) > 0))
        return false;

    UStaticMesh* Started = Import(File, Root / TEXT("FromDialogStart"), nullptr);
    if (!TestNotNull(TEXT("The painted FBX imports with the import dialog's starting options"), Started))
        return false;
    Made.Add(Started);
    const auto* StartedRecord = Cast<UFbxStaticMeshImportData>(Started->AssetImportData);
    TestTrue(TEXT("Kadan leaves Unreal's starting Vertex Color Import Option as it is"),
        StartedRecord && StartedRecord->VertexColorImportOption ==
            GetDefault<UFbxStaticMeshImportData>()->VertexColorImportOption);

    auto* Keep = NewObject<UFbxImportUI>();
    Keep->StaticMeshImportData->VertexColorImportOption = EVertexColorImportOption::Replace;
    UStaticMesh* Fresh = Import(File, Root / TEXT("FromReplaceTask"), Keep);
    if (!TestNotNull(TEXT("The painted FBX imports with Replace"), Fresh))
        return false;
    Made.Add(Fresh);
    TestTrue(TEXT("The new mesh is the creator's own"), P4HasCreatorSource(Fresh));
    TestTrue(TEXT("With Replace the painted black corners of the file are kept"), LowestRed(Fresh) < 0.05f);

    auto* Ignore = NewObject<UFbxImportUI>();
    Ignore->StaticMeshImportData->VertexColorImportOption = EVertexColorImportOption::Ignore;
    UStaticMesh* Ignored = Import(File, Root / TEXT("FromIgnoreTask"), Ignore);
    if (!TestNotNull(TEXT("The painted FBX imports with a task that ignores its colors"), Ignored))
        return false;
    Made.Add(Ignored);
    const auto* IgnoredRecord = Cast<UFbxStaticMeshImportData>(Ignored->AssetImportData);
    TestTrue(TEXT("A task's explicit Ignore is kept"),
        IgnoredRecord && IgnoredRecord->VertexColorImportOption == EVertexColorImportOption::Ignore);
    TestTrue(TEXT("Ignore leaves the new mesh white"), LowestRed(Ignored) > 0.99f);

    TStrongObjectPtr<UMaterial> Plain(NewObject<UMaterial>());
    Plain->PostEditChange();
    TStrongObjectPtr<UMaterial> Masked(NewObject<UMaterial>());
    auto* Color = NewObject<UMaterialExpressionVertexColor>(Masked.Get());
    Masked->Expressions.Add(Color);
    Masked->BaseColor.Expression = Color;
    Masked->PostEditChange();
    Ignored->SetMaterial(0, Plain.Get());
    TestTrue(TEXT("A white mesh whose materials read no vertex colors gets no note"),
        P4IgnoredVertexColorsNote(Ignored).IsEmpty());
    Ignored->SetMaterial(0, Masked.Get());
    const FString Note = P4IgnoredVertexColorsNote(Ignored);
    TestTrue(TEXT("A white mesh imported with Ignore under a vertex color material is explained"),
        Note.Contains(TEXT("Vertex Color Import Option set to Ignore")) && Note.Contains(TEXT("Replace")));
    TestTrue(TEXT("The selected-assets help carries the same note"),
        P4InspectCreatorSurface(Ignored).ContainsByPredicate(
            [](const FP4CreatorSurfaceAdvice& Row) { return Row.Code == TEXT("mesh-vertex-colors-not-imported"); }));
    Fresh->SetMaterial(0, Masked.Get());
    TestTrue(TEXT("A mesh with its file's colors gets no note"), P4IgnoredVertexColorsNote(Fresh).IsEmpty());
    return !HasAnyErrors();
}
#endif

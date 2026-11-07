#include "P4NativeCustomCompilerBridge.h"
#include "P4CreatorMaterialFactories.h"
#include "P4CreatorSource.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MaterialEditor/PreviewMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialEditorPreviewGraphTest, "P4MapKit.Kadan.MaterialEditorPreviewGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialEditorPreviewGraphTest::RunTest(const FString&)
{
    const FString Name = TEXT("/Game/Mods/__P4PreviewPolicy_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) +
        TEXT("/M_Control");
    TStrongObjectPtr<UPackage> Package(CreatePackage(*Name));
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Material(Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), Package.Get(), TEXT("M_Control"), RF_Public | RF_Standalone, nullptr, GWarn)));
    if (!TestNotNull(TEXT("Genuine creator factory makes the controlled material"), Material.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package.Get(), Objects, true);
        for (UObject* Object : Objects)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->SetFlags(RF_Transient);
        }
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    };
    if (!TestTrue(TEXT("Untouched creator defaults are supported opaque Default Lit settings"),
            Material->MaterialDomain == MD_Surface && Material->BlendMode == BLEND_Opaque &&
                Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit) && !Material->TwoSided) ||
        !TestTrue(TEXT("Creator factory supplies supported Color Roughness decal response"),
            Material->MaterialDecalResponse == MDR_ColorRoughness))
        return false;
    auto* Color = NewObject<UMaterialExpressionConstant3Vector>(Material.Get());
    Color->Material = Material.Get();
    Color->Constant = FLinearColor(0.9f, 0.2f, 0.15f);
    auto* Roughness = NewObject<UMaterialExpressionConstant>(Material.Get());
    Roughness->Material = Material.Get();
    Roughness->R = 0.6f;
    Material->Expressions = {Color, Roughness};
    Material->BaseColor.Expression = Color;
    Material->Roughness.Expression = Roughness;
    if (!TestTrue(TEXT("Factory provenance and the original supported graph qualify"),
            P4HasCreatorSource(Material.Get()) && P4CreatorMaterialGraphIssue(Material.Get()).IsEmpty()))
        return false;

    // Match FMaterialEditor::InitEditorForMaterial: a base UMaterial duplicate
    // misses the actual subclass that triggered the UI regression.
    TStrongObjectPtr<UMaterial> Preview(Cast<UMaterial>(StaticDuplicateObject(
        Material.Get(), GetTransientPackage(), NAME_None, ~RF_Standalone, UPreviewMaterial::StaticClass())));
    if (!TestNotNull(TEXT("Stock editor preview subclass duplicates the real graph"), Preview.Get()))
        return false;
    Preview->bIsPreviewMaterial = true;
    const auto Bytes = [](UObject* Object)
    {
        TArray<uint8> Out;
        FObjectWriter Writer(Object, Out, false, false, false);
        return Out;
    };
    const auto OriginalBefore = Bytes(Material.Get()), PreviewBefore = Bytes(Preview.Get());
    const bool DirtyBefore = Package->IsDirty();
    TestTrue(TEXT("Real stock preview receives graph/settings feedback without false origin warning"),
        P4CreatorMaterialEditorGraphIssue(Preview.Get()).IsEmpty());
    const FString OriginIssue = P4CreatorMaterialGraphIssue(Preview.Get());
    TestFalse(TEXT("UI preview acceptance never admits the export producer"), OriginIssue.IsEmpty());
    FP4CreatorMaterialCompileRequest Request;
    Request.CreatorMaterial = Preview.Get();
    const auto Refused = P4CompileCreatorMaterial(Request);
    TestTrue(TEXT("Compile refuses the preview before compiling"), !Refused.Succeeded && Refused.Error == OriginIssue);

    Preview->BlendMode = BLEND_Additive;
    const FString EditedIssue = P4CreatorMaterialEditorGraphIssue(Preview.Get());
    TestTrue(TEXT("Unsupported unsaved preview settings remain visible"),
        !EditedIssue.IsEmpty() && EditedIssue != OriginIssue);
    TestTrue(TEXT("Unsaved preview edits do not change the original graph feedback"),
        P4CreatorMaterialEditorGraphIssue(Material.Get()).IsEmpty());
    Preview->BlendMode = BLEND_Opaque;
    Preview->bIsPreviewMaterial = false;
    TestFalse(TEXT("A stock subclass without the editor preview role refuses"),
        P4CreatorMaterialEditorGraphIssue(Preview.Get()).IsEmpty());
    Preview->bIsPreviewMaterial = true;
    TStrongObjectPtr<UPreviewMaterial> WrongOuter(
        NewObject<UPreviewMaterial>(Package.Get(), TEXT("WrongOuter"), RF_Transient));
    WrongOuter->bIsPreviewMaterial = true;
    TestFalse(TEXT("A stock preview subclass in an asset package refuses"),
        P4CreatorMaterialEditorGraphIssue(WrongOuter.Get()).IsEmpty());
    TestTrue(TEXT("Read-only feedback preserves original native values"), OriginalBefore == Bytes(Material.Get()));
    TestTrue(
        TEXT("Read-only feedback preserves restored native preview values"), PreviewBefore == Bytes(Preview.Get()));
    TestEqual(TEXT("Read-only feedback preserves original dirty state"), Package->IsDirty(), DirtyBefore);
    TestTrue(TEXT("Actual creator origin is preserved"), P4HasCreatorSource(Material.Get()));
    TestFalse(TEXT("The controlled material was never saved"), FPackageName::DoesPackageExist(Name));
    return !HasAnyErrors();
}
#endif

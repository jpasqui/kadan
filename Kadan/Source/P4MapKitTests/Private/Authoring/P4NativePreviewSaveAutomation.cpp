#include "Misc/AutomationTest.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "P4NativeConstructionPreview.h"

// A save suspends the previews and collects garbage before it writes. The preview material is then held only by the
// suspended copy of the value, and has to come back alive when the save ends: Kadan 0.3.0 freed it, put the freed
// pointer back on the mesh, and the editor crashed drawing the mesh after every save of a map with previews.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativePreviewSurvivesSave, "P4MapKit.Editor.NativePreviewSurvivesSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4NativePreviewSurvivesSave::RunTest(const FString&)
{
    TStrongObjectPtr<UStaticMeshComponent> Mesh(
        NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient));
    FProperty* Overrides = FindFProperty<FProperty>(UMeshComponent::StaticClass(), TEXT("OverrideMaterials"));
    if (!TestNotNull(TEXT("OverrideMaterials"), Overrides))
        return false;
    P4RememberNativePreviewValue(Mesh.Get(), Overrides);
    UMaterialInstanceDynamic* Preview =
        UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), GetTransientPackage());
    Preview->SetFlags(RF_Transient);
    Mesh->OverrideMaterials.Add(Preview);
    const TWeakObjectPtr<UMaterialInstanceDynamic> Held(Preview);
    Preview = nullptr;

    P4SetNativePreviewsSuspended(true);
    TestEqual(TEXT("the map's own value while suspended"), Mesh->OverrideMaterials.Num(), 0);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("the preview material survives the save's garbage collection"), Held.IsValid());
    P4SetNativePreviewsSuspended(false);
    TestTrue(TEXT("the same preview material is shown again"),
        Mesh->OverrideMaterials.Num() == 1 && Held.IsValid() && Mesh->OverrideMaterials[0] == Held.Get());

    // Once its component is gone, the remembered value no longer holds the preview: the first collection destroys
    // the component, the second the preview.
    Mesh.Reset();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("a preview of a destroyed component is collected"), Held.IsValid());
    return true;
}

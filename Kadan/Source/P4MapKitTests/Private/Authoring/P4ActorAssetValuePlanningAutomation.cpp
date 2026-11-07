#include "P4ActorAssetValuePlanning.h"
#include "P4MaterialReferenceContext.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "P4TypedMaterialValueFixture.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TypedMaterialValueTest, "P4MapKit.Export.TypedMaterialValuePlanning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TypedMaterialValueTest::RunTest(const FString&)
{
    TStrongObjectPtr<UPackage> Package(CreatePackage(
        *(TEXT("/Game/Mods/TypedMaterialValue_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/M_Test"))));
    TStrongObjectPtr<UMaterial> Graph(NewObject<UMaterial>(Package.Get(), TEXT("M_Test"), RF_Transient));
    TStrongObjectPtr<UMaterialExpressionScalarParameter> Node(
        NewObject<UMaterialExpressionScalarParameter>(Graph.Get()));
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(NewObject<UMaterialInstanceConstant>(GetTransientPackage()));
    const FString Target = Graph->GetPathName(), Runtime = Package->GetName() + TEXT("_Runtime.M_Test");
    Node->ParameterName = FName(*Target);
    Node->Desc = Target;
    Instance->Parent = Graph.Get();
    const FNameProperty* Name = FindFProperty<FNameProperty>(Node->GetClass(), TEXT("ParameterName"));
    const FStrProperty* String = FindFProperty<FStrProperty>(Node->GetClass(), TEXT("Desc"));
    const FObjectPropertyBase* Parent = FindFProperty<FObjectPropertyBase>(Instance->GetClass(), TEXT("Parent"));
    if (!TestNotNull(TEXT("Actual name property exists"), Name) ||
        !TestNotNull(TEXT("Actual string property exists"), String) ||
        !TestNotNull(TEXT("Actual material interface property exists"), Parent))
        return false;
    FP4ActorValues Values;
    TArray<FString> Packages;
    TArray<FP4ActorAssetReference> Typed;
    FString Error;
    TestTrue(TEXT("Name field containing graph path planned exactly"),
        P4PlanActorValue(Name, Name->ContainerPtrToValuePtr<void>(Node.Get()), TEXT("Name"), TEXT("Actor"),
            GetTransientPackage(), Node.Get(), Values, Packages, Typed, Error));
    TestTrue(TEXT("String field containing graph path planned exactly"),
        P4PlanActorValue(String, String->ContainerPtrToValuePtr<void>(Node.Get()), TEXT("String"), TEXT("Actor"),
            GetTransientPackage(), Node.Get(), Values, Packages, Typed, Error));
    TestEqual(TEXT("Ordinary text has no invented asset provenance"), Typed.Num(), 0);
    TestTrue(TEXT("Genuine material interface asset value planned"),
        P4PlanActorValue(Parent, Parent->ContainerPtrToValuePtr<void>(Instance.Get()), TEXT("Material"), TEXT("Actor"),
            GetTransientPackage(), Instance.Get(), Values, Packages, Typed, Error));
    TestTrue(TEXT("Repeated before/after line deduplicates native provenance"),
        P4PlanActorValue(Parent, Parent->ContainerPtrToValuePtr<void>(Instance.Get()), TEXT("Material"), TEXT("Actor"),
            GetTransientPackage(), Instance.Get(), Values, Packages, Typed, Error));
    TestEqual(TEXT("Only the actual asset slot is recorded"), Typed.Num(), 1);
    if (Typed.Num() == 1)
        TestTrue(TEXT("Supported reference points to generated runtime asset"),
            P4RemapActorAssetValue(Values, Typed[0], Runtime, Error));
    TestEqual(TEXT("Name path text stays unchanged"), FString(UTF8_TO_TCHAR(Values["Name"].Text.c_str())), Target);
    TestEqual(TEXT("String path text stays unchanged"), FString(UTF8_TO_TCHAR(Values["String"].Text.c_str())), Target);
    TestEqual(
        TEXT("Material path alone is rewritten"), FString(UTF8_TO_TCHAR(Values["Material"].Text.c_str())), Runtime);
    TStrongObjectPtr<UP4TypedMaterialValueFixture> Negative(
        NewObject<UP4TypedMaterialValueFixture>(GetTransientPackage()));
    Negative->ConcreteMaterial = Graph.Get();
    Negative->BroadSoft = Graph.Get();
    const FObjectPropertyBase* Concrete = FindFProperty<FObjectPropertyBase>(
        Negative->GetClass(), TEXT("ConcreteMaterial"));
    const FSoftObjectProperty* Broad = FindFProperty<FSoftObjectProperty>(Negative->GetClass(), TEXT("BroadSoft"));
    if (!TestNotNull(TEXT("Actual concrete material property exists"), Concrete) ||
        !TestNotNull(TEXT("Actual broad soft property exists"), Broad))
        return false;
    TestFalse(TEXT("Concrete hard material property refuses a generated MIC"),
        P4PlanActorValue(Concrete, Concrete->ContainerPtrToValuePtr<void>(Negative.Get()), TEXT("Concrete"),
            TEXT("Actor"), GetTransientPackage(), Negative.Get(), Values, Packages, Typed, Error));
    FP4ActorValues SoftValues;
    TArray<FP4ActorAssetReference> SoftTyped;
    TestTrue(TEXT("Soft asset target retains its actual broad slot type"),
        P4PlanActorValue(Broad, Broad->ContainerPtrToValuePtr<void>(Negative.Get()), TEXT("BroadSoft"), TEXT("Actor"),
            GetTransientPackage(), Negative.Get(), SoftValues, Packages, SoftTyped, Error));
    if (SoftTyped.Num() == 1)
    {
        TestFalse(TEXT("Broad soft graph slot cannot be remapped as a material interface"),
            P4RemapActorAssetValue(SoftValues, SoftTyped[0], Runtime, Error));
        TestEqual(TEXT("Refused soft reference remains exact"),
            FString(UTF8_TO_TCHAR(SoftValues["BroadSoft"].Text.c_str())), Target);
        TestTrue(TEXT("An unrelated soft asset is unaffected"),
            P4RemapActorAssetValue(SoftValues, SoftTyped[0], Target, Error));
    }
    else
        AddError(TEXT("Soft native provenance was not retained."));
    // Existing-game value edits use the same real native property grammar.
    // Renderer owner provenance applies to graphs, descendants and mesh defaults.
    const TSet<UMaterialInterface*> Graphs = {Graph.Get()};
    TStrongObjectPtr<UPackage> ChildPackage(CreatePackage(*(Package->GetName() + TEXT("_Child"))));
    TStrongObjectPtr<UMaterialInstanceConstant> Child(
        NewObject<UMaterialInstanceConstant>(ChildPackage.Get(), TEXT("MI_Child"), RF_Transient));
    Child->Parent = Graph.Get();
    TStrongObjectPtr<UDecalComponent> Decal(NewObject<UDecalComponent>(GetTransientPackage()));
    const FObjectPropertyBase* DecalField = FindFProperty<FObjectPropertyBase>(
        Decal->GetClass(), TEXT("DecalMaterial"));
    if (!TestNotNull(TEXT("Actual existing decal material field exists"), DecalField))
        return false;
    for (UMaterialInterface* Material :
        {static_cast<UMaterialInterface*>(Graph.Get()), static_cast<UMaterialInterface*>(Child.Get())})
    {
        Decal->SetDecalMaterial(Material);
        FP4ActorValues Planned;
        TArray<FP4ActorAssetReference> PlannedRefs;
        if (!TestTrue(TEXT("Actual decal reference retains typed native owner provenance"),
                P4PlanActorValue(DecalField, DecalField->ContainerPtrToValuePtr<void>(Decal.Get()),
                    TEXT("Decal.DecalMaterial"), TEXT("ExistingActor"), GetTransientPackage(), Decal.Get(), Planned,
                    Packages, PlannedRefs, Error)))
            return false;
        bool Uses = false;
        TestTrue(TEXT("Actual graph/child ancestry is inspectable"),
            P4MaterialReferenceUsesGraphs(Material, Graphs, Uses, Error));
        TestTrue(TEXT("Graph and child both carry the new shader"), Uses);
        if (PlannedRefs.Num() == 1)
        {
            TestFalse(TEXT("Existing decal set cannot attach a new static shader"),
                P4ValidateActorShaderContext(PlannedRefs[0], Uses, Error));
            TestTrue(TEXT("Unrelated ordinary decal material edits stay admitted"),
                P4ValidateActorShaderContext(PlannedRefs[0], false, Error));
        }
        else
            AddError(TEXT("Actual decal native reference was not retained."));
        TestTrue(TEXT("Refusal preserves actual existing decal value"), Decal->GetDecalMaterial() == Material);
    }
    TStrongObjectPtr<UPackage> MeshPackage(CreatePackage(*(Package->GetName() + TEXT("_Mesh"))));
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(MeshPackage.Get(), TEXT("SM_Test"), RF_Transient));
    Mesh->StaticMaterials.Add(FStaticMaterial(Child.Get()));
    TStrongObjectPtr<UStaticMeshComponent> Static(NewObject<UStaticMeshComponent>(GetTransientPackage()));
    TStrongObjectPtr<UInstancedStaticMeshComponent> Instanced(
        NewObject<UInstancedStaticMeshComponent>(GetTransientPackage()));
    for (UStaticMeshComponent* Component : {Static.Get(), static_cast<UStaticMeshComponent*>(Instanced.Get())})
    {
        Component->SetStaticMesh(Mesh.Get());
        const FObjectPropertyBase* MeshField = FindFProperty<FObjectPropertyBase>(
            Component->GetClass(), TEXT("StaticMesh"));
        if (!TestNotNull(TEXT("Actual static mesh set property exists"), MeshField))
            return false;
        FP4ActorValues Planned;
        TArray<FP4ActorAssetReference> PlannedRefs;
        if (!TestTrue(TEXT("Existing mesh set uses the supported native object-value grammar"),
                P4PlanActorValue(MeshField, MeshField->ContainerPtrToValuePtr<void>(Component), TEXT("Mesh.StaticMesh"),
                    TEXT("ExistingActor"), GetTransientPackage(), Component, Planned, Packages, PlannedRefs, Error)))
            return false;
        bool Uses = false;
        TestTrue(TEXT("Mesh default child shader ancestry is inspectable"),
            P4MaterialReferenceUsesGraphs(Mesh.Get(), Graphs, Uses, Error));
        TestTrue(TEXT("Default slots carry the new shader without component overrides"),
            Uses && Component->OverrideMaterials.Num() == 0);
        if (PlannedRefs.Num() == 1)
        {
            if (Component == Static.Get())
                TestTrue(TEXT("Plain static renderer admits existing mesh set"),
                    P4ValidateActorShaderContext(PlannedRefs[0], Uses, Error));
            else
                TestFalse(TEXT("Instanced renderer refuses default-only shader through existing mesh set"),
                    P4ValidateActorShaderContext(PlannedRefs[0], Uses, Error));
            TestTrue(TEXT("Ordinary unrelated mesh sets remain admitted"),
                P4ValidateActorShaderContext(PlannedRefs[0], false, Error));
        }
        else
            AddError(TEXT("Actual mesh native reference was not retained."));
    }
    return !HasAnyErrors();
}
#endif

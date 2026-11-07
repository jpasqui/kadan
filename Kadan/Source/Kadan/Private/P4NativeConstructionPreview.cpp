#include "P4NativeConstructionPreview.h"
#include "P4Common.h"
#include "P4MaterialGraph.h"
#include "P4OriginalBuiltLighting.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "StaticParameterSet.h"
#include "UObject/GCObject.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
// A decal component the class's construction gives a dynamic instance of a material the actor holds, with the
// instance's height parameter set to a component's world height.
struct FDecalStep
{
    FString Class, Decal, Material, HeightParameter, HeightComponent;
};

const TArray<FDecalStep>& Steps()
{
    static TArray<FDecalStep> Read;
    static bool bRead = false;
    if (bRead)
        return Read;
    bRead = true;
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Kadan"));
    const FString File = Plugin ? Plugin->GetBaseDir() / TEXT("Config/DefaultKadan.ini") : FString();
    if (File.IsEmpty() || !IFileManager::Get().FileExists(*File))
        return Read;
    FConfigFile Config;
    // Combine, not Read: only Combine applies the record's + entries.
    Config.Combine(File);
    TArray<FString> Lines;
    Config.GetArray(TEXT("NativeConstruction"), TEXT("DecalMaterial"), Lines);
    for (const FString& Line : Lines)
    {
        FDecalStep Step;
        FParse::Value(*Line, TEXT("Class="), Step.Class);
        FParse::Value(*Line, TEXT("Decal="), Step.Decal);
        FParse::Value(*Line, TEXT("Material="), Step.Material);
        FParse::Value(*Line, TEXT("HeightParameter="), Step.HeightParameter);
        FParse::Value(*Line, TEXT("HeightComponent="), Step.HeightComponent);
        if (Step.Class.IsEmpty() || Step.Decal.IsEmpty() || Step.Material.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("Kadan's native construction record has an incomplete step: %s"), *Line);
            continue;
        }
        Read.Add(Step);
    }
    return Read;
}

// The game's base pass draws a water box decal (mode 2 of its decal mask, KadanSwitchSourcePatches.inl, "the game's
// decals") as a wet look: the surface's base color B goes to B + (B * B - B) * Blend * Wet * 0.75, Blend being
// saturate(Roughness * 10) less its metallic part, then on a surface facing up by N.z to that times
// 1 + N.z * (10 * Caustics - 1). The decal's base color carries Wet in red and Caustics in green, and the game writes
// the mask only where red is not 0. A stock color decal cannot read the surface's own color or roughness, so the
// preview takes B as 0.4 and Blend as 1, and leaves roughness and specular as they are: the surface's color is scaled
// by M = (1 - 0.45 * Wet) * (1 + N.z * (10 * Caustics - 1)), with N from the surface's depth: the decal's opacity is
// 1 - M over black. Where the caustics would brighten the surface (M above 1) it is left as it is, since a stock decal
// can only add a color of its own, not more of the surface's.
// M, or -1 where the decal writes nothing; Tail returns the output from it.
FString WetCode(const TCHAR* Tail)
{
    return FString(TEXT("float M = -1;\n"
                        "if (Mask.r != 0)\n"
                        "{\n"
                        "    float3 P = GetWorldPosition(Parameters);\n"
                        "    float3 N = normalize(cross(ddx(P), ddy(P)));\n"
                        "    M = (1 - 0.45 * saturate(Mask.r)) * (1 + saturate(abs(N.z)) * (10 * Mask.g - 1));\n"
                        "}\n")) +
        Tail;
}

TSharedRef<FJsonObject> CustomNode(const FString& Code, const TCHAR* OutputType, const TSharedPtr<FJsonValue>& Mask)
{
    auto Node = MakeShared<FJsonObject>();
    Node->SetStringField(TEXT("type"), TEXT("Custom"));
    auto Inputs = MakeShared<FJsonObject>();
    Inputs->SetField(TEXT("Mask"), Mask);
    Node->SetObjectField(TEXT("inputs"), Inputs);
    auto Props = MakeShared<FJsonObject>();
    Props->SetStringField(TEXT("Code"), Code);
    Props->SetStringField(TEXT("OutputType"), OutputType);
    auto Name = MakeShared<FJsonObject>();
    Name->SetStringField(TEXT("InputName"), TEXT("Mask"));
    Props->SetArrayField(TEXT("Inputs"), {MakeShared<FJsonValueObject>(Name)});
    Props->SetStringField(TEXT("Description"), TEXT("Kadan preview of the game's water box decal"));
    Node->SetObjectField(TEXT("props"), Props);
    return Node;
}

TMap<TWeakObjectPtr<UMaterial>, TStrongObjectPtr<UMaterial>>& Previews()
{
    static TMap<TWeakObjectPtr<UMaterial>, TStrongObjectPtr<UMaterial>> Held;
    return Held;
}

// The material the decal draws in the editor: the source with its master replaced by the preview copy, as a
// transient instance holding the source's own parameter values.
UMaterialInterface* PreviewOf(UMaterialInterface* Source, FString& Error)
{
    UMaterial* Master = Source ? Source->GetMaterial() : nullptr;
    UMaterial* Preview = Master ? P4WaterBoxDecalPreview(Master, Error) : nullptr;
    if (!Preview || Preview == Master)
        return Preview ? Source : nullptr;
    auto* Instance = Cast<UMaterialInstanceConstant>(Source);
    if (!Instance)
        return Preview;
    auto* Copy = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
    Copy->SetParentEditorOnly(Preview);
    Copy->ScalarParameterValues = Instance->ScalarParameterValues;
    Copy->VectorParameterValues = Instance->VectorParameterValues;
    Copy->TextureParameterValues = Instance->TextureParameterValues;
    FStaticParameterSet Static;
    Instance->GetStaticParameterValues(Static);
    Copy->UpdateStaticPermutation(Static);
    Copy->PostEditChange();
    return Copy;
}
} // namespace

UMaterial* P4WaterBoxDecalPreview(UMaterial* Master, FString& Error)
{
    if (!Master || Master->MaterialDomain != MD_DeferredDecal)
        return Master;
    if (const TStrongObjectPtr<UMaterial>* Held = Previews().Find(Master))
        return Held->Get();
    const FString Source = P4MaterialGraphJson(Master, Error);
    TSharedPtr<FJsonObject> Graph;
    const TSharedPtr<FJsonObject>* Render = nullptr;
    const TSharedPtr<FJsonObject>* Outputs = nullptr;
    const TSharedPtr<FJsonObject>* Nodes = nullptr;
    FString Mode;
    if (Source.IsEmpty() || !P4ParseJson(Source, Graph) || !Graph->TryGetObjectField(TEXT("render_settings"), Render) ||
        !(*Render)->TryGetStringField(TEXT("DecalBlendMode"), Mode) || Mode != TEXT("DBM_DBuffer_C4_WaterBox"))
        return Master;
    if (!Graph->TryGetObjectField(TEXT("outputs"), Outputs) || !Graph->TryGetObjectField(TEXT("nodes"), Nodes) ||
        !(*Outputs)->HasField(TEXT("BaseColor")))
    {
        Error = TEXT("the water box decal master has no base color to preview");
        return nullptr;
    }
    const TSharedPtr<FJsonValue> Mask = (*Outputs)->TryGetField(TEXT("BaseColor"));
    (*Nodes)->SetObjectField(TEXT("Custom_9000"),
        CustomNode(TEXT("return float3(0, 0, 0);\n"), TEXT("CMOT_Float3"), Mask));
    (*Nodes)->SetObjectField(TEXT("Custom_9001"),
        CustomNode(WetCode(TEXT("return M < 0 ? 0 : saturate(1 - M);\n")), TEXT("CMOT_Float1"), Mask));
    (*Outputs)->SetField(TEXT("BaseColor"),
        MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueString>(TEXT("Custom_9000")), MakeShared<FJsonValueString>(TEXT(""))}));
    (*Outputs)->SetField(TEXT("Opacity"),
        MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueString>(TEXT("Custom_9001")), MakeShared<FJsonValueString>(TEXT(""))}));
    const FString Temporary = TEXT("/Game/__KadanWaterBoxDecalPreview/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UMaterial* Built = P4BuildMaterialFromGraph(P4CondensedJson(Graph.ToSharedRef()), Temporary, Error);
    // Only the material is kept. The package it was built in is not project content.
    if (UPackage* Package = FindPackage(nullptr, *Temporary))
    {
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    }
    if (!Built)
        return nullptr;
    Built->ClearFlags(RF_Public | RF_Standalone);
    Built->SetFlags(RF_Transient);
    Built->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
    Previews().Add(Master, TStrongObjectPtr<UMaterial>(Built));
    return Built;
}

namespace
{
// A saved value a preview changed, with the other of its two values: the map's own while the preview shows, the
// preview's while previews are suspended.
struct FRemembered
{
    TWeakObjectPtr<UObject> Object;
    FProperty* Property = nullptr;
    TArray<uint8> Other;
    // For a value a property copy cannot hold: swaps the two values itself.
    FName Key;
    TFunction<void()> Exchange;
};
TArray<FRemembered>& RememberedValues()
{
    static TArray<FRemembered> Values;
    return Values;
}

void AddValueReferences(FReferenceCollector& Collector, FProperty* Property, void* Value)
{
    if (CastField<FObjectProperty>(Property))
        Collector.AddReferencedObject(*static_cast<UObject**>(Value));
    else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Items(Array, Value);
        for (int32 Index = 0; Index < Items.Num(); ++Index)
            AddValueReferences(Collector, Array->Inner, Items.GetRawPtr(Index));
    }
    else if (FStructProperty* Struct = CastField<FStructProperty>(Property))
        for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member)
            for (int32 Index = 0; Index < Member->ArrayDim; ++Index)
                AddValueReferences(Collector, *Member, Member->ContainerPtrToValuePtr<void>(Value, Index));
}

// The other value is held as bytes, out of the garbage collector's sight. A save collects garbage while previews
// are suspended, when the other value is the preview's: a preview material that nothing else holds would be freed
// there and then swapped back into the map after the save.
class FRememberedReferences final : public FGCObject
{
public:
    void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        // The thread-safe test only asks whether the object still exists: during reachability analysis, objects
        // not yet reached are marked unreachable.
        for (FRemembered& Value : RememberedValues())
            if (Value.Object.IsValid(true, true) && Value.Property && Value.Other.Num() == Value.Property->GetSize())
                for (int32 Index = 0; Index < Value.Property->ArrayDim; ++Index)
                    AddValueReferences(
                        Collector, Value.Property, Value.Other.GetData() + Index * Value.Property->ElementSize);
    }
    FString GetReferencerName() const override
    {
        return TEXT("Kadan preview values");
    }
};

TArray<FRemembered>& Remembered()
{
    // Never destroyed: it may outlive the garbage collector at exit.
    static FRememberedReferences* References = new FRememberedReferences();
    (void)References;
    return RememberedValues();
}
int32 SuspendDepth = 0;

void Swap(FRemembered& Value)
{
    UObject* Object = Value.Object.Get();
    if (Object && Value.Exchange)
    {
        Value.Exchange();
        return;
    }
    if (!Object || !Value.Property)
        return;
    void* At = Value.Property->ContainerPtrToValuePtr<void>(Object);
    TArray<uint8> Held;
    Held.SetNumZeroed(Value.Property->GetSize());
    Value.Property->InitializeValue(Held.GetData());
    Value.Property->CopyCompleteValue(Held.GetData(), At);
    Value.Property->CopyCompleteValue(At, Value.Other.GetData());
    Value.Property->CopyCompleteValue(Value.Other.GetData(), Held.GetData());
    Value.Property->DestroyValue(Held.GetData());
    if (auto* Scene = Cast<USceneComponent>(Object))
        Scene->UpdateComponentToWorld();
    if (auto* Component = Cast<UActorComponent>(Object))
        Component->MarkRenderStateDirty();
}

void SwapAll()
{
    TMap<UPackage*, bool> WasDirty;
    for (const FRemembered& Value : Remembered())
        if (UObject* Object = Value.Object.Get())
            WasDirty.FindOrAdd(Object->GetOutermost(), Object->GetOutermost()->IsDirty());
    for (FRemembered& Value : Remembered())
        Swap(Value);
    for (const auto& Package : WasDirty)
        Package.Key->SetDirtyFlag(Package.Value);
}
} // namespace

void P4RememberNativePreviewValue(UObject* Object, FProperty* Property)
{
    if (!Object || !Property || Remembered().ContainsByPredicate([&](const FRemembered& Value)
            { return Value.Object.Get() == Object && Value.Property == Property; }))
        return;
    FRemembered Value;
    Value.Object = Object;
    Value.Property = Property;
    Value.Other.SetNumZeroed(Property->GetSize());
    Property->InitializeValue(Value.Other.GetData());
    Property->CopyCompleteValue(Value.Other.GetData(), Property->ContainerPtrToValuePtr<void>(Object));
    Remembered().Add(MoveTemp(Value));
}

void P4RememberNativePreviewExchange(UObject* Object, FName Key, TFunction<void()> Exchange)
{
    if (!Object || !Exchange || Remembered().ContainsByPredicate([&](const FRemembered& Value)
            { return Value.Object.Get() == Object && Value.Exchange && Value.Key == Key; }))
        return;
    FRemembered Value;
    Value.Object = Object;
    Value.Key = Key;
    Value.Exchange = MoveTemp(Exchange);
    Remembered().Add(MoveTemp(Value));
}

FP4SuspendNativePreviews::FP4SuspendNativePreviews()
{
    if (SuspendDepth++ == 0)
        SwapAll();
}

FP4SuspendNativePreviews::~FP4SuspendNativePreviews()
{
    if (--SuspendDepth == 0)
        SwapAll();
}

void P4SetNativePreviewsSuspended(bool Suspended)
{
    if (Suspended ? SuspendDepth++ == 0 : --SuspendDepth == 0)
        SwapAll();
}

int32 P4ApplyNativeConstructionPreviews(UWorld* World)
{
    if (!World || SuspendDepth)
        return 0;
#if P4_KADAN_TEST_SEAMS
    // Tests compare a map with and without the previews.
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PREVIEWS_OFF")) == TEXT("1"))
        return 0;
#endif
    // A preview is not an edit: every level keeps whether it had unsaved changes.
    TMap<UPackage*, bool> WasDirty;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            WasDirty.Add(Level->GetOutermost(), Level->GetOutermost()->IsDirty());
    ON_SCOPE_EXIT
    {
        for (const auto& Package : WasDirty)
            Package.Key->SetDirtyFlag(Package.Value);
    };
    int32 Applied = 0;
    for (const FDecalStep& Step : Steps())
    {
        UClass* Class = FindObject<UClass>(nullptr, *Step.Class);
        if (!Class)
            continue;
        for (ULevel* Level : World->GetLevels())
            if (Level)
                for (AActor* Actor : Level->Actors)
                {
                    if (!IsValid(Actor) || !Actor->IsA(Class))
                        continue;
                    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), *Step.Material);
                    UMaterialInterface* Source = Property
                        ? Cast<UMaterialInterface>(Property->GetObjectPropertyValue_InContainer(Actor))
                        : nullptr;
                    TInlineComponentArray<UDecalComponent*> Decals(Actor);
                    UDecalComponent* const* Decal =
                        Decals.FindByPredicate([&](UDecalComponent* Part) { return Part->GetName() == Step.Decal; });
                    // A decal that already has a material keeps it: it is the map's, or this step's.
                    if (!Source || !Decal || (*Decal)->GetDecalMaterial())
                        continue;
                    FString Error;
                    UMaterialInterface* Drawn = PreviewOf(Source, Error);
                    if (!Drawn)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Kadan cannot preview %s on %s: %s"), *Source->GetPathName(),
                            *Actor->GetPathName(), *Error);
                        continue;
                    }
                    UMaterialInstanceDynamic* Instance =
                        UMaterialInstanceDynamic::Create(Drawn, GetTransientPackage());
                    Instance->SetFlags(RF_Transient);
                    TInlineComponentArray<USceneComponent*> Parts(Actor);
                    USceneComponent* const* Height = Parts.FindByPredicate(
                        [&](USceneComponent* Part) { return Part->GetName() == Step.HeightComponent; });
                    if (!Step.HeightParameter.IsEmpty() && Height)
                        Instance->SetScalarParameterValue(FName(*Step.HeightParameter), (*Height)->GetComponentLocation().Z);
                    P4RememberNativePreviewValue(*Decal, FindFProperty<FProperty>(UDecalComponent::StaticClass(), TEXT("DecalMaterial")));
                    (*Decal)->SetDecalMaterial(Instance);
                    ++Applied;
                }
    }
    return Applied + P4ApplyOriginalBuiltLightingPreviews(World) + P4ApplyNativeEnvironmentPreviews(World);
}

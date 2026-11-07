#include "P4KadanEditor.h"
#include "P4KadanReadOnly.h"
#include "Widgets/SNullWidget.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Serialization/ObjectWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TMap<UObject*, TArray<uint8>> Snapshot(UPackage* Package)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    TMap<UObject*, TArray<uint8>> Out;
    for (UObject* Object : Objects)
    {
        TArray<uint8>& Bytes = Out.Add(Object);
        FObjectWriter Writer(Object, Bytes, false, false, false); // Full values, including defaults.
    }
    return Out;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyRouteTest, "P4MapKit.Kadan.ReadOnlyRoute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanReadOnlyRouteTest::RunTest(const FString&)
{
    if (IsRunningCommandlet() || !GEditor || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
    {
        AddError(TEXT("ReadOnlyRoute requires an interactive Slate editor. A commandlet cannot validate this route."));
        return false;
    }
    P4RegisterKadanEditor();
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!Editors)
    {
        AddError(TEXT("Asset editor subsystem is unavailable."));
        return false;
    }

    struct FKind
    {
        const TCHAR* Name;
        UClass* Parent;
        UClass* Blueprint;
        UClass* Generated;
    };
    const FKind Kinds[] = {
        {TEXT("Blueprint"), AActor::StaticClass(), UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()},
        {TEXT("WidgetBlueprint"), UUserWidget::StaticClass(), UWidgetBlueprint::StaticClass(),
            UWidgetBlueprintGeneratedClass::StaticClass()}};
    for (const FKind& Kind : Kinds)
    {
        const FString Prefix = FString(Kind.Name) + TEXT(": ");
        UPackage* Package = CreatePackage(
            *(TEXT("/Game/__P4KadanReadOnlyTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        TStrongObjectPtr<UBlueprint> Asset(FKismetEditorUtilities::CreateBlueprint(
            Kind.Parent, Package, FName(Kind.Name), BPTYPE_Normal, Kind.Blueprint, Kind.Generated));
        if (!Asset.IsValid())
        {
            AddError(Prefix + TEXT("could not create test Blueprint."));
            continue;
        }
        struct FCleanup
        {
            UAssetEditorSubsystem* Editors;
            UBlueprint* Asset;
            UPackage* Package;
            ~FCleanup()
            {
                Package->SetDirtyFlag(false);
                Editors->CloseAllEditorsForAsset(Asset);
                TArray<UObject*> Objects;
                GetObjectsWithOuter(Package, Objects, true);
                for (UObject* Object : Objects)
                {
                    Object->ClearFlags(RF_Public | RF_Standalone);
                    Object->SetFlags(RF_Transient);
                }
                Package->SetFlags(RF_Transient);
            }
        } Cleanup{Editors, Asset.Get(), Package};

        // Establish the stock editor route before applying the protected marker.
        TestTrue(Prefix + TEXT("creator opens through asset subsystem"),
            Editors->OpenEditorForAsset(Asset.Get(), EToolkitMode::Standalone, nullptr, false));
        IAssetEditorInstance* Stock = Editors->FindEditorForAsset(Asset.Get(), false);
        const FName StockName = Stock ? Stock->GetEditorName() : NAME_None;
        TestTrue(Prefix + TEXT("creator uses a stock editor"), Stock && StockName != FName(TEXT("KadanReference")));
        Package->SetDirtyFlag(false);
        Editors->CloseAllEditorsForAsset(Asset.Get());

        Package->GetMetaData()->SetValue(Asset.Get(), TEXT("P4BlueprintStandIn"), TEXT("1"));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
        const bool WasDirty = Package->IsDirty();
        const uint32 PackageFlags = Package->GetPackageFlags();
        TArray<UEdGraph*> Graphs;
        Asset->GetAllGraphs(Graphs);
        TestTrue(Prefix + TEXT("test Blueprint contains a graph"), Graphs.Num() > 0);
        TMap<UEdGraph*, bool> Editable;
        for (UEdGraph* Graph : Graphs)
            if (Graph)
                Editable.Add(Graph, Graph->bEditable);
        const auto Before = Snapshot(Package);
        const auto Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
                                 .Get()
                                 .GetAssetTypeActionsForClass(Asset->GetClass())
                                 .Pin();
        TestTrue(Prefix + TEXT("asset browser has routed actions"), Actions.IsValid());
        if (Actions)
            TestTrue(Prefix + TEXT("protected thumbnail has a lock overlay"),
                P4Kadan::LockMark(FAssetData(Asset.Get())) != SNullWidget::NullWidget);
        TestFalse(Prefix + TEXT("stock edit route is locked"),
            Editors->OpenEditorForAsset(Asset.Get(), EToolkitMode::Standalone, nullptr, false));
        if (Actions)
            Actions->OpenAssetEditor({Asset.Get()});
        IAssetEditorInstance* Reference = Editors->FindEditorForAsset(Asset.Get(), false);
        const bool Routed = Reference && Reference->GetEditorName() == FName(TEXT("KadanReference"));
        TestTrue(Prefix + TEXT("protected asset uses KadanReference"), Routed);
        if (Routed)
        {
            auto* Toolkit = static_cast<FAssetEditorToolkit*>(Reference);
            const auto Save = FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), TEXT("SaveAsset"));
            TestTrue(Prefix + TEXT("stock Save command exists"), Save.IsValid());
            if (Save)
                TestFalse(Prefix + TEXT("reference toolkit cannot execute Save"),
                    Toolkit->GetToolkitCommands()->CanExecuteAction(Save.ToSharedRef()));
        }
        const auto After = Snapshot(Package);
        TestEqual(Prefix + TEXT("opening adds no package objects"), After.Num(), Before.Num());
        for (const auto& Entry : Before)
        {
            const TArray<uint8>* Bytes = After.Find(Entry.Key);
            TestTrue(
                Prefix + TEXT("serialized object unchanged: ") + Entry.Key->GetName(), Bytes && *Bytes == Entry.Value);
        }
        for (const auto& Entry : Editable)
            TestEqual(Prefix + TEXT("graph editable flag unchanged: ") + Entry.Key->GetName(),
                bool(Entry.Key->bEditable), Entry.Value);
        TestEqual(Prefix + TEXT("package dirty flag unchanged"), Package->IsDirty(), WasDirty);
        TestEqual(Prefix + TEXT("package flags unchanged"), uint32(Package->GetPackageFlags()), PackageFlags);
        Editors->CloseAllEditorsForAsset(Asset.Get());

        Package->GetMetaData()->RemoveValue(Asset.Get(), FName(TEXT("P4BlueprintStandIn")));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
        TestTrue(Prefix + TEXT("creator reopens"),
            Editors->OpenEditorForAsset(Asset.Get(), EToolkitMode::Standalone, nullptr, false));
        IAssetEditorInstance* Creator = Editors->FindEditorForAsset(Asset.Get(), false);
        TestTrue(
            Prefix + TEXT("creator retains its original editor"), Creator && Creator->GetEditorName() == StockName);
        if (Creator && Creator->GetEditorName() == StockName && StockName != NAME_None)
        {
            Package->SetDirtyFlag(true);
            const auto Save = FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), TEXT("SaveAsset"));
            if (Save)
                TestTrue(Prefix + TEXT("creator retains executable Save"),
                    static_cast<FAssetEditorToolkit*>(Creator)->GetToolkitCommands()->CanExecuteAction(
                        Save.ToSharedRef()));
        }
        Package->SetDirtyFlag(false);
        Editors->CloseAllEditorsForAsset(Asset.Get());
    }
    return !HasAnyErrors();
}
#endif

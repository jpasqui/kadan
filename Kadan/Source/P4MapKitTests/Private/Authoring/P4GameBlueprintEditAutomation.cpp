#include "P4KadanAssetPolicy.h"
#include "P4KadanReadOnly.h"
#include "P4GameReplacement.h"
#include "P4OriginalRecord.h"
#include "P4ExportDriver.h"
#include "P4ExportReadbackContext.h"
#include "P4MapKitCore/package_reader.h"
#include "P4AuthoringRows.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include <algorithm>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameBlueprintFamilyPolicy, "P4MapKit.Kadan.GameBlueprintFamilyPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameBlueprintFamilyPolicy::RunTest(const FString&)
{
    using namespace P4Kadan;
    const FString Name = TEXT("BP_Family_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Game/__KadanFamily/") + Name));
    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(*Name),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Synthetic actor Blueprint"), BP))
        return false;
    FP4OriginalRecord Record;
    Record.Package = Package->GetName();
    Record.Class = BP->GetClass()->GetPathName();
    Record.Kind = TEXT("blueprint");
    Record.Fidelity = TEXT("recovered");
    Record.ReferenceOnly = true;
    FString Error;
    TestTrue(TEXT("Synthetic original record"), P4MarkOriginalAsset(BP, Record, Error));
    const FString Before = Package->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"));
    const bool Enabled = GameAssetShipping(EGameAssetFamily::ActorBlueprint) == EGameAssetShipping::WholePackage;
    TestTrue(TEXT("Correct actor family"), GameAssetFamily(BP) == EGameAssetFamily::ActorBlueprint);
    TestEqual(TEXT("Editor permission follows family row"), GetAssetPolicy(BP).AllowEdit, Enabled);
    TestEqual(TEXT("Generated-class editor follows family row"), GetAssetPolicy(BP->GeneratedClass).AllowEdit, Enabled);
    TestEqual(TEXT("Class-default editor follows family row"),
        GetAssetPolicy(BP->GeneratedClass->GetDefaultObject()).AllowEdit, Enabled);
    TestFalse(TEXT("Untouched dependency is not a replacement"), P4IsGameReplacement(BP));
    P4RecordGameBlueprintEdit(BP);
    FP4GameReplacement Replacement;
    TestEqual(TEXT("Edit-intent admission follows family row"), P4IsGameReplacement(BP, &Replacement), Enabled);
    if (Enabled)
    {
        TestEqual(TEXT("Generated class identity"), Replacement.GeneratedClass, BP->GeneratedClass->GetPathName());
        TestTrue(TEXT("No fictitious source file"), Replacement.File.IsEmpty());
        FP4ModDescriptor Mod;
        Mod.Id = TEXT("bp.family");
        Mod.Name = TEXT("Family");
        Mod.Version = TEXT("1.0.0");
        TestTrue(TEXT("Dedicated whole Blueprint route"),
            P4ExportAssetAvailability(BP, Mod).Route == EP4ExportAssetRoute::GameActorBlueprint);
    }
    TestEqual(TEXT("Recovery provenance remains intact"),
        FString(Package->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"))), Before);
    Package->GetMetaData()->SetValue(BP, TEXT("P4BlueprintStandIn"), TEXT("1"));
    Package->SetDirtyFlag(true);
    TestFalse(TEXT("Dirty stand-in remains locked"), GetAssetPolicy(BP).AllowEdit);
    TestFalse(TEXT("Dirty stand-in cannot become a replacement"), P4IsGameReplacement(BP));
    Package->GetMetaData()->RemoveValue(BP, TEXT("P4BlueprintStandIn"));
    Package->GetMetaData()->SetValue(BP, TEXT("P4BlueprintReconstructionIncomplete"), TEXT("1"));
    TestFalse(TEXT("Incomplete reconstruction stays locked"), GetAssetPolicy(BP).AllowEdit);
    TestFalse(TEXT("Incomplete reconstruction cannot ship"), P4IsGameReplacement(BP));
    Package->SetDirtyFlag(false);
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
    {
        Object->ClearFlags(RF_Public | RF_Standalone);
        Object->SetFlags(RF_Transient);
    }
    Package->SetFlags(RF_Transient);
    return true;
}

// Opt-in desktop fixture for an owned project with the actor family enabled.
// It never changes family policy. All edits use the real asset and normal panel exporter.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4EditGameActorBlueprint, "P4MapKit.Kadan.EditGameActorBlueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4EditGameActorBlueprint::RunTest(const FString&)
{
    const auto Env = [](const TCHAR* Key) { return FPlatformMisc::GetEnvironmentVariable(Key); };
    if (Env(TEXT("P4_BP_EDIT_PROOF")) != TEXT("1"))
    {
        AddError(TEXT("Use the owned Part B fixture script."));
        return false;
    }
    const FString Mode = Env(TEXT("P4_BP_EDIT_MODE")).IsEmpty() ? TEXT("all") : Env(TEXT("P4_BP_EDIT_MODE"));
    if (Mode != TEXT("none") && Mode != TEXT("value") && Mode != TEXT("component") && Mode != TEXT("graph") &&
        Mode != TEXT("all"))
    {
        AddError(TEXT("Unknown Blueprint control mode."));
        return false;
    }
    const bool EditComponent = Mode == TEXT("all") || Mode == TEXT("component");
    const bool EditGraph = Mode == TEXT("all") || Mode == TEXT("graph");
    const bool EditValue = Mode == TEXT("all") || Mode == TEXT("value");
    const FString Package = Env(TEXT("P4_BP_EDIT_PACKAGE"));
    if (Package != TEXT("/Game/Carrot4/Placeables/Gimmicks/CrackPot/GCrackPotS") &&
        Package != TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController"))
    {
        AddError(TEXT("This fixture edits only the two selected originals."));
        return false;
    }
    if (Env(TEXT("P4_BP_EDIT_IMPORT")) == TEXT("1"))
    {
        if (FPackageName::DoesPackageExist(Package))
        {
            AddError(
                TEXT("Fresh-import controls require an absent target package; existing edits are never reset here."));
            return false;
        }
        const FString Import = UP4AuthoringTools::NativeKadanImportMapFromContainers(Env(TEXT("P4_BP_EDIT_GAMEFILES")),
            Package, Env(TEXT("P4_BP_EDIT_PAKS")), Env(TEXT("P4_BP_EDIT_RETOC")), Env(TEXT("P4_BP_EDIT_REPAK")));
        FFileHelper::SaveStringToFile(Import, *(Env(TEXT("P4_BP_EDIT_REPORT")) + TEXT(".import.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
    if (!TestNotNull(TEXT("Imported original Blueprint"), BP) ||
        !TestTrue(TEXT("Recovered original"), P4Kadan::IsRecoveredGameActorBlueprint(BP)) ||
        !TestTrue(TEXT("Owned test build has enabled the family"), P4Kadan::GetAssetPolicy(BP).AllowEdit))
        return false;
    P4Kadan::UpdateReadOnlyAsset(BP);
    USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
    if (!TestNotNull(TEXT("Construction script"), SCS) || SCS->FindSCSNode(TEXT("KadanEditMarker")))
    {
        AddError(TEXT("Use fresh imported assets for this proof, not a previous edited fixture."));
        return false;
    }
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Game/Carrot4/Objects/Gimmicks/CrackPotS/SM_stg_CrackPotS_BR01.SM_stg_CrackPotS_BR01"));
    if (!TestNotNull(TEXT("Imported stock marker mesh"), Mesh))
        return false;
    // Blueprint-created roots live in SCS templates, not necessarily on the parent CDO.
    // Resolve the existing root before adding a scene node, so the marker cannot become it.
    USCS_Node* RootNode = nullptr;
    USceneComponent* Root = SCS->GetSceneRootComponentTemplate(false, &RootNode);
    if (!Root && SCS->GetRootNodes().Contains(SCS->GetDefaultSceneRootNode()))
    {
        RootNode = SCS->GetDefaultSceneRootNode();
        Root = RootNode ? Cast<USceneComponent>(RootNode->ComponentTemplate) : nullptr;
    }
    if (!TestNotNull(TEXT("Existing scene root template"), Root))
        return false;
    AddInfo(TEXT("Preserving scene root: ") + Root->GetPathName());
    // The no-edit control records export intent only; it changes no runtime default,
    // component or graph. Each other control changes exactly one runtime feature.
    BP->Modify();
    P4RecordGameBlueprintEdit(BP);
    if (EditComponent)
    {
        USCS_Node* Marker = SCS->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("KadanEditMarker"));
        UStaticMeshComponent* Template = CastChecked<UStaticMeshComponent>(Marker->ComponentTemplate);
        Template->SetStaticMesh(Mesh);
        Template->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Template->SetMobility(EComponentMobility::Movable);
        Template->SetRelativeLocation(FVector(0, 0, 200));
        Template->SetRelativeScale3D(FVector(3));
        Template->SetCastShadow(false);
        if (RootNode && RootNode->GetOuter() == SCS)
            RootNode->AddChildNode(Marker);
        else
        {
            SCS->AddNode(Marker);
            if (RootNode)
                Marker->SetParent(RootNode);
            else
            {
                if (!TestTrue(TEXT("Otherwise the existing root is native"),
                        Root->CreationMethod == EComponentCreationMethod::Native))
                    return false;
                Marker->SetParent(Root);
            }
        }
        USCS_Node* AfterRootNode = nullptr;
        USceneComponent* AfterRoot = SCS->GetSceneRootComponentTemplate(false, &AfterRootNode);
        if (!AfterRoot && AfterRootNode == nullptr && RootNode == SCS->GetDefaultSceneRootNode() &&
            SCS->GetRootNodes().Contains(RootNode))
        {
            AfterRoot = Cast<USceneComponent>(RootNode->ComponentTemplate);
            AfterRootNode = RootNode;
        }
        if (!TestTrue(TEXT("Added component preserves the actual scene root"),
                Root == AfterRoot && RootNode == AfterRootNode))
            return false;
    }
    if (EditGraph)
    {
        UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(BP);
        if (!Graph)
        {
            Graph = FBlueprintEditorUtils::CreateNewGraph(
                BP, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            FBlueprintEditorUtils::AddUbergraphPage(BP, Graph);
        }
        UK2Node_Event* Event = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (UK2Node_Event* Found = Cast<UK2Node_Event>(Node))
                if (Found->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
                    Event = Found;
        // UE4's inspected default graph has a disabled BeginPlay -> Parent pair, even when
        // the recovered recipe has no authored graphs. Preserve that exact pair; refuse other logic.
        UK2Node_CallParentFunction* Super = nullptr;
        if (Event && Event->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo.Num())
        {
            const auto& Links = Event->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo;
            if (Links.Num() == 1)
                Super = Cast<UK2Node_CallParentFunction>(Links[0]->GetOwningNode());
            if (!Event->IsAutomaticallyPlacedGhostNode() || !Super ||
                Links[0] != Super->FindPinChecked(UEdGraphSchema_K2::PN_Execute) ||
                Super->FunctionReference.GetMemberName() != TEXT("ReceiveBeginPlay") ||
                Super->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo.Num())
            {
                AddError(TEXT("Unexpected BeginPlay logic: inspect it before preparing this fixture."));
                return false;
            }
            AddInfo(TEXT("Preserving the inspected disabled default BeginPlay parent call."));
        }
        if (!Event)
        {
            Event = NewObject<UK2Node_Event>(Graph);
            Event->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
            Event->bOverrideFunction = true;
            Graph->AddNode(Event);
            Event->CreateNewGuid();
            Event->AllocateDefaultPins();
        }
        Event->SetEnabledState(ENodeEnabledState::Enabled, true);
        const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
        UEdGraphPin* Then = Event->FindPinChecked(UEdGraphSchema_K2::PN_Then);
        if (!Super)
        {
            Super = NewObject<UK2Node_CallParentFunction>(Graph);
            Super->SetFromFunction(BP->ParentClass->FindFunctionByName(TEXT("ReceiveBeginPlay")));
            Graph->AddNode(Super);
            Super->CreateNewGuid();
            Super->AllocateDefaultPins();
            Super->NodePosX = Event->NodePosX + 300;
            Super->NodePosY = Event->NodePosY;
            if (!Schema->TryCreateConnection(Then, Super->FindPinChecked(UEdGraphSchema_K2::PN_Execute)))
                return false;
        }
        Super->SetEnabledState(ENodeEnabledState::Enabled, true);
        Then = Super->FindPinChecked(UEdGraphSchema_K2::PN_Then);
        UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
        Call->SetFromFunction(AActor::StaticClass()->FindFunctionByName(TEXT("SetActorScale3D")));
        Graph->AddNode(Call);
        Call->CreateNewGuid();
        Call->AllocateDefaultPins();
        Call->NodePosX = Super->NodePosX + 400;
        Call->NodePosY = Super->NodePosY;
        Call->FindPinChecked(TEXT("NewScale3D"))->DefaultValue = TEXT("1.75,1.75,1.75");
        if (!TestTrue(TEXT("Saved graph connects BeginPlay to scale constant"),
                Schema->TryCreateConnection(Then, Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute))))
            return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (!TestTrue(TEXT("Edited graph compiles"), BP->Status != BS_Error && BP->GeneratedClass))
        return false;
    AActor* CDO = CastChecked<AActor>(BP->GeneratedClass->GetDefaultObject());
    CDO->Modify();
    if (EditValue)
        CDO->Tags.AddUnique(TEXT("KadanBPValue_1025"));
    BP->MarkPackageDirty();
    if (!TestTrue(TEXT("Whole edited original saves"),
            UEditorLoadingAndSavingUtils::SavePackages({BP->GetOutermost()}, false)))
        return false;
    const FString Id = Package.EndsWith(TEXT("GCrackPotS")) ? TEXT("bpedit.pot") : TEXT("bpedit.env");
    UP4AuthoringTools::NativeSaveMod(Id, TEXT("Blueprint proof"), TEXT("1.0.0"), true);
    const FString Report = UP4AuthoringTools::NativeExportMod(Id, Package, FString(), Env(TEXT("P4_BP_EDIT_PAKS")),
        Env(TEXT("P4_BP_EDIT_OUT")), Env(TEXT("P4_BP_EDIT_WORK")), Env(TEXT("P4_BP_EDIT_RETOC")),
        Env(TEXT("P4_BP_EDIT_REPAK")), Env(TEXT("P4_BP_EDIT_GAMEFILES")));
    FFileHelper::SaveStringToFile(
        Report, *Env(TEXT("P4_BP_EDIT_REPORT")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TSharedPtr<FJsonObject> Json;
    bool Published = false;
    const bool Read = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Report), Json) && Json.IsValid();
    if (!TestTrue(TEXT("Normal panel export publishes the whole game-path package"),
            Read && Json->TryGetBoolField(TEXT("published"), Published) && Published))
        return false;
    // Inspect only this mod, beside the original game's global script metadata. No original
    // asset container can fill a missing class/function/component during conversion.
    const FString Packed = Json->GetStringField(TEXT("directory")) / TEXT("romfs/tsuchi/mods") / Id;
    TArray<FString> Containers;
    IFileManager::Get().FindFiles(Containers, *(Packed / TEXT("*.utoc")), true, false);
    if (!TestEqual(TEXT("One packed mod container"), Containers.Num(), 1))
        return false;
    FP4ExportContainerTools Tools;
    Tools.Retoc = Env(TEXT("P4_BP_EDIT_RETOC"));
    Tools.Repak = Env(TEXT("P4_BP_EDIT_REPAK"));
    const FString Readback = Env(TEXT("P4_BP_EDIT_WORK")) / TEXT("bp-readback");
    const FString Relative = TEXT("Carrot4/Content/") + Package.Mid(6) + TEXT(".uasset");
    const FString Header = Readback / Relative;
    // Delete only this fixture's two exact prior outputs, so retoc failure cannot reuse stale bytes.
    IFileManager::Get().Delete(*Header, false, true);
    IFileManager::Get().Delete(*FPaths::ChangeExtension(Header, TEXT("uexp")), false, true);
    FString Error;
    if (!P4ExtractExportReadback(Tools, Packed / Containers[0], Env(TEXT("P4_BP_EDIT_PAKS")),
            Env(TEXT("P4_BP_EDIT_WORK")), Readback, TEXT("../../../") + Relative, Error))
    {
        AddError(Error);
        return false;
    }
    TArray<uint8> Bytes, Body;
    if (!TestTrue(TEXT("Packed Blueprint converts with original script imports"),
            FFileHelper::LoadFileToArray(Bytes, *Header) &&
                FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(Header, TEXT("uexp"))) && Body.Num() > 0))
        return false;
    const auto Inventory = P4MapKit::ReadPackageHeader(
        std::vector<uint8>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()), std::size_t(Body.Num()));
    if (!TestTrue(TEXT("Packed Blueprint header reads"), Inventory.Error.Code.empty()))
        return false;
    const std::string ClassName = TCHAR_TO_UTF8(*(FPackageName::GetShortName(Package) + TEXT("_C")));
    bool Class = false, Script = false, Component = false;
    for (const auto& Export : Inventory.Exports)
    {
        Class |= Export.Name == ClassName;
        Script |= Export.Name == "ReceiveBeginPlay" || Export.Name.find("ExecuteUbergraph") == 0;
        Component |= Export.Name == "KadanEditMarker_GEN_VARIABLE";
    }
    // FName stores the numeric suffix separately; the name map holds "KadanBPValue".
    // Decode the CDO's actual Tags value, rather than accepting a name-table occurrence.
    bool HasTag = false;
    const std::vector<uint8> PackedBody(Body.GetData(), Body.GetData() + Body.Num());
    for (const auto& Export : Inventory.Exports)
        if (Export.Name == "Default__" + ClassName)
        {
            const auto Properties = P4MapKit::ReadPropertyEnvelopes(
                PackedBody, Inventory.Names, Export.Offset, Export.Offset + Export.Size);
            if (!TestTrue(TEXT("Packed CDO properties read"), Properties.Error.Code.empty()))
                return false;
            for (const auto& Property : Properties.Properties)
                if (Property.Name == "Tags" && Property.Type == "ArrayProperty" &&
                    Property.Detail == std::vector<std::string>{"NameProperty"})
                {
                    P4MapKit::FByteReader Reader(
                        PackedBody, Property.PayloadOffset, Property.PayloadOffset + Property.Size);
                    std::size_t Count = 0;
                    bool Found = false;
                    if (!TestTrue(TEXT("Packed Tags array count reads"), Reader.Count(Count)))
                        return false;
                    for (std::size_t Index = 0; Index < Count; ++Index)
                    {
                        std::string Name;
                        if (!TestTrue(TEXT("Packed Tags FName reads"), Reader.Name(Inventory.Names, Name)))
                            return false;
                        Found |= Name == "KadanBPValue_1025";
                    }
                    HasTag = Found && Reader.Tell() == Reader.Limit();
                }
        }
    const bool HasCall = std::find(Inventory.Names.begin(), Inventory.Names.end(), "SetActorScale3D") !=
        Inventory.Names.end();
    bool Complete = TestTrue(TEXT("Packed generated class exists"), Class);
    if (EditGraph)
        Complete &= TestTrue(TEXT("Packed script export exists"), Script);
    Complete &= TestEqual(TEXT("Packed component matches the selected control"), Component, EditComponent);
    Complete &= TestEqual(TEXT("Packed value matches the selected control"), HasTag, EditValue);
    Complete &= TestEqual(TEXT("Packed graph call matches the selected control"), HasCall, EditGraph);
    Json->SetStringField(TEXT("blueprint_edit_mode"), Mode);
    const TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
    Checks->SetBoolField(TEXT("class"), Class);
    Checks->SetBoolField(TEXT("script"), Script);
    Checks->SetBoolField(TEXT("component"), Component);
    Checks->SetBoolField(TEXT("value"), HasTag);
    Checks->SetBoolField(TEXT("graph_call"), HasCall);
    Json->SetObjectField(TEXT("blueprint_readback_checks"), Checks);
    Json->SetBoolField(TEXT("blueprint_readback_ok"), Complete);
    Json->SetStringField(TEXT("blueprint_readback"), Header);
    FString FinalReport;
    FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&FinalReport));
    FFileHelper::SaveStringToFile(
        FinalReport, *Env(TEXT("P4_BP_EDIT_REPORT")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return Complete;
}
#endif

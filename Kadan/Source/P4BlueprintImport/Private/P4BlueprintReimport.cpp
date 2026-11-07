#include "P4BlueprintReimport.h"
#include "P4BlueprintImport.h"
#include "P4BlueprintAnimOwnership.h"
#include "P4OriginalImportSave.h"
#include "P4BlueprintWidgetMembers.h"
#include "AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "PackageTools.h"
#include "Editor.h"
#include "BlueprintEditorSettings.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "WidgetBlueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/GCObject.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

extern void P4RestorePendingStandIns(const TArray<FString>& Packages);

namespace
{
const TCHAR* ImportTag = TEXT("P4BlueprintImportVersion");
const TCHAR* ImportVersion = TEXT("2");
const TCHAR* SourceTag = TEXT("P4BlueprintSourceMembers");
struct FEntry
{
    UObject* Asset = nullptr;
    bool bCreatedHere = false;
    bool bInitializeAnimation = false;
    bool bAnimationPrepared = false;
    TSharedPtr<FJsonObject> Plan;
    TSharedPtr<FJsonObject> Source;
    TArray<TSharedPtr<FJsonValue>> ValuesNotHeld;
};
// Import bookkeeping keeps roots alive across collection. It owns no editor transaction.
struct FImportRun : FGCObject
{
    TMap<FString, FEntry> Entries;
    TSet<UObject*> Reused;
    TArray<FString> PriorStandIns;
    void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        for (auto& Pair : Entries) Collector.AddReferencedObject(Pair.Value.Asset);
    }
};
TUniquePtr<FImportRun> ImportRun;
TSet<TWeakObjectPtr<UObject>> FreshStandIns;

FString AssetPath(const TSharedPtr<FJsonObject>& Plan)
{
    const FString Name = Plan->GetStringField(TEXT("kind")) == TEXT("LevelSequence")
        ? Plan->GetObjectField(TEXT("sequence"))->GetStringField(TEXT("name")) : Plan->GetStringField(TEXT("name"));
    return Plan->GetStringField(TEXT("package")) + TEXT(".") + Name;
}
TArray<FString> Names(const TSharedPtr<FJsonObject>& Source, const TCHAR* Field)
{
    TArray<FString> Out;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (Source.IsValid() && Source->TryGetArrayField(Field, Values))
        for (const auto& Value : *Values) Out.AddUnique(Value->AsString());
    return Out;
}
TSharedRef<FJsonObject> SourceMembers(const TSharedPtr<FJsonObject>& Plan)
{
    auto Source = MakeShared<FJsonObject>();
    auto Collect = [&](const TCHAR* Field, const TCHAR* Name, const TSharedPtr<FJsonObject>& From)
    {
        TArray<TSharedPtr<FJsonValue>> Names;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (From.IsValid() && From->TryGetArrayField(Field, Values)) for (const auto& Value : *Values)
        {
            FString Text;
            if (Value->AsObject().IsValid() && Value->AsObject()->TryGetStringField(Name, Text))
                Names.Add(MakeShared<FJsonValueString>(Text));
        }
        Source->SetArrayField(Field, Names);
    };
    Collect(TEXT("variables"), TEXT("name"), Plan);
    Collect(TEXT("components"), TEXT("variable"), Plan);
    Collect(TEXT("graphs"), TEXT("name"), Plan);
    Source->SetBoolField(TEXT("has_animation"), Plan->HasTypedField<EJson::Object>(TEXT("anim")));
    const TSharedPtr<FJsonObject>* Defaults = nullptr;
    if (Plan->TryGetObjectField(TEXT("class_defaults"), Defaults)) Collect(TEXT("values"), TEXT("name"), *Defaults);
    else if (Plan->TryGetObjectField(TEXT("sequence"), Defaults)) Collect(TEXT("values"), TEXT("name"), *Defaults);
    return Source;
}
FEntry* EntryFor(UObject* Asset, FString& Why)
{
    FEntry* Entry = ImportRun && Asset ? ImportRun->Entries.Find(Asset->GetOutermost()->GetName()) : nullptr;
    const ULevelSequence* Sequence = Entry ? Cast<ULevelSequence>(Entry->Asset) : nullptr;
    if (!Entry || (Entry->Asset != Asset && (!Sequence || Sequence->GetDirectorBlueprint() != Asset)))
    { Why = TEXT("the asset is not selected for this import"); return nullptr; }
    if (ImportRun->Reused.Contains(Asset)) { Why = TEXT("the asset was already declared in this import"); return nullptr; }
    return Entry;
}
TSharedPtr<FJsonObject> PreviousSource(UObject* Asset)
{
    TSharedPtr<FJsonObject> Source;
    const FString Text = Asset->GetOutermost()->GetMetaData()->GetValue(Asset, SourceTag);
    if (!Text.IsEmpty()) FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Source);
    return Source;
}
void OriginalMarker(UObject* Asset, const TCHAR* Kind, bool ReadOnly)
{
    auto Record = MakeShared<FJsonObject>();
    Record->SetNumberField(TEXT("format"), 2);
    Record->SetStringField(TEXT("owner"), TEXT("p4mapkit-original"));
    Record->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
    Record->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Record->SetStringField(TEXT("kind"), Kind);
    Record->SetStringField(TEXT("source_digest"), TEXT(""));
    Record->SetStringField(TEXT("source_digest_kind"), TEXT("blueprint-import"));
    Record->SetStringField(TEXT("comparison_digest"), TEXT(""));
    Record->SetStringField(TEXT("comparison_schema"), TEXT(""));
    Record->SetStringField(TEXT("fidelity"), ReadOnly ? TEXT("stand-in") : TEXT("recovered"));
    Record->SetBoolField(TEXT("reference_only"), true);
    Record->SetBoolField(TEXT("read_only"), ReadOnly);
    FEntry* Entry = ImportRun ? ImportRun->Entries.Find(Asset->GetOutermost()->GetName()) : nullptr;
    Record->SetArrayField(TEXT("values"), Entry ? Entry->ValuesNotHeld : TArray<TSharedPtr<FJsonValue>>());
    FString Text;
    FJsonSerializer::Serialize(Record, TJsonWriterFactory<TCHAR>::Create(&Text));
    Asset->GetOutermost()->GetMetaData()->SetValue(Asset, TEXT("P4OriginalRecord"), *Text);
}
}

bool P4Reimport::PreflightPlan(const TSharedPtr<FJsonObject>& Plan, bool bReimport, FString& Why)
{
    if (!Plan.IsValid()) { Why = TEXT("no import plan"); return false; }
    const FString Package = Plan->GetStringField(TEXT("package"));
    if (!Package.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Package))
    { Why = TEXT("Blueprint imports require a valid /Game package"); return false; }
    if(!P4WidgetMembers::Preflight(Plan,nullptr,Why))return false;
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath(Plan), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if(Asset&&!P4WidgetMembers::Preflight(Plan,Asset,Why))return false;
    if (!Asset)
    {
        UPackage* Loaded = FindPackage(nullptr, *Package);
        TArray<UObject*> Objects;
        if (Loaded) GetObjectsWithOuter(Loaded, Objects, false);
        for (UObject* Object : Objects) if (Object && !Object->IsA<UMetaData>())
        { Why = TEXT("another asset occupies the destination package"); return false; }
        if (FPackageName::DoesPackageExist(Package)) { Why = TEXT("the package does not hold the expected asset"); return false; }
        return true;
    }
    const FString Kind = Plan->GetStringField(TEXT("kind"));
    if ((Kind == TEXT("LevelSequence") && !Asset->IsA<ULevelSequence>()) ||
        (Kind != TEXT("LevelSequence") && !Asset->IsA<UBlueprint>()) ||
        (Kind == TEXT("AnimBlueprint") && !Asset->IsA<UAnimBlueprint>()) ||
        (Kind == TEXT("WidgetBlueprint") && !Asset->IsA<UWidgetBlueprint>()) ||
        (Kind == TEXT("Blueprint") && (Asset->IsA<UAnimBlueprint>() || Asset->IsA<UWidgetBlueprint>())))
    { Why = TEXT("in-place reimport cannot change the asset kind"); return false; }
    if (UP4BlueprintImport::IsStandIn(Asset)) return IsUntouchedStandIn(Asset, Why);
    if (Asset->GetOutermost()->IsDirty()) { Why = TEXT("save your changes before reimporting this asset"); return false; }
    if (!bReimport) { Why = TEXT("the asset already exists; request an in-place reimport"); return false; }
    if (FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, ImportTag)) != ImportVersion)
    { Why = TEXT("the existing asset was not created by this Blueprint importer"); return false; }
    if (!P4AnimOwnership::Preflight(Cast<UBlueprint>(Asset), Plan, Why)) return false;
    return true;
}
bool P4Reimport::BeginRun(FString& Why)
{
    if (ImportRun || !GEditor || GEditor->IsTransactionActive())
    { Why = TEXT("finish the current editor operation before importing Blueprints"); return false; }
    ImportRun = MakeUnique<FImportRun>();
    ImportRun->PriorStandIns = UP4BlueprintImport::TakeStandIns();
    return true;
}
bool P4Reimport::IsRunActive() { return !!ImportRun; }
void P4Reimport::WithCompilerSavePolicy(const TFunction<void()>& Operation)
{
    UBlueprintEditorSettings* Settings = GetMutableDefault<UBlueprintEditorSettings>();
    TGuardValue<decltype(Settings->SaveOnCompile)> SavePolicy(Settings->SaveOnCompile, SoC_Never);
    Operation();
}
bool P4Reimport::BeginPlan(const TSharedPtr<FJsonObject>& Plan, bool bReimport, FString& Why)
{
    if (!ImportRun) { Why = TEXT("no active Blueprint import"); return false; }
    const FString Package = Plan->GetStringField(TEXT("package"));
    if (ImportRun->Entries.Contains(Package)) { Why = TEXT("a package is listed twice"); return false; }
    if (!PreflightPlan(Plan, bReimport, Why)) return false;
    FEntry Entry;
    Entry.Asset = LoadObject<UObject>(nullptr, *AssetPath(Plan), nullptr, LOAD_NoWarn | LOAD_Quiet);
    Entry.bInitializeAnimation = !Entry.Asset || UP4BlueprintImport::IsStandIn(Entry.Asset);
    Entry.Plan = Plan;
    Entry.Source = SourceMembers(Plan);
    ImportRun->Entries.Add(Package, Entry);
    return true;
}
bool P4Reimport::ReuseBlueprint(UBlueprint* Blueprint, UClass* Parent, FString& Why)
{
    FEntry* Entry = EntryFor(Blueprint, Why);
    if (!Entry) return false;
    if (Blueprint->ParentClass != Parent) { Why = TEXT("in-place reimport cannot change the parent class"); return false; }
    if (!Blueprint->GeneratedClass) { Why = TEXT("the existing Blueprint has no generated class"); return false; }
    // Remove admitted source animation objects before variable removal/compilation can change
    // their nodes or pins. The saved baseline was checked before any declaration mutation.
    if (Entry->Source->GetBoolField(TEXT("has_animation")))
    {
        if (!P4AnimOwnership::Reset(Blueprint, Entry->Plan, Why)) return false;
        Entry->bAnimationPrepared = true;
    }
    // Only source-named members are rebuilt. The Blueprint, generated-class scaffolding and
    // creator additions stay in place; ordinary compilation owns class/CDO reinstancing.
    auto Previous = PreviousSource(Blueprint);
    TArray<FString> Variables = Names(Entry->Source, TEXT("variables"));
    for (const FString& Name : Names(Previous, TEXT("variables"))) Variables.AddUnique(Name);
    for (const FString& Name : Variables)
    {
        // An event dispatcher is its variable and the graph that holds its signature, as the editor's
        // "add event dispatcher" makes them and as DeclareVariables makes them again. RemoveMemberVariable
        // takes only the variable, so the old signature graph stayed and the dispatcher was declared twice.
        const FName Member(*Name);
        for (UEdGraph* Signature : TArray<UEdGraph*>(Blueprint->DelegateSignatureGraphs))
            if (Signature && Signature->GetFName() == Member)
                FBlueprintEditorUtils::RemoveGraph(Blueprint, Signature, EGraphRemoveFlags::Recompile);
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Member);
    }
    if (Blueprint->SimpleConstructionScript)
    {
        const auto Components = Names(Entry->Source, TEXT("components"));
        for (const FString& Name : Names(Previous, TEXT("components"))) if (!Components.Contains(Name))
            if (USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(FName(*Name)))
                Blueprint->SimpleConstructionScript->RemoveNodeAndPromoteChildren(Node);
    }
    TArray<FString> GraphNames = Names(Entry->Source, TEXT("graphs"));
    for (const FString& Name : Names(Previous, TEXT("graphs"))) GraphNames.AddUnique(Name);
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph || !GraphNames.Contains(Graph->GetName())) continue;
        bool CreatorNodes = false;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Node && FString(Node->GetOutermost()->GetMetaData()->GetValue(Node, TEXT("P4BlueprintSourceNode"))) != TEXT("1"))
            {
                CreatorNodes = true;
                if (Previous.IsValid()) Node->GetOutermost()->GetMetaData()->SetValue(Node, TEXT("P4BlueprintCreatorNode"), TEXT("1"));
            }
        // Event declaration uses the existing ubergraph container. Removing it would make
        // Signatures/Graphs omit source custom events instead of rebuilding their nodes.
        if (!CreatorNodes && !Blueprint->UbergraphPages.Contains(Graph)) FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
        else for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
            if (Node && !Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_FunctionResult>() &&
                FString(Node->GetOutermost()->GetMetaData()->GetValue(Node, TEXT("P4BlueprintSourceNode"))) == TEXT("1")) Graph->RemoveNode(Node);
    }
    ImportRun->Reused.Add(Blueprint);
    return true;
}
void P4Reimport::ResetReimportSourceDefaults(UBlueprint* Blueprint)
{
    FEntry* Entry = ImportRun && Blueprint ? ImportRun->Entries.Find(Blueprint->GetOutermost()->GetName()) : nullptr;
    if (!Entry || Entry->Asset != Blueprint || !ImportRun->Reused.Contains(Blueprint) ||
        !Blueprint->GeneratedClass || !Blueprint->ParentClass) return;
    UClass* Parent = Blueprint->ParentClass;
    const auto Previous = PreviousSource(Blueprint);
    // Declaration is too early: selected parents have not received source Values.
    // Reset only source-named fields here; preserve saved non-source annotations.
    TArray<FString> Properties = Names(Previous, TEXT("values"));
    for (const FString& Name : Names(Entry->Source, TEXT("values"))) Properties.AddUnique(Name);
    UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
    UObject* ParentDefaults = Parent->GetDefaultObject();
    for (const FString& Name : Properties)
    {
        FProperty* Field = FindFProperty<FProperty>(Blueprint->GeneratedClass, FName(*Name));
        FProperty* ParentField = FindFProperty<FProperty>(Parent, FName(*Name));
        if (!Field || Field->HasAnyPropertyFlags(CPF_Transient)) continue;
        if (ParentField && Field->SameType(ParentField)) Field->CopyCompleteValue(Field->ContainerPtrToValuePtr<void>(Defaults), ParentField->ContainerPtrToValuePtr<void>(ParentDefaults));
        else Field->ClearValue(Field->ContainerPtrToValuePtr<void>(Defaults));
    }
}
bool P4Reimport::ReuseSequence(ULevelSequence* Sequence, FString& Why)
{
    if (!EntryFor(Sequence, Why)) return false;
    ImportRun->Reused.Add(Sequence);
    return true;
}
void P4Reimport::MarkImported(UObject* Asset)
{
    if (!Asset) return;
    if (ImportRun) if (FEntry* Entry = ImportRun->Entries.Find(Asset->GetOutermost()->GetName()))
        if (!Entry->Asset) { Entry->Asset = Asset; Entry->bCreatedHere = true; }
    Asset->GetOutermost()->GetMetaData()->SetValue(Asset, ImportTag, ImportVersion);
    SetReconstructionIncomplete(Asset, true);
    Asset->MarkPackageDirty();
}
bool P4Reimport::SetReconstructionIncomplete(UObject* Asset, bool Incomplete)
{
    if (!Asset||!Asset->IsA<UBlueprint>()||!ImportRun) return false;
    const FEntry* Entry=ImportRun->Entries.Find(Asset->GetOutermost()->GetName());
    if (!Entry||Entry->Asset!=Asset||(!Entry->bCreatedHere&&!ImportRun->Reused.Contains(Asset))) return false;
    UMetaData* Metadata=Asset->GetOutermost()->GetMetaData();
    if (Incomplete) Metadata->SetValue(Asset,TEXT("P4BlueprintReconstructionIncomplete"),TEXT("1"));
    else Metadata->RemoveValue(Asset,TEXT("P4BlueprintReconstructionIncomplete"));
    return true;
}
bool P4BlueprintReconstructionIncomplete(const UObject* Asset)
{
    if (const UClass* Class=Cast<UClass>(Asset)) Asset=Class->ClassGeneratedBy;
    if (!Asset||!Asset->IsA<UBlueprint>()) return false;
    TArray<UObject*> Objects; GetObjectsWithOuter(Asset->GetOutermost(),Objects,false);
    for (UObject* Object:Objects)
        if (UMetaData* Metadata=Cast<UMetaData>(Object))
            return FString(Metadata->GetValue(Asset,ImportTag))==ImportVersion
                &&FString(Metadata->GetValue(Asset,TEXT("P4BlueprintReconstructionIncomplete")))==TEXT("1");
    return false;
}
bool P4Reimport::IsFreshBlueprint(UBlueprint* Blueprint)
{
    const FEntry* Entry = ImportRun && Blueprint ? ImportRun->Entries.Find(Blueprint->GetOutermost()->GetName()) : nullptr;
    return Entry && Entry->Asset == Blueprint && Entry->bCreatedHere && !ImportRun->Reused.Contains(Blueprint);
}
bool P4Reimport::CanInitializeAnimationOwnership(UBlueprint* Blueprint)
{
    const FEntry* Entry = ImportRun && Blueprint ? ImportRun->Entries.Find(Blueprint->GetOutermost()->GetName()) : nullptr;
    return Entry && Entry->Asset == Blueprint && Entry->bInitializeAnimation;
}
bool P4Reimport::HasPreparedAnimation(UBlueprint* Blueprint)
{
    const FEntry* Entry = ImportRun && Blueprint ? ImportRun->Entries.Find(Blueprint->GetOutermost()->GetName()) : nullptr;
    return Entry && Entry->Asset == Blueprint && Entry->bAnimationPrepared;
}
void P4Reimport::RetainOriginalValue(UObject* Asset, const TSharedPtr<FJsonObject>& Value, const FString& GameAsset)
{
    FEntry* Entry = ImportRun && Asset ? ImportRun->Entries.Find(Asset->GetOutermost()->GetName()) : nullptr;
    if (!Entry || !Value.IsValid()) return;
    FString Text;
    FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<TCHAR>::Create(&Text));
    const FString Object = Asset->GetPathName(Asset->GetOutermost()), Property = Value->GetStringField(TEXT("name"));
    // One note per value: a value refused for two reasons is kept once.
    for (const TSharedPtr<FJsonValue>& Earlier : Entry->ValuesNotHeld)
    {
        const TSharedPtr<FJsonObject> Held = Earlier->AsObject();
        if (Held.IsValid() && Held->GetStringField(TEXT("object")) == Object && Held->GetStringField(TEXT("property")) == Property &&
            Held->GetStringField(TEXT("game_value")) == Text) return;
    }
    auto Note = MakeShared<FJsonObject>();
    Note->SetStringField(TEXT("kind"), GameAsset.IsEmpty() ? TEXT("unsupported-value") : TEXT("game-asset-reference"));
    Note->SetStringField(TEXT("object"), Object);
    Note->SetStringField(TEXT("property"), Property);
    Note->SetStringField(TEXT("game_value"), Text);
    Note->SetStringField(TEXT("action"), GameAsset.IsEmpty() ? FString(TEXT("retained-original; not applied in editor"))
        : TEXT("retained-original; ") + GameAsset + TEXT(" stays the game's and is not in the project"));
    Entry->ValuesNotHeld.Add(MakeShared<FJsonValueObject>(Note));
}
bool P4Reimport::IsRunPackage(const FString& PackageName)
{
    return ImportRun && ImportRun->Entries.Contains(PackageName);
}
void P4Reimport::RecordStandInCreated(UObject* Asset)
{
    if (!Asset) return;
    FreshStandIns.Add(Asset);
    OriginalMarker(Asset, TEXT("placeholder"), true);
}
bool P4Reimport::IsFreshStandIn(UObject* Asset) { return Asset && UP4BlueprintImport::IsStandIn(Asset) && FreshStandIns.Contains(Asset); }
bool P4Reimport::IsUntouchedStandIn(UObject* Asset, FString& Why)
{
    if (!Asset || !UP4BlueprintImport::IsStandIn(Asset)) { Why = TEXT("the asset is not a placeholder"); return false; }
    if (Asset->GetOutermost()->IsDirty() && !FreshStandIns.Contains(Asset))
    { Why = TEXT("the placeholder has unsaved changes"); return false; }
    return true;
}
bool P4Reimport::ConfigureStandIn(UObject* Asset, const TFunction<void()>& Configure, FString& Why)
{
    if (!IsFreshStandIn(Asset) || !Configure) { Why = TEXT("only a newly created placeholder may receive this initializer"); return false; }
    Configure();
    Asset->MarkPackageDirty();
    P4RestorePendingStandIns({Asset->GetOutermost()->GetName()});
    return true;
}
bool P4Reimport::PrepareStandInSave(const FString& Package, FString& Why)
{
    UPackage* Loaded = FindPackage(nullptr, *Package);
    if (!Loaded) { Why = TEXT("the placeholder package is not loaded"); return false; }
    return true;
}
void P4Reimport::CancelStandInSave(const FString&) {}
bool P4Reimport::FinishStandInPackage(const FString& Package, FString& Why)
{
    UPackage* Loaded = FindPackage(nullptr, *Package);
    if (!Loaded || Loaded->IsDirty()) { Why = TEXT("the placeholder was not saved"); return false; }
    for (auto It = FreshStandIns.CreateIterator(); It; ++It)
        if (!It->IsValid() || It->Get()->GetOutermost() == Loaded) It.RemoveCurrent();
    return true;
}
bool P4Reimport::PrepareImportedRecord(UObject* Asset, const FString&, FString& Why)
{
    FEntry* Entry = ImportRun && Asset ? ImportRun->Entries.Find(Asset->GetOutermost()->GetName()) : nullptr;
    if (!Entry || Entry->Asset != Asset) { Why = TEXT("the asset is not selected for this import"); return false; }
    // A source animation baseline is sealed after all selected assets and placeholders save.
    // Cold reconstruction changes persistent editor nodes and pins that a fresh compile keeps.
    if (Asset->IsA<UAnimBlueprint>() && Entry->Source->GetBoolField(TEXT("has_animation")))
        Asset->GetOutermost()->GetMetaData()->RemoveValue(Asset, TEXT("P4BlueprintAnimationSourceContent"));
    OriginalMarker(Asset, Asset->IsA<ULevelSequence>() ? TEXT("level-sequence") : TEXT("blueprint"), false);
    FString Text;
    FJsonSerializer::Serialize(Entry->Source.ToSharedRef(), TJsonWriterFactory<TCHAR>::Create(&Text));
    Asset->GetOutermost()->GetMetaData()->SetValue(Asset, SourceTag, *Text);
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        const TArray<FString> ImportedGraphs = Names(Entry->Source, TEXT("graphs"));
        for (UEdGraph* Graph : Graphs) if (Graph && ImportedGraphs.Contains(Graph->GetName()))
        {
            auto* Metadata = Graph->GetOutermost()->GetMetaData();
            Metadata->SetValue(Graph, TEXT("P4BlueprintSourceGraph"), TEXT("1"));
            for (UEdGraphNode* Node : Graph->Nodes) if (Node && FString(Metadata->GetValue(Node, TEXT("P4BlueprintCreatorNode"))) != TEXT("1"))
                Metadata->SetValue(Node, TEXT("P4BlueprintSourceNode"), TEXT("1"));
        }
    }
    Asset->MarkPackageDirty();
    return true;
}
bool P4Reimport::FinishPackage(const FString& Package, const FString&, FString& Why)
{
    FEntry* Entry = ImportRun ? ImportRun->Entries.Find(Package) : nullptr;
    if (!Entry || !Entry->Asset || Entry->Asset->GetOutermost()->IsDirty())
    { Why = TEXT("the imported asset was not saved"); return false; }
    FreshStandIns.Remove(Entry->Asset);
    return true;
}
bool P4Reimport::SealAnimationPackages(const TArray<FString>& Packages, FString& Why)
{
    struct FSeal { FString Package, Path, Expected; TArray<FString> Witness; bool Reused = false; TWeakObjectPtr<UAnimBlueprint> Previous; };
    TArray<FSeal> Seals;
    for (const FString& Package : Packages)
    {
        FEntry* Entry = ImportRun ? ImportRun->Entries.Find(Package) : nullptr;
        if (!Entry || !Entry->Asset) { Why = TEXT("animation-source-publication-not-selected: ") + Package; return false; }
        if (!Entry->Asset->IsA<UAnimBlueprint>() || !Entry->Source->GetBoolField(TEXT("has_animation"))) continue;
        if (!Entry->bCreatedHere && !ImportRun->Reused.Contains(Entry->Asset))
        { Why = TEXT("animation-source-publication-not-rebuilt: ") + Package; return false; }
        FSeal Seal; Seal.Package = Package; Seal.Path = Entry->Asset->GetPathName();
        Seals.Add(MoveTemp(Seal));
    }
    if (!Seals.Num()) return true;
    bool Sealed = false;
    ON_SCOPE_EXIT
    {
        if (!Sealed) for (const FSeal& Seal : Seals)
        {
            // Never leave a persisted successful seal after a failed publication check.
            auto Failed = FindObject<UAnimBlueprint>(nullptr, *Seal.Path);
            if (!Failed) { Why += TEXT("; animation-incomplete-record-not-resolved: ") + Seal.Package; continue; }
            ImportRun->Entries.FindChecked(Seal.Package).Asset = Failed;
            auto Metadata = Failed->GetOutermost()->GetMetaData();
            Metadata->RemoveValue(Failed, TEXT("P4BlueprintAnimationSourceContent"));
            Metadata->SetValue(Failed, TEXT("P4BlueprintReconstructionIncomplete"), TEXT("1"));
            Failed->MarkPackageDirty();
            P4ImportPublication::FSaveScope Publication(Failed->GetOutermost());
            const FString File = FPackageName::LongPackageNameToFilename(Seal.Package, FPackageName::GetAssetPackageExtension());
            if (!UPackage::SavePackage(Failed->GetOutermost(), Failed, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError))
                Why += TEXT("; animation-incomplete-record-not-saved: ") + Seal.Package;
        }
    };
    const auto Resolve = [&](const FSeal& Seal) -> UAnimBlueprint*
    {
        auto Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Seal.Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Blueprint || Blueprint->GetOutermost()->GetName() != Seal.Package || !Blueprint->GeneratedClass || Blueprint->Status == BS_Error ||
            Blueprint->GeneratedClass->ClassGeneratedBy != Blueprint || Blueprint->GeneratedClass->GetOutermost() != Blueprint->GetOutermost())
        { Why = TEXT("animation-source-publication-reloaded-class-unavailable: ") + Seal.Package; return nullptr; }
        ImportRun->Entries.FindChecked(Seal.Package).Asset = Blueprint;
        return Blueprint;
    };
    const auto Reload = [&]()
    {
        TArray<UPackage*> Batch;
        for (FSeal& Seal : Seals)
        {
            auto Blueprint = Resolve(Seal);
            if (!Blueprint) return false;
            if (Blueprint->GetOutermost()->IsDirty() || Blueprint->GetOutermost()->HasAnyPackageFlags(PKG_InMemoryOnly))
            { Why = TEXT("animation-source-publication-not-saved: ") + Seal.Package; return false; }
            Batch.Add(Blueprint->GetOutermost());
            Seal.Previous = Blueprint;
        }
        for (FSeal& Seal : Seals)
            Seal.Reused = ImportRun->Reused.Remove(ImportRun->Entries.FindChecked(Seal.Package).Asset) != 0;
        FText Error;
        const bool Reloaded = UPackageTools::ReloadPackages(Batch, Error, EReloadPackagesInteractionMode::AssumeNegative);
        // Reload remaps FGCObject roots. Re-resolve every identity and refresh the raw reuse set.
        bool Resolved = true;
        for (const FSeal& Seal : Seals)
        {
            auto Blueprint = Resolve(Seal);
            const bool Replaced = Blueprint && TWeakObjectPtr<UAnimBlueprint>(Blueprint) != Seal.Previous;
            Resolved &= Replaced;
            if (!Replaced && Why.IsEmpty()) Why = TEXT("animation-source-publication-identity-not-replaced: ") + Seal.Package;
            if (Blueprint && Seal.Reused) ImportRun->Reused.Add(Blueprint);
        }
        if (!Reloaded || !Error.IsEmpty()) { Why = TEXT("animation-source-publication-reload-failed: ") + Error.ToString(); return false; }
        return Resolved;
    };
    // A later selected parent reload can recompile an earlier child. Capture/compare only
    // after each complete finite batch returns, never after an individual package in it.
    if (!Reload()) return false;
    for (FSeal& Seal : Seals)
    {
        auto Blueprint = Resolve(Seal);
        if (!Blueprint || !P4AnimOwnership::CaptureBaseline(Blueprint, Why)) return false;
        Seal.Expected = Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4BlueprintAnimationSourceContent"));
        FString Unused;
        P4AnimOwnership::Fingerprint(Blueprint, Unused, &Seal.Witness);
    }
    for (const FSeal& Seal : Seals)
    {
        auto Blueprint = Resolve(Seal);
        if (!Blueprint) return false;
        if (!SetReconstructionIncomplete(Blueprint, false))
        { Why = TEXT("animation-source-publication-completion-not-owned: ") + Seal.Package; return false; }
        Blueprint->MarkPackageDirty();
        P4ImportPublication::FSaveScope Publication(Blueprint->GetOutermost());
        const FString File = FPackageName::LongPackageNameToFilename(Seal.Package, FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError))
        { Why = TEXT("animation-source-publication-baseline-not-saved: ") + Seal.Package; return false; }
    }
    if (!Reload()) return false;
    for (const FSeal& Seal : Seals)
    {
        auto Blueprint = Resolve(Seal); FString Actual;
        if (!Blueprint || !P4AnimOwnership::Fingerprint(Blueprint, Actual) || Actual != Seal.Expected ||
            FString(Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4BlueprintAnimationSourceContent"))) != Seal.Expected)
        {
            // Name the first part that reads differently after the reload.
            TArray<FString> After;
            FString Unused, First;
            if (Blueprint) P4AnimOwnership::Fingerprint(Blueprint, Unused, &After);
            for (int32 At = 0; At < FMath::Max(After.Num(), Seal.Witness.Num()) && First.IsEmpty(); ++At)
                if (!After.IsValidIndex(At) || !Seal.Witness.IsValidIndex(At) || After[At] != Seal.Witness[At])
                {
                    // Each side is shown from a little before the first character that differs.
                    const FString Now = After.IsValidIndex(At) ? After[At] : FString(TEXT("none"));
                    const FString Was = Seal.Witness.IsValidIndex(At) ? Seal.Witness[At] : FString(TEXT("none"));
                    int32 Same = 0;
                    while (Same < Now.Len() && Same < Was.Len() && Now[Same] == Was[Same])
                        ++Same;
                    const int32 From = FMath::Max(0, Same - 60);
                    First = FString::Printf(TEXT(" (part %d: %s, from character %d after reload %s, before %s)"), At,
                        At > 0 && Seal.Witness.IsValidIndex(At - 1) ? *Seal.Witness[At - 1].Left(200) : TEXT(""), From,
                        *Now.Mid(From, 300), *Was.Mid(From, 300));
                }
            if (Why.IsEmpty()) Why = TEXT("animation-source-content-not-cold-stable: ") + Seal.Package + First;
            return false;
        }
    }
    Sealed = true;
    return true;
}
bool P4Reimport::DiscardIncompleteCreatedAssets(FString& Why)
{
    if (!ImportRun) return true;
    for (auto& Pair : ImportRun->Entries)
    {
        FEntry& Entry = Pair.Value;
        UBlueprint* Blueprint = Cast<UBlueprint>(Entry.Asset);
        if (!Entry.bCreatedHere || !Blueprint || !P4BlueprintReconstructionIncomplete(Blueprint)) continue;
        UPackage* Package = Blueprint->GetOutermost();
        if (Package->GetName() != Pair.Key || !Pair.Key.StartsWith(TEXT("/Game/")))
        { Why = TEXT("failed-declaration-custody-changed: ") + Pair.Key; return false; }
        TArray<UObject*> Owned; GetObjectsWithOuter(Package, Owned, true);
        // Native Blueprint rename moves the class/CDO identities too. Keep the
        // objects reachable by live children; force-killing them breaks SuperStruct.
        const FString Name = TEXT("P4RefusedDeclaration_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        if (!Blueprint->Rename(*Name, GetTransientPackage(), REN_Test | REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
        { Why = TEXT("failed-declaration-could-not-vacate: ") + Pair.Key; return false; }
        FAssetRegistryModule::AssetDeleted(Blueprint);
        if (!Blueprint->Rename(*Name, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
        { FAssetRegistryModule::AssetCreated(Blueprint); Why = TEXT("failed-declaration-could-not-vacate: ") + Pair.Key; return false; }
        for (UObject* Object : Owned) if (!Object->IsA<UMetaData>()) Object->ClearFlags(RF_Public | RF_Standalone);
        Package->SetDirtyFlag(false);
        // bCreatedHere proves that this run began with no asset at this path.
        // A failed animation seal can have written its incomplete package already.
        FString Stem;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Pair.Key, Stem))
        { Why = TEXT("failed-declaration-invalid-package: ") + Pair.Key; return false; }
        for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
        {
            const FString File = Stem + Extension;
            if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true, true))
            { Why = TEXT("failed-declaration-file-not-removed: ") + File; return false; }
        }
    }
    return true;
}
void P4Reimport::EndRun()
{
    if (ImportRun) P4RestorePendingStandIns(ImportRun->PriorStandIns);
    ImportRun.Reset();
}

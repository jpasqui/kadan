#include "P4BlueprintSourceTemplates.h"
#include "P4BlueprintGraphs.h"
#include "P4OriginalImportSave.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Serialization/JsonReader.h"

namespace
{
const FName SourceKey(TEXT("P4OriginalInheritedTemplates"));

USCS_Node* SourceNode(UBlueprint* Blueprint, const FString& Variable, const FString& Owner)
{
    for (UClass* Class = Blueprint->ParentClass; Class; Class = Class->GetSuperClass())
    {
        UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(Class);
        if (!Generated || Generated->GetPathName() != Owner || !Generated->SimpleConstructionScript) continue;
        return Generated->SimpleConstructionScript->FindSCSNode(FName(*Variable));
    }
    return nullptr;
}
}

bool P4BlueprintSourceTemplateSaysNothing(const TSharedPtr<FJsonObject>& Record)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Subobjects = nullptr;
    return Record.IsValid() && (!Record->TryGetArrayField(TEXT("values"), Values) || !Values->Num()) &&
        (!Record->TryGetArrayField(TEXT("subobjects"), Subobjects) || !Subobjects->Num());
}

void P4RememberBlueprintSourceTemplates(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Plan)
{
    const TArray<TSharedPtr<FJsonValue>>* Overrides = nullptr;
    if (!Blueprint || !Plan.IsValid() || !Plan->TryGetArrayField(TEXT("overrides"), Overrides)) return;
    if (!Overrides->Num())
    {
        Blueprint->GetOutermost()->GetMetaData()->RemoveValue(Blueprint, SourceKey);
        return;
    }
    auto Stored = MakeShared<FJsonObject>();
    Stored->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
    Stored->SetArrayField(TEXT("overrides"), *Overrides);
    FString Text;
    if (!FJsonSerializer::Serialize(Stored, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))) return;
    Blueprint->GetOutermost()->GetMetaData()->SetValue(Blueprint, SourceKey, *Text);
}

void P4RestoreBlueprintSourceTemplates(UBlueprint* Blueprint, P4Graphs::FReport& Report)
{
    if (!IsValid(Blueprint) || !Blueprint->GeneratedClass || Blueprint->Status == BS_Error) return;
    const FString Text = Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, SourceKey);
    if (Text.IsEmpty()) return;
    TSharedPtr<FJsonObject> Stored;
    const TArray<TSharedPtr<FJsonValue>>* Overrides = nullptr;
    FString Identity;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Stored) || !Stored.IsValid() ||
        !Stored->TryGetStringField(TEXT("blueprint"), Identity) || Identity != Blueprint->GetPathName() ||
        !Stored->TryGetArrayField(TEXT("overrides"), Overrides))
    { Report.Problem(TEXT("source-template-record-identity-differs")); return; }
    P4ImportPublication::FImportScope Import;
    for (const auto& Item : *Overrides)
    {
        const TSharedPtr<FJsonObject> Record = Item.IsValid() ? Item->AsObject() : nullptr;
        FString Variable, Owner, Kind;
        if (!Record.IsValid() || !Record->TryGetStringField(TEXT("variable"), Variable) ||
            !Record->TryGetStringField(TEXT("owner"), Owner) || !Record->TryGetStringField(TEXT("cls"), Kind))
        { Report.Problem(TEXT("source-template-record-incomplete")); continue; }
        if (P4BlueprintSourceTemplateSaysNothing(Record)) continue;
        USCS_Node* Node = SourceNode(Blueprint, Variable, Owner);
        UInheritableComponentHandler* Handler = Node ? Blueprint->GetInheritableComponentHandler(true) : nullptr;
        if (!Handler) { Report.Problem(TEXT("source-template-owner-not-found: ") + Owner + TEXT(".") + Variable); continue; }
        const FComponentKey Key(Node);
        // An existing template is the creator's current state, never replaced here.
        if (Handler->GetOverridenComponentTemplate(Key)) continue;
        UActorComponent* Template = Handler->CreateOverridenComponentTemplate(Key);
        if (!Template || Template->GetClass()->GetPathName() != Kind)
        { Report.Problem(TEXT("source-template-class-differs: ") + Kind); continue; }
        P4ApplyBlueprintSourceTemplateValues(Template, Record, Report);
        Report.Count(TEXT("original inherited templates made"));
    }
}

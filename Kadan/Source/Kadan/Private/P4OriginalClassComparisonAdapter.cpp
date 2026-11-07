#include "P4OriginalClassComparison.h"
#include "P4Common.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

FString P4OriginalClassComparisonJson(const FP4OriginalClassComparisonResult& Result)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("valid"), Result.IsValid());
    Json->SetStringField(TEXT("error"), Result.ErrorDetail);
    Json->SetBoolField(TEXT("same"), Result.Same);
    Json->SetBoolField(TEXT("defaults_readable"), Result.DefaultsReadable);
    if (!Result.GameDefaultFactsDigest.IsEmpty() || !Result.DefaultFactsDigest.IsEmpty())
    {
        Json->SetStringField(TEXT("default_facts_digest_kind"), TEXT("class-default-facts-v1"));
        Json->SetStringField(TEXT("source_default_facts_sha256"), Result.GameDefaultFactsDigest);
        Json->SetStringField(TEXT("native_default_facts_sha256"), Result.DefaultFactsDigest);
    }
    if (Result.UnreadableDefaultNodes.Num())
    {
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for (const FString& Node : Result.UnreadableDefaultNodes)
            Nodes.Add(MakeShared<FJsonValueString>(Node));
        Json->SetArrayField(TEXT("unreadable_default_nodes"), Nodes);
    }
    if (Result.PackageKnown)
        Json->SetStringField(TEXT("package"), Result.Package);
    if (Result.ValuesKnown)
    {
        Json->SetNumberField(TEXT("values_this_engine_cannot_hold"), Result.ValuesNotHeld);
        TArray<TSharedPtr<FJsonValue>> Details;
        for (const auto& Value : Result.ValuesNotHeldDetails)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("object"), Value.Object);
            Row->SetStringField(TEXT("property"), Value.Property);
            Row->SetStringField(TEXT("kind"), Value.Kind);
            Row->SetStringField(TEXT("schema"), Value.Schema);
            Row->SetStringField(TEXT("reason"), Value.Reason);
            Row->SetStringField(TEXT("source_value"), Value.SourceValue);
            Details.Add(MakeShared<FJsonValueObject>(Row));
        }
        Json->SetArrayField(TEXT("values_not_held_details"), Details);
        Json->SetBoolField(
            TEXT("values_not_held_details_truncated"), Result.ValuesNotHeldDetails.Num() != Result.ValuesNotHeld);
    }
    if (Result.ValuesKnown)
    {
        Json->SetNumberField(TEXT("empty_compiler_frames_rebuilt"), Result.CompilerFramesRebuilt);
        Json->SetNumberField(TEXT("editor_empty_compiler_frames_rebuilt"), Result.EditorCompilerFramesRebuilt);
    }
    if (Result.TemplatesKnown)
        Json->SetNumberField(TEXT("empty_templates_of_inherited_components_not_compared"), Result.EmptyTemplates);
    if (Result.ComparedKnown)
    {
        Json->SetNumberField(TEXT("objects"), Result.Objects);
        Json->SetNumberField(TEXT("compared"), Result.Compared);
        Json->SetNumberField(TEXT("variable_ids_not_compared"), Result.VariableIds);
        Json->SetNumberField(TEXT("differences"), Result.Differences);
        TArray<TSharedPtr<FJsonValue>> OnlyGame, OnlyProject;
        for (const FString& Value : Result.OnlyGameSays)
            OnlyGame.Add(MakeShared<FJsonValueString>(Value));
        for (const FString& Value : Result.OnlyProjectSays)
            OnlyProject.Add(MakeShared<FJsonValueString>(Value));
        Json->SetArrayField(TEXT("only_the_game_says"), OnlyGame);
        Json->SetArrayField(TEXT("only_the_project_says"), OnlyProject);
    }
    FString Text = P4CondensedJson(Json);
    return Text;
}

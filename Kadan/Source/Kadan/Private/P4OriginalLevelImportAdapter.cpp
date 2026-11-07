#include "P4OriginalLevelImport.h"
#include "P4Common.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

FString P4OriginalLevelImportJson(const FP4OriginalLevelImportResult& Result)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("valid"), Result.IsValid());
    Json->SetStringField(TEXT("error"), Result.ErrorDetail);
    Json->SetBoolField(TEXT("created"), Result.Created);
    Json->SetBoolField(TEXT("saved"), Result.Saved);
    Json->SetBoolField(TEXT("game_backed"), Result.GameBacked);
    if (Result.GameBacked)
        Json->SetStringField(TEXT("game_backed_reason"), Result.GameBackedReason);
    if (Result.ActorCountsKnown)
    {
        Json->SetNumberField(TEXT("original_actor_count"), Result.OriginalActorCount);
        Json->SetNumberField(TEXT("editable_original_actor_count"), Result.EditableOriginalActorCount);
    }
    const auto Words = [&](const TCHAR* Key, const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Array;
        for (const FString& Value : Values)
            Array.Add(MakeShared<FJsonValueString>(Value));
        Json->SetArrayField(Key, Array);
    };
    if (Result.IdentityKnown)
    {
        Json->SetStringField(TEXT("package"), Result.Package);
        Json->SetStringField(TEXT("asset"), Result.AssetPath);
        Json->SetNumberField(TEXT("objects"), Result.Objects);
    }
    if (Result.GameBacked)
        Words(TEXT("uneditable_original_actors"), Result.UneditableOriginalActors);
    if (Result.ClassesKnown)
    {
        Json->SetNumberField(TEXT("classes_compared_with_the_game's"), Result.ClassesCompared);
        Words(TEXT("classes_that_differ_from_the_game's"), Result.DifferingClasses);
        Words(TEXT("classes_given_what_the_game's_files_say_while_the_level_was_made"), Result.LentClasses);
        Words(TEXT("classes_that_could_not_be_given_it"), Result.ClassProblems);
    }
    if (Result.BuildDetailsKnown)
    {
        Words(TEXT("references_to_what_the_game_does_not_ship"), Result.UnshippedReferences);
        Words(TEXT("made_by_a_class_of_the_project_and_not_in_the_game's_file"), Result.ProjectOnlyObjects);
    }
    if (Result.ValuesKnown)
        Words(TEXT("values_this_engine_cannot_hold"), Result.ValuesNotHeld);
    if (Result.TemplatesKnown)
        Json->SetNumberField(
            TEXT("made_from_a_parent's_template_that_says_the_same"), Result.EquivalentParentTemplates);
    if (Result.DifferencesKnown)
    {
        Json->SetNumberField(TEXT("differences"), Result.Differences);
        Words(TEXT("only_the_game_says"), Result.OnlyGameSays);
        Words(TEXT("only_the_level_made_says"), Result.OnlyLevelSays);
    }
    if (Result.ComparedKnown)
    {
        Words(TEXT("engine_made_and_not_compared"), Result.EngineMadeNotCompared);
        Json->SetNumberField(TEXT("compared"), Result.Compared);
    }
    if (Result.ConstructionKnown)
    {
        Json->SetNumberField(TEXT("not_kept_by_a_construction_script_run"), Result.NotKeptCount);
        Words(TEXT("not_kept"), Result.NotKept);
        Json->SetNumberField(TEXT("added_by_a_construction_script_run"), Result.AddedCount);
        Words(TEXT("added"), Result.Added);
    }
    if (Result.PlacementKnown)
    {
        Json->SetNumberField(TEXT("gameplay_objects_with_a_row"), Result.PlacementRows);
        Json->SetNumberField(TEXT("values_given_from_the_rows"), Result.PlacementValues);
        Words(TEXT("gameplay_objects_without_a_row"), Result.PlacementWithoutRow);
        Words(TEXT("placement_blobs_not_read_yet"), Result.PlacementNotRead);
    }
    if (Result.PlacementNotHeld.Num())
        Words(TEXT("placement_values_not_given"), Result.PlacementNotHeld);
    if (Result.BaselineKnown)
    {
        Json->SetStringField(TEXT("source_sha256"), Result.SourceDigest);
        Json->SetStringField(TEXT("imported_state"), Result.ImportedState);
        Json->SetStringField(TEXT("state"), Result.State);
    }
    FString Text = P4CondensedJson(Json);
    return Text;
}

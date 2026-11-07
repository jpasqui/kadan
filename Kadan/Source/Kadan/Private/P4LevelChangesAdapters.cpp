#include "P4LevelChanges.h"
#include "P4Common.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FString& Value : Values)
        Out.Add(MakeShared<FJsonValueString>(Value));
    return Out;
}
TSharedRef<FJsonObject> Lines(const FP4LevelLineChanges& Values)
{
    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetArrayField(TEXT("no_longer_says"), Strings(Values.Before));
    Out->SetArrayField(TEXT("now_says"), Strings(Values.After));
    return Out;
}
TArray<TSharedPtr<FJsonValue>> Numbers(float A, float B, float C)
{
    return {MakeShared<FJsonValueNumber>(A), MakeShared<FJsonValueNumber>(B), MakeShared<FJsonValueNumber>(C)};
}
FString ErrorName(const FP4LevelChangesResult& R)
{
    switch (R.Error)
    {
        case EP4LevelChangesError::None:
            return FString();
        case EP4LevelChangesError::EditorThreadRequired:
            return TEXT("level-changes-require-editor-thread");
        case EP4LevelChangesError::LevelRequired:
            return TEXT("level-changes-need-a-level");
        case EP4LevelChangesError::ImportedLevelRequired:
            return TEXT("level-changes-need-a-level-imported-from-the-game");
        case EP4LevelChangesError::ImportedComparisonRequired:
            return TEXT("level-changes-need-what-the-level-said-when-imported: import the level again");
        case EP4LevelChangesError::CookFailed:
            return R.ErrorDetail;
        case EP4LevelChangesError::StateUnreadable:
            return TEXT("level-changes-state-unreadable");
    }
    return TEXT("level-changes-unknown-error");
}
} //namespace

FString P4LevelChangesErrorText(const FP4LevelChangesResult& R)
{
    return ErrorName(R);
}

FString P4LevelChangesJson(const FP4LevelChangesResult& R)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("valid"), R.IsValid());
    Json->SetStringField(TEXT("error"), ErrorName(R));
    if (!R.ErrorDetail.IsEmpty())
        Json->SetStringField(TEXT("error_detail"), R.ErrorDetail);
    if (R.IsValid())
    {
        Json->SetStringField(TEXT("package"), R.Package);
        Json->SetStringField(TEXT("level"), R.Level);
        Json->SetStringField(TEXT("state"), R.State);
        Json->SetStringField(TEXT("comparison_schema"), R.ComparisonSchema);
        Json->SetBoolField(TEXT("unchanged"), R.Unchanged);
        Json->SetBoolField(TEXT("edited"), !R.Unchanged);
        Json->SetBoolField(TEXT("held_by_level_changes"), R.HeldByLevelChanges);
        Json->SetArrayField(TEXT("removed"), Strings(R.Removed));
        Json->SetArrayField(TEXT("added"), Strings(R.Added));
        Json->SetArrayField(TEXT("navigation_data_the_editor_made"), Strings(R.EditorNavigation));
        TArray<TSharedPtr<FJsonValue>> Moved, Other;
        for (const auto& Move : R.Moved)
        {
            TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
            One->SetStringField(TEXT("actor"), Move.Actor);
            One->SetArrayField(TEXT("location"), Numbers(Move.Location.X, Move.Location.Y, Move.Location.Z));
            One->SetArrayField(TEXT("rotation"), Numbers(Move.Rotation.Pitch, Move.Rotation.Yaw, Move.Rotation.Roll));
            One->SetArrayField(TEXT("scale"), Numbers(Move.Scale.X, Move.Scale.Y, Move.Scale.Z));
            Moved.Add(MakeShared<FJsonValueObject>(One));
        }
        for (const auto& Change : R.OtherChanged)
        {
            TSharedRef<FJsonObject> One = Lines(Change.Lines);
            One->SetStringField(TEXT("actor"), Change.Actor);
            Other.Add(MakeShared<FJsonValueObject>(One));
        }
        Json->SetArrayField(TEXT("moved"), Moved);
        Json->SetArrayField(TEXT("changed_otherwise"), Other);
        Json->SetObjectField(TEXT("the_level_itself"), Lines(R.LevelChanges));
        TSharedRef<FJsonObject> NotBuilt = MakeShared<FJsonObject>();
        NotBuilt->SetArrayField(TEXT("navigation_takes_into_account"), Strings(R.NavigationRelevant));
        NotBuilt->SetArrayField(TEXT("lit_by_the_game's_lighting_build"), Strings(R.LitByGame));
        NotBuilt->SetArrayField(TEXT("added_without_a_lighting_build"), Strings(R.AddedUnlit));
        Json->SetObjectField(TEXT("not_built_again"), NotBuilt);
        if (!R.WorldSettings.IsEmpty())
            Json->SetStringField(TEXT("world_settings"), R.WorldSettings);
    }
    FString Text = P4CondensedJson(Json);
    return Text;
}

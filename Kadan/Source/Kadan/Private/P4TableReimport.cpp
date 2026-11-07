#include "P4AuthoringRows.h"
#include "UObject/StrongObjectPtr.h"

FString UP4AuthoringTools::NativeMergeTableRows(UDataTable* Current, UDataTable* Previous, UDataTable* Source)
{
    // Source and Previous must already have passed complete source/schema/readback
    // admission. This stage only plans rows; the import job publishes them later.
    if (!IsInGameThread()) return TEXT("{\"error\":\"Table reimport planning requires the editor thread\"}");
    if (!IsValid(Source) || !IsValid(Previous) || !Source->RowStruct ||
        !Source->RowStruct->IsChildOf(FTableRowBase::StaticStruct()) ||
        Previous->RowStruct != Source->RowStruct ||
        (Current && (!IsValid(Current) || Current->RowStruct != Source->RowStruct)))
        return TEXT("{\"error\":\"Table reimport inputs have missing or different native row schemas\"}");
    const FString SourceJson = NativeTableRowsJson(Source);
    const FString PreviousJson = NativeTableRowsJson(Previous);
    const FString CurrentJson = Current ? NativeTableRowsJson(Current) : TEXT("[]");
    if (SourceJson.IsEmpty() || PreviousJson.IsEmpty() || CurrentJson.IsEmpty())
        return TEXT("{\"error\":\"Table reimport input needs an exact native value adapter\"}");
    const int64 Bytes = (int64(SourceJson.Len()) + PreviousJson.Len() + CurrentJson.Len()) * sizeof(TCHAR);
    if (Bytes > 64 * 1024 * 1024)
        return TEXT("{\"error\":\"Table reimport exceeds the staging text budget\"}");

    TStrongObjectPtr<UDataTable> Merged(NewNativeTable(Source->RowStruct));
    TStrongObjectPtr<UDataTable> Baseline(NewNativeTable(Source->RowStruct));
    if ((Current && Merged->CreateTableFromOtherTable(Current).Num() != 0) ||
        Baseline->CreateTableFromOtherTable(Previous).Num() != 0)
        return TEXT("{\"error\":\"Could not stage native reimport row copies\"}");
    // Only new rows are merged. A row imported before that the game's file now changes or drops
    // has no rule yet for whose value wins, so the plan is refused rather than the difference lost.
    for (const auto& Row : Previous->GetRowMap())
    {
        uint8* const* Now = Source->GetRowMap().Find(Row.Key);
        if (!Now || !Source->RowStruct->CompareScriptStruct(Row.Value, *Now, PPF_None))
            return TEXT("{\"error\":\"The game's table ") + FString(Now ? TEXT("changes") : TEXT("drops")) +
                TEXT(" a row that was imported before, which a reimport cannot merge yet: ") +
                Row.Key.ToString().Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"}");
    }
    int32 Added = 0;
    for (const auto& Row : Source->GetRowMap())
    {
        // Native FName equality, including numbered names, defines row identity.
        // A row in Previous but absent from Current is an intentional deletion.
        if (Previous->GetRowMap().Contains(Row.Key)) continue;
        Baseline->AddRow(Row.Key, *reinterpret_cast<const FTableRowBase*>(Row.Value));
        if (!Current || !Current->GetRowMap().Contains(Row.Key))
        {
            Merged->AddRow(Row.Key, *reinterpret_cast<const FTableRowBase*>(Row.Value));
            ++Added;
        }
    }
    const FString Rows = NativeTableRowsJson(Merged.Get());
    const FString Before = NativeTableRowsJson(Baseline.Get());
    if (Rows.IsEmpty() || Before.IsEmpty())
        return TEXT("{\"error\":\"Native reimport row copies could not be read back exactly\"}");
    return TEXT("{\"rows\":") + Rows + TEXT(",\"baseline\":") + Before +
        FString::Printf(TEXT(",\"added\":%d}"), Added);
}
